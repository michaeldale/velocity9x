"""Count Quake 3 frames with the top-rows garbage: a run of 20 or more
pure-white pixels in the frame's first 12 rows (glyphs are 8 wide, so text
never makes one). Reads Quake 3's own 24-bit TGAs and the agent's window
PNGs (client area at x 3, y 22 for the 640x480 window)."""
import struct
import sys
import zlib
from pathlib import Path


def tga_rows(path):
    d = path.read_bytes()
    idl = d[0]
    w, h = struct.unpack('<HH', d[12:16])
    bp = d[16] // 8
    desc = d[17]
    px = d[18 + idl:]
    rows = []
    for y in range(12):
        sy = y if (desc & 0x20) else h - 1 - y
        row = px[sy * w * bp:(sy + 1) * w * bp]
        rows.append([tuple(row[x * bp:x * bp + 3]) for x in range(w)])
    return rows


def png_rows(path, x0=3, y0=22, w=640):
    d = path.read_bytes()
    pos = 8
    idat = b''
    width = height = 0
    ctype = 2
    while pos < len(d):
        n = struct.unpack('>I', d[pos:pos + 4])[0]
        kind = d[pos + 4:pos + 8]
        body = d[pos + 8:pos + 8 + n]
        if kind == b'IHDR':
            width, height, _, ctype = struct.unpack('>IIBB', body[:10])
        elif kind == b'IDAT':
            idat += body
        pos += 12 + n
    bpp = {2: 3, 6: 4}[ctype]
    raw = zlib.decompress(idat)
    stride = width * bpp
    out = []
    prev = bytearray(stride)
    i = 0
    for y in range(height):
        f = raw[i]
        line = bytearray(raw[i + 1:i + 1 + stride])
        i += 1 + stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if f == 1:
                line[x] = (line[x] + a) & 255
            elif f == 2:
                line[x] = (line[x] + b) & 255
            elif f == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 255
        prev = line
        if y0 <= y < y0 + 12:
            out.append([tuple(line[(x0 + x) * bpp:(x0 + x) * bpp + 3])
                        for x in range(w)])
        if y >= y0 + 12:
            break
    return out


def garbage(rows):
    for row in rows:
        run = 0
        for p in row:
            run = run + 1 if p == (255, 255, 255) else 0
            if run >= 20:
                return True
    return False


for folder in sys.argv[1:]:
    hits = []
    for f in sorted(Path(folder).glob('tga/*.tga')):
        hits.append((f.name, garbage(tga_rows(f))))
    for f in sorted(Path(folder).glob('shot[0-9].png')):
        hits.append((f.name, garbage(png_rows(f))))
    bad = sum(1 for _, g in hits if g)
    print(f'{folder}: {bad} of {len(hits)} frames with top garbage  ' +
          ' '.join(f'{n}={"X" if g else "."}' for n, g in hits))
