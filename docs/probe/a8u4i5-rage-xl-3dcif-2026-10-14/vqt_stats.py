"""Header and codebook statistics of ATI .vqt files: sizes, and whether the
codebook words look like ARGB1555 (top bit constant) or RGB565."""
import struct
import sys


def stats(path):
    data = open(path, 'rb').read()
    tag, height, width, r0, r1 = struct.unpack('<5I', data[:20])
    words = struct.unpack('<1024H', data[20:20 + 2048])
    indices = len(data) - 20 - 2048
    top = sum(1 for w in words if w & 0x8000)
    # In 565 the top bit is red's MSB; in 1555 it is alpha. Compare the
    # green fields' smoothness under each reading: neighbouring texels in a
    # 2x2 block are similar, so the right reading has smaller differences.
    def diffs(fields):
        total = 0
        for e in range(256):
            block = words[e * 4:e * 4 + 4]
            vals = [fields(w) for w in block]
            total += max(vals) - min(vals)
        return total / 256.0
    g565 = diffs(lambda w: (w >> 5) & 0x3f) / 63.0
    g555 = diffs(lambda w: (w >> 5) & 0x1f) / 31.0
    r565 = diffs(lambda w: (w >> 11) & 0x1f) / 31.0
    r555 = diffs(lambda w: (w >> 10) & 0x1f) / 31.0
    print('%s: tag=%08X header h=%u w=%u res=%u,%u index bytes=%u (%s)' % (
        path, tag, height, width, r0, r1, indices,
        'h*w' if indices == height * width else 'not h*w'))
    print('  top bit set in %u of 1024 words; first words %s' % (
        top, ' '.join('%04X' % w for w in words[:8])))
    print('  mean block spread: green 565 %.3f / 555 %.3f, red 565 %.3f / 555 %.3f'
          % (g565, g555, r565, r555))


for p in sys.argv[1:]:
    stats(p)
