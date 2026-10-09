"""Rollcage demo, Glide 3 census on A8U4I5 (2026-10-10): what V9XGLD3.LOG says.

Run: python analyze_rc.py > analysis.txt

The log holds three processes, split at each "attach" line:
  1. the census DLL before grQueryResolutions was written (start-up only);
  2. Play pressed: front end, then a 3D scene with no input, then Glide shut
     down and the process went away;
  3. the process the game started next; Enter was pressed once in it, then
     it was closed with WCLOSE.EXE.
Glide 3 constants are from glide.h (names only, for reading the output).
"""
import collections
import os
import re
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
LOG = os.path.join(HERE, "V9XGLD3.LOG")

PARAM = {0x01: "XY", 0x02: "Z", 0x03: "W", 0x04: "Q", 0x05: "FOG_EXT",
         0x10: "A", 0x20: "RGB", 0x30: "PARGB", 0x40: "ST0", 0x41: "ST1",
         0x42: "ST2", 0x50: "Q0", 0x51: "Q1", 0x52: "Q2"}
PRIM = {0: "POINTS", 1: "LINE_STRIP", 2: "LINES", 3: "POLYGON",
        4: "TRIANGLE_STRIP", 5: "TRIANGLE_FAN", 6: "TRIANGLES"}
BLEND = {0: "ZERO", 1: "SRC_ALPHA", 2: "SRC_COLOR", 3: "DST_ALPHA", 4: "ONE",
         5: "ONE_MINUS_SRC_ALPHA", 6: "ONE_MINUS_SRC_COLOR",
         7: "ONE_MINUS_DST_ALPHA", 15: "ALPHA_SATURATE"}
CMP = {0: "NEVER", 1: "LESS", 2: "EQUAL", 3: "LEQUAL", 4: "GREATER",
       5: "NOTEQUAL", 6: "GEQUAL", 7: "ALWAYS"}

line_re = re.compile(r"^\d+ t=(\d+) (\w+) #(\d+)((?: [0-9A-F]{8})*)$")


def f32(word):
    return struct.unpack("<f", struct.pack("<I", int(word, 16)))[0]


def s32(word):
    value = int(word, 16)
    return value - (1 << 32) if value & 0x80000000 else value


processes = []
for raw in open(LOG, errors="replace"):
    raw = raw.rstrip()
    if " attach census " in raw:
        processes.append({"lines": [], "attach": raw})
        continue
    if processes:
        processes[-1]["lines"].append(raw)

for number, proc in enumerate(processes, 1):
    calls = collections.defaultdict(collections.Counter)
    texinfo = collections.Counter()
    vertices = []
    notes = []
    summaries = []
    for raw in proc["lines"]:
        m = line_re.match(raw)
        if m:
            calls[m.group(2)][tuple(m.group(4).split())] += 1
            continue
        m = re.search(r"info (small=.*)$", raw)
        if m:
            texinfo[m.group(1)] += 1
            continue
        m = re.search(r"(grDrawVertexArrayContiguous|grDrawVertexArray) vertex((?: [0-9A-F]{8})+)", raw)
        if m:
            vertices.append((m.group(1), m.group(2).split()))
            continue
        if re.search(r"grGet pname|grQueryResolutions template", raw):
            notes.append(re.sub(r"^\d+ t=\d+ ", "", raw))
            continue
        m = re.search(r"summary (\w+) swaps=(\d+)", raw)
        if m:
            summaries.append([m.group(1), int(m.group(2)), {}])
            continue
        m = re.match(r"^\d+ t=\d+   (\w+) (\d+)$", raw)
        if m and summaries:
            summaries[-1][2][m.group(1)] = int(m.group(2))

    print("=" * 72)
    print("process %d: %s" % (number, re.sub(r"^\d+ t=\d+ ", "", proc["attach"])))
    if summaries:
        why, swaps, counts = summaries[-1]
        print("last summary (%s): swaps=%d" % (why, swaps))
        for name in sorted(counts):
            print("    %-30s %d" % (name, counts[name]))

    print("\nqueries:")
    for text in collections.OrderedDict.fromkeys(notes):
        print("    " + text)

    if calls["grSstWinOpen"]:
        print("\ngrSstWinOpen (hwnd, res, refresh, cformat, origin, colbuf, auxbuf):")
        for args in calls["grSstWinOpen"]:
            print("    " + " ".join(args))

    if calls["grVertexLayout"]:
        print("\ngrVertexLayout (param, offset, mode) in call order:")
        for args in calls["grVertexLayout"]:
            p, off, mode = (int(a, 16) for a in args)
            print("    %-8s offset=%-3d %s" % (PARAM.get(p, hex(p)), off,
                                             "enable" if mode else "disable"))

    print("\ndistinct state arguments (logged sets; captured frames repeat them):")
    skip = {"grDrawVertexArrayContiguous", "grDrawVertexArray", "grDrawTriangle",
            "grTexSource", "grTexDownloadMipMap", "grTexDownloadTable",
            "grTexTextureMemRequired", "grBufferSwap", "grGet",
            "grQueryResolutions", "grVertexLayout", "grSstWinOpen"}
    for name in sorted(calls):
        if name in skip:
            continue
        print("  %s" % name)
        for args, n in calls[name].most_common(10):
            extra = ""
            if name == "grAlphaBlendFunction":
                extra = "  rgb %s/%s alpha %s/%s" % tuple(
                    BLEND.get(int(a, 16), a) for a in args)
            elif name == "grDepthBufferFunction":
                extra = "  " + CMP.get(int(args[0], 16), args[0])
            elif name == "grClipWindow":
                extra = "  (%d,%d)-(%d,%d)" % tuple(s32(a) for a in args)
            print("      %s  x%d%s" % (" ".join(args), n, extra))

    print("\ndraws (mode, count[, stride]) as logged:")
    shapes = collections.Counter()
    for name in ("grDrawVertexArrayContiguous", "grDrawVertexArray"):
        for args, n in calls[name].items():
            key = (name, PRIM.get(int(args[0], 16), args[0]), int(args[1], 16),
                   int(args[3], 16) if len(args) > 3 else None)
            shapes[key] += n
    for (name, mode, count, stride), n in shapes.most_common():
        print("    %-28s %-10s count=%-3d stride=%s  x%d" % (name, mode, count,
                                                           stride, n))
    print("    grDrawTriangle logged x%d" % sum(calls["grDrawTriangle"].values()))

    print("\ntexture infos (Glide 3 LOD/aspect log2 numbers):")
    for info, n in texinfo.most_common():
        print("    %s  x%d" % (info, n))
    addresses = sorted({int(a[1], 16) for a in calls["grTexDownloadMipMap"]})
    if addresses:
        print("    download addresses: %d distinct, 0x%X..0x%X" % (
            len(addresses), addresses[0], addresses[-1]))

    print("\nfirst vertices as floats (x y r g b q s t at the declared offsets):")
    for name, words in vertices[:6]:
        print("    %-28s %s" % (name, " ".join("%.4g" % f32(w) for w in words[:8])))
    print("    LFB calls: %d" % sum(sum(c.values()) for k, c in calls.items()
                                    if k.startswith("grLfb")))
    print()
