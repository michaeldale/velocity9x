import collections
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
logs = [os.path.join(HERE, n) for n in ("V9XGLD3-run1-intro-menu.log", "V9XGLD3-run2-escape-quit.log", "V9XGLD3-run3-menu-clicks.log")]

calls = collections.defaultdict(collections.Counter)
texinfo = collections.Counter()
draws = collections.Counter()
vertices = []
grget = collections.Counter()
strings = []
summary_counts = {}

line_re = re.compile(r"^\d+ t=\d+ (\w+) #(\d+)((?: [0-9A-F]{8})*)$")
for path in logs:
    for raw in open(path, errors="replace"):
        raw = raw.rstrip()
        m = line_re.match(raw)
        if m:
            name, args = m.group(1), m.group(3).split()
            calls[name][tuple(args)] += 1
            if name == "grDrawVertexArrayContiguous":
                draws[(int(args[0], 16), int(args[1], 16), int(args[3], 16))] += 1
            continue
        m = re.search(r"(grTexSource|grTexDownloadMipMap) info small=(-?\d+) large=(-?\d+) aspect=(-?\d+) format=([0-9A-F]+)", raw)
        if m:
            texinfo[(m.group(1), int(m.group(2)), int(m.group(3)), int(m.group(4)), m.group(5))] += 1
            continue
        m = re.search(r"(grDrawVertexArrayContiguous|grDrawVertexArray) vertex((?: [0-9A-F]{8})+)", raw)
        if m:
            vertices.append(m.group(2).split())
            continue
        m = re.search(r"grGet pname=([0-9A-F]{2})", raw)
        if m:
            grget[m.group(1)] += 1
            continue
        m = re.search(r"summary (shutdown|detach|periodic) swaps=(\d+)", raw)

def floats(words):
    out = []
    for w in words:
        value = struct.unpack("<f", struct.pack("<I", int(w, 16)))[0]
        out.append("%g" % value)
    return out

skip = {"grTexSource", "grTexDownloadMipMap", "grDrawVertexArrayContiguous", "grBufferSwap",
        "grDitherMode", "grConstantColorValue"}
for name in sorted(calls):
    distinct = calls[name]
    print("%s: %d distinct argument sets logged" % (name, len(distinct)))
    if name in skip:
        continue
    for args, n in distinct.most_common(12):
        print("    %s  x%d" % (" ".join(args), n))

print("\ngrDitherMode values:", sorted({a[0] for a in calls["grDitherMode"]}))
print("grConstantColorValue values:", sorted({a[0] for a in calls["grConstantColorValue"]})[:20])
print("grBufferSwap intervals:", sorted({a[0] for a in calls["grBufferSwap"]}))
print("\ngrGet pnames:", dict(grget))
print("\ntexture infos (call, small, large, aspect, format): count")
for k, n in sorted(texinfo.items()):
    print("   ", k, n)
print("\ndraw (mode, count, stride): logged samples")
for k, n in draws.most_common(20):
    print("   ", k, n)
print("\nvertex samples as floats:")
for v in vertices[:8]:
    print("   ", " ".join(v[:8]), "|", " ".join(floats(v[:8])))
starts = collections.Counter()
for args in calls["grTexDownloadMipMap"]:
    starts[args[2]] += 1
print("\nTexDownloadMipMap evenOdd:", dict(starts))
