import re, sys
# snapdiff.py pre.ini post.ini : the 3DMark run's engine and texture counters.


def load(f):
    d = {}
    for l in open(f, encoding="latin-1"):
        m = re.match(r'^(\w+)=(0x[0-9A-Fa-f]+|-?\d+)\s*$', l)
        if m:
            d[m.group(1)] = int(m.group(2), 0) & 0xffffffff
    return d


a, b = load(sys.argv[1]), load(sys.argv[2])
keys = ["DumpUptimeMs", "I9xxDrawsSubmitted", "I9xxDrawsRefused", "AsyncSubmits",
        "SyncSubmits", "RingPlanInvalid", "RingSpaceTimeouts", "BreadcrumbTimeouts",
        "BreadcrumbAbandoned", "M64Draws", "M64Refused", "M64Triangles",
        "EngineResets", "FlipHandled", "D3dTextureCreates", "D3dTextureRefusedShape",
        "D3dTextureRefusedVidMem", "BatchesEngineRefused"]
for k in keys:
    if k in b:
        print(f"{k:24} {(b[k] - a.get(k, 0)) & 0xffffffff}")
for k in ["I9xxRefuseLast", "M64PolicyLast", "Build"]:
    if k in b:
        print(f"{k:24} last {b[k]}")
for k in sorted(b):
    if k.startswith("M64Policy") and k != "M64PolicyLast":
        x = (b[k] - a.get(k, 0)) & 0xffffffff
        if x:
            print(f"{k:24} {x}")
