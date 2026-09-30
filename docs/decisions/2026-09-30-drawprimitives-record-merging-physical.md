# Retain DrawPrimitives record merging after physical comparison

Date: 2026-09-30. Implementation: `c52281d-dirty`. Plan:
[record merging](../plans/d3d-drawprimitives-record-merging.md). Evidence:
[physical validation](../probe/d3d-record-merge-2026-09-30/README.md).

Retain the implementation. The tested workloads completed, the broad probes
matched their controls, and the fresh-boot Half-Life repeat series was faster
than the original `5da6648-dirty` control on the same Wi-Fi setup. This is
qualified workload evidence, not complete hardware sign-off: the initial
Gateway hard lock remains unexplained. The subsequent targeted clipped-fan
check now passes. The [gameplay comparison](2026-09-30-halflife-gameplay-record-merging.md)
is complete and measures a 17.8% gain, but its reason-6 refusal-count
difference prevented a clean gameplay correctness pass for that build.
The [subsequent fix](2026-09-30-record-refusal-neighbours.md) preserves valid
records around a rejected Gen3 vertex and passes netbook validation, with
48.622 fps versus 40.659 fps. Its updated Mach64 clipped-fan retest passes on boot 71; the broad probe hard-locked on boot 70 and remains unresolved.

## Half-Life comparison

GMA950 netbook, Windows 98 SE, 640x480 Direct3D, Half-Life 1.1.1.0,
`hl.exe -game valve -d3d -console +map c1a1`, spawn position reported by
`status` as `(984, 654, -79)`, `notarget`. Both HALs were hash-verified after
separate fresh boots. Boot 61 is the candidate; boot 62 is the preserved
control, SHA256 `9AEE1EC1874469FE4FFF99CCFDB6C46FF14B787CEF9BB2B2A03D64A436897CF3`.
Four spins were issued with 2.5-second delays, without trace tools or screen
captures during the spins. Console captures were taken afterwards.

| Repeat series | Spin 1 | Spin 2 | Spin 3 | Spin 4 | Mean fps |
|---|---:|---:|---:|---:|---:|
| Candidate, boot 61 | 100.186 | 101.429 | 99.488 | 95.592 | 99.174 |
| Control, boot 62 | 87.434 | 89.149 | 84.860 | 86.970 | 87.103 |

The measured repeat-series mean is 13.9% higher, outside the spread of
these two four-spin series. The earlier control series was
88.336, 86.516, 87.134, 90.244 fps. Candidate warmup series were
101.091, 100.717, 99.320, **70.691** fps, followed by
101.191, 100.722, 101.550, 99.107 fps. The 70.691-fps outlier is retained;
its cause is unknown and the full set of observations has overlapping
ranges. Do not generalize the repeat-series gain to gameplay or all boots.
The older wired boot-58 result of 104.0–104.1 fps is not the comparison here.

Snapshot windows include console frames between spins. Wi-Fi interruptions
extended the control window, so raw totals are reported with their own
durations. They are not equal-duration measures or spin-only totals.
Half-Life presents by blitting and supplied no flip-clock calibration;
both reports use the same netbook's 3DMark calibration, 1,662,249 cycles/ms.

| Counter or time | Candidate (13.565 s) | Control (21.932 s) |
|---|---:|---:|
| DrawPrimitives records | 209,969 | 287,606 |
| No-op joined runs | 49,996 | 68,251 |
| List calls | 59,961 | 300,563 |
| Sink batches | 59,961 | 298,877 |
| Presentation-copy calls | 386 | 580 |
| Sink batches / presentation-copy call | 155.34 | 515.31 |
| D3D calls time | 9,682 ms | 16,632 ms |
| Engine draw time | 9,141 ms | 15,365 ms |
| Ring write time | 709 ms | 1,606 ms |
| Ring head wait time | 7,876 ms | 11,262 ms |

The frame-normalized row uses presentation blits as the observable proxy;
the timing spins also render views without presenting each one. It therefore
does not count batches per individual timerefresh view. No scene/frame
counter was added to make that stronger measurement. Candidate list calls
are the joined-run count plus 9,471 indexed calls and 494 other calls. This
confirms that the accumulation is active. Both Half-Life runs had zero engine
refusals, timeouts, resets and abandoned breadcrumbs. The spawn screenshot
shows textured geometry. Flip-triggered driver capture produced no new
image in Half-Life; its old PPM was stale and is not spawn evidence.

## Correctness limits

The netbook broad probe matches its historical control on every final check
(197 passing, 19 existing failures). 3DMark completed at both 1024x576 and
640x480, with scores 651 and 852 respectively, no timeouts or resets, and
the existing reason-6 vertex-builder refusals. These scores are completion
evidence, not a benchmark comparison.

On the Gateway, the first candidate probe hard-locked. A same-tree HAL with
record merging removed subsequently completed the broad probe on boot 65.
A clean candidate installation completed on boot 66 and matched every final
check (202 passing, 14 existing failures), including 38 existing policy
refusals. Its 640x480 3DMark run completed at 589 3DMarks / 7065 CPU 3DMarks,
without adding refusals, timeouts or resets. The successful control and
candidate runs shared the short DOS staging path, installed-file verification
and DirectDraw warmup. That does not establish the cause of the first lock.

Mach64 is a clip-in-core engine, but the benchmark's list-clipping counter
was zero. A subsequent [targeted physical probe](2026-09-30-mach64-clipped-fan-ordering.md)
clipped three input triangles per case and compared every pixel against
separate original-order calls. It passed ordinary, redundant-state and
capacity-boundary cases on the candidate and control, with no new failures.
This closes the outstanding clipped-fan ordering gate.

Gateway follow-up: three broad reruns completed on boots 71-73, including
two fresh boots and the unmodified probe. All checks match baseline (202/14),
with zero timeouts/resets. The boot-70 intermittent lock remains open; see
[retained investigation](../probe/d3d-record-merge-2026-09-30/gateway-refusal-fix/README.md).
