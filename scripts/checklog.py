import sys
d=open(sys.argv[1],'rb').read()
print('has THAISTEST bytes:', b'T\x00H\x00A\x00I\x00S\x00T\x00E\x00S\x00T' in d)
idx=[i for i in range(len(d)) if d[i:i+9]==b'T\x00H\x00A\x00I\x00S\x00']
print('utf16 offsets:', idx[:5])
