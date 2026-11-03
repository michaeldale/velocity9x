"""Judge V9XN32D /d3dstate dumps against models.

Usage: d3dstate_models.py <folder> [<folder to compare>]
"""
import math
import struct
import sys
from fractions import Fraction as F
from pathlib import Path

LINES = 16
WIDTH = 128


def load(path):
    data = Path(path).read_bytes()
    words = struct.unpack('<%dH' % (len(data) // 2), data)
    colour = [words[y * WIDTH:(y + 1) * WIDTH] for y in range(LINES)]
    base = LINES * WIDTH
    zeta = [words[base + y * WIDTH:base + (y + 1) * WIDTH]
            for y in range(LINES)]
    return colour, zeta


def drawn(colour, background=0):
    return {(x, y) for y in range(LINES) for x in range(WIDTH)
            if colour[y][x] != background}


def edge_set(vertices, offset):
    """Top-left rule over sample points (x + offset, y + offset).

    The triangle is (a, b, c) with a top-left, b top-right, c bottom-left:
    top edge a-b and left edge a-c are inclusive, the diagonal b-c is not.
    """
    (ax, ay), (bx, by), (cx, cy) = vertices
    out = set()
    for y in range(LINES):
        for x in range(WIDTH):
            px = x + offset
            py = y + offset
            if py < ay or px < ax:
                continue
            # Diagonal from b to c: inside where the cross product is
            # strictly positive on the a side.
            side = (cx - bx) * (py - by) - (cy - by) * (px - bx)
            a_side = (cx - bx) * (ay - by) - (cy - by) * (ax - bx)
            if side * a_side > 0:
                out.add((x, y))
    return out


def edge_models(frac):
    base = [(8, 2), (72, 2), (8, 14)]
    exact = [(F(x) + frac, F(y) + frac) for x, y in base]
    floor = [(F(math.floor(x)), F(math.floor(y))) for x, y in exact]
    near = [(F(math.floor(x + F(1, 2))), F(math.floor(y + F(1, 2))))
            for x, y in exact]
    return {
        'integer centres, exact vertices': edge_set(exact, F(0)),
        'integer centres, floored vertices': edge_set(floor, F(0)),
        'integer centres, rounded vertices': edge_set(near, F(0)),
        'half centres, exact vertices': edge_set(exact, F(1, 2)),
    }


def main():
    folder = Path(sys.argv[1])
    for number, frac in ((5, F(1, 2)), (13, F(1, 4)), (14, F(3, 4))):
        path = folder / ('N3D%02d.BIN' % number)
        if not path.exists():
            continue
        colour, _ = load(path)
        got = drawn(colour)
        print('N3D%02d vertices +%s: %d pixels' % (number, frac, len(got)))
        for name, model in edge_models(frac).items():
            print('   %-36s %4d predicted, %4d differ' %
                  (name, len(model), len(model ^ got)))
    if len(sys.argv) > 2:
        other = Path(sys.argv[2])
        for path in sorted(folder.glob('N3D*.BIN')):
            twin = other / path.name
            if twin.exists():
                same = path.read_bytes() == twin.read_bytes()
                print('%s %s' % (path.name, 'same' if same else 'DIFFERS'))


main()
