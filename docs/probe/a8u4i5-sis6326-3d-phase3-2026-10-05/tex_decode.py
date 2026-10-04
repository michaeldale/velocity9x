"""Decode index-texture dumps: each pixel is the word the engine fetched."""
import re
import sys
from collections import Counter

scenes = {}
for line in open(sys.argv[1]):
    m = re.match(r'(\w+?)Pixels(\d+)=(\w+)', line.strip())
    if m:
        scenes.setdefault(m.group(1), {})[int(m.group(2))] = [
            int(m.group(3)[i:i + 4], 16) for i in range(0, 128, 4)]

GUARD = 0xA5A5


def show(name, pitch_words, rows=range(0, 32, 4), cols=None):
    d = scenes[name]
    print('==', name)
    for y in rows:
        line = d[y]
        out = []
        for x, p in enumerate(line):
            if cols is not None and x not in cols:
                continue
            if p == GUARD:
                out.append('  .  ')
            else:
                out.append('%2d,%-2d' % (p // pitch_words, p % pitch_words)
                           if pitch_words else '%04X' % p)
        print('%2d' % y, ' '.join(out))


for name in sys.argv[2:]:
    pw = 256 if name == 'Prestep' else 32
    show(name, pw)
