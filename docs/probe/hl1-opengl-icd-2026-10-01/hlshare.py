import re, sys
# hlshare.py a.ini b.ini : where a snapshot window's wall time went, as rates
HZ = 1662.0
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
def cyc(k):
    return (((b.get(k + 'CyclesHi', 0) << 32) | b.get(k + 'CyclesLo', 0)) -
            ((a.get(k + 'CyclesHi', 0) << 32) | a.get(k + 'CyclesLo', 0))) / HZ / 1e6
print(f"window {wall:.1f} s")
for k in ['TimeD3dCalls', 'TimeEngineDraw', 'TimeRingWait', 'TimeRingWrite', 'TimeDecode', 'TimeBltCopy', 'TimeLockHeld']:
    print(f"  {k:15} {cyc(k):6.2f} s  {100*cyc(k)/wall:5.1f}%   calls/s {dd(k+'Calls')/wall:9.0f}")
b_ = dd('I9xxDrawsSubmitted')
print(f"  batches/s {b_/wall:.0f}  tris in/s {dd('R3dListTrianglesIn')/wall:.0f}  "
      f"tris/batch {(dd('R3dListTrianglesIn')-dd('R3dListCulled'))/max(b_,1):.1f}  "
      f"RP calls/s {dd('D3dRenderPrimitiveCalls')/wall:.0f}  present blits/s {dd('TimeBltCopyCalls')/wall:.1f}")
print(f"  refused {dd('I9xxDrawsRefused')}  timeouts fifo {dd('EngineFifoTimeouts')} crumb {dd('BreadcrumbTimeouts')} resets {dd('EngineResets')}")
