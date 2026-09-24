/* s32k396_main.c — reset entry, interrupt binding and the background loop (S32K396, lockstep
 * Cortex-M7 pair). Order follows §9 step 1: clocks, time base, every output at its safe level with
 * ASC_CLR_N high before any output buffer is enabled, then the application (which locks the
 * eFlexPWM fault configuration before anything can modulate).
 *
 * Interrupts (NVIC priority, 0 = highest):
 *   0  eFlexPWM_1 fault (FFLAG)        -> app_fault_isr_entry()   FW-15 step 1, FW-06, FW-05
 *   1  eDMA major loop, SDADC2 (SIN)    -> s32k_sdadc_block_irq()  resolver block time stamp
 *   2  BCTU end of list (phase currents)-> app_isr_current()       2 x f_sw
 *   4  STM_0 channel 0 (1 ms)           -> app_task_1ms()
 *   5  FlexCAN0/1                        -> RTD ISR -> s32k_can_callback()
 * app_idle() runs in the background loop (NVM queue only). */
#include "app.h"
#include "s32k396.h"
#include "ti_params.h"

#ifdef TI_RTD_AVAILABLE
#include "Clock_Ip.h" /* TODO(RTD) */
#include "Fccu_Ip.h"
#include "IntCtrl_Ip.h"
#include "Siul2_Port_Ip.h"
extern const Clock_Ip_ClockConfigType Clock_Ip_aClockConfig[];        /* TODO(RTD): Config Tools */
extern const IntCtrl_Ip_CtrlConfigType IntCtrlConfig_0;               /* TODO(RTD) */
extern const Siul2_Port_Ip_PinSettingsConfig g_pin_mux_InitConfigArr0[]; /* TODO(RTD) */
extern const Fccu_Ip_ConfigType Fccu_Ip_Config;                       /* TODO(RTD) */
#define TI_PIN_COUNT NUM_OF_CONFIGURED_PINS0                          /* TODO(RTD) */
/* TODO(RTD): IRQ numbers of the S32K39 device header */
#define TI_IRQ_PWM1_FAULT PWM1_FAULT_IRQn
#define TI_IRQ_BCTU BCTU_IRQn
#define TI_IRQ_SDADC_DMA DMATCD_SDADC2_IRQn
#define TI_IRQ_STM0 STM0_IRQn
#endif

#ifndef TI_RTD_AVAILABLE
ti_shadow_t g_ti_shadow; /* register shadow of the host syntax-check build */
#endif

/* ---------------- interrupt handlers ---------------- */
static void isr_pwm_fault(void) { app_fault_isr_entry(); /* FFLAG stays set until FW-15 step 4 */ }

static void isr_current(void)
{
    app_isr_current(&g_app);
#ifdef TI_RTD_AVAILABLE
    /* TODO(RTD): clear the BCTU list-complete flag (Bctu_Ip_ClearStatusFlag or the ISR wrapper) */
#endif
}

static void isr_task(void)
{
#ifdef TI_RTD_AVAILABLE
    /* TODO(RTD): STM channel 0 flag clear and CMP += 1000 (the Stm_Ip ISR wrapper does both) */
#endif
    app_task_1ms(&g_app);
}

/* 64-bit device UID as the calibration serial (FW-20 binds the record to it). */
static void read_uid(uint8_t serial[8])
{
    for (uint32_t i = 0u; i < 8u; i++) {
        serial[i] = 0u; /* TODO(RTD/HW-RM): the S32K39 unique ID (UTEST / DCM); 0 fails calib_check */
    }
}

static void bind_interrupts(void)
{
#ifdef TI_RTD_AVAILABLE
    IntCtrl_Ip_Init(&IntCtrlConfig_0);
    IntCtrl_Ip_InstallHandler(TI_IRQ_PWM1_FAULT, isr_pwm_fault, NULL_PTR);
    IntCtrl_Ip_InstallHandler(TI_IRQ_SDADC_DMA, s32k_sdadc_block_irq, NULL_PTR);
    IntCtrl_Ip_InstallHandler(TI_IRQ_BCTU, isr_current, NULL_PTR);
    IntCtrl_Ip_InstallHandler(TI_IRQ_STM0, isr_task, NULL_PTR);
    IntCtrl_Ip_SetPriority(TI_IRQ_PWM1_FAULT, TI_PRIO_PWM_FAULT);
    IntCtrl_Ip_SetPriority(TI_IRQ_SDADC_DMA, TI_PRIO_SDADC);
    IntCtrl_Ip_SetPriority(TI_IRQ_BCTU, TI_PRIO_CURRENT);
    IntCtrl_Ip_SetPriority(TI_IRQ_STM0, TI_PRIO_TASK);
    /* FlexCAN IRQs: priority TI_PRIO_CAN in IntCtrlConfig_0 */
    IntCtrl_Ip_EnableIrq(TI_IRQ_PWM1_FAULT);
    IntCtrl_Ip_EnableIrq(TI_IRQ_SDADC_DMA);
    IntCtrl_Ip_EnableIrq(TI_IRQ_BCTU);
    IntCtrl_Ip_EnableIrq(TI_IRQ_STM0);
#else
    (void)isr_pwm_fault;
    (void)isr_current;
    (void)isr_task;
#endif
}

int main(void)
{
#ifdef TI_RTD_AVAILABLE
    /* TODO(RTD): Clock_Ip_Init(&Clock_Ip_aClockConfig[0]) — PLL, core, AIPS, eFlexPWM (TI_PWM_CLK_HZ),
     * SDADC and STM clocks. Fccu_Ip_Init(): lockstep (DCM) and ECC faults to FCCU_ERR0/1 (the FS26
     * ERRMON input). Siul2_Port_Ip_Init(): alternate functions only; GPIO outputs are enabled by
     * s32k_gpio_init() after their latches are written. */
    (void)Clock_Ip_Init(&Clock_Ip_aClockConfig[0]);
    (void)Siul2_Port_Ip_Init(TI_PIN_COUNT, g_pin_mux_InitConfigArr0);
    (void)Fccu_Ip_Init(&Fccu_Ip_Config);
#endif
    (void)s32k_timer_init();
    (void)s32k_gpio_init();
    uint8_t serial[8];
    read_uid(serial);
    app_init(&g_app, ti_params_active(), NULL, serial); /* NULL: calibration from NVM (FW-20) */
    bind_interrupts();
    for (;;) {
        app_idle(&g_app); /* NVM queue; everything else is interrupt driven */
    }
}
