# Where the 20 µs before a Gen3 draw goes: clipping, and a scanout census read on every draw

Date: 2026-11-01

Machine: the netbook (W9QI2BE2, 945GSE / GMA 950, Atom N280 1.66 GHz),
boot 261, build `07693792-dirty` (= `c30d34ae`'s code), omit mask 0.

Follows [step 0](2026-11-01-gen3-batch-cost-step0-profile.md), which
found the shared core's front end at 19-20 µs of a 45-49 µs Direct3D
submission, double the 10.0 µs of 2026-10-01, and unexplained.
Evidence: [`../probe/core-front-end-profile-2026-11-01/`](../probe/core-front-end-profile-2026-11-01/).

## How it was run

- Half-Life mwd5 D3D, full screen, started under V9XPROF (`hlprof.ps1
  -Profiler`): a warm-up timedemo, then V9XTRACE snapshots either side of
  a second. V9XPROF samples the main thread about once a millisecond.
- Under the profiler the profiled demo ran 45.6 fps; without it the same
  build ran 47.5-47.8 (step 1). The sampling costs about 4 %.
- The demo's samples are 25.0-32.5 s of the profile (inside the demo,
  found on a 2 s timeline): 7,343 samples.
- **Statics resolved.** The HAL map lists public symbols only, so
  `report-profile.ps1` used to file a static function's samples under the
  public symbol before it. `halmap_statics.py` adds every function `wdis`
  names in each object (1,146 statics, 97 modules), placing each module
  from one of its public symbols. The map is the one linked into the
  deployed DLL: the package copy differs from `build\ddraw-hal` in ten
  timestamp bytes only.
- Microseconds below are each group's share of the HAL's samples times the
  HAL entry's 46.8 µs a submission from the snapshots (131,578
  submissions, 76.1 % state repeats, as step 1's warm runs).

## Where the demo's CPU goes

| module | share |
|---|---:|
| V9XHAL.DLL | 53.2 % |
| HL.EXE | 25.6 % |
| D3DIM.DLL | 11.4 % |
| CLIENT.DLL | 4.4 % |
| KERNEL32.DLL | 4.1 % |

Inside the HAL:

| group | samples | of the HAL | µs a submission |
|---|---:|---:|---:|
| clipping (`r3d_clip`) | 772 | 19.8 % | **9.2** |
| core walk and lists (`d3d_core`) | 364 | 9.3 % | 4.4 |
| cull, records, state, select, trace, Win16 lock | 229 | 5.9 % | 2.7 |
| *front end, sampled* | *1,365* | *34.9 %* | *16.3* |
| engine: `d3d_i9xx` (ring submit 9.6, render drain 3.1, the rest) | 1,375 | 35.2 % | 16.5 |
| engine: decode and its float checks | 483 | 12.4 % | 5.8 |
| engine: **scanout register reads** | 268 | 6.9 % | **3.2** |
| engine: vertex runs, ring plan, state and texture builders | 416 | 10.6 % | 5.0 |

The TSC buckets over the same demo put the front end at 20.5 µs and the
decode at 5.7 µs; the sampled split agrees with the second and puts most
of the first in clipping.

## Clipping

`R3dListClipped` 81,409 of 135,645 list calls (60 %). The clip module's
largest functions: `v9x_r3d_draw_list` 2.6 µs, `v9x_r3d_clip_triangle`
2.1, `v9x_r3d_triangle_on_target` 1.9, the copy and interpolation helpers
the rest.

This is why the front end doubled since 2026-10-01. `0bdf0c1a`
(2026-10-07) moved Gen3's clipping into the core: the Gen3 vertex builders
and the stream decoder accept only vertices on the target (the
memory-safety design), so before it an off-screen vertex refused its whole
batch, and Half-Life's refused batches cost nothing to draw. The 10-01
profile measured a front end that did not clip.

The [2026-10-25 record](2026-10-25-intel-indexed-draws-and-hardware-cull.md)
turned down a Gen3 guard band because nothing public says Gen3 rasterises
a vertex far off the target correctly, and noted it could be measured the
Rage 128's way (V9XD32 T10). A guard band is the one change that removes
most of this 9.2 µs rather than shaving it.

## The scanout census

`d3d_i9xx.c` calls `v9x_scanout_displayed_offset()` before every draw to
count draws into the displayed buffer (the intel74 diagnostic:
`DrawsToFront`/`DrawsToBack`, the last target and displayed offsets). On
Gen3 that is `v9x_i9xx_scanout_pipe`: both PIPECONFs, then the active
pipe's display registers, each an uncached MMIO read. It is 3.2 µs a
submission, 6.9 % of the HAL, spent on a counter. Half-Life presents by
Blt here (no flips in the window), so the flip wait before it returned at
once and is not part of the figure. The call is Gen3's only one.

## What it says

- **Two thirds of the front end is clipping**, and the clipping arrived
  with `0bdf0c1a`. It is the price of Gen3's on-target-only vertex
  builders.
- **One diagnostic costs 3.2 µs a draw.** Of all the costs here it is the
  one that buys nothing for the frame.
- **The ring submit's 9.6 µs is mostly the GPU**: step 1's TSC split puts
  4.8 µs of it in waiting for ring space and 2.7 in the write.
- D3DIM, Half-Life and its client DLL are 41 % of the demo's CPU; the HAL
  cannot reach them.

## Not established

- One profiled run; the sampling's own 4 % cost.
- The microsecond column apportions a mixed-window TSC figure by sample
  share; it is an estimate, not a timing.
- How many of the clipped triangles a guard band would pass whole; that
  needs the clip counters split by how far off the target a vertex was.
- Whether the census could be read less often without losing what intel74
  needed it for.
