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

### What that does NOT establish

One channel at one value is a single data point. Alternatives not excluded:

- Arithmetic in the seven-dword fragment program losing a low bit - that program
  is the piece the audit never verified.
- Rounding in the colour interpolation, even though all three vertices carry the
  same colour and flat shading should make it exact.
- A different conversion rule again that happens to agree with scaling here.

Distinguishing them needs a triangle whose colour channels separate the
candidate rules at more than one point, which is a cheap change to the vertex
colour and one boot. Nothing should be written into the reference until that
has run.

The software reference is therefore still **reported and not failed**, and no
golden is promoted. The comparison has now run against hardware exactly once.

## What Phase 5 does not claim

- Fourteen probes. The triangle was observed at seven interior points and
  absent at seven exterior ones; the edges, the fill rule and the exact
  silhouette are unmeasured, and the bounded read-back cannot measure them.
- One draw, on one machine, on AC, once.
- Nothing about sustained or repeated 3D work. Erratum 7's word "extended" is
  why the errata decision authorises one triangle and no more.
