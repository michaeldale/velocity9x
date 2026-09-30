# Gateway intermittently hard-locks with a black screen during the broad probe

Status: open. Date: 2026-09-30. Host: Gateway SOLO2150 / ATI Rage Mobility-M,
Windows 98, remote agent at `10.0.1.22`.

The original DrawPrimitives merging candidate hard-locked on boot 63. The
final record-refusal fix also hard-locked on boot 70. The user confirmed
Windows was frozen and later identified the screen as black. Each required
a manual hard reset. Neither successful later tests nor a targeted pixel
pass establishes a fix for this incident.

The boot-70 report ends after `ExclusiveVBlankHr`; subsequent report data
was cached or damaged. It does not locate the failing call. A mode switch
is a candidate because the next request is 640x480x16, but that is an
inference, not a demonstrated cause. No useful post-lock counter snapshot
could be obtained. Do not infer zero timeouts from the pre-lock snapshot.

## Investigation

The same fixed HAL completed three broad reruns on boots 71-73, including
two fresh boots and the original unmodified probe. All matched the
successful historical baseline: 202 passing and 14 existing failing final
boolean checks, 38 known Mach64 reason-3 policy refusals per run, and zero
FIFO timeouts, idle timeouts or resets. An isolated switch/restore also passed.

A matched repeat sequence installed the same-tree no-record-merge control
on boot 74 and the fixed HAL on boot 75. Each was readback hash-verified.
Five original-probe runs completed on each HAL, every check matching the
baseline. Each series added 190 known reason-3 policy refusals and no
timeouts or resets. Control run three was still incomplete at its initial
15-second read, then completed without intervention; it was not a lock.
Later waits allowed 30 seconds and no overlapping probe processes.

On boot 75 the diagnostic mode stress test completed 32 switch/restore
round trips without submitting geometry and without adding contexts,
refusals, timeouts or resets. A fresh-boot counterpart on boot 76 is recorded
in the retained evidence. None of these successful tests determines whether
the intermittent lock depends on record merging.

## Capture for recurrence

The repository broad probe now flushes `Result` boundaries before and after
`SetDisplayMode`. `/modeonly` isolates one switch/restore; `/modestress`
isolates 32 round trips and records `ModeCycle` plus switch/restore boundaries.
They stop before geometry submission. Profile flushes change timing, so a
diagnostic pass must not be presented as proof the original probe is fixed.
The unmodified probe remains separately available on the host.

On a recurrence, stop launching tests. Allow time for completion, distinguish
a responsive agent from a frozen host, retain all report files and the last
durable boundary, then recover after the necessary manual reset. Instrument
the implicated driver path only once the durable boundary identifies it.
The old cached report tail alone does not justify a speculative driver patch.

[Original failures, readbacks, comparisons and stress evidence](../probe/d3d-record-merge-2026-09-30/gateway-refusal-fix/README.md)

## Fresh-boot transition result

Boot 76 completed all 32 isolated transition round trips before any
rendering probe. The final trace has zero contexts, records, triangles,
refusals, timeouts or resets. Across boots 75 and 76 this is 64 completed
mode cycles, in addition to the matched five-run control and fixed broad
series. The confirmed black-screen lock remains unreproduced and open;
no lock repair is claimed. The final fixed HAL remains installed on the
responsive host. The original broad probe and diagnostic transition probe
are separately staged for further controlled testing.
