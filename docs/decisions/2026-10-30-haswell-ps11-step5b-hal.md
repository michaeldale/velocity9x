# Pixel shader 1.1 draws in the Haswell HAL: V9XD32 T19 passes through Direct3D 8

Date: 2026-10-30. Step 5b of
[`../plans/haswell-ddi8-pixel-shaders.md`](../plans/haswell-ddi8-pixel-shaders.md).
Machine: the Dell i5-4570 (HD 4600), boots 124-128, builds from 363958c5
plus this change. Evidence:
[`../probe/hsw-4570-ps11-step5b-2026-10-30/`](../probe/hsw-4570-ps11-step5b-2026-10-30/).

## What changed

- **The engine reserve grew from 256 to 320 KB** for a pixel-shader
  kernel cache at 40000h: 16 slots of 4 KB.
  - `intel_hsw.h`, the mini-VDD's `V9X_HSW_RESERVE_BYTES`/`_PAGES` and
    V9XHSWMM's `HSW_RCS_RING_BELOW` (4E000h) changed together.
  - `check-tree.ps1` now ties the three; it refused a stale probe value
    when tried.
  - On boot 124 the reserve sat at 01EA0000, 64 KB below boot 120's, with
    both rings enabled. V9XD32 passed at DDI 7 and 8.
- **The wide vertex is Direct3D's screen-space vertex.** At DDI 8 the
  runtime transforms vertices itself, so a shader draw arrives with sz and
  rhw. 5a's wide vertex (x y, 104 bytes) became sx sy sz rhw (112 bytes),
  drawn with the TL path's CLIP state. Depth (under depth_test) and
  scissor are allowed. V9XHSWMM /ps11, updated to the new vertex, still
  passed all six programs on boot 125.
- **The core** (`d3d_ps.c`, pure, `test_d3d_ps.c`) keeps:
  - a shader table: CREATEPIXELSHADER parsed by `v9x_ps11_parse`, keyed by
    context and handle, each program with a serial;
  - SETPIXELSHADERCONST's c0-c7;
  - stages 0-3's bump matrix, luminance terms, TEXCOORDINDEX, filters and
    address modes.
- **The draw path.** A DDI 8 draw under a shader goes through
  `v9x_dp2_draw` with its own sink. The sink turns each triangle into wide
  vertices and hands batches of 96 to a new engine op, `draw_shader`,
  appended to `V9X_D3D_ENGINE_OPS`.
- **Haswell's `draw_shader`:**
  - maps the draw's fixed-function state as any draw's;
  - makes each sampled stage a texture unit;
  - writes a five-entry binding table, four surfaces and samplers and the
    push block in the slot's spare state space;
  - takes the kernel from the cache, tagged in the status page as the
    vertex kernels are;
  - submits through `v9x_intel_hsw_draw_workload_ps11`.
- **Caps.** PS 1.1 and four textures are claimed to DDI 8 when the engine
  has `draw_shader`: V9XD32's T17 reads PixelShaderVersion FFFF0101 and
  four stages (boot 126). Cube maps stay behind the measurement switch,
  now `V9X_D3D_CUBE_CLAIMED`.

## The gate: V9XD32 T19

