import re, sys
# sstimeline.py V9XGL.LOG : Serious Sam demo frames and ICD/HAL time per ten
# seconds, last session only (sessions start at attach; Windows 9x reuses
# pids). The demo is the run of intervals with draws above `--min-draws`;
# the menu's intro level draws a few hundred.

path = sys.argv[1]
min_draws = int(sys.argv[2]) if len(sys.argv) > 2 else 1000
lines = open(path, encoding="latin-1").read().splitlines()
lines = lines[[i for i, l in enumerate(lines) if " attach " in l][-1]:]
rows = []
for i, l in enumerate(lines):
    if " tsc " not in l:
        continue
    t = dict(re.findall(r"([a-z-]+)=(\d+)", l))
    c = None
    p = None
    for m in lines[i:i + 7]:
        if " counters " in m:
            c = dict(re.findall(r"([a-z-]+)=(\d+)", m))
        if " paths " in m:
            p = re.search(r"hw=(\d+)/(\d+) cpu=(\d+)/(\d+)", m)
    rows.append((t, c, p))

prev = {}


def delta(key, value):
    result = value - prev.get(key, value)
    prev[key] = value
    return result


demo = []
for t, c, p in rows:
    d = {
        "frames": int(t["frames"]), "wall": int(t["wall-ms"]),
        "draws": int(t["draws"]), "iface": int(t["iface-ms"]),
        "flush": int(t["flush-other-ms"]), "bind": int(t["bind-ms"]),
        "swap": int(t["swap-ms"]),
        "creates": delta("cr", int(c["creates"])) if c else 0,
        "failed": delta("fa", int(c["create-failed"])) if c else 0,
        "evict": delta("ev", int(c["evictions"])) if c else 0,
        "upkb": delta("up", int(c["upload-kb"])) if c else 0,
        "hwb": delta("hb", int(p.group(1))) if p else 0,
        "cpub": delta("cb", int(p.group(3))) if p else 0,
        "cput": delta("ct", int(p.group(4))) if p else 0,
    }
    if d["draws"] >= min_draws:
        demo.append(d)

n = len(demo)
s = lambda k: sum(d[k] for d in demo)
wall = s("wall") / 1000.0
print(f"demo intervals {n}, {wall:.0f} s, {s('frames')} frames, {s('frames') / wall:.2f} fps average")
print(f"  per 10 s: draws {10 * s('draws') / wall:.0f}, hw batches {10 * s('hwb') / wall:.0f}, "
      f"cpu batches {10 * s('cpub') / wall:.0f} ({10 * s('cput') / wall:.0f} triangles)")
for k in ["iface", "flush", "bind", "swap"]:
    print(f"  {k:6} {100 * s(k) / 1000.0 / wall:5.1f}% of wall")
print(f"  textures per 10 s: creates {10 * s('creates') / wall:.0f}, failed {10 * s('failed') / wall:.0f}, "
      f"evictions {10 * s('evict') / wall:.0f}, upload {10 * s('upkb') / wall / 1024:.1f} MB")
print(f"  slowest 10 s: {min(d['frames'] for d in demo) / 10.0:.1f} fps, fastest {max(d['frames'] for d in demo) / 10.0:.1f}")
