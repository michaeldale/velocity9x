# RV280 2D engine: the RS690 builder is exact, with the pre-R300 flush

Date: 2026-11-15

Machine: M-HNBJNV, Radeon 9250 (`1002:5960`), Windows 98 SE. Phase 3 of
[`../plans/ati-rv280-radeon-9250.md`](../plans/ati-rv280-radeon-9250.md),
after the [register probe](2026-11-15-rv280-register-probe.md).

## The write probe

Tool: `V9XR22D.EXE` + `R22D.VXD` at `19ec1531-dirty`
(`tools/diag/rv280_2d.asm`, `rv280_2d_win32.c`). It is a copy of the
RS690's V9XRS2D, with these changes:
- the RV280's ids;
- `MC_FB_LOCATION` (0148h) for the frame buffer's GPU address, where
  zero is accepted;
- the destination-cache flush through `RB3D_DSTCACHE_CTLSTAT` (325Ch,
  `RB3D_DC_FLUSH_ALL` 0Fh, busy bit 31), the pre-R300 branch of
  xf86-video-ati 6.14.6's `RADEONEngineFlush` (`radeon_accel.c:195-210`).
  The RS690's flush register, 1714h, is the R300 one.

The register values themselves are the RS690 builder's,
`src/chipsets/ati/rs690/rs690_2d.c`, compiled in unchanged.

The method is the RS690's:
- write the init list once on an idle engine;
- draw on a 4 KiB surface at frame-buffer offset 16 MiB, above everything
  VBE reports;
- wait for idle and flush;
- compare the readback with a C model, byte for byte.

Evidence: [`../probe/rv280-2d-write-probe-2026-11-15/`](../probe/rv280-2d-write-probe-2026-11-15/).

| Test | 16 bpp | 32 bpp | 8 bpp |
|---|---|---|---|
| Fill | PASS | PASS | PASS |
| FillUnaligned | PASS | PASS | PASS |
| CopyForward | PASS | PASS | PASS |
| CopyRight (overlap, right to left) | PASS | PASS | PASS |
| CopyDown (overlap, bottom to top) | PASS | PASS | PASS |
| CopyDownRight | PASS | PASS | PASS |

Every run read `CP_CSQ_CNTL` = `02010080`, mode 0, and `FbGpuBase` = 0.
The engine needed no `RBBM_SOFT_RESET` first.

## In the driver

The `rv280` row, on branch `ati-rv280`:
- **16-bit row:** `rv280_hw16.c` maps BAR2 through the RS690's mini-VDD
  call and stamps engine type `ATI_RV280` (17) with fill and copy. GDI
  stays in the screen shadow.
- **HAL module:** `eng_rv280.c` is the RS690 module without its 3D
  setup. It uses the RV280's flush, `MC_FB_LOCATION`, and the BAR0 window
  size from `CONFIG_APER_SIZE`.

Installed on M-HNBJNV as the ati package, all eight files by WININIT
renames, hash-checked; boot 18:
- **`V9XHW.INI`:** `Adapter=ATI Radeon 9250 (RV280)`,
  `Acceleration=directdraw-fill-copy`, `EngineCaps=00000003`,
  `ScreenShadow=on`; `Stage=enable-ok`.
- **V9XDDP:** `Result=COMPLETE`. Fill, source copy, colour-keyed fill and
  the four overlap copies are all `PixelOk=1`.
- **V9XTRACE:** `EngineType=17`, `CountBltEngine=7`, no FIFO or idle
  timeouts, and `BuildsAgree=1`. V9XDDP's blits ran on the engine.
- **V9XFBCMP:** PASS.
- **V9XWPRS:** PASS. DirectDraw presents average 0.90 ms into a 512x384
  window.

## Not checked

- Nobody looked at the monitor.
- Flips: V9XDDP's `FlipHr=0` with `Flip20Ms=0`. The flip is not
  implemented for this row and does not reach the CRTC (phase 4).
- **Direct3D is `none` on this build.** The ati family defaults to
  hardware Direct3D, which resolves to none on a chip without a 3D
  engine. The `vbe` package gave this card software Direct3D. Until
  phase 5, `[Velocity9x] Direct3D=2` in SYSTEM.INI gives the software
  engine.
- Games, the DOS box, and the 9200SE.
