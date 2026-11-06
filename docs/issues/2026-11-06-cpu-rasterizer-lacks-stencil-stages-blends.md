# The CPU rasterizer has no stencil, two texture units and five blend factors

Date: 2026-11-06. Feature gap, found in a feature review. Status: open,
nothing written, nothing measured.

## Gap

The Direct3D CPU engine (`src/display32/d3d/d3d_soft.c`, arithmetic in
`d3d_raster.c`, contract in
`docs/specifications/cpu-rasterizer-contract.md`) is the Direct3D HAL on the
VBE family and the fallback elsewhere. It has:

- no stencil buffer;
- at most two texture units;
- five blend factors (ONE, SRCALPHA, DESTCOLOR as source; ZERO,
  INVSRCALPHA as destination).

Programs that need more are refused, or fall to Direct3D's own RGB/MMX
devices.

## Feature

Extend the rasterizer, under the same host-tested contract, towards the
DirectX 7 fixed-function pipeline:

1. every D3DBLEND factor on both sides;
2. up to four texture stages with every D3DTOP operation;
3. 8-bit stencil with 24-bit Z (D24S8), all stencil operations;
4. W buffering, and 24/32-bit Z where a target allows it;
5. a fast path for the common case (one stage, modulate, Z, fog, blend) so
   the general path does not slow it.

Each item gets its pixel tests in `tests/host/test_d3d_raster.c` first.
The OpenGL ICD's missing 1.1 features are tracked separately
(`docs/plans/opengl-1.1-requirements.md`).

## Acceptance

The host tests for each item, and V9XD32 on a VBE guest with the stencil
and multi-stage checks passing instead of being refused.
