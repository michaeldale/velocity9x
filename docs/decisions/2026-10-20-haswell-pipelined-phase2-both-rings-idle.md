# HD 4600: the blitter's idle test covers the render ring; V9XDDP and 3DMark 2000 read as before

Date: 2026-10-20. Machine: the i5-4570, boots 37 and 38, build
`45f42a9c-dirty`, installed by WININIT rename. Phase 2 of
`docs/plans/intel-haswell-pipelined-submission.md`. Evidence:
[`../probe/hsw-4570-pipelined-phase2-2026-10-20/`](../probe/hsw-4570-pipelined-phase2-2026-10-20/).

## The change

`eng_hsw.c`'s `v9x_hsw_idle_now` now requires both status pairs to
agree: the blitter's (dwords 0 and 1) and the render ring's (8 and 9,
which `d3d_hsw.c` keeps). Every caller of the engine's `wait_idle` now
waits for draws as well as blits: Flip, Lock, Blt, `GetBltStatus` and
`v9x_render_drain`. A timeout traces the head of whichever ring is
still busy. Submission is still synchronous, so the render pair always
agrees by the time anything else runs. The phase predicts no
observable change.

## V9XDDP

Run with no switches (`C:\V9XDDP.EXE`, the probe from the same
package). The control is the Phase 0 build `a94cb112-dirty` on boot 36,
run after 3DMark 2000 had been closed (`control-a94cb112/`).

| run | build, boot | before it | `Result` | `Ok=1` | `Ok=0` |
|---|---|---|---|---|---|
| control | a94cb112, 36 | 3DMark 2000 | COMPLETE | 207 | 17 |
| 1 | 45f42a9c, 37 | fresh boot | COMPLETE | 207 | 17 |
| 2, 3 | 45f42a9c, 37 | V9XDDP | COMPLETE | 207 | 17 |
| 4 | 45f42a9c, 38 | 3DMark 2000 | COMPLETE | 207 | 17 |

The 17 `Ok=0` keys are the same set in every run. They include the
`D3DZ*`/`Mixed*` start-up keys this machine's notes already explain,
and they are not new.

Runs 1-3 differed from the control in two diagnostic keys:
`ChainFrontRaw` and `SoloFrontRaw` read 65535, not 0. Both read pixel
(16,12) of the primary to say where a misdirected draw went. V9XDDP
never writes the primary, so they read what video memory held before
the probe set its mode. Run 4 repeated the control's order (3DMark,
then the probe) on the new build and read 0, and its keys match the
control's except timings (`BackFillMs` 9 to 10 and `PrimaryFillMs` 9
to 15, one sample each, not looked into). The 65535 is the previous
occupant of that memory, not this change.

## 3DMark 2000

Default run on boot 38 with a clean set of counters. Score 1991,
recorded and not compared.

| | Phase 0, boot 36 | Phase 2, boot 38 |
|---|---|---|
| batches | 1,534,574 | 1,532,479 |
| pre-write waits that stalled | 0 | 0 |
| engine timeouts / resets / refused batches | 0 / 0 / 0 | 0 / 0 / 0 |
| engine draw per batch | 157.0 µs | 157.4 µs |
| render waits | 37.76 µs each | 37.77 µs each |
| flips handled / declined | 8,181 / 0 | 8,211 / 0 |
| Flip calls, per call | 15.0M, 0.95 µs | 11.7M, 1.22 µs |
| Lock calls, per call | 45,239, 0.42 µs | 45,409, 0.68 µs |
| fills | 11,968, 590 µs | 11,959, 598 µs |

Flip and Lock cost about a quarter of a microsecond more per call: the
idle test now makes one more uncached status read. Nothing else moved.

## Not covered

- No call site was seen waiting on a busy render ring, because none
  can be busy yet. That every site reaches the render check is shown
  by reading the code (all of them go through `v9x_hsw_wait_idle`),
  not by a measurement. Phase 3 is the first build where it is
  exercised.
- Glide, 3DCIF and OpenGL were not run on this build. They reach the
  same idle test and the same synchronous draw.
