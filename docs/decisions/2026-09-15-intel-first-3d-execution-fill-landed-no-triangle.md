# The GPU executed a Phase 5 3D stream. Fill reached every probe; the triangle did not.

Measured on MICHAEL-NETBOOK (945GSE A3), 2026-09-15, build `4628b66`.
Armed one-shot, token `phase5-20260915-192853`, retired.
Capture at `probe/intel-phase5/INTEL3D0-armed3-4628b66.txt`.

## What happened

The full chained transaction completed, and the machine did not hang.

```
Access=armed-one-shot     ArmPhase=00000005
ChainStateOnEntry=00000002   (REPLAYED)
Phase4Passed=00000001     Precondition=00000000
StageBase=D06B0000        StageFail=00000000    P5Marker=00000040
ExecFailure=00000000      EX0016..EX001A all 00000000
IntentStep=0000001D       Result=PASS
ChainDrawVerdict=00000000 ChainState=00000003   (DREW)
TokenRetired=1
```

`INTELARM.TXT` afterwards: `IntelInFlight=` cleared,
`IntelLastResult=pass:phase5:phase5-20260915-192853`. The one-shot token was
retired exactly once, at the draw result, which is the only place permitted to
retire it.

**The GPU executed the stream.** All 66 dwords staged, verified against the
generated table, the ring was programmed, both submissions drained without a
timeout, and teardown completed.

**The fill reached the probed points.** All fourteen probes read `08420842` -
`V9X_I9XX_FILL_DWORD`. On the unarmed B1 two hours earlier, samples in the same
target region read a mix of `00000000` and `FFFFFFFF`. Those were eight sample
addresses, not these fourteen probe addresses, so the comparison is between two
different point sets in one region and not a before-and-after of the same
points. Fourteen points do not establish that all 614,400 bytes
were written; the bulk read-back that could have is the operation that hard
locks this part. What is established is that the GPU executed a 3D-pipeline
command stream and altered the render target, for the first time under this
driver.

**The triangle was not observed.** The seven probes inside the expected
triangle read the fill colour, not `0000FB25`:

| Probe | Expected | Read |
|---|---|---|
| NearV0, NearV1, NearV2 | `FB25` | `0842` |
| MidTop, MidLeft, MidRight | `FB25` | `0842` |
| Corner00, CornerX0, Corner0Y, CornerXY | `0842` | `0842` |
| OutsideTop, OutsideLeft, OutsideRight | `0842` | `0842` |

The outside probes are correct. The inside probes read fill rather than some
third value, which rules out the triangle being rasterised **in a wrong colour
at those points**.

It does **not** establish that nothing rasterised anywhere. Two explanations
survive this capture and are not distinguished by it:

- **Displaced geometry.** A triangle drawn somewhere other than where the
  vertices specify would leave all fourteen probes reading fill, because the
  probes sample where the triangle was *expected*. Only three of the fourteen
  are far from the expected shape.
- **Fill-coloured output.** A triangle rasterised correctly but shaded with the
  fill value - a fragment program that passes through the wrong register, say -
  is indistinguishable from absence at every probe.

The honest statement is narrower than "nothing was drawn": the expected
triangle was not observed at the fourteen sampled points.

## What this rules out

- **Submission.** Both drains completed and teardown ran.
- **Stream corruption.** `StreamCrc=0ED8C9A3` equals `GeneratedCrc`, and the
  mini-VDD re-verified all 66 staged dwords against the generated table at
  step 22 before programming anything.
- **Culling.** `S4` assembles to `0x00902480`, which carries
  `V9X_I9XX_S4_CULLMODE_NONE`. The winding order cannot matter.
- **The drawing rectangle.** `0x01DF027F` is 639 x 479; the triangle at
  (160,120) (480,120) (320,400) is well inside it.
- **The probe addresses.** They read the fill correctly, so they address the
  target.
- **Depth and blend.** `S5` and `S6` are both zero, and every relevant enable
  in them is a set bit.

## What it does NOT establish

Nothing about *why*. The remaining candidates - the fragment program not
writing its output, the `S4` vertex-format bits disagreeing with the 5-dword
XYZW+diffuse layout the vertex run actually emits, the `_3DPRIMITIVE` header's
count encoding, or a state register this audit set to zero that needed
something else - are not distinguished by this capture and no attempt is made
here to rank them.

The audit that licensed these packets recorded that the two source trees
genuinely disagree about `S4`'s vertex format, and that the field could not
meet the project's two-use-site rule. That is a recorded judgement, and it is
now the first thing evidence could overturn.

## The gap this exposed in the capture

Phase 5 did not read the GPU's error registers. Phase 4 has read EIR, EMR,
ESR, PGTBL_ER and the instruction-error trio since its first armed boot; Phase
5 never did. So the capture cannot say whether the parser **rejected** the
primitive or **accepted it**, and those are different faults with different
fixes.

Added in the same change as this record: `PreErr0..8` before the first
submission and `PostErr0..8` after the draw and before any pixel read, nine
bounded reads through the mini-VDD's existing diagnostic verb. The next armed
boot answers that question.

## Standing

Phase 5's objective - one triangle on screen - is **not met**. What is met is
everything up to and including the GPU executing a reviewed 3D command stream
and altering the render target, under a one-shot arm that retired correctly, on
a part whose errata made all of it risky.

Where the triangle went - if it went anywhere - is not known, and the fourteen
probes cannot answer it. Locating a displaced or fill-coloured triangle needs
more sampled points, which is the read-budget question in
`plans/intel-phase5-bounded-readback.md`, not a free one.
