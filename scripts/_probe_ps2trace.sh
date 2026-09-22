#!/bin/bash
# Diagnostico QEMU PS/2 trace: sendkey -> ps2 queue -> odigos
# roda com trace ps2 ativo e envia 3 teclas isoladas, mostra trace
cd "$(cd "$(dirname "$0")/.." && pwd)" || exit 1
make 2>&1 | tail -2
mkdir -p build/esp/EFI/BOOT
cp -f build/kernel.elf build/esp/kernel.elf
cp -f build/BOOTX64.EFI build/esp/EFI/BOOT/BOOTX64.EFI

printf 'ps2_put_keycode\nps2_keyboard_event\nps2_read_data\nps2_write_keyboard\npckbd_kbd_read_status\npckbd_kbd_read_data\npckbd_kbd_write_command\npckbd_kbd_write_data\n' > /tmp/ptr.events
rm -f /tmp/qtrace.log /tmp/xk2.log /tmp/qmon2

timeout 80 qemu-system-x86_64 -drive format=raw,file=fat:rw:build/esp \
    -bios /usr/share/ovmf/OVMF.fd -m 512M -serial file:/tmp/xk2.log \
    -display none -no-reboot \
    -monitor unix:/tmp/qmon2,server,nowait \
    -trace events=/tmp/ptr.events -trace file=/tmp/qtrace.log 2>/dev/null &
QPID=$!

python3 <<'PYEOF'
import socket, time
def wait(path, pat=b"Novo admin", timeout=60):
    deadline = time.time()+timeout
    while time.time()<deadline:
        try:
            f=open(path,"rb"); f.seek(0,2); f.seek(max(0,f.tell()-8192)); d=f.read(); f.close()
            if pat in d: return True
        except: pass
        time.sleep(0.5)
    return False

s=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM)
deadline=time.time()+60
while time.time()<deadline:
    try: s.connect("/tmp/qmon2"); break
    except: time.sleep(0.2)
else: print("monitor miss"); raise SystemExit(1)

if not wait("/tmp/xk2.log", timeout=60):
    print("login miss"); raise SystemExit(1)
time.sleep(1)

seq = ["a","n","a","ret", "1","2","3","4","ret", "1","2","3","4","ret",
       "a","n","a","ret", "1","2","3","4","ret", "y","ret"]
for k in seq:
    s.sendall(("sendkey "+k+("\n" if not k.startswith("kp") else "\n")).encode()); time.sleep(0.4)
s.sendall(b"quit\n"); print("done")
PYEOF

sleep 3
echo "=== ps2 trace lines ==="
wc -l /tmp/qtrace.log 2>/dev/null
echo "=== first 60 trace entries ==="
head -60 /tmp/qtrace.log 2>/dev/null
echo "=== serial hits ==="
grep -a 'Novo admin\|st=0x\|KEY\|odigos>\|sysread\|kbd-poll' /tmp/xk2.log 2>/dev/null | head -60
echo "=== serial tail ==="
tail -c 2000 /tmp/xk2.log 2>/dev/null | strings | tail -25
echo "=== salvo ==="
mkdir -p build/logs
cp -f /tmp/xk2.log build/logs/_probe_ps2trace.log 2>/dev/null && echo "copiado p/ build/logs/_probe_ps2trace.log"
echo "=== perf counter ==="
wc -l /tmp/qtrace.log 2>/dev/null
echo "=== ps2_put_keycode count ==="
grep -c 'ps2_put_keycode' /tmp/qtrace.log 2>/dev/null
echo "=== ps2_read_data count ==="
grep -c 'ps2_read_data' /tmp/qtrace.log 2>/dev/null
echo "=== tick-kbd / iop / KEY / odig ==="
grep -aE 'tick-kbd|iop|KEY|odig|kbd-poll|sysread' /tmp/xk2.log 2>/dev/null | head -40
