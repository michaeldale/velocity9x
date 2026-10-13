"""Summarise a V9XCIF.LOG from the census ATI3DCIF.DLL, one section per process.

Usage: python -I analyze_cif.py V9XCIF.LOG

For each attach: the calls made (from the last summary), every distinct
state value set, the texture registrations (format, size, Rage PRO
fields), the draw targets, decoded first vertices, rewrites, and the
last lines before the process ended.
"""
import collections
import re
import struct
import sys

STATE_FMT = {'SURF_DRAW_PTR', 'SURF_Z_PTR', 'TMAP_SELECT', 'COMPOSITE_SELECT',
             'FG_CLR', 'SOLID_CLR'}


def f32(word):
    return struct.unpack('<f', struct.pack('<I', int(word, 16)))[0]


def sections(lines):
    current = None
    for line in lines:
        m = re.match(r'\d+ t=\d+ attach census build=\S+ process=(.*)$', line)
        if m:
            if current:
                yield current
            current = {'process': m.group(1), 'lines': []}
            continue
        if current is not None:
            current['lines'].append(line)
    if current:
        yield current


def body(line):
    return re.sub(r'^\d+ t=\d+ ', '', line)


def report(sec):
    out = []
    lines = [body(l) for l in sec['lines']]
    out.append('=' * 72)
    out.append('process ' + sec['process'])
    out.append('log lines %d' % len(lines))
    summaries = [i for i, l in enumerate(lines) if l.startswith('summary ')]
    if summaries:
        i = summaries[-1]
        out.append(lines[i])
        j = i + 1
        while j < len(lines) and lines[j].startswith('  '):
            out.append(lines[j])
            j += 1
    info = [l for l in lines if l.startswith('GetInfo size')]
    out.extend(sorted(set(info)))
    states = collections.OrderedDict()
    for l in lines:
        m = re.match(r'state hrc=\S+ (\S+?)(?:=(\S+) \((\d+)\)| (top=.*))$', l)
        if m:
            name = m.group(1)
            value = m.group(2) or m.group(4)
            states.setdefault(name, collections.OrderedDict())[value] = 1
    out.append('states (distinct values, in first-seen order):')
    for name, values in states.items():
        vals = list(values)
        if name in ('SURF_DRAW_PTR', 'SURF_Z_PTR', 'TMAP_SELECT', 'COMPOSITE_SELECT'):
            out.append('  %-24s %d distinct, e.g. %s' % (name, len(vals), ' '.join(vals[:4])))
        else:
            out.append('  %-24s %s' % (name, ' '.join(vals[:12]) + (' ...' if len(vals) > 12 else '')))
    regs = [l for l in lines if l.startswith('TextureReg htx') and 'fmt=' in l]
    shapes = collections.Counter()
    for l in regs:
        m = re.search(r'size=(\d+) mip=(\d+) levels=(\d+) (\d+x\d+) fmt=(\d+) chroma=(\S+) pal=(\S+)', l)
        if m:
            shapes[(m.group(1), m.group(5), m.group(4), m.group(2), m.group(3),
                    m.group(6) != '00000000', m.group(7) != '00000000')] += 1
    out.append('textures registered: %d' % len(regs))
    for key, n in sorted(shapes.items()):
        out.append('  size=%s fmt=%s %s mip=%s levels=%s chroma=%s pal=%s : %d' % (key + (n,)))
    rage3 = sorted(set(re.sub(r'htx=\S+ ', '', l) for l in lines if 'clampS=' in l))
    out.extend('  ' + l for l in rage3)
    pals = sorted(set(re.sub(r'htxpal=\S+ |first=.*', '', l) for l in lines if l.startswith('TexturePaletteCreate htxpal')))
    out.extend('  ' + l for l in pals)
    level0 = sorted(set(l.split('level0=')[1][:2] for l in regs if 'level0=' in l))
    out.append('texture level0 high bytes: ' + ' '.join(level0))
    rew = [l for l in lines if 'rewritten' in l]
    out.append('texture rewrites logged: %d' % len(rew))
    out.extend('  ' + l for l in rew[:5])
    targets = collections.Counter(re.sub(r'hrc=\S+ ', '', l) for l in lines if l.startswith('target '))
    out.append('draw targets (at RenderBegin of logged frames):')
    for t, n in targets.most_common(8):
        out.append('  %4d  %s' % (n, t))
    verts = [l for l in lines if re.match(r'(strip|list|mesh) vertex \d+ [0-9A-F]{8}', l)]
    out.append('vertex samples: %d; first decoded:' % len(verts))
    for l in verts[:6]:
        words = l.split()[3:]
        if len(words) == 10:
            vals = [f32(w) for w in words]
            out.append('  VTCF x=%.2f y=%.2f z=%.4f s=%.4f t=%.4f w=%.4f r=%.1f g=%.1f b=%.1f a=%.1f' % tuple(vals))
        elif len(words) == 11:
            v = [f32(w) for w in words[:4]]
            out.append('  TLV x=%.2f y=%.2f z=%.4f rhw=%.4f color=%s spec=%s u=%.4f v=%.4f c.rhw=%.4f c.u=%.4f c.v=%.4f'
                       % (v[0], v[1], v[2], v[3], words[4], words[5], f32(words[6]), f32(words[7]),
                          f32(words[8]), f32(words[9]), f32(words[10])))
    odd = sorted(set(l for l in lines if re.search(r'BADPARAM|refused|outside|unreadable|not registered|NOTIMPYET|pool full', l)))
    out.append('anomalies: %d distinct' % len(odd))
    out.extend('  ' + l for l in odd[:12])
    out.append('last lines:')
    out.extend('  ' + l for l in lines[-6:])
    return out


def main():
    lines = open(sys.argv[1], encoding='latin-1').read().splitlines()
    for sec in sections(lines):
        print('\n'.join(report(sec)))


main()
