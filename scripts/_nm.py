import re, subprocess, sys
out = subprocess.run(['nm', 'kernel.elf'], capture_output=True, text=True, cwd='build').stdout
target = int(sys.argv[1], 16) if len(sys.argv) > 1 else int('ffffffff8000b9a0', 16)
prev = None
for line in out.splitlines():
    m = re.match(r'^([0-9a-fA-F]+)\s+\w\s+(\S+)', line)
    if m:
        a = int(m.group(1), 16)
        if a <= target:
            prev = (hex(a), m.group(2))
        elif prev and a > target:
            break
print(prev)
# also list functions around target
for line in out.splitlines():
    m = re.match(r'^([0-9a-fA-F]+)\s+\w\s+(\S+)', line)
    if m:
        a = int(m.group(1), 16)
        if target - 0x2000 <= a <= target + 0x2000:
            print(hex(a), m.group(2), line)