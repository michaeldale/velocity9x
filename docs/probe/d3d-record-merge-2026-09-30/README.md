# DrawPrimitives record merging: physical validation

Measured on 2026-09-30. Candidate is `c52281d-dirty` with record accumulation;
host gates and all five family package builds pass. See the qualified
[retain decision](../../decisions/2026-09-30-drawprimitives-record-merging-physical.md).

The GMA950 netbook is on Wi-Fi at `10.0.1.254`, boot 60. Its broad probe
completed with 197 passing and 19 failing final checks, matching the
historical control on every check. The failures are existing unsupported
cases. Engine timeouts and resets remained zero. 3DMark 99 Max completed
at 1024x576, 16-bit colour and depth, triple buffering: 651 3DMarks and
14807 CPU 3DMarks. This is a completion result, not a performance comparison.
There were 333 existing vertex-builder refusals (reason 6), with no timeout
or reset. The sampled image is low resolution and does not establish full
scene correctness. The 640x480 run completed with 852 3DMarks and 14762 CPU
3DMarks, adding 262 reason-6 refusals (595 cumulative), with zero timeouts
or resets. A driver-sampled scene image is retained. Historical boot 54
also had reason-6 refusals in both resolutions; workload duration affects
their totals. The candidate subsequently rebooted successfully to boot 61
for fresh-boot Half-Life validation.

The Gateway Mach64 is at `10.0.1.22`. The first candidate probe on boot 63
hard-locked and required an operator reset. Its ordinary retained keys end
near the exclusive-mode/vblank tests, but cached writes and a corrupt tail
prevent locating the precise failing call. The second probe file was stale;
it is explicitly named `gateway-stale-V9XDD2.INI` and is not failed-run evidence.
The cause of the lock remains unresolved.

A same-tree control with record merging removed was installed through a
verified short DOS staging path. Its boot-65 probe completed. The candidate
was then installed through the same staging path, verified after boot 66,
and run with the same DirectDraw warmup and probe procedure. Every final
check matched the control (202 passing, 14 failing). Both had 38 known
Mach64 refusals, reason 3, and zero new engine timeouts or resets. The
640x480 3DMark run completed with 589 3DMarks and 7065 CPU 3DMarks. It added
no refusals, timeouts or resets. It submitted 7.3 million triangles but
reported zero clipped list triangles, so a subsequent targeted probe was
needed to close the clipped-fan ordering check. These successful runs do not
explain the initial hard lock.

The initial netbook WININIT used a long source directory unsupported by DOS
rename processing. It removed the old HAL without installing the candidate.
The missing file was repaired directly and its hash verified before graphics
validation. Future staging uses `C:\V9XNHAL.BIN`. Gateway reboot proof is now
allowed 180 seconds; an earlier 60-second deadline was too short.

Fresh-boot Half-Life was measured on candidate boot 61 and control boot 62,
both on Wi-Fi, with identical map, spawn and graphics settings. The final
four-spin series averaged 99.174 fps and 87.103 fps respectively (13.9%
higher on the candidate). Earlier series, including a 70.691-fps candidate
outlier, are retained. Snapshot windows include console rendering and had
different durations due to Wi-Fi delays. Both had zero engine refusals,
timeouts, resets and abandoned breadcrumbs. The timing reporter accepts an
independent same-machine calibration and uses the actual `RingWait` and
`RenderDrain` bucket names; the retained 3DMark snapshot supplies the clock
calibration absent from Half-Life's blit presentation path.

The full spawn screenshot is the Half-Life image evidence. The attempted
flip-triggered driver capture produced no new frame; its old PPM was stale
and is excluded. The implementation is retained with the limitations in the
decision. The targeted physical clipped-fan check now passes; see the
[probe decision](../../decisions/2026-09-30-mach64-clipped-fan-ordering.md) and
the `clipped-fans/` reports and readbacks. The subsequent scripted gameplay
demo comparison is retained in `gameplay/`; see the
[gameplay decision](../../decisions/2026-09-30-halflife-gameplay-record-merging.md).
Four runs average 48.823 fps on the candidate and 41.436 fps on control.
Three sampled frames are byte-identical; the fourth differs only around
animated sparks. Refusal counts remain a qualification issue, documented
with the original logs and diagnostic windows. The netbook returned to the
verified candidate on boot 66 and its original game configuration was restored.
The Gateway also has the candidate installed.

The later [vertex-refusal investigation](../../decisions/2026-09-30-record-refusal-neighbours.md)
is retained in `vertex-refusal/`. It captures a tiny negative Half-Life Z on
the no-merge control and proves that an invalid record could discard valid
neighbours in a merged batch. The fixed driver passes all three physical
refusal cases with exact pixel matches and matches all broad netbook checks.
Its four gameplay repeats average 48.622 fps versus 40.659 fps and add no
refusals, timeouts or resets. The fix remains installed on netbook boot 70;
the original game configuration is restored. The Gateway fixed HAL passes the clipped-fan retest on boot 71. Its broad probe hard-locked on boot 70; see [follow-up evidence](gateway-refusal-fix/README.md).

Continued [Gateway investigation](gateway-refusal-fix/README.md) completed
three broad reruns on boots 71-73, including two fresh boots and the original
probe. All match baseline (202 passing / 14 existing failures), with zero
timeouts/resets. The confirmed boot-70 lock remains an open intermittent incident.
