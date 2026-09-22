#!/usr/bin/env python3
"""run_auto_test.py - roda o Thais OS direto da source (fat, sem ISO) e
automatiza login + comandos via monitor QEMU (sendkey), usando conexao
persistente ao socket unix e esperando os prompts no serial log.

Uso (WSL):  python3 scripts/run_auto_test.py [comandos...]
Ex.: python3 scripts/run_auto_test.py "praxia /praxis/fetch" "praxia /praxis/mem"
"""
import os
import socket
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)

LOG = "/tmp/thais_auto.log"
QSOCK = "/tmp/thais_mon.sock"
for p in (LOG, QSOCK):
    if os.path.exists(p):
        os.unlink(p)

os.makedirs("build/esp/EFI/BOOT", exist_ok=True)
subprocess.run(["cp", "-f", "build/BOOTX64.EFI", "build/esp/EFI/BOOT/BOOTX64.EFI"], check=True)
subprocess.run(["cp", "-f", "build/kernel.elf", "build/esp/kernel.elf"], check=True)

qemu = subprocess.Popen([
    "qemu-system-x86_64",
    "-drive", "format=raw,file=fat:rw:build/esp",
    "-bios", "/usr/share/ovmf/OVMF.fd",
    "-m", "512M",
    "-serial", "file:" + LOG,
    "-display", "none", "-no-reboot",
    "-monitor", "unix:%s,server,nowait" % QSOCK,
])

# conecta ao monitor
sock = None
for _ in range(100):
    try:
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.connect(QSOCK)
        break
    except OSError:
        time.sleep(0.2)
if sock is None:
    print("[auto] nao consegui conectar ao monitor")
    qemu.kill()
    sys.exit(1)


def log_text():
    try:
        with open(LOG, "r", encoding="utf-8", errors="replace") as f:
            return f.read()
    except OSError:
        return ""


def wait_for(pat, timeout=25):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if pat in log_text():
            return True
        if qemu.poll() is not None:
            return False
        time.sleep(0.25)
    return False


def send(line):
    try:
        sock.sendall((line + "\n").encode())
        time.sleep(0.02)
    except OSError:
        pass


def key(k):
    send("sendkey " + k)
    time.sleep(0.15)


def enter():
    key("ret")
    time.sleep(0.4)


def type_text(s):
    for c in s:
        if c == " ":
            key("spc")
        elif c == "/":
            key("slash")
        else:
            key(c)
        time.sleep(0.20)


def main():
    cmds = sys.argv[1:]

    # bootstrap admin (se necessario)
    if not wait_for("Novo admin", 30):
        print("[auto] boot nao chegou no prompt de admin")
    else:
        type_text("thais"); enter()
        if not wait_for("Senha:", 12):
            print("[auto] sem prompt de senha do admin")
        type_text("1234"); enter()
        if not wait_for("Confirme", 12):
            print("[auto] sem prompt de confirmacao")
        type_text("1234"); enter()

    # login
    wait_for("usuario:", 25)
    type_text("thais"); enter()
    wait_for("senha:", 12)
    type_text("1234"); enter()

    # consentimento
    for _ in range(2):
        wait_for("Deseja prosseguir", 12)
        type_text("s"); enter()

    # comandos
    wait_for("$", 15) or wait_for("#", 15)
    for cmd in cmds:
        type_text(cmd); enter()
        time.sleep(1.2)

    time.sleep(0.5)
    qemu.kill()

    print("=== SERIAL LOG ===")
    print(log_text())


if __name__ == "__main__":
    main()
