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
