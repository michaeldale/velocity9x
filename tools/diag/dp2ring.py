"""Read C:\\V9XDIAG\\V9XDP2R.BIN, the DrawPrimitives2 capture
(src\\display32\\d3d\\d3d_dp2_ring.h), and say what ends each call.

    python tools/diag/dp2ring.py V9XDP2R.BIN [--calls N] [--from I]

Prints the shape of the calls (records, triangles, buffer use), what the
runtime did between calls, the time inside and between calls, the most
common call signatures, and with --calls a listing of N entries.
"""
import argparse
import collections
import struct
import sys

OPS = {1: "POINTS", 2: "IDXLINELIST", 3: "IDXTRILIST", 8: "RS",
       15: "LINELIST", 16: "LINESTRIP", 17: "IDXLINESTRIP", 18: "TRILIST",
       19: "TRISTRIP", 20: "IDXTRISTRIP", 21: "TRIFAN", 22: "IDXTRIFAN",
       23: "TRIFAN_IMM", 24: "LINELIST_IMM", 25: "TSS", 26: "IDXTRILIST2",
       27: "IDXLINELIST2", 28: "VIEWPORT", 29: "WINFO", 30: "SETPALETTE",
       31: "UPDATEPALETTE", 32: "ZRANGE"}

RS = {1: "TEXTUREHANDLE", 2: "ANTIALIAS", 3: "TEXTUREADDRESS",
      4: "TEXTUREPERSPECTIVE", 5: "WRAPU", 6: "WRAPV", 7: "ZENABLE",
      8: "FILLMODE", 9: "SHADEMODE", 10: "LINEPATTERN", 14: "ZWRITEENABLE",
      15: "ALPHATESTENABLE", 16: "LASTPIXEL", 17: "TEXTUREMAG",
      18: "TEXTUREMIN", 19: "SRCBLEND", 20: "DESTBLEND",
      21: "TEXTUREMAPBLEND", 22: "CULLMODE", 23: "ZFUNC", 24: "ALPHAREF",
      25: "ALPHAFUNC", 26: "DITHERENABLE", 27: "ALPHABLENDENABLE",
      28: "FOGENABLE", 29: "SPECULARENABLE", 30: "ZVISIBLE",
      33: "STIPPLEDALPHA", 34: "FOGCOLOR", 35: "FOGTABLEMODE",
      36: "FOGSTART", 37: "FOGEND", 38: "FOGDENSITY", 40: "EDGEANTIALIAS",
      41: "COLORKEYENABLE", 43: "BORDERCOLOR", 44: "TEXTUREADDRESSU",
      45: "TEXTUREADDRESSV", 46: "MIPMAPLODBIAS", 47: "ZBIAS",
      48: "RANGEFOGENABLE", 49: "ANISOTROPY", 50: "FLUSHBATCH",
      51: "TRANSLUCENTSORTINDEPENDENT", 52: "STENCILENABLE",
      60: "TEXTUREFACTOR", 128: "WRAP0"}

TSS = {1: "COLOROP", 2: "COLORARG1", 3: "COLORARG2", 4: "ALPHAOP",
       5: "ALPHAARG1", 6: "ALPHAARG2", 11: "TEXCOORDINDEX", 12: "ADDRESS",
       13: "ADDRESSU", 14: "ADDRESSV", 15: "BORDERCOLOR", 16: "MAGFILTER",
       17: "MINFILTER", 18: "MIPFILTER", 19: "MIPMAPLODBIAS",
       20: "MAXMIPLEVEL", 21: "MAXANISOTROPY"}

KINDS = {1: "CALL", 2: "LOCK", 3: "UNLOCK", 4: "BLT", 5: "FLIP",
         6: "CREATESURF", 7: "DESTROYSURF", 8: "TEXCREATE", 9: "TEXDESTROY",
         10: "TEXSWAP", 11: "TEXGETSURF", 12: "CLEAR2", 13: "VALIDATETSS",
         14: "DX3RENDERSTATE", 15: "SETTARGET", 16: "CTXCREATE",
         17: "CTXDESTROY", 18: "DX5DRAW", 19: "GETDRIVERINFO",
         20: "STATUSPOLL", 21: "VBLANK"}

