import os, re, sys
# halmap_statics.py v9xhal.map wdis-dir out.map
#
# The HAL's map lists public symbols only, so report-profile.ps1 files a
# static function's samples under the public symbol before it. wdis still
# names every function in an object, statics included. Each module's base
# in the image is a public symbol's map address less its offset in that
# object's _TEXT; every other label of the object is placed from it. The
# output is the map with those labels added, in the format report-profile
# reads.

map_path, wdis_dir, out_path = sys.argv[1:4]
lines = open(map_path, encoding='latin-1').read().splitlines()

modules = []          # (header line index, object name, {name: address})
current = None
for i, line in enumerate(lines):
    m = re.match(r'^Module: .*\\([^\\(]+)\.obj\(', line)
    if m:
        current = (i, m.group(1), {})
        modules.append(current)
        continue
    m = re.match(r'^([0-9a-f]{8})[*+ ]\s+(\S+)', line)
    if m and current is not None:
        current[2][m.group(2)] = int(m.group(1), 16)


def text_labels(obj):
    path = os.path.join(wdis_dir, obj + '.txt')
    if not os.path.exists(path):
        return {}
    labels, in_text = {}, False
    for line in open(path, encoding='latin-1'):
        if line.startswith('Segment:'):
            in_text = line.split()[1] == '_TEXT'
            continue
        m = re.match(r'^([0-9A-F]{4,8})\s+([A-Za-z_][A-Za-z0-9_@$]*):$', line.rstrip())
        if in_text and m and not m.group(2).startswith('L$'):
            labels[m.group(2)] = int(m.group(1), 16)
    return labels


added = {}
for index, obj, publics in modules:
    labels = text_labels(obj)
    base = None
    for name, address in publics.items():
        if name in labels:
            base = address - labels[name]
            break
    if base is None:
        continue
    extra = [(base + off, name) for name, off in labels.items() if name not in publics]
    if extra:
        added[index] = sorted(extra)

out = []
total = 0
for i, line in enumerate(lines):
    out.append(line)
    if i in added:
        for address, name in added[i]:
            out.append(f'{address:08x}       {name}')
            total += 1
open(out_path, 'w', encoding='latin-1').write('\n'.join(out) + '\n')
print(f'{total} static labels added across {len(added)} modules')
