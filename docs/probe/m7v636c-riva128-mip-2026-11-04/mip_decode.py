import struct, sys, collections, os

# name, log2 of level 0, quad labels
CASES = [
    ("MipOneLevel", 3, "1/2 2 4 8"),
    ("MipChain", 3, "1/2 2 4 8"),
    ("MipChainAdjust10", 3, "1/2 2 4 8"),
    ("MipChainAdjustF0", 3, "1/2 2 4 8"),
    ("MipChainSpread04", 3, "1/2 2 4 8"),
    ("MipChainSpread1F", 3, "1/2 2 4 8"),
    ("MipChainFoh", 3, "1/2 2 4 8"),
    ("MipChainFar", 3, "16 32 64 128"),
    ("MipChainMin1", 3, "16 32 64 128"),
    ("MipChain4x4", 2, "1/2 2 4 8"),
    ("MipChainU", 3, "u1/2 u2 u4 u8"),
    ("MipChainV", 3, "v1/2 v2 v4 v8"),
    ("MipChainFarAdjustF0", 3, "16 32 64 128"),
    ("MipBiasF0toC0", 3, "F0 E0 D0 C0"),
    ("MipBiasB0to80", 3, "B0 A0 90 80"),
    ("MipBiasFine", 3, "FF FE FC F8"),
    ("MipBias4x4", 2, "F0 E0 D0 C0"),
]

def level(s, log2):
    start = 0
    for lv in range(log2 + 1):
        n = 1 << (2 * (log2 - lv))
        if s < start + n:
            return lv
        start += n
    return 9

def decode(p):
    if (p & 0x1f) < 28:
        return None
    r = (p >> 10) & 31
    g = (p >> 5) & 31
    return int(round(r / 2.0)) + 16 * int(round(g / 2.0))

d = sys.argv[1]
for i, (name, log2, labels) in enumerate(CASES):
    path = os.path.join(d, "N3D%d.BIN" % (41 + i))
    if not os.path.exists(path):
        continue
    data = open(path, "rb").read()
    px = struct.unpack("<%dH" % (128 * 16), data[:128 * 16 * 2])
    out = []
    for q, label in enumerate(labels.split()):
        seen = collections.Counter()
        levels = collections.Counter()
        other = 0
        for y in range(16):
            for x in range(q * 32, q * 32 + 32):
                s = decode(px[y * 128 + x])
                if s is None:
                    other += 1
                else:
                    seen[s] += 1
                    levels[level(s, log2)] += 1
        lv = "/".join("L%d" % k for k in sorted(levels))
        out.append("%s:%s(%d,%s%s)" % (label, lv, len(seen),
                   "%d-%d" % (min(seen), max(seen)) if seen else "-",
                   ",other %d" % other if other else ""))
    print("%-20s %s" % (name, "  ".join(out)))
