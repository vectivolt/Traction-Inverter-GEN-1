/* qemu/mps2-an500.ld — the test image on QEMU's mps2-an500 (make qemu-test). RAM per QEMU 11.1 `info mtree`:
 * 0x00000000 ssram1 4 MiB (the vector table is fetched here at reset), 0x20000000 ssram23 4 MiB (unused),
 * 0x60000000 16 MiB. Everything is RAM to QEMU; the split mirrors the target's flash / SRAM. */
MEMORY
{
    ROM (rx)  : ORIGIN = 0x00000000, LENGTH = 4M  /* vectors, code, constants, the .data image */
    RAM (rwx) : ORIGIN = 0x60000000, LENGTH = 16M /* .data, .bss, the heap up from `end`, the stack down from the top */
}

ENTRY(Reset_Handler)

SECTIONS
{
    .text :
    {
        KEEP(*(.isr_vector))
        *(.text .text.*)
        *(.rodata .rodata.*)
        . = ALIGN(4);
        __init_array_start = .; /* newlib's __libc_init_array / __libc_fini_array */
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        __init_array_end = .;
        __fini_array_start = .;
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array))
        __fini_array_end = .;
        KEEP(*(.init))
        KEEP(*(.fini))
        . = ALIGN(8);
    } > ROM

    .ARM.exidx :
    {
        __exidx_start = .;
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
        __exidx_end = .;
    } > ROM

    .data : ALIGN(8)
    {
        _sdata = .;
        *(.data .data.*)
        . = ALIGN(8);
        _edata = .;
    } > RAM AT > ROM
    _sidata = LOADADDR(.data);

    .bss (NOLOAD) : ALIGN(8)
    {
        _sbss = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(8);
        _ebss = .;
    } > RAM

    end = .; /* librdimon's _sbrk: the heap starts here and stops at the stack pointer */
    __StackTop = ORIGIN(RAM) + LENGTH(RAM);
}
