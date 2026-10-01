import re, sys
# Usage: delta.py pre.ini post.ini  -> flip/draw timing deltas (1662 MHz TSC)
HZ = 1662e6
def load(f):
    d = {}
    for l in open(f, encoding='latin-1'):
        m = re.match(r'^(\w+)=(0x[0-9A-Fa-f]+|\d+)\s*$', l)
        if m:
            d[m.group(1)] = int(m.group(2), 0)
    return d
a, b = load(sys.argv[1]), load(sys.argv[2])
dd = lambda k: b.get(k, 0) - a.get(k, 0)
pair = lambda d, k: (d.get(k + 'Hi', 0) << 32) | d.get(k + 'Lo', 0)
dp = lambda k: pair(b, k) - pair(a, k)
flips = dd('FlipHandled')
print(f"flips {flips}  flip calls {dd('TimeFlipCalls')}  window-closed {dd('FlipWindowClosed')}  still-drawing {dd('FlipStillDrawing')}")
n = dd('FlipIntervalCount'); c = dp('FlipIntervalCycles')
if n:
    print(f"frame interval: {n} x {c/HZ/n*1000:.2f} ms avg, max {b.get('FlipIntervalMax',0)/HZ*1000:.1f} ms (boot max)")
n = dd('FlipWaitCalls'); c = dp('FlipWaitCycles')
if n:
    print(f"draw flip wait: {n} waits, {c/HZ:.2f} s total, {c/HZ/n*1000:.2f} ms avg")
for k in ['TimeD3dCalls', 'TimeEngineDraw', 'TimeRingWait', 'TimeFlip', 'TimeBltFill']:
    c = dp(k + 'Cycles'); n = dd(k + 'Calls')
    print(f"{k:16} {c/HZ:7.2f} s  calls {n:>8,}")
print(f"DrawsFlipWaited {dd('DrawsFlipWaited')}  batches {dd('I9xxDrawsSubmitted')}  refused {dd('I9xxDrawsRefused')}  RP calls {dd('D3dRenderPrimitiveCalls')}")
print(f"timeouts fifo {dd('EngineFifoTimeouts')} idle {dd('EngineIdleTimeouts')} resets {dd('EngineResets')} flipwait {dd('DrawsFlipWaitTimeouts')}")
