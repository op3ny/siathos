#!/bin/bash
# Verifica a toolchain de boot EFI no WSL para permitir build completo do ISO.
echo "== EFI headers =="
ls /usr/include/efi/x86_64 2>&1 | head -3
echo "== GNU-EFI libs =="
ls /usr/lib/*efi* /usr/lib/*gnuefi* 2>&1 | head
echo "== crt0 =="
ls /usr/lib/crt0-efi-x86_64.o /usr/lib/elf_x86_64_efi.lds 2>&1
echo "== xorriso/ovmf =="
which xorriso 2>&1
ls /usr/share/ovmf/OVMF.fd 2>&1
echo "== limine? =="
ls limine 2>&1 | head
