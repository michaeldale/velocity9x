# V9XTRACE identity fields on the 86Box ViRGE/DX guest, 2026-10-06

What the snapshot now records about who produced its counters, run once on
`WIN98-86BOX` (86Box, S3 ViRGE/DX, port 9869, agent boot 641).

## Why

marxveix's Rage archive (2026-10-05) had to be attributed by guesswork. Its
snapshots named only V9XTRACE's own build, not the driver's. Nothing said
whether two files came from one boot, which matters because the counters
add up per boot and only same-boot files subtract. No program names were
recorded, in the snapshot or in `V9XGL.LOG`.

## What was run

The build was `bea97bd-dirty` with this change. `V9XDISP.DRV`, `V9XHAL.DLL`,
`V9XGL.DLL` and `V9XMINI.VXD` went in by a `WININIT.INI` rename (plain
`dest=src`, no `NUL=`), followed by a reboot. Then:

1. `C:\V9XTRACE.EXE` before any 3D program →
   [V9XSNA7-1-BEFORE-3D.INI](V9XSNA7-1-BEFORE-3D.INI). This run used the
   first version of the tool. It wrote `BootStamp=` (renamed `BootId=`
   afterwards), and its `BuildsAgree=0` came from comparing an empty
   `HalBuild` (see below).
2. `C:\V9XDDP.EXE` (the DirectDraw/Direct3D probe), then `C:\V9XGLP.EXE`
   twice (the OpenGL probe, `Result=PASS`), then `C:\V9XTRACE.EXE` →
   [V9XSNA7-2-AFTER-DDP-GLP.INI](V9XSNA7-2-AFTER-DDP-GLP.INI).

All eight snapshot names already existed on this guest, so both runs
reused `V9XSNA7.INI` and say `SnapshotReused=1`.

## What the files show

- **Builds.** `DriverBuild`, `HalBuild` and `TraceBuild` all read
  `0.11.0 bea97bd-dirty` in the second file, so `BuildsAgree=1`. In the
  first, `HalBuild` was empty: nothing had loaded DirectDraw, so DriverInit
  had not run (`DriverInitDone=0`, mode 0x0x0). The tool now treats an
  empty `HalBuild` as "not loaded yet" rather than as a disagreement.
- **Boot.** Both files carry `2026-10-06 15:34:39.31`. That is the DOS clock
  when the 16-bit driver created its shared block, and here run 1's own
  escape created it, which is why it equals run 1's `DumpTime`. It
  identifies the boot; it is not the time the boot started.
- **Programs.** `Process1=V9XDDP.EXE d3d-contexts=5`,
  `Process2=V9XGLP.EXE gl-describes=2` (one describe per run).
- **ICD log.** The attach line now names the build and the executable
  ([V9XGL-ATTACH.TXT](V9XGL-ATTACH.TXT)). Lines from before the deploy
  still read `build-marker=1`.
- **Installation.** The run shows that a machine with a history yields
  usable facts:
  - `[Card]` lists the Matrox Millennium this guest once had, as
    `Card1Present=0`, and the ViRGE as `Card2Present=1`. Both are at the
    same `BUS_00&DEV_0C&FUNC_00`.
  - `[Installed]` lists both display-class entries (`Display\0000`
    Matrox, `Display\0001` Velocity9x).
  - Every `V9X*` file in `SYSTEM` is listed with its size, date and build:
    `V9XSETP.DLL` is from 2026-09-26, and `V9XMINI.S3B` is a 2026-09-14
    leftover carrying `build=neproof2`.
- `V9XSETP.DLL` reads `build=unknown`. It embeds its id without the
  `build=` marker the scan looks for.

Not covered by this run: a real-hardware machine, the abi-mismatch path
(an old driver read by the new tool, which should still write every
machine section before `Error=abi-mismatch`), and the process table past
eight programs (host-tested only, `tests/host/test_diag_identity.c`).
