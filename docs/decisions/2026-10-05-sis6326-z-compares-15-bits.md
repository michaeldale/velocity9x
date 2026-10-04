# SiS 6326 Z: the test compares 15 bits, so depth fills are halved

Date: 2026-10-05. Machine: A8U4I5, SiS 6326 card 2 (rev 0Bh), boots 228
and 229. Evidence: `docs/probe/a8u4i5-sis6326-d3d-engine-2026-10-05/`
(`B228-SIS3D-P4Z-FILLS.TXT`, `B229-SIS3D-P4Z-FORMATS.TXT`, `B229-*`).
Follows [the Direct3D engine](2026-10-05-sis6326-d3d-engine.md) and refines
[shading and depth](2026-10-05-sis6326-3d-shading-and-depth.md), which found
Z16 writing z x 2^15.

## Why

V9XDDP's sprite-with-Z cells drew nothing on boot 228, and its ramp drew
no wall. Both depth-tested LESS at z 0.5 against a buffer V9XDDP had
depth-filled with ABCDh. On a 16-bit scale ABCDh is 0.67, so a fragment at
0.5 passes. The engine never writes a Z value with bit 15 set (phase 2), so
the question was how many bits the test reads.

## Measured

**The compare** (SIS3D `/phase4z`, boot 228). One green triangle at z 0.5,
which the engine writes as 4000h and 3FFFh on alternate pixels. LESS, Z
write off, over a buffer filled with:

| Fill | Low 15 bits | Pixels drawn (of 378) |
|---|---|---|
| FFFFh | 7FFFh | 378 |
| 7FFFh | 7FFFh | 378 |
| C000h | 4000h | 190 (the pixels written 3FFFh) |
| ABCDh | 2BCDh | 0 |
| 8000h | 0 | 0 |
| 3FFFh | 3FFFh | 0 |

**The other Z formats** (boot 229). z 0.5 with ALWAYS and Z write on,
8A04h D[21:20]:

| Format | Written |
|---|---|
| 1 (Z16) | 4000h and 3FFFh |
| 2 (not in the datasheet) | per-byte values such as 3F3Fh, 4040h, FF3Fh: 8-bit lanes |
| 3 (not in the datasheet) | 4000h and 3FFFh, as Z16 |

No format writes the full 16-bit scale.

## What this settles

1. **The depth buffer is 15 bits.** The engine writes z x 2^15 below 1.0
   and compares the low 15 bits; bit 15 is ignored.
2. **DDBLT_DEPTHFILL values are halved for this engine.** A depth fill's
   value is on the 16-bit scale (FFFFh is the far plane).
   `depth_fill_shift = 1` in the engine's limits moves it to the engine's
   scale, through `v9x_d3d_state_depth_fill` (host-tested):
   - FFFFh stays the far plane (7FFFh);
   - ABCDh stays 0.67 (55E6h);
   - a fill of 0.5 means 0.5, where a raw 7FFFh would mean 1.0.
3. **The cost: a Z buffer read through Lock shows half the value
   written.** V9XDDP's `ZDepthFillOk` compares the raw readback, so it now
   fails (it read 091Ah after writing 1234h). Because it fails, V9XDDP
   skips its second fill (ABCDh), and the sprite and ramp cells then
   depth-test against 0.07. Their failure on boot 229 follows from the
   readback test, not from the depth path. An application that clears Z
   and draws gets the right answer; one that reads Z back does not get its
   own values.

## Not established

- **What format 2 is for.** It may be a Z8 laid over 16-bit lanes; it is
  not used.
- **The sprite and ramp cells with a meaningful fill.** They were not run
  against a fill that survives V9XDDP's readback check. The 15-bit result
  above predicts they pass.
- **`FlipPixelOk`.** It fails here and failed on boot 209, before this
  engine existed, while the Rage IIC passes it. It is a DirectDraw flip
  question on the SiS, not a Direct3D one.
