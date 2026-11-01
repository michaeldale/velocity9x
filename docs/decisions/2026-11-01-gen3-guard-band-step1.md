# The GMA 950 keeps every write inside its drawing rectangle to 3,968 pixels off it; Gouraud holds to 2,048

Date: 2026-11-01

Machine: the netbook (W9QI2BE2, 945GSE / GMA 950, Atom N280), boot 263,
build `3c736490-dirty` (this change), all eight intel-gma files by WININIT
rename, every hash matching; 1024x576x16 desktop.

Plan: [intel-gen3-guard-band.md](../plans/intel-gen3-guard-band.md),
step 1, approved by Michael with a separate probe.
Evidence: [`../probe/gen3-guard-band-2026-11-01/`](../probe/gen3-guard-band-2026-11-01/).

## What was built (measurement only)

- **The GUARD variant**, bit 0x100 of the mask `V9XTRACE -omit:` and the
  probe set (`hsw_flush_omit`, zero at every boot), with the band d in
  bits 16-27. Under it `v9x_d3d_i9xx_ready` sets `clip_in_core` 0, and the
  run builders (`v9x_i9xx_vertex_set_band`) and the decoder
  (`guard_band`) accept a vertex up to d off the target, refusing beyond.
- **INSET**, bit 0x200: DRAW_RECT 32 pixels inside the target on every
  side, and the decoder (`draw_inset`) requires exactly that rectangle.
- **The flat builds only.** All of it is under `V9X_I9XX_TWO_UNITS`, the
  switch the two-unit work used, so the 16-bit driver's code is unchanged.
  A first version put its I9XXCODE segment 409 bytes over the 57,344-byte
  budget, and the two appended limits fields alone 4 bytes over (its
  callers' frames grew); both are now 32-bit only.
- **`V9XGBND.EXE`** (`tools\diag\guard_band_probe_win32.c`): three 128x128
  surfaces (before, target, after; here exactly adjacent, 32 KiB apart),
  DrawPrimitives called directly as V9XRCLP does. For each d, six
  triangles reaching d past the rectangle (each side, a corner, a Gouraud
  one over three edges), drawn clipped by the core as the reference and
  then unclipped under GUARD and INSET. It counts changes in the 32-pixel
  frame, the pitch padding and both neighbours, and refusals, breadcrumb
  timeouts, engine resets and FIFO and idle timeouts; it stops at the first
  d with any.
- **Host test** `test_guard_band`: the span helper, the builders' band,
  the decoder taking exactly the band and inset it is told, and refusing
  either on a scene. Written after the code; not seen failing first.

## Control

Mask 0 on boot 263: V9XD32 `/ddi6` and `/ddi5` PASS, V9XGLP PASS,
V9XGLIDP PASS, V9XR3DP COMPLETE.

## Results

d = 64 was run alone first and looked at, then 64 to 3,968.

| d | outside the rectangle (frame / padding / neighbours) | refusals, timeouts, resets | core clipped | interior against the reference |
|---:|---|---|---:|---|
| 64 | 0 / 0 / 0, all shapes | 0 | 0 | edges: 3 px; Gouraud 217 px, within 1 step |
| 256 | 0 / 0 / 0 | 0 | 0 | edges: 1-2 px a shape; Gouraud 632 px, within 1 step |
| 1024 | 0 / 0 / 0 | 0 | 0 | edges: 2 px; Gouraud 191 px, within 1 step |
| 2048 | 0 / 0 / 0 | 0 | 0 | flat shapes identical; Gouraud 1,488 px, within 1 step |
| 3968 | 0 / 0 / 0 | 0 | 0 | flat shapes identical; **Gouraud all 4,096 px, up to 31 steps** |

"Edges" are single pixels on a triangle's edge, differing by a whole
channel (21 or 43 steps of 63): coverage decided on the other side of the
sample point by the reference's clipped vertices, not a wrong colour. The
images (`shapes-64.png`, `gouraud-by-distance.png`) show every triangle
cut exactly at the rectangle and the frame untouched. At 3,968 the Gouraud
triangle is drawn flat magenta across the rectangle, where 2,048 is the
expected green-brown gradient.

The probe left the mask as it found it (0).

## Readings

- **No write left the drawing rectangle at any distance measured**, up to
  3,968 pixels past it on every side and corner, with the neighbouring
  surfaces adjacent. This is the memory-safety question, and the answer
  to 2026-10-25's objection, for this machine and these shapes.
- **Interpolation breaks between 2,048 and 3,968.** Coverage is still
  right at 3,968; colour is not. A texture coordinate is interpolated the
  same way and is expected to fail with it; textures were not drawn.
- **The band, by the plan's rule**: 2,048 is the largest d passing all
  three tests; halved for margin, **1,024**. With d measured past the
  drawing rectangle, that is a vertex within 1,024 pixels of the target.

## Hypotheses this disputes

- 2026-10-25: "the only evidence available would be V9XD32 T10's three
  samples". The frame-and-neighbour canary is stronger evidence than T10,
  which the Rage 128 passed while corrupting memory elsewhere.
- The old engine table's comment that Gen3 "clips inside the 4096 guard
  band": coverage holds to 3,968, but colour does not, so 4,096 is not a
  usable band.

## Not established

- One run, one machine; the GMA 900 and GMA 3150 not run.
- Textured triangles, depth, fog and two units under the band.
- Whether games behave: step 2 (3DMark 2000, Half-Life, UT99's intro,
  3DMark 2001 SE) is next and needs the netbook again.
- Where between 2,048 and 3,968 interpolation fails.
