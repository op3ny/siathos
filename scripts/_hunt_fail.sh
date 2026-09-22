#!/bin/bash
cd "$(cd "$(dirname "$0")/.." && pwd)" || exit 1
rm -f src/kernel/util.o src/kernel/proc.o
make TEST=1 2>&1 | grep -iE 'error|undefined'
mkdir -p build/esp/EFI/BOOT
cp -f build/kernel.elf build/esp/kernel.elf
cp -f build/BOOTX64.EFI build/esp/EFI/BOOT/BOOTX64.EFI
for i in $(seq 1 12); do
    rm -f /tmp/xt.log
    timeout 40 qemu-system-x86_64 -drive format=raw,file=fat:rw:build/esp \
        -bios /usr/share/ovmf/OVMF.fd -m 512M -serial file:/tmp/xt.log \
        -display none -no-reboot 2>/dev/null &
    qpid=$!
    sleep 22
    kill -9 $qpid 2>/dev/null
    wait $qpid 2>/dev/null
    P=$(grep -c 'todos os apps' /tmp/xt.log)
    F=$(grep -c 'EXCECAO' /tmp/xt.log)
    echo "RUN$i P=$P F=$F"
    if [ "$F" -gt 0 ]; then
        cp /tmp/xt.log /tmp/xt_fail.log
        echo "FAIL capture RUN$i"
        exit 2
    fi
done
echo "sem FAIL em 12"
exit 3