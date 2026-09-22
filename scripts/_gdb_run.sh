#!/bin/bash
cd "$(cd "$(dirname "$0")/.." && pwd)" || exit 1
mkdir -p build/esp/EFI/BOOT
cp -f build/kernel.elf build/esp/kernel.elf
cp -f build/BOOTX64.EFI build/esp/EFI/BOOT/BOOTX64.EFI
qemu-system-x86_64 -drive format=raw,file=fat:rw:build/esp \
    -bios /usr/share/ovmf/OVMF.fd -m 512M -serial file:/tmp/xt.log \
    -display none -no-reboot -s -S 2>/dev/null &
qpid=$!
sleep 3
timeout 45 gdb -q -batch -x scripts/_gdb_hunt.gdb 2>&1
echo "=== serial ==="
grep -E 'pk|sw|gc|exit|EXCECAO|PANIC' /tmp/xt.log | tail -8
kill -9 $qpid 2>/dev/null