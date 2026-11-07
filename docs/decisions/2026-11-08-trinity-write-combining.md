# Write-combining Trinity's frame buffer: FbWrite 312 to 2,720 MB/s, and the 0.40.0 record that said it was already on

Date: 2026-11-08

Status: implemented for every Evergreen row (Cedar, Juniper, Redwood,
Caicos, Trinity); **measured on Trinity only**, M-D7TFW4's HD 7660D
(`1002:9901`, A10-5700), one build, one run each way. The owner had
removed the HD 8570 and put the APU's graphics back on the signed 0.40.0
`ati` package for this.

Precedents, same mechanism: [vbe](2026-11-07-vbe-write-combining.md),
[Rage 128](2026-10-24-rage128-write-combining.md) (the one with an
engine).

## What the first two runs showed

Boot 56's `V9XBOOT.INI` read `MtrrWrite=ok s=4 b=c0000001 m=ff000800`, so
the plan was an A/B of 0.40.0 with `C:\V9XDIAG\NOWC.TXT` absent and
present. Both runs gave FbWrite 312.7 and 312.5 MB/s, and boot 57's
record, with the marker in place, still said `ok`. The line was stale:
its 16 MiB mask (`ff000800`) does not match the same file's plan line,
`z=08000000` (128 MiB), and `V9XBOOT.INI`'s date was the boot before.
It is left from the HD 8570's `vbe` boot 52. In 0.40.0 no Evergreen row
sets `V9X_HW16_DEVICE.write_combine`, so `v9x_mtrr_install` returns
before writing any `MtrrWrite` key, and the old one stays. **0.40.0
writes Trinity's frame buffer uncached.** Those two runs are the 0.40.0
baseline (`20261108-083704`, `-084244`).

## The change

Every Evergreen row in `cedar_hw16.c` now opts in with
`V9X_HW16_WC_MAPPING`: Stage B over the family's 128 MiB mapping, which
lies inside BAR0 (256 MiB at C0000000 on M-D7TFW4; 256 MiB or more on
every Evergreen card). Nothing else is needed: the DMA engine
(`eng_eg.c`) and the CP (`eg_cp.c`) already drain the CPU's
write-combining buffers before each kick, and the shared Unlock and Flip
drains cover the rest. Only Trinity's row was in the measured build; the
others follow the rule that a fix measured on one chip of a design goes
to all of them.

## The A/B

Build `400cbd15-dirty` (main plus Trinity's opt-in), DRV, VXD and HAL by
WININIT rename, each SHA-256 checked. Boot 58 with `NOWC.TXT`
(`MtrrWrite=off-marker`); boot 59 with it renamed away by WININIT
(`MtrrWrite=ok s=4 b=c0000001 m=f8000800`, the 128 MiB pair).
dispbench `run-bench.ps1 -Mode 1024x768x16 -Tools DBTIME,DBGDI,DBDD`,
runs `m-d7tfw4-trinity-v9x-ati/20261108-085506` (off) and `-090054` (on).

| | 0.40.0 | off (boot 58) | on (boot 59) |
|---|---:|---:|---:|
| FbWrite MB/s | 312.7 | 312.5 | **2,719.5** |
| FbRead MB/s | 9.0 | 9.0 | 17.7 |
| FillRect 1024x768, cyc/px | | 21.42 | 2.16 |
| FillRect 64x64 | | 24.73 | 5.11 |
| FillRect 16x16 | | 74.24 | 60.05 |
| BitBlt bitmap to screen 256x256 | | 21.54 | 2.27 |
| BitBlt DIB to screen 256x256 | | 21.65 | 2.38 |
| BitBlt screen to screen 256x256 | | 795.5 | 751.4 |
| Ellipse 128x128 | | 39.73 | 20.36 |
| DirectDraw Blt system to video 512x512 | | 11.84 | 3.24 |
| DirectDraw video to video 512x512 | | 1.11 | 1.11 |

Cycles per pixel are dispbench's; lower is faster. The off build and
0.40.0 agree to the run's noise on every row, so the change is the
write-combining and nothing else in the newer build.

## What it does not change

Reads: screen-to-screen copies and video-to-system blits stay where they
were, as on the vbe machines, because write-combined memory is not cached
for reads. Engine work (video-to-video blits, 1.11 cyc/px) is unchanged.

## Not checked

The other four Evergreen chips, a game, the screen by eye, and 32 bpp.
M-D7TFW4 is left on boot 59, the measured build with write-combining on.
