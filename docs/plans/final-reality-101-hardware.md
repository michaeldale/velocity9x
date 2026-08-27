# Final Reality 1.01 hardware Direct3D

Status (2026-08-14, still current as of 2026-08-28):
hardware enumeration and benchmark submission path working on VM 9869;
subpixel accuracy, specular Gouraud, vertex fog, vertex-alpha blending,
ARGB1555 texturing, mipmapping, and trilinear filtering working. Real Z-buffer
state remains incomplete.

## 2026-08-14 baseline diagnosis

Final Reality 1.01 showed only `Direct3D Software` and reported `No Direct3D
hardware rendering platforms found!`. Its bundled `e2driver\d3d_mfc.dll`
contains explicit device-rejection paths for `failed: no zbuffer` and `failed:
no texture filters`.

The full-capability V9XDDP comparison found:

- Velocity9x: `HwTriFilter=0`, `HwZDepth=0`;
- stock ViRGE: `HwTriFilter=63`, `HwZDepth=1024` (16-bit).

The desktop and fullscreen modes were already 16-bpp, so this was not FR's
documented 32-bpp rejection case.

## Implemented

- Advertise 16-bit Z-buffer surfaces and point/linear texture filtering.
- Include Z-buffer surfaces in the DirectDraw surface capability set.
- Validate and accept an attached video-memory Z surface during D3D context
  creation and render-target changes.
- Expand V9XDDP enumeration output to record every D3D device and its complete
  hardware primitive capability block.
- Accept FR's 128-triangle RenderPrimitive batches.
- Use the full 16-bit D3D triangle-index contract instead of the former
  arbitrary 192/1024 vertex limits. FR uses one large transformed-vertex
  buffer and indices observed beyond `0x1000`.
- Clip guard-band triangles to the active viewport in software before writing
  the ViRGE's unsigned S11.20 X-start setup register.
- Add `D3dPrimitiveReject` trace events to distinguish index, input-coordinate,
  and post-clip hardware-submission failures.
- Advertise subpixel rasterization and exercise the fractional S11.20 triangle
  setup path in V9XDDP (`D3DSubpixelTriangleOk`).
- Retain `SPECULARENABLE`, `FOGENABLE`, and `FOGCOLOR` per D3D context.
- Saturating-add each transformed vertex's specular RGB to its diffuse colour,
  then apply vertex fog from the specular alpha/fog factor before the existing
  ViRGE Gouraud colour interpolation.
- Advertise specular Gouraud and vertex Gouraud fog, with independent
  pixel-verified V9XDDP tests (`D3DSpecularGouraudOk` and `D3DDepthFogOk`).
- Retain source/destination blend state and vertex alpha per context; program
  ViRGE source-alpha/inverse-source-alpha blending. Advertise only the
  supported crossfade equation, leaving additive and multiplicative alpha
  disabled in FR.
- Advertise the ViRGE's actual ARGB1555 texture format, resolve D3D texture
  handles to hardware-resident DirectDraw surfaces, and program affine U/V
  gradients and point/bilinear sampling.
- Advertise DirectDraw complex mip surfaces as well as all ViRGE mip filter
  modes. Select the mip level from the texture-coordinate derivatives and
  program the ViRGE D/DS state.
- Implement trilinear filtering as two ViRGE texture passes when fractional
  LOD is present: bilinear level N followed by bilinear level N+1 blended by
  the LOD fraction. This supplies the between-level interpolation omitted by
  86Box's current ViRGE emulation while retaining hardware rasterization and
  blending.

## Verified result

Installed build: `fr101-hardware-index16`, boot counter 145.

- FR's platform selector shows `Direct3D On-board Accelerator`.
- FR creates all hardware contexts and runs the 25-pixel benchmark.
- The focused test completes with 18.06 Kpolys/s and 0.58 marks.
- Its final 128-triangle batches return `S_OK` with no primitive-reject event.
- All FR test contexts and texture handles observed in the earlier broad run
  were destroyed cleanly.
- Final V9XDDP gate: `Result=COMPLETE`, `D3DHalFound=1`,
  `D3DCreateDeviceHr=0`, `D3DTrianglePixelOk=1`, `D3DContextCycleOk=1`, and
  `BltFillPixelOk=1`.
- Final trace: zero FIFO timeouts, idle timeouts, engine resets, or context
  rejects.

Evidence is under
`build\driver-results\fr101-hardware-index16-vm1`.

### Specular/fog verification

Installed build: `fr101-specular-fog`, boot counter 147.

- FR enables `Depth fog`, `Specular gouraud`, and `Subpixel accuracy` for the
  `Direct3D On-board Accelerator` platform.
- The focused 25-pixel hardware benchmark returns to the Advanced Options UI.
- V9XDDP reports both new state calls successful and both pixel checks equal to
  1, while retaining every prior mandatory DirectDraw/Direct3D gate.
- The post-FR trace records 6 clean context create/destroy pairs, 2,371
  primitive calls, 8 render-state calls, and zero FIFO timeouts, idle timeouts,
  engine resets, or context rejects.

Evidence is under
`build\driver-results\fr101-specular-fog-vm1`.

### Alpha/texture/mipmap/trilinear verification

Installed build: `fr101-trilinear-twopass`, boot counter 159.

- FR enables `Texture bi-linear filtering`, `Texture mip-mapping`, `Texture
  tri-linear mapping`, `Vertex alpha`, and `Alpha blending (crossfade)` for
  `Direct3D On-board Accelerator`. Additive and multiplicative alpha remain
  disabled, matching the advertised blend equation.
- V9XDDP independently pixel-verifies vertex-alpha crossfade, an ordinary
  ARGB1555 texture, discrete mip selection, and fractional-LOD trilinear
  filtering. The exact results are `D3DVertexAlphaBlendOk=1`,
  `D3DBaseTextureRaw=992` (green), `D3DMipmapLevelRaw=31` (blue), and
  `D3DTrilinearRaw=495` (`0x01ef`, half green/half blue).
- The focused FR 25-pixel run completes at 28.54 Kpolys/s, 0.91 R-marks, and
  74.07% visual appearance.
- The post-FR trace records 6 hardware context creates and 2,311 hardware
  primitive calls, with zero FIFO timeouts, idle timeouts, engine resets,
  context rejects, or primitive rejects.

Evidence is under
`build\driver-results\fr101-trilinear-twopass-vm1`.

## Remaining correctness work

FR visual appearance is now 74.07%. The remaining major correctness gap is
that the driver validates the attached Z surface without programming Z
coordinates, comparison mode, or updates. The next implementation slice is:

1. retain Z enable, write-enable, and comparison render states per context;
2. program Z_BASE/Z_STRIDE and Z gradients for transformed vertices;
3. add pixel-verified depth-test and depth-write probes;
4. rerun the Robots and City scene tests after the Z gate passes.
