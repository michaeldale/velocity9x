# HD 5450 scissored depth clears: drawn on the CP

Date: 2026-10-28

Status: **measured, one card, one demo.** M-BXMFA8's HD 5450 (`1002:68F9`,
Cedar), boot 42. Follows [128 MiB heap](2026-10-27-cedar-heap-128-mib.md).
Screenshots in
[`../probe/m-bxmfa8-cedar-hud-depth-2026-10-28/`](../probe/m-bxmfa8-cedar-hud-depth-2026-10-28/).

## The report

The owner saw Quake 3's HUD models (the ammo, head and armour icons beside
the numbers) vanish whenever the weapon or a wall was behind them
(`q3-owner-before-behind-weapon.png`: the icon beside the armour count is
missing). Quake 3 draws them as a small 3D scene after the world, first
clearing depth inside a scissor box round each icon.

## What the log showed

`C:\V9XDIAG\V9XGL.LOG` on the guest, every Quake 3 run since boot 33:
162,425 lines `glClear result=1 mask=00000100`. Result 1 is
`V9X_R3D_RESULT_INVALID`, the mask depth only. Nothing else failed in
those runs.

The path: `v9x_r3d_clear_tiled_depth` sends each rectangle to
`v9x_hal_depth_fill`. `eng_eg.c` took a depth fill only for the whole
surface, since its DMA engine writes linear rows and the Z16 is 1D tiled
(first-light decision). A smaller rectangle was declined, a tiled buffer
has no CPU fallback (`v9x_depthfill_body`), and the clear failed. The
icons were then depth-tested against the world's Z.

## The change

A depth fill short of the whole surface is drawn on the CP:
two triangles over the rectangle, ZFUNC ALWAYS with writes on, scissored
to it, every colour channel masked (CB_TARGET_MASK 0), the colour target
the Z buffer's own memory named as RGB565, which only sizes the draw. It
is the fill Haswell and Gen4 already use for their Y-tiled Z buffers
(`d3d_hsw.c`, `d3d_g4.c`). The description is
`v9x_d3d_eg_depth_fill_draw` in `d3d_eg_map.c`, host-tested
(`test_d3d_eg_map.c`, written first and failed to build); `d3d_eg.c`'s
`v9x_d3d_eg_depth_fill` submits it, and `v9x_eg_fill` hands over what it
declines. A whole-surface fill stays on the DMA engine.

`v9x_d3d_eg_depth_fill` is a new external symbol, the third of its kind
beside `v9x_d3d_hsw_depth_fill` and `v9x_d3d_g4_depth_fill`.

## Boot 42

- DRV, VXD, HAL and ICD from this working tree (`d1769f4b` + the change),
  by WININIT rename, each SHA-256 checked against the build.
- Quake 3 demo, windowed: pixel format 1, 1346 frames, 62.2 fps at
  1024x768 and 67.5 fps at 800x600. 5,471 `glClear` calls, none failed.
- `q3-b42-800x600-hud.png`: the ammo icon and the head draw beside their
  counts; the head overlaps the weapon's edge and draws over it.

## A trap on the way (boot 41)

A HAL-only swap onto the guest's older `e3fbac9e` driver left no 3D engine
at all: the ICD's describe returned `NOT_READY` (no pixel formats, Quake 3
fell back to `3dfxvgl`), and V9XD32 found no HAL (`NO-HAL-AT-DEPTH`). The
snapshot read `BuildsAgree=0` with an empty `HalBuild`. Main had appended
`hsw_flush_omit` to `V9X_DD_SHARED` since then (3fd984d6). Which check
refused was not traced. The matching set at boot 42 cleared it. A HAL
swap needs the driver it was built with.

## Not checked

- Only demo001, and one frame shows an icon over the weapon. The owner has
  not looked yet.
- Direct3D's partial clears go through the same `v9x_hal_depth_fill`, so
  they were failing too. Whether that is 3DMark 2000's vanishing trees was
  not tested.
- The change put the SiS floppy's disk 1 188 bytes over (V9XHAL.DLL is
  shared by every family). GLIDE3X.DLL moved to the SiS third disk, as on
  the ati, matrox, s3 and vbe disks (Michael, 2026-10-28).
