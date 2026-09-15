# Phase 5: the GMA 950 drew the triangle

Measured on MICHAEL-NETBOOK (945GSE A3), 2026-09-15, build `6c81c52`.
Armed one-shot, token retired. Capture at
`probe/intel-phase5/INTEL3D0-armed5-6c81c52.txt`.

**Phase 5's objective is met.** A Velocity9x-authored 3D command stream ran on
the GPU and rasterised a triangle into a render target the same stream filled.

## The evidence

```
Access=armed-one-shot   ArmPhase=00000005   ChainStateOnEntry=00000002 (REPLAYED)
Phase4Passed=00000001   StreamCrc=3C23CA17 = GeneratedCrc
StageFail=00000000      P5Marker=00000040   ExecFailure=00000000
IntentStep=0000001D     Result=PASS
ChainDrawVerdict=00000000  ChainState=00000003 (DREW)  TokenRetired=1
PostErrOk=1             PreErrOk=1
```

The probes:

| Probe | Expected | Read |
|---|---|---|
| `NearV0`, `NearV1`, `NearV2` | inside | `F325F325` |
| `MidTop`, `MidLeft`, `MidRight` | inside | `F325F325` |
| 7th inside probe | inside | `F325F325` |
| `Corner00`, `CornerX0`, `Corner0Y`, `CornerXY` | outside | `08420842` |
| `OutsideTop`, `OutsideLeft`, `OutsideRight` | outside | `08420842` |

**Every inside probe carries triangle colour and every outside probe carries
fill.** Seven and seven, at the coordinates the vertices specify. The guards
either side of the target are untouched - `GLow0`/`GLow1` both `A5A5A5A5`,
`GUpp0`/`GUpp1` both zero - so nothing wrote outside the reserve.

## The cause of the previous null, confirmed

The only change from `4628b66`/`9064942`, which drew nothing, is
`S6_COLOR_WRITE_ENABLE`. Those builds accepted the primitive, reported no
error, and wrote no colour; this one writes colour. That is as direct a
confirmation of the S6 diagnosis as the hardware can give.

## The one discrepancy: red is one LSB low

The software reference says `FB25`; the hardware produced `F325`.

| | R | G | B |
|---|---|---|---|
| Reference `FB25` | 31 | 25 | 5 |
| Hardware `F325` | **30** | 25 | 5 |

The source colour is `0xFFF86428`: R=248, G=100, B=40.

- Truncation, `v >> 3` and `v >> 2`, gives R=31, G=25, B=5 - the reference.
- Scaling, `round(v * max / 255)`, gives R=30, G=25, B=5 - the hardware.

Green and blue are identical under both rules. **Red at 248 is the only one of
the three channels that distinguishes them**, and it favours scaling.

### The fragment program is not the cause

Audited against Mesa's `i915_reg.h`, 2026-09-15, no boot. Both instructions
decode cleanly:

| Dwords | Decoded |
|---|---|
| `190A3C00 00000000 00000000` | `D0_DCL` (`0x19<<24`), type `REG_TYPE_T`, nr 8, channels ALL - `dcl t8.xyzw`, the interpolated diffuse |
| `02203CA0 01230000 00000000` | `A0_MOV` (`0x2<<24`), dest `REG_TYPE_OC` nr 0 channels ALL, src0 `REG_TYPE_T` nr 8, swizzle `x=X y=Y z=Z w=W`, saturate clear - `mov oC, t8` |

`_3DSTATE_PIXEL_SHADER_PROGRAM` is `0x7D050000`; ours is `0x7D050005`, a length
of 5 for six program dwords.

This is the canonical minimal pass-through: declare the interpolated diffuse,
move it to output colour. There is no arithmetic, the swizzle is the identity
and saturate is clear, so **nothing in the shader can lose a low bit.** That
alternative is eliminated.

### What the colour difference is, and what still is not proven

With the shader cleared, the difference is in how a normalised byte colour
becomes a 5- or 6-bit channel:

| Channel | Source byte | Truncate `v>>n` | `round(v/255 * max)` | Hardware | Discriminates? |
|---|---|---|---|---|---|
| R | 248 | 31 | 30 | **30** | **yes** |
| G | 100 | 25 | 25 | 25 | no |
| B | 40 | 5 | 5 | 5 | no |

The hardware is consistent with `round(v/255 * max)` on **all three** channels.
Truncation matches two of three and fails on red. That is the rule the hardware
path implies anyway: the vertex colour is a packed normalised BGRA dword, the
shader moves it as floats, and the conversion happens on write to an RGB565
target.

The software reference in `d3d_raster.c` truncates. On this evidence it is the
reference that is wrong, not the hardware.

**Corrected 2026-09-15:** the two rows above compare truncate against round
only, and on that pair red is the sole discriminator. Scored against a full
candidate set - truncate, floor, round, ceil, round-in-8-bit-space and
`(v*(max+1))>>8` - this colour discriminates on **two independent channels**:
red eliminates four candidates and green and blue eliminate a fifth, leaving
`round(v*max/255)` alone. The table is in
`plans/intel-phase5-colour-conversion-experiment.md`.

That is still six candidate rules at three values. It does not establish the
hardware's conversion in general, and tie-breaking cannot be probed at all
because `v*max/255` is never exactly `x.5` when the denominator is odd.

The reference is not altered, the comparison stays reported rather than
failed, and no golden is promoted.

## What Phase 5 does not claim

- Fourteen probes. The triangle was observed at seven interior points and
  absent at seven exterior ones; the edges, the fill rule and the exact
  silhouette are unmeasured, and the bounded read-back cannot measure them.
- One draw, on one machine, on AC, once.
- Nothing about sustained or repeated 3D work. Erratum 7's word "extended" is
  why the errata decision authorises one triangle and no more.
