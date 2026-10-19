# Direct3D on the Mystique: the 2164W's engine with 16-bit Z, matching it cell for cell

Date: 2026-10-19

Status: measured on A8U4I5 (Michael's Mystique, 051A revision 02), boot
508, build `d1a6f262-dirty`. Phase 3 of
[matrox-mystique.md](../plans/matrox-mystique.md), on the strength of the
texture-interface measurements
([record](2026-10-19-mystique-texture-interface.md)).

## What changed

- **A chip capability bit, `V9X_DD_ENGINE_CAP_MGA_DEPTH32`**
  (`engine_abi.h`, 0x00010000), stamped by the 2164W's engine hook only.
  The Mystique's hook stamps `CAP_D3D` without it. This is a change to the
  shared engine ABI, in the pattern of `I9XX_MIP_STACKED`: the 16-bit side
  states what the chip has, and the HAL does not guess.
- **The selector** (`d3d_select.c`, host-tested) returns a new
  `V9X_D3D_SELECT_MGA16` for MGA with D3D and without MGA_DEPTH32. The
  test case was written first and failed.
- **`d3d_mga.c`** has a second ops table, `v9x_d3d_engine_mga16`, which is
  new external data: the same functions with limits whose `depth32_bits` is
  0. The core then refuses 32-bit Z surfaces and publishes one Z format.
  `dwDeviceZBufferBitDepth` is DDBD_16 alone without the bit. The core
  treats the table as MGA wherever it named the 2164W's (the fallback, the
  render-interface description and texture formats), with the name
  "Velocity9x Mystique".
- **FIFO waits in chunks of 32 words.** A textured trapezoid takes more
  than 32 words, and the 1064SG's bus FIFO holds 32 (spec 4-56), so the old
  single wait for the whole trapezoid could never be satisfied there. On
  the 2164W's 64-entry FIFO, the credit from the first read covers the
  second chunk, so no extra read is added there. That is by the code, not
  measured: the 2164W is not fitted.
- `millennium_hw16.c` and the manifest give the Mystique
  `Direct3D=hardware-mga1064sg` and `CAP_D3D`.

## Measured

Deployed by WININIT rename. Evidence in
[`docs/probe/a8u4i5-mystique-d3d-2026-10-19/`](../probe/a8u4i5-mystique-d3d-2026-10-19/).

- `V9XHW.INI`: `Direct3D=hardware-mga1064sg`, `EngineCaps=00000017`
  (MGA_DEPTH32 clear), `Direct3DMode=hardware`, `Stage=enable-ok`.
- **V9XDDP at 800x600x16, with `/mixed`**: COMPLETE, HAL found, hardware
  device, `D3DTrianglePixelOk=1`, `TargetsOk=6/6`, `MixedOk=1` (engine and
  CPU fallback agree on Z16 encoding: z 0.5 stored as 32768). Against the
  2164W's run on the same probe (boot 493,
  `a8u4i5-mga2164w-render-interface-2026-10-18/V9XDD*-mixed-boot493.INI`),
  all 222 `...Ok` cells are identical. Those failing on both are the
  features the MGA path does not do or a texture format it does not offer
  (4444, fog, specular, mip selection, trilinear, and others).
- **V9XD32** (exclusive mode, every pixel read back):
  - 16 bpp + Z16: PASS, `Failures=0`.
  - 32 bpp + Z16: PASS, `Failures=0`.
  - 16 or 32 bpp + Z32 (`/z24`): `NO-Z-AT-DEPTH`. The HAL device reports
    `HalZDepth=0x400` (DDBD_16 only), so no 32-bit Z is offered. That is
    the intended result.
- The textured cases pass at both depths, so the chunked FIFO wait runs on
  this chip without timing out.

## What this does not establish

- The 2164W on this build. Its selection and Z32 should be unchanged (the
  host test covers the selection), but the emit loop changed and the
  2164W is not fitted. Owed when it goes back in: V9XD32 b32z32 and
  V9XDDP.
- Games, OpenGL (the ICD) and Glide on the Mystique.
- The 1164SG.

Gates: `build-host.ps1` (selector test written first, seen failing, then
passing) and `run-checks.ps1` pass.
