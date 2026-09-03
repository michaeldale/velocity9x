# Trio3D/2X on the ViRGE path: alpha blend and mip selection do not match the ViRGE/DX

Filed: 2026-09-03
Status: open, measured on silicon, not attempted
Component: `src\display32\d3d\d3d_virge.c` as driven on `5333:8A13`

## Context

The Trio3D/2X is bound to the ViRGE/DX's hooks
(`docs\decisions\2026-09-02-trio3d-on-the-s3-path.md`), on 86Box's evidence
that its S3D engine carries no branch for the part, with two modelled
differences recorded as unaccounted for. Until today every colour key on the
part failed for one reason - the 5:6:5/1555 target mismatch - and nothing else
could be seen behind it. With a 5:5:5 desktop that reason is gone and eight
keys pass. Two do not, and they are not the emulator's.

## Measured

A8U4I5, physical Trio3D/2X, `ColourLayout=555-auto`, hardware Direct3D, boot 13.
Raw values are XRGB1555. The 86Box ViRGE/DX column is the same probe, same
build, same layout, boot 530.

| Key | Trio3D/2X (silicon) | ViRGE/DX (86Box) | Expected |
|---|---|---|---|
| `D3DVertexAlphaBlendRaw` | **25352** (`0x6308`: r24 g24 b8) | 16399 (`0x400F`: r16 g0 b15) | ~r16 g0 b15 |
| same key, 2026-09-02, 5:6:5 desktop | **527** (`0x020F`) | - | - |
| `D3DMipmapLevelRaw` | **0** (black) | 31 (blue) | blue |
| `D3DTrilinearRaw` | 24856 | 495 | green/blue mix |

The alpha value is the telling one: it changed between two boots of the same
part with the same command word, and neither value is a blend of red over blue.
Grey-with-red at r24 g24 b8 is not reachable from those two colours by any
source/inverse-source weighting. Either the part is blending against something
other than the destination - uninitialised memory, a stale texel, its own
previous fragment - or the alpha-blend control bits do not mean on this part
what they mean on the ViRGE/DX. The mip raw of 0 says the texture unit fetched
nothing at all for a level-selected draw, where the emulator's ViRGE fetched
the right texel.

Everything else on the part agrees with the ViRGE/DX model to the pixel:
triangle, subpixel, specular, both depth ladders, base texture, 4444 texture,
fog. So this is not "the Trio3D is not a ViRGE"; it is two specific units.

## Why nothing is changed yet

The engine already publishes `D3DPBLENDCAPS_SRCALPHA` and the four mip filter
caps for this part, inherited from the ViRGE/DX descriptor. On the measurements
above both are advertised and neither is delivered - the pattern this driver
has paid for twice. But the fix is not to pull the caps blind: the right first
move is to read what the 86Box model does for `S3_TRIO3D2X` in its alpha and
texture-fetch paths against what it does for the ViRGE/DX, since the emulator
is what said the two were the same, and then either find the register
difference or split the descriptor. The two differences already recorded -
FIFO depth and the aperture decode mask - are the first suspects for the mip
fetch.

## Tested, 2026-09-03: two hypotheses, both dead

Recorded in full in
[`../decisions/2026-09-03-two-hypotheses-on-the-trio3d-and-what-they-left.md`](../decisions/2026-09-03-two-hypotheses-on-the-trio3d-and-what-they-left.md).

- **Source stride.** `DEST_SRC_STRIDE` was written with a zero low half.
  Written with both halves: `D3DVertexAlphaBlendRaw` 25352 before and after.
  Not the cause. The change is kept because the register has two halves.
- **Mip chain layout.** The engine assumed the levels were contiguous after
  `TEX_BASE`. A draw-time walk of the attachment list now checks: on A8U4I5,
  `D3dMipChainChecks=2 D3dMipChainGaps=0` and `D3DMipmapLevelRaw=0` still.
  The chain is where the engine expects it and the fetch still returns nothing.
  Not the cause. The guard is kept because a gap would be an out-of-surface
  read.

## Next

1. ~~Diff 86Box's S3D alpha-blend and texture-fetch code by chip id.~~ Done:
   there is no chip-conditional code in the S3D unit at all. The emulator
   cannot answer this; only the card can.
2. Re-run the alpha rung on A8U4I5 several boots in a row and record the raw
   each time; a value that wanders is a read of something uninitialised.
3. Until one of those lands, treat `D3DVertexAlphaBlendOk`,
   `D3DMipmapLevelSelectOk` and `D3DTrilinearBlendOk` as open on the Trio3D,
   not as regressions.
