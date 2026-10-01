import re, sys
# glsink.py log... : per-polygon sink and Begin costs from V9XGL.LOG, last
# session (sessions start at attach; pids repeat on Windows 9x), demo
# intervals of over 100 frames.


def stats(f):
    lines = open(f, encoding="latin-1").read().splitlines()
    lines = lines[[i for i, l in enumerate(lines) if " attach " in l][-1]:]
    tsc = [dict(re.findall(r"([a-z-]+)=(\d+)", l)) for l in lines if " tsc " in l]
    snk = [dict(re.findall(r"([a-z-]+)=(\d+)", l)) for l in lines if " sink " in l]
    pairs = [(t, k) for t, k in zip(tsc, snk) if int(t["frames"]) > 100]
    n = len(pairs)
    T = [a for a, _ in pairs]
    K = [b for _, b in pairs]
    avg = lambda d, k: sum(int(x[k]) for x in d) / n
    sinks = avg(T, "sinks")
    wall = avg(T, "wall-ms")
    print(f"{f}: {n} intervals, {sinks:.0f} sinks and {avg(T, 'frames'):.0f} frames per 10 s")
    for d, keys in ((T, ["beginend-ms", "sink-ms", "sink-prep-ms", "vertex-ms"]),
                    (K, ["begin-ms", "flush-ms", "copy-ms", "prep-texture-ms",
                         "prep-state-ms", "prep-same-ms"])):
        for k in keys:
            print(f"  {k:16} {avg(d, k):6.0f} ms {100 * avg(d, k) / wall:5.1f}%"
                  f"  {1e3 * avg(d, k) / sinks:5.2f} us/sink")


for f in sys.argv[1:]:
    stats(f)
