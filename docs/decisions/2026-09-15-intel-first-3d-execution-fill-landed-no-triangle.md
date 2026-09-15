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

## The error registers, measured 2026-09-15, build `9064942`

A second armed boot, same result at every probe, with `PreErr`/`PostErr` added.
Capture at `probe/intel-phase5/INTEL3D0-armed4-9064942.txt`.

| Index | Offset | Register | Pre | Post |
|---|---|---|---|---|
| 0 | `2088` | IPEIR | `00000000` | `00000000` |
| 1 | `208C` | IPEHR | `00000000` | `7F00000E` |
| 2 | `2090` | INSTDONE | `7FFFFFC0` | `7FFFFFC0` |
| 3 | `20C8` | | `00000000` | `00000000` |
| 4 | `20B0` | EIR | `00000000` | `00000000` |
| 5 | `20B4` | EMR | `FFFFFFFF` | `FFFFFFFF` |
| 6 | `20B8` | ESR | `00000000` | `00000000` |
| 7 | `203C` | RING_CTL | `00000000` | `00000000` |
| 8 | `2038` | RING_START | `00000000` | `00000000` |

`PreErrOk` and `PostErrOk` are both 1 with a count of nine, so both reads
completed.

**EIR, ESR and IPEIR read clear before and after.** With `EMR=FFFFFFFF` that is
not evidence of no error - see below.

`IPEHR` moves from zero to `7F00000E`, which is the `_3DPRIMITIVE` header dword
at stream index `0x32`. The parser latched that specific instruction.

### This does NOT establish that the primitive was accepted

That was claimed here when the measurement landed, and it was wrong.

**`EMR=FFFFFFFF` masks every error.** Linux's i915 writes the error mask as the
complement of what it wants detected -
`error_mask = ~(I915_ERROR_PAGE_TABLE | I915_ERROR_MEMORY_REFRESH)` in
`i965_irq_postinstall`, `drivers/gpu/drm/i915/i915_irq.c` - so a **set** bit
masks that error and i915 must actively clear bits to enable detection. Its
comment there also records that the instruction-error mask bit is reserved and
deliberately left masked.

This machine's EMR is all ones, which nothing in this driver wrote. So EIR
could not latch whatever happened, and **EIR reading clear is uninformative**.
The same caution applies to ESR and IPEIR until their relationship to the mask
is established rather than assumed.

What the measurement does establish is narrower: no error was *reported*, which
is not the same as none occurring, and `IPEHR` moved from zero to the
`_3DPRIMITIVE` header. `IPEHR` semantics remain unverified - on this part it may
hold the header of an erroring instruction or simply the last header parsed -
so it is corroboration that the primitive was reached and nothing more.

Making EIR informative means writing EMR, which is a GPU register write this
driver has never made and which the 2026-09-15 errata decision does not
authorise. That is a scope question, not a code change.

RING_CTL and RING_START reading zero after teardown is expected; teardown
clears them.

## Packets re-audited against Mesa's register header, 2026-09-15

No boot. Source: `src/gallium/drivers/i915/i915_reg.h` and
`i915_prim_emit.c` from Mesa, fetched from gitlab.freedesktop.org.

This was prompted by a fair objection: the original audit recorded that xf86
uses `S4_VFMT_XY` and Mesa uses `XYZW` plus colour, and treated that as the
trees disagreeing. They do not disagree - those are two valid layouts, and the
only question that matters is whether **our** format bits match **our** emitted
vertices.

| Packet | Our value | Verified against | Verdict |
|---|---|---|---|
| `S4` | `00902480` | `S4_VFMT_XYZW (2<<6)`, `S4_VFMT_COLOR (1<<10)`, `S4_CULLMODE_NONE (1<<13)`, `S4_LINE_WIDTH_ONE (0x2<<19)`, point width `1<<23` | correct |
| `_3DPRIMITIVE` | `7F00000E` | `(0x3<<29) OR (0x1f<<24)`, `PRIM_INLINE`, `PRIM3D_TRILIST`, count from `(4 + vertex_size*nr)/4 - 2` = `(4+60)/4-2` = 14 | correct |
| `LOAD_STATE_IMMEDIATE_1` | `7D0407C4` | `(0x3<<29) OR (0x1d<<24) OR (0x04<<16)`, `I1_LOAD_S(n) = 1<<(4+n)` giving S2..S6, length 4 | correct |
| `DST_BUF_VARS` | `00880200` | `COLOR_BUF_RGB565 (2<<8)`, `DSTORG_HORT_BIAS(8)`, `DSTORG_VERT_BIAS(8)` | correct |
| colour `BUF_INFO` | `03000500` + `006C2000` | `BUF_3D_ID_COLOR_BACK (0x3<<24)`, `BUF_3D_PITCH(x) = (x/4)<<2` giving 1280 | correct |

**`S4` is cleared.** `V9X_I9XX_S4_VFMT_XYZW` is `0x80`, which is exactly Mesa's
`(2<<6)`, and `XYZW + COLOR` declares four position floats plus a colour dword -
the five dwords per vertex `i9xx_vertex.c` emits. The bits and the vertices
agree. The earlier framing that this field was a fragile judgement call is
withdrawn.

`_3DSTATE_DFLT_DIFFUSE`, `_3DSTATE_DFLT_SPEC` and `_3DSTATE_DFLT_Z` are all
present in the stream at the opcodes Mesa defines.

### One thing this audit noticed and did not resolve

The depth `BUF_INFO` is `07001000` with a second dword of `00000000`:
`BUF_3D_ID_DEPTH` with a pitch of 4096, at **address zero**, while `S6` leaves
both depth test and depth write disabled.

Whether declaring a depth buffer at address zero is benign when depth is
disabled on this part is **not established**. It is recorded as the one
oddity the re-audit surfaced, not as a diagnosis - nothing here says it is
related to the missing triangle.

Still unverified by this pass: the seven-dword fragment program, and the
values of `S2`, `S3`, `S5` and `S6`.

## Standing

Phase 5's objective - one triangle on screen - is **not met**, and the result
is reproducible: two armed boots on different builds, fourteen probes reading
fill on both. What is met is
everything up to and including the GPU executing a reviewed 3D command stream
and altering the render target, under a one-shot arm that retired correctly, on
a part whose errata made all of it risky.

Where the triangle went - if it went anywhere - is not known, and the fourteen
probes cannot answer it. Locating a displaced or fill-coloured triangle needs
more sampled points, which is the read-budget question in
`plans/intel-phase5-bounded-readback.md`, not a free one.
