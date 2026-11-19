# Vsync off on the HD 4600: half-drawn frames from a flip released before the blank; async flips tear with a strip; copy-present tears once

Date: 2026-11-19. Machine: the i5-4570, HD 4600 (`8086:0412`), 10.0.1.254,
boots 282 to 285, desktop 800x600x16, its VGA output into a VGA-to-HDMI
capture recorded by OBS. Builds: `7d658066` (main, 0.57.0) and the changes
below on top of it, each installed whole by `run-release-checks.ps1
-Machine hd4600` (every probe passed after each install). Workload: 3DMark
2001 SE, default benchmark, 1024x768x32, started by
`vsync-run.ps1`/`hsw-3dm2001.ps1` with V9XTRACE snapshots either side and
frame grabs; `[Velocity9x] VSync=` set in SYSTEM.INI before each run.
Evidence: [`../probe/hd4600-vsync-off-2026-11-19/`](../probe/hd4600-vsync-off-2026-11-19/),
one folder per run, OBS frame sheets as `obs-*`.

## Why

Michael: vsync off "has always been lots of flicking", which is why
performance has been judged with vsync on. Tearing is expected with vsync
off; whole frames flickering are not.

## 1. The cause: an unsynced flip is released before the scanout takes it

`v9x_vsync_flip_stays_pending` (since `efd2fa90`, the GMA 950's fix) holds
an unsynced flip until the hardware takes it only where
`v9x_scanout_unsynced_base_latches()` says the base latches at the blank:
the Gen3-Gen5 plane and GCN's queued flips. Haswell's `DSPASURF` is taken
at the next blank wherever it is written (Phase F3, `hsw_scanout.c`), so an
unsynced flip there was called done at once and the game drew its next
frame into the buffer still on the panel. Rage 128, Evergreen, RS690 and
RV280 flips were in the same position.

Run A (main, `VSync=2`): OBS shows frames cut off with black or unfinished
bands (`obs-rec1`, run A's sheets). `FlipVSyncOverridden` +1678,
`FlipUnsyncedHeld` 0. `DrawsIntoPresented` stayed 0: it compares draws with
the buffer the HAL calls presented, which this case moves at once, so it
cannot see it. The recordings are the evidence.

**Fix 1:** `v9x_scanout_unsynced_base_latches()` also answers yes for every
hardware flip (`v9x_scanout_hw_flip()`). Each of the five scanouts reads
its own pending state from the chip (`DSPASURFLIVE`, `GRPH_UPDATE_PENDING`,
`CRTC_OFFSET`), so one that applies a base at once reports it taken at the
first poll. Run B (`VSync=2`): whole frames, no flicker; every flip held
(`FlipUnsyncedHeld` +978) and taken (`FlipTakenAtDone`). But vsync off now
ran exactly as vsync on: 6,879 against 6,919 flips over the same 2.9
minutes, because a linear Haswell plane cannot change mid-frame.

## 2. Async flips: fast, but a strip at every tear

i915 (v6.6) flips asynchronously on g4x+ by setting `DISP_ASYNC_FLIP`,
`DSPCNTR` bit 9, before the `DSPSURF` write (`g4x_primary_async_flip`), and
`intel_async_flip_check_hw` refuses a linear plane before display 12; the
2013 IVB patches required X tiling too. Measured anyway with a new probe
switch, `V9XHSWMM /flipasync:N` (`HSW_FLIP_FLAG_ASYNC`): bit 9 set, armed at
the base already scanned, two frames, then the timed write, then `DSPACNTR`
put back.

| | Line at write | Live base changed | Polls |
|---|---:|---|---:|
| `/flip:2` | 30 | line 599 (the blank) | 68,107 |
| `/flipasync:2` | 0 | at once | 0 |

`DSPACNTR` read back 94000000 after the restore (`probe-flip*.INI`).

Run C (unsynced flips with bit 9 set): about 1.8x the flips (about 12,500
against 6,919 in 2.9 minutes), no engine resets, Dragothic 67-69 fps on
screen. But every tear carried a strip about 20 capture lines thick (about
11 of 600 source lines) of a third frame, the same thickness wherever the
tear fell (`obs-band2`, `obs-bands4`). Michael: "doesn't look like other
native driver tearing". Not the plane offset: both 3DMark buffers (0 and
300000h) are page-aligned at a 4096 pitch, so `DSPAOFFSET` stayed 0. Most
likely the display's already-fetched lines on a linear plane, which is
what i915's rule avoids; not proven.

## 3. Copy-present

For vsync off the plane scans a copy target, and each flipped-to buffer is
copied into it on the blitter (`v9x_hsw_present_copy`, an
`XY_SRC_COPY_BLT` of the whole visible frame). The copy writes far faster
than the plane scans, so it passes the beam once. Flip already drains the
render engine before it, and the next Flip waits for the blitter. X's DRI2
presented swap-interval-0 swaps the same way, by a blit, for years.

**First version, 8 MiB carved below the engine reserve at boot**
(`HeapEnd=016A0000` instead of `01EA0000`): the screen changed only 3-11
times a second and 3DMark's meter read 1-2 fps through Car Chase (OBS,
Michael's 17:37 and 17:55 captures). The frame grabs of the same 17:55 run
read 43-67 fps, but their files are dated 17:58:45-17:59:00: Dragothic, not
Car Chase, so the two did not disagree. The counters showed the cause, with
vsync on as well as off:

| Run (build) | MipTreeDeclined | LockKindTexture | Flips |
|---|---:|---:|---:|
| C (async, full heap) | 205 | 20,087 | 15,314 |
| E-v0 (carve, vsync on) | 817 | 31,622 | 5,307 |
| D, E-v2, F (carve, vsync off) | 834-958 | about 31,700 | 9,600-12,800 |

The carve cost every Direct3D program its textures. A profile of that run
(V9XPROF) put 48 % of samples in `d3d_rv280_map!v9x_d3d_rv280_map_draw_`,
which should not run on Haswell; the symbol resolution was not checked and
nothing rests on it.

**Second version, the target taken on demand:** the first unsynced flip
takes a page-aligned, screen-sized block from the flipping object's heap
through DDRAW's `DDHAL32_VidMemAlloc` (`v9x_d3d_place_raw`, the mip-tree
placer's route), and the next synced base write gives it back; a mode
change forgets it, since DirectDraw rebuilds the heap. No room, or a copy
the blitter refuses, falls back to the async write. Runs G and H, recorded
through the OBS MCP:

- The screen changes 57-58 times a second outside loading screens; the
  meter reads 33-156 fps (Lobby 147-156). 16 consecutive frames at 49 fps
  and 16 at 147-156 fps: every frame whole, no strip. The scenes moved
  slowly, so a single tear line would be hard to see; that there is one
  tear per frame is from the mechanism, not seen.
- Car Chase, same build: vsync on read 7, 26, 57, 58, 29, 48 in the Low
  test and 34, 1, 1, 18, 1 in High (185 mip trees refused); vsync off read
  1, 38, 42, 6, 1, 6 in Low (354 refused). Car Chase dips to 1 fps with
  vsync on too: this machine's 32 MiB of stolen memory is short of
  textures there. The 3 MB block held while vsync is off roughly doubles
  the refusals in that test.

## Earlier hypotheses the evidence killed

- The plane's x offset latching apart from the async base (both buffers
  page-aligned, offset 0).
- The render engine still drawing the flipped buffer (Flip drains it first,
  `ddhal_core.c`).
- Textures overlapping the carved target: the 018F1000h mip tree in the
  first check came from run C's boot, before the carve; on the carved boot
  every mip tree sat below 016A0000h.
- The two-minute stall at Car Chase High's load in runs A and B, first
  taken for a vsync-off freeze: runs C, D and later loaded straight
  through, and a vsync-on run loaded at the same speed. Cause not found.

## Also seen

Before run A, after a reset, the desktop showed stretched into the top half
of the capture with green line garbage below (`obs-desk`). The next boot's
registers read true 800x600 timing, source 800x600, the fitter 1:1,
`PipeSource=matches` (`boot283-desktop-V9XHSWMM.INI`); it did not recur.
Unexplained.

## Not established

- Ivy Bridge (HD 4000) shares `hsw_scanout.c` and so takes copy-present and
  the async fallback: not run.
- The hold rule on Rage 128, Evergreen, RS690 and RV280: not run.
- Games other than 3DMark 2001 SE; a game that writes the front buffer
  after a flip would not be seen while copy-present scans the target.
- Whether the tear in copy-present is one clean line in every case.
