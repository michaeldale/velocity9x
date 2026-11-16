import sys,struct
from PIL import Image
d=open(sys.argv[1],'rb').read()
h=struct.unpack('<16I',d[:64]); pitch=h[6]; W=h[3]; H=h[4]
zl=d[64:64+0x10000]; c0=d[64+0x10000:64+0x20000]; cend=d[64+0x20000:64+0x40000]
o=64+0x40000; top=d[o:o+pitch*8]; bot=d[o+pitch*8:o+pitch*16]
# word index within 512-byte block for (dx,y)
def widx(dx,y):
    g = 0 if dx<8 else 1
    sub = dx%8
    return (y//4)*64 + g*32 + ((y%4)//2)*16 + (sub//2)*4 + (sub%2) + (y%2)*2
def tiledrow(buf,base_block,y,x):
    b=x//16; i=(base_block+b)*512 + widx(x%16,y)*4; return buf[i:i+4]
blocks_per_row=(W*4+63)//64
def img_from(buf, block0, rows=8):
    im=Image.new('RGB',(W,rows))
    for y in range(rows):
        for x in range(W):
            v=tiledrow(buf,block0,y,x); im.putpixel((x,y),(v[2],v[1],v[0]))
    return im
def img_lin(buf):
    im=Image.new('RGB',(W,8))
    for y in range(8):
        for x in range(W):
            i=y*pitch+x*4; v=buf[i:i+4]; im.putpixel((x,y),(v[2],v[1],v[0]))
    return im
a=img_from(c0,0); b=img_lin(top)
# last block row of colour: offset of block row (H/8-1) relative to cend start
colour=h[2]; cend_start=(h[10]-0x10000)&~0xffff
lastrow_off=colour+((H+7)//8-1)*blocks_per_row*512 - cend_start
c=img_from(cend,lastrow_off//512); e=img_lin(bot)
# match counts
def same(p,q,x0,x1): return sum(p.getpixel((x,y))==q.getpixel((x,y)) for x in range(x0,x1) for y in range(8))
print('top: shadow vs backbuf, x0-304:',same(a,b,0,304),'/',304*8,' x304-W:',same(a,b,304,W),'/',(W-304)*8)
print('bottom: shadow vs backbuf, all:',same(c,e,0,W),'/',W*8)
sheet=Image.new('RGB',(W,4*10),(255,0,255))
for i,im in enumerate((b,a,e,c)): sheet.paste(im,(0,i*10))
sheet.resize((W*2,80*2),Image.NEAREST).save(sys.argv[2])
