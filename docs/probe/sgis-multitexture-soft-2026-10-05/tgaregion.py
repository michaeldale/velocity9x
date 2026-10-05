"""Channel differences of two 24-bit TGAs inside x0,y0,x1,y1 (top-down)."""
import struct
import sys


def read(path):
    data = open(path, 'rb').read()
    width, height = struct.unpack('<HH', data[12:16])
    return width, height, data[18 + data[0]:]


wa, ha, a = read(sys.argv[1])
_, _, b = read(sys.argv[2])
x0, y0, x1, y1 = (int(v) for v in sys.argv[3].split(','))
hist = {}
count = 0
for y in range(y0, y1):
    row = ha - 1 - y
    for x in range(x0, x1):
        i = (row * wa + x) * 3
        d = max(abs(a[i] - b[i]), abs(a[i + 1] - b[i + 1]), abs(a[i + 2] - b[i + 2]))
        hist[d] = hist.get(d, 0) + 1
        count += 1
print('region %s: %d pixels' % (sys.argv[3], count))
print('  ' + ', '.join('%d: %d' % (k, hist[k]) for k in sorted(hist)))
