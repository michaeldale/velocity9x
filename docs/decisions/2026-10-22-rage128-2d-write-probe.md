# Rage 128 2D engine: the builder's fills and copies are byte-exact at 8, 16 and 32 bpp

Date: 2026-10-22

Status: **measured, one card.** A8U4I5's Rage Fury Pro (`1002:5046`),
boot 521, Velocity9x tier-0 (no engine code in the driver). Phase 3 of
[ati-rage128.md](../plans/ati-rage128.md), first step. Evidence:
[`../probe/a8u4i5-rage128-2d-probe-2026-10-22/`](../probe/a8u4i5-rage128-2d-probe-2026-10-22/).

## What was built

- `src\chipsets\ati\rage128\r128_2d.c` with `include\velocity9x\r128_2d.h`:
  pure builders for the engine's init list, a solid fill and a screen
  copy, from xf86-video-r128's `r128_accel.c` and `r128_exa.c`.
  - Every operation sets its own `DST_PITCH_OFFSET` (and
    `SRC_PITCH_OFFSET`), so a surface may sit anywhere in memory.
  - A start that is not 32-byte aligned has its remainder moved onto x.
  - A pitch that is not a whole number of 8-pixel units is refused, and
    goes to the CPU.
  - The init list opens the scissors. POST leaves the default scissor at
    the mode's size
    ([register probe](2026-10-22-rage128-register-probe.md)), which would
    clip anything below the visible screen.
  - `tests\host\test_r128_2d.c` holds the encodings: the init list,
    fills on the primary and on an unaligned surface, copies in each
    overlap direction and between surfaces, the refusals, and the status
    decodes. It was seen to fail on a wrong expectation before it was
    trusted.
- `R1282D.EXE` + `R1282D.VXD` (`tools\diag\r128_2d*`,
  `scripts\build-r128-2d.ps1`): the write probe, in the shape of the
  Voodoo4's TDFX2D. The VxD runs op lists and may write only
  `PC_NGUI_CTLSTAT`, the 2D registers 1400h-16FCh, `SCALE_3D_CNTL` and the
  framebuffer. It checks the register BAR's configuration copy at 0F00h
  first. The EXE refuses unless `PM4_BUFFER_CNTL` is NONPM4. It writes the
  init list once, then draws on a 4 KiB surface at VRAM 16 MiB: off the
  desktop and above the 16 MiB the tier-0 driver maps. Each test compares
  every byte with a C model. "Idle" is r128_accel.c's: 64 free FIFO slots,
  `GUI_ACTIVE` clear, then the pixel cache flushed and its busy bit clear.

## Results

| Desktop | Fill | Fill, surface 16 bytes past 32 | Copy forward | Copy right (overlap) | Copy down (overlap) | Copy down and right (overlap) |
|---|---|---|---|---|---|---|
| 800x600x8 | PASS | PASS | PASS | PASS | PASS | PASS |
| 800x600x16 | PASS | PASS | PASS | PASS | PASS | PASS |
| 1024x768x32 | PASS | PASS | PASS | PASS | PASS | PASS |

Every test: 0 mismatching bytes in 4,096. Idle took 5 to 7 status reads
before and after each operation. No wait timed out. `GUI_STAT` read
`00000040` at the start and the end of each run. Depth changes were made
live with V9XMSW, and the desktop went back to 800x600x16.

The primary pitch the probe read from `CRTC_PITCH` was 800, 1600 and
4096 bytes, matching each mode.

## What this licenses

The HAL engine may write these builders' values for DirectDraw fill and
copy at 8, 16 and 32 bpp, after the init list and with the engine in
NONPM4. Not measured: rectangles larger than 24x10, the visible screen,
copies between two surfaces, the engine under load or interleaved with CPU
writes, anything after ATI's driver has run the CCE in the same power
cycle, and engine reset.

Gates: `build-host.ps1` and `run-checks.ps1` passed.
