# Frame grabs on any flipping engine, and three probe fixes

Date: 2026-10-27

Status: **measured on one machine.** M-J0F0SJ's Ironlake (`8086:0042`),
boot 76, intel-gma package built from `4132bb86` plus this change
(`4132bb86-dirty`), installed by WININIT rename and checked by SHA-256.
Evidence: [`../probe/frame-grab-ironlake-2026-10-27/`](../probe/frame-grab-ironlake-2026-10-27/).
Gates: run-checks.

## Why

The 0.32.0 release checks left two visual faults open that the agent's
screenshot could not reach:

- the Ironlake's wrong early frame in 3DMark 2000's Game 1, seen once on
  22 October and once in the release checks. Each screenshot took 40 to
  70 s during 3D, so the moment could not be aimed at
  ([record](2026-10-27-0.32.0-release-checks-ati-ironlake.md));
- the HD 5450's every-second-frame flicker, which the screenshots never
  showed because they read memory after drawing finished
  ([record](2026-10-27-cedar-page-flips.md)).

Intel's frame cover (`i9xx_scanout.c`) writes one downsampled image a
session, on Intel only. The probes also cost reruns: a dash switch V9XD32
ignored, two probes overlapping, and a snapshot whose file name had to be
searched for.

## The changes

- **Frame grab.** `V9XTRACE -grab:<interval>x<count>` sends the new
  `V9X_DDGRABFRAMES` escape. The 16-bit driver stores the interval, the
  countdown and the count in the shared block (appended within ABI
  2026102002). At each accepted flip, `ddhal_core.c` counts down. On a
  due flip it drains the engine and writes the new front buffer whole to
  `C:\V9XDIAG\V9XGRAB<n>.BMP`, recording the flip sequence, the offset
  and the result per file. The read plan (bounds against video memory,
  overflow-safe), the pixel decode from the display's own masks, the BMP
  layout and the schedule are in `src\common\frame_grab.c`, host-tested
  in `tests\host\test_frame_grab.c`. Breaking the bit replication failed
  nine of its checks.
  - A static 6 KB row buffer first put 6 KB into V9XHAL.DLL's file and
    the SiS floppy's first disk 5,210 bytes over 1.44 MB; the buffer is
    now allocated per grab. The HAL is 1,536 bytes larger than 0.32.0's,
    which leaves about 900 bytes on that disk.
- **V9XD32 switches.** `-name` is taken as `/name`, in any case, for flag
  and numeric switches. The report gains `CommandLine=` and an
  `UnknownSwitch=` line per word that names no switch, with the count.
- **Probe lock.** V9XD32, V9XGLP, V9XGLIDP, V9XR3DP and V9XGL3P take one
  named mutex (`tools\diag\probe_lock.h`) and answer `Result=BUSY`
  without running when another holds it. V9XD32's T14 child does not
  take it.
- **Snapshot pointer.** V9XTRACE writes the name of the snapshot file it
  used to `C:\V9XDIAG\V9XSNAL.TXT`.

## Boot 76

| Check | Result |
|---|---|
| `V9XTRACE -grab:40x10`, then 3DMark 2000 default | 10 files, each 2,359,350 bytes (54 + 768 × 3,072); `GrabSeq` 40 to 400 by 40; `GrabOffset` rotating 18C000h, 318000h, 0 (the triple buffer); `GrabStatus=1` |
| The grabs | Game 1 from mission time 0.0 to about 10, whole, colours right, HUD drawn (`grab-sheet.jpg`, `V9XGRAB0.png`) |
| 3DMark 2000 with the grab armed | completes, 2833 (the grabs stall ten flips; not comparable) |
| `V9XD32 -ddi6 -T14NOMODE -bogus` | PASS; `DdiRequested=6`; `UnknownSwitch=-bogus`, `UnknownSwitches=1`; the child ran `/child /nomode` and exited `CHILD-EXITED-NO-MODE` |
| V9XGLP, then V9XGLIDP while it ran | V9XGLIDP `Result=BUSY`; V9XGLP `Result=PASS` |
| V9XGLIDP alone after | PASS, 0 failures |
| `V9XSNAL.TXT` | `C:\V9XDIAG\V9XSNA7.INI` (all eight names taken, the last reused) |

## Not checked

- The Ironlake's wrong frame. None of the ten grabs shows it. All ten
  carry the mission-time panel, which the wrong frame did not, so it may
  come before the mission clock starts; a grab armed with a shorter
  interval would reach it.
- The grab on any other engine. The hook is in the core flip path, so
  any engine that takes page flips should grab, the HD 5450 included;
  only the Ironlake was run.
- An 8 bpp or 24 bpp mode: refused by the plan (`GrabStatus=4`), not run.
- V9XR3DP and V9XGL3P's BUSY answers: same code as V9XGLIDP's, not run.
