# Half-Life gameplay comparison: faster, refusal qualification open

Date: 2026-09-30. Evidence: [demo, logs, snapshots and provenance](../probe/d3d-record-merge-2026-09-30/gameplay/).

Follow-up: the [refusal investigation](2026-09-30-record-refusal-neighbours.md)
captures invalid depth on control and fixes a proven loss of valid neighbouring
records. The fixed build passes netbook validation and retains its speed gain.
Results below describe the preceding build; the updated Mach64 clipped-fan retest passes on boot 71; the broad probe hard-locked on boot 70 and remains unresolved.

The recorded `v9xbench` route completes on both builds. The candidate averages
48.823 fps against 41.436 fps for the original `5da6648-dirty` control, a
17.8% gain outside the repeat spread. This completes the owed demo comparison,
but does not establish a clean correctness pass: the candidate added more
reason-6 vertex-builder refusals in the collected intervals.

## Procedure and performance

GMA950 netbook, Windows 98 SE, Half-Life 1.1.1.0, 640x480 Direct3D, Wi-Fi.
Candidate boot 64 and control boot 65 each used a verified installed HAL,
DirectDraw warmup, fresh game launch and one excluded startup demo warmup.
Four sequential `timedemo v9xbench` runs each completed 1,530 frames.
No screenshots were taken during these measured runs.

| Run | Candidate fps | Control fps |
|---|---:|---:|
| 1 | 48.773 | 41.478 |
| 2 | 48.803 | 41.406 |
| 3 | 48.799 | 41.444 |
| 4 | 48.918 | 41.415 |
| Mean | 48.823 | 41.436 |

The demo was recorded with chained frame-wait scripts on `c1a1`: forward,
turning, strafing and backward movement, with `notarget`. It contains 1,526
recorded playback frames over 33.123 seconds. This is a repeatable navigation
route, not combat validation or evidence for every gameplay workload.
The demo and both recording and capture scripts are retained with SHA256
provenance. Neither build required an operator reset in this comparison;
each driver-swap reboot was allowed 180 seconds.

## Rendering and diagnostics

A separate timedemo used engine `snapshot` commands after cumulative waits
of 180, 540, 1,020 and 1,440 frames. The first three candidate/control BMPs
are byte-identical. The fourth differs at 84 pixels, confined to the visible
animated sparks; inspection found matching scene geometry and textures.
This is four sampled views, not an exhaustive frame-by-frame comparison.

The four-run counter windows include console rendering and delays between
replays: 397.033 seconds for candidate and 303.952 seconds for control.
They added seven and five engine refusals respectively, all reason 6
(runtime vertex-builder rejection). Their timing reports therefore cannot
be treated as demo-only totals or divided by 6,120 frames. Both use the
same netbook's retained 1,662,249 cycles/ms calibration because Half-Life
presents by blitting and supplies no flip calibration.

One additional bounded replay on control boot 65 added zero refusals.
After restoring the candidate on fresh boot 66, its excluded warmup completed
at 46.096 fps and the additional replay completed at 48.814 fps, adding one
reason-6 refusal. The diagnostic windows still include console time
(76.200 and 90.689 seconds respectively), so they do not locate the rejected
draw inside the replay. No collected window added timeouts, resets or
abandoned breadcrumbs.

The refusal category demonstrably occurs on the control, but its frequency
and the geometry discarded by a merged refusal remain unproven. The plan's
stop-on-new-refusal requirement prevents calling this a clean correctness
pass. The next investigation should isolate the rejected vertex and draw
record on both builds, including whether merging discards otherwise valid
neighbouring records. The earlier Gateway hard lock also remains unexplained.

The candidate was restored and its installed SHA256 verified on netbook boot
66. Half-Life was closed and its original `config.cfg` restored with verified
readback. No driver implementation changed during this comparison.

Gateway follow-up: three broad reruns completed on boots 71-73, including
two fresh boots and the unmodified probe. All checks match baseline (202/14),
with zero timeouts/resets. The boot-70 intermittent lock remains open; see
[retained investigation](../probe/d3d-record-merge-2026-09-30/gateway-refusal-fix/README.md).
