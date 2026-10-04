# A8U4I5 hard-locks under V9XDDP with the SiS 6326 Direct3D engine

Date: 2026-10-05. Machine: A8U4I5, SiS 6326 card 2 (rev 0Bh), Velocity9x
`sis` with the first Direct3D engine (`d3d_sis6326.c`, working tree on
`1feb92a`), boot 212, desktop 800x600x16. Status: open, high.
Plan: [sis-6326-hardware-3d.md](../plans/sis-6326-hardware-3d.md), phase 4.
Evidence: `docs/probe/a8u4i5-sis6326-d3d-engine-2026-10-05/`.

## Symptom

`V9XDDP.EXE`, detached, switched to exclusive 640x480x16. About four
minutes in, the agent stopped answering; Michael found the machine hard
locked and reset it (boot 213).

`B212-V9XDDT.TXT`, V9XDDP's write-through step log, ends at:

    key D3DZSurfaceHr 00000000
    key D3DZSurfacePitch 00000080
    key D3DZSurfaceCaps 10024000
    key ZDepthFillHr 00000000

The next record would be `ZDepthFillRaw`, after V9XDDP locks the Z surface
and reads a word back (`v9x_probe_depth_fill`). So the lock came between
a DDBLT_DEPTHFILL that the HAL served on the SiS 2D engine (a 64x64 fill at
a 128-byte pitch) and that read. `B212-V9XDD.INI` lags the step log; it
stops at `TexM_128_1555_plain_near`.

## What ran before it

- Untextured Direct3D matched SiS's own HAL on every check:
  - `D3DTrianglePixelOk`, `D3DSubpixelTriangleOk`, `D3DTriangleShapeOk`;
  - the orientation and slot checks;
  - `D3DVertexAlphaBlendOk`.
- Every textured draw was refused. The mapping refused `wrap_either`,
  which the core starts at 1; that is fixed since. The texture-matrix
  cells read 0.
- Two steps before the depth fill, `BlendMultiply` drew with source
  factor DESTCOLOR. The engine published that factor on the datasheet's
  word; phase 2 never measured it. Its read-back
  (`BlendMultiplyRaw=F81F`) is the untouched destination.
- The depth fill was the first DDBLT_DEPTHFILL served by the SiS 2D engine.
  Before this build, Direct3D was not advertised, and every Z surface
  returned V9XDDP's not-run marker.

## Hypotheses, none tested

1. **The DESTCOLOR draw wedged the 3D engine.** The engine's bounded idle
   wait then gave up without stopping anything, and the 2D fill and the
   CPU read met a stuck memory path. The draw's own read-back fits this,
   as does an engine that drew nothing.
2. **The depth fill, or the read after it, on its own.** The 2D engine was
   measured filling at 8 and 16 bpp, but not into a Z surface or at this
   pitch.
3. **The machine.** A8U4I5 has two open hard-lock issues under other
   cards (2026-10-02 Rage IIC first engine copy, 2026-10-03 Half-Life
   additive sprites).

## Changes made without a measurement

- The engine publishes and accepts only the measured blend factors:
  ZERO, ONE, SRCALPHA and INVSRCALPHA.
- A 3D idle timeout now quarantines the engine for the rest of the boot.

Neither change is known to address the lock.

## Next

Rerun V9XDDP with the narrowed build. If the step log ends at the same
depth fill, hypothesis 1 is out. Each run risks a hard lock that someone
has to reset at the machine.
