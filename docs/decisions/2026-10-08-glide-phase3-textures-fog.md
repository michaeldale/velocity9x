# Glide textures, palettes, chroma key, blending and table fog draw alike on Gen3 and the software engine, and both fog through the render interface

Date: 2026-10-08
Machines: MICHAEL-NETBOOK (GMA 950, Gen3), boot 127; `Win98SE-Fast-D3D`
(86Box, software engine), boot 602.
Driver: as installed on each. GLIDE2X.DLL and V9XGLIDP.EXE built from the
Phase 3 tree.
Evidence: [`../probe/glide-phase3-probe-2026-10-08/`](../probe/glide-phase3-probe-2026-10-08/)
Plan: [glide-2x-wrapper.md](../plans/glide-2x-wrapper.md), Phase 3.

## Why

Phase 2 drew untextured triangles. NFS II SE draws almost everything
textured, from a game-managed TMU, with P_8 palettes, a chroma key,
alpha blending and table fog
([census](2026-10-08-nfs2se-glide-census.md)). The census left open
whether any engine fogs through the render interface. The ICD never sends
`fog_enable`, and Gen3 was known to fog only Direct3D.

## What was built

- **Downloads.** `grTexDownloadMipMap` records the download in
  `glide_texmem.c` and keeps a copy of the largest level's texels. The
  game's pointer is its own to reuse. A record overwritten by a later
  download loses its copy.
- **Conversion.** At draw time the current source is converted by
  `glide_texfmt.c`, again only when the palette (for P_8) or the chroma
  key changes it.
- **Where the texture goes.**
  - When `describe` says the engine samples video memory and the size and
    format fit, the conversion goes into a texture surface, as the ICD's
    are made.
  - Otherwise the CPU texels are passed.
  - Only the largest level is used; NFS II SE downloads single levels.
- **Batching.** Triangles are held in batches of up to 64 while the
  mapped state, texture and target stay the same, as the ICD holds them.
  A batch is drawn before:
  - a swap, clear, frame-buffer lock or close;
  - a change of render buffer;
  - a download or new palette;
  - a conversion of the texture it samples.
- **Fog fallback.** A fogged batch the engine refuses as unsupported is
  drawn again without fog and counted (`fog-dropped`).

## Results

Seven checks were added to Phase 2's fourteen. All 21 pass on both
engines with the same pixels.

| Check | Gen3 | Software |
|---|---|---|
| RGB565 texture, all red, point sampled, times white | F800 | F800 |
| P_8, palette entry 1 green | 07E0 | 07E0 |
| The same texture after the palette makes entry 1 blue | 001F | 001F |
| Green texture keyed on green over red: red stays | F800 | F800 |
| The same with the key off | 07E0 | 07E0 |
| White at alpha 128, (SRC_ALPHA, ONE_MINUS_SRC_ALPHA), over black | 8410 | 8410 |
| Table fog 255 everywhere, fog colour blue, over white | 001F | 001F |
| Texture uploads / fog-dropped batches | 5 / 0 | 0 / 0 |

**Both engines fog through the render interface.** Gen3 was not known to
take fog from it, and docs\STATUS.md lists no fog for the software
rasterizer. This record's evidence disputes both for this path: the fog
colour replaced the triangle's white, and no batch was redrawn without
fog. The checks do not measure the interpolation between vertices.

## What this does not cover

- Mipmapped, non-square or larger-than-hardware textures. The code passes
  CPU texels when a surface does not fit, which on Gen3 means the
  software fallback. Not exercised.
- Every chroma-key case. Only the decal-style case is exact by design
  (`glide_texfmt.h`).
- The cost of re-uploading. A texture's surface is refilled on every
  re-download and every palette or key change. NFS II SE re-downloads
  about 300 times a second (census); what that costs on the netbook is
  for the game run.
- Gamma. Still not drawn.

## Gates

`build-host.ps1`, `check-tree.ps1`, `build-glide.ps1`,
`build-glide-probe.ps1`, `run-checks.ps1`, and V9XGLIDP on both
machines.
