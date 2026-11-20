# Vsync off on the R7 250 (Oland): half-drawn frames from a flip called done before the blank; the unsynced flip now takes the next line

Date: 2026-11-20. Machine: W9QI0DAD, Radeon Oland XT `1002:6610` (R7 250
class), its HDMI output into the capture OBS records. Build: branch
`gcn-vsync-off` from main `6a493f64`, package `6a493f64-dirty`, installed
whole by `run-release-checks.ps1 -Machine oland` (boot 120). Workload:
3DMark 2001 SE default benchmark, 1024x768x32, `[Velocity9x] VSync=2`.
Evidence: [`../probe/r7250-gcn-vsync-off-2026-11-20/`](../probe/r7250-gcn-vsync-off-2026-11-20/).

## What the recording showed

Michael's OBS recording `2026-11-20 12-23-12.mkv` (7.2 s, Car Chase at
120-163 fps on a 60 Hz output, build on the box not identified): of 16
consecutive captured frames most lack whole parts of the scene: bands of
sky in the dark clear colour, wheels drawn without their car bodies, the
HUD's mph, time and LOCKED box cut off or absent, the MadOnion logo gone
(`obs-before-1223-carchase-16frames.jpg`). Tearing joins two finished
frames at a line; these were frames still being drawn, on the screen.
The same symptom as the HD 4600's run A
(`2026-11-19-haswell-vsync-off-async-flips.md`).

## The cause

`eg_scanout.c` wrote every flip with `GRPH_FLIP_CONTROL` clear, which
the controller takes at the next vertical blank. On GCN (queued flips,
`e6266162`) an unsynced flip was then called done as soon as it was
written (`v9x_eg_written_now`), so the buffer leaving the screen went back
to the game while still scanned until the blank. At 160 fps 3DMark cycles
its three buffers within one refresh and drew into the one on the panel.

The Haswell fix (hold every hardware flip until its readback says taken)
did not reach this case: `v9x_scanout_unsynced_base_latches()` was
already true for GCN, but GCN's pending check skipped the readback for an
unsynced flip once written. The queued-flips record's "frame grabs ...
show whole frames" with vsync off could not see it: a grab is taken after
the drawing.

## The fix

An unsynced flip writes `GRPH_FLIP_CONTROL` = 1,
`GRPH_SURFACE_UPDATE_H_RETRACE_EN`, so the controller takes the address
at the next horizontal blank, and the flip is pending until
`GRPH_UPDATE`'s `SURFACE_UPDATE_PENDING` clears, as a synced one is.
Linux writes the same bit for an async flip (`dce_v6_0_page_flip`,
`evergreen_page_flip`; bit 0 in `dce_6_0_sh_mask.h` and
`evergreen_reg.h`, read here from `build\external`). The non-queued
Evergreen path (Cedar, Caicos, Trinity) takes the same write: it held an
unsynced flip until the blank before, so vsync off there ran as vsync on.

## Measured (boot 120)

- Release probes: V9XD32 default and `/bpp16` give the same verdicts as
  the 0.57.0 checks on this card (T19 FAIL in both, unchanged); GL,
  Glide, R3DP, WPRS, FBCMP pass.
- 3DMark 2001 SE, `VSync=2`, stopped at 4 minutes (Car Chase Low,
  Dragothic Low and High, into Lobby), recorded by OBS
  (`2026-11-20 14-42-01.mkv`):
  - 19,363 flips handled, every one unsynced and held
    (`FlipUnsyncedHeld` 19,353, `FlipVSyncOverridden` 19,353),
    `FlipTakenAtDone` 19,356: the controller had taken each flip when
    the HAL called it done. 53-93 flips a second through Car Chase
    (`run.log`), so vsync off is not held to the refresh. No engine
    timeout or reset in the snapshots.
  - Frames: Car Chase at 42 fps and Dragothic at 154-170 fps, 16
    consecutive captured frames each, every one whole with its HUD
    (`obs-after-1442-*-16frames.jpg`). One Car Chase frame shows a
    tear: the previous camera's sky above a line, the new shot below,
    both finished.
- SYSTEM.INI put back afterwards (no `VSync` key, as before).
- The first 22 s of Car Chase on the recording freeze for about 2 s, move
  for about 0.2 s, and repeat (ffmpeg `freezedetect`: ten freezes from
  0 to 22.4 s), and 3DMark's meter reads 9 fps after one of them. These
  are the run's own frame grabs: `-grab:40x10` wrote ten whole frames as
  BMPs from inside the flip (`GrabWritten` 10, `GrabSeq` flips 50 to 410,
  40 apart, about 0.25 s at 160 fps), and the game waits for each. The
  later 4 s and 7 s freezes (27 s, 31.7 s, 65 s) are the loading screens
  between tests. A run meant for watching should not grab.
- Rerun without grabs (`vsync2-nograb`, OBS `2026-11-20 14-56-38.mkv`,
  stopped at 3 minutes): Car Chase plays in real time, meter 74-173 fps,
  no freeze from 2 s to its end at 22.8 s. 16 consecutive captured frames
  at 120 fps and 16 at 157 fps (`obs-nograb-*`): every one whole with its
  HUD; several show a tear, the explosion's glow cut at one line with both
  frames finished. 18,270 flips, `FlipNotTakenAtDone` 0, no engine
  timeout or reset, `DrawsIntoPresented` 0. One freeze of about 1.2 s
  near the test's start (recording 0.7-2.0 s, meter 8 fps at time 03:67)
  is not a grab (none were taken) and is not explained.

## Michael's run (boot 120, 15:07)

Michael ran 3DMark 2001 SE himself with `[Velocity9x] VSync=2` in
SYSTEM.INI, on the same boot and build: **9220**. Settings as 3DMark
reports them: 1024x768x32, Z16, compressed textures, double buffering,
D3D software T&L, Low detail. Game 1 Car Chase Low 118.1 fps, High
55.6; Game 2 Dragothic Low 107.1, High 58.6; Game 3 Lobby Low 174.8,
High 88.9; Game 4 Nature 58.0 fps. The run completed to its score.
Screenshots of the score and the details list:
`michael-run\score.png`, `details-*.png`. As measured with vsync off,
this score is diagnostic only. SYSTEM.INI was left with `VSync=2` as
Michael set it.

## Not established

- Carrizo and Mullins (same code) and the Evergreen parts: not run.
- Whether every tear is a single clean line.
