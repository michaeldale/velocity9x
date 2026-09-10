# The lightmap pass now draws, instead of being skipped

Date: 2026-09-11. Measured on the 86Box Trio64 guest, agent 9871, boot 349,
against the same guest's boot 346 from earlier the same day.

Earlier today the software engine stopped drawing `DESTCOLOR`/`ZERO` opaque
and started skipping it
([record](2026-09-11-the-software-engine-drew-an-inexpressible-blend.md)).
That stopped the multiplicative lightmap pass destroying the frame, and it
left the pass drawing nothing. This implements the factor, so the pass draws
what it means.

## Why the rasterizer gets a fifth factor

The four it had - ONE and SRCALPHA for source, ZERO and INVSRCALPHA for
destination - were chosen by copying what S3's own ViRGE driver publishes on
this generation (98DDK D3DDRV.C:239-242). That was the right choice for the
hardware engine, which drives the S3D unit and can only ask it for what it
has. It was never a fact about the CPU rasterizer, which shares nothing with
the S3D unit but the caps table it was copied from.

`DESTCOLOR` costs this engine one product per channel on pixels that are
already being blended. It is what a multiplicative lightmap pass asks for,
and it is the reason 3DMark 99's panels came out saw-toothed on the hardware
path. So the software engine now has five factors and says so; the hardware
engine still has four and says so. They publish from different
`describe_caps`, which is what makes that possible.

Advertising it matters as much as implementing it. An application reads
`dwSrcBlendCaps` to decide whether to attempt the pass at all - leaving the
bit out and accepting the state anyway means the pass is never asked for.

## The arithmetic, and why the divide is exact

Per channel, `out = src * dst / 255`, with the destination's own channel as
the factor - so it cannot use the scalar weight pair the other four share,
and it is carried as its own per-span flag with the weights applied to the
product afterwards. Both operands are 0..255 by the time it runs, the source
by the span's colour clamp and the destination by `expand5`/`expand6`, which
puts the rounded product inside the range the exact divide is asserted for.

The divide has to be exact rather than a shift by eight. A lightmap that
lights nothing multiplies by white, and `255 * c` over an approximate divide
returns `c - 1` for most of the range: the frame comes back one level darker
everywhere the pass touched, which reads as a haze rather than as a bug. It
is the same error the 255-to-256 weight correction already exists to avoid.

## The test that could tell the difference, and the one that could not

The probe's `BlendModulate` cell draws a white triangle over green, and its
own comment is right that green is the only acceptable answer - but a correct
multiply and a skipped draw both leave green. It cannot separate the fix
above from this one, and it was never meant to.

So both layers gained a case with a source that is not white.

`test_alpha_destcolor_multiplies` in the host tests draws (128, 255, 64) over
magenta and requires (128, 0, 64): a skip leaves magenta, an opaque draw
leaves the source, only a multiply lands there, and the destination's zero
green forces the full-scale source green to vanish, which is what says the
factor is per channel rather than scalar. It failed before the change, on the
`alpha_valid` refusal, and passes after.
`test_alpha_destcolor_white_is_identity` pins the round trip the exact divide
buys.

The probe's new `BlendMultiply` cell asks it through the installed driver:
green over magenta, which have no channel in common, so a multiply leaves
black, a skip leaves magenta and an opaque draw leaves green - three
outcomes, three hues, no arithmetic that depends on 5:6:5 against 5:5:5. It
reads the destination back before the draw as well, because black is also
what an empty surface looks like and the fill has to be shown to have landed
before its disappearance means anything.

## What boot 349 says

```
BlendMultiplyDstRaw      63519         0xF81F, the magenta fill landed
BlendMultiplyRaw         0             black: the multiply happened
BlendMultiplyOk          1
BlendModulateRaw         992           0x03E0, the white identity case, unchanged
BlendModulateOk          1
D3dBlendSkipped          0             where boot 346 read 1, pair 0x00090001
D3DDevice2HwTriSrcBlend  274           0x112: ONE | SRCALPHA | DESTCOLOR
```

`compare-probe.ps1` over boot 346 and boot 349 reports exactly one changed
key - `D3DDevice2HwTriSrcBlend`, 18 to 274 - plus the four keys of the new
cell. Nothing else moved, `VBlankStatus` included this time. In particular
the existing blends still land: `VtxAlphaCurveOk` and the alpha rungs are
unchanged.

## It costs nothing measurable

The new arm is a branch inside the block that only blended pixels enter, and
that block already pays an unpack and three blends. `V9XSOFT` on the same
boot, against the sampler-fix candidate from 2026-09-10:

| | Small | Gouraud | Point | Bilinear | Depth | Alpha |
|---|---|---|---|---|---|---|
| RAM | 1.007 | 1.000 | 0.994 | 0.995 | 1.000 | 1.000 |
| VRAM | 1.000 | 0.999 | 1.000 | 1.000 | 1.000 | 1.000 |

The alpha rung is identical to the millisecond in both locations. Everything
here is inside this instrument's noise; nothing in the table is a claim of a
gain or a loss.

## What this does not cover

**No application, and nothing physical.** A lightmapped scene through
software Direct3D has still never been drawn, on silicon or in a guest. What
is established is that the pass now produces the product instead of nothing.

**The destination side is unchanged.** `DESTCOLOR` is accepted as a *source*
factor only; as a destination factor it is still refused, and the host test
that checks that refusal is unchanged. `SRCCOLOR`, `INVDESTCOLOR` and the
rest are still refused and still skipped-and-counted by the engine, which is
what `D3dBlendSkipped` is for.

**The hardware path is untouched.** `d3d_virge.c` keeps its four factors, its
own `describe_caps` and its own skip. The S3D unit cannot express this blend
and nothing here claims otherwise.

## Gates

`run-checks.ps1` green, including the rasterizer's frozen pixel table and its
8-group 256-draw hash corpus, which are unchanged - the new arm is only
reachable through a factor pair the corpus does not use. Verified on
`Win98SE-Trio64` with `Direct3D=2`, which serves every Direct3D draw from the
CPU. `build-host-msvc.ps1` cannot run on this host.
