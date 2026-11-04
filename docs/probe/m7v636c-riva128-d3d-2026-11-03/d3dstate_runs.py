"""Run-length dump of V9XN32D /d3dstate N3Dnn.BIN files.

Each file: 16 lines x 128 pixels of colour (16-bit LE), then the same of
zeta. Prints, per line, runs "x0-x1:value" for colour and zeta.
"""
import struct
import sys
from pathlib import Path

LINES = 16
WIDTH = 128


def load(path):
    data = Path(path).read_bytes()
    words = struct.unpack('<%dH' % (len(data) // 2), data)
    colour = [words[y * WIDTH:(y + 1) * WIDTH] for y in range(LINES)]
    base = LINES * WIDTH
    zeta = [words[base + y * WIDTH:base + (y + 1) * WIDTH] for y in range(LINES)]
    return colour, zeta


def runs(row, fmt):
    out = []
    start = 0
    for x in range(1, WIDTH + 1):
        if x == WIDTH or row[x] != row[start]:
            out.append('%d-%d:%s' % (start, x - 1, fmt(row[start])))
            start = x
    return ' '.join(out)


def texel(p):
    r = (p >> 10) & 31
    g = (p >> 5) & 31
    b = p & 31
    if b or r % 4 or g % 4:
        return '%04X' % p
    return 's%d' % (r // 4 + 8 * (g // 4))


def main():
    folder = Path(sys.argv[1])
    mode = sys.argv[2] if len(sys.argv) > 2 else 'hex'
    for path in sorted(folder.glob('N3D*.BIN')):
        colour, zeta = load(path)
        print('==', path.name)
        fmt = texel if mode == 'texel' else (lambda p: '%04X' % p)
        for y in range(LINES):
            print(' c%2d %s' % (y, runs(colour[y], fmt)))
        if mode != 'texel':
            for y in range(LINES):
                print(' z%2d %s' % (y, runs(zeta[y], lambda p: '%04X' % p)))


main()