T19 runs at the end of V9XD32. It makes a windowed HAL device with a
lockable back buffer in the desktop's format and four 8x8 managed
textures. It then draws one triangle under each of four PS 1.1 programs
with DrawPrimitiveUP: Push (c0, c5, c7, v0, v1), FourTex (four samplers),
Kill (texkill) and Bem (texbem, stage 1's bump matrix). It reads the back
buffer, judges each pixel at Direct3D's pixel centre against the
reference interpreter linked into V9XD32, and counts towards V9XD32's
Result.

The 4570's desktop is R5G6B5, so T19 learned 565. Both sides are rounded
through 565 and widened back. The tolerance is 9 (one 5-bit step plus 2),
against 2 at 32 bpp, and the record names it.

| Program | Judged | Killed | Mismatches | Max error |
|---|---|---|---|---|
| Push | 1,098 | 0 | 0 | 9 |
| FourTex | 1,024 | 0 | 0 | 9 |
| Kill | 1,039 | 335 | 0 | 0 |
| Bem | 972 | 0 | 0 | 0 |

T12-T16 passed in the same run.

**The first T19 build was wrong, and the driver said so.** Its FVF was
5C4h, which declares five coordinate sets, on 88-byte vertices of four.
The core refused every draw as a 96-byte format on an 88-byte stream
(`dx8-stride 58 60` in V9XD7.LOG). Every judged pixel kept the guard and
all four programs failed. So T19 does catch an undrawn shader. FVF 4C4h
passed.

## Fog after the shader, and 32 bpp (boot 129)

Built from e15ecca1 plus this change; DRV, HAL and mini-VDD by WININIT.

- **The kernel fogs.** With fog, r0 is saturated into the colour written,
  then `(colour - fog) f + fog`, f the interpolated specular alpha and the
  fog colour immediates in the kernel: hsw_eu.c's fixed-function rule.
  The kernel cache keys on fog and its colour. Host test first:
  `cnd fog` (channels outside [0, 1] clamped first) and `texkill fog`
  failed against the unchanged kernel (33 failures), then passed.
- **Table fog** puts its factor in each wide vertex's specular alpha in
  the core's shader sink, as `v9x_d3d_dispatch_draw` does for a
  fixed-function draw. Not host-tested: the sink is in d3d_core.c.
- **An absent specular is now black with alpha 1**, `FF000000h`, which
  d3d_dp2.c already gives the fixed-function vertex. Before, it was 0, so
  a fogged shader draw without specular would have been all fog. A
  shader reading v1.a from such a vertex now reads 1; nothing measured
  says which Direct3D gives.
- `Ps11Unfogged` became `Ps11Fogged`: shader draws made with fog on.

T19 gained PushFog (Push under vertex fog, the vertices' specular alphas
33h, E6h, 80h) and KillFog (Kill under LINEAR table fog, start 0.1, end
0.9, vertex Z 0.15, 0.85, 0.5: inside the range, where a per-vertex
factor equals a per-pixel one). Each is judged against Direct3D's rule
applied to the interpreter's result. Fog colour 0040A0E0h.

| Program | 565 max error | 32 bpp max error |
|---|---|---|
| Push | 9 | 1 |
| FourTex | 9 | 0 |
| Kill | 0 | 0 |
| Bem | 0 | 0 |
| PushFog | 9 | 1 |
| KillFog | 8 | 1 |

No mismatches in either run; tolerance 9 at 565 and 2 at X8R8G8B8 (the
desktop switched live with V9XMSW and back). T12-T16 passed in both.
Push and FourTex's 9 at 565 was rounding: at 32 bpp they are within 1.
Had fog not been drawn, PushFog would miss by far more than 9: its
factors run 0.2 to 0.9 against a fog colour unlike the program's.

## Not done, not checked

- **Alpha test with a shader, and Kill Pixel's need with depth**, are
  unmeasured. T19 has no depth buffer.
- **3DMark 2001 SE makes no shader without cube maps.** On boot 126,
  with PS claimed, it created none. On boot 127 a measurement HAL with
  cubes claimed (not committed) answered every ValidateTextureStageState
  question one pass. 3DMark still stopped before its score window and
  created none, as at step 3, so 3DMark cannot exercise this path before
  5c and 5d. The plan's "3DMark completes with PS claimed" was met only
  for the Nature and Pixel Shader selection, which ran no game test (boot
  126). A full default run is owed, as is Half-Life Direct3D.
- **Triangles past the guard band** under a shader are dropped and
  counted, not clipped: the core's clipper knows the 32-byte vertex only.
- **Cube-map stages** are refused by the engine until 5c. The core marks
  no stage as a cube yet: the surface's caps2 field has not been
  measured.
- **Points and lines** under a shader are not drawn.
