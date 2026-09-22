/* Thaís OS — linker script para BOOTX64.EFI (UEFI PE32+) */
ENTRY(efi_main)
SECTIONS {
    . = 0x1000;
    .text : {
        *(.text .text.*)
    }
    .rodata : {
        *(.rodata .rodata.*)
    }
    .data : {
        *(.data .data.*)
    }
    .bss (NOLOAD) : {
        *(COMMON)
        *(.bss .bss.*)
    }
    /DISCARD/ : {
        *(.comment)
        *(.note*)
    }
}
