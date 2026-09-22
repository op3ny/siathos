#!/bin/sh
# Convenience: roda a automacao de teste (login + comandos) via QEMU.
# Uso (WSL):  bash scripts/run_auto_test.sh [comandos...]
# Ex.: bash scripts/run_auto_test.sh "praxia /praxis/fetch" "praxia /praxis/mem"
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR/.."
exec python3 "$DIR/run_auto_test.py" "$@"
