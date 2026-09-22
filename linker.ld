/* Thais OS - linker script x86_64, HHDM ThaisBoot */
ENTRY(_start)
SECTIONS {
    . = 0xffffffff80000000;

    .text : {
        *(.text .text.*)
    }
    .rodata : {
        *(.rodata .rodata.*)
    }
    .data : {
        *(.data .data.*)
    }
    .bss : {
        *(COMMON)
        *(.bss .bss.*)
    }
}
