import sys
for f in sys.argv[1:]:
    b=bytearray(open(f,'rb').read())
    e=int.from_bytes(b[0x3C:0x40],'little')
    assert b[e:e+4]==b'PE\x00\x00', (f,'no PE', b[e:e+4])
    coff=e+4
    machine=int.from_bytes(b[coff:coff+2],'little')
    nsec=int.from_bytes(b[coff+2:coff+4],'little')
    soh=int.from_bytes(b[coff+16:coff+18],'little')
    oh=e+4+20
    magic=int.from_bytes(b[oh:oh+2],'little')
    # AddressOfEntryPoint at optional offset 16
    ep_off=oh+16
    entry=int.from_bytes(b[ep_off:ep_off+4],'little')
    ibase=int.from_bytes(b[oh+24:oh+32],'little')
    char=int.from_bytes(b[coff+18:coff+20],'little')
    # first section header
    sh=oh+soh
    s0_name=b[sh:sh+8]
    s0_va=int.from_bytes(b[sh+12:sh+16],'little')
    s0_raw=int.from_bytes(b[sh+20:sh+24],'little')
    print('%-40s machine=%#x nsec=%d soh=%#x entry=%#x ibase=%#x coffchar=%#x'%(f,machine,nsec,soh,entry,ibase,char))
    print('   entry bytes @%#x = %s ; entry points to VA %#x ; section0 %s va=%#x raw=%#x'%(ep_off,b[ep_off:ep_off+4].hex(),ibase+entry,s0_name,s0_va,s0_raw))
