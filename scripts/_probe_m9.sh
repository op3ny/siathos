#!/bin/bash
# probe MARCO 9 com contexto do PANIC (se houver). Roda qemu e analisa na mesma invocacao.
cd "$(cd "$(dirname "$0")/.." && pwd)" || exit 1
mkdir -p build/esp/EFI/BOOT
cp -f build/kernel.elf build/esp/kernel.elf
cp -f build/BOOTX64.EFI build/esp/EFI/BOOT/BOOTX64.EFI
rm -f /tmp/xp.log
timeout 60 qemu-system-x86_64 -drive format=raw,file=fat:rw:build/esp \
    -bios /usr/share/ovmf/OVMF.fd -m 512M -serial file:/tmp/xp.log \
    -display none -no-reboot 2>/dev/null &
sleep 45
echo "===== svctest/svc objetivos ====="
grep -a 'devd\|fsd\|svctest\|testando\|ping ->\|uptime ->\|read /nomos\|MARCO 9\|FALHAS\|nao achou\|registrado' /tmp/xp.log | tail -40
echo "===== contexto do PANIC (se houver) ====="
L=$(grep -an 'PANIC' /tmp/xp.log | head -1 | cut -d: -f1)
if [ -n "$L" ]; then
    S=$((L-45))
    [ $S -lt 1 ] && S=1
    sed -n "${S},${L}p" /tmp/xp.log
else
    echo "sem PANIC"
fi
echo "===== fim do log (ultimas 15 linhas) ====="
tail -15 /tmp/xp.log
mkdir -p build/logs
cp /tmp/xp.log build/logs/_m9.log 2>/dev/null
echo "salvo: build/logs/_m9.log"