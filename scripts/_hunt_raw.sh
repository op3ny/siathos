#!/bin/bash
cd "$(cd "$(dirname "$0")/.." && pwd)" || exit 1
for i in $(seq 1 12); do
  bash scripts/_runtest.sh 1 >/tmp/rt.log 2>&1
  if grep -q 'CORROMPIDO\|PANIC\|EXCECAO' /tmp/rt.log; then
    echo "=== RUN $i FAIL ==="
    grep -E 'oikos|corr|EXCECAO|PANIC|gc|exit|pk|sw|irq' /tmp/rt.log | head -60
    break
  fi
  echo "run $i ok"
done