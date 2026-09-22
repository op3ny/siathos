#!/bin/bash
# probe SYS_EXEC (MARCO 3/5): boot TEST=1 unico, checa marcadores dos apps externos.
cd "$(cd "$(dirname "$0")/.." && pwd)" || exit 1
mkdir -p build/esp/EFI/BOOT
cp -f build/kernel.elf build/esp/kernel.elf
cp -f build/BOOTX64.EFI build/esp/EFI/BOOT/BOOTX64.EFI
rm -f /tmp/xp.log
timeout 60 qemu-system-x86_64 -drive format=raw,file=fat:rw:build/esp \
    -bios /usr/share/ovmf/OVMF.fd -m 512M -serial file:/tmp/xp.log \
    -display none -no-reboot 2>/dev/null &
sleep 45
echo "===== marcadores ====="
for m in "exec_elf: 'echo'" "[demoexec]" "todos os apps" "exec_elf: 'spoudazo'" "exec_elf: 'praxia'" "exec_elf: 'ls'" "EXCECAO" "PANIC"; do
    if grep -qa "$m" /tmp/xp.log; then echo "OK  : $m"; else echo "MISS: $m"; fi
done
echo "===== trecho do echo final ====="
grep -a "sessao\|praxia\|echo\|demoexec\|appctl\|EXCECAO\|PANIC" /tmp/xp.log | tail -30
echo "===== como p/ espaco de trabalho ====="
mkdir -p build/logs
cp /tmp/xp.log build/logs/_probe_exec.log 2>/dev/null
echo "salvo em build/logs/_probe_exec.log"
