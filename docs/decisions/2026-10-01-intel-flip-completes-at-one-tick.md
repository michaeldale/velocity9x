# An Intel flip now completes at the first frame tick: Final Reality's 3D score from 2.19 to 2.71

Date: 2026-10-01
Machine: MICHAEL-NETBOOK, 945GSE / GMA 950, 1024x576x16 desktop, FR at
640x480, two-buffer flip chain.
Evidence: [`../probe/netbook-flip-ticks-2026-10-01/`](../probe/netbook-flip-ticks-2026-10-01/)
(`delta.py` reads any pre/post pair).

## Why

[The fill-rate record](2026-10-01-netbook-fill-rate-is-presentation-bound.md)
found Final Reality's Fill rate paced by an untimed wait in the draw path.
That wait is `v9x_flip_wait_done`: before a batch, the Gen3 engine waits
for a pending flip to complete, and an Intel flip was called complete only
at the **second** frame tick after the plane-base write
(`V9X_I9XX_FLIP_TICKS_TO_COMPLETE`, `73709d2`, intel79). That second tick
was added as a flicker mitigation. The flicker it was for is still open
([issue](../issues/2026-09-18-final-reality-flicker-is-the-buffer-under-construction.md)):
intel86 measured no draw waiting on a flip while the flicker continued,
and intel90 found the pipe underrunning. With two buffers, every frame's
first draw arrives while its flip is pending, so the second tick cost a
frame on every frame.

## Instrument (ABI 2026100101, an append)

`FlipWaitCycles`/`FlipWaitCalls`: cycles a draw spent in
`v9x_flip_wait_done` with a flip pending. `FlipIntervalCycles`/`Count`/
`Max`: the TSC interval between accepted flips, i.e. the frame period,
ignoring gaps over 2^31 cycles (about 1.3 s). Gen3 only, like the existing
time buckets.

## A/B, Final Reality Fill rate alone, one pass

Package A (instrument, two ticks) on boot 79; package B (the same, one
tick) on boot 80. Every installed file hash-verified after the WININIT
rename. Same procedure, snapshots either side.

| | A: 2 ticks | B: 1 tick |
|---|---|---|
| Fill rate | 8.66 Mpixels/s | **16.14 Mpixels/s** |
| Frames (`FlipHandled`) | 143 | 261 |
| Frame interval | 35.10 ms | **19.02 ms** |
| Draw flip wait, per frame | 28.49 ms (142 waits) | **12.74 ms** (260 waits) |
| `TimeEngineDraw` | 4.55 s | 4.18 s |
| Ring-space waits | 0.56 s | 0.98 s |
| Refusals, engine timeouts, resets, flip-wait timeouts | 0 | 0 |

On A the flip wait is 4.05 s of the 4.55 s inside the engine draw, which
measures what the earlier record inferred. The earlier record's "about 14
frames a second" was wrong: the measured frame period at two ticks is 35 ms.
At one tick a frame is 19 ms against a 16.7 ms refresh, so the test is now
close to vsync-paced, which a two-buffer chain with vsync cannot beat.

## Full default run, package B (boot 80)

All eight tests, five repeats, 10:13:55 to between 10:30 and 10:32
(this morning's two-tick run took up to 19 minutes). Against this morning's run on
`d78d4f4` (two ticks), same machine and procedure:

| Test | 2 ticks (`d78d4f4`, 09:06) | 1 tick (B) |
|---|---|---|
| 25 pixel | 158.34 Kpolys/s | 208.65 Kpolys/s |
| Robots | 18.63 images/s | 28.02 images/s |
| Fill rate | 8.18 Mpixels/s | 16.68 Mpixels/s |
| City scene | 17.85 images/s | 27.95 images/s |
| Visual appearance | 85.19 % | 85.19 % |
| **3D performance** | **2.19** | **2.71** |
| 2D image processing | 19.44 | 19.88 |
| Bus transfer rate | 7.53 | 7.58 |
| **Overall** | **8.17** | **8.59** |

The comparison is the point of this record: one constant changed between
them. Over the run, 7,673,881 Gen3 batches, none refused; no FIFO, idle,
flip-wait or ring-space timeouts and no resets.

## Not established

- **Whether the panel flickers more, less or the same.** Nobody watched
  either run. The flicker is judged by eye or camera only, and the second
  tick never measurably fixed it, but this change has not been looked at.
- **That one tick is never early.** The argument is the latch model: the
  write lands in active video short of the blank-start latch (eight-line
  guard, issue delta measured at one line in intel90) and the counter ticks
  at line 671 of the same frame. A write that lands after the latch would
  make one tick a frame early. Nothing here observed the panel.
- **The Gateway and other families.** The constant is Intel-only. The
  Gateway still runs ABI 2026093003 and was not updated.
