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
| `D3DMipmapLevelRaw` | **0** (black) | 31 (blue) * | blue |
| `D3DTrilinearRaw` | 24856 | 495 | green/blue mix |

\* Both mip rows were measured with the probe binding the wrong texture - see
the second-round section below - and neither says what it appeared to.

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

## Second round, 2026-09-03: the mip rung was mis-bound, and two registers were unwritten

The probe's mip and trilinear rungs bound the **plain** texture, not the
two-level one, so neither of the mip rows in the table above ever measured mip
selection on either target. Fixed in the probe. Separately, the driver never
wrote the mip-level gradients `TdDdX`/`TdDdY` (0xB518/0xB524), so the level
index drifted by whatever the registers last held; both are now written as
zero. With both corrections the two-level texture's level-1 fetch reads
**black on the emulator and on the card alike**, and the driver's own mip-chain
counter says the mipmapped path was never entered for that texture. That
contradiction is the open question, and it is answerable on the emulator first.
Record: [`../decisions/2026-09-03-two-hypotheses-on-the-trio3d-and-what-they-left.md`](../decisions/2026-09-03-two-hypotheses-on-the-trio3d-and-what-they-left.md).

## Next

1. ~~Diff 86Box's S3D alpha-blend and texture-fetch code by chip id.~~ Done:
   there is no chip-conditional code in the S3D unit at all. The emulator
   cannot answer this; only the card can.
2. Re-run the alpha rung on A8U4I5 several boots in a row and record the raw
   each time; a value that wanders is a read of something uninitialised.
3. Until one of those lands, treat `D3DVertexAlphaBlendOk`,
   `D3DMipmapLevelSelectOk` and `D3DTrilinearBlendOk` as open on the Trio3D,
   not as regressions.

## Third round, 2026-09-04: mip is not a difference; alpha is, across the whole space

The 117-cell matrix ran on A8U4I5 for the first time, with the pair at 6797d93
(`../decisions/2026-09-04-the-trio3d-runs-the-matrix.md`,
`../probe/references/trio3d-a8u4i5-v9x-2026-09-04.ini`).

**Mip selection is no longer a Trio3D-versus-ViRGE/DX difference.**
`D3DMipmapLevelRaw` reads 992 on the card and 992 on the emulator,
`D3DTrilinearRaw` 992 on both, and every `mipnear` and `trilin` cell in the
matrix passes on the card at 64, 128 and 256 texels, in both formats, plain and
chained and gapped. The rows in the table above were taken with the wrong
texture stride on every mipmapped draw. `D3DMipmapLevelSelectOk=0` and
`D3DTrilinearBlendOk=0` stay open on both machines, as one question rather than
two.

**Alpha is the only difference the probe can still see, and it is every blended
cell.** All 27 fail; all 90 unblended cells pass. In each `alpha` cell the
opaque half reads 0 where the emulator reads 992, and the alpha-0 half
correctly reads 0. The `halfa` cell's right half reads 23254 (`0x5AD6`,
r22 g22 b22) against the emulator's 16 (blue at half), so blended fragments are
not simply discarded. The driver refused no texture, skipped no blend and
suffered no engine fault during the run (`TexMatrixCountsOk=1`, no `_Dref`,
`_Dskip` or `_Dfault` key written).

Also measured: the card never sets SUBSYS_STAT bit 1. `_Dmiss` is written for
every cell, so every 3D-done wait degraded to the idle bit.

Next item 2 is still open: `D3DVertexAlphaBlendRaw` read 25352 today against
527 in the committed 2026-09-03 capture on the same 800x600x16 desktop, but the
driver changed between them, so it is not the controlled repeat that item asks
for.

## Fourth round, 2026-09-04: the alpha readings were a warm-boot artefact

Retracted, with the A/B that did it, in
[`../decisions/2026-09-04-the-trilinear-two-pass-and-a-retraction.md`](../decisions/2026-09-04-the-trilinear-two-pass-and-a-retraction.md).

The Trio3D/2X performs the S3D alpha blend, and performs it exactly as the
emulated ViRGE/DX does. On a cold-booted machine `AlphaCurveOk`,
`AlphaCurveBOk`, `AlphaCurveCOk`, `Alpha1555Ok` and `Alpha4444Ok` are all 1,
the transfer curve is a clean interpolation between the measured ends, the
forced-encoding sweep reads as it does on the emulator, and all eighteen
`alpha` cells of the matrix pass - `TexMatrixOk` 90 of 117 to 108 of 117.

Every alpha measurement in the three rounds above, and in today's two retracted
decisions, was taken after a **warm restart**. A power cycle clears whatever
state makes the blend wrong. That also disposes of the wandering
`D3DVertexAlphaBlendRaw`: 527 is the cold-boot value and 25352 the warm one,
and four warm boots in a row gave 25352 every time, which is what made it look
settled.

What is left on this part, by the probe: the nine `halfa` cells - ARGB4444 at
alpha 8 of 15, all sizes, all layouts - whose right half reads 15871
(`0x3DFF`) where a half-blended blue is wanted. And the open question is now a
better one than the old ones: **what does this card carry across a warm restart
that breaks its blend, and what clears it?**
