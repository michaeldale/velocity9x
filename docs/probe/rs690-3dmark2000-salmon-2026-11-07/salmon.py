"""Find the salmon pixels in the grabs: count, a histogram of their colours,
and their neighbours just below (the terrain they sit on). BMP 24-bit,
bottom-up rows."""
import struct
import sys
from collections import Counter

folder = sys.argv[1]


def load(path):
    data = open(path, 'rb').read()
    offset = struct.unpack('<I', data[10:14])[0]
    width, height = struct.unpack('<ii', data[18:26])
    stride = (width * 3 + 3) & ~3
    def pixel(x, y):
        at = offset + (height - 1 - y) * stride + x * 3
        b, g, r = data[at], data[at + 1], data[at + 2]
        return r, g, b
    return width, height, pixel


def salmon(p):
    r, g, b = p
    return r > 180 and r - g > 50 and r - b > 60


best = None
for i in range(10):
    w, h, px = load(f'{folder}/V9XGRAB{i}.BMP')
    hits = [(x, y) for y in range(0, 420, 2) for x in range(0, w, 2) if salmon(px(x, y))]
    print(f'grab {i}: {len(hits)} salmon samples')
    if best is None or len(hits) > best[1]:
        best = (i, len(hits), hits, px)

i, n, hits, px = best
print(f'best grab {i}')
colours = Counter(tuple(c >> 3 << 3 for c in px(x, y)) for x, y in hits)
print('salmon colours (quantised 8):', colours.most_common(8))
# The pixel 6 rows below each hit that is not salmon: the terrain under it.
below = Counter()
for x, y in hits[::5]:
    for dy in range(2, 30, 2):
        if y + dy < 768 and not salmon(px(x, y + dy)):
            below[tuple(c >> 4 << 4 for c in px(x, y + dy))] += 1
            break
print('neighbours below:', below.most_common(8))
ys = sorted(y for x, y in hits)
print('rows', ys[0], 'to', ys[-1])
