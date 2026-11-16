import sys,struct
from PIL import Image
d=open(sys.argv[1],'rb').read()
h=struct.unpack('<16I',d[:64]); pitch=h[6]; W=h[3]; H=h[4]
o=64+0x40000+pitch*16
pre=d[o:o+0x10000]; z0=d[o+0x10000:o+0x20000]
def widx(dx,y):
    g = 0 if dx<8 else 1; sub=dx%8
    return (y//4)*64 + g*32 + ((y%4)//2)*16 + (sub//2)*4 + (sub%2) + (y%2)*2
def zval(x,y,rowblock=0):
    bpr=(W*2+63)//64
    b=x//32; w=(x%32)//2
    i=(rowblock*bpr+b)*512 + widx(w,y)*4 + (x%2)*2
    return struct.unpack('<H',z0[i:i+2])[0]
for y in range(0,10,1):
    yy=y%8; rb=y//8
    print(y, ' '.join('%04x'%zval(x,yy,rb) for x in range(0,800,32)))
# bytes before Z: how much non-zero near the end
nz=[i for i in range(len(pre)) if pre[i]!=0]
print('pre-Z nonzero bytes', len(nz), 'last nonzero at -%x'%(len(pre)-nz[-1]) if nz else '')
print('pre-Z last 64 bytes', pre[-64:].hex())
