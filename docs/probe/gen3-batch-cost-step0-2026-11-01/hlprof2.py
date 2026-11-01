import re, sys
# hlprof2.py pre.ini post.ini : the 2026-10-01 hlprof.py, reading decimal
# values with leading zeros, and splitting the asynchronous path's waits
# (render drain, ring space) as well as the old head wait.
HZ = 1662e6


def load(f):
    d = {}
    for l in open(f, encoding='latin-1'):
        m = re.match(r'^(\w+)=(0x[0-9A-Fa-f]+|-?\d+)\s*$', l)
        if m:
            v = m.group(2)
            d[m.group(1)] = (int(v, 16) if v.lower().startswith('0x') else int(v, 10)) & 0xffffffff
    return d


a, b = load(sys.argv[1]), load(sys.argv[2])
wall = (b['DumpUptimeMs'] - a['DumpUptimeMs']) / 1000.0
dd = lambda k: (b.get(k, 0) - a.get(k, 0)) & 0xffffffff
pair = lambda d, k: (d.get(k + 'Hi', 0) << 32) | d.get(k + 'Lo', 0)
dp = lambda k: pair(b, k) - pair(a, k)
sec = lambda k: dp(k + 'Cycles') / HZ

subs = dd('I9xxDrawsSubmitted')
print(f"window {wall:.1f} s, {subs} submissions ({subs/max(wall,1e-9):.0f}/s)")
rows = [('D3D entries', 'TimeD3dCalls'), ('render-interface draw entry', 'R3dDraw'),
        ('engine draw', 'TimeEngineDraw'), ('  wait for GPU (head)', 'TimeRingWait'),
        ('  ring space', 'TimeRingSpaceWait'), ('  ring write', 'TimeRingWrite'),
        ('  decode', 'TimeDecode'), ('  render drain', 'TimeRenderDrain'),
        ('present Blt', 'TimeBltCopy'), ('Flip', 'TimeFlip'), ('Lock held', 'TimeLockHeld')]
waits = 0.0
for label, k in rows:
    calls = dd(k + 'Calls')
    s = sec(k)
    if k in ('TimeRingWait', 'TimeRingSpaceWait', 'TimeRingWrite', 'TimeDecode', 'TimeRenderDrain'):
        waits += s
    if calls or s:
        print(f"  {label:30} {s:6.2f} s {100*s/wall:5.1f}%  {calls:8} calls  "
              f"{1e6*s/max(calls,1):7.1f} us/call  {1e6*s/max(subs,1):6.1f} us/submission")
eng = sec('TimeEngineDraw')
build = eng - waits
print(f"  {'  build (rest of engine draw)':30} {build:6.2f} s {100*build/wall:5.1f}%  "
      f"{1e6*build/max(subs,1):6.1f} us/submission")
front = max(sec('TimeD3dCalls'), sec('R3dDraw')) - eng
print(f"  {'front end (entry - engine)':30} {front:6.2f} s {100*front/wall:5.1f}%  "
      f"{1e6*front/max(subs,1):6.1f} us/submission")
sd, pd = dp('I9xxStateDwords'), dp('I9xxPrimDwords')
print(f"dwords per submission: state {sd/max(subs,1):.1f}, primitive {pd/max(subs,1):.1f}")
comp, rep, chg = dd('I9xxStateCompared'), dd('I9xxStateRepeats'), dd('I9xxStateChangedDwords')
print(f"state compared {comp}, identical to previous {rep} ({100*rep/max(comp,1):.1f}%), "
      f"changed dwords per compared {chg/max(comp,1):.1f} of {sd/max(subs,1):.1f}")
print(f"render drain waits {dd('RenderDrainWaits')} stalls {dd('RenderDrainStalls')}; "
      f"sync {dd('SyncSubmits')} async {dd('AsyncSubmits')}; refused {dd('I9xxDrawsRefused')}; "
      f"timeouts crumb {dd('BreadcrumbTimeouts')} resets {dd('EngineResets')}")
print(f"triangles in {dd('R3dListTrianglesIn')}, D3D RenderPrimitive calls {dd('D3dRenderPrimitiveCalls')}, "
      f"flips {dd('FlipCalls')}")
