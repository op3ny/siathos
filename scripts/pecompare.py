import sys
def parse(f):
    b=bytearray(open(f,'rb').read())
    e=int.from_bytes(b[0x3C:0x40],'little')
    assert b[e:e+4]==b'PE\x00\x00',(f,'no PE')
    coff=e+4
    d={}
    d['machine']=int.from_bytes(b[coff:coff+2],'little')
    d['nsec']=int.from_bytes(b[coff+2:coff+4],'little')
    d['soh']=int.from_bytes(b[coff+16:coff+18],'little')
    d['coffchar']=int.from_bytes(b[coff+18:coff+20],'little')
    oh=e+4+20
    d['magic']=int.from_bytes(b[oh:oh+2],'little')
    d['entry']=int.from_bytes(b[oh+16:oh+20],'little')
    d['baseofcode']=int.from_bytes(b[oh+20:oh+24],'little')
    d['baseofdata']=int.from_bytes(b[oh+24:oh+28],'little')
    d['ibase']=int.from_bytes(b[oh+28:oh+36],'little')
    d['sectalign']=int.from_bytes(b[oh+32:oh+36],'little')
    d['filealign']=int.from_bytes(b[oh+36:oh+40],'little')
    d['subsys']=int.from_bytes(b[oh+68:oh+70],'little')
    d['dllchar']=int.from_bytes(b[oh+70:oh+72],'little')
    d['numrva']=int.from_bytes(b[oh+92:oh+96],'little')
    d['dirs']=[]
    dp=oh+96
    for i in range(d['numrva']):
        va=int.from_bytes(b[dp:dp+4],'little'); sz=int.from_bytes(b[dp+4:dp+8],'little')
        d['dirs'].append((va,sz)); dp+=8
    d['secs']=[]
    sh=oh+d['soh']
    for i in range(d['nsec']):
        nm=b[sh:sh+8]; va=int.from_bytes(b[sh+8:sh+12],'little')
        vsz=int.from_bytes(b[sh+12:sh+16],'little'); raw=int.from_bytes(b[sh+16:sh+20],'little')
        rsz=int.from_bytes(b[sh+20:sh+24],'little'); chr=int.from_bytes(b[sh+36:sh+40],'little')
        d['secs'].append((nm.rstrip(b'\x00').decode('latin1'),va,vsz,raw,rsz,chr)); sh+=40
    return d
a=parse(sys.argv[1]); b=parse(sys.argv[2])
def diff(key):
    if a[key]!=b[key]: print('DIFF',key,'A=%#x B=%#x'%(a[key],b[key]))
for k in ['machine','nsec','soh','coffchar','magic','entry','baseofcode','baseofdata','ibase','sectalign','filealign','subsys','dllchar','numrva']:
    diff(k)
print('--- dirs ---')
for i,(x,y) in enumerate(zip(a['dirs'],b['dirs'])):
    if x!=y: print('DIFF dir',i,'A',x,'B',y)
print('--- sections ---')
for s in a['secs']:
    if s not in b['secs']: print('ONLY A',s)
for s in b['secs']:
    if s not in a['secs']: print('ONLY B',s)
