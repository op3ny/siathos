#!/bin/bash
set -e
cd "$(cd "$(dirname "$0")/.." && pwd)"
EFI_INC=/usr/include/efi
CFLAGS="-I $EFI_INC -I $EFI_INC/x86_64 -fno-stack-protector -fno-builtin -fshort-wchar -fPIC -fno-plt -mno-red-zone -maccumulate-outgoing-args -m64 -DEFI_FUNCTION_WRAPPER"
gcc $CFLAGS -c src/boot/thaisboot/tinymain.c -o build/tiny.o
ld -T /usr/lib/elf_x86_64_efi.lds -shared -Bsymbolic \
   /usr/lib/crt0-efi-x86_64.o build/tiny.o -L/usr/lib -lefi -lgnuefi \
   -o build/tiny.so
objcopy -j .text -j .sdata -j .data -j .dynamic -j .dynsym -j .rel -j .rela -j .reloc \
   --target=efi-app-x86_64 --strip-debug build/tiny.so build/BOOTX64.EFI
echo 'SUDO_PASS' | sudo -S bash scripts/build_disk.sh
timeout 12 qemu-system-x86_64 -drive if=ide,media=disk,format=raw,file=build/disk.img \
    -bios /usr/share/ovmf/OVMF.fd -m 512M -nographic -no-reboot \
    > build/serial.log 2>&1
echo captured
