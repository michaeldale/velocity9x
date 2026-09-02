# The D3D clipper loses the last row and column of any full-target triangle

Date: 2026-09-01
Status: open, measured on two engines, not fixed
Component: `src\display32\d3d\d3d_core.c`, `v9x_d3d_clip_triangle`

## The defect

`v9x_d3d_clip_triangle` cuts geometry to `context->width - 1` and
`context->height - 1`. A triangle that overhangs the render target therefore
arrives at the engine with its furthest vertex at the **top edge** of the last
row, not its bottom - at `y = 63.0` on a 64-pixel-tall target rather than at
`y = 64.0`.

Both engines then sample coverage somewhere inside the pixel, so that row's
sample lies past the furthest geometry the clipper will express, and the row is
never covered. The same applies to the last column.

On a full-screen render target this is a one-pixel dark line down the right
edge and along the bottom.

## Measured, 2026-09-01

The probe draws a triangle with vertices at `(-32,-32)`, `(224,-32)` and
`(-32,224)` into a 64x64 target - overhanging by a wide margin, so the clipper
and not the geometry decides the edge - and reads four pixels.

| | Trio64, software engine | ViRGE/DX, S3D hardware |
|---|---|---|
| `D3DEdgeTopLeftRaw` (0,0) | 63488 | 31744 |
| `D3DEdgeCentreRaw` (32,32) | 63488 | 31744 |
| `D3DEdgeRightRaw` (63,32) | **0** | **0** |
| `D3DEdgeBottomRaw` (32,63) | **0** | **0** |

Guests `Win98SE-Trio64` (port 9871, `Direct3D=2`) and `Win86SE` (port 9869,
hardware mode), same `s3` binary, `Build=2fee8ce-dirty`.

**Confirmed on real hardware, 2026-09-02.** The physical machine A8U4I5, an S3
Trio3D on the `vbe` tier-0 package at `Build=1ca57e0`, reports the same shape:
`D3DEdgeCentreRaw=63488` and `D3DEdgeTopLeftRaw=63488` against
`D3DEdgeRightRaw=0` and `D3DEdgeBottomRaw=0`. That is three engines on three
machines, one of them silicon, so the cause is not an emulator artefact.

The centre and top-left keys are the control: the triangle drew, and it reached
the first row and column. Only the last ones are missing.

## Why it is filed rather than fixed

It looked at first like a property of the new software rasterizer's coverage
rule, and it is not. The ViRGE column says the S3D unit loses the same two
lines from the same clipped geometry, which places the cause in
`v9x_d3d_clip_triangle` - a chip-neutral file whose output three chips have
been validated against.

Widening the boundary to `width` and `height` is a two-character change with a
wide blast radius:

- It changes what every engine receives, so the ViRGE's own ladder has to be
  re-measured, not reasoned about.
- The software rasterizer refuses a coordinate above
  `(dimension - 1) << 4`, an overflow bound whose margin is 852,127 out of
  2^31 (see `d3d_raster.h`). Raising the clip boundary raises that too, and the
  bound has to be recomputed rather than assumed to still hold.
- `-1` may have been chosen to match something in the S3D setup path. Nothing
  in the tree says so either way, which is itself a reason not to change it on
  a guess.

## Reproducing

```
scripts\build-active-package.ps1 -Family s3
```

Push `V9XHAL.DLL` to `C:\WINDOWS\SYSTEM` and `V9XDDP.EXE` to `C:\` on the
guest, run `C:\V9XDDP.EXE`, and read the four `D3DEdge*Raw` keys from
`C:\V9XDIAG\V9XDD.INI`.

The host side has the same boundary pinned as a number:
`test_depth_full_height_interpolation` in `tests\host\test_d3d_raster.c`
asserts that a 2048-tall quad covers 2047 rows and not 2048, so changing the
clipper without changing that test fails the host gate.
