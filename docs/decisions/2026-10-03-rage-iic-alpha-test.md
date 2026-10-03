# Rage IIC: the alpha test on ARGB4444 and RGB565

Date: 2026-10-03. Machine: A8U4I5 (Rage IIC AGP `1002:4757`, P3 1002 MHz,
Win98SE). Evidence: `docs/probe/a8u4i5-rage-iic-registers-2026-10-02/`
`phase8-amask/ATIRX-AMASK.TXT`, `hl-d3d-mwd5-amask/`,
`hl-d3d-mwd5-alphatest/`.

Half-Life refused 10,371 alpha-tested batches a run (`M64Policy14`): the
chip has no alpha compare, only `TEX_AMASK_AEN`, which drops a texel whose
alpha LSB is 0 (F5, F6), and the policy served it on ARGB1555 alone.

## What the mask reads under bilinear filtering

`ATIRX /amask`: a 32x32 map alternating two texels by column, red even
and green odd, S an eighth of a texel a pixel, so row 16 crosses four
texel boundaries at eight weights. Background `5AA5` is "not drawn".

| Scene | Even / odd alpha | Drawn |
|---|---|---|
| K6, bilinear, no mask | 0 / 15 | every pixel, colour ramping red-green |
| K1, bilinear, mask | 0 / 15 | the half nearer the odd texel |
| K3, bilinear, mask | 14 / 15 | the same as K1 |
| K2, bilinear, mask | 1 / 14 | the half nearer the even texel |
| K4, point, mask | 0 / 15 | the odd texels |
| K5, bilinear, 1555 | 0 / 1 | the same as K1 |

The mask tests the LSB of the texel the nearest sample would take, and
the colour of a drawn pixel is still filtered (K1's drawn pixels are
K6's). K3 kills a threshold on the filtered alpha: alpha 14 is high and
is dropped. Ties at the half-texel go to the higher texel.

## ARGB4444: texels that answer the test

So any comparison can be served on 4444 by texels whose alpha LSB is
the answer for their own alpha (`v9x_r2_alpha_mask_texel`, host-tested):
the policy accepts it with `alpha_mask_key`, and the HAL rewrites every
level the draw reads before the draw, once per upload and test, as the
ViRGE rewrites colour-keyed texels. The per-surface table is cleared on
Unlock, a Blt into the surface, TextureSwap and destroy; a HEL blit is
the same gap as the colour keys'.

What this costs, on paper: the texels are the application's, so their
alpha moves by at most one step of fifteen; a texture both tested and
blended by its alpha blends by that. A texture rewritten for one test and
then tested against another is answered from the moved alphas. The test
is on the nearest texel's alpha, not the filtered alpha Direct3D
specifies - the same approximation 1555 has had.

Half-Life with it (`hl-d3d-mwd5-amask`): 164 surfaces rewritten, none
failed, mwd5 5.209, 5.561, 5.555 fps (5.500 / 5.596 / 5.583 before;
recorded, not compared). Refusals fell only from 10,371 to 10,133: the
4444 batches were few.

## RGB565: the vertex alpha

The new trace keys named the rest: `R2AlphaLastTest` `0x04000006` -
NOTEQUAL 0 on an RGB565 texture under MODULATEALPHA - with 3,247 of the
10,133 unblended. A 565 texel has no alpha, so the tested alpha is the
vertex's (MODULATE, MODULATEALPHA, DECALALPHA, untextured) or 255 (DECAL,
COPY). Where every alpha from the batch's least vertex alpha up passes -
GREATER and NOTEQUAL above the reference, GREATEREQUAL at it - the test
is no test and the draw goes without it (`vertex_alpha_min`,
host-tested). LESS, EQUAL and the rest would need the greatest too and
stay refused.

Except one class. Drawn, the blended textured batches without Z -
Half-Life's additive screen-space sprites - hard-locked A8U4I5 four
times. The bisection, and what ATIRX `/hud` drew without a lock, are in
`docs/issues/2026-10-03-a8u4i5-hard-lock-on-additive-sprites.md`. The
policy keeps refusing them, and also blended mip-mapped batches, which
were never drawn. What it accepts is exactly what the `bisect-zon` build
drew through three clean runs: untextured, unblended, and blended with Z
and one level.

The final build (`hl-d3d-mwd5-alphatest-final`): three runs without a
lock, 5.203, 5.553, 5.547 fps (recorded, not compared). Alpha-test
refusals fell from 10,371 to 5,633 a session, all of them blended; 164
surfaces were rewritten, none failed. The HUD's numbers are still missing:
they are the refused class.

## Open

- The hard lock on the additive sprites (the issue above).
- `dwAlphaCmpCaps` still publishes the 1555 set (GREATER, NOTEQUAL,
  GREATEREQUAL, ALWAYS); Direct3D has no per-format cap.
