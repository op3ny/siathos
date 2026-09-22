#!/bin/bash
# Linka o kernel completo para validar que nada quebrou (sem bootloader EFI).
cd "$(cd "$(dirname "$0")/.." && pwd)" || exit 1
CFLAGS="-Wall -Wextra -O2 -ffreestanding -fno-stack-protector -fno-pic -mno-red-zone -m64 -mcmodel=kernel -march=x86-64 -g -I src/kernel/include"
LDFLAGS="-T linker.ld -nostdlib -static"
rm -rf /tmp/thaistlink && mkdir -p /tmp/thaistlink
obj=()
for f in src/kernel/*.c; do
    b=$(basename "$f" .c)
    o="/tmp/thaistlink/c_$b.o"
    gcc $CFLAGS -c "$f" -o "$o" || { echo "COMPILE FAIL $f"; exit 1; }
    obj+=("$o")
done
for a in src/kernel/arch/x86_64/*.asm; do
    b=$(basename "$a" .asm)
    o="/tmp/thaistlink/a_$b.o"
    nasm -f elf64 "$a" -o "$o" || { echo "ASM FAIL $a"; exit 1; }
    obj+=("$o")
done
if ld $LDFLAGS "${obj[@]}" -o /tmp/thaistlink/kernel.elf 2>/tmp/thaistlink/linkerr.txt; then
    echo "LINK OK -> /tmp/thaistlink/kernel.elf ($(stat -c%s /tmp/thaistlink/kernel.elf) bytes)"
else
    echo "LINK FAILED:"
    cat /tmp/thaistlink/linkerr.txt
    exit 1
fi
