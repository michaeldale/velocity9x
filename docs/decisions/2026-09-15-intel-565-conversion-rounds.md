# The GMA 950 rounds 8-bit colour to RGB565; it does not truncate

**Machine:** MICHAEL-NETBOOK, 945GSE A3, `8086:27AE` rev 03, AC power.
**Builds:** `6c81c52` (first colour), `83f24ec` (second colour).
**Captures:** `C:\temp\intel41\INTEL3D0.TXT`, `C:\temp\intel42\INTEL3D0.TXT`.

## Finding

The colour backend converts an 8-bit channel to its RGB565 field by
**rounding**: `round(v * max / 255)`, with `max` 31 for red and blue and 63 for
green. Truncation, floor, `round8` and ceil are all excluded.

## Evidence

Two triangle colours, each a flat fill with the same colour at all three
vertices, sampled at seven interior probes and seven exterior probes.

| Colour (ARGB) | R,G,B | `round` | `trunc` | `floor` | `round8`/`ceil` | Observed |
|---|---|---|---|---|---|---|
| `0xFFF86428` | 248,100,40 | `F325` | `FB25` | `FB25` | `FB25` | **`F325`** |
| `0xFF1587F9` | 21,135,249 | `1C3E` | `143F` | `143E` | `1C5F` | **`1C3E`** |

All seven interior probes read the same value in both captures; all seven
exterior probes read the fill `0842`; guards `A5A5A5A5` and `00000000`
untouched; `Result=PASS`; token retired.

**Why two colours and not one.** The first colour separates `round` from
`trunc` in red alone — green and blue agree under both rules, so it licenses a
claim about one channel and nothing more. The second separates them in red and
blue, and agrees with the first on green. Only together do they cover all three
channels. A single colour would have left two channels satisfied by either
rule, which is the reading error this table exists to prevent.

The second colour was chosen before the boot, with its predictions written
down, precisely so the result could not be fitted afterwards:
`docs/plans/intel-phase5-colour-conversion-experiment.md`.

## What this does not establish

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
