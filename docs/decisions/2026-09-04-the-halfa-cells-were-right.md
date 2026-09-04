# The halfa cells were right: partial alpha is wrong, and the curve rung hid it

Date: 2026-09-04
Status: measured on A8U4I5 against the emulated ViRGE/DX. The instrument is
fixed; the defect it now reports is open.

## Nine cells nobody believed

`TexMatrixOk` has sat at 108 of 117 on the Trio3D/2X all day, and the nine
failures have been the same nine: the `halfa` cells, ARGB4444 at alpha 8 of 15,
at every size and every layout. Their right half reads 15871 - `(15, 15, 31)`
in 5-bit channels - where the emulated ViRGE/DX reads 16, `(0, 0, 16)`: a
half-strength blue over black.

They were left alone because `AlphaCurve` said partial alpha was fine.
`AlphaCurveOk`, `AlphaCurveBOk` and `AlphaCurveCOk` all read 1 on the same
card, on the same boot, in the same file.

Both cannot be true, and the rung was the one that was wrong.

## What AlphaCurveOk actually asserted

Three things: that A = 0 leaves the destination standing, that A = 15 is the
source, and that the source's channel never falls as A rises. **Nothing about
the middle.** A part that jumps from the destination to a saturated source at
the first step and stays there satisfies all three.

That is what this card does:

```
      A   card            emulator        dst*(1-A) + src*A
      0   (31,  0,  0)    (31,  0,  0)    (31,  0,  0)
      2   (27, 27, 31)    (27,  0,  4)    (27,  0,  4)
      4   (23, 23, 31)    (23,  0,  8)    (23,  0,  8)
      6   (19, 19, 31)    (19,  0, 12)    (19,  0, 12)
      8   (14, 15, 31)    (14,  0, 16)    (14,  0, 17)
     10   (10, 10, 31)    (10,  0, 21)    (10,  0, 21)
     12   ( 6,  6, 31)    ( 6,  0, 25)    ( 6,  0, 25)
     14   ( 2,  2, 31)    ( 2,  0, 29)    ( 2,  0, 29)
     15   ( 0,  0, 31)    ( 0,  0, 31)    ( 0,  0, 31)
```

Destination red, source blue. **The destination's term is exact at every
step** - red follows `dst * (1 - A)` to the rounding. The source's term is not:
its own channel is saturated to full at every step instead of scaling with A,
and the value that belongs to the destination is *also* written into green,
which neither operand has.

The two endpoints are right by accident: at A = 0 the source contributes
nothing so its saturation cannot show, and at A = 15 the destination
contributes nothing so its duplication cannot.

The `halfa` cell is the same defect at one point. `(15, 15, 31)` over a black
destination: source blue saturated to 31, and green carrying a value the
destination should have supplied - here half of nothing, which is why it is 15
rather than 0 is not explained, and is part of what is open.

## The instrument, fixed

`AlphaCurveOk` and its two rotations now check **every** step against
`dst * (1 - A)` in the destination's channel, `src * A` in the source's, and
nothing in the third, to a tolerance of two 5-bit steps - 17 of 255, a twelfth
of the range, far too tight for a saturated channel or a duplicated one to hide
in.

Emulated ViRGE/DX under the new rule: `AlphaCurveOk`, `BOk`, `COk`,
`VtxAlphaCurveOk` all still 1, along with `SpriteOk`, `MipLadderOk`, `MipTriOk`
and `TexMatrixOk` at 117 of 117. The tolerance is not too tight.

A8U4I5 under the new rule: all four curve keys 0, `TexMatrixOk` still 108 with
the same nine `halfa` cells. The file now says one thing instead of two
contradictory things.

## What this corrects

`2026-09-04-the-trilinear-two-pass-and-a-retraction.md` says of the cold-booted
card that "the transfer curve is a clean interpolation between the measured
ends". That is wrong, and it was written on the strength of `AlphaCurveOk=1`.
The ends are right and the interpolation is not. The retraction's own finding -
that the part blends at all, and that the total failure before it was a
warm-boot state - stands: every reading in it was of endpoints, and endpoints
are exactly what changed.

It does not change the `Spr_*` result either. That rung tests opaque and
alpha-zero texels, both endpoints, and the substitution it drove was measured
against them.

## Open

- Why the source's channel saturates. It is not a missing multiply - a missing
  multiply by A would leave the source at full and the destination absent, and
  the destination is exact.
- Why the destination's value appears in green. Together those two read like a
  channel-mask or pixel-format disagreement inside the blend unit, which is a
  family of fault this project has met twice before on this chip
  (`2026-09-02-a-555-desktop-needs-three-places-to-agree.md`,
  `2026-09-02-s3d-writes-1555-because-it-can-only-write-1555.md`). That is a
  direction, not a finding.
- Whether this is the black box. 3DMark 99's sprites survived the UNLIT fix
  (`2026-09-04-an-unlit-blend-loses-its-texture-on-the-trio3d.md`), and a
  partial-alpha blend that saturates its source is a good candidate for a
  border that should fade and instead does not. The rung to settle it would
  draw a texture whose alpha ramps across it, over a textured destination,
  which is what a sprite actually is.

## The lesson worth keeping

This is the third instrument today whose `*Ok` was weaker than its raws, after
the matrix's black destination and the sprite rung's missing axes. In all three
the raw values were right there and correct. A `*Ok` key that tests the ends of
a range is a regression check, not a measurement, and it should be written to
say so.

## Gates

check-tree, vga survey safety gate, host tests and family packages
(run-checks). The change is to the probe only; no driver code moved.
