#!/bin/bash
# probe E2E teclado USB nativo via xHCI (qemu-xhci + usb-kbd)
cd "$(cd "$(dirname "$0")/.." && pwd)" || exit 1
mkdir -p build/esp/EFI/BOOT
cp -f build/kernel.elf build/esp/kernel.elf
cp -f build/BOOTX64.EFI build/esp/EFI/BOOT/BOOTX64.EFI
rm -f /tmp/xu.log /tmp/qmon
timeout 90 qemu-system-x86_64 -drive format=raw,file=fat:rw:build/esp \
    -bios /usr/share/ovmf/OVMF.fd -m 512M -serial file:/tmp/xu.log \
    -display none -no-reboot -monitor unix:/tmp/qmon,server,nowait \
    -device qemu-xhci -device usb-kbd 2>/dev/null &

python3 - <<'PYEOF'
import socket, time

PATHS = ["/tmp/xu.log"]

def peek(path):
    try:
        with open(path, "rb") as f:
            f.seek(0, 2)
            f.seek(max(0, f.tell() - 16 * 1024))
            return f.read()
    except Exception:
        return b""

def wait_marker(prefix, pat, timeout=80):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if pat in peek(prefix):
            return True
        time.sleep(0.5)
    return False

def _cmd(s, line, delay=0.4):
    s.sendall((line + "\n").encode())
    time.sleep(delay)
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

def cmd(s, line, delay=0.4):
    data = _cmd(s, line, delay=delay)
    print("MONKEY " + line.strip() + " -> " + repr(data))

s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
deadline = time.time() + 60
while time.time() < deadline:
    try:
        s.connect("/tmp/qmon"); break
    except FileNotFoundError:
        time.sleep(0.2)
else:
    print("MONITOR_MISS"); raise SystemExit(1)

if not wait_marker("/tmp/xu.log", b"teclado HID reconhecido", 80):
    print("XHCI_MISS"); raise SystemExit(1)
time.sleep(1)

for k in ["a", "t", "h", "ret"]:
    cmd(s, "sendkey " + k)
    time.sleep(8)
cmd(s, "sendkey e")
time.sleep(3)
cmd(s, "sendkey c")
time.sleep(3)
cmd(s, "sendkey h")
time.sleep(3)
cmd(s, "sendkey o")
time.sleep(3)
cmd(s, "sendkey spc")
time.sleep(3)
cmd(s, "sendkey o")
time.sleep(3)
cmd(s, "sendkey i")
time.sleep(3)
cmd(s, "sendkey ret")
time.sleep(6)
cmd(s, "quit")
PYEOF

sleep 2
echo "===== marcadores ====="
grep -qa "teclado HID reconhecido via xHCI" /tmp/xu.log && echo "XHCI_OK" || echo "XHCI_MISS"
grep -qa "Novo admin" /tmp/xu.log && echo "LOGIN_OK" || echo "LOGIN_MISS"
grep -qa "Bem-vindo\|cadastrado\|Praxia (shell\|conectado" /tmp/xu.log && echo "SHELL_OK" || echo "SHELL_MISS"
grep -qa "echo: oi" /tmp/xu.log && echo "KBD_OK" || echo "KBD_MISS"
grep -qa "EXCECAO\|PANIC\|triple" /tmp/xu.log && echo "PANIC_DETECTED" || echo "PANIC_NONE"
mkdir -p build/logs
cp /tmp/xu.log build/logs/_probe_usbkbd_e2e.log 2>/dev/null
echo "===== trecho xhci ====="
grep -a "xhci\|usb\]\|aisthesis\|Novo admin\|echo:" /tmp/xu.log | head -30