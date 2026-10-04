"""Fit the Persp and Linear dumps of a phase 3 run."""
import math
import re
import sys

scenes = {}
for line in open(sys.argv[1]):
    m = re.match(r'(\w+?)Pixels(\d+)=(\w+)', line.strip())
    if m:
        scenes.setdefault(m.group(1), {})[int(m.group(2))] = [
            int(m.group(3)[i:i + 4], 16) for i in range(0, 128, 4)]

# Persp: 32x32 index texture, 64-byte pitch, clamp. Vertices at x = 0 and
# 32 (shifted 1/256 up-left), RHW 1.0 and 0.25, U 0.5/32 and 1 + 0.5/32.
d = scenes['Persp']
uL, uR = 0.5 / 32, 1 + 0.5 / 32
rL, rR = 1.0, 0.25
S = 1 / 256


def clamp(t):
    return max(0, min(31, t))


def models(x):
    t = (x + S) / 32.0
    out = {}
    out['affine'] = uL + (uR - uL) * t
    # Direct3D: U*RHW and RHW linear in screen space
    out['d3d rhw'] = (uL * rL * (1 - t) + uR * rR * t) / (rL * (1 - t) + rR * t)
    # W taken as w (eye depth), so 1/W is what interpolates
    wl, wr = 1 / rL, 1 / rR
    out['w as depth'] = (uL / rL * (1 - t) + uR / rR * t) / (1 / rL * (1 - t) + 1 / rR * t)
    return out


scores = {}
for y, line in d.items():
    for x, p in enumerate(line):
        col = p % 32
        for k, u in models(x).items():
            scores.setdefault(k, 0)
            scores[k] += col == clamp(math.floor(u * 32))
print('Persp columns right of 1024:', scores)
print('row 0 columns:', [p % 32 for p in d[0]])
print('row 0 d3d    :', [clamp(math.floor(models(x)['d3d rhw'] * 32)) for x in range(32)])
print('row 0 wdepth :', [clamp(math.floor(models(x)['w as depth'] * 32)) for x in range(32)])
print('row 31 cols  :', [p % 32 for p in d[31]])

for name in ('LinearU', 'LinearV'):
    d = scenes[name]
    if name == 'LinearU':
        line = [p >> 11 for p in d[16]]
    else:
        line = [d[y][16] >> 11 for y in range(32)]
    print(name, 'R5 along the axis:', line)
