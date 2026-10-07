# The Rage Pro composite's mip level follows W unless its coordinates arrive premultiplied: with TEX_ST_DIRECT the level is right, and Quake 2 and Half-Life draw single-pass on the Rage XL

Date: 2026-10-08
Machine: A8U4I5, ATI Rage XL PCI (1002:4752), 800x600x16, boots 337-341.
Driver: V9XDISP.DRV 0.12.0 (5904c9e-dirty) as installed, with HALs built
from branch `rage-pro-composite` (948649a, then bece268) and the
installed 0.12.0 HAL for baselines. The machine was left on 0.12.0.
Instrument: V9XGLP's new SgisMip section
(`tools/diag/gl_probe_win32.c`, 2ae6271), V9XTRACE snapshots, the ICD
log.
Evidence: [`../probe/a8u4i5-rage-xl-composite-direct-2026-10-08/`](../probe/a8u4i5-rage-xl-composite-direct-2026-10-08/)
Supersedes the outcome of
[2026-10-07](2026-10-07-rage-pro-composite-multitexture.md): the
composite is now enabled.

## Why

The composite sampled unit 0 at level 0 and halved Quake 2 (2026-10-07).
Keeping unit 0's chain made Quake 2 faster than two passes but drew its
walls without their detail ([2026-10-08 draw cost](2026-10-08-rage-xl-draw-cost.md)).
Which level the engine read, and why, was unmeasured.

## The probe

A 64x64 texture whose seven levels are each one solid colour, drawn
NEAREST_MIPMAP_NEAREST over squares of 64, 32, 16, 8 and 4 pixels, so the
right level is 0 to 4. Each square's centre is named by the level whose
colour it is. Unit 1 is white under MODULATE, which leaves the colour
alone: 2x2 and 64x64 with coordinates 0..1, and 2x2 with 0..8. Then one
change at a time from what Quake 2 does: unit 0 LINEAR_MIPMAP_NEAREST
with LINEAR magnification, unit 1 LINEAR, and the square in perspective
at eye depth 2, so every vertex's W is 2.

## Measured

| HAL | unit 1 off | unit 1 on, W = 1 (sizes, rates, filters) | unit 1 on, W = 2 |
|---|---|---|---|
| 0.12.0, one unit (boot 337) | L0..L4, exact | not offered | not offered |
| composite, chain kept, TEX_ST_MULT_W (boot 338) | exact | exact in all 20 | L1..L5: one level coarse at every size |
| composite, TEX_ST_DIRECT, S and T premultiplied (boot 339) | exact | exact | L0..L4, exact |

So under the composite the level came out log2 W too coarse. The
setup builder sends plain S and T and lets the engine multiply them by W
(TEX_ST_MULT_W), the mode Phase 4 measured for one texture. Mesa's
driver, which drove the composite, sent S*W and T*W under TEX_ST_DIRECT.
Doing the same under the composite (bece268) puts every case on its
level. A Quake 2 wall a few hundred units away has W in the hundreds,
which is why its walls read their smallest levels. One-texture draws keep
TEX_ST_MULT_W. Why the composite treats W differently is not known; the
evidence only shows that it does.

The SGIS Modulate, CoordLeft and CoordRight cases still read right on the
DIRECT HAL.

## Quake 2 and Half-Life

Recorded, not benchmarks.

| | composite (TEX_ST_DIRECT HAL) | two passes |
|---|---|---|
| Quake 2 demo1 spawn, timerefresh, 640x480 window | 17.9 fps | 15.5 fps (same HAL, `gl_ext_multitexture 0`) |
| Quake 2 two-unit batches / refused two-unit | 328,286 / 0 | - |
| Half-Life `timedemo mwd5`, 400x300 window, measured run | 16.6 and 16.0 fps (two runs) | 14.8 fps (0.12.0 HAL) |
| Half-Life two-unit batches / refused | 45,796 / 0 | - |

- Quake 2's spawn view keeps the panels, stripes and screen that two
  passes draw (`q2-mt1-spawn.png` against `q2-mt0-spawn.png`). The
  composite frame is slightly lighter; that was not investigated.
- Every refused batch in both programs is the one-unit texel-alpha
  blend already open
  (`docs/issues/2026-10-03-rage-iic-texel-times-vertex-alpha-refused.md`):
  Quake 2's `hw-refused` equals the `TEXTURE_OP` count.
- Half-Life's `-nomtex` does nothing in this build (identical batch
  counts to the multitexture run, deterministically), so its two-pass
  figure comes from the HAL that does not offer the extension. Its
  second composite run had `-nomtex` on the command line for that
  reason.
- Half-Life drew from its saved 400x300 mode, not the 640x480 asked for.
  Its registry key was exported before its video dialog was touched
  and put back after; the export afterwards matches.

## What stays open

- Unit-1 REPLACE, BLEND and DECAL are refused and, with no CPU fallback
  on this engine, draw nothing. Neither program here used them; a
  program that does would lose those draws where it used to get two
  passes.
- ATI's other composite functions (modulate x2, linear blend, add) are
  not programmed.
- Trilinear on unit 0 is folded to bilinear within the selected level
  under the composite, as its blend function is the composite's.
- The Gateway's Rage Mobility-M shares this code and was not run.

## Gates

`build-host.ps1` (each changed test watched failing first),
`check-tree.ps1`, and `run-checks.ps1` on the branch and on `main` after
the merge (7084831), all passing.
