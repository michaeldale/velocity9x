# Haswell 24-bit depth: D24_UNORM_X8 Y-tiled beside D16, at both colour depths

2026-10-18, the i5-4570 (HD 4600, 10.0.1.254), boots 30 (before) and 31
(this change), driver `163b034a-dirty`. Decision 1 of
[`../plans/32bpp-acceleration-and-3d.md`](../plans/32bpp-acceleration-and-3d.md)
on Gen7.5, using the core's per-surface depth size from
[2026-10-18-gen3-24-bit-depth.md](2026-10-18-gen3-24-bit-depth.md).
Evidence:
[`../probe/hsw-4570-z24-2026-10-18/`](../probe/hsw-4570-z24-2026-10-18/).

## Source

Vol 2b (HSW), extracted from the local PRM:
- 3DSTATE_DEPTH_BUFFER DW1 20:18 Surface Format: `3h D24_UNORM_X8_UINT`.
- 3DSTATE_SF's matching table also has `3h D24_UNORM_X8_UINT`, and marks
  it HSW-only.
- 3DSTATE_CLEAR_PARAMS: D24_UNORM_X8_UINT is "U24 UNORM in bits [23:0]".

Gen7 depth is always Y-tiled, as F2b built D16.

## The change

- `v9x_d3d_hsw_depth_format` and `v9x_d3d_hsw_fill_z` (`d3d_hsw_map.c`,
  host-tested, failed first):
  - a 2-byte cell is D16_UNORM and a 4-byte cell D24_UNORM_X8;
  - a fill value becomes z over 65535 or over 16777215, computed in double.
  The batch builder already fed one depth format to both DEPTH_BUFFER and
  SF.
- The draw takes the cell from the core's `V9X_R3D_DEPTH_Z24X8`, and the
  depth fill takes it from the surface (`v9x_d3d_depth_bytes_per_pixel`).
  `eng_hsw.c` hands 4-byte depth fills to it, not only 2-byte ones.
- Z placement makes a 32-bit Z buffer in whole Y tiles at width x 4.
- The engine opts in with `depth32_bits` 24 and reports DDBD_16 | DDBD_32
  Z depths.
- The 16-bit side adds DDBD_32 to `dwZBufferBitDepths` for Gen7.5 as well
  as Gen3. `_TEXT` grew 14 bytes; I9XXCODE is unchanged.

The host test's first form wanted a 24-bit midpoint within half a step.
That was wrong, not the code: at 0.5 a float's own step is one 24-bit
step, and the vertex's z is a float. It now asks for one step.

## Measured

V9XD32 at 640x480, with T5 (a quad a tenth of a 16-bit step nearer than
another):

| Boot | Run | HAL Z depths | T3 | T5 pixel | Result |
|---|---|---|---|---|---|
| 30 | `/bpp16 /z24`, `/z24` | `0400h` | - | - | `NO-Z-AT-DEPTH` |
| 31 | 16 bpp, Z16 | `0500h` | 3 of 3 | `07E0`, farther kept | PASS |
| 31 | 16 bpp, Z24 | `0500h` | 3 of 3 | `F800`, nearer drawn | PASS |
| 31 | 32 bpp, Z16 | `0500h` | 3 of 3 | `0000FF00`, farther kept | PASS |
| 31 | 32 bpp, Z24 | `0500h` | 3 of 3 | `00FF0000`, nearer drawn | PASS |

The snapshot after the four runs:
- 4 Z surfaces placed (`ZPlaced=4`), the last at `ZPlacedPitch=2560`
  (640 x 4, 20 tiles);
- 4 offered and accepted, `D3dDepthPitch=2560`;
- 8 contexts and 44 batches through the engine sink, no context rejects,
  every refusal counter zero, no breadcrumb or idle timeouts;
- builds agree.

V9XR3DP, V9XGLP and V9XGLIDP on boot 31 are identical to boot 30 apart
from counters: COMPLETE, PASS, and PASS with 0 failures.

## Findings

- **Haswell holds 24 bits of depth in D24_UNORM_X8, Y-tiled**, with
  ARGB8888 or 565 colour, and the depth-only fill clears it at the 24-bit
  full scale.
- As on Gen3 (boot 211), T5 separates the depths at both colour depths.

## Not measured

- A title choosing 32-bit Z here. 3D WinBench 98 asks for 16 bits.
- The OpenGL ICD's depth stays 16-bit; its pixel formats do not offer 24.
