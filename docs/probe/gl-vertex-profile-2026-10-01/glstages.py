import re, sys
# glstages.py log... : per-vertex stage costs from V9XGL.LOG tsc/prim lines,
# demo intervals (over 100 frames) of the last process that logged a prim line.


def stats(f):
    lines = open(f, encoding="latin-1").read().splitlines()
    # The last session only: pids repeat on Windows 9x, sessions start at attach.
    starts = [i for i, l in enumerate(lines) if " attach " in l]
    lines = lines[starts[-1]:]
    pid = [re.search(r"pid=\w+", l).group(0) for l in lines if " prim " in l][-1]
    lines = [l for l in lines if pid in l]
    tsc = [dict(re.findall(r"([a-z-]+)=(\d+)", l)) for l in lines if " tsc " in l]
    prim = [dict(re.findall(r"([a-z-]+)=(\d+)", l)) for l in lines if " prim " in l]
    pairs = [(t, p) for t, p in zip(tsc[-len(prim):], prim) if int(t["frames"]) > 100]
    n = len(pairs)
    T = [t for t, _ in pairs]
    P = [p for _, p in pairs]
    avg = lambda d, k: sum(int(x[k]) for x in d) / n
    v = avg(T, "vertices")
    wall = avg(T, "wall-ms")
    out = [("intervals", n), ("vertices/10s", v), ("frames/10s", avg(T, "frames")),
           ("glVertex ns/vertex", 1e6 * avg(T, "vertex-ms") / v),
           ("glVertex % wall", 100 * avg(T, "vertex-ms") / wall),
           ("Begin/End % wall", 100 * avg(T, "beginend-ms") / wall)]
    for k in ["transform-ms", "inside-ms", "window-ms", "assemble-ms", "history-ms"]:
        out.append((k[:-3] + " ns/vertex", 1e6 * avg(P, k) / v))
    return out


for f in sys.argv[1:]:
    print(f)
    for k, x in stats(f):
        print(f"  {k:22} {x:10.1f}")
