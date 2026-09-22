import sys
d=open(sys.argv[1],'rb').read()
# strip ANSI escape sequences
import re
d=re.sub(rb'\x1b\[[0-9;]*[A-Za-z]', b'', d)
out=[]
buf=bytearray()
for b in d:
    if 32<=b<127:
        buf.append(b)
    elif b in (10,13):
        if buf: out.append(bytes(buf).decode('latin1')); buf=bytearray()
    else:
        if buf: out.append(bytes(buf).decode('latin1')); buf=bytearray()
        out.append('<0x%02x>'%b)
print('\n'.join(out))
