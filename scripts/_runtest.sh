#!/bin/bash
# Temporario (debug): roda a regressao serial TEST=1 N vezes analisando /tmp/xt.log.
# Valida TODOS os markers esperados do boot TEST (apps + fsd + svctest + auth + synd):
#   [appctl] todos os apps, [fsd] heap-probe/coalesce, [fsd] store ring3,
#   [svctest] MARCO 9 OK, [svctest] auth IPC ok, [svctest] fsd IPC ops ok,
#   [svctest] synd IPC ok, [wtest], [phrourio], [authd] PBKDF2 self-test ok,
#   [synd] store ring3, [authd] servico 'auth' ... hash via kernel
cd "$(cd "$(dirname "$0")/.." && pwd)" || exit 1
mkdir -p build/esp/EFI/BOOT build/logs
cp -f build/kernel.elf build/esp/kernel.elf
cp -f build/BOOTX64.EFI build/esp/EFI/BOOT/BOOTX64.EFI

QEMU="${QEMU:-qemu-system-x86_64}"
OVMF="${OVMF:-/usr/share/ovmf/OVMF.fd}"
XHCI_DEV="${XHCI_DEV:--device qemu-xhci -device usb-kbd}"

N="${1:-8}"
P=0; F=0
for i in $(seq 1 "$N"); do
    rm -f /tmp/xt.log
    timeout 60 "$QEMU" -drive format=raw,file=fat:rw:build/esp \
        -bios "$OVMF" -m 512M -serial stdio \
        -display none -no-reboot $XHCI_DEV > /tmp/xt.log 2>/dev/null &
    sleep 45
    if grep -qa 'todos os apps' /tmp/xt.log && \
       grep -qa '\[fsd\] heap-probe ok' /tmp/xt.log && \
       grep -qa '\[fsd\] heap coalesce ok' /tmp/xt.log && \
       grep -qa '\[fsd\] store ring3' /tmp/xt.log && \
       grep -qa '\[svctest\] MARCO 9 OK' /tmp/xt.log && \
       grep -qa '\[svctest\] auth IPC ok' /tmp/xt.log && \
       grep -qa '\[svctest\] fsd IPC ops ok' /tmp/xt.log && \
       grep -qa '\[svctest\] synd IPC ok' /tmp/xt.log && \
       grep -qa '\[wtest\] escrita+leitura ok' /tmp/xt.log && \
       grep -qa '\[phrourio\] capacidades isoladas OK' /tmp/xt.log && \
       grep -qa '\[authd\] PBKDF2 self-test ok' /tmp/xt.log && \
       grep -qa '\[synd\] store ring3' /tmp/xt.log && \
       grep -Fa "[authd] servico 'auth' ring 3: store+politica+ticket (ABI 1.5), hash via kernel" /tmp/xt.log >/dev/null; then
        echo "RUN$i PASS"
        P=$((P+1))
        cp /tmp/xt.log "build/logs/run$i.pass.log"
    else
        echo "RUN$i FAIL"
        F=$((F+1))
        cp /tmp/xt.log "build/logs/run$i.fail.log"
    fi
done
echo "RESULT PASS=$P FAIL=$F"