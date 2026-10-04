import re
import sys

path = sys.argv[1]
rows = {}
for line in open(path):
    m = re.match(r'(Gouraud\w*?)Pixels(\d+)=(\w+)', line.strip())
    if m:
        rows.setdefault(m.group(1), {})[int(m.group(2))] = [
            int(m.group(3)[i:i + 4], 16) for i in range(0, 128, 4)]

GUARD = 0xA5A5
x = rows['GouraudX']

# GouraudX: red 255 at x = 2.25 falling to 0 at x = 29.75; green rising.
# Model: colour evaluated at x + dx.  R8(x) = 255 * (29.75 - x) / 27.5.
def model(col, dx):
    xs = col + dx
    r = 255.0 * (29.75 - xs) / 27.5
    g = 255.0 * (xs - 2.25) / 27.5
    r = min(255.0, max(0.0, r))
    g = min(255.0, max(0.0, g))
    return r, g

print('row 20, per column: actual R5 G6 | reference(dx=0) R5 G6')
for col in range(32):
    p = x[20][col]
    if p == GUARD:
        continue
    r5, g6 = p >> 11, (p >> 5) & 63
    r, g = model(col, 0.0)
    print(col, r5, g6, '|', int(r) >> 3, int(g) >> 2)

# Per row: is the error the same on every row, or does it track where the
# row starts (the left edge is x = 2.25 + (y - 2.25) for the X triangle)?
best = None
for step in range(-64, 65):
    dx = step / 32.0
    err = 0
    n = 0
    for y, line in x.items():
        for col, p in enumerate(line):
            if p == GUARD:
                continue
            r, g = model(col, dx)
            err += abs((p >> 11) - (int(r) >> 3)) + abs(((p >> 5) & 63) - (int(g) >> 2))
            n += 1
    if best is None or err < best[1]:
        best = (dx, err, n)
print('best constant dx', best)

# Per-row best dx
for y in sorted(x):
    line = x[y]
    cols = [c for c, p in enumerate(line) if p != GUARD]
    if not cols:
        continue
    rb = None
    for step in range(-64, 65):
        dx = step / 32.0
        err = 0
        for col in cols:
            p = line[col]
            r, g = model(col, dx)
            err += abs((p >> 11) - (int(r) >> 3)) + abs(((p >> 5) & 63) - (int(g) >> 2))
        if rb is None or err < rb[1]:
            rb = (dx, err)
    print('row', y, 'first col', cols[0], 'best dx', rb)
