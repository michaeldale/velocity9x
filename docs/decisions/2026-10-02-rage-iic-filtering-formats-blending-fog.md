# Rage IIC: bilinear, texel formats, texture modes, blending, fog, scissor

Date: 2026-10-02. Machine: A8U4I5 (10.0.1.172), ATI 3D Rage IIC AGP
`1002:4757` rev `7A`, Win98SE, boot 136, Velocity9x `06f760b` installed.
Harness: `ATIRX.EXE /texbil /texlod /texbil2 /texbil3 /texmix` (runner
from `94a719d` plus this change). Evidence:
`docs/probe/a8u4i5-rage-iic-registers-2026-10-02/BOOT136-ATIRX-TEXBIL*.TXT`,
`-TEXLOD.TXT`, `-TEXMIX.TXT`, `-TEXMIX2.TXT`.

Plan: [ati-rage-iic-hardware-3d.md](../plans/ati-rage-iic-hardware-3d.md),
Phase 4 steps 4-9. Addressing and the S/T walk are in
[texture addressing](2026-10-02-rage-iic-texture-addressing.md).

Every scene draws the 32x8 rectangle at (16, 16) over a 5AA5 background
(which is also the blend destination), RGB565 target. Guards intact, no
pixel outside the scissor, FIFO and idle timeouts and resets 0 throughout.

## Bilinear (step 4)

Maps: a channel checker (red 31 on odd u, green 63 on odd v), a u+v
checker, and a pseudo-random 565 map (LCG seed 0x13579BDF).

- **Sample point** at texel centres: S = 0 blends texels -1 and 0 evenly,
  S = 0.5 is texel 0 alone (B2). Direct3D's convention.
- **Weights** are 3 bits: k = floor(8 frac(S - 1/2)); 1/16-texel steps
  pair up (B2, B5).
- **Arithmetic:** channels zero-extended to 8 bits; one axis at a time it
  is `(a(8-k) + bk + 4) >> 3`, then truncated to 565 (B2, B5: green 8 at
  k = 1, where floor(63/8) is 7). In 2-D, `(sum c w + 32) >> 6` over the
  four corner weights matches 493 of 512 noise pixels (TEXBIL3); the
  other 19 are one LSB in one channel, high and low. No variant tried
  (order, per-stage rounding 0-7, extra precision, expansion, 4-bit
  weights) did better than 17. Unexplained; a reference must allow
  +-1 LSB.
- **Magnification vs minification** is decided per pixel, from the live
  increments: minifying once any of the S or T X or Y increments reaches
  one texel (15/16 magnifies, 16/16 minifies: L1, L2; T's Y increment L7,
  T's X L8, S's Y L10; L9's growing increment switches mid-span exactly
  where it reaches 1). Only the 32 map was tried.
  - Magnifying, BILINEAR_TEX_EN alone decides (B3 = B2).
  - Minifying, TEX_BLEND_FCN alone decides: 2 blends 2x2 (B8 = B10),
    0 is nearest (B9 = B7).
  - TEX_BLEND_FCN 2 without BILINEAR_TEX_EN draws **nothing** when
    magnifying (B4, L1), as the RRG warns. Direct3D's point-mag /
    linear-min pair cannot be programmed as asked.

## Texel formats (step 5)

Each map's 32 columns are 32 test colours.

- ARGB1555 and ARGB4444 reach the 565 target **zero-extended** (F2: G5 31
  becomes G6 62; F3: R4 15 becomes R5 30), and so does 565 itself (F1).
- TEX_MAP_AEN alone changes nothing without blending (F4 = F2).
- TEX_AMASK_AEN: a texel whose alpha LSB is 0 is not drawn, for 1555 (F5)
  and for 4444 (F6: the LSB of the 4-bit alpha, not a threshold). That is
  Direct3D's 1-bit transparency, not a general alpha test.

## Texture modes (step 6)

- The colour interpolators run under SCALE_3D_FCN = 2: modulate by a flat
  white is the identity (M1), by 128 halves (M2).
- **Modulate** (TEX_LIGHT_FCN 1): `t (c + 1) >> 8` per channel, texel zero-
  extended (M2, M3 exact).
- **Alpha decal** (2, with TEX_MAP_AEN): `t(a+1)>>8 + c(256-a)>>8`, the
  4-bit alpha **replicated** to 8 bits (a = A4 x 17), exact (M4). With
  zero-extended alpha 22 of 32 were wrong.

## Blending (step 7)

ALPHA_FOG_EN 1, over the 5AA5 destination (zero-extended, 88/84/40):

| Scene | Source factor | Destination factor | Exact |
|---|---|---|---|
| A1 | As (texel, 4444) | 1-As | 31/32 |
| A2 | 1 | 1 | 32/32, saturating |
| A3 | 0 | Src colour | 31/32 |
| A4 | Dst colour | 0 | 32/32 |
| A5 | As (interpolator 64) | 1-As | 32/32 |
| A6 | 1-Dst colour | 1-Src colour | 28/32 |
| A7 | As (ramp 0..248) | 1-As | 31/32 |

Model: each product `x (f + 1) >> 8`, 1-f as 255-f, the two summed and
saturated at 255, truncated to 565; texel and destination colour
zero-extended, 4444 alpha replicated. 376 of 384 pixels across the blend,
modulate, decal and fog scenes are exact; the 8 others are one LSB.
Replicating colour instead made 53-181 wrong. Destination alpha was not
tried (the target has none).

## Fog (step 8)

- ALPHA_FOG_EN 2 replaces the destination with **DP_FRGD_CLR read as
  ARGB8888**: 0x0000F800 fogged green 0xF8 (G7, factors 0 and 1).
- Fog goes through the blend factors: with both 0 everything is black
  (G1, G2); with As and 1-As it is `src a + fog (1-a)` by the blend
  model, a from the alpha interpolator (G3-G5; G6 with 1 and 0 is the
  texel).
- It shares the unit with blending, so the two are exclusive (RRG).

## Scissor (step 9)

A rectangle from x 2 to 61 across the 8..55 scissor drew 8..55 only and
nothing outside (C1).

## Not measured

- Bilinear on 1555/4444, and its alpha; mip-mapping and trilinear; the
  min/mag threshold on other map sizes.
- COLOR_OVERRIDE, TEX_AMASK_MODE 1, the alpha interpolator's gradient
  under modulate.
- A second boot for these scenes.
