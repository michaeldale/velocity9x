"""Turn a driver V9XSIS3D.TXT stall log into a SIS3D /phase6 /file replay.

The current batch is replayed as d3d_sis6326.c emits it: idle wait, state,
texture words with D4 pulsed, idle wait, then per triangle an idle wait,
the primitive and 24 vertex writes, and a final wait. Every base moves up
by DELTA so the stream lands above the probe's 2 MiB floor; the colour, Z
and texture contents are filled, since the log does not hold them.

Variants (comma-separated in argv[3]):
  noz        Z test and write off
  nearest    texture filter nearest
  clamp      clamp addressing
  tq3        SR3C split 4K/28K (Turbo Queue itself stays off)
  delayN     N extra status reads before each triangle
  upto=N     only the first N triangles
  from=N     triangles from N on
  only=N     triangle N alone
  texdelta=HEX, zdelta=HEX  move the texture or Z buffer further
  z=HEX      every vertex's Z register
  pollN      after the last triangle, N tolerant idle waits
"""
import re
import sys

DELTA = 0x200000
STATE_BASES = {0x8A08, 0x8A18}
TEXTURE_BASES = {0x8A44, 0x8A48, 0x8A4C, 0x8A50, 0x8A54, 0x8A58, 0x8A5C,
                 0x8A60, 0x8A64, 0x8A68}


def parse(path):
    section = None
    sub = None
    batch = {'state': [], 'clear': [], 'texture': []}
    triangles = []
    pending = None
    for raw in open(path, encoding='ascii', errors='replace'):
        line = raw.strip()
        if line.startswith('['):
            section = line
            sub = None
            continue
        if section == '[Current]':
            if line in ('State', 'TextureClear', 'Texture', 'Vertices0'):
                sub = {'State': 'state', 'TextureClear': 'clear',
                       'Texture': 'texture', 'Vertices0': None}[line]
                continue
            m = re.match(r'Offset=([0-9A-F]+)', line)
            if m:
                pending = int(m.group(1), 16)
                continue
            m = re.match(r'Value=([0-9A-F]+)', line)
            if m and sub is not None:
                batch[sub].append((pending, int(m.group(1), 16)))
        elif section == '[CurrentTriangles]':
            m = re.match(r'Triangle=([0-9A-F]+)', line)
            if m:
                triangles.append({'primitive': None, 'writes': []})
                continue
            m = re.match(r'Primitive=([0-9A-F]+)', line)
            if m:
                triangles[-1]['primitive'] = int(m.group(1), 16)
                continue
            m = re.match(r'Offset=([0-9A-F]+)', line)
            if m:
                pending = int(m.group(1), 16)
                continue
            m = re.match(r'Value=([0-9A-F]+)', line)
            if m:
                triangles[-1]['writes'].append((pending, int(m.group(1), 16)))
    return batch, triangles


EXTRA = {'tex': 0, 'z': 0}


def moved(offset, value):
    if offset in TEXTURE_BASES:
        return value + DELTA + EXTRA['tex']
    if offset == 0x8A08 and value != 0:
        return value + DELTA + EXTRA['z']
    if offset in STATE_BASES and value != 0:
        return value + DELTA
    return value


def main():
    log, out = sys.argv[1], sys.argv[2]
    variants = sys.argv[3].split(',') if len(sys.argv) > 3 and sys.argv[3] else []
    batch, triangles = parse(log)
    state = dict(batch['state'])
    lines = ['# replay of ' + log.replace('\\', '/') + ' ' + ','.join(variants)]

    upto = len(triangles)
    start = 0
    delay = 0
    zbits = None
    for v in variants:
        if v.startswith('upto='):
            upto = int(v[5:])
        if v.startswith('from='):
            start = int(v[5:])
        if v.startswith('texdelta='):
            EXTRA['tex'] = int(v[9:], 16)
        if v.startswith('zdelta='):
            EXTRA['z'] = int(v[7:], 16)
        if v.startswith('z='):
            zbits = int(v[2:], 16)
        if v.startswith('only='):
            start = int(v[5:])
            upto = start + 1
        if v.startswith('delay'):
            delay = int(v[5:])

    enable = state[0x8A00]
    if 'noz' in variants:
        enable &= ~0x00300000
    texture_words = []
    for offset, value in batch['texture']:
        if offset == 0x8A38:
            if 'nearest' in variants:
                value &= ~0x0F
            if 'clamp' in variants:
                value = (value & ~0x00FF0000) | 0x00300000
        texture_words.append((offset, value))

    # Fills: target, Z and level 0, sized from the state and texture words.
    pitch = state[0x8A14] & 0x3FFF
    height = (state[0x8A30] & 0x1FFF) + 1
    lines.append('F %X 0 %X' % (state[0x8A18] + DELTA, pitch * height // 4))
    if state[0x8A08] != 0:
        zpitch = state[0x8A04] & 0x3FFF
        lines.append('F %X 7FFF7FFF %X' % (state[0x8A08] + DELTA + EXTRA['z'],
                                           zpitch * height // 4))
    tex = dict(texture_words)
    size = tex[0x8A80]
    w = 1 << ((size >> 28) & 0xF)
    h = 1 << ((size >> 24) & 0xF)
    texel = {0x50: 2, 0x51: 2, 0x52: 2, 0x53: 2, 0x73: 4}.get(tex[0x8A38] >> 24, 2)
    lines.append('F %X 7BEF39E7 %X' % (tex[0x8A44] + DELTA + EXTRA['tex'],
                                       w * h * texel // 4))

    if 'tq3' in variants:
        lines.append('S 3C 43')

    lines.append('I')
    for offset, value in batch['state']:
        if offset == 0x8A00:
            value = enable
        lines.append('W %X %X' % (offset, moved(offset, value)))
    for offset, value in texture_words:
        if offset == 0x8A38:
            value |= 0x10
        lines.append('W %X %X' % (offset, moved(offset, value)))
    for offset, value in texture_words:
        lines.append('W %X %X' % (offset, moved(offset, value)))
    for index, t in enumerate(triangles[start:upto]):
        lines.append('I')
        if delay:
            lines.append('D %X' % delay)
        lines.append('W 89F8 %X' % t['primitive'])
        for offset, value in t['writes']:
            if zbits is not None and (offset - 0x8800) % 0x20 == 4:
                value = zbits
            lines.append('W %X %X' % (offset, value))
    for v in variants:
        if v.startswith('poll'):
            lines.append('P %X' % int(v[4:]))
    lines.append('I')
    open(out, 'w', newline='\r\n').write('\n'.join(lines) + '\n')
    print('triangles', min(upto, len(triangles)) - start, 'lines', len(lines),
          'waits', sum(1 for l in lines if l == 'I'))


main()
