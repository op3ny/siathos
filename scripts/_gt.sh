#!/bin/bash
f="$1"
grep -E "oikos|corr|EXCECAO|PANIC|gc|exit|pk|sw|irq|hb" "$f" | head -70