# Direct3D throughput in the shared core

Date: 2026-10-24

Status: **measuring.** Michael (2026-10-24): "after all that work we still
seem to be 50% in 3dmark than the native driver ... This is the focus and
I think it is part of the shared core, not just ati 128 specific." This
overrides the standing rule against chasing 3DMark scores, for this work.

## The gap, measured

A8U4I5: P3 1 GHz, 440BX, Rage Fury Pro, 3DMark 2000 build 340 default run,
1024x768x16, Z16, triple buffered. ATI is ATI 4.13.7192, 2026-10-22
([record](../decisions/2026-10-22-rage128-ati-baseline-and-tier0.md)).
Velocity9x is the CCE-ring build, boot 543, second run
([record](../decisions/2026-10-23-rage128-driver-runs-the-cce-ring.md)).

| Test | ATI | Velocity9x |
|---|---|---|
| 3DMarks | 2404 | 1249 (first run 1335) |
| CPU speed | 255 | 109 |
| Helicopter low / med / high | 42.6 / 32.8 / 16.1 | 23.3 / 18.8 / 7.1 |
| Adventure low / med / high | 41.0 / 37.9 / 29.9 | 24.1 / 17.5 / 12.3 |
| Fill rate single / multi | 128.6 / 178.2 | 129.6 / 129.6 |
| High polygon 1 / 4 / 8 lights (KTri/s) | 2341 / 2067 / 1864 | 689 / 665 / 640 |
| Texture rendering 8 / 16 / 32 MB | 136.4 / 134.1 / 121.2 | 59.8 / 59.5 / 58.4 |

The chip is not the limit: single-texture fill rate is ATI's.

## Hypotheses, from the numbers and the code, not yet measured

1. **Every flip serialises the CPU on the GPU, and the previous flip's
   latch.** `v9x_flip_body` (`ddhal_core.c`) works in four steps:
   - `v9x_wait_idle`, then `v9x_render_drain`, so the whole frame must
     finish before the flip;
   - then `v9x_flip_done`, which answers WASSTILLDRAWING until the
     previous start address is latched.

   The Rage 128 latches `CRTC_OFFSET` at the vertical blank even when the
   flip is written "now". The texture tests sit at 58-60 fps on a 59 Hz
   desktop, while ATI's run at 121-136. 3DMark called Flip 2,571,619 times
   for about 5,960 accepted flips. The CPU builds nothing while the GPU
   draws, and the GPU draws nothing while the CPU waits.
2. **Per-triangle CPU cost in the core.** High polygon count runs at
   about 30% of ATI's, and CPU speed at 43%. The D3D runtime does
   transform and lighting in both cases, so the difference is the
   driver's per-batch and per-triangle work: the `R3dList` clip, cull and
   mip checks, the vertex copies into the engine's stream, and the stream
   copy into the ring.
3. **Features ATI uses and we do not:** two textures per pass (multi fill
   rate equals single), emboss bump mapping, and the second 16 MiB.
   3DMark reports 16384 KB of video memory on a 32 MiB card.

## Measured, 2026-10-24

The Rage 128 is timed from boot 544. In the default run (388 s), the
buckets nest, so they do not sum:

| Bucket | Boot 544, uncached ring (1322) | Boot 545, write-combined ring (1444) |
|---|---|---|
| D3D entry points, everything inside | 162 s | 134 s |
| Ring write, total | 106 s | 70 s |
| of which waiting for ring space | 29 s | 43 s |
| Rage 128 decode (map and assembly) | 22 s | 24 s |
| Render drain (CPU waits for GPU) | 43 s | 45 s |
| Flip (2.7 M calls, about 6,000 taken) | 50 s | 52 s |

**Step done: the ring is write-combined**, as ATI's mini-VDD maps it:
reserve `PG_WRITECOMBINED`, commit with flags 0, and a locked instruction
before each `DL_WPTR` write. Uncached writes cost 60 cycles a dword;
write-combined writes cost 5.3 (`R128CCE /time`). VGARTD refuses a
write-combined commit. The reservation lands at `DA000000`, so
`AGP_BASE` is `DA000000` and the offset is 0: ATI's placement.

What is left:
- **The CPU waits for the GPU:** ring space (43 s) plus drains (45 s).
- **Flip:** 52 s.

Two causes, one shared and one the Rage 128's:
- **Shared core:** the flip serialises frame N+1's building behind frame
  N's drawing (hypothesis 1).
- **Rage 128:** each batch is at most 64 triangles (38 on average over
  50.4 M), and each ends with a GUI pixel-cache flush that stalls the
  engine, 1.33 M times in one run. Linux's r128 flushes at clears and
  swaps, not per primitive.

## Steps

1. Time the Rage 128 (`V9X_TIME_ENABLED` on `ATI_RAGE128`). Use the
   existing buckets: D3D calls, decode (the Rage 128's map and assembly),
   ring write, ring space wait, render drain, flip, lock. Take one clean
   boot and one 3DMark run, and split the wall time.
2. Fix whichever of 1 and 2 dominates, in the core where the cause is
   there, so every engine gets it ([[same-design-fix-applies-to-all]]).
   For 1, that means flips queued behind rendering instead of the CPU
   waiting for both. For 2, that means less work per triangle on the path
   every engine shares.
3. Then the features in 3, on the Rage 128.

Each step is measured on A8U4I5 against the table above, and on a second
engine (the netbook's Gen3) for any core change.
