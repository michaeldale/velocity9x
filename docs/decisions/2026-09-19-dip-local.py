"""Isolated single-frame dips, measured against each frame's neighbours.

A flicker frame is one display frame far darker than the frames either side
of it. Comparing against a run-wide or window-wide median instead measures
how dark the SCENE is, which differs between runs and between windows, and
produced two unusable comparisons before this.
"""
import cv2
import statistics
import sys


def means(path, lo, hi):
    cap = cv2.VideoCapture(path)
    if not cap.isOpened():
        return None
    fps = 60.0
    cap.set(cv2.CAP_PROP_POS_FRAMES, int(lo * fps))
    out = []
    idx = int(lo * fps)
    while idx <= int(hi * fps):
        ok, f = cap.read()
        if not ok:
            break
        g = cv2.cvtColor(f[0:1079, 242:1680], cv2.COLOR_BGR2GRAY)
        out.append(float(cv2.resize(g, (180, 135)).mean()))
        idx += 1
    cap.release()
    return out


def isolated_dips(m, half=5, ratio=0.75):
    hits = []
    for i in range(half, len(m) - half):
        neigh = m[i - half:i] + m[i + 1:i + 1 + half]
        local = statistics.median(neigh)
        if local > 5.0 and m[i] < local * ratio:
            hits.append((i, m[i], local))
    return hits


runs = [
    (r"C:\temp\intel87\2026-09-19 15-48-23.mkv", 65, 128, "unfixed baseline"),
    (r"C:\temp\intel88\2026-09-19 16-22-12.mkv", 52, 115, "edge write (reverted)"),
    (r"Y:\MWD\videos\obs\2026-09-19 21-45-58.mkv", 50, 112, "unblank fix"),
    (r"Y:\MWD\videos\obs\2026-09-19 22-06-06.mkv", 43, 106, "idle confirm (32)"),
    (r"Y:\MWD\videos\obs\2026-09-19 22-15-14.mkv", 44, 106, "strict flip settle"),
]

for path, lo, hi, label in runs:
    m = means(path, lo, hi)
    if not m:
        print("%-24s could not open" % label)
        sys.stdout.flush()
        continue
    d = isolated_dips(m)
    secs = len(m) / 60.0
    depth = statistics.median([h[1] / h[2] for h in d]) if d else 0.0
    print("%-24s frames=%4d span=%5.1fs  ISOLATED DIPS=%3d  rate=%5.2f/s  "
          "median depth=%.0f%% of neighbours"
          % (label, len(m), secs, len(d), len(d) / secs, depth * 100))
    sys.stdout.flush()
