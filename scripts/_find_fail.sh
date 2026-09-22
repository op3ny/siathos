#!/bin/bash
cd "$(cd "$(dirname "$0")/.." && pwd)" || exit 1
mkdir -p build/esp/EFI/BOOT
cp -f build/kernel.elf build/esp/kernel.elf
cp -f build/BOOTX64.EFI build/esp/EFI/BOOT/BOOTX64.EFI
for i in 1 2 3 4 5 6 7 8; do
    rm -f /tmp/xt.log
    timeout 45 qemu-system-x86_64 -drive format=raw,file=fat:rw:build/esp \
        -bios /usr/share/ovmf/OVMF.fd -m 512M -serial file:/tmp/xt.log \
        -display none -no-reboot 2>/dev/null &
    qpid=$!
    sleep 25
    kill -9 $qpid 2>/dev/null
    P=$(grep -c 'todos os apps' /tmp/xt.log)
    F=$(grep -c 'EXCECAO' /tmp/xt.log)
    echo "RUN$i PASS=$P FAIL=$F"
    if [ "$P" -gt 0 ]; then
        cp /tmp/xt.log /tmp/xt_pass.log
        echo "PASSOU na RUN$i"
        exit 0
    fi
    if [ "$F" -gt 0 ]; then
        cp /tmp/xt.log /tmp/xt_fail.log
        echo "FALHOU na RUN$i"
        exit 2
    fi
done
echo "nenhum passou e nenhum falhou em 8 (pode ter travado sem excecao)"
exit 3