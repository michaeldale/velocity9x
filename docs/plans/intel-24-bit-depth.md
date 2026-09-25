# A 24-bit depth buffer for the Intel engine

Drafted 2026-09-25 after 3D WinBench 98's Z-accuracy tests failed on the
netbook (`docs\decisions\2026-09-24-3d-winbench-98-quality-on-the-netbook.md`,
pass 3). Not started; for review.

## Diagnosis

Both failures are what a 16-bit screen-space Z buffer gives, not a defect
in how this driver produces Z:

- The Intel path stores depth as `DEPTH_FRMT_16_FIXED`
  (`include\velocity9x\intel_gen3_3d.h`), and each vertex's Z is Direct3D's
  `sz` passed straight into the XYZW position (`d3d_i9xx.c`, the vertex copy
  loop). That is the ordinary Z buffer, interpolated linearly in screen
  space, 1/65536 steps.
- **Wide Z Accuracy** puts geometry near the far plane, where projected `sz`
  crowds towards 1.0. At 16 bits the two cubes' depths collapse together and
  one disappears - the same as the benchmark's bad reference, which is the
  RGB emulator's own 16-bit Z.
- **Narrow Z Accuracy**: a sawtooth along one intersection, milder than the
  bad reference - quantisation along a shallow depth difference.

Not established: that the netbook's result is the 16-bit ideal. A software
reference (d3d_raster.c at 16 bits over the same geometry) would settle
whether there is also a precision loss of our own - the benchmark's
geometry is not available, so it would have to be reconstructed. The
benchmark's own note says "Z buffer depth: 16 bits", so a deeper buffer
was never on offer.

## The remedy the hardware has

Gen3 has `DEPTH_FRMT_24_FIXED_8_OTHER` (24-bit depth in a 32-bit pixel,
eight bits of stencil or unused), 256 times the resolution.

Evidence that it pairs with a 16-bit colour target, which this driver
renders into:

- Mesa 21.3 classic i915 `i915_vtbl.c:611-616` selects the depth format from
  the depth region's cpp alone, independent of the colour format.
- Its FBO validation (`intel_fbo.c`) does not reject a colour/depth cpp
  mismatch.
- But its window configs (`intel_screen.c:1073-1079`) pair RGB565 only with
  16-bit depth. Convention or hardware rule - not established.

So 565 + Z24 is plausible and **unmeasured**; the first step measures it.

## What it touches

The whole stack assumes 16-bit depth today:

1. `ddhal_core.c` publishes `ddCaps.dwZBufferBitDepths = DDBD_16`,
   engine-neutrally. Needs to come from the engine (DDBD_16 | DDBD_24 or
   DDBD_32 - which one DX5 applications ask for is to be checked).
2. Each engine's `dwDeviceZBufferBitDepth`; the Intel one gains the deeper
   format, the ViRGE and software engines stay 16.
3. `V9X_D3D_ENGINE_LIMITS.depth_bits_per_pixel` is one value per engine and
   drives Z-surface validation and depth arithmetic in `d3d_core.c`
   (`v9x_d3d_depth_bytes_per_pixel`, the Z surface checks). Needs to become
   per surface: the bytes per pixel of the attached Z surface, checked
   against the set the engine allows.
4. The depth-fill blit (DDBLT_DEPTHFILL) at 32 bits per pixel, and what fill
   value means "far" for a 24-bit format in a 32-bit pixel.
5. Intel: `DST_BUF_VARS` carries the depth format; the depth BUF_INFO pitch
   is already the surface's own. The runtime builder takes the depth format
   as a parameter and the allowlist decoder holds it to what the limits
   declare, as S3 and S6 are held now.
6. Host tests for each, including the decoder refusing a 24-bit format the
   limits did not declare.

## Order

1. **Measure 565 + Z24 on the netbook first**, before any caps change: an
   armed diagnostic or the Phase 6 depth scene built with a 32-bit depth
   surface and `DEPTH_FRMT_24_FIXED_8_OTHER`, read back. If the part refuses
   or corrupts, stop here and record it.
2. Items 3-6, host-tested, with caps unchanged.
3. Items 1-2: publish the deeper format.
4. Re-run 3D WinBench 98's Z-buffer, Narrow and Wide Z Accuracy tests, and
   record which Z depth the benchmark now reports choosing. If it still
   chooses 16 bits, the test cannot show the gain and a DX5 title that
   prefers deeper Z is needed as the measurement instead.
