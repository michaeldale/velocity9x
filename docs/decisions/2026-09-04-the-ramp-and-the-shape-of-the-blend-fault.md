# The ramp: a sprite as an application draws one, and the fault's exact shape

Date: 2026-09-04
Status: measured on A8U4I5 against the emulated ViRGE/DX. The rule is now
precise. It is still not the black box.

## What the ramp adds

Every alpha rung before it holds alpha constant over a draw and varies it
between draws. A sprite does the opposite: one quad, one draw, alpha varying
across it, interpolated by the sampler - and it lands on a destination that is
itself textured, not a flat fill. Neither had been measured.

`Ramp_*` paints the target with an opaque green texture, then draws a blue
ARGB4444 texture over it whose alpha runs 0 to 15 across u, and reads seven
points along the result. Correct is green fading to blue with no red anywhere,
since neither operand has any.

## Measured

Emulated ViRGE/DX, `RampOk=1`:

```
  x=12  (0, 29,  2)      x=30  (0, 19, 12)      x=48  (0,  6, 25)
  x=18  (0, 25,  6)      x=36  (0, 14, 16)
  x=24  (0, 21, 10)      x=42  (0, 10, 21)
```

A clean fade. A8U4I5, `RampOk=0`, the wall itself correct at `(0, 31, 0)`:

```
   x     card          correct
  12   (29, 29, 31)   (0, 29,  2)
  18   (25, 25, 31)   (0, 25,  6)
  24   (21, 21, 31)   (0, 21, 10)
  30   (19, 19, 31)   (0, 17, 14)
  36   (15, 14, 31)   (0, 13, 18)
  42   (10, 10, 31)   (0,  9, 22)
  48   ( 6,  6, 31)   (0,  5, 26)
```

## The rule, now stated exactly

The green column is right at every sample - the **destination's term is exact**,
`dst * (1 - A)`, with alpha interpolated across a triangle and quantised by a
64-wide texture, and it still lands. Two things are wrong with it, and this
rung pins both:

1. **The source's channel saturates.** Blue is 31 at every sample instead of
   `src * A`.
2. **The destination's value is duplicated into the channel neither operand
   uses.** Here that is red, and red equals green at every sample.

The second point is what the ramp settles. In `AlphaCurve` the destination was
red and the source blue, and the duplicate appeared in *green*; here the
destination is green and the source blue, and it appears in *red*. It is not a
fixed channel - it is whichever channel is neither operand's. `AlphaCurveBOk`
and `COk` fail on their own rotations, so all three pairings agree.

So the fault is: `out[dst] = dst*(1-A)`, `out[src] = full`,
`out[third] = dst*(1-A)`. Three statements, consistent across four operand
pairings and two rungs, on a boot where the endpoints A=0 and A=15 are exactly
right.

## It is still not the black box

Extrapolate the ramp to alpha 0, which is what a sprite's transparent border
is: the destination term goes to full in both its own channel and the third,
and the source's channel is saturated, so a fully transparent pixel over this
green wall draws **white**. Over a dark wall it draws saturated blue. Neither
is black.

3DMark 99's sprites sit in *black* rectangles, and every blended draw in that
run is now `LIT MODULATE`
(`2026-09-04-an-unlit-blend-loses-its-texture-on-the-trio3d.md`), which is the
pairing this rung exercises. So this defect and those rectangles are still two
different things, and that is now the second rung in a row to say so.

What is left unexercised, and what the black boxes must therefore be made of:
the sprite quads in that benchmark are drawn against a destination this probe
has never used - the flipping chain in exclusive mode, not an offscreen render
target - and at 640x480 with the depth buffer live across a whole frame rather
than one triangle. The probe has no rung on the primary chain at all; the
render-target block deliberately stops at offscreen surfaces
(`docs/probe/README.md`, "Render targets of real sizes"). That is the gap.

## Open

- Why the source's channel saturates. It is not a missing multiply: that would
  leave the destination absent, and the destination is exact.
- Why the destination's value reaches the third channel. Together the two read
  like a channel-mask or pixel-format disagreement inside the blend unit - a
  family this chip has produced twice
  (`2026-09-02-a-555-desktop-needs-three-places-to-agree.md`,
  `2026-09-02-s3d-writes-1555-because-it-can-only-write-1555.md`) - but that is
  a direction, not a finding.
- The black boxes, which now want a rung that blends onto the primary flipping
  chain in exclusive mode.

## Gates

check-tree, vga survey safety gate, host tests and family packages
(run-checks). The change is to the probe only; no driver code moved.
