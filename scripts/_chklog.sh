#!/bin/bash
for f in /tmp/xt*.log; do
  echo "== $f"
  grep -c PANIC "$f"
done