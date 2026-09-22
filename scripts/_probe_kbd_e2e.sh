#!/bin/bash
# probe E2E teclado (producao) com delays reais via monitor unix-socket + python
cd "$(cd "$(dirname "$0")/.." && pwd)" || exit 1
mkdir -p build/esp/EFI/BOOT
cp -f build/kernel.elf build/esp/kernel.elf
cp -f build/BOOTX64.EFI build/esp/EFI/BOOT/BOOTX64.EFI
rm -f /tmp/xk.log /tmp/qmon
timeout 60 qemu-system-x86_64 -drive format=raw,file=fat:rw:build/esp \
    -bios /usr/share/ovmf/OVMF.fd -m 512M -serial file:/tmp/xk.log \
    -display none -no-reboot -monitor unix:/tmp/qmon,server,nowait 2>/dev/null &

python3 - <<'PYEOF'
import socket, time

def wait_login(path, timeout=60):
    pat = b"Novo admin"
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            with open(path, "rb") as f:
                f.seek(0, 2)  # tail aproximado
                tgt = 2 * 4096
                f.seek(max(0, f.tell() - tgt))
                data = f.read()
            if pat in data:
                return True
        except Exception:
            pass
        time.sleep(0.5)
    return False

def _cmd(s, line):
    s.sendall((line + "\n").encode())
    time.sleep(0.4)
    data = b""
    s.settimeout(0.3)
    try:
        while True:
            chunk = s.recv(4096)
            if not chunk: break
            data += chunk
    except socket.timeout:
        pass
    return data

def cmd(s, line):
    data = _cmd(s, line)
    print("MONKEY " + line.strip() + " -> " + repr(data))
    return data

s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
deadline = time.time() + 60
while time.time() < deadline:
    try:
        s.connect("/tmp/qmon"); break
    except FileNotFoundError:
        time.sleep(0.2)
else:
    print("MONITOR_MISS"); raise SystemExit(1)

if not wait_login("/tmp/xk.log", 60):
    print("LOGIN_TIMEOUT"); raise SystemExit(1)
time.sleep(1)   # deixa o prompt estabilizar

KEYS = ["a", "t", "h", "ret"]
for k in KEYS:
    cmd(s, "sendkey " + k)
    time.sleep(6)
cmd(s, "quit")
PYEOF

sleep 2
echo "===== marcadores ====="
if grep -qa "Novo admin" /tmp/xk.log; then echo "LOGIN_OK"; else echo "LOGIN_MISS"; fi
if grep -qa "Bem-vindo\|cadastrado\|Praxia (shell\|conectado" /tmp/xk.log; then echo "SHELL_OK"; else echo "SHELL_MISS"; fi
if grep -qa "echo: oi\|oi\b" /tmp/xk.log; then echo "KBD_OK"; else echo "KBD_MISS"; fi
grep -qa "EXCECAO\|PANIC" /tmp/xk.log && echo "PANIC_DETECTED" || echo "PANIC_NONE"
mkdir -p build/logs
cp /tmp/xk.log build/logs/_probe_kbd_e2e.log 2>/dev/null
echo "===== trecho p/ login ====="
grep -a "Novo admin\|Bem-vindo\|cadastrado\|Praxia\|echo:" /tmp/xk.log | head -15