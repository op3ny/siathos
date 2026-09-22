#!/bin/bash
cd "$(cd "$(dirname "$0")/.." && pwd)" || exit 1
echo '=== Thais OS / ThaisOS / Thais-OS (nome do sistema) ==='
grep -rn 'Thais OS\|ThaisOS\|Thaís OS\|Thais-OS OS\|Siath' --include='*.c' --include='*.h' --include='*.asm' --include='*.md' --include='Makefile' . 2>/dev/null | grep -v '/build/' | grep -v '.git/'