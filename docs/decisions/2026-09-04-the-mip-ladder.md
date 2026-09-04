# The mip ladder: level selection is right, and trilinear is the alpha defect

Date: 2026-09-04
Status: measured on A8U4I5 and on the emulated ViRGE/DX; the mip question is
closed, and what replaces it is a driver change nobody has made yet

## The rung the census asked for

The command-word census showed that 3,775 of 3DMark 99's 58,619 draws use a mip
filter, that they are the only mip usage in the whole run, and that the race
scene's noise ground has to be among them
(`2026-09-04-the-command-word-census.md`). It also showed that the probe did
not really cover them: the matrix loosens its rule for a chained trilinear cell
to "something was drawn on both halves", because a real trilinear result is
neither level's hue - so a sampler reading a wrong but non-black level passes.

`MipLadder` asks the question directly. A four-level chain, 128 texels down to
16, each level filled a different colour - red, green, blue, magenta. Four hues
the classifier separates, and three adjacent pairs each of which leaves one
channel absent, which is what makes a blend of two levels checkable rather than
merely non-black. The triangle's u axis spans 47.5 pixels, so the
texel-per-pixel ratio is the tu span alone; the four ladder steps sit just
above 1, 2, 4 and 8, a tenth of a level clear of each boundary so floor and
nearest agree and the reading does not depend on which rounding rule a part
uses. Three more steps sit half a level between them, under
LINEARMIPLINEAR.

`MipLadderDelta1..3` record the chain's actual shape first, because a reading
is worth nothing if the chain is not what was asked for. **That needs a Lock:**
a video-memory surface's `GetSurfaceDesc` carries `lpSurface = 0`, which is
what `TexMipTopAddress` and `TexMipLevelAddress` have been reporting as zero
all along. The first version of this rung used the desc and gated itself off
on four zero addresses.

## Level selection is correct - on both machines

Chain contiguous on both, deltas 32768, 8192, 2048 exactly as asked.

```
                      emulated ViRGE/DX        Trio3D/2X
MipLadder_0_Raw       31744  (31, 0, 0) red    31744  (31, 0, 0)
MipLadder_1_Raw         992  ( 0,31, 0) green    992  ( 0,31, 0)
MipLadder_2_Raw          31  ( 0, 0,31) blue      31  ( 0, 0,31)
MipLadder_3_Raw       31775  (31, 0,31) mag     31775  (31, 0,31)
MipLadderOk               1                        1
```

Four levels, four ratios, exactly the right level every time on the physical
card. Mip level selection on the Trio3D/2X is not a defect and never was: the
part descends a contiguous chain correctly.

**And the old rungs are wrong, not the driver.** In the same emulator run that
scores `MipLadderOk=1`, `D3DMipmapLevelSelectOk` is still 0 with
`D3DMipmapLevelRaw=992` - level 0 where level 1 was expected - and
`D3DTrilinearRaw` is 992, pure level 0. Those two rungs bind a two-level
64-texel texture created early in the run; the ladder's chain is contiguous and
theirs, on this evidence, is not, so the driver correctly falls back to a plain
LINEAR filter and the rung reads level 0 and calls it a failure. That is
untested - the older rungs record no delta - but it is the only reading
consistent with the ladder passing on the same machine in the same run. Those
two keys should not be read as defects on either machine until they carry a
delta of their own.

## Trilinear is the alpha defect wearing a different hat

```
                      emulated ViRGE/DX        Trio3D/2X
MipTri_0_Raw (L0/L1)  (14, 17,  0)             ( 7, 22, 22)
MipTri_1_Raw (L1/L2)  ( 0, 14, 17)             (22,  7, 22)
MipTri_2_Raw (L2/L3)  (17,  0, 31)             (22, 22,  7)
MipTriOk                    1                        0
```

The emulator blends the two levels and leaves the third channel alone, three
times. The card produces the same three numbers every time - one channel at 7
of 31 and the other two at 22 - with **the low channel rotating to follow the
lower level's own colour**: red low between red and green, green low between
green and blue, blue low between blue and magenta.

That is the alpha defect's signature. The curve found the same shape: a
destination whose own channel came out the odd one while the other two rose,
with the operands' values ignored
(`2026-09-04-what-the-trio3d-blend-does-with-its-operands.md`).

And the mechanism is in our own code. `d3d_virge.c` implements
LINEARMIPLINEAR as **two passes with an alpha blend between them**: the first
draws level N, the second re-draws the same triangle at level N+1 with
`ALPHA_SOURCE | ALPHA_ENABLE` - encoding 11 - and the LOD fraction as the
vertex alpha. Encoding 11 is the one the forced sweep measured on this card and
found to draw nothing at all
(`2026-09-04-no-encoding-of-the-alpha-field-blends-on-the-trio3d.md`). Trilinear
here is not a texture-unit question at all: it is synthesised out of the one
operation this part cannot perform.

That the exact arithmetic of the second pass is not reproduced by the forced
sweep's numbers is not settled and is not claimed. What is measured is that
both failures rotate a channel with their operands and neither reaches the
value the blend equation puts there.

## What follows, and is not done here

The S3D command word has a native `LINEAR_MIP_NEAREST` encoding, which needs no
second pass and no blend, and the census shows 3DMark already uses it for 1,022
of its 3,775 mip draws. On a part that cannot blend, LINEARMIPLINEAR should
degrade to that rather than to a two-pass blend that produces the rotation
above. That would very likely also be the noise ground: 2,753 of those 3,775
draws - the LINMIPLIN ones - are the population the census pointed at.

It is not changed here. It is a chip-conditional degrade, which is the same
descriptor-splitting decision the alpha finding already raised, and one run
should not settle two of those at once. The rung that would confirm it exists
now, so the change can be measured rather than argued.

## Gates

check-tree, vga survey safety gate, host tests and family packages
(run-checks). The rung's own control is the emulated ViRGE/DX, where all seven
readings are exactly right and `MipLadderOk` and `MipTriOk` are both 1.
