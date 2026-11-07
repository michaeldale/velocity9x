# Gen3 depth at the edge: 3DMark 2001 SE refuses no batches

Date: 2026-11-07. Machine: the netbook (W9QI2BE2, 945GSE / GMA 950),
boot 284, build `089a5e70`. Follows
[the guard-band merge](2026-11-07-gen3-guard-band-merge.md). Evidence:
[`../probe/gen3-depth-edge-2026-11-07/`](../probe/gen3-depth-edge-2026-11-07/).

## The change

Under the guard band, 3DMark 2001 SE lost 293 batches. Each was refused
for one vertex whose z was a hair below 0 (last `B5800020`, about
-2^-20). The run builders refuse any z outside [0, 1], and negative
zero too.

`v9x_i9xx_float_depth_edge` (`i9xx_float.c`) moves a depth within one
Z16 step (2^-16) outside [0, 1] to the nearest edge, and negative zero
to +0. A 16-bit depth buffer cannot tell such a vertex from one on the
plane. Anything further out, and infinities and NaNs, comes back
unchanged and is still refused, so UT99's z 1.045 with a negative rhw
behaves as before. The HAL applies it as each vertex is copied into the
stream, and counts the vertices it moved in `I9xxZEdged`.
`test_depth_edge` was written first and failed on its five edge cases
against a pass-through.

Gates: `check-tree.ps1`, `build-host.ps1` and `run-checks.ps1` passed.
Deployed by WININIT rename and `reset`; all eight installed files match
the package by SHA-256; `Stage=enable-ok`.

## Results

| | 0.39.0 (boot 282) | Guard band merged (boot 283) | Depth at the edge (boot 284) |
|---|---|---|---|
| V9XD32 `/ddi6`, `/ddi5` | not run | PASS, PASS | PASS, PASS |
| 3DMark 2001 SE: batches | 1,273,920 | 1,447,116 | 1,439,872 |
| refused | 2,551 | 293 (z) | **0** |
| depths moved to the edge | (no counter) | (no counter) | 317 |
| texture coordinates zeroed | (no counter) | 7,100 | 6,964 |
| cut past the band / dropped | (core clipped) | 471,099 / 0 | 462,828 / 0 |
| `DrawsToFront`, engine resets | 0, 0 | 0, 0 | 0, 0 |
| `DrawsIntoPresented` | 0 | 0 | **11** |
| score (recorded, not compared) | 676 | 687 | 683 |

The batch counts exclude V9XD32's 3,145 in each boot.

## Readings

- **No batch is refused any more**: 3DMark 2001 SE now draws every batch
  it sends. 317 vertices were moved, about one per refused batch of the
  previous run, as expected.
- **`DrawsIntoPresented` went from 0 to 11.** It counts batches aimed at
  the buffer the most recent flip named. `DrawsToFront`, which compares
  each batch with the buffer the plane is scanning at submission, stayed
  0. One reading: some of the batches that used to be refused draw into
  the presented buffer on purpose, for example an overlay with a depth
  near 0. That is a guess. Nobody has looked at which batches the 11 are,
  and the present trace was not armed.
- Whether the flicker is gone **was not seen**. Nobody watched the panel.

## Not done

- A watched run of 3DMark 2001 SE, and an armed present trace to say what
  the 11 batches are.
- Half-Life and 3DMark 2000 on this build: they sent no out-of-range
  depth on boot 283, so this change cannot reach them; not rerun.
- The GMA 900 and GMA 3150.
