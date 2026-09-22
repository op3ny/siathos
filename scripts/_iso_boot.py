# -*- coding: utf-8 -*-
import subprocess, time
subprocess.Popen(['qemu-system-x86_64','-cdrom','build/thais.iso','-bios','/usr/share/ovmf/OVMF.fd','-m','512M','-serial','file:/tmp/iso.log','-display','none','-no-reboot'])
time.sleep(35)
data = open('/tmp/iso.log', encoding='utf-8', errors='replace').read()
print('boot_ok:', 'Novo admin:' in data)
print('panic:', 'PANIC' in data)
print(data[-400:])