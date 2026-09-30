# Preserve valid records around a Gen3 vertex rejection

Date: 2026-09-30. Base revision: `b182f94`. Evidence:
[captures, pixel readbacks, logs and provenance](../probe/d3d-record-merge-2026-09-30/vertex-refusal/).

The reason-6 investigation found a real record-merging regression. An invalid
vertex could reject an entire merged run and discard valid neighbouring
records. The fix preserves original record boundaries and replays them only
after a Gen3 vertex-builder rejection that submitted nothing. The physical
regression test now matches the no-merge control pixel for pixel. The fixed
netbook gameplay series averages 48.622 fps against 40.659 fps for control,
19.6% faster on this navigation route.

## Rejected input and source

Failure-only instrumentation captured vertex bit patterns, failed predicates,
callback, target, triangle count and valid triangles rejected with the batch.
It temporarily overlaid Gen3's unused S3 census storage; the shared layout and
ABI did not change. Its patch is retained, and it was removed from shipping
sources before the final build.

On control boot 69, Half-Life added five reason-6 refusals during the four-run
counter interval. The last was callback 37, `DrawPrimitives`, at cumulative
record count 5,033,866: one triangle, vertex 2, with Z bits `0xB280002C`
(-0.000000014901239). Only the Z predicate failed. Its target was 640x480,
and the captured record had no valid triangles. This proves the invalid
input also occurs without record merging. Earlier instrumented candidate
replays did not reproduce it; that absence did not establish correctness.

The known 3DMark reproducer independently captured callback 38, the indexed
path, rejecting Z `0x3F800001` (1.000000119), one float step beyond the far
plane. It is separate workload evidence, not the Half-Life failure. The
benchmark was stopped normally after capture; its displayed score is not
used as a completed performance comparison.

## Deterministic regression and repair

`V9XRCLP.EXE -refusal` sends the published DX5 callback four ordered records
on a 64x64 video-memory target, with ordinary, redundant-state and capacity
cases. Valid red, blue-fan and yellow records surround a record containing
Z `0x3F800001`. The final fixture also includes a valid cyan triangle inside
the bad record: it must remain refused with that record, rather than becoming
visible through triangle-wise replay.

The initial candidate fixture failed with 1,841 mismatched pixels in each
ordinary case and 87 at the capacity boundary. Its diagnostic capacity
capture showed 63 triangles refused together, 62 individually valid. The
explicit no-merge control and the final fix both pass every case, and all
twelve retained reference/run PPMs are byte-identical. The final fixture has
two triangles in the bad record, so its capacity case contains 64 triangles
before the next record starts.

The accumulator stores at most 64 DWORD record ends and their count. Sink
success is positive; zero retains the existing refusal behavior; negative
requests replay only with a guarantee of no submission. Replayed records
are called directly and cannot trigger recursive replay. The core requests
replay only for Gen3 reason 6, when the engine refusal counter changed and
the submitted-draw counter did not. A submission failure, timeout, another
engine's refusal or an accepted prefix never requests it. State-change
flushes still run under the saved previous context.

The bad record remains rejected. Counters include every failed attempt:
the deliberate fixture adds six refusals on control and nine on the fix,
including its failed merged attempts. That increase is expected attempt
accounting, not additional discarded valid geometry. Neither fixture added
timeouts or resets. No vertex bounds were relaxed and no malformed input
was silently accepted.

## Validation and performance

The new host regression failed before implementation and passes afterward.
It verifies record order, whole-record refusal, nonrecursive replay and no
replay of an ordinary failure that might have emitted a prefix. Existing
capacity, fan and oversized-fan refusal tests also pass. `check-tree.ps1`,
`build-host.ps1` and complete `run-checks.ps1` passed, including all five
family packages; the final additional ordinary-failure host case also passed.
Watcom allocates `1ad4h` bytes for the DrawPrimitives body and `1848h` for
the list wrapper: roughly 12.8 KiB together before deeper callees.

The fixed broad netbook probe matches every final boolean result of the
earlier candidate: 197 passing, 19 existing failures. Its final driver SHA256
is `622DD5424FFC923450D06A84B36D2F7044758EA5BE8E63A18C4E93DDB9C89EA8`,
verified after installation on boot 70.

| Replay | Fixed fps | No-merge control fps |
|---|---:|---:|
| 1 | 48.509 | 40.551 |
| 2 | 48.736 | 40.921 |
| 3 | 48.623 | 40.337 |
| 4 | 48.618 | 40.825 |
| Mean | 48.622 | 40.659 |

The control uses the current tree with the explicit pre-merge `c52281d`
core and the temporary capture; the fixed build contains no temporary
capture. Both use the retained `v9xbench` demo, fresh boots, a verified HAL
and an excluded startup warmup. Each repeat completes 1,530 frames. The
fixed measured interval adds zero engine refusals, timeouts, resets or
abandoned breadcrumbs. Its initial nine refusals came from the deliberate
fixture before gameplay, and stay at nine.

Three separately captured gameplay images are byte-identical to control;
the fourth differs at 126 pixels confined to the animated sparks. Geometry
and textures match in these sampled views. This remains navigation-route
evidence, not exhaustive combat or frame-by-frame validation.

Timing snapshots include console rendering between replays: 374.833 seconds
on control and 456.316 seconds on the fix. Their retained reports use the
same netbook's 1,662,249 cycles/ms calibration; they are not demo-only totals
and must not be divided by the 6,120 timedemo frames.

## Remaining qualification

The demonstrated loss of valid neighbouring records is fixed and physically
verified on the netbook. The underlying out-of-range application vertices
remain refused, as they were on control. The updated shared accumulator passes the Mach64 clipped-fan retest on boot 71. The broad probe hard-locked on boot 70; both that failure and the original Gateway lock remain unexplained.

The final fix remains installed on netbook boot 70. Half-Life was closed and
the original game configuration restored with verified readback. No commits
were created during this investigation.

## Gateway follow-up

The fixed HAL was installed and hash-verified on Gateway boot 70. Windows
then hard-locked during the detached broad probe; the user confirmed the
freeze and began a hard reset. The targeted clipped-fan probe had not run.
The failed-run evidence is retained in
[Gateway follow-up](../probe/d3d-record-merge-2026-09-30/gateway-refusal-fix/README.md).
This does not establish a cause for either Gateway hard lock.


The corrected targeted probe passed on Gateway boot 71 with the final
fixed HAL: zero pixel differences in all three cases, unchanged expected
call/batch counts, and zero new refusals, timeouts or resets. All six PPMs
are identical to the earlier no-merge control. The fix remains installed.
The broad-probe hard lock on boot 70 remains an unresolved qualification;
this targeted pass does not establish an overall broad-probe pass.

Investigation continued after that initial stop. The isolated mode switch
and restore passed, and three broad reruns completed on boots 71-73 (two
fresh boots), including the unmodified probe on boot 73. Every run matches
all baseline boolean checks: 202 passing, 14 existing failures, 38 known
reason-3 policy refusals and zero timeouts/resets. Durable mode checkpoints
and /modeonly are retained in the repository probe for recurrence diagnosis.
The confirmed boot-70 lock remains an open intermittent incident; successful
reruns do not establish its cause or constitute a lock fix.

The later controlled investigation completed five original-probe runs on
the no-merge control (boot 74), five on the fixed HAL (boot 75), and two
32-cycle isolated mode-transition tests (boots 75/76). No lock reproduced,
no baseline check changed, and no timeout/reset occurred. These tests
remain negative reproduction evidence, not a lock fix. The incident stays
[open](../issues/2026-09-30-gateway-intermittent-black-screen-probe-lock.md).
