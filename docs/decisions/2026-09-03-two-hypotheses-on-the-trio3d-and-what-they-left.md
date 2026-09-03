# Two hypotheses on the Trio3D's alpha and mip, and what testing them left behind

Date: 2026-09-03
Status: both hypotheses dead on silicon; two hardening changes kept; one emulator
finding reversed

## The question

`docs/issues/2026-09-03-trio3d-alpha-and-mip-differ-from-virge-dx.md`: on a
5:5:5 desktop the physical Trio3D/2X passes every S3D rung except vertex alpha
(a raw that wandered between boots and was never a blend of red over blue) and
mip selection (black where the emulated ViRGE/DX fetches blue). 86Box models
the S3D unit identically for the two chips - every Trio3D branch in its source
is RAMDAC, aperture size or PCI config - so the emulator cannot explain the
difference and any answer has to be tested on the card.

Two hypotheses came out of reading how the engine programs the two units,
each with a change that is right on its own terms whether or not it fixed the
symptom.

## Hypothesis 1: the blend reads its destination through the source stride

`DEST_SRC_STRIDE` (0xB4E4) is destination stride in the high word and source
stride in the low, and `d3d_virge.c` wrote `pitch << 16` - the low half zero.
86Box's 3D blend reads the destination back through `dest_str` and never looks
at the low half, so the emulator forgave it; real silicon fetching the pixel it
blends over through a source stride of zero would read the wrong row every
time, and a boot-varying garbage colour is what that looks like.

**Change kept:** the register is now written with both halves equal. The 3D
engine has one surface to read back from and it is the one it writes to.

**Result on A8U4I5:** `D3DVertexAlphaBlendRaw` 25352 before, 25352 after.
Dead. The Trio3D's alpha result does not depend on that field.

## Hypothesis 2: the mip chain is not where TEX_BASE arithmetic expects it

The S3D unit takes one `TEX_BASE` and derives every level from it, largest
first, each level following the last (the emulator's own loop, and the only
reading of a single base register). DirectDraw creates each level as its own
surface and promises nothing about placement. The engine assumed contiguity on
the strength of the emulated ViRGE passing the mip rung, which it does because
its allocator happened to put level 1 after level 0.

**Change kept:** `v9x_d3d_mip_chain_contiguous` walks the attachment list at
draw time and checks each level sits at the previous level's end with half its
edge. A chain with a gap draws from level 0 with mip selection off - wrong,
visibly, from the right texels - rather than fetching past the top level into
whatever is there. Two counters, `D3dMipChainChecks` and `D3dMipChainGaps`,
publish through `V9XTRACE`. The attachment node is `DBLNODE` from `DDRAWI.H`,
mirrored as an ABI shape and recorded in `docs/ddk-inputs.md`.

**Result on A8U4I5:** `D3dMipChainChecks=2 D3dMipChainGaps=0`,
`D3DMipmapLevelRaw=0`. Dead. The chain **is** contiguous on the card and the
level fetch still returns nothing. Whatever the Trio3D does with a mipmapped
texture, it is not a layout problem.

## What the emulator said, and a reversal

Regression on the 86Box ViRGE/DX, same build: every `*Ok` key identical to the
previous run except one - `D3DZWriteMaskOk` went **0 to 1**, and stayed 1 on a
second probe run in the same boot (`D3DZMaskRaw` 992 to 31744). The only
register change is the stride word's low half.

That contradicts `docs/issues/2026-09-03-86box-virge-ignores-depth-write-disable.md`,
filed this morning, which said the emulator ignores `Z_UP_EN`. It does not;
the rung failed for a reason connected to the half-written stride word and now
passes. How the low half reaches the Z path in the model is **not**
established - 86Box's triangle code derives the Z address from `z_str`, not
`src_str` - and the issue is amended to say exactly that rather than to invent
a mechanism. What it did establish stands: the silicon passed that rung both
ways.

## Where this leaves the Trio3D

Two fewer hypotheses, two counters that will say so if either ever recurs, and
the same two symptoms. The engine's programming of alpha and mip is now known
to be right on three counts - the values (they render on the emulator), the
layout (measured contiguous), and the strides (both halves) - and wrong on
silicon for a reason none of those reach. The next moves are differential
probes on the card rather than more reading:

