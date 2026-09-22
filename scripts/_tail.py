#!/usr/bin/env python3
import sys
with open(sys.argv[1] if len(sys.argv)>1 else '/tmp/xt.log', encoding='utf-8', errors='replace') as f:
    data=f.read()
print(data[-6000:])