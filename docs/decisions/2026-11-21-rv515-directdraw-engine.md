# RV515 DirectDraw fill and copy on the RS690 engine: first run on M-5CAPM3

Date: 2026-11-21
Machine: remote-help `M-5CAPM3`, Radeon X1300 `1002:7146`, boot 38,
800x600x16 (the owner changed the desktop from 640x480 during boot 37;
V9XBOOT.INI `w=800 h=600`).
Build: ati `c4ef4973` (branch `ati-rv515`), DRV, VXD, HAL, SETP and GL
by WININIT rename over the `e25a096f` tier-0 install (that package stays
in `C:\V9XATI` for rollback). Staging folder empty after the reboot;
DRV, VXD and HAL hashes matched the package.
Reports in
[`docs/probe/m-5capm3-rv515-2026-11-21/engine-boot38/`](../probe/m-5capm3-rv515-2026-11-21/engine-boot38/).
Plan: [`ati-rv515-x1300.md`](../plans/ati-rv515-x1300.md), phase 3.

## What changed

- The RV515 row has an engine hook: BAR2 by `V9xPciReadAtiMmioBar`,
  mapped by the mini-VDD's RS690 map, engine type `ATI_RS690`, caps
  `SOLID_FILL | SCREEN_COPY`. The hook body is in LEAFCODE
  (`rv515_engine16.c`): in `_TEXT` it put the ati driver 155 bytes over
  budget; `_TEXT` is now 57,293 of 57,344 bytes.
- `eng_rs690.c` writes its 3D-core init only for a row whose caps include
  D3D. The RS690 row has D3D, so it is unchanged; it was not re-run on
  the RS690 for this change.

## Boot 38

- `V9XHW.INI`: `Acceleration=directdraw-fill-copy`, `EngineCaps=00000003`,
  `GdiAcceleration=none`, `Direct3DMode=none`, `ScreenShadow=on`.
- `V9XDDP` (`c4ef4973`): `Result=COMPLETE`, DirectDraw half only (329
  keys; with no Direct3D the D3D half does not run). `BltFillPixelOk`,
  `SrcCopyPixelOk`, `KeySrcFillPixelOk` and the four overlap checks
  (down, right, pitch-down, pitch-right) are 1. `FlipPixelOk` is 0, as in
  the software-engine run on boot 32; the row does not claim FLIP.
- `V9XTRACE` after it (`V9XSNA1.INI`): driver and HAL both
  `0.60.0 c4ef4973`, `EngineType=14` (ATI_RS690), `EngineFlags=1`
  (validated), `CountBltEngine=7`, every timeout counter 0,
  `FlipDeclined=23`.
- `V9XGDI /auto` PASS; `V9XMSW /cycle:4` and `/depth:4` PASS 4/4.
- `V9XDDP` again after those mode and depth changes: the same DirectDraw
  results. `V9XSNA2.INI`: `CountBltEngine=14`, still validated, timeouts
  0. The engine re-validates per mode, as on the RS690.
- `BootCounter` stayed 38 from the deploy to the last snapshot. Boot 37
  was the owner (remote help paused 21:55, restart 21:57 guest time).

## Not covered

- Games or 3DMark: only V9XDDP's blits went through the engine (14 in
  all), so throughput and sustained load are unmeasured.
- 8 and 32 bpp through the HAL: V9XDDP ran at 16 bpp (800x600). The builder was
  byte-exact at all three depths in the write probe.
- FLIP through D1GRPH, and Direct3D.
- A DOS box or standby with the engine live.
