# Gen3 guard band, step 3: shipped at 1,024 pixels with a classifier

Date: 2026-11-04. Machine: the netbook (W9QI2BE2, 945GSE / GMA 950),
boot 269, 1024x576x16 desktop. Build `e44c8882-dirty` (step 3 on the
rebased `park/gen3-batch-cost`), deployed DRV, VxD, HAL, SETP and ICD by
WININIT rename; staged files gone, installed HAL, DRV and ICD equal to
the package by SHA-256. Plan: [intel-gen3-guard-band.md](../plans/intel-gen3-guard-band.md),
step 3. Evidence: `docs/probe/gen3-guard-band-step3-2026-11-04/`.

After the deploy, the INSET variant was made to need the GUARD bit again
(as it did before step 3). The netbook build lacks that one-line change;
with the mask zero, as every run here was, it changes nothing.

## What changed

- `clip_in_core` 0 for Gen3; `guard_band_ddi6` 1, so DDI 6 programs are
  still told the 4,095 band they were told while the core clipped.
- `v9x_d3d_i9xx_draw` classifies: triangles inside 1,024 pixels of the
  target go as runs; one past it is cut by `v9x_r3d_clip_triangle` and
  drawn as its fan; one the clipper refuses, or a two-unit one past the
  band, is dropped. Counters `I9xxGuardCut`, `I9xxGuardDropped`.
- The band test is `v9x_i9xx_band_holds`, the run builders' own bounds,
  host-tested (`test_band_holds`).
- GUARD remains a measurement override of the band; band 0 under it is
  the old rule (everything off the target cut).

## Results (mask 0 unless stated)

| Run | Result | Gen3 refusals | Cut | Dropped |
|---|---|---|---|---|
| V9XD32 `/ddi6`, `/ddi5` | PASS, PASS | 0 | 2 | 0 |
| V9XDDP | COMPLETE | 0 | 0 | 0 |
| V9XDDP, GUARD band 0 (control) | COMPLETE | 0 | 2 | 0 |
| 3DMark 2000 default | completes, 1193 | 0 of 1,196,270 | 0 | 0 |
| UT99 CityIntro, 60 s | draws (frame at 30 s) | 7, all z | 0 | 0 |

- V9XDDP's 58 `...Ok=1` / 21 `...Ok=0` keys are identical under the
  band and under the band-0 control, so none of the 21 is the band's.
  Whether they match the netbook before step 3 is not shown here; no
  netbook V9XDDP with these keys is in the tree.
- UT99's 7 refusals are check 2 (z outside [0, 1]: z 1.045, rhw
  negative), not position. Step 2's runs refused 12 (mask 0, core
  clipping) and 22 (GUARD) on the same intro; it predates step 3.
- No triangle in 3DMark 2000 or UT99 reached past 1,024 pixels; what
  they send off the target is now drawn uncut. There is no counter for
  triangles drawn off the target inside the band.

## Not done

- Half-Life mwd5 D3D: step 2's driver script was in an old session's
  scratchpad and is gone; not run.
- 3DMark 2000's frames were not grabbed, only its completion and score.
- 3DMark 2001 SE (its flicker issue is separate and open).
- The GMA 900 and GMA 3150: same engine, same code; not run.
