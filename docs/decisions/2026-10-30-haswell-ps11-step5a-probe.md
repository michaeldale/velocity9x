# Pixel shader 1.1 kernels on the HD 4600: the step 5a probe passes

Date: 2026-10-30. Step 5a of
[`../plans/haswell-ddi8-pixel-shaders.md`](../plans/haswell-ddi8-pixel-shaders.md).
Machine: the Dell i5-4570 (HD 4600, 8086:0412), boots 120-123, driver set
105059c8 running the desktop. Tool: `V9XHSWMM /ps11:0`, `/ps11:1` and
`/ps11:2`, built from 3d64acfc plus this change (`3d64acfc-dirty`). The
HAL is unchanged. Evidence: [`../probe/hsw-4570-ps11-step5a-2026-10-30/`](../probe/hsw-4570-ps11-step5a-2026-10-30/).

## What the probe does

Each run draws two PS 1.1 programs, one per 64x64 target, over Phase D's
triangle. Each kernel is built by step 4's translator
(`v9x_intel_hsw_ps11_build`). The batch comes from a new builder,
`v9x_intel_hsw_draw_workload_ps11`, host-tested in
`test_ps11_workload`. It sends:

- `CONSTANT_PS`, buffer 3, seven GRFs, after a zero-vertex `3DPRIMITIVE`,
  as the VS constants are sent;
- `3DSTATE_PS` with Push Constant Enable (DW4 11), five binding entries
  and one sampler group, setup data from GRF 4;
- `3DSTATE_WM` with Pixel Shader Kill Pixel (DW1 25) when the program
  has texkill;
- the SBE reading six attributes (offset 1, length 3);
- a 104-byte vertex: x y, then six four-float attributes, as eight
  vertex elements.

The textures are four 8x8 B8G8R8A8 2D textures and one 8x8 linear cube.
The cube's surface state comes from `v9x_intel_hsw_surface_cube`:
`SURFTYPE_CUBE`, Surface Array Spacing LOD0, all six face enables, the
faces eight rows apart. Sampling is nearest, CLAMP, cube control mode left
at 0.

Every covered pixel is judged against the reference interpreter
(`d3d_ps11_ref.c`, linked into the probe). It gets the same attributes,
weighted by the edges opposite each vertex, and point samples of the same
texels. The cube lookup uses the face table Direct3D and OpenGL share. A
pixel is not judged when:

- it is within 1/16 pixel of an edge;
- a lookup lands within 1/64 texel of a texel edge or a cube face edge;
- a texkill coordinate is within 0.001 of zero.

A judged pixel fails if any channel differs by more than 2. A killed pixel
must keep the guard value.

## Results

| Run | Program | Judged | Killed | Mismatches | Max error |
|---|---|---|---|---|---|
| 0 | Push: `mad r1, v0, c0, c7; mad r0, v1, c5, r1` | 1,103 | 0 | 0 | 1 |
| 0 | FourTex: `tex t0-t3`, two `mul`, `lrp` by c0 | 1,015 | 0 | 0 | 0 |
| 1 | Cube: `tex t1` from the cube, all six faces | 1,040 | 0 | 0 | 0 |
| 1 | Kill: `tex t0; texkill t1; mov r0, t0` | 1,028 | 338 | 0 | 0 |
| 2 | Vspec: `tex t0`, two `texm3x3pad`, `texm3x3vspec` from the cube, all `t0_bx2` | 958 | 0 | 0 | 0 |
| 2 | Bem: `tex t0; texbem t1, t0` (stage 1's matrix pushed) | 967 | 0 | 0 | 0 |

All three runs completed both draws (`StoresSeen=1FF`). Each gave the
driver's render ring back at its own START, `01EB2000`.

What this settles, from the plan's list:

- **Setup data after seven push-constant registers.** Push and Bem read
  c0, c5, c7 and stage 1's bump matrix from GRFs 4-8. Every program
  reads its attributes from GRF 11 = 4 + 7. The hardware puts the setup
  data straight after the pushed registers, with no rounding.
- **The SBE with six attributes.** v0, v1 and t0-t3, all four components,
  including the w the texm3x3 eye vector rides in.
- **Four binding entries and samplers.** FourTex reads stage N through
  entry 1 + N and sampler N for all four stages.
- **The three-coordinate SIMD8 sample** of a cube, both for `tex` and
  for `texm3x3vspec`'s reflection vector. The face selection and
  orientation match the shared table. The single-level linear cube layout
  (faces side rows apart) is the one the sampler reads.
- **A predicated EOT write.** Killed pixels keep the guard and kept ones
  are written. The killed region is one solid third of the triangle, 338
  pixels, so dispatches with all eight pixels killed almost certainly
  occurred; the probe does not count dispatches. The draw retired.

## The judge has teeth

A temporary build (not committed) gave the translator a setup register one
too high (12). Push, FourTex, Cube and Kill failed on every judged pixel
(1,103, 1,015, 1,040 and 1,028 mismatches; first bad pixel at 8,8). Both
draws still completed.

## Kill Pixel: unmeasured here

A second temporary build left `3DSTATE_WM`'s Kill Pixel bit clear. The
Kill program still passed: 338 killed, no mismatches. The probe has no
depth buffer, and the bit governs how the WM orders depth and stencil
against a shader that can discard. So this run says nothing about whether
the bit matters, and nothing about alpha test with depth (spec section 8).
The builder sets the bit for texkill, as the PRM requires. Whether the
HAL's alpha-test draws need it is left for 5b, which draws with depth.

## Two faults in the probe, found and fixed on the way

1. **The VxD borrowed the render ring only at `01802000`.** That is
   where the fallback reserve kept it. The driver's reserve is now at
   `01EB0000` (heap plan), so the ring is at `01EB2000`, and the VxD
   refused it (`RING_WAS_ON`, stage 4; run 1, boot 120). Nothing was
   drawn and nothing was touched. The VxD now also accepts the ring at
   the region less `3E000h` (the reserve's 40000h less the ring's offset
   2000h). It gives back whichever START it found, instead of a constant.
   A refusal now records the START, HEAD and TAIL it saw.
2. **The loader took `C:\WINDOWS\SYSTEM\HSWMM.VXD`**, an older build,
   before the copy in the working directory or `C:\`. Phase C recorded
   the same trap. Boots 121 and 122 ran that copy and refused the same
   way; run 2's file shows the new diagnostic fields still zero. Both
   copies were replaced, and the old one is kept in the session
   scratchpad. Boot 123 ran the new VxD.

The ring constant has to change again when the reserve grows to 320 KB in
5b: `HSW_RCS_RING_BELOW` becomes 4E000h. No tree check ties it to
`V9X_INTEL_HSW_RESERVE_BYTES` yet.

## Not checked

- Projected coordinates (texbeml on a projected stage), texm3x3spec,
  texm3x3tex, cnd and the full modifier set ran only in the host EU model.
- Fog after the shader, and alpha test with a shader, are not in the
  kernel yet (5b).
- Mipmapped and tiled cubes, and the cube control mode against
  `TEXCOORDMODE_CUBE`: this cube is single-level and linear, and the
  sampler is CLAMP (5c).
- Only nearest sampling. Bilinear is the sampler's own business and
  already measured for 2D (Phase F1a).
