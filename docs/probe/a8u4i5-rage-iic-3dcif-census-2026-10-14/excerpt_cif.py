"""Cut a V9XCIF.LOG down to a committable excerpt.

Usage: python -I excerpt_cif.py V9XCIF.LOG > excerpt.log

Per process (each 'attach' line): its first HEAD lines, which hold the
set-up, the first captured frames and the first texture registrations,
then its last summary block. The full logs (14.5 MB) are not committed;
analysis-*.txt was produced from them by analyze_cif.py.
"""
import re
import sys

HEAD = 300

lines = open(sys.argv[1], encoding='latin-1').read().splitlines()
starts = [i for i, l in enumerate(lines) if re.search(r' attach census build=', l)]
starts.append(len(lines))
for a, b in zip(starts, starts[1:]):
    section = lines[a:b]
    out = section[:HEAD]
    summary = max((i for i, l in enumerate(section) if ' summary ' in l), default=None)
    if summary is not None and summary >= HEAD:
        out.append('... %d lines omitted ...' % (summary - HEAD))
        j = summary + 1
        while j < len(section) and re.match(r'\d+ t=\d+   ', section[j]):
            j += 1
        out.extend(section[summary:j])
    print('\n'.join(out))