- Alpha: draw the vertex-alpha rung with `ABC_SRC` clear (texture-alpha path)
  and with alpha at the extremes, 0 and 255, and read what comes back. Two
  extremes that do not give blue and red respectively say the blend is reading
  a different alpha; two that do say the mid-range arithmetic differs.
- Mip: force the level through the `D` register at 0 and at 1 with
  `MIP_NEAREST`, on a two-level texture whose levels are different colours. If
  level 0 renders and level 1 is black on both, the fetch for any level above 0
  is what fails; if the forced level renders either colour, the derivative
  arithmetic that picks the level is what differs.

Both are probe switches, not driver changes, and both need the card.

## Second round, same day: the instrument was wrong, and two registers were never written

**The mip rung never tested mip selection.** `ddraw_probe_win32.c` creates two
textures - a plain 64x64 (`texture_handle`) and a 64x64 with a 32x32 level
attached (`texture_handle2`, level 0 green, level 1 blue). The base-texture rung
binds `texture_handle2`; the mip rung and the trilinear rung after it bound
**`texture_handle`**, the plain one, and then asked for `MIPNEAREST` and
expected blue. The plain texture has no blue. So `D3DMipmapLevelSelectOk=1` on
the emulator was the engine reading a level that texture does not have from
memory that happened to hold blue, and `=0` on the Trio3D was the same read
landing on zeros. Neither was about mip selection. Fixed: both rungs now bind
`texture_handle2`. This is the third instrument defect of the `FlipPixelOk`
kind this week, and the second found by the silicon disagreeing with the model.

**Two S3D registers were never written.** The mip level `D` is interpolated
across the triangle like `U` and `V` are, from `DS` and two gradients -
`TdDdX` at 0xB518 and `TdDdY` at 0xB524 (86Box's register decode at
`build\reference-vid_s3_virge.c` around 1830-1839, and its per-pixel `state->d`
arithmetic). The driver wrote `DS` and neither gradient. The level index
therefore drifted across every textured triangle by whatever those registers
last held: zero after power-on, another driver's leftovers after a game,
different on every boot. The engine picks one level per triangle, so both are
now written as zero, and the texture setup reserves eleven FIFO slots instead of
nine.

**Measured, corrected probe, both registers written:**

| | 86Box ViRGE/DX | A8U4I5 Trio3D/2X, boot 16 |
|---|---|---|
| `D3DBaseTextureRaw` (level 0, handle2) | 992 green | 992 green |
| `D3DMipmapLevelRaw` (expect blue) | **0** | **0** |
| `D3DTrilinearRaw` | 960 | 992 |
| `D3DVertexAlphaBlendRaw` | 16399 (correct) | 25352 (unchanged) |
| `D3dMipChainChecks` | **0** | **0** |

Two things in that table do not fit together, and they are the open question:

1. With the two-level texture bound, the mip rung reads **black on both
   targets**, where a non-mipmapped fetch of that texture would be green and a
   correct level-1 fetch blue. Black is a fetch that found nothing - or a draw
   that did not happen.
2. `D3dMipChainChecks` is **zero on both**, which says `v9x_d3d_texture_info`
   never saw `DDSCAPS_MIPMAP` on the surface `texture_handle2` resolves to -
   yet the base rung textures from that same handle. A controlled A/B on the
   emulator (the counter patch stashed, the same probe) also read zero, so the
   counters are not what changed. The earlier readings of 2 and 4 came from
   the old, mis-bound probe, and are unexplained by the same token.

The 86Box model is deterministic and its texel address arithmetic is on the
page, so this is answerable there before the card is involved again: a trace of
one mip draw's register writes and the level the model computes from them.
That is a probe-plus-trace exercise for a fresh session, not more inference
from this one.

**The emulator's Z-write-mask rung is not stable.** It read 0 this morning,
1 twice after the stride change, and 0 again on this run with nothing in
the Z path touched. The reversal recorded above - that the stride word
explained the pass - was overreach; the rung varies between boots on 86Box
and the silicon passes it every time. The issue is amended again to say
only that.

**Alpha is unchanged** on the Trio3D by any of today's changes, and correct on
the emulator throughout. The differential rungs proposed above remain the next
move for it.
