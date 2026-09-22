OUTPUT_FORMAT("pei-x86-64")
ENTRY(efi_main)
SECTIONS
{
  . = 0x1000;
  .text : { *(.text .text.*) }
  . = ALIGN(0x200);
  .rdata : { *(.rdata .rodata .rodata.*) }
  . = ALIGN(0x200);
  .data : { *(.data .data.*) }
  . = ALIGN(0x200);
  .bss : { *(.bss .bss.*) }
}
