import sys,struct
d=open(sys.argv[1],'rb').read()
h=struct.unpack('<16I',d[:64]); pitch=h[6]; W=h[3]
o=64+0x40000+pitch*16
pre=d[o:o+0x10000]; z0=d[o+0x10000:o+0x20000]
zbase=h[1]; prebase=zbase-0x10000
# back buffer end
bbend=h[5]+pitch*h[8]
print('backbuf end %x, Z base %x, slack %x bytes'%(bbend,zbase,zbase-bbend))
# scan the slack in 512-byte blocks: classify as zero / ffff / z-like (high byte 0xf?)
s=bbend-prebase
for off in range(s - (s%512), 0x10000, 512):
    blk=pre[off:off+512]
    vals=struct.unpack('<256H',blk)
    nz=sum(v!=0 for v in vals); ff=sum(v==0xffff for v in vals); hi=sum(0xf000<=v<0xffff for v in vals)
    if off % 2560 == 0 or off>=0x10000-512*30:
        print('%x: nonzero %3d ffff %3d zlike %3d  first %s'%(prebase+off,nz,ff,hi,' '.join('%04x'%v for v in vals[:6])))
