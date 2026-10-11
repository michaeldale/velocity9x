# The Voodoo4's DirectDraw fills and copies run on its 2D engine

Date: 2026-10-11. Machine: A8U4I5, boot 411. Card: retail Voodoo4 4500 AGP.
Build: branch `vsa100-tier0`, working tree on `e7d76ab` (DRV 47,922 bytes,
mini-VDD 12,332, HAL 391,680; snapshot `DriverBuild` and `HalBuild` both
`0.16.1 e7d76ab-dirty`). Evidence:
[a8u4i5-voodoo4-2d-2026-10-11/hal-boot411](../probe/a8u4i5-voodoo4-2d-2026-10-11/hal-boot411/).
Plan: [3dfx-vsa100-family.md](../plans/3dfx-vsa100-family.md), Phase 3
step 3.

## What was wired

- Engine type `V9X_DD_ENGINE_TYPE_TDFX` (8), manifest `EngineType 'TDFX'`,
  caps `SOLID_FILL | SCREEN_COPY`.
- Mini-VDD function `V9XMINI_FN_TDFX_MMIO_MAP` (1Dh): maps BAR0 + 100000h,
  4 KiB, once; withholds it if status reads all ones.
- `runtime.asm`: `V9xPciReadTdfxMmioBar` (BAR0 from config 10h) and
  `V9xMiniTdfxMmioMap`. `vsa100_hw16.c` fills the engine descriptor; no
  enable hook, since the probe needed none.
- `eng_tdfx.c`: the builder's writes, idle as three consecutive clear reads
  of status bit 9, bounded at 2M reads; a timeout invalidates the engine for
  the mode and declines to the CPU.

## Measured

- `Stage=enable-ok`; `V9XHW.INI`: `Acceleration=directdraw-fill-copy`,
  `EngineCaps=00000003`, `VramUsableBytes=33550336`.
- `V9XDDP`: `Result=COMPLETE`; fill, source copy and all four overlap cases
  pixel-correct. `FlipPixelOk=0`, the tracked GDI-readback-after-flip
  result; `FlipDeclined=23`, `FlipHandled=0`, since no FLIP cap is claimed.
- HAL snapshot after the probe: `EngineType=8`, `EngineFlags=9` (valid and
  status-validated; only `eng_tdfx.c`'s validate sets the second),
  `EngineIdleTimeouts=0`, `EngineFifoTimeouts=0`, `EngineResets=0`.
- `V9XGDI /auto` PASS; the screenshot is the desktop, drawn correctly.

## Not established

- A per-operation count of engine against CPU blits: the snapshot carries no
  such counter, so engine use rests on the validated flag and the pixels.
- GDI acceleration (`GdiAcceleration=none`; `gdi_accel.c` has no TDFX arm).
- 8 and 32 bpp through the HAL, page flips on the VSA-100's own start
  register, throughput.
