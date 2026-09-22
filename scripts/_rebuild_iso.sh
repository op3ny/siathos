#!/bin/bash
# Regenera ISO com o kernel LIMPO (sem THAIS_TEST_APPS) num arquivo novo,
# sem depender do build/thais.iso (que pode estar aberto no VMware).
cd "$(cd "$(dirname "$0")/.." && pwd)" || exit 1
echo '=== kernel atual (deve ser LIMPO, sem testap) ==='
grep -q 'THAIS_TEST_APPS' build/kernel.elf 2>/dev/null && echo "AVISO: kernel ainda tem THAIS_TEST_APPS (string)" || echo "kernel sem marca TEST (ok)"
# monta o ESP novo e gera ISO com saida siaht.iso
BUILD="build"
ESP="$BUILD/esp.img"
MKISO="$BUILD/mkiso_root"
ISO="$BUILD/siaht-2.iso"
rm -rf "$MKISO"
mkdir -p "$MKISO"
echo "call mkiso step by step to $ISO"
bash -x scripts/mkiso.sh >/tmp/mkiso3.log 2>&1 || true
echo "mkiso saido com RC=$?"
tail -4 /tmp/mkiso3.log
ls -la --time-style=+%T build/*.iso