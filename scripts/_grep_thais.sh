#!/bin/bash
cd "$(cd "$(dirname "$0")/.." && pwd)" || exit 1
echo '=== esqueletos ==='
find src/userspace/thais-init src/userspace/thais-login src/userspace/thais-sh -type f 2>/dev/null
echo '=== contagem por arquivo ==='
for f in docs/BLUEPRINT.md docs/STATUS.md Makefile README.md scripts/_offset_probe.c skill.md \
         src/boot/thaisboot/main.c src/boot/thaisboot/README.md src/boot/thaisboot/thaisboot.lds \
         src/kernel/arch/x86_64/boot.asm src/kernel/arch/x86_64/ctxswitch.asm src/kernel/arch/x86_64/idt.asm \
         src/kernel/arch/x86_64/usermode.asm src/kernel/fetch.asm src/kernel/fs.c src/kernel/login.c \
         src/kernel/main.c src/kernel/mem.asm src/kernel/shell.c src/kernel/splash.c \
         src/userspace/crt0.asm src/userspace/spoudazo/spoudazo.c src/userspace/spud.h; do
    n=$(grep -c 'Thais\|thais' "$f" 2>/dev/null)
    echo "$f: $n"
done