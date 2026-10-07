# Where a Rage XL draw's time goes: the chip filling at 640x480, the driver's setup at 320x240, and not the texture-cache flush

Date: 2026-10-08
Machine: A8U4I5, ATI Rage XL PCI (1002:4752), 800x600x16, boots 334-337.
Driver: V9XDISP.DRV 0.12.0 (5904c9e-dirty) as installed, with measurement
HALs built from d1ae711 (and, for the last section, branch
`rage-pro-composite` 948649a); the machine was left on its 0.12.0 HAL.
Instrument: the Mach64 draw's cost split (d1ae711: `r2_cycles`/`r2_work`
charged by `v9x_d3d_mach64_charge`, the render interface's
`r3d_draw_cycles`), read by V9XTRACE snapshots either side of Quake 2's
`timerefresh`.
Evidence: [`../probe/a8u4i5-rage-xl-draw-cost-2026-10-08/`](../probe/a8u4i5-rage-xl-draw-cost-2026-10-08/)
(`q2-cost.ps1` drives a run; `cost.py` turns two snapshots into the
`*-cost.txt` tables).

## Why

Quake 2 on the Rage XL spent about 0.4 ms per render-interface draw,
and the composite experiment
([2026-10-07](2026-10-07-rage-pro-composite-multitexture.md)) lost on
the number of draws. The ICD's own timer stops at the render interface;
nothing said where inside the HAL the time went.

## The method

Quake 2 demo, `+set vid_ref gl +set vid_fullscreen 0 +set gl_mode <m>
+set cheats 1 +map demo1`, `notarget`, a snapshot, `timerefresh` (128
frames), a snapshot. The counters subtract to the refresh plus the few
seconds either side, so the per-batch and per-triangle figures are the
measurement and the totals are not. Parts of an accepted draw: PREPARE
(policy and state), BUILD (vertices and setup packets), SPLIT (the
state's emission) and EMIT (the packets' emission); each emission
includes its FIFO waits.

## Measured

| Run | fps | triangles a batch | PREPARE a batch | BUILD a triangle | EMIT a triangle | FIFO reads a register write |
|---|---|---|---|---|---|---|
| `cost0`, 640x480 | 15.4 | 36.2 | 5,500 | 2,349 | 12,767 | 1.72 |
| `cost0m0`, 320x240 | 41.2 | 34.0 | 5,307 | 2,369 | 2,924 | 0.25 |
| `noflush`, 640x480, TEX_CACHE_FLUSH never set | 15.5 | 36.7 | 5,326 | 2,357 | 12,834 | 1.72 |

Cycles are the HAL's TSC; at 640x480, EMIT is 79% of the render
interface's time and the render interface outside the engine draw
(validation, the Win16 mutex, the FPU save) 4%.

- **At 640x480 the draw is the chip.** Emission waits for FIFO room: 1.7
  status reads for every register written. A quarter of the pixels cut
  that wait by more than four times per triangle while building the
  packets costs the same, so the chip's limit is filling pixels, not
  accepting triangles or registers.
- **At 320x240 the driver's setup is half the time.** BUILD, about 2,360
  cycles a triangle whatever the resolution, is 39% of the render
  interface there against EMIT's 48%.
- **The texture-cache flush in every textured state costs nothing
  measurable.** A HAL that never set it ran the same rate and the same
  per-triangle emission (a measurement build only; uploads need the
  flush, per the texture-mutation gate).
- Quake 2's textures arrive as chains the sampler uses: 141,915 chain
  checks, 7 levels, no gaps.

## The composite with level selection (branch only)

The 2026-10-07 composite sampled unit 0 at level 0. With unit 0's chain
kept (MIP_MAP_DISABLE clear, no trilinear; branch 948649a), boot 336,
640x480:

| `gl_ext_multitexture` | fps | triangles a batch | EMIT a triangle | FIFO reads a write |
|---|---|---|---|---|
| 1 (composite) | 19.8 | 13.2 | 8,241 | 0.83 |
| 0 (two passes) | 15.5 | 36.8 | 12,619 | 1.69 |

333,176 two-unit batches, none refused. The rate is not a result: the
spawn view (`cmip1-spawn.png` against `cmip0-spawn.png`) has the world
textures without their detail, smooth colour where two passes show
panels and stripes. Unit 0 reads levels far smaller than it should, and
small levels are cheap to fill. Which level the engine selects under the
composite, and from what, was not measured. Sampling level 0 and
selecting a level have now each been ruled out, with different
symptoms.

## Hypotheses the evidence killed

- That the 0.4 ms is driver bookkeeping. At 640x480 it is waiting for the
  chip; redundant-state skipping would save at most the state's 2-3%.
- That the per-batch TEX_CACHE_FLUSH costs fill.
- That level selection simply works under the composite.

## What follows

- On this machine at the resolutions games use, a faster Rage path means
  fewer pixels or cheaper ones, which the driver does not choose. The
  composite would be that lever if its level selection were understood:
  a texture whose levels are tagged by colour, drawn minified under the
  composite, would say which level the engine reads.
- BUILD, about 2,360 cycles a triangle, is the driver's own cost and
  matters where the chip waits less - low resolutions, and every
  Rage Pro-class part faster at filling than this PCI one.
