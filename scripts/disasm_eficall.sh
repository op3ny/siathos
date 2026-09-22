#!/bin/bash
cd /tmp
rm -rf efixtr && mkdir efixtr && cd efixtr
ar x /usr/lib/libefi.a 2>/dev/null
for o in *.o; do
  if objdump -t "$o" 2>/dev/null | grep -q efi_call3; then
    echo "=== $o ==="
    objdump -d "$o" 2>/dev/null | sed -n '/<efi_call3>:/,/^$/p'
    objdump -d "$o" 2>/dev/null | sed -n '/<efi_call4>:/,/^$/p'
  fi
done
