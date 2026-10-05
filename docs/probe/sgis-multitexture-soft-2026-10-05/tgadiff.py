"""Compare two uncompressed 24-bit TGA screenshots of the same view.

Prints the per-channel difference histogram and how many pixels differ by
more than a threshold, and writes PNGs of both frames and an amplified
difference image beside the first file.
"""
import struct
import sys
import zlib


def read_tga(path):
    data = open(path, 'rb').read()
    id_length, _, image_type = data[0], data[1], data[2]
    width, height = struct.unpack('<HH', data[12:16])
    depth = data[16]
    if image_type != 2 or depth != 24:
        sys.exit('%s: type %d depth %d not handled' % (path, image_type, depth))
    pixels = data[18 + id_length:18 + id_length + width * height * 3]
    return width, height, pixels


def write_png(path, width, height, rgb_rows):
    raw = b''.join(b'\x00' + row for row in rgb_rows)
    def chunk(tag, body):
        return (struct.pack('>I', len(body)) + tag + body +
                struct.pack('>I', zlib.crc32(tag + body) & 0xffffffff))
    png = (b'\x89PNG\r\n\x1a\n' +
           chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)) +
           chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))
    open(path, 'wb').write(png)


def rows_rgb(width, height, bgr):
    rows = []
    for y in range(height - 1, -1, -1):   # TGA rows are bottom-up
        line = bgr[y * width * 3:(y + 1) * width * 3]
        out = bytearray(len(line))
        out[0::3] = line[2::3]
        out[1::3] = line[1::3]
        out[2::3] = line[0::3]
        rows.append(bytes(out))
    return rows


a_path, b_path, out_prefix = sys.argv[1], sys.argv[2], sys.argv[3]
wa, ha, a = read_tga(a_path)
wb, hb, b = read_tga(b_path)
if (wa, ha) != (wb, hb):
    sys.exit('sizes differ')
hist = {}
over = {4: 0, 8: 0, 16: 0, 32: 0}
diff = bytearray(len(a))
identical = 0
for i in range(0, len(a), 3):
    d = max(abs(a[i] - b[i]), abs(a[i + 1] - b[i + 1]), abs(a[i + 2] - b[i + 2]))
    hist[d] = hist.get(d, 0) + 1
    if d == 0:
        identical += 1
    for t in over:
        if d > t:
            over[t] += 1
    v = min(255, d * 8)
    diff[i] = diff[i + 1] = diff[i + 2] = v
total = len(a) // 3
print('pixels %d, identical %d (%.1f%%)' % (total, identical, 100.0 * identical / total))
for t in sorted(over):
    print('  max channel difference over %d: %d (%.2f%%)' % (t, over[t], 100.0 * over[t] / total))
print('  histogram (difference: pixels):',
      ', '.join('%d: %d' % (k, hist[k]) for k in sorted(hist)[:24]))
write_png(out_prefix + '-a.png', wa, ha, rows_rgb(wa, ha, a))
write_png(out_prefix + '-b.png', wa, ha, rows_rgb(wa, ha, b))
write_png(out_prefix + '-diff8x.png', wa, ha, rows_rgb(wa, ha, bytes(diff)))
