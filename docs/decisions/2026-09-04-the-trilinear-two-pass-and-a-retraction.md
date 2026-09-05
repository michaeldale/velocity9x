# The trilinear two-pass, and a retraction: the Trio3D/2X does blend

Date: 2026-09-04
Status: measured on A8U4I5 with a controlled A/B, one variable, two boots. It
retracts two of today's decisions.

> **Corrected again 2026-09-05.** "A power cycle clears it" is refuted: the
> card entered the wrong state at boot 34 and stayed in it through a deliberate
> power cycle at boot 36, with the driver A/B'd out. Two states exist and
> blending is wrong in one of them; what moves the card between them is not
> known. See `2026-09-05-a-power-cycle-does-not-clear-the-trio3d-blend-state.md`.

> **Corrected 2026-09-04**, later the same day: where this says the cold-booted
> card's transfer curve is "a clean interpolation between the measured ends",
> only the ends are right. `AlphaCurveOk` tested the endpoints and not the
> middle, and the middle is wrong - see
> `2026-09-04-the-halfa-cells-were-right.md`. The finding below is unaffected:
> every reading in it is of endpoints, and endpoints are exactly what the power
> cycle changed.

## What is retracted

`2026-09-04-what-the-trio3d-blend-does-with-its-operands.md` and
`2026-09-04-no-encoding-of-the-alpha-field-blends-on-the-trio3d.md` conclude
that the S3 Trio3D/2X does not perform the S3D alpha blend under any encoding
of the command word's alpha field. **That is wrong.** The part blends, and it
blends exactly like the emulated ViRGE/DX.

On A8U4I5 boot 30, with a driver whose only functional difference was that it
emitted no trilinear second pass:

```
                      2026-09-04d (boot 27)      2026-09-04f (boot 30)
AlphaCurve_*          0, 4095, 7101, 8026, ...   31744, 28543, 24319, 20095,
                      8918, 7729, 5483, 2180, 0  14847, 10591, 6367, 2143, 31
AlphaCurveOk                0                          1
AlphaCurveBOk / COk         0 / 0                      1 / 1
Alpha1555Ok / 4444Ok        0 / 0                      1 / 1
AlphaCurveF3Ok              0                          1
TexMatrixOk                90 of 117                 108 of 117
D3DVertexAlphaBlendRaw      25352                      527
```

The new curve is a clean linear interpolation from the measured destination to
the measured source, and it is the emulated ViRGE/DX's curve to within the
rounding. All eighteen `alpha` cells of the matrix pass. The forced-encoding
sweep now reads exactly as it does on the emulator: encoding 10 blends,
00, 01 and 11 draw the fragment opaque.

## The A/B, and what it says the cause was not

Three things changed between those runs: the driver, an agent update, and a
power cycle - the machine dropped off the network for some minutes and came
back on a cold boot. So the driver was not established as the cause, and one
variable was put back.

Boot 31, same agent, same probe binary, the trilinear two-pass **restored** and
nothing else touched:

```
                     two-pass ON (b31)   two-pass OFF (b30)
AlphaCurveOk               1                   1
AlphaCurveBOk / COk        1 / 1               1 / 1
Alpha1555Ok / 4444Ok       1 / 1               1 / 1
TexMatrixOk              108                 108
AlphaCurve_* raws        identical           identical
MipTri_0/1/2         15342,14815,32192   31744, 992, 31
MipTriDegradedOk           0                   1
```

**The two-pass is not what broke alpha.** With it restored, every alpha
reading stays correct. So the change between boot 27 and boot 30 that fixed
alpha was not the driver: on the evidence available it was the power cycle,
and the agent update cannot touch a display driver. One observation, so that is
a hypothesis and not a finding - but it is the only one of the three that
survives the A/B.

It also explains the value that was said to "wander".
`D3DVertexAlphaBlendRaw` reads 527 on this cold-booted machine and read 25352
across boots 22 to 27, which were all warm restarts of a machine that had been
running since before those measurements. Four warm boots in a row gave 25352
every time, which is what made it look deterministic
(`2026-09-04-what-the-trio3d-blend-does-with-its-operands.md`). It is
deterministic - per power cycle.

**Something the card carries across a warm restart makes its blend wrong, and a
power cycle clears it.** What that something is, this does not say. It is now
the open question, and it is worth more than anything else on the list: every
Trio3D alpha measurement this project has taken before today was made in that
state.

## What survives, and is kept

The two-pass form itself is wrong on this part, and that is independent of the
alpha question - it is wrong on a boot where every single-pass blend is right:

```
                emulated ViRGE/DX      Trio3D/2X, two-pass ON
MipTri_0_Raw    (14, 17,  0)           (14, 31, 14)
MipTri_1_Raw    ( 0, 14, 17)           (14, 14, 31)
MipTri_2_Raw    (17,  0, 31)           (31, 14,  0)
```

Every step has the channel neither of its two levels carries. So the degrade
stays, under a capability that says what was actually measured:
`V9X_DD_ENGINE_CAP_S3D_TWO_PASS` - "two passes over one triangle, blended
together, produce the right answer on this part" - set for the ViRGE/DX and
cleared for the Trio3D/2X. It is not about alpha, and the code that reads it is
named `v9x_d3d_virge_two_pass_ok`.

With it cleared, on boot 32: `MipLadderOk=1`, `MipTriDegradedOk=1`, and
`MipTri_0/1/2` read 31744, 992, 31 - exactly red, green and blue, the lower
level of each pair, clean bilinear with no blend in it.

## Where the card stands now

`TexMatrixOk=108` of 117. The nine that remain are the `halfa` cells and only
those - ARGB4444 at alpha 8 of 15, at all three sizes and all three layouts.
Their left half is now correct (992, green) and the right reads 15871
(`0x3DFF`: r15 g15 b31) where a half-blended blue is wanted. That is the whole
of what the probe still calls wrong on this part.

## Gates

check-tree, vga survey safety gate, host tests and family packages
(run-checks), before each of the three deployments above.

## What to do about the retracted documents

They stay where they are, with this one linked from the issue that collects
them. A decision that was wrong is evidence about how it was reached: both were
measured carefully, on hardware, with an emulator control - and both were
wrong because every reading was taken in a machine state nobody knew was a
variable. The rung that would have caught it is a power-cycle repeat of the
kind the vertex-alpha item asked for, done cold rather than warm.
