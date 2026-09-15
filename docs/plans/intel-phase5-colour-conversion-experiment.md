# The 8-bit to 5/6-bit colour conversion: prediction table and experiment

Open. Nothing implemented; no colour has been changed.

Follows `decisions/2026-09-15-intel-phase5-triangle-drawn.md`, where the
hardware produced `F325` for a triangle the software reference predicts as
`FB25` - red one step low.

## Confounders cleared first

The fragment program is **not** a confounder. It is
`dcl t8.xyzw` followed by `mov oC, t8` - a pass-through with no arithmetic,
identity swizzle and saturate clear, decoded field by field against Mesa's
`i915_reg.h`. Nothing in it can alter a channel value.

Interpolation is not excluded by audit, but all three vertices carry the same
colour, so any correct interpolator returns that colour unchanged.

## What the colour already measured establishes

Source `0xFFF86428`: R=248, G=100, B=40. Hardware: R=30, G=25, B=5.

| Candidate | R | G | B | Verdict |
|---|---|---|---|---|
| `truncate v>>(8-n)` | 31 | 25 | 5 | eliminated by R |
| `floor(v*max/255)` | 30 | 24 | 4 | eliminated by G **and** B |
| `round(v*max/255)` | 30 | 25 | 5 | **survives** |
| `ceil(v*max/255)` | 31 | 25 | 5 | eliminated by R |
| round in 8-bit space | 31 | 25 | 5 | eliminated by R |
| `(v*(max+1))>>8` | 31 | 25 | 5 | eliminated by R |
| hardware | **30** | **25** | **5** | |

This is stronger than first reported. The earlier record said only red
discriminated; that was true of truncate-versus-round alone. Against the full
candidate set the existing colour discriminates on **two independent
channels** - red eliminates four candidates, green and blue eliminate a fifth.

**Tie-breaking cannot be tested at all.** `v*max/255` is never exactly `x.5`
because 255 is odd, so no byte value distinguishes round-half-up from
round-half-even or round-half-to-zero.

## What a second colour can and cannot add

It cannot eliminate the five already eliminated - that is done. Its value is
narrower: confirming `round` at three further points, where a rule that
coincides with `round` at the first colour's values but diverges elsewhere -
a lookup table, a piecewise approximation, or an ordered dither - would show
up.

It cannot establish that `round` is the hardware's rule in general. Six values
across two colours is six values.

## Proposed colour, chosen so all three channels discriminate

`V9X_I9XX_TRI_COLOR_BGRA = 0xFF272B5F` - R=39, G=43, B=95.

| Channel | Byte | `truncate` | `floor` | `round` | Discriminates |
|---|---|---|---|---|---|
| R | 39 | 4 | 4 | **5** | yes |
| G | 43 | 10 | 10 | **11** | yes |
| B | 95 | 11 | 11 | **12** | yes |

Predicted RGB565:

| Rule | Value |
|---|---|
| `round` | `0x296C` |
| `truncate` | `0x214B` |
| `floor` | `0x214B` |

Distinct from the fill `0x0842` and from the current triangle `0xF325`, so a
capture cannot be misread as either.

Every channel separates `round` from both `truncate` and `floor`, so the boot
yields three independent confirmations rather than one.

## The conclusion this experiment licenses

If the probes read `296C`:

> The hardware's 8-bit to 5/6-bit conversion agrees with
> `round(v*max/255)` and disagrees with `truncate` and `floor` **at the six
> channel values tested across two colours.**

Not "the hardware rounds". Six points do not characterise a function over 256
inputs.

If the probes read `214B`, `round` is eliminated too and the conversion is
something none of the six candidates describes - which would be a more
interesting result than confirmation.

## Consequence for the software reference

`d3d_raster.c` truncates. If this confirms, the reference is wrong at the
tested values and should change - but changing it alters every software-path
expectation in the tree, so it is its own diff with its own gate run, not part
of this experiment.

## Not in this experiment

The depth `BUF_INFO` at address zero. Mesa's `i915_state_emit.c` emits no
depth `BUF_INFO` when there is no depth buffer - it is guarded on
`i915->current.depth_bo`, above a comment reading "What happens if no zbuf??".
So our packet is unjustified rather than justified, and the reference path
simply omits it.

It did not prevent this draw, which says only that: it did not prevent **this**
draw. It does not establish that declaring a depth buffer at address zero is
generally harmless. Removing it is a change to a configuration that currently
works, so it belongs in its own diff and its own boot - **not** folded in
alongside a colour change, which would put two variables in one experiment.
