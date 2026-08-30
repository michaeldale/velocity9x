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

FR visual appearance was 74.07% when the driver validated the attached Z
surface without programming Z coordinates, comparison mode, or updates. Steps
1-3 of that slice are done and measured on the guest — see
[`2026-08-30-virge-depth-fifo-reservation.md`](../decisions/2026-08-30-virge-depth-fifo-reservation.md):

1. ~~retain Z enable, write-enable, and comparison render states per
   context;~~ done.
2. ~~program Z_BASE/Z_STRIDE and Z gradients for transformed vertices;~~ done.
3. ~~add pixel-verified depth-test and depth-write probes;~~ done. Both
   ladders pass on `Win86SE` with zero FIFO timeouts and zero engine resets;
   the baseline pixel results are unchanged.
4. ~~rerun the Robots and City scene tests after the Z gate passes.~~ done,
   see below. The procedure this plan never recorded is now
   [`final-reality-101-runbook.md`](../specifications/final-reality-101-runbook.md).

### Robots and City scene, with hardware depth working

Installed build: `zfifo-001`, boot counter 498, desktop 1024x768x16.
Evidence under `build\driver-results\fr101-zfifo-vm1`.

All four 3D tests, five repeats, 2D and bus-transfer tests cleared. 14.5
minutes wall clock. Robots and City scene produce numbers for the first time;
every earlier round left them `n/a`.

| Test | Raw speed | R marks |
|---|---|---|
| 25 pixel | 23.62 Kpolys/s | 0.76 |
| **Robots** | **9.45 images/s** | **2.45** |
| Fill rate | 67.74 Mpixels/s | 14.66 |
| **City scene** | **11.46 images/s** | **2.84** |
| Visual appearance | 74.07 % | - |
| **3D performance** | | **1.97 Reality marks** |

The driver carried it without a single engine fault. Post-FR trace:

```
EngineFifoTimeouts=0  EngineIdleTimeouts=0  EngineResets=0
D3dContextCreates=8   D3dContextDestroys=8  D3dContextRejects=0
D3dRenderPrimitiveCalls=2697602   D3dRenderStateCalls=2674296
D3dTextureCreates=808
D3dDepthOffered=5     D3dDepthAccepted=5    D3dDepthRejectName=accepted
D3dDepthCaps=0x10026000  D3dDepthOffset=0x0012C000  D3dDepthPitch=1280
```

FR attached a depth surface five times and the driver accepted all five;
`CountAddAttachedSurface=5` agrees. Pitch 1280 is 640 x 2, matching FR's
fullscreen 640x480, and the offset sits above the front and back buffers.
2.7 million primitive calls with depth testing live and zero FIFO timeouts is
what says the reservation fix of
[`2026-08-30-virge-depth-fifo-reservation.md`](../decisions/2026-08-30-virge-depth-fifo-reservation.md)
holds under real load rather than only on a seven-rung ladder.

**25 pixel fell from 28.54 to 23.62 Kpolys/s, and that was predicted.** The
28.54 was measured when the driver advertised depth and did none, so its
triangles paid for no depth registers, no depth reads and no depth writes.
The deferred `DDBLT_DEPTHFILL` item below says a per-frame software depth
clear "will show in FR's polygon rate, which is exactly what step 4
measures"; it now has. How much of the 17% is the clear and how much is the
depth work itself is **not** separated by this run - the two are only
separable by implementing the depth fill and re-running. Note also that the
28.54 came from a 25-pixel-only run while this one runs four tests in
sequence, so the two are not perfectly matched.

> **Separated on 2026-08-30, and the answer is "none of it".** The depth fill
> was implemented and a controlled A/B run
> ([decision](../decisions/2026-08-30-ddblt-depthfill.md)): 25 pixel goes
> 23.42 without it to 23.35 with it, which is no change. The whole 17% was the
> depth work itself. The paragraph above hedged toward the clear being part of
> it; it was not. The same A/B found Robots +22.6% and City scene +36.2%
> against Fill rate -37.8%, with `3D performance` flat at 1.97 to 1.96 - so
> serving the clear is a trade-off between scene types rather than a win, and
> why the fill rate falls is not established.

**Do not read anything into `Visual appearance` staying at 74.07 %.** It was
74.07 before the depth work and 74.07 after, and FR's own built-in ViRGE
reference entry also reads 74.07. The runbook sets out why it is most likely
a capability-derived score rather than an image comparison, and what would be
needed to prove that.

## Deferred, deliberately

- ~~**`DDBLT_DEPTHFILL`.** No depth-fill path exists in `src\`; DirectDraw
  emulates the clear on the CPU, so correctness does not depend on it — but a
  per-frame software depth clear will show in FR's polygon rate, which is
  exactly what step 4 measures. Worth doing before reading too much into a
  regression there.~~ **Done and pixel-verified, 2026-08-30**
  ([decision](../decisions/2026-08-30-ddblt-depthfill.md)); the re-run that
  separates the clear from the depth work is recorded above.
- **The `DEST_BASE` 8-byte alignment hole**, the same shape as the `Z_BASE`
  one the depth work closed, and predating it.
- **Depth gradients, still unverified.** Every vertex in both probe
  ladders carries the same `sz`, so `dZdX`/`dZdY` are written but never
  checked against a slope; `D3DZGradientTested=0` says so in every result
  file. 86Box doubles a triangle's start depth but not its per-pixel X
  gradient (`build\reference-vid_s3_virge.c:4261` against `:4413`), so a
  sloped pixel test on this guest would measure that inconsistency rather
  than the driver. The FR run above did drive them - its scenes are sloped,
  across 2.7 million primitives, and nothing faulted - but "it did not
  fault" is not "it computed the right depth", and `Visual appearance`
  cannot close that gap, for the reason given above. The honest position is
  that the gradients are now exercised and still unverified; closing it
  needs either a 86Box fix or a second ViRGE target.
