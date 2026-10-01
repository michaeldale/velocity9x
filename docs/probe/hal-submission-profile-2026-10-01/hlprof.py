import re, sys
# hlprof.py pre.ini post.ini : where a snapshot window's wall time went in the
# HAL, and what each Gen3 submission was made of.
HZ = 1662e6


def load(f):
    d = {}
    for l in open(f, encoding='latin-1'):
        m = re.match(r'^(\w+)=(0x[0-9A-Fa-f]+|-?\d+)\s*$', l)
        if m:
            d[m.group(1)] = int(m.group(2), 0) & 0xffffffff
    return d


a, b = load(sys.argv[1]), load(sys.argv[2])
wall = (b['DumpUptimeMs'] - a['DumpUptimeMs']) / 1000.0
dd = lambda k: (b.get(k, 0) - a.get(k, 0)) & 0xffffffff
pair = lambda d, k: (d.get(k + 'Hi', 0) << 32) | d.get(k + 'Lo', 0)
dp = lambda k: pair(b, k) - pair(a, k)
sec = lambda k: dp(k + 'Cycles') / HZ

print(f"window {wall:.1f} s")
rows = [('D3D entries', 'TimeD3dCalls'), ('render-interface draw entry', 'R3dDraw'),
        ('engine draw', 'TimeEngineDraw'), ('  wait for GPU (head)', 'TimeRingWait'),
        ('  ring write', 'TimeRingWrite'), ('  decode', 'TimeDecode'),
        ('  breadcrumb drain', 'TimeRenderDrain'), ('present Blt', 'TimeBltCopy'),
        ('Lock held', 'TimeLockHeld')]
for label, k in rows:
    calls = dd(k + 'Calls')
    s = sec(k)
    if calls or s:
        print(f"  {label:30} {s:6.2f} s {100*s/wall:5.1f}%  {calls:8} calls  {1e6*s/max(calls,1):7.1f} us/call")
eng = sec('TimeEngineDraw')
build = eng - sec('TimeRingWait') - sec('TimeRingWrite') - sec('TimeDecode') - sec('TimeRenderDrain')
print(f"  {'  build (rest of engine draw)':30} {build:6.2f} s {100*build/wall:5.1f}%")
front = max(sec('TimeD3dCalls'), sec('R3dDraw')) - eng
print(f"  {'front end (entry - engine)':30} {front:6.2f} s {100*front/wall:5.1f}%")
subs = dd('I9xxDrawsSubmitted')
sd, pd = dp('I9xxStateDwords'), dp('I9xxPrimDwords')
print(f"submissions {subs} ({subs/wall:.0f}/s); dwords per submission: state {sd/max(subs,1):.1f}, primitive {pd/max(subs,1):.1f}")
comp, rep, chg = dd('I9xxStateCompared'), dd('I9xxStateRepeats'), dd('I9xxStateChangedDwords')
print(f"state compared {comp}, identical to previous {rep} ({100*rep/max(comp,1):.1f}%), "
      f"changed dwords per compared {chg/max(comp,1):.1f} of {sd/max(subs,1):.1f}")
print(f"triangles in {dd('R3dListTrianglesIn')}, D3D RenderPrimitive calls {dd('D3dRenderPrimitiveCalls')}, "
      f"sync {dd('SyncSubmits')} async {dd('AsyncSubmits')}, refused {dd('I9xxDrawsRefused')}, "
      f"timeouts crumb {dd('BreadcrumbTimeouts')} resets {dd('EngineResets')}")
