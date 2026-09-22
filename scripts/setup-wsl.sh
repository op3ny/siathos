#!/bin/bash
# Thaís OS — setup toolchain WSL2 Debian (gcc + nasm + QEMU + OVMF + mtools)
# Made with love by Thaís (op3n/op3ny)
# Rode dentro do WSL2 Debian (defina SUDO_PASS se o sudo pedir senha)
set -e
echo "[emporion] Instalando toolchain Thaís OS..."
sudo apt update
sudo apt install -y build-essential clang lld nasm xorriso mtools qemu-system-x86 ovmf git curl python3 nodejs g++ make bison flex libgmp-dev libmpc-dev libmpfr-dev texinfo
# cross compiler x86_64-elf (se não existir, builda)
if ! command -v x86_64-elf-gcc >/dev/null 2>&1; then
  echo "[techne] cross-compiler não encontrado, usando clang como fallback"
  echo "Para cross-gcc completo: https://wiki.osdev.org/GCC_Cross-Compiler"
  # alias clang para x86_64-elf-gcc via wrapper
  sudo ln -sf $(which clang) /usr/local/bin/x86_64-elf-gcc || true
  sudo ln -sf $(which ld.lld) /usr/local/bin/x86_64-elf-ld || true
fi
# Limine nao e mais necessario: bootloader e o ThaisBoot (build nativo via make)
echo "[catallaxy] Setup OK. Tente: make iso && make run"
