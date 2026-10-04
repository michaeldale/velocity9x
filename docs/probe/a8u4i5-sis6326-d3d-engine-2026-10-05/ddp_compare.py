"""Compare two V9XDDP reports key by key: pass/fail keys first."""
import re
import sys


def load(path):
    out = {}
    for line in open(path, errors='replace'):
        line = line.strip()
        if '=' in line and not line.startswith('['):
            k, v = line.split('=', 1)
            out[k] = v
    return out


base = load(sys.argv[1])
ours = load(sys.argv[2])
okkeys = [k for k in ours if re.search(r'(Ok|Pass|Match)$', k)]
both_pass = both_fail = 0
regress = []
improve = []
for k in okkeys:
    b = base.get(k)
    o = ours[k]
    if b is None:
        continue
    if b == o:
        if o in ('1', 'PASS'):
            both_pass += 1
        else:
            both_fail += 1
    elif b in ('1', 'PASS'):
        regress.append(k)
    else:
        improve.append(k)
print('ours keys', len(ours), 'base keys', len(base))
print('compared', len(okkeys), 'both pass', both_pass, 'both fail', both_fail)
print('PASS under SiS, not ours (%d):' % len(regress))
for k in regress:
    print('  ', k, 'sis=', base[k], 'ours=', ours[k])
print('ours pass, SiS not (%d):' % len(improve))
for k in improve[:40]:
    print('  ', k, 'sis=', base[k], 'ours=', ours[k])
missing = [k for k in base if k not in ours]
print('keys only in SiS run (not reached):', len(missing))
print('  first:', missing[:8])
