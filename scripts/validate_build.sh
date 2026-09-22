#!/bin/bash
# Valida que os fontes do kernel compilam (sem bootloader EFI).
# Uso: dentro do WSL2 Debian.
cd "$(cd "$(dirname "$0")/.." && pwd)" || exit 1

CFLAGS="-Wall -Wextra -O2 -ffreestanding -fno-stack-protector -fno-pic -mno-red-zone -m64 -mcmodel=kernel -march=x86-64 -g -I src/kernel/include"

fail=0
for f in src/kernel/*.c; do
    o="/tmp/$(basename "$f").o"
    if ! gcc $CFLAGS -c "$f" -o "$o" 2>/tmp/err.txt; then
        echo "FAILED: $f"
        cat /tmp/err.txt
        fail=1
    fi
done

# assuntos de asm também (ctxswitch/idt/usermode/boot)
for a in src/kernel/arch/x86_64/*.asm; do
    o="/tmp/$(basename "$a").o"
    if ! nasm -f elf64 "$a" -o "$o" 2>/tmp/err.txt; then
        echo "ASM FAILED: $a"
        cat /tmp/err.txt
        fail=1
    fi
done

if [ "$fail" = "0" ]; then
    echo "ALL OK"
else
    echo "BUILD FAILURES"
    exit 1
fi
