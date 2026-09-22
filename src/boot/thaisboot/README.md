# ThaisBoot — bootloader 100% inédito (futuro)

Stub para substituir Limine. Objetivo: UEFI + BIOS híbrido, escrito do zero em C/ASM, sem copiar código.

Fase 1 (atual): usa Limine 7.x como temporário.
Fase 2: ThaisBoot implementa:
- PE32+ para UEFI (entra via efi_main)
- MBR + stage2 para BIOS
- Carrega kernel.elf via FAT32 ESP
- Passa framebuffer GOP, memmap, rsdp via struct thais_boot_info (não limine)

Esqueleto já previsto em `src/boot/thaisboot/main.c` (a criar quando toolchain UEFI estiver pronta).