HEADER = struct.Struct("<5I")
FIXED = struct.Struct("<16I")
ITEM = struct.Struct("<BBHI")


def load(path):
    data = open(path, "rb").read()
    magic, entry_bytes, count, skipped, _ = HEADER.unpack_from(data, 0)
    if magic != 0x31523956:
        sys.exit("not a V9R1 capture")
    slots = (entry_bytes - FIXED.size) // ITEM.size
    entries = []
    at = HEADER.size
    for _ in range(count):
        f = FIXED.unpack_from(data, at)
        e = dict(zip(("kind", "tsc_in", "tsc_out", "flags", "command_offset",
                      "command_length", "vertex_offset", "vertex_length",
                      "command_surface", "vertex_surface", "vertex_type",
                      "records", "triangles", "req_vertex", "req_command",
                      "items"), f))
        e["item"] = [ITEM.unpack_from(data, at + FIXED.size + i * ITEM.size)
                     for i in range(min(e["items"], slots))]
        entries.append(e)
        at += entry_bytes
    return entries, skipped, slots


def item_text(item):
    op, count, state, value = item
    name = OPS.get(op, "OP%d" % op)
    if op == 8:
        return "%s=%#x" % (RS.get(state, "RS%d" % state), value)
    if op == 25:
        return "TSS%d.%s=%#x" % (state >> 8, TSS.get(state & 0xff,
                                                     str(state & 0xff)), value)
    return "%s(%d)" % (name, count)


def shape(item):
    """An item without its value, for signatures."""
    op, count, state, _ = item
    if op == 8:
        return RS.get(state, "RS%d" % state)
    if op == 25:
        return "TSS." + TSS.get(state & 0xff, str(state & 0xff))
    return OPS.get(op, "OP%d" % op)


def entry_text(e):
    kind = KINDS.get(e["kind"], str(e["kind"]))
    if e["kind"] != 1:
        return "%-12s a=%#x b=%#x" % (kind, e["flags"], e["command_offset"])
    items = " ".join(item_text(i) for i in e["item"])
    more = " +%d" % (e["items"] - len(e["item"])) if e["items"] > len(e["item"]) else ""
    return ("CALL f=%#x cmd@%d+%d vtx@%d+%d fvf=%#x rec=%d tri=%d in=%d | %s%s"
            % (e["flags"], e["command_offset"], e["command_length"],
               e["vertex_offset"], e["vertex_length"], e["vertex_type"],
               e["records"], e["triangles"],
               (e["tsc_out"] - e["tsc_in"]) & 0xffffffff, items, more))


def percentile(values, p):
    if not values:
        return 0
    values = sorted(values)
    return values[min(len(values) - 1, int(len(values) * p))]


