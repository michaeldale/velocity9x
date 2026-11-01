# Gen3 reads the front/back census once per flip, not once per draw

Date: 2026-11-01

Machine: the netbook (W9QI2BE2, 945GSE / GMA 950, Atom N280 1.66 GHz).
Before: boot 261, `07693792-dirty` (the step 1 build, omit mask 0). After:
boot 262, `c30d34ae-dirty` (this change), all eight intel-gma files by
WININIT rename, every hash matching.
Evidence: [`../probe/gen3-census-per-flip-2026-11-01/`](../probe/gen3-census-per-flip-2026-11-01/);
the Half-Life control is step 1's mask-0 run
([record](2026-11-01-gen3-batch-cost-step1-variants.md)).

## Why

The [front-end profile](2026-11-01-gen3-core-front-end-profile.md) put
3.2 µs of each Half-Life submission in `v9x_i9xx_scanout_pipe`: before
every draw, `d3d_i9xx.c` read the pipe's configuration and display
registers, uncached, to count draws into the displayed buffer (intel74's
`DrawsToFront`/`DrawsToBack`).

## The change

`v9x_d3d_i9xx_displayed_offset` keeps the last plane base read and reads
it again only when it may have moved: the present sequence advanced (an
accepted flip, any process's), this draw just waited out a pending flip,
a new session began (`v9x_d3d_i9xx_reset`), or 256 draws have passed since
the last read. The last bounds how long a change the HAL does not see (a
mode set, a DOS box, FlipToGDISurface) can be counted against the old
buffer. The counters and their meaning are unchanged; the ABI comment says
when the register is read.

## Results

| | before | after |
|---|---:|---:|
| Half-Life mwd5 D3D, profiled warm demo | 47.67 fps | 48.03 fps |
| HAL entry, µs a submission | 45.2 | 44.5 |
| engine draw | 26.0 | 25.2 |
| build (the engine draw less its timed waits) | 10.2 | **8.8** |
| ring-space wait (GPU behind) | 5.7 | 6.2 |
| front end | 19.2 | 19.3 |
| 3DMark 2000 (1024x576x16, all tests but image quality) | 1194 | 1192 |
| 3DMark: DrawsToFront / DrawsToBack | 0 / 1,219,087 | 0 / 1,222,925 |
| 3DMark: flips handled | 26,075 | 26,326 |
| V9XD32 /ddi6, /ddi5; V9XGLP; V9XGLIDP; V9XR3DP | pass (boot 261) | pass (boot 262) |

## Readings

- **The build is 1.4 µs a submission cheaper**, not the profile's 3.2:
  that figure apportioned a mixed-window time by sample share and was an
  estimate. Some of the CPU saved went into waiting for ring space
  (+0.5 µs): with less to do, the CPU reaches a full ring sooner. The HAL
  entry is 0.7 µs cheaper.
- **Half-Life moved 0.8 %**, inside one run's noise; it is not claimed as
  a gain.
- **The census still answers.** 3DMark 2000 flips 26,326 times and counts
  no draw into the displayed buffer, as on the control. That is the case
  the counter exists for. A fault that drew into the front buffer between
  flips would still be caught after at most one flip or 256 draws.

## Not established

- One run each.
- No run where the census should count front draws (a broken build), so
  the counter's detection is shown only as unchanged zeros.
- The GMA 900 and GMA 3150 take the change through the shared engine;
  neither ran it.
