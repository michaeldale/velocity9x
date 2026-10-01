# Intel asynchronous submission's corruption was a busy ring's dword head: fixed, and on by default

Date: 2026-10-01
Machine: MICHAEL-NETBOOK, 945GSE / GMA 950 `8086:27AE` rev 03, Atom N280,
wifi (10.0.1.254), boots 84-86, full Intel set each boot, hash-verified.
Evidence: [`../probe/intel-async-dword-head-2026-10-01/`](../probe/intel-async-dword-head-2026-10-01/)
Resolves: [2026-09-29 issue](../issues/2026-09-29-intel-gen3-async-corrupts-3dmark99.md)
Plan: [intel-gen3-async-submission.md](../plans/intel-gen3-async-submission.md)

## Why look again

The netbook's submission profile ([record](2026-10-01-hal-submission-profile-on-the-netbook.md))
put 63% of the HAL's Direct3D time in waiting for the GPU after each batch.
Asynchronous submission removes that wait, and had been rejected on
2026-09-29 for corrupting 3DMark 99: stale bands, displaced geometry,
blank rectangles. That run's probe had no ring-space wait and no refusal,
so it never ran with the ring busy, and no snapshot was taken after the
corrupt run.

## The hypothesis

`v9x_i9xx_ring_free_space` refused a head that was not qword aligned. A
synchronous submit only reads the head at rest on the tail, which is
aligned; a busy parser stops wherever a command ends, on any dword. The
refusal made the plan INVALID, the submit return 0, and the draw refuse
with reason 9 - and a refused Direct3D batch is drawn by nobody. Undrawn
regions of a triple-buffered frame show older frames, which is the shape
of all three photographs. The overwrite and lifetime hypotheses in the
issue need no part of this.

## Measured

ABI 2026100106 appends `RingPlanInvalid` with the last head and tail, and
`RingOccupiedMax`. Half-Life `mwd5`, 640x480 Direct3D, `hl-window.ps1`
(warm-up, then snapshots around a second timedemo):

| | synchronous (`nbprofd3d`, boot 83) | async, old free space (`nbasync0`, boot 84) | async, fixed (`nbasync1`, boot 85) | async by default (`nbasyncdef`, boot 86) |
|---|---|---|---|---|
| timedemo | 29.06 fps | 45.09 fps | **42.42 fps** | **42.24 fps** |
| ring plans refused as invalid | 0 | **39,080** (last head 0xF424) | 0 | 0 |
| draws refused, reason 9, dropped | 0 | **38,897** | 0 | 0 |
| ring-space waits / timeouts | 0 / 0 | 0 / 0 | 3,510 / 0 | - / 0 |
| ring occupancy high-water | - | 0xFDC0 | 0xFFB8 of 0x10000 | - |
| breadcrumb timeouts, abandonments, resets | 0 | 0 | 0 | 0 |

Boot 84 is the unfixed code with the new counters: the head it refused,
0xF424, is a dword offset, and about 8% of the session's draws were
dropped - which is also why it is the fastest column. With the fix, every
draw is submitted and the ring fills to its guard and waits, as designed.

On `nbasync1` the HAL took 42.8 us a Direct3D call against 84.3 us
synchronous; the head wait (52.8 us a submission) became 0.83 s of
ring-space waits over 1,739 calls.

The operator watched Half-Life Direct3D and 3DMark 99 at 1024x576 on boot
85 and reported both correct.

## The change

- `v9x_i9xx_ring_free_space` accepts a dword head. The ring is empty only
  when the head is exactly on the tail; otherwise the free bytes are the
  distance less the 8-byte guard, rounded down to a qword, and never
  negative. A head a dword past the tail is a ring a dword from full: the
  old one-expression form `(head - tail - guard) & mask` would have called
  it nearly empty and overwritten unread commands, had the alignment check
  not refused it first. Host cases in `test_i9xx_ring.c` were written
  first and failed (ten failures).
- Asynchronous submission is the default: `IntelAsyncSubmit` absent means
  on, `0` means off, like `IntelRuntime3D` and `IntelFlip`. Boot 86, with
  no key in `INTELARM.TXT`, reported the async engine cap
  (`EngineCaps=0x614`) and `AsyncEnabled=1`.
- `V9X3D FLIP` (and a fresh install) now permits async; the `ASYNC` verb is
  replaced by `V9X3D SYNC`, which keeps runtime Direct3D and the flip with
  the CPU waiting after every batch. `ON` and `OFF` still write it off.

## Quake 2 with async (boot 87)

`q2-timedemo.ps1`: `quake2.exe +set vid_ref gl +set logfile 2 +set timedemo
1`, 640x480 fullscreen, the attract loop replaying `demo2`; `config.cfg`
saved before and found unchanged after. Four full passes at 19.6, 19.8,
19.8 and 19.8 fps (31.9-32.3 s), no refusal, invalid plan, ring-space
timeout or reset. The synchronous figure is 17.2-17.3 fps on boots 54-56,
before the ICD's per-call cost work (`43fe5b7`), so the 15% between them
is not async's alone; no synchronous control was run on this build, at
the operator's request.

The gain is small because the HAL is not where Quake 2's time goes. Over
the 156 s window the render interface's draws were 11.3% of wall time,
81.9 us a call for 796 primitive dwords; the ICD's own buckets in each ten
seconds were about 3.4 s in `glVertex*`, 1.7 s in `glBegin`/`glEnd`, 1.6 s
in the batch sink and 1.2 s in the render interface. Writing a submission
through the uncached aperture is now the largest HAL phase, 38.5 us a
submission (8.4 s, 5.4% of wall).

An earlier attempt the same evening is discarded: a Quake 2 launched by a
cancelled command was still running when the second started, the ICD log
shows two processes rendering at once, and the netbook stopped answering
while they were being quit (boot 86 to 87, restarted by the operator).

## Not established

- Intel erratum 12 (309220-0132): the CPU's ring writes now overlap GPU
  execution, the interleave the 2026-09-29 gate decision named. Nothing
  here shows it triggering; nothing shows it cannot.
- Other applications. Quake 2, Final Reality and the OpenGL ICD were not
  run with async this session; the ICD's draws take the same submit.
- How much of the gain survives a GPU-bound scene; Half-Life at 640x480 is
  CPU-heavy.
