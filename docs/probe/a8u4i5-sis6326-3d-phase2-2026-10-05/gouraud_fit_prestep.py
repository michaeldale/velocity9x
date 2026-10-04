import re
import sys

rows = {}
for line in open(sys.argv[1]):
    m = re.match(r'GouraudPixels(\d+)=(\w+)', line.strip())
    if m:
        rows[int(m.group(1))] = [int(m.group(2)[i:i + 4], 16)
                                 for i in range(0, 128, 4)]

V = [(36, 36), (476, 104), (136, 476)]
C = [(255, 0, 0), (0, 255, 0), (0, 0, 255)]


def edge(a, b, px, py):
    return (b[0] - a[0]) * (py - a[1]) - (b[1] - a[1]) * (px - a[0])


def ref(px, py):
    w = [edge(V[1], V[2], px, py), edge(V[2], V[0], px, py),
         edge(V[0], V[1], px, py)]
    area = sum(w)
    out = []
    for ch in range(3):
        s = sum(w[i] * C[i][ch] for i in range(3))
        out.append(int(s / area))
    return out


def judge(p, rgb):
    r, g, b = (max(0, min(255, v)) for v in rgb)
    return max(abs((p >> 11) - (r >> 3)), abs(((p >> 5) & 63) - (g >> 2)),
               abs((p & 31) - (b >> 3)))


# No x prestep: the span starts with the colour at the exact left edge
# crossing (long edge A-C, x = 2.25 + (y - 2.25) * 6.25 / 27.5) and steps by
# the true dC/dx from there.
for name, left in (('long edge', lambda y: 36 + (y * 16 - 36) * 100 / 440.0),):
    off = worst = n = 0
    for y, line in rows.items():
        cols = [x for x, p in enumerate(line) if p != 0xA5A5]
        if not cols:
            continue
        first = cols[0]
        xl = left(y)
        for x in cols:
            px = x * 16 - (first * 16 - xl)
            w = [edge(V[1], V[2], px, y * 16), edge(V[2], V[0], px, y * 16),
                 edge(V[0], V[1], px, y * 16)]
            area = sum(w)
            rgb = [int(sum(w[i] * C[i][ch] for i in range(3)) / area)
                   for ch in range(3)]
            e = judge(line[x], rgb)
            worst = max(worst, e)
            off += e > 0
            n += 1
    print('no-x-prestep', name, 'pixels', n, 'nonzero', off, 'worst', worst)

for dx16 in (0, -16, -8, 8, 16):
    off = 0
    worst = 0
    n = 0
    for y, line in rows.items():
        for x, p in enumerate(line):
            if p == 0xA5A5:
                continue
            r, g, b = (max(0, min(255, v)) for v in ref(x * 16 + dx16, y * 16))
            e = max(abs((p >> 11) - (r >> 3)), abs(((p >> 5) & 63) - (g >> 2)),
                    abs((p & 31) - (b >> 3)))
            worst = max(worst, e)
            off += e > 0
            n += 1
    print('dx', dx16 / 16.0, 'pixels', n, 'nonzero', off, 'worst', worst)
