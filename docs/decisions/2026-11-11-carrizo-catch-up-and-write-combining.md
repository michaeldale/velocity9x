# Carrizo from 0.34.0 to main: the updater's route, Glide's first run, and write-combining (FbWrite 204 to 4,779 MB/s)

Date: 2026-11-11

Status: **measured, one machine, one run of each.** M-XP7053's Radeon R7E
(`1002:9874`, SUBSYS `8158103C`, REV 88; AMD GX-420GI, HP board), remote
help, agent 0.16.0 (`dev-fault-3`). The machine was last checked at
0.33.0 and had not run anything since 0.34.0 went on by the updater on
2026-11-01 ([record](2026-11-01-updater-moves-vbe-to-chip-family.md)).

Evidence: [`../probe/carrizo-catchup-2026-11-11/`](../probe/carrizo-catchup-2026-11-11/)
(`carrizo-before/` on 0.34.0, `carrizo/` on main), and dispbench
`docs/results/carrizo-0.46.0-uc/` and `carrizo-main-wc/` (dispbench
2fb75c7).

## Three builds, in order

| Boot | Build | How it got there |
|---|---|---|
| 50 | 0.34.0, d9d7aeb9 | V9XUPD, 2026-11-01 |
| 51 | 0.46.0, c8d92737 | V9XUPD `/CHECK` today, then agent `reset` |
| 52 | main 254b7655-dirty, this record's change | `run-release-checks.ps1`, WININIT renames and `reset` |

## The updater, 0.34.0 to 0.46.0

`V9XUPD.EXE /CHECK` asked consent, offered 0.46.0 for the `ati` family,
downloaded and verified it, and staged ten `[rename]` lines in
`WININIT.INI` (no `NUL=`) with `V9xUpdateFinish` in RunOnce. The machine's
NDIS2 network hangs Windows' own restart, so "Restart Windows now?" was
answered No and the agent's `reset` used instead. After it `/FINISH` wrote
`State=committed`, `RunningBuild=c8d92737` and showed "Velocity9x was
updated to 0.46.0 (build c8d92737)". `V9XHW.INI` read
`Direct3DMode=hardware`. The backup is in
`C:\V9XDIAG\UPDATE\BACKUP\V0_34_0` (ten files, `OEM.INF`,
`REGISTRY.REG`). A twelve-release jump through the updater works on this
machine.

`V9XUPD.INI` said `LastCheck=2026-11-01` and `current` until today: the
updater checks only when someone runs it, and nobody had.

## Probes and 3DMark 2000, 0.34.0 against main

| Check | 0.34.0 (boot 50) | main (boot 52) |
|---|---|---|
| V9XD32, 32 bpp | PASS | PASS |
| V9XD32, `/bpp16` | PASS | PASS |
| V9XGLP | PASS; CVA `FAIL-MISSING`, lighting `differ`, TnL `match` | PASS; CVA **PASS**, lighting `differ`, TnL `match` |
| V9XGLIDP | PASS, 0 failures | PASS, 0 failures |
| V9XR3DP | COMPLETE | COMPLETE |
| 3DMark 2000 (1024x768x16) | 1372 | 2768 |
| 3DMark counters | not reported by 0.34.0 | 609,839 draws, 0 refused batches, 0 engine stalls |

No probe verdict changed. **V9XGLIDP is Glide's first recorded run on
Carrizo**, and every check reads back exactly or within its tolerance:
clears, flips, triangles, depth, clipping, 565 and palette textures,
chroma key, half blend and table fog. The ten 3DMark frames grabbed each
way draw correctly by eye.

The 3DMark scores are recorded, not compared: the late-October dev
builds scored 2860 to 3020 on this machine, 0.34.0 1372, and this run
cannot say what 0.34.0 did differently because it writes none of the
counters. Not investigated.

## Write-combining

Before today no VI row set `V9X_HW16_DEVICE.write_combine`, so every
build, 0.46.0 included, wrote Carrizo's frame buffer uncached. Stage A
planned it without a refusal at every boot (`Mtrr=... r=0 s=4
b=e0000000 z=01000000`: 16 MiB at E0000000, free variable MTRR 4).

