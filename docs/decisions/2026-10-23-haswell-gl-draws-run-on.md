# HD 4600: draws outside the GDI primary run on; Quake 3 at 1024x768 35.7 to 42.6 fps, and the GPU's cost per batch is now the limit

Date: 2026-10-23. Machine: the i5-4570 (W9QI5A83), boots 64 and 65.
For the Quake 3 stutter in tester report V9X-WM1C1K
([../issues/2026-10-23-haswell-tux-racer-untextured-and-quake3-gl-sync.md](../issues/2026-10-23-haswell-tux-racer-untextured-and-quake3-gl-sync.md)).
Narrows decision 3 of
[../plans/intel-haswell-pipelined-submission.md](../plans/intel-haswell-pipelined-submission.md)
a second time, after
[2026-10-20-haswell-pipelined-gdi-rule.md](2026-10-20-haswell-pipelined-gdi-rule.md).
Evidence:
[`../probe/hsw-gl-draws-run-on-2026-10-23/`](../probe/hsw-gl-draws-run-on-2026-10-23/).

## The change

Since 2026-10-20 a Haswell draw waited for its own sequence whenever GDI
might be drawing, which is whenever no process holds exclusive mode. The
ICD never takes exclusive mode, so every OpenGL draw waited.

The hazard the wait exists for is one buffer: the GDI primary, which the
16-bit driver writes for GDI and the software cursor with the CPU and no
drain hook. Every other target is reached by the CPU only through Lock,
Blt, the render interface's clear, CPU fallback and finish, and each of
those drains both rings already. Gen3 leaves its draws running with no
such rule at all.

`v9x_intel_hsw_pipe_draw_waits` (`hsw_pipe.c`, host-tested) now answers:
wait only while GDI may draw **and** the target's extent (pitch x
height from its offset) overlaps the primary, `[0, fb.visible_bytes)`.
A primary of unknown size, or an extent that wraps, waits. Exclusive mode
is unchanged: nothing waits. `d3d_hsw.c` applies it. The host test was
seen failing against a stub of the old rule (the two back-buffer cases),
then passing. `build-host` and `run-checks` pass.

On the 4570 only `V9XHAL.DLL` changed (`3137ed2e-dirty`, by a WININIT
rename and `reset`; the previous HAL is `V9XHAL.7C0`). The rest of the
set is still `9d4c7012-dirty`, with the `b09a5d77-dirty` ICD. The HAL
changes between that set's code (`7c02ad42`) and this one are Ironlake
and Rage 128 only, and the shared ABI gained constants only.

## Quake 3 demo 1.11, `demo001`, 1024x768x32, Z24, `com_speeds 1`

`timedemo 1`, multitexture on, `com_maxfps 85`. Frame times are Quake 3's
own `all:` field per frame, ms.

| | fps | median | p90 | p99 | max | > 33 | > 50 | > 66 | > 100 |
|---|---|---|---|---|---|---|---|---|---|
| boot 64, before | 35.7 | 26 | 40 | 65 | 79 | 301 | 39 | 10 | 0 |
| boot 65, run 1 | 42.6 | 21 | 34 | 61 | 104 | 146 | 23 | 2 | 1 |
| boot 65, run 2 | 42.6 | 21 | 34 | 57 | 86 | 151 | 19 | 2 | 0 |
| boot 65, run 3 | 42.6 | 21 | 34 | 60 | 77 | 143 | 21 | 4 | 0 |

The before run is one process, one run; the three after runs are one
process. The 104 ms frame (demo frame 352) did not recur in runs 2 and 3
and is not explained. The slow stretch near demo frames 915-945 is slow in
every run, 60-86 ms.

## Where the time is now (V9XTRACE, boot 65, Quake 3 only)

860,492 draws went out asynchronously and 30,764 synchronously; which
draws the synchronous ones were (depth fills, the initialising batch,
anything that overlapped the primary) is not split by the counters. No
refusals, timeouts or resets. Per draw, at the 4570's 3.2 GHz TSC:

| | µs |
|---|---|
| the whole engine draw (`TimeEngineDraw`) | 178.8 |
| waiting for a free slot (`TimeRenderDrain`, per call) | 99.3 |
| vertex copy into the uncached reserve (`TimeRingWrite`) | 54.4 |
| state write and kernel (`TimeRingWait`, `TimeDecode`) | 5.9 |

The ICD still spends 56-69 % of wall time in its flush (`sink flush-ms`,
`after-boot65-V9XGL-excerpt.txt`), about 150 µs a flush in the demo
against 171 before. With the per-draw wait gone, the CPU now waits for the
render engine to free a slot: the GPU's own cost per batch, about 100 µs,
is the limit. The plan named that cost out of scope. Each batch carries
the full pipeline state and several CS-stall and render-target-flush
PIPE_CONTROLs (`hsw_3d.c`), which is the presumed cause; not measured.

## Not established

- Whether this is the stutter the tester feels. A timedemo shows no
  frame over 104 ms either side, and nothing about the mouse.
- 800x600 and 16 bpp after the change; Tux Racer after the change.
- A windowed Direct3D application: its back-buffer draws now run on too.
  The reasoning covers it (it presents by Blt, which drains). Michael ran
  Half-Life's Direct3D windowed on the 4570 on this HAL and reported it
  working fine (2026-10-23); no log or capture was kept from that run.
- V9XDDP and 3DMark 2000: their draws are in exclusive mode, where the
  rule is unchanged, so they were not rerun.
- Ivy Bridge: it draws through the same `d3d_hsw.c`, so it takes the
  same rule. Not run there.
