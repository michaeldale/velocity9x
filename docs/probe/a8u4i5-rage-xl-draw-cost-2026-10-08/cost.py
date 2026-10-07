import re
import sys


def load(path):
    values = {}
    for line in open(path, encoding='latin-1'):
        match = re.match(r'^(\w+)=(.*)$', line.strip())
        if match:
            values[match.group(1)] = match.group(2)
    return values


def num(text):
    return int(text, 16) if text.startswith('0x') else int(text)


before = load(sys.argv[1])
after = load(sys.argv[2])


def pair(d, key):
    return num(d[key + 'Lo']) + (num(d[key + 'Hi']) << 32)


def delta(key):
    return num(after[key]) - num(before[key])


total = pair(after, 'R3dDrawCycles') - pair(before, 'R3dDrawCycles')
calls = delta('R3dDrawCalls')
batches = delta('R2Batches')
tris = delta('R2Pieces')
writes = delta('R2Writes')
reads = delta('R2FifoReads')
wall = delta('DumpUptimeMs')
print('wall ms %d, r3d calls %d, batches %d, triangles %d, triangles/batch %.1f'
      % (wall, calls, batches, tris, tris / batches))
print('r3d cycles %.4g, per call %.0f' % (total, total / calls))
parts = {}
for key in ['Prepare', 'Split', 'Build', 'Emit']:
    parts[key] = pair(after, 'R2Cycles' + key) - pair(before, 'R2Cycles' + key)
inside = sum(parts.values())
for key, value in parts.items():
    print('%-8s %5.1f%% of r3d, %7.0f cycles a batch, %6.0f a triangle'
          % (key, 100.0 * value / total, value / batches, value / tris))
print('outside the engine draw %5.1f%%, %7.0f cycles a call'
      % (100.0 * (total - inside) / total, (total - inside) / calls))
print('register writes a batch %.1f, FIFO status reads a batch %.1f, reads a write %.2f'
      % (writes / batches, reads / batches, reads / writes))
