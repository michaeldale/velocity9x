# Phase 0.5: ordering works; ViRGE colour encoding does not agree

Date: 2026-09-26

## Question

Can one colour/depth surface pair move from a hardware draw to CPU fallback
and back to hardware with ordered, mutually visible results and the same
16-bit encodings?

## Instrument

`V9XDDP.EXE /mixed` draws red at depth 0.5, locks the target and depth surface,
stores RGB565 green and depth `0x4000` at `(16,16)`, then draws blue at depth
0.375. The last draw must be rejected at `(16,16)` and accepted at `(20,16)`.
The locks are the public shared-drain boundary; the final reads lock again.

Both guests received fresh packages containing the external three-state
`v9x_render_drain`. Evidence beside this record:
`2026-09-26-phase05-mixed-virge-V9XDD.INI` (the split-verdict run on
`Win86SE`, `V9XDD2-SPLIT.INI` in its result folder) and
`2026-09-26-phase05-mixed-soft-V9XDD.INI` (`V9XDD2.INI` from the software
guest). The other INIs in `build/driver-results/phase05-*-20260926` are the
pre-`/mixed` default runs, with `MixedRun=0`, and are not evidence for
anything below.

## ViRGE result

Guest: `Win86SE`, 86Box ViRGE/DX, agent port 9869, boot 637. Driver build
`phase05-mixed-20260926`; split-verdict probe build
`phase05-mixed-split-20260926`. The full probe completed.

| Reading | Value |
| --- | ---: |
| First hardware colour | `31744` (`0x7C00`) |
| First hardware depth | `32768` (`0x8000`) |
| CPU colour | `2016` (`0x07E0`) |
| CPU depth | `16384` (`0x4000`) |
| Rejected pixel after second draw | colour `0x07E0`, depth `0x4000` |
| Accepted neighbor after second draw | colour `0x001F`, depth `0x6000` |
| `MixedOrderingOk` | `1` |
| `MixedColorEncodingOk` | `0` |
| `MixedDepthEncodingOk` | `1` |
| `MixedOk` | `0` |

The shared drain orders both ownership transitions, and the ViRGE's 1.31
depth conversion agrees exactly with the software renderer's 16-bit depth.
Colour does not agree: the target declares RGB565 and therefore expects red
`0xF800`, while the S3D engine writes the XRGB1555 value `0x7C00`. This is not
a stale-read symptom: the CPU green and later blue/depth results are mutually
visible exactly where expected.

## Software reference

Guest: `Win98SE-Fast-D3D`, VBE package with `Direct3D=2`, agent port 9878,
boot 595, build `phase05-softref-20260926`. The exported Direct3D object is
still the driver's HAL object (`D3DMainIsHardware=1`); its selected render
engine is the CPU implementation. The full probe completed.

It produced first colour `0xF800` and the same depth sequence `0x8000`,
`0x4000`, `0x6000`. `MixedOrderingOk`, `MixedColorEncodingOk`,
`MixedDepthEncodingOk`, and `MixedOk` were all one. This establishes that the
probe's exact RGB565/depth oracle and transition sequence pass on the software
reference.

## Decision

The ViRGE shared-drain ordering gate passes, but generic hardware-to-software
fallback on its declared RGB565 targets remains disabled. A fallback would
combine two incompatible colour encodings in one image. Fix or explicitly
model the ViRGE target format before wiring `accepts` to the CPU engine.

The Gen3 gate was not run: MICHAEL-NETBOOK timed out at its documented
`10.0.1.254:9869` endpoint. Timeout injection, texture-update visibility,
blended overlap, and an XRGB1555 target are also still required by Phase 0.5.
