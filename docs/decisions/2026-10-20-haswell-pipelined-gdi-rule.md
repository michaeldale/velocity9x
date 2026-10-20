# HD 4600: draws wait only while GDI may own the screen; 3DMark 2000 112 µs a batch, Half-Life mwd5 60.5 to 79.3 fps

Date: 2026-10-20. Machine: the i5-4570, boots 42 to 44, build
`a3825fa3-dirty`. It narrows decision 3 of
`docs/plans/intel-haswell-pipelined-submission.md`, which Michael asked
for after `2026-10-20-haswell-pipelined-phase3-deferred-wait.md`.
Evidence:
[`../probe/hsw-4570-pipelined-gdi-rule-2026-10-20/`](../probe/hsw-4570-pipelined-gdi-rule-2026-10-20/).

## The change

`ddhal_core.c` tracks whether GDI may be drawing on the screen:
- `V9xHalSetExclusiveMode` sets "exclusive" on entry and clears it on
  leave. The leave sets it only after the existing drain succeeds.
- `V9xHalFlipToGDISurface` marks the GDI surface shown, after its drain.
- An accepted Flip clears that mark.

`v9x_present_gdi_may_draw()` is true unless exclusive is set and the
GDI surface is not shown. `d3d_hsw.c` waits after a draw only while it
is true; the target offset no longer matters. The depth fill and the
init batch stay synchronous, as before.

## A correction to the Phase 3 record

The Phase 3 record says Half-Life went synchronous because it flipped
23 times while starting. That is wrong:
- Boot 44's Half-Life-only session shows `FlipHandled=0`.
- Boot 39's 23 flips were already counted after V9XDDP, before
  Half-Life started.

Every section of V9XHAL.DLL is marked shared
(`scripts/build-ddraw-hal-dll.ps1`, `IMAGE_SCN_MEM_SHARED`), so
`ddhal_core.c`'s statics are one instance for every process.
`v9x_present_offset` still held `0x96000`, the buffer V9XDDP last
flipped to at 640x480x16. Half-Life's render target is at the same
offset, so its draws matched "the last flip's buffer"
(`DrawsIntoPresented` 1,071,292). The new flags are shared the same
way. That fits what they track, since only one process holds exclusive
mode at a time. A process that dies holding it would leave the flag set
until the next `SetExclusiveMode`; that case was not tested.

## V9XDDP, boot 42

COMPLETE. 207 `Ok=1`, and the same 17 `Ok=0` keys as every run since
Phase 2 (identical set).

## 3DMark 2000, boot 43

Default benchmark, 1024x768x16 triple-buffered, clean boot. Every
captured test is drawn whole (`3dm2000-run-frames.png`).

| | before pipelining (Phase 2, boot 38) | offset rule (boot 40) | GDI rule (boot 43) |
|---|---|---|---|
| score (recorded) | 1991 | 2297 | **2438** |
| batches | 1,532,479 | 1,712,897 | 1,819,368 |
| engine draw per batch | 157.4 µs | 126.9 µs | **112.3 µs** |
| async / sync submits | 0 / all | 1,141,635 / 579,360 | 1,819,368 / 8,442 |
| slot waits that stalled | 0 | 9.4% | 14.1% |
| vertex copy per batch | 60.3 µs | 60.3 µs | 60.1 µs |
| timeouts, resets, refused batches | 0, 0, 0 | 0, 0, 0 | 0, 0, 0 |

Every draw batch now goes out without waiting. The 8,442 synchronous
submits are depth fills. A slot is now found busy 14% of the time, so
for those batches the GPU, not the CPU, is the slower half. What
remains of the 112 µs is mostly the CPU: the 60 µs uncached vertex copy
and the batch build.

| Test | boot 38 | boot 43 |
|---|---|---|
| Game 1 low / medium / high | 34.9 / 23.8 / 8.4 (boot 34) | 45.2 / 31.2 / 11.0 |
| Game 2 low / medium / high | 45.1 / 31.2 / 21.9 (boot 34) | 52.0 / 37.4 / 26.3 |
| High polygon, 1 / 4 / 8 lights | 906 / 967 / 961 (boot 34) | 1150 / 1049 / 1123 |
| Fill rate single / multi | 189.1 / 189.0 (boot 34) | 189.1 / 183.3 |

The per-test column for the synchronous build is the boot 34 baseline
(score 1982), the one whose details were read.

## Half-Life mwd5, boot 44

`hl.exe -console -d3d -w 640 -h 480 -full -condebug`, then
`timedemo mwd5` three times in one session, on a clean boot.

| run | frames | seconds | fps |
|---|---|---|---|
| 1 | 393 | 5.869 | 66.958 |
| 2 | 393 | 5.177 | 75.914 |
| 3 | 393 | 4.956 | **79.293** |

The best of three was 60.522 on boot 39 (every draw synchronous), and
52.447 on boot 18 (`2026-10-18-haswell-half-life-d3d-mwd5.md`).

- 702,437 batches: 702,437 async, 1,165 sync (depth fills).
- 0 refused batches, 0 timeouts or resets.
- Engine draw per batch: 43.0 µs, against 60.2 µs on boot 39.
- No flips (`FlipHandled=0`): Half-Life presents by blitting
  (5,372 copies).
- The mid-run frame is whole (`halflife-mwd5-mid-run.png`).

## Not covered

- A windowed Direct3D application (GDI owns the screen, so its draws
  would be synchronous), and FlipToGDISurface during a running game.
- A process that dies in exclusive mode.
- The engine close at Disable and concurrent Direct3D processes, still
  open from Phase 3.
- No one watched the monitor; the captures are of offset 0.
