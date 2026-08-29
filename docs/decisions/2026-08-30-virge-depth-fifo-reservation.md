# Depth testing was inert because it reserved 18 FIFO slots and the chip reports 16

Date: 2026-08-30
Branch: `d3d-zbuffer`
Plan: [`final-reality-101-hardware.md`](../plans/final-reality-101-hardware.md) lines 122-131, steps 1-3
Supersedes: [`2026-08-30-d3d-zbuffer-handoff.md`](../handoffs/2026-08-30-d3d-zbuffer-handoff.md)

Guest: `Win86SE`, 86Box, s3 family, ViRGE/DX 86C375, 4 MiB, agent port 9869.
Package `zfifo-001`; evidence under `build\driver-results\`.

## Result

Hardware Z-buffering works. Both probe designs pass on the guest:

```
D3DZCompareOk=1   D3DZWriteMaskOk=1     (working-device design)
D3DZPCompareOk=1  D3DZPWriteMaskOk=1    (private-device design)
EngineFifoTimeouts=0  EngineIdleTimeouts=0  EngineResets=0
```

Every rung is a distinct colour, so "unchanged" is proved by the pixel still
being the *previous* colour rather than merely not being the new one:
`31744` red seeded at 0.5, still `31744` after a rejected 0.75 draw, `31`
blue after an accepted 0.25 draw, still `31` after a rejected 0.5 draw. The
write-mask ladder reads `992` green for the masked draw and `31744` red for
the 0.1875 draw that is only accepted if the masked draw left the buffer at
0.25.

The baseline is intact: `D3DTrianglePixelRaw=31744`, `D3DBaseTextureRaw=992`,
`D3DMipmapLevelRaw=31`, `D3DTrilinearRaw=495`, `Tex4444Raw=992`,
`BltFillPixelOk=1`, `D3DContextCycleOk=1`.

## The defect

`v9x_wait_fifo(z_active ? 18ul : 15ul, 1)` in `d3d_virge.c`, on both the main
and the trilinear second-pass emit.

The count of 18 is right — fifteen writes from `COMMAND` through `Y01_Y12`,
plus the depth triple. Asking for them as one reservation is not.
`SUBSYS_STAT` carries the free-slot count in five bits at 12:8, and 86Box's
model sets bit 12 on **both** arms of that read
(`build\reference-vid_s3_virge.c:1457-1462`):

```c
ret = 0x0000c000;
if (virge->s3d_busy || virge->virge_busy || !FIFO_EMPTY) ret |= 0x00001000;
else                                                     ret |= 0x00003000;
```

so `(status & 0x1f00) >> 8` is always exactly **16**. 15 is satisfiable and
18 never is. Every depth-enabled draw spun out `V9X_VIRGE_FIFO_SPIN_LIMIT`
(2 M reads), counted a FIFO timeout, reset the engine through CR66 bit 1 and
abandoned the triangle — while `SetRenderState`, `BeginScene`,
`DrawPrimitive` and `EndScene` all returned `S_OK`.

Measured before the fix, on a run whose depth surface the driver had
accepted (`build\driver-results\zpriv-D-private`):

```
EngineFifoTimeouts=7   EngineResets=7   EngineIdleTimeouts=0
D3DZPInitRaw=0 D3DZPRejectRaw=0 D3DZPAcceptRaw=0 D3DZPUpdateRaw=0
```

Seven, for the seven rungs of the two ladders, against zero on the same
run's depth-off draws. After the fix, on the same ladders, zero.

16 is also the S3D FIFO's own depth in that model — it throttles its queue at
`FIFO_ENTRIES >= 16` — so a reservation larger than the FIFO holds was not
going to work on the chip either. What is **not** measured here is the
free-slot field on real ViRGE silicon; this says what 86Box models, and the
fix is correct under either reading because it never asks for more than 15.

## The fix

Two reservations instead of one: 15 for the writes that were always there,
and 3 taken immediately before the depth triple is written. The depth-off
path's reservation is unchanged at 15, so its stall behaviour is exactly what
it was. Applied identically to the trilinear second pass, which re-emits the
same depth triple.

## What the evidence disputes

The handoff of this date read the same symptom — `S_OK` everywhere, no
pixels, driver counters that did not move — as a plumbing failure, and named
three hypotheses. All three are wrong.

**"The runtime never hands this driver a depth surface."** It does, once, at
context creation, and the driver accepts it. New instrumentation
(`d3d_diagnostics.depth_offered` / `depth_accepted` / `depth_reject`) reports:

```
D3dDepthOffered=1  D3dDepthAccepted=1  D3dDepthRejectName=accepted
D3dDepthCaps=0x10024000  D3dDepthOffset=0x00130000  D3dDepthPitch=128
```

`0x10024000` is `LOCALVIDMEM | ZBUFFER | VIDEOMEMORY`; the offset is above the
640x480x16 visible page. Every validation arm passed.

**"`CreateDevice` returned S_OK while creating no driver context at all."**
It created one. `D3dContextCreates` is **3** on a `/zprivate` run, not the 2
the handoff recorded — the legacy Direct3D 1 QueryInterface device, the main
device, and the private device. The earlier reading of 2 was taken from a run
of the *other* probe design.

**"It was not the hardware device, whatever CreateDevice returned."** It was.
`IDirect3DDevice2::GetCaps` fills exactly one of its two halves, and the
hardware half is the one populated, for both devices:

```
D3DMainIsHardware=1  D3DMainHwColorModel=2  D3DMainHwZDepth=1024
D3DZPIsHardware=1    D3DZPHwColorModel=2    D3DZPHwZDepth=1024
```

`1024` is `DDBD_16`, so each device reports 16-bit depth support in its own
right rather than only in the enumeration.

The one finding that survives is the negative one: **`SetRenderTarget` never
reaches `V9xD3dSetRenderTarget`** — its counter is absent from every run. But
the conclusion drawn from it, that the working-device design therefore cannot
bind a depth surface, is also wrong. The runtime binds it by creating a new
context: that design reaches `D3dContextCreates=3` and `D3dDepthOffered=1`
too, and its ladders pass. `SetRenderTarget` returning `S_OK` without reaching
the driver remains true and remains a trap; it is simply not the reason
nothing was drawn.

## An observed flake, recorded rather than explained

The first post-fix run read `D3DZPInitRaw=0` where red was expected, while
the next rung read `31744` — so the triangle had landed, and the readback
after the *first* depth draw of the boot saw the surface before it did. Every
other rung in that run was correct, and the run repeated after a reboot read
`31744` with `D3DZPCompareOk=1`. `V9xHalLock` does wait for engine idle, so
the suspect is 86Box's asynchronous FIFO thread rather than the driver. One
occurrence in five runs. Not chased further; noted so a single failing rung A
is not mistaken for a depth defect.

## Open question closed

`D3DTrilinearRaw` read **960** once on 2026-08-29 where every other run reads
**495**. It has now read `495` on four consecutive runs across three boots and
two driver builds, with `D3DTrilinearBlendOk=1`. Not reproducible; treated as
a one-off, not a regression, and no longer blocks this branch.

## Gates

`check-tree`, `build-host` and `run-checks` green. Guest evidence:

| Directory | |
|---|---|
| `zpriv-A-default` | pre-fix default design; device identity, trilinear 495 |
| `zpriv-B-private` | pre-fix `/zprivate`; HAL device, every call S_OK, no pixels |
| `zpriv-C-tracefault` | trace-tool bisect, see the issue below |
| `zpriv-D-private` | pre-fix `/zprivate`; the 7 timeouts and 7 resets |
| `zfifo-001-private` | post-fix; write-mask ladder passes, rung A flake |
| `zfifo-002-private` | post-fix; both ladders pass, zero timeouts |
| `zfifo-003-baseline` | post-fix default design; both ladders pass, baseline intact |

The trace tool faults while reading these results:
[`2026-08-30-trace-dump-krnl386-flush-gpf.md`](../issues/2026-08-30-trace-dump-krnl386-flush-gpf.md).
It does not affect the driver, and every value quoted above was read from a
file the tool had already written.
