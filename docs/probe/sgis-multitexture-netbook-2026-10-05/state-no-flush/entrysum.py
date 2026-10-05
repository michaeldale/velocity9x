"""Sum the ICD's ten-second lines over the last session in a V9XGL.LOG.

The last session is everything after the final 'attach' line for that
process id (Win9x reuses ids, so a pid alone spans several runs). Only
intervals with at least `min_frames` swaps are summed: the demos, not the
menu. Prints per-frame milliseconds for each bucket of the tsc, sink and
entry lines.
"""
import re
import sys

path = sys.argv[1]
min_frames = int(sys.argv[2]) if len(sys.argv) > 2 else 100
lines = open(path, encoding='latin-1').read().splitlines()
attach = max(i for i, l in enumerate(lines) if ' attach ' in l)
pid = re.search(r'pid=([0-9A-F]+)', lines[attach]).group(1)
session = [l for l in lines[attach:] if 'pid=' + pid in l]

frames = 0
totals = {}
calls = {}
take = False
for line in session:
    body = line.split(' ', 2)[2] if line.count(' ') >= 2 else ''
    if body.startswith('tsc '):
        f = int(re.search(r'frames=(\d+)', body).group(1))
        take = f >= min_frames
        if take:
            frames += f
    if not take:
        continue
    if body.startswith(('tsc ', 'sink ', 'entry ')):
        tag = body.split(' ', 1)[0]
        for key, value, count in re.findall(r'([a-z-]+)=(\d+)(?:/(\d+))?', body):
            name = tag + ':' + key
            totals[name] = totals.get(name, 0) + int(value)
            if count:
                calls[name] = calls.get(name, 0) + int(count)
print('session pid %s, %d frames in demo intervals' % (pid, frames))
for name in sorted(totals):
    if name.endswith('-ms'):
        extra = ''
        if name in calls:
            extra = '  %8.1f calls/frame  %6.2f us/call' % (
                calls[name] / frames,
                1000.0 * totals[name] / max(calls[name], 1))
        print('  %-26s %7.2f ms/frame%s' % (name, totals[name] / frames, extra))
    elif name in ('tsc:vertices', 'tsc:draws', 'tsc:sinks', 'tsc:flushes'):
        print('  %-26s %7.1f per frame' % (name, totals[name] / frames))
