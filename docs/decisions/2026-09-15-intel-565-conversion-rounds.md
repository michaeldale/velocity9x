# The GMA 950 agrees with rounding at every 565 channel value tested

Not "the GMA 950 rounds". Six channel values, and green never separates
rounding from truncation - see the limits below before quoting this.

**Machine:** MICHAEL-NETBOOK, 945GSE A3, `8086:27AE` rev 03, AC power.
**Builds:** `6c81c52` (first colour), `83f24ec` (second colour).
**Captures:** `C:\temp\intel41\INTEL3D0.TXT`, `C:\temp\intel42\INTEL3D0.TXT`.

## Finding

Every channel value tested agrees with **rounding**, `round(v * max / 255)`,
with `max` 31 for red and blue and 63 for green. Six channel values across two
colours. As *uniform* rules, `trunc`, `round8`, `floor` and `ceil` are each
excluded.

That is narrower than "the chip rounds", and the gap is stated below rather
than glossed.

## Evidence

Two triangle colours, each a flat fill with the same colour at all three
vertices, sampled at seven interior probes and seven exterior probes.

Per channel, because the whole-value table hides which channel did the work:

| Colour | ch | byte | `trunc` | `round8` | `floor` | `round` | `ceil` | **observed** |
|---|---|---|---|---|---|---|---|---|
| `0xFFF86428` | R | 248 | 31 | 31 | 30 | 30 | 31 | **30** |
| | G | 100 | 25 | 25 | 24 | 25 | 25 | **25** |
| | B | 40 | 5 | 5 | 4 | 5 | 5 | **5** |
| `0xFF1587F9` | R | 21 | 2 | 3 | 2 | 3 | 3 | **3** |
| | G | 135 | 33 | 34 | 33 | 33 | 34 | **33** |
| | B | 249 | 31 | 31 | 30 | 30 | 31 | **30** |

As whole RGB565 values:

| Colour | `round` | `trunc` | `round8` | `floor` | `ceil` | **Observed** |
|---|---|---|---|---|---|---|
| `0xFFF86428` | `F325` | `FB25` | `FB25` | `F304` | `FB25` | **`F325`** |
| `0xFF1587F9` | `1C3E` | `143F` | `1C5F` | `143E` | `1C5F` | **`1C3E`** |

All seven interior probes read the same value in both captures; all seven
exterior probes read the fill `0842`; guards `A5A5A5A5` and `00000000`
untouched; `Result=PASS`; token retired.

The predictions were written down before the boot, so the result could not be
fitted afterwards: `docs/plans/intel-phase5-colour-conversion-experiment.md`.

## What this does NOT establish, and it is more than it first appears

**Green does not separate `round` from `trunc`.** At both tested green values
the two rules agree - 100 gives 25 under either, 135 gives 33 under either. So
a backend that **rounds red and blue but truncates green** fits every
observation here exactly as well as a uniform rounding backend does. Nothing
measured distinguishes them.

Green is not useless: it excluded `floor` at colour 1 and `round8`/`ceil` at
colour 2. It is specifically `trunc` that it cannot separate, and `trunc` is
the most plausible alternative for a hardware colour path, since it is a shift.

This is testable and was simply not tested. A green byte where the rules part
company - 3, say, where `round(3*63/255)` is 1 and `3>>2` is 0 - would settle
it in one more boot. It has not been spent.

**The exclusions above are of uniform rules only.** Each candidate is excluded
as a rule applied to all three channels. A per-channel mixture is excluded only
where some channel separates the two members of the mixture, and for
`round`-versus-`trunc` on green, none does.

**Nothing about dithering.** Seven identical interior reads are consistent with
no dither, and also consistent with an ordered dither whose period happens to
align with scattered probes. The probes cannot settle it and are not claimed
to. `S5_COLOR_DITHER_ENABLE` is clear in the emitted stream, which is a
statement about what was submitted, not about what the backend did with it.

**Nothing about the fill rule.** The probes deliberately avoid the triangle's
edges. How the hardware includes or excludes edge pixels remains unmeasured.

**Nothing about other formats.** This is RGB565 from an 8-bit inline vertex
colour. It says nothing about 1555, 8888, or about texture sampling.

**Nothing about other Intel parts.** One 945GSE at one revision.

## What changed as a result

The expectation lives on the Intel side, not in the shared rasteriser.
`src/display32/d3d/d3d_raster.c` truncates. That is a legitimate choice, it is
what every other chip in this project is checked against, and changing it to
match one chip on one chip's evidence would alter every family's output for a
reason that applies to none of them.

So `v9x_i9xx_rgb565_round()` in `src/chipsets/intel/i9xx_3d_stream.c` carries
the measured rule, the generator publishes **both** references, and the
validator reports the software disagreement while **failing** on a disagreement
with the measured one. Two references rather than one, because collapsing them
would mean either failing on a known and understood difference or downgrading a
real regression to a warning.

The host tests assert the two **observed** values, not a recomputation of the
formula — a test that re-derives the expected number from the same arithmetic
the function uses passes for any formula.

## Incidental, recorded because it will be misread later

`PostErr0001` reads `7F00000E` after the draw in both captures, where it is
zero before. That offset is `0x208C`, the instruction-parser header register,
and `7F00000E` is the `_3DPRIMITIVE` header the stream ends with. It is the
last command header the parser saw, not an error latch: `EIR` (`0x20B0`) and
`ESR` (`0x20B8`) are zero, `INSTDONE` (`0x2090`) reads its idle pattern
`7FFFFFC0` before and after, and the pixels landed.

`EMR` (`0x20B4`) reads `FFFFFFFF` throughout, and a set bit **masks**, so `EIR`
being zero is not independent evidence of anything. The pixels are the
evidence. This is the same caution recorded when a clear `EIR` was briefly
read as proof the GPU had accepted a primitive it had in fact discarded.

## Repeat mode, confirmed on hardware

This was the first boot under `V9XARM5 R`. `IntelArmOnce` survived the run,
`IntelInFlight` is empty, `IntelIncomplete=0`, and
`IntelLastResult=pass:phase5:p5-20260915-83f24ec`. The next cold boot re-arms
with no DOS trip. The hang stop is untouched: a run that does not resolve
leaves `IntelInFlight` set and the following boot refuses.
