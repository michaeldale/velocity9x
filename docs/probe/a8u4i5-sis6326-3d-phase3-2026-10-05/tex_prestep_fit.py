"""Fit where the engine evaluates U, from the Prestep index-texture dump.

Texture 256 x 32 at a 512-byte pitch: index = row * 256 + column. The
vertices were shifted up-left by 1/256 px with U and V kept, so on screen
texel u = 8 * (X + 1/256) + 4 and v = (Y + 1/256) + 0.5.
"""
import math
import re
import sys
from fractions import Fraction as F

rows = {}
for line in open(sys.argv[1]):
    m = re.match(r'PrestepPixels(\d+)=(\w+)', line.strip())
    if m:
        rows[int(m.group(1))] = [int(m.group(2)[i:i + 4], 16)
                                 for i in range(0, 128, 4)]

S = F(1, 256)
A = (F(36, 16) - S, F(36, 16) - S)
C = (F(136, 16) - S, F(476, 16) - S)


def tu(X):
    return 8 * (X + S) + 4


def tv(Y):
    return (Y + S) + F(1, 2)


def x_left(y):
    # long edge A-C (the middle vertex B is right of it)
    return A[0] + (C[0] - A[0]) * (y - A[1]) / (C[1] - A[1])


counts = {'prestep': 0, 'no-x-prestep': 0}
v_ok = 0
n = 0
for y, line in sorted(rows.items()):
    cols = [x for x, p in enumerate(line) if p != 0xA5A5]
    if not cols:
        continue
    x0 = cols[0]
    xl = x_left(F(y))
    for x in cols:
        p = line[x]
        row, col = divmod(p, 256)
        n += 1
        v_ok += row == math.floor(tv(F(y)))
        counts['prestep'] += col == math.floor(tu(F(x)))
        counts['no-x-prestep'] += col == math.floor(tu(xl) + 8 * (x - x0))
print('pixels', n, 'v rows right', v_ok)
for k, v in counts.items():
    print(k, 'columns right', v)
# Per-row offset of the measured column from the correct-prestep column
for y, line in sorted(rows.items()):
    cols = [x for x, p in enumerate(line) if p != 0xA5A5]
    if not cols:
        continue
    x0 = cols[0]
    d = set((line[x] % 256) - math.floor(tu(F(x))) for x in cols)
    print('row %2d x0 %2d frac %.3f  col - prestep %s  model %d' % (
        y, x0, float(x0 - x_left(F(y))), sorted(d),
        math.floor(tu(x_left(F(y))) + 0) - math.floor(tu(F(x0)))))
