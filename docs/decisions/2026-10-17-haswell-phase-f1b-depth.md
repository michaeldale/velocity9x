# Haswell Phase F1b: a D32_FLOAT depth buffer, cleared by drawing, resolves three overlapping triangles

2026-10-17, i5-4570 (`8086:0412` rev 06), Windows 98 SE, boot 9, with the
B2 driver on the desktop. `V9XHSWMM.EXE /depth`, part of Phase F in
`docs\plans\intel-haswell-direct3d.md`. Evidence:
`docs\probe\hsw-4570-depth-f1b-2026-10-17\`.

## What ran

One draw batch (224 dwords) into target 0 with a 64x64 D32_FLOAT depth
buffer in target 1's 16 KB.

- **Depth buffer setup.** Before the depth state changes: a depth stall,
  a depth cache flush and a depth stall (Vol 2b's restriction). Then
  `3DSTATE_DEPTH_BUFFER` (2D, depth write, D32_FLOAT, pitch 256, 64x64),
  and `3DSTATE_HIER_DEPTH_BUFFER`, `3DSTATE_STENCIL_BUFFER` and
  `3DSTATE_CLEAR_PARAMS` all off. SF carries the same depth format.
- **Clearing.** Gen7 depth buffers are always Y-tiled. The buffer is
  cleared by drawing: one triangle covering the target at z = 1.0, black,
  under a DEPTH_STENCIL_STATE of ALWAYS with write.
- **The scene.** The DEPTH_STENCIL_STATE pointer is switched to LESS with
  write, and one 9-vertex TRILIST draws, in order, A (z 0.5, red), B
  (z 0.25, green, overlapping A) and C (z 0.75, blue, overlapping both).
  Positions are R32G32B32_FLOAT, so z reaches the depth test with the
  viewport transform off.

## Result (two runs, the same)

Both draws' stores landed, EIR 0, and `DepthVerdict=pass`:

| | pixels |
|---|---|
| judged | 3986 |
| within 1/64 px of an edge, not judged | 110 |
| mismatches | 0 |
| expected (and found) clear, black | 1397 |
| A, red | 1264 |
| B, green, where it is nearer than A | 837 |
| C, blue, only where neither A nor B covers | 488 |

Afterwards the RCS ring registers read 0, the BCS ring was idle and
enabled, forcewake was held, and a V9XDDF fill and copy passed.

## Findings

- Depth test and write work with a linear-addressed allocation used as a
  Y-tiled depth surface (pitch 256, 16 KB, 4 KB aligned).
- A depth-stencil pointer change between two `3DPRIMITIVE`s takes effect
  for the second, with no flush between them.
- Clearing by drawing at z = 1.0 needs no knowledge of the tiled layout.
  The HAL can clear depth the same way until `3DSTATE_CLEAR_PARAMS` or a
  HiZ clear is measured.

## Not measured

- HiZ, stencil, D16 and D24 formats, depth bias, depth larger than 64x64
  (tile layout under a pitch that is not a whole number of tiles).
- Reading depth back.
- Depth on the pixel-centre ties along shared edges.

## Next

F2: the `V9X_D3D_ENGINE_OPS` table for Haswell in the HAL, built on these
batches. It needs its own design.