def phases(entries):
    """One line per span between LOCK entries: the reproducer's phases.

    The capture arms at the first DrawPrimitives2 call, which comes after
    the reproducer's first marker, so span n is usually phase n + 1."""
    bounds = [i for i, e in enumerate(entries) if e["kind"] == 2]
    for n, (a, b) in enumerate(zip(bounds, bounds[1:]), 1):
        calls = [entries[k] for k in range(a, b) if entries[k]["kind"] == 1]
        tris = sum(c["triangles"] for c in calls)
        recs = sum(c["records"] for c in calls)
        sig = collections.Counter(" ".join(shape(i) for i in c["item"])
                                  for c in calls)
        top = sig.most_common(1)[0] if sig else ("", 0)
        print("span %2d: calls %4d records %5d triangles %4d | %dx %s"
              % (n, len(calls), recs, tris, top[1], top[0][:90]))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("path")
    ap.add_argument("--calls", type=int, default=0)
    ap.add_argument("--from", dest="start", type=int, default=0)
    ap.add_argument("--phases", action="store_true",
                    help="split at LOCK entries (tools/diag/dp2_repro_win32.c)")
    args = ap.parse_args()
    entries, skipped, slots = load(args.path)
    if args.phases:
        phases(entries)
        return
    calls = [i for i, e in enumerate(entries) if e["kind"] == 1]
    print("entries %d, calls %d, calls skipped before %d, item slots %d"
          % (len(entries), len(calls), skipped, slots))

    kinds = collections.Counter(KINDS.get(e["kind"], e["kind"]) for e in entries)
    print("entry kinds:", dict(kinds))

    recs = collections.Counter(entries[i]["records"] for i in calls)
    tris = collections.Counter(entries[i]["triangles"] for i in calls)
    print("records/call:", sorted(recs.items())[:16])
    print("triangles/call:", sorted(tris.items())[:24])
    flags = collections.Counter(entries[i]["flags"] for i in calls)
    print("dwFlags:", {hex(k): v for k, v in flags.items()})
    fvf = collections.Counter(entries[i]["vertex_type"] for i in calls)
    print("vertex types:", {hex(k): v for k, v in fvf.items()})
    cmdsurf = collections.Counter(entries[i]["command_surface"] for i in calls)
    vtxsurf = collections.Counter(entries[i]["vertex_surface"] for i in calls)
    print("command surfaces %d, vertex surfaces %d" % (len(cmdsurf), len(vtxsurf)))
    print("command offset 0 in %d of %d calls"
          % (sum(1 for i in calls if entries[i]["command_offset"] == 0), len(calls)))
    print("command bytes p50 %d p90 %d max %d" % (
        percentile([entries[i]["command_length"] for i in calls], .5),
        percentile([entries[i]["command_length"] for i in calls], .9),
        max(entries[i]["command_length"] for i in calls) if calls else 0))
    print("vertex count p50 %d p90 %d max %d; vertex offset 0 in %d calls" % (
        percentile([entries[i]["vertex_length"] for i in calls], .5),
        percentile([entries[i]["vertex_length"] for i in calls], .9),
        max(entries[i]["vertex_length"] for i in calls) if calls else 0,
        sum(1 for i in calls if entries[i]["vertex_offset"] == 0)))
    reqs = collections.Counter((entries[i]["req_vertex"], entries[i]["req_command"])
                               for i in calls)
    print("req sizes in (vertex, command):", reqs.most_common(4))

    # Does the next call continue the vertex buffer where the last left off?
    cont = reset = other = 0
    for a, b in zip(calls, calls[1:]):
        ea, eb = entries[a], entries[b]
        if eb["vertex_surface"] != ea["vertex_surface"]:
            other += 1
        elif eb["vertex_offset"] == 0:
            reset += 1
        elif eb["vertex_offset"] >= ea["vertex_offset"]:
            cont += 1
        else:
            other += 1
    print("next call's vertices: continue %d, restart at 0 %d, other %d"
          % (cont, reset, other))

    inside = [(entries[i]["tsc_out"] - entries[i]["tsc_in"]) & 0xffffffff
              for i in calls]
    gaps = []
    between = collections.Counter()
    for a, b in zip(calls, calls[1:]):
        gaps.append((entries[b]["tsc_in"] - entries[a]["tsc_out"]) & 0xffffffff)
        evs = tuple(KINDS.get(entries[k]["kind"], entries[k]["kind"])
                    for k in range(a + 1, b))
        between[evs] += 1
    print("cycles inside a call p50 %d p90 %d; between calls p50 %d p90 %d"
          % (percentile(inside, .5), percentile(inside, .9),
             percentile(gaps, .5), percentile(gaps, .9)))
    if gaps:
        print("sum inside %d, sum between %d (cycles)" % (sum(inside), sum(gaps)))
    print("HAL entries between consecutive calls:")
    for evs, n in between.most_common(8):
        print("  %7d  %s" % (n, " ".join(evs) if evs else "(none)"))

    sig = collections.Counter(
        " ".join(shape(i) for i in entries[c]["item"]) for c in calls)
    print("most common call signatures:")
    for s, n in sig.most_common(12):
        print("  %7d  %s" % (n, s))
    first = collections.Counter(shape(entries[c]["item"][0]) if entries[c]["item"]
                                else "-" for c in calls)
    last = collections.Counter(shape(entries[c]["item"][-1]) if entries[c]["item"]
                               and entries[c]["items"] <= slots else "?"
                               for c in calls)
    print("first item of a call:", first.most_common(6))
    print("last item of a call:", last.most_common(6))

    if args.calls:
        for k in range(args.start, min(len(entries), args.start + args.calls)):
            print("%6d %s" % (k, entry_text(entries[k])))


if __name__ == "__main__":
    main()
