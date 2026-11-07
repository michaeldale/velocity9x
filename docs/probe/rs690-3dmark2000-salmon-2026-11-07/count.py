"""Salmon pixels per grab in each mask folder under the given run folder,
sampled every 2 pixels over the top 420 rows."""
import glob
import os
import struct
import sys


def salmon_count(path):
    data = open(path, 'rb').read()
    offset = struct.unpack('<I', data[10:14])[0]
    width, height = struct.unpack('<ii', data[18:26])
    stride = (width * 3 + 3) & ~3
    count = 0
    for y in range(0, 420, 2):
        row = offset + (height - 1 - y) * stride
        for x in range(0, width, 2):
            b, g, r = data[row + 3 * x], data[row + 3 * x + 1], data[row + 3 * x + 2]
            if r > 180 and r - g > 50 and r - b > 60:
                count += 1
    return count


for folder in sorted(glob.glob(os.path.join(sys.argv[1], 'mask*')),
                     key=lambda p: int(os.path.basename(p)[4:])):
    grabs = sorted(glob.glob(os.path.join(folder, 'V9XGRAB*.BMP')))
    counts = [salmon_count(g) for g in grabs]
    print(f'{os.path.basename(folder):8} grabs {len(grabs)} salmon {counts} total {sum(counts)}')
