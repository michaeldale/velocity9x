# What the Trio3D's blend does with its operands: nothing with the source

Date: 2026-09-04
Status: measured on A8U4I5 and on the emulated ViRGE/DX; the alpha defect is
one mechanism, and it is not where A comes from

> **RETRACTED 2026-09-04.** The Trio3D/2X does perform the S3D alpha
> blend. Every reading below was taken on a machine that had been warm
> restarted rather than power cycled, and its blend is wrong only in that
> state; a cold boot makes all of it correct. See
> `2026-09-04-the-trilinear-two-pass-and-a-retraction.md`. What the
> readings still document is the shape of the failure in that state, and
> the instruments built to take them.


## Why a curve

The matrix said every blended cell fails on this card and every unblended one
passes (`2026-09-04-the-trio3d-runs-the-matrix.md`), but its three blended
readings told three different stories: an opaque texel drew nothing, an alpha-0
texel correctly drew nothing, and a half-alpha texel drew a neutral grey out of
a texel with colour in one channel. Three points on a curve nobody had drawn.

`AlphaCurve` draws it. The destination is filled a known primary - not black,
so the destination's own contribution is visible - and one uniform ARGB4444
texture is drawn over it nine times with the texel's alpha stepped 0, 2, 4 ...
14, 15. Both ends are measured rather than assumed: `AlphaCurveDstRaw` is the
fill read back with nothing over it, `AlphaCurveSrcRaw` the same texel drawn
with the blend off. The walk is taken three times over rotated operand pairs -
blue over red, red over green, green over blue - and once more with vertex
alpha on an untextured triangle, which is the engine's other alpha path
(`ALPHA_SOURCE|ALPHA_ENABLE` where the textured one sets `ALPHA_ENABLE` alone).

## The control

Emulated ViRGE/DX, same probe binary, same driver
(`docs/probe/references/virge-dx-86box-v9x-2026-09-04.ini`): all three pairs and
the vertex walk interpolate linearly from the measured destination to the
measured source, monotonically, `AlphaCurveOk=AlphaCurveBOk=AlphaCurveCOk=
VtxAlphaCurveOk=1`. Blue over red reads 31744, 27652, 23560, 19468, 14352,
10261, 6169, 2077, 31. That is the blend equation, to the rounding.

## The card

`docs/probe/references/trio3d-a8u4i5-v9x-2026-09-04b.ini`, decoded to 5-bit
channels:

```
                        A=0    2     4     6     8    10    12    14    15
A  dst red,  src blue    ---  3,31,31  6,29,29  7,26,26  8,22,22  7,17,17  5,11,11  2,4,4  ---
B  dst green,src red     ---  31,3,31  29,6,29  26,7,26  22,8,22  17,7,17  11,5,11  4,2,4  ---
C  dst blue, src green   ---  31,31,3  29,29,6  26,26,7  22,22,8  17,17,7  11,11,5  4,4,2  ---
```

A=0 and A=15 read black in all three. Every other step is the *same pair of
numbers* in all three pairs:

- the destination's own channel: 3, 6, 7, 8, 7, 5, 2 - a hump;
- the other two channels: 31, 29, 26, 22, 17, 11, 4 - falling from full.

Only the *position* of the odd channel changes between pairs, and it follows the
destination. Pair A's source is blue, pair B's is red, pair C's is green, and
the three outputs are the same triples.

## What that says

**The source's colour has no effect on a blended draw.** Rotate it through
three primaries and the output does not move. This is the finding: the
question is no longer how the part weights the two operands, because one of
them is not arriving.

**The destination's colour has no effect on the magnitudes either** - it
selects which channel is the odd one out, and nothing more. So the output is a
function of A alone, positioned by the destination.

**Both alpha paths give byte-identical results.** `VtxAlphaCurve_*` on an
untextured triangle reads 0, 4095, 7101, 8026, 8918, 7729, 5483, 2180, 0 - the
same nine values as `AlphaCurve_*` with texel alpha. `ALPHA_SOURCE` set and
clear make no difference to the outcome, so this is one mechanism, not two.

**Neither endpoint is right.** A=0 should leave the destination standing and
reads black; A=15 should be the source and reads black. A blend that got A
backwards would still hit one of the two ends.

## Hypotheses this kills

- *The textured path takes A from the wrong place.* The two paths agree
  exactly. `docs/issues/2026-09-03-trio3d-alpha-and-mip-differ-from-virge-dx.md`
  framed the defect around where A is sourced; that framing is wrong.
- *A is simply read as 0, or as 1.* The output varies over nine steps of A and
  is zero at both ends.
- *The destination read is incoherent with the fill.* `AlphaCurveDstRaw`,
  `AlphaCurveBDstRaw` and `AlphaCurveCDstRaw` read 31744, 992 and 31 - the
  fills, exactly - and the destination does reach the blender well enough to
  place the odd channel.
- *The texture is not sampled under a blend.* `AlphaCurveSrcRaw` reads the
  texel correctly with the blend off in all three pairs, and all 90 unblended
  matrix cells pass. The sampler is fine; the blender does not use what it
  produces.
- *`D3DVertexAlphaBlendRaw` wanders between boots.* Four boots of A8U4I5 with
  one driver (22, 23, 24, 25) produced files with **zero** differing keys under
  `compare-probe.ps1`, 25352 every time. The two values on record, 527 and
  25352, are two driver builds, not two reads of uninitialised memory. The
  second Next item of that issue is answered and closed.

## Where the work is now

In `src/display32/d3d/d3d_virge.c`, on the register and command-word setup for
a blended draw on `5333:8A13` - what the source of the blend is taken from - not
in `v9x_d3d_virge_alpha_bits`, whose choice between `ALPHA_ENABLE` and
`ALPHA_SOURCE|ALPHA_ENABLE` demonstrably does not change the outcome. The
emulator cannot help: 86Box's S3D unit has no chip-conditional code at all
(recorded in that issue's Next 1), so it models one blend for both parts and
gets it right. Only the card can say which register it reads a blend source
from.

## Gates

check-tree, host tests and family packages (run-checks). The change is to the
probe only; no driver code moved.

## Open

- What the two sequences are. The odd channel's hump and the other two's fall
  are reproducible and identical across all three operand pairs, so they encode
  something - but nothing measured so far says what.
- The `halfa` grey in the matrix is the same effect seen at one point; it needs
  no separate explanation.
