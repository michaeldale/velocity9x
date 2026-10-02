# Rage IIC texture addressing: S/T units, sampling, wrap, and the walk

Date: 2026-10-02. Machine: A8U4I5 (10.0.1.172), ATI 3D Rage IIC AGP
`1002:4757` rev `7A`, Win98SE, boot 136, Velocity9x `06f760b` installed.
Harness: `ATIRX.EXE /tex`, `/texprec`, `/textri`, `/textri2` (runner built
from `ed17f39` plus this change), mapping only through `ATIRX.VXD`.
Evidence: `docs/probe/a8u4i5-rage-iic-registers-2026-10-02/BOOT136-ATIRX-TEX*.TXT`.

Plan: [ati-rage-iic-hardware-3d.md](../plans/ati-rage-iic-hardware-3d.md),
Phase 4 steps 1-3.

## Method

A 32x32 RGB565 map at VRAM `0x220000` whose texel (u, v) is
`u << 11 | v << 5 | 31`, so every drawn pixel names the texel it sampled
(blue 31 cannot be the 5AA5 sentinel). Each scene draws the 32x8 rectangle
at (16, 16) or a trapezoid whose leading edge leans one pixel a row, with
one S/T term set at a time, and dumps rows 16, 17, 20 and 23 as u.v. Two
more maps: 16x16 at pitch 16 (`0x221000`) and 16x32 at pitch 16
(`0x221800`). SCALE_3D_CNTL = texture mapping, TEX_CACHE_DIS,
MIP_MAP_DISABLE; TEX_<size>_OFF holds the offset. The map was intact after
every run (`TextureDamage=0`); no scene wrote outside the scissor or
touched a guard; FIFO and idle timeouts and resets 0 throughout.

## Findings

1. **Unit.** S and T are normalised: 1.0 = 2^26 spans the map's larger
   dimension, on both axes. One texel of the 32 map is 2^21
   (TEX.TXT, TEX2.TXT T1); of the 16x16 map 2^22 (TEX3 T9); of the 32x16
   and 16x32 maps 2^21 on both axes (TEX3 T10, TEX4 T14). The 10.11
   START field (bits 25:5) is ten integer bits of a 1024 map.
2. **Sampling** is floor(): u = S >> (26 - size). Starting half a texel in
   changes nothing (T2); a half-texel step shows each texel twice (T3).
   Direct3D's point sample of u * width is the same floor, so the setup
   adds no bias.
3. **Wrap**, no clamp: u wraps at 2^TEX_PITCH, v at 2^TEX_HEIGHT (T4,
   T14, T15). The GT2C has no TEX_CNTL. With the pitch nibble above the
   width, u ran past the width to the pitch (T11), so the builder requires
   pitch = width; a size nibble below the pitch read outside the map (T8).
4. **Surface.** To within the accumulator's resolution S is the quadratic
   anchored at DST_Y_X: START + XINC_START dx + Y_INC dy +
   X_INC2 dx(dx-1)/2 + Y_INC2 dy(dy-1)/2 + XY_INC2 dx dy, dx counted in
   DST_X_DIR's direction (T6, T12, T13), and edge steps advance it as pixel
   steps do, in both directions (T16-T19). XINC_START is the X increment
   at the start of every row; the axes swap cleanly (T7).
5. **Against DST_X_DIR** each span pixel loses 32 units more than the
   increment: it is applied as a ones' complement at 32-unit resolution.
   T5 read 7.31 where 8.0 was expected, the T coordinate dropping below 0;
   T5b, START biased by 32, absorbed exactly one pixel's loss.
6. **Resolution** (`/texprec`). START one 32-unit step below texel 1, one
   term 2^j:
   - XINC_START and Y_INC below 32 never reach texel 1 (PX1_0..4,
     PY1_0..4): each addend is floored to a multiple of 32 as it is added.
   - X_INC2, Y_INC2 and XY_INC2 accumulate into the increments at full
     precision; the crossing column is that of `sum 32 floor(inc_k / 32)`
     for every j (PX2_1 at dx 17, PX2_2 at dx 9, PX2_3 at dx 5 where the
     closed form says 7, 5 and 4).
7. **The walk.** As a replay of the logged writes established, then the
   card confirmed:
   - an edge step adds the X increment (floored), then X_INC2 to it and
     XY_INC2 to the Y increment;
   - a new row adds the Y increment (floored), then Y_INC2 to it and
     XY_INC2 to the X increment;
   - a span pixel in DST_X_DIR's direction adds the X increment (floored)
     then X_INC2; against it, X_INC2 comes off first and the floored
     increment plus 32 is subtracted.

   `v9x_r2_st_*` in `rage2_setup.c` is this walk.

## Triangles

`v9x_r2_setup_texture` fits, per triangle, the quadratic through the exact
perspective value at the vertices and edge midpoints, re-anchored per
trapezoid. 60 random triangles, 20 affine and 40 with q screen-linear over
1..2 (`/textri`), then 60 more on another seed with q over 1..4
(`/textri2`), each pixel compared with the engine model:

| Run | Model | Pixels | Differ |
|---|---|---|---|
| TEXTRI | closed-form quadratic | 9,753 | 3 |
| TEXTRI (offline replay of the logged writes) | the walk | 9,753 | 0 |
| TEXTRI-WALK | the walk | 9,753 | 0 |
| TEXTRI2-WALK | the walk | 10,767 | 0 |

The three closed-form misses were one texel low, all in perspective
triangles, where the card's floored increments fell a texel boundary short.

## Accuracy against exact perspective (host)

`tests/host/test_rage2_setup.c`, 1,500 random triangles per row in the
64x64 grid, texture coordinates of a projected plane (q and tu q, tv q
screen-linear, up to 2 texels a pixel), every pixel's walked value against
the exact S = N / Q:

| q across the grid | Map | Worst error | Triangles over 0.5 texel |
|---|---|---|---|
| 1 (affine) | 32x32 | 0.0009 texel | 0 |
| 1..2 | 32x32 | 3.0 texels | 121 |
| 1..8 | 32x32 | 16.0 texels | 432 |
| 1..2 | 256x64 | 5.0 texels | 226 |

A quadratic through six exact points misses a rational function by about
2.5% of the texel range across an edge whose q doubles. That is the chip's
design (the Rage Pro replaced it with W interpolation), not a setup defect:
the fit is exact at all six nodes. `v9x_r2_texture_error` reads the error
on a barycentric grid in eighths and never under-read the measured error
in these runs; Phase 5 subdivides on it. A sliver whose quadratic needs
more curvature than S.10.16 holds gets the tangent plane at its centroid
(12 of 6,000 triangles here).

## Hypotheses the evidence killed

- S/T in 16.16 texels: the first `/tex` run (TEX.TXT) sampled texel 0 or 1
  everywhere.
- The unit following the width field: T11 (16 wide, size 5) sampled at the
  32 unit.
- u wrapping at the declared width: T11 ran to 31.
- The closed-form quadratic as the engine's arithmetic: right to within a
  texel boundary, wrong on 3 of 9,753 card pixels.
- A ones' complement of one LSB: T5b put the loss at 32 units.

## Not measured

- Bilinear filtering, ARGB1555/4444, texture modes, blending, fog: Phase 4
  steps 4-8.
- T's resolution: assumed equal to S's (same field widths); T7 and the
  triangle runs exercise T, but `/texprec` set only S.
- A second boot for these scenes.
