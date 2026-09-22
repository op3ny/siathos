#!/bin/bash
# Rebranding: nome do sistema "Thaís/Thais OS" -> "Siaht OS" (identificadores
# tecnicos thaisboot/thais.h/thais-*/libthais permanecem).
cd "$(cd "$(dirname "$0")/.." && pwd)" || exit 1
FILES="
docs/BLUEPRINT.md
docs/COMANDOS.md
docs/DIRS.md
docs/RUNTIMES.md
docs/SERVICES.md
docs/STATUS.md
docs/SYSCALL-ABI.md
Makefile
README.md
skill.md
src/kernel/arch/x86_64/boot.asm
src/kernel/arch/x86_64/ctxswitch.asm
src/kernel/arch/x86_64/idt.asm
src/kernel/arch/x86_64/usermode.asm
src/kernel/fetch.asm
src/kernel/fs.c
src/kernel/login.c
src/kernel/main.c
src/kernel/mem.asm
src/kernel/splash.c
src/userspace/crt0.asm
src/userspace/spoudazo/spoudazo.c
src/userspace/spud.h
src/userspace/thais-sh/README.md
"
for f in $FILES; do
    [ -f "$f" ] || continue
    sed -i 's/Thaís OS/Siaht OS/g; s/Thais OS/Siaht OS/g; s/Thais OS/Siaht OS/g' "$f"
done
echo '=== restantes (nao devem existir "Thais OS"/"Thaís OS") ==='
grep -rn 'Thaís OS\|Thais OS' --include='*.c' --include='*.h' --include='*.asm' --include='*.md' --include='Makefile' . 2>/dev/null | grep -v '/build/' | grep -v '/.git/'
echo '=== conferencia Siaht ==='
grep -rn 'Siaht OS' --include='*.c' --include='*.h' --include='*.asm' --include='*.md' --include='Makefile' . 2>/dev/null | grep -v '/build/' | grep -v '/.git/' | head -30