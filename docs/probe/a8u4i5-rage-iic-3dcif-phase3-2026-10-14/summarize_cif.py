"""Summarise a V9XCIF.LOG from the Phase 3 ATI3DCIF.DLL, one block per process.

Usage: python -I summarize_cif.py V9XCIF.LOG [build-id]

For each attach (of build-id, if given): the last summary line, the
skipped-draw counts, the distinct refusal and approximation lines, the
textures not converted, and the pixel readbacks.
"""
import collections
import re
import sys


def body(line):
    return re.sub(r'^\d+ t=\d+ ', '', line)


def main():
    lines = open(sys.argv[1], encoding='latin-1').read().splitlines()
    build = sys.argv[2] if len(sys.argv) > 2 else None
    sections = []
    current = None
    for line in lines:
        m = re.search(r'attach build=(\S+) process=(.*)$', line)
        if m:
            current = None
            if build is None or m.group(1) == build:
                current = {'process': m.group(2), 'lines': []}
                sections.append(current)
            continue
        if current is not None:
            current['lines'].append(body(line))
    for sec in sections:
        text = sec['lines']
        print('=' * 72)
        print(sec['process'])
        summaries = [i for i, l in enumerate(text) if l.startswith('summary ')]
        if summaries:
            i = summaries[-1]
            print(' ', text[i])
            j = i + 1
            while j < len(text) and text[j].startswith('  '):
                if 'skipped' in text[j]:
                    print('  ', text[j].strip())
                j += 1
        groups = collections.OrderedDict()
        for l in text:
            for key in ('draw refused', '  format=', 'state drawn approximately',
                        'not converted', 'draw skipped', 'device:', 'hwtex'):
                if l.startswith(key) or l.startswith(key.strip()):
                    groups.setdefault(key, collections.Counter())[
                        re.sub(r'(frame|detail)=\S+', r'\1=*', l)] += 1
        for key, counter in groups.items():
            for l, n in list(counter.items())[:6]:
                print('  %4d x %s' % (n, l))
        pixels = [l for l in text if l.startswith('pixel ')]
        changed = 0
        for a, b in zip(pixels[0::2], pixels[1::2]):
            if a.split()[3] != b.split()[3]:
                changed += 1
        print('  pixel readbacks: %d pairs, %d changed by the draw' %
              (len(pixels) // 2, changed))


main()
