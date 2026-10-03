# Rage IIC mip-mapping: the engine's level rule, and what it bought

Date: 2026-10-03. Machine: A8U4I5 (Rage IIC AGP `1002:4757`, P3 1002 MHz,
Win98SE). Harness: `ATIRX.EXE /mip`. Evidence:
`docs/probe/a8u4i5-rage-iic-registers-2026-10-02/phase7-mip/`
(`ATIRX-MIP.TXT`, `-MIP2.TXT`, `-MIP3.TXT`), `hl-d3d-mwd5-mip*/`,
`phase7-mip-q2/`.

## Method

A 64x64 RGB565 chain to 1x1 and a 64x16 one, each level at a 64-byte
boundary after the last, `TEX_n_OFF` holding level n (n = log2 of the
level's larger edge). Every texel names itself: red 16 + level, green u,
blue v. Each scene draws `ATIRX_RECT` (32x8) with S/T increments set by
hand in base texels (2^20 each), `MIP_MAP_DISABLE` clear, and dumps four
rows. No timeouts or resets in any run.

## Findings

1. **The chain is read.** With `MIP_MAP_DISABLE` clear the engine samples
   level n from `TEX_n_OFF`, addressed at the level's own size: u = S >>
   (26 - n), wrapping at the level's width and height (A, W scenes). A
   64x16 chain's levels are 64x16, 32x8 ... 4x1, 2x1, 1x1.
2. **The level** is the smallest whose texels cover at least a pixel:
   with r top-level texels a pixel, r <= 1 the top level, (1, 2] one
   down, (2, 4] two, ... 64 or more the 1x1 (A050-A6400, K128). The
   threshold sits near 1.125 x 2^k rather than 2^k (E1, E2: 8.5 and 2.125
   read as 8 and 2), as if four bits of the increment are compared.
3. **r is the largest of |dS/dx|, |dT/dx|, |dS/dy|, |dT/dy|** from the
   engine's own increments - the current X increment and the row's Y
   increment - per pixel: S and T count alike (B), the larger of the two
   decides (AB22, AB44), the per-row step counts (C, D), negative
   increments by their magnitude (N1-N3), a leftward span the same (L1).
   So the setup's perspective quadratic gives a per-pixel, perspective-
   correct level for nothing.
4. **TEX_BLEND_FCN** with mip-mapping (raw F, G scenes): 0 the nearest
   texel of the chosen level; 1 the nearest texel of the two levels about
   r, blended (u read halfway between level 5's and level 4's); 2 a 2x2
   blend in the chosen level; 3 a 2x2 blend in the next finer level. There
   is no trilinear.
5. **Magnification** under mip-mapping (H scenes): BILINEAR_TEX_EN decides
   alone. Codes 2 and 3 without it draw nothing where they magnify, as B4
   found for code 2 with mip-mapping off; codes 0 and 1 magnify nearest.
6. An increment of 1024 texels a pixel overflows the 28-bit register and
   reads as zero (K1024: top level); the setup refuses past S.11.16
   already.

## What the driver now does

- `rage2_draw.c`: a mip filter on a chain that reaches 1x1 clears
  `MIP_MAP_DISABLE` and sets every `TEX_n_OFF`; MIPNEAREST is FCN 0,
  LINEARMIPNEAREST 1, MIPLINEAR 2, LINEARMIPLINEAR drawn as MIPLINEAR. A
  shorter chain samples its top level alone (a short chain would leave
  stale `TEX_n_OFF` registers for the engine to read). Host test
  `test_mip`, written first and seen to fail.
- `d3d_rage2.c`: the chain walk (the Mach64's rules at the Rage II's
  alignment), the caps MIPNEAREST, MIPLINEAR and LINEARMIPNEAREST, and
  every level placed in a block of its own (`v9x_d3d_place_each`).
- The OpenGL ICD needs nothing: its textures are DirectDraw chains and its
  mip mode becomes the same filter.

## Placement

The first build placed a chain in one contiguous block, as the Mach64's
does. Half-Life then found no room for 960 of 1,274 texture creations
(`R2SurfaceNoPlace`): Direct3D's texture management fills the 4 MiB, and
DirectDraw's heap placed the declined chains level by level, at pitches
rounded to 64 bytes, which the sampler cannot read below 32 texels (6,656
shape gaps). The Rage II addresses each level separately, so each level
now has a block of its own: chains we place rose from 78 to 204, shape
gaps fell to 0, and 762 creations still find no room.

## What it bought

| Run (640x480) | fps | mip-mapped batches | cycles a pixel |
|---|---|---|---|
| Half-Life mwd5, before | 4.294 | 0 of 210,709 | 229.7 |
| Half-Life mwd5, mip, one block a chain | 4.269 | 9,131 of 306,367 | 235.3 |
| Half-Life mwd5, mip, a block a level | 4.291 | 14,801 of 305,658 | 234.9 |
| Quake 2, before | 3.6 | - | 210.8 |
| Quake 2, mip | 3.5 | 30,660 of 52,940 | 214.8 |

Nothing measurable. Both pictures are right (`hl-d3d-mwd5-mip3`,
`phase7-mip-q2` screenshots); distant surfaces no longer shimmer.

## Hypotheses the evidence killed

- **Mip-mapping explains part of Half-Life's gap to ATI's 6.63 fps**
  (2026-10-03-rage-iic-half-life-d3d-against-ati.md). With 14,801 batches
  mip-mapped the frame rate and the cost a pixel did not move: in mwd5 and
  Quake 2's demo few pixels are minified enough for a level to matter.
- **TEX_BLEND_FCN 3 is trilinear.** It is bilinear in the finer level.
- **A chain needs one contiguous block.** Not on this chip, and asking
  for one is what lost three quarters of Half-Life's chains.

## Open

- Half-Life's 35 % gap to ATI's driver remains unexplained. The engine
  is the limit (emit 74 % of the draw); the frame's ~0.94 M pixels at our
  measured rates cost what we take. Whether ATI's driver draws fewer, or
  draws them in a cheaper mode, needs ATI's driver running.
- 762 texture creations still find no room in 4 MiB.
