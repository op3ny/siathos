#!/bin/bash
# Thaís OS — setup MSYS2 (alternativa nativa Windows)
pacman -S --needed --noconfirm mingw-w64-x86_64-gcc mingw-w64-x86_64-clang mingw-w64-x86_64-lld mingw-w64-x86_64-nasm mingw-w64-x86_64-qemu git make xorriso mtools
echo "MSYS2 pronto. Use make CC=clang LD=ld.lld"
