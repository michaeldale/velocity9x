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

## Candidate formulas

Five formulas, not six. `(v*(max+1))>>8` is **not** a distinct rule: with
`max = 2^n - 1` it is `v * 2^n >> 8`, which is `v >> (8-n)` exactly, verified
over every `v` in 0..255 for n in {5,6}. It is merged into `trunc` below.

All are stated exactly so the table is reproducible. `n` is the channel width,
`max = 2^n - 1`.

| Name | Formula |
|---|---|
| `trunc` | `v >> (8-n)` |
| `round8` | `min(max, (v + 2^(7-n)) >> (8-n))` - round in 8-bit space, clamped, since the add can carry past `max` |
| `floor` | `(v * max) / 255`, integer division |
| `round` | `floor(v * max / 255 + 0.5)` |
| `ceil` | `ceil(v * max / 255)` |

These are **candidate formulas**, not independent hypotheses. `round8` and
`ceil` coincide on many inputs, and any two may agree at a given value; a
channel that separates `round` from one of them has not thereby tested the
others.

## What the colour already measured establishes

Source `0xFFF86428`: R=248, G=100, B=40. Hardware: R=30, G=25, B=5.

| Candidate | R | G | B | Verdict |
|---|---|---|---|---|
| `trunc` | 31 | 25 | 5 | eliminated by R |
| `round8` | 31 | 25 | 5 | eliminated by R |
| `floor` | 30 | 24 | 4 | eliminated by G and B |
| `round` | 30 | 25 | 5 | **survives** |
| `ceil` | 31 | 25 | 5 | eliminated by R |
| hardware | **30** | **25** | **5** | |

Four candidate formulas are excluded at these three values and one survives.
The earlier record said only red discriminated; that was true of `trunc`
against `round` alone.

**Tie-breaking cannot be tested.** `v*max/255` is never exactly `x.5` when the
denominator is odd, so no byte value separates round-half-up from
round-half-even or round-half-to-zero.

**No single byte value separates `round` from `trunc`, `round8` and `floor` at
once** - searched exhaustively over 0..255 for both channel widths. A colour
can only cover them across different channels.

## Proposed colour

`V9X_I9XX_TRI_COLOR_BGRA = 0xFF1587F9` - R=21, G=135, B=249.

| ch | byte | `trunc` | `round8` | `floor` | `round` | `ceil` | separates `round` from |
|---|---|---|---|---|---|---|---|
| R | 21 | 2 | 3 | 2 | **3** | 3 | `floor`, `trunc` |
| G | 135 | 33 | 34 | 33 | **33** | 34 | `ceil`, `round8` |
| B | 249 | 31 | 31 | 30 | **30** | 31 | `ceil`, `round8`, `trunc` |

Across the three channels this separates `round` from all four alternatives -
but note it does so **per channel**, not three times over. Predicted RGB565:

| Rule | Value |
|---|---|
| `round` | `0x1C3E` |
| `trunc` | `0x143F` |
| `round8` | `0x1C5F` |
| `floor` | `0x143E` |
| `ceil` | `0x1C5F` |

`round8` and `ceil` predict the same value here, so a `1C5F` reading would not
separate those two - both are already excluded by the first colour. All five
differ from the fill `0x0842` and from the current triangle `0xF325`.

## The conclusion this experiment licenses

If the probes read `1C3E`:

> At the six channel values tested across two colours, the hardware's
> conversion agrees with `round(v*max/255)` and disagrees with `trunc`,
> `round8`, `floor` and `ceil`.

Not "the hardware rounds". Six values do not characterise a function over 256
inputs, and the five formulas are not an exhaustive set - a lookup table, a
piecewise approximation or an ordered dither could agree with `round` at all
six.

Any other reading excludes `round` as well, which would be the more
informative outcome.

## Consequence for the expected values

**Not a change to the shared software rasteriser.** A mismatch here establishes
that the current reference does not predict *this GPU at these values*. It does
not establish that the conversion in `d3d_raster.c` is wrong for its own
contract - that code serves the software D3D path across every family, and what
it owes its callers is a separate question that has not been reviewed.

So if this confirms, the change belongs in an **Intel-specific expectation**:
the value `check-intel-3d-capture.ps1` compares probes against for this part.
The shared conversion stays as it is until its contract is examined on its own
terms, in its own diff.

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
