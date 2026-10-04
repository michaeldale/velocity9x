# SiS 6326: DirectDraw fills and copies on the 2D engine, across mode changes

Date: 2026-10-05. Machine: A8U4I5 boot 209, SiS 6326 card 2 (rev 0Bh).
Driver: `sis` build `afd2154`. Evidence:
`docs/probe/a8u4i5-sis6326-engine-2026-10-05/`. Follows
[the write probe](2026-10-05-sis6326-2d-engine-writes.md); plan
[sis-6326-family.md](../plans/sis-6326-family.md), Phase 2.

## Deployed

`V9XDISP.DRV`, `V9XMINI.VXD` and `V9XHAL.DLL` from `afd2154` replaced the
`c58dfe5` tier-0 files by a WININIT rename (plain `dest=src` pairs, no
`NUL=`). Installed sizes matched the package after the warm restart.

## Measured

- **Boot 209:** `Stage=enable-ok` at 640x480x16. `V9XHW.INI` reports
  `Acceleration=directdraw-fill-copy`, `Direct3D=not-advertised`.
- **`V9XDDP` default run:** `COMPLETE`. Colour fill, and overlapping
  copies down, right and pitch-down, all pixel-correct (`BltFillPixelOk`,
  `Overlap*PixelOk` = 1).
- **The HAL's own counters after it (`V9XTRACE`):** EngineType 6
  (SIS_6326), caps 3 (fill and copy), the validated flag set, the control
  window mapped, and `CountBlt=6`, `CountBltEngine=6`. Every blit went to
  the engine. No idle timeouts, no resets.
- **Regression, same boot, in order:**

  | Run | Result |
  |---|---|
  | `V9XGDI /auto` | PASS |
  | `V9XMSW /cycle:10` | PASS, 10 of 10 |
  | `V9XMSW /depth:10` | PASS, 10 of 10 |
  | `V9XDDP /modestress` | 32 of 32 round trips |
  | `V9XMSW /set:800x600x16` | PASS |
  | `V9XDDP`, at 800x600x16 | `COMPLETE`, fill and overlap copies pixel-correct |

- **Final snapshot:** `EnableCount=24`. Every mode set ran the engine
  enable hook again, and `CountBlt=12`, `CountBltEngine=12`, with
  `EngineIdleTimeouts=0`.

`BltFillMs` was 71 against 72 on the CPU path at boot 208. That timing
covers the probe's whole fill step, not the engine alone, and is not a
throughput measurement.

## What this settles

1. The driver's enable hook brings the engine up from the BIOS state after
   every mode set, and the HAL drives it for DirectDraw fill and copy with
   correct pixels, including both overlap directions.
2. Mode changes and DirectDraw mode cycling leave the engine usable: the
   hook re-runs each time and no operation timed out.

## Not established

- **Engine throughput** against the CPU path; no benchmark was run.
- **A full-screen DOS box** (the reset path calls the same hook). Not run on
  this machine, by standing instruction.
- **8 bpp DirectDraw on the engine.** V9XDDP's fills and copies run at
  16 bpp; the write probe covered 8 bpp at the register level.
- **GDI acceleration:** still declines (`gdi_accel.c` drives only the S3
  engines).
- **The three 8 bpp low-resolution modes** DirectDraw refused at tier-0;
  not re-run.
