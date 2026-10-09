import sys,struct,capstone
d=open(sys.argv[1],'rb').read()
base=0x10000
hdr=struct.unpack_from('<I',d,0x120)[0]; n=struct.unpack_from('<I',d,0x11C)[0]
secs=[struct.unpack_from('<5I',d,hdr-base+i*56) for i in range(n)]
def off(va):
    for fl,v,vs,r,rs in secs:
        if v<=va<v+rs: return va-v+r
va=int(sys.argv[2],16); cnt=int(sys.argv[3]) if len(sys.argv)>3 else 30
o=off(va); md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
for i,ins in enumerate(md.disasm(d[o:o+cnt*8],va)):
    if i>=cnt: break
    print("%08X  %-24s %s %s"%(ins.address,ins.bytes.hex(),ins.mnemonic,ins.op_str))
