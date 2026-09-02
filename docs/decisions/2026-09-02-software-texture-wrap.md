# The software engine tiles textures, by dividing before it multiplies

Date: 2026-09-02
Status: implemented, measured on an 86Box Trio64 in software mode

## The blocker was one line of arithmetic

`docs/decisions/2026-09-01-software-textures-and-caps.md` recorded that the
sampler clamps rather than wraps, and was careful to say why: the edge
interpolator formed `max(from, to) * denominator`, the denominator being the
triangle's height in subpixels and at most 32752, so anything it carried had to
stay under 65566. One repeat. `describe_caps` published `CLAMP` and not `WRAP`,
which was honest, and it made most period content impossible to render
correctly - nearly every 3D game of the era tiles a texture across a wall or a
floor.

The sampler itself always wrapped. It indexes texels through `& mask`, and has
since it was written, where it was a no-op guarding against a coordinate that
had drifted a fraction past the end. Tiling never needed a second code path in
the sampler. It needed a coordinate wide enough to reach one.

## The change

`v9x_d3d_raster_lerp` computed `(from * (d - n) + to * n) / d`. It now computes

```c
whole = delta / d;  part = delta % d;
if (part < 0) { whole -= 1; part += d; }
return from + whole * n + (part * n) / d;
```

`delta * n / d` is exactly `whole * n + part * n / d`, and because `whole * n`
is an integer the floor of the sum is `whole * n` plus the floor of the second
term. Neither part is large: `0 <= part < d` bounds `part * n` by `d * d`, at
most 1,072,693,504, and `n <= d` bounds `whole * n` by `|delta|`. **The largest
intermediate no longer depends on the value being interpolated at all.**

The sign fix-up is what makes it floor rather than truncation. C89 leaves both
`/` and `%` implementation defined for a negative operand; the old form rounded
down because its numerator was never negative, and a quotient truncating toward
zero would round every falling gradient the other way by one level.

It costs a second division per edge crossing. The file already spends two per
scanline recomputing edges rather than stepping them, on the argument that mode
2's trade is "slow, correct".

With that gone the coordinate is bounded by the sampler instead: `u * size`
with `size` at most 512, plus a whole texture of bilinear bias, must stay inside
a signed 32-bit integer. At 33 repeats that is 1,140,850,688. Thirty-three
because the engine clamps a float coordinate to +/- 16 repeats, so a triangle
can span 32, and shifting its smallest corner into the first repeat leaves the
largest just short of the thirty-third.

Around that:

- `V9X_D3D_RASTER_ADDRESS_WRAP` / `_CLAMP` on the texture, numbered as
  `D3DTADDRESS_*`. MIRROR and BORDER are published by neither engine and land
  as WRAP.
- `V9X_D3D_CONTEXT.texture_address`, from `D3DRENDERSTATE_TEXTUREADDRESS` and
  its two per-axis forms. This is a **different render state** from the
  existing `texture_wrap`, which carries `D3DRENDERSTATE_WRAPU`/`WRAPV` - that
  one is about how a coordinate is interpolated, not about what happens past
  its end. The default is `D3DTADDRESS_WRAP`, which is Direct3D's own.
- `v9x_d3d_soft_normalise` shifts a triangle's coordinates down by a whole
  number of repeats so the smallest corner lands in the first one. Under WRAP
  the sampled texel depends only on the coordinate modulo a repeat, so this
  changes no pixel - and it must be the same shift for all three corners or the
  texture shears across the triangle. It is what lets a negative `tu` work at
  all, since the rasterizer refuses a negative coordinate outright.
- `V9X_D3D_SOFT_TEXCOORD_SCALE` is 65536 and no longer 65535. `tu = 1.0` has to
  land on the first texel of the next repeat rather than the last texel of this
  one, or every seam in a tiled surface repeats a texel column.
- `dwTextureAddressCaps` publishes `WRAP | CLAMP`. Not `INDEPENDENTUV`: one
  context field carries both axes, so the last of the two states to be set
  wins.

## Evidence

The interpolator change had to be exactly equivalent wherever the old form was
defined, and the whole existing host suite passing unchanged is the first
evidence of that - it includes exact-pixel colour tables, Gouraud ramps, the
shared-edge coverage property and the 4x2048 full-height worst case. A
throwaway harness then compared the two forms directly over **48,147,468
cases**: exhaustive on a small grid, then random over the ranges the rasterizer
uses, with the overflowing cases excluded. Identical on every one.

Four new tests: WRAP tiles a one-column texture four times across a quad
spanning four repeats and CLAMP shows it once at the left with black after it;
a draw at `TEXCOORD_MAX` on both axes with the bilinear filter - the arm with
the larger intermediate - samples correctly and one beyond it is refused;
`D3DTADDRESS_MIRROR` is refused as an address mode; and a falling depth
gradient lands on 825 rather than 826, which is the floor correction.

The CLAMP arm was **wrong when first written** - it clamped to `TEXCOORD_MAX`
rather than to the texture's edge, so a clamped draw tiled. The test caught it
before anything was deployed.

Five mutations were run against the suite. Three were caught (WRAP clamping
instead of wrapping, the address mode not validated, the floor correction
dropped). Two were equivalent mutants rather than test gaps: `& mask` rewritten
as `% (mask + 1)` is the same for non-negative operands, and clamping to
`33 * ONE - 1` instead of `ONE - 1` selects the same texel, because the mask
discards whole repeats. Worth recording, because both looked like gaps for
about twenty minutes.

`./scripts/run-checks.ps1` green.

**WIN98-S3NATIVE, 86Box, S3 Trio64 (5333:8811), `Direct3DMode=software`**,
boot 300:

```
D3DDevice2HwTriAddress=5      0x5 = WRAP | CLAMP, was 4
```

Every other `*Ok`, `*Count`, `*Hr` and `HwTri*` key in the file is
byte-identical to the run before. That covers a great deal more than the caps
bit: the interpolator was replaced under every draw the probe makes, the
texture coordinate scale changed, the default address mode flipped to WRAP, and
the coordinate range widened thirty-three fold. One key moved.

## What is still not there

Perspective correction. The interpolation is affine, so a texture on a surface
receding from the viewer swims - which is what the ViRGE's own non-perspective
command type does too, and `D3DPTEXTURECAPS_PERSPECTIVE` is published by
neither engine here while S3's driver publishes it.
