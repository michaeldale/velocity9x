# Rage 128: DirectDraw fills and copies run on the 2D engine

Date: 2026-10-22

Status: **measured, one card.** A8U4I5's Rage Fury Pro (`1002:5046`),
boot 522, ati package from `79b3c467-dirty` (the change this record
commits). Phase 3 of [ati-rage128.md](../plans/ati-rage128.md). Evidence:
[`../probe/a8u4i5-rage128-hal-engine-2026-10-22/`](../probe/a8u4i5-rage128-hal-engine-2026-10-22/).

## The change

- Engine type `ATI_RAGE128` (10), never `ATI_MACH64`. `d3d_select.c`
  already gives an unknown type no Direct3D engine. Its test gains the
  rows, and the unknown-type sentinel moves to 11.
- Mini-VDD function 23h, `V9XMINI_FN_R128_MMIO_MAP`, built into the ati
  image (`V9X_R128_MMIO`). It maps the 16 KiB register BAR once, refuses a
  moved BAR, and hands the window over only if the configuration copy at
  0F00h names vendor 1002h. `runtime.asm` gains `V9xMiniR128MmioMap`. BAR2
  comes from the existing `V9xPciReadAtiMmioBar`.
- `rage128_hw16.c` gains the engine hook (SOLID_FILL, SCREEN_COPY), and
  the manifest now says `directdraw-fill-copy`.
- `engines\eng_r128.c` validates once per mode: the configuration copy
  names ATI, `PM4_BUFFER_CNTL` is NONPM4, and the builder's init list goes
  in on an idle engine. Fills and copies then write the measured
  builder's values. Idle is the probe's: FIFO empty, engine inactive,
  then a pixel-cache flush, done only after the engine has drawn. A
  timeout gives the engine up for the mode, as on the SiS and VSA-100.

## Results, boot 522

- V9XHW: `Acceleration=directdraw-fill-copy`, `EngineCaps=00000003`.
- V9XDDP `Result=COMPLETE`, four runs. `BltFillPixelOk`,
  `SrcCopyPixelOk` and all four overlap cases (down, right, and both on a
  pitched surface) read 1 every time. V9XDDP switches to 16 bpp for its
  own work (`AfterModeBpp=16`) whatever the desktop is, so these four
  runs are all 16 bpp. 8 and 32 bpp rest on the write probe
  ([record](2026-10-22-rage128-2d-write-probe.md)), not on the HAL.
- `BltFillMs`: 105 under tier-0 (boot 519), 51 on the first engine run,
  then 0, 0 and 1. The first run includes the first validation; timings
  this coarse are not a benchmark.
- V9XTRACE snapshot after the runs: `EngineType=10`, idle, FIFO and
  ring timeouts 0, resets 0, `BuildsAgree=1`. `EngineFlags=1` (VALID
  without VALIDATED): the flags are restamped at every enable, and the
  desktop's mode came back after V9XDDP's last run.
- The engine's own registers after V9XDDP (V9XR1MM): `DP_GUI_MASTER_CNTL`
  `32CC34F3`, the builder's copy word, and `DP_CNTL` 2, the right-to-left
  direction of the last overlap case. Also `DEFAULT_SC_BOTTOM_RIGHT`
  `1FFF1FFF`, the init list's opened scissor, and `DEFAULT_PITCH` 80, set
  while V9XDDP had the card at 640x480. The drawing went through the
  engine, not the CPU path.
- V9XGDI `/auto` PASS.

## Not established

- 8 and 32 bpp through the HAL.
- GDI acceleration: the ati family's GDI path has no Rage 128 arm.
- The engine after ATI's driver has run in the same power cycle (CCE in
  mode 8): validation refuses then, and DirectDraw stays on the CPU.
  Measured only in reading, not run.
- Engine reset.

Gates: `run-checks.ps1` passed.
