import struct, sys, collections, heapq
# evictsim.py V9XGLUSE.BIN : replay the ICD's recorded texture use order
# through a byte-capacity cache under several eviction rules. The capacity
# is calibrated so plain LRU (the rule the trace ran under) reproduces the
# hit rate the trace observed (its fourth field: the copy had a surface).
# DirectDraw's heap fragmentation is not modelled; bytes are.

data = open(sys.argv[1], "rb").read()
n = len(data) // 16
uses = [struct.unpack_from("<4I", data, i * 16) for i in range(n)]
uses = [u for u in uses if u[1] != 0]
names = [u[0] for u in uses]
sizes = {}
for u in uses:
    sizes[u[0]] = u[1]
frames = [u[2] for u in uses]
observed = sum(u[3] for u in uses) / len(uses)
print(f"{len(uses)} uses, {len(sizes)} textures, {len(set(frames))} frames, "
      f"{sum(sizes.values()) / 1048576:.1f} MiB if all resident; observed hit rate {observed:.3f}")
by_size = collections.Counter(sizes.values())
print("  copy sizes:", ", ".join(f"{s // 1024} KiB x{c}" for s, c in sorted(by_size.items(), reverse=True)[:8]))
per_frame = collections.Counter(frames)
print(f"  uses per frame: median {sorted(per_frame.values())[len(per_frame) // 2]}")

# next-use index for Belady
nxt = [0] * len(uses)
last = {}
for i in range(len(uses) - 1, -1, -1):
    nxt[i] = last.get(names[i], 1 << 60)
    last[names[i]] = i


def simulate(policy, capacity):
    resident = {}          # name -> last use index
    first_seen = {}
    frame_of = {}
    count2 = {}            # uses while resident (for LRU-2 style)
    hist = collections.defaultdict(list)
    used = 0
    hits = 0
    next_use = {}
    for i, (name, size, frame) in enumerate(zip(names, (sizes[x] for x in names), frames)):
        if name in resident:
            hits += 1
        else:
            while used + size > capacity and resident:
                if policy == "lru":
                    v = min(resident, key=resident.get)
                elif policy == "mru":
                    v = max(resident, key=resident.get)
                elif policy == "lru-spare-frame":
                    old = [k for k in resident if frame_of[k] != frame]
                    v = min(old, key=resident.get) if old else min(resident, key=resident.get)
                elif policy == "lru-largest":
                    # among the oldest half, the largest
                    order = sorted(resident, key=resident.get)
                    half = order[:max(1, len(order) // 2)]
                    v = max(half, key=lambda k: sizes[k])
                elif policy == "lru2":
                    # evict by second-to-last use (older = better victim)
                    v = min(resident, key=lambda k: hist[k][-2] if len(hist[k]) > 1 else -1)
                elif policy == "belady":
                    v = max(resident, key=lambda k: next_use.get(k, 1 << 60))
                else:
                    raise ValueError(policy)
                used -= sizes[v]
                del resident[v]
            if size <= capacity:
                resident[name] = i
                used += size
        if name in resident:
            resident[name] = i
        frame_of[name] = frame
        hist[name].append(i)
        if len(hist[name]) > 2:
            hist[name] = hist[name][-2:]
        next_use[name] = nxt[i]
    return hits / len(uses)


# Calibrate LRU's capacity to the observed hit rate.
lo, hi = 256 * 1024, 64 * 1048576
for _ in range(30):
    mid = (lo + hi) // 2
    if simulate("lru", mid) < observed:
        lo = mid
    else:
        hi = mid
cap = hi
print(f"calibrated capacity {cap / 1048576:.2f} MiB (LRU {simulate('lru', cap):.3f})")
for scale in (1.0, 1.5, 2.0, 4.0):
    c = int(cap * scale)
    row = {p: simulate(p, c) for p in ("lru", "mru", "lru-spare-frame", "lru-largest", "lru2", "belady")}
    print(f"  capacity x{scale:<4} {c / 1048576:5.2f} MiB: " +
          "  ".join(f"{p} {v:.3f}" for p, v in row.items()))
