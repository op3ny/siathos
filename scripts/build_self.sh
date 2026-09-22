#!/bin/bash
set -e
cd "$(cd "$(dirname "$0")/.." && pwd)"
gcc -c -m64 -mno-red-zone -fPIC -fno-plt -fno-stack-protector -ffreestanding \
    -o build/self.o src/boot/thaisboot/test_self.c
ld -m i386pep --subsystem 10 --image-base 0x0 -T src/boot/thaisboot/thaisboot.lds \
    -e efi_main -shared --unresolved-symbols=ignore-all build/self.o -o build/self.so
objcopy --target=efi-app-x86_64 --strip-debug \
    --remove-section .comment --remove-section .eh_frame --remove-section .edata \
    --remove-section .note --remove-section .note.gnu.property \
    build/self.so build/BOOTX64.EFI
echo 'SUDO_PASS' | sudo -S bash scripts/build_disk.sh
timeout 12 qemu-system-x86_64 -drive if=ide,media=disk,format=raw,file=build/disk.img \
    -bios /usr/share/ovmf/OVMF.fd -m 512M -nographic -no-reboot \
    -chardev file,path=build/ovmfdbg.log,id=dbc -device isa-debugcon,chardev=dbc,iobase=0x402 \
    > build/serial.log 2>&1
echo captured
