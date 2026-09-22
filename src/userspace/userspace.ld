/* userspace.ld - linker script dos apps userspace do Thais OS (MARCO 1+).
   Linka o app em 0x200000000000 (user space, L4 idx 64): o kernel NAO mapeia
   esse indice (isolamento real); exec_elf mapeia os PT_LOAD no MESMO vaddr.
   A separacao texto/rodata (RX) x data/bss (RW) gera 2 segmentos PT_LOAD
   com flags distintas -> exercita segmentos multimapa no loader (W^X). */
ENTRY(_start)

SECTIONS
{
  . = 0x200000000000;

  .text : {
    *(.text*)
  }

  .rodata : {
    *(.rodata* .rodata.*)
    *(.data.rel.ro*)
  }

  . = ALIGN(0x1000);

  .data : {
    *(.data*)
  }

  .bss : {
    *(.bss*)
    *(COMMON)
  }
}