# Gen3 guard band merged: 3DMark 2001 SE refusals 2,551 to 293, Half-Life clean

Date: 2026-11-07. Machine: the netbook (W9QI2BE2, 945GSE / GMA 950,
Atom N280), 1024x576x16 desktop. Plan:
[intel-gen3-guard-band.md](../plans/intel-gen3-guard-band.md).
Evidence: [`../probe/gen3-guard-band-merge-2026-11-07/`](../probe/gen3-guard-band-merge-2026-11-07/).

## What was merged

`park/gen3-batch-cost` was rebased from 0.37.0-dev onto 0.40.0-dev main
as `merge/gen3-guard-band`, with one commit added on top (`6f581a9f`). The
branch carries:
- the guard band (step 3): no clipping in the core, a 1,024-pixel band,
  and a classifier that cuts only triangles past it;
- non-finite texture coordinates drawn as 0;
- vertex-refusal counters;
- the front/back census read once per flip.

The rebase conflicted in two files. Both sides had appended fields to
`V9X_D3D_DIAGNOSTICS`, and both had extended the trace dump. Main's
fields (`alpha_diffuse_draws`, the state census) shipped in 0.37.0 to
0.39.0, so they keep their offsets and the branch's follow them.

The added commit does two things:
- It removes the two batch-cost skips that the branch's own record
  priced and found not to pay (`OMIT_STATE`, `OMIT_TEXTURE`).
- It corrects the comments that still called the band "measurement
  only", with zero as the shipped rule. The inset rectangle alone is
  still measurement only.

Gates on `6f581a9f`: `check-tree.ps1`, `build-host.ps1` and
`run-checks.ps1` passed.

## Deploy

The DRV, VxD, HAL, SETP, ICD, GLIDE2X, GLIDE3X and ATI3DCIF from the
`6f581a9f` package went in by a rename-only WININIT.INI, then `reset`
(boot 283). Every staged file was gone afterwards, and all eight
installed files match the package by SHA-256. `V9XBOOT.INI`:
`Stage=enable-ok`, 1024x576x16, `MtrrWrite=ok s=2 b=d0000001`.

## Results

| Run | 0.39.0 release, boot 282 (control) | Merge build, boot 283 |
|---|---|---|
| V9XD32 `/ddi6`, `/ddi5` | not run | PASS, PASS, 0 failures |
| 3DMark 2001 SE default, 640x480x16, all tests: batches | 1,273,920 | 1,447,116 (excluding V9XD32's 3,145) |
| batches refused (reason 6, a vertex) | **2,551** | **293**, all z |
| texture coordinates drawn as 0 | (no counter) | 7,100 |
| triangles cut past the band / dropped | (core clipped 9,000,564) | 471,099 / 0 |
| draws into the displayed buffer, engine resets | 0, 0 | 0, 0 |
| score | 676 | 687 (recorded, not compared) |
| Half-Life mwd5 D3D, windowed, 3 runs | not run | 46.4, 56.6, 56.6 fps; 967,967 batches, 0 refused, 0 cut |
| 3DMark 2000, new custom project (1024x576x16, Z16, triple buffer) | not run | completes, 1201 (recorded, not compared); 1,196,473 batches, 0 refused, 0 cut |

Readings:
- **The control is the shipped release.** 0.39.0 refuses 2,551 batches in
  3DMark 2001 SE, almost exactly the 2,557 of the issue's 2026-11-01 run
  on a dev build. So the refusals behind the
  [flicker issue](../issues/2026-11-01-gma950-3dmark2001se-flicker-refused-vertices.md)
  are in every release since at least then.
- **The merge removes 89 % of them, and the band brings some back.** The
  branch's 2026-11-01 run had the texture-coordinate fix and the core
  still clipping, and it refused 0 (the issue record). The 293 left now
  are all depth. The last one has z = `B5800020` (about -9.5e-7, just
  below 0) and rhw 1.0. The core's clipper does not clamp z
  (`r3d_clip.c`), so the likely mechanism is this: with the core
  clipping, a triangle crossing the screen edge was cut there, and a
  near-zero vertex beyond the edge left with the cut. Under the band the
  triangle arrives whole, and the run builders refuse its batch for
  z < 0. That reading is inferred from the code, not measured.
- So against the release this is a gain (2,551 to 293). Against the
  branch before step 3 it is a loss (0 to 293). Each refusal still drops
  a whole batch. The next step is to treat z a rounding step outside
  [0, 1] as the edge, in the HAL rather than by refusing the batch, and
  run 3DMark 2001 SE again. That is not part of this merge.
- Whether the flicker is gone **was not seen**: nobody watched the panel,
  and the agent's screenshots are blind during flips. The counters say
  how many batches are still lost, not what the eye sees.
- Half-Life and 3DMark 2000 send nothing past the band and nothing
  refusable. Half-Life's first run is a cold one, as the benchmark's
  README expects.
- 3DMark 2000's "Run Default Benchmark" asks for 1024x768x32, which the
  netbook's panel does not offer, and stops at a warning. A new custom
  project picks 1024x576x16 itself.

## Not done

- A visual check of 3DMark 2001 SE on the panel.
- The GMA 900 and GMA 3150: same engine, same code, not run.
- UT99, run under step 3 on the parked branch, was not repeated.
