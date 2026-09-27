/* qemu/startup.c — start-up of the host test suite built as a bare-metal Cortex-M7 image for QEMU's
 * mps2-an500 (make qemu-test): the vector table, the reset (FPU on, .data and .bss, semihosted stdio), a fault
 * report that exits at once instead of hanging until the timeout, and system(). Not part of the firmware. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define REG(a) (*(volatile uint32_t *)(a))

extern uint32_t _sidata[], _sdata[], _edata[], _sbss[], _ebss[], __StackTop[]; /* qemu/mps2-an500.ld */
extern void initialise_monitor_handles(void); /* librdimon: opens the semihosted stdin/stdout/stderr */
extern void __libc_init_array(void);          /* newlib: preinit/init arrays and _init, as crt0 would */
int main(void);
void Reset_Handler(void);
void Fault_Handler(void);
void fault_report(const uint32_t *frame);
int _system(const char *cmd); /* librdimon: semihosting SYS_SYSTEM */
void _init(void);
void _fini(void);

__attribute__((used, section(".isr_vector"))) static void (*const s_vectors[16])(void) = {
    (void (*)(void))(uintptr_t)__StackTop, Reset_Handler,
    Fault_Handler, Fault_Handler, Fault_Handler, Fault_Handler, Fault_Handler, /* NMI, Hard, MemManage, Bus, Usage */
    NULL, NULL, NULL, NULL,
    Fault_Handler, Fault_Handler, NULL, Fault_Handler, Fault_Handler, /* SVCall, DebugMon, -, PendSV, SysTick */
};

void _init(void) {} /* crti/crtn are not linked (-nostartfiles): nothing to add around the arrays */
void _fini(void) {}

/* This newlib is built NO_EXEC: its system() returns 0 for NULL and -1 otherwise. librdimon's SYS_SYSTEM runs the
 * command in the host shell, in QEMU's working directory (make's): test_capture's node decoder round trip. */
int system(const char *cmd) { return _system(cmd); }

void Reset_Handler(void)
{
    REG(0xE000ED88u) |= 0xFu << 20; /* CPACR: CP10/CP11 full access, before the first floating-point instruction */
    __asm volatile("dsb\n\tisb" ::: "memory");
    for (uint32_t i = 0u; &_sdata[i] < _edata; i++) {
        _sdata[i] = _sidata[i];
    }
    for (uint32_t i = 0u; &_sbss[i] < _ebss; i++) {
        _sbss[i] = 0u;
    }
    initialise_monitor_handles();
    __libc_init_array();
    (void)setvbuf(stdout, NULL, _IONBF, 0); /* nothing buffered is lost at a fault or a timeout */
    exit(main());
}

/* Thread mode runs on MSP, but take the frame from whichever stack the exception used. */
__attribute__((naked)) void Fault_Handler(void)
{
    __asm volatile("tst lr, #4\n\tite eq\n\tmrseq r0, msp\n\tmrsne r0, psp\n\tb fault_report");
}

void fault_report(const uint32_t *frame)
{
    printf("\nqemu: exception %lu, CFSR 0x%08lx HFSR 0x%08lx BFAR 0x%08lx, pc 0x%08lx lr 0x%08lx\n",
           (unsigned long)(REG(0xE000ED04u) & 0x1FFu), (unsigned long)REG(0xE000ED28u), (unsigned long)REG(0xE000ED2Cu),
           (unsigned long)REG(0xE000ED38u), (unsigned long)frame[6], (unsigned long)frame[5]);
    _Exit(3);
}