**Change:** `carrizo_hw16.c` opts in with `V9X_HW16_WC_MAPPING` over the
family's 16 MiB mapping, which lies inside the 256 MiB BAR0. The SDMA
and CP windows at BAR0+240 and +248 MiB are mapped apart from it.
`eng_vi.c` and `vi_cp.c` already drain the write-combining buffers before
each kick, so nothing else changed. Boot 52's record:
`MtrrWrite=ok s=4 b=e0000001 m=ff000800`.

dispbench at 1024x768x16, 0.46.0 (`carrizo-0.46.0-uc`, DBDD
`20261111-223743`, DBGDI `-223427`) against main (`carrizo-main-wc`,
`20261111-225615`):

| Measure | 0.46.0, uncached | main, write-combined |
|---|---:|---:|
| FbWrite (DBDD, primary) | 204 MB/s | 4,779 MB/s |
| FbWrite, offscreen | 202 MB/s | 4,683 MB/s |
| FbRead | 7.8 MB/s | 7.8 MB/s |
| FillRect 1024x768 | 203 MB/s | 2,165 MB/s |
| FillRect 64x64 | 149 MB/s | 433 MB/s |
| BitBlt bitmap to screen, 1024x768 | 182 MB/s | 1,235 MB/s |
| BitBlt DIB to screen, 1024x768 | 203 MB/s | 1,057 MB/s |
| BitBlt screen to screen, 256x256 | 8.2 MB/s | 8.1 MB/s |
| ExtTextOut, fixed opaque | 130,525 cyc/string | 51,251 cyc/string |

Between 0.46.0 and main only Gen3 code and two shared HAL texture
commits landed (`git log c8d92737..HEAD -- src include`): nothing on the
GDI or frame-buffer path, so the difference is the write-combining. Reads
do not change, as on Trinity: screen-to-screen GDI still reads at 8 MB/s
because GDI has no engine on this card (`GdiAcceleration=none`).

The probes and 3DMark above ran on the write-combined build, so they are
the check that the drains hold. Mullins (`cik/`, GCN 2) is a different
engine and is left alone.

## The desktop went from 1280x1024x32 to 1024x768x16

Not the updater's doing: its `REGISTRY.REG` backup and its writes cover
the driver key only, never `Config\0001\Display\Settings`. Boot 50's
`V9XBOOT.INI` listed the BIOS's 1280x1024 modes with attributes `00bb`;
boots 51 and 52 list `00ba`, bit 0 (supported by the hardware
configuration) clear, and `V9XMSW /set:1280x1024x32` returns
`DISP_CHANGE_BADMODE` (-2). The BIOS stopped offering 1280x1024 between
boots 50 and 51, which usually follows the monitor it read at POST. Left
at 1024x768x16.

## Quake 3 on main (boot 52, 2026-11-12)

The demo at `C:\games\Q3Ademo`, started as at boot 38 (`+set logfile 2
+set nextdemo quit +timedemo 1 +demo demo001`), its saved config
unchanged: 640x480 fullscreen, 16-bit colour and Z,
`GL_LINEAR_MIPMAP_NEAREST`, compiled vertex arrays, no multitexture.

| Run | Result | ICD paths |
|---|---|---|
| fullscreen, boot 52 | 1346 frames, 71.9 s: **18.7 fps** | hw 211,282 batches / 5,255,160 triangles; cpu 0; hw-refused 0; 355 textures, 0 failed, 0 evicted; 0 refused draws or clears |
| fullscreen, boot 38 (October) | 1346 frames, 63.0 s: 21.4 fps | hw 209,541 / 5,263,344 |

It ran to the end and quit by itself with no fault. Every batch went to
the engine. The rate is recorded, not compared: nothing was done to find
the cause of the difference. No frame was looked at; a fullscreen frame
is not visible to the agent's screenshot, and a windowed run would have
saved `r_fullscreen 0` into the owner's config.

## Not covered

- 32 bpp desktop for dispbench: only 1024x768x16 was measured.
- Why 0.34.0's 3DMark score is half the dev builds', or Quake 3's is 13%
  under October's.

Gates: `run-checks.ps1` passed on the change before it was installed.
