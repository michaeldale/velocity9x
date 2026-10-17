# HD 4600: Glide, 3DCIF and OpenGL through the render interface

Date: 2026-10-18

Status: plan, for review. Asked for by Michael on 2026-10-18 while the
Half-Life install copied. Nothing here is built.

Target: the i5-4570 (`8086:0412`), on the `intel-gma` family's
`hd4600-hsw` chip, with the Direct3D engine of
[intel-haswell-direct3d.md](intel-haswell-direct3d.md) (Phases A-F3 done:
`d3d_hsw.c` on the neutral draw entry, D16 Z, flips). The approach
follows [sis-6326-opengl.md](sis-6326-opengl.md), which followed
[opengl-1.1-icd.md](opengl-1.1-icd.md) Phases 4-5, as Gen3, ViRGE, Mach64,
Rage IIC and the VSA-100 did.

## How the three front ends get to this engine

- **One entry.** `GLIDE2X.DLL`/`GLIDE3X.DLL` (`src\glide`), `ATI3DCIF.DLL`
  (`src\cif`) and `V9XGL.DLL` (`src\opengl`) all load `V9XHAL.DLL` and
  call `V9xRenderInterface` (ABI 5, `include\velocity9x\r3d_abi.h`).
  Their draws reach `ops->accepts` and then `ops->draw` through
  `v9x_r3d_draw_body` (`d3d_core.c`).
- **No per-front-end gate.** The engine is `v9x_d3d_engine()`, which is
  already `v9x_d3d_engine_hsw` whenever `CAP_D3D` is set. The family
  already ships all three DLLs: Glide is off only where
  `Package.GlideWrappers=$false`, which is 3dfx alone.
- **The texture caches are the front ends' own.** No engine uploads
  anything: the ICD (`v9x_gl_hw_texture`), Glide
  (`v9x_glide_surface_for`) and 3DCIF make DirectDraw texture surfaces
  when `describe` says the engine samples them. Otherwise they send CPU
  levels (`texture.levels`).

So "support" is the engine meeting the render interface's contract. No
new path is needed.

## Where it stands: wrong today, not just slow

A survey of the code (2026-10-18), with nothing run, finds Haswell
already selected for all three front ends, and wrong in four ways:

1. **CPU-level textures are drawn untextured.** `v9x_d3d_hsw_map` reads
   only `texture.object`. A draw with `object` NULL and `levels` set maps
   to `V9X_HSW_PS_DIFFUSE`, `accepts` says yes, and the triangle is drawn
   without its texture. Because of item 2, every ICD and Glide texture
   arrives as CPU levels on this engine.
2. **`describe` has no Haswell case.** It reports engine 0, the renderer
   "Velocity9x", and `hw_texture_size_max` 0, so the front ends never
   make surface textures here.
3. **The render interface's clear writes depth with the CPU, linearly.**
   `v9x_r3d_clear_body` hands the Z buffer's linear address to
   `v9x_r3d_clear`. On Haswell the buffer is Y-tiled (F2b), so a
   `glClear(GL_DEPTH_BUFFER_BIT)` or a Glide `grBufferClear` scatters the
   value across the wrong pixels. `depth_tiled` gates only the DirectDraw
   path (`v9x_depthfill_body`).
4. **Scissor and write mask are ignored.** `explicit_state`, `scissor_*`
   and `write_mask` are never read: a partial scissor draws outside it,
   and a masked draw writes every channel.

Also:

- **3DCIF's raw depth is refused.** A raw-depth draw carries a placeholder
  `V9X_DD_SURFACE_LCL` with no `lpGbl`, so
  `v9x_d3d_hsw_depth_surface` fails it with `REFUSE_DEPTH`, and there is
  no fallback, so the draw is UNSUPPORTED. 3DCIF draws without depth pass.
- **One texture unit** (`texture_units` 1): the core refuses two-unit
  draws, so the ICD and 3DCIF split them into two passes or decline.
- **No software fallback**: `v9x_r3d_fallback` allows Gen3 only.

## Steps

Each step ends at a gate, with a record in `docs\decisions` for anything
measured on the 4570.

### 1. Host: make it correct before making it complete

- **Refuse what is not drawn.**
  - `accepts` and `map` refuse a draw with `texture.levels` set and no
    surface: a new reason, `V9X_D3D_HSW_REFUSE_CPU_TEXTURE`.
  - Until step 2 they also refuse a partial scissor and a write mask
    other than RGB, as Gen3's `v9x_d3d_i9xx_accepts` does.
  - Host-tested through the map's pure part, which means moving the
    decision into `src\chipsets\intel\` as `d3d_mga_map.c` and
    `d3d_tdfx_map.c` are.
- **The tiled clear.**
  - `v9x_r3d_clear_body`, on an engine with `depth_tiled`, sends the
    depth part through `v9x_hal_depth_fill`, so it reaches the engine's
    depth fill and fails rather than scatter.
  - The colour part stays the CPU's (the target is linear), or uses the
    blitter's fill.
  - A partial write mask with depth goes in two parts: the CPU for
    colour, the engine for depth.
- **`describe`.**
  - Append `V9X_R3D_ABI_ENGINE_GEN75` (9) to the ABI's engine list. The
    list is append-only; the version stays 5, because a new value of an
    existing field is not a layout change. Confirm against the ABI
    header's rule before relying on that.
  - The renderer is "Velocity9x HD 4600".
  - `hw_texture_size_max` 2048 (the engine's limit), and
    `hw_texture_size_min` 1.
  - Shape POW2 until non-power-of-two sampling is measured. Phase E
    sampled 8x8 only. Gen7's sampler takes any size for a linear 2D
    surface (Vol 2d), but that is a claim, and POW2 costs nothing to
    start with.
  - Formats 565, 1555 and 4444, which `d3d_hsw.c` already samples.
- **Gate:** build-host with failing tests first, then run-checks.

### 2. Host, then the 4570: scissor and write mask

- **Scissor:** `3DSTATE_SCISSOR_STATE_POINTERS` already points at a
  zeroed scissor, so a per-draw `SCISSOR_RECT` from `scissor_*` is the
  change. Vol 2b: SF clips to it when the scissor enable in SF DW3 is
  set; i915 programs it so.
- **Write mask:** the `BLEND_STATE` write-disable bits per channel,
  which F2b measured: the depth fill writes no colour with all four set.
- **Gate:** V9XR3DP's scissor and mask checks at 640x480x16, the SiS's
  route (its scissor was measured exact there).

### 3. The 4570: V9XGLP, then OpenGL titles

- V9XGLP (`tools\diag\`, the ICD probe) runs against this engine and
  is read as on Gen3 (`2026-09-26-phase4-icd-on-gen3-netbook.md`): every
  check, and every refusal by reason.
- **Then Half-Life `-gl` `timedemo mwd5` and Quake 2 `timedemo`**, best of
  three, against the netbook's records
  (`2026-10-09-icd-call-census-and-attrib-stacks.md`: Half-Life `-gl`
  16.8 fps on the GMA 950).
- Neither title is on the 4570 yet; Half-Life is being installed for
  the Direct3D comparison.
- **Gate:** V9XGLP green apart from refusals named in the record, and
  both titles drawing textured, which needs eyes, as F3 did.

### 4. Two texture units

Phase E already measured a two-stage kernel (`MODULATE_TEX1`), and the
binding table carries two textures. What is missing:

- mapping `texture1`, `texcoords1`, `color_op` and `alpha_op` onto
  `v9x_hsw_ps_desc`;
- a second sampler state;
- the second texture coordinate in the TL vertex. That is a layout
  change from `V9X_HSW_TL_VERTEX_BYTES` 32: the render interface carries
  `texcoords1` beside the vertex, not in it.

Then `texture_units` 2, `describe` reports 2, and the ICD's SGIS
multitexture and Half-Life's lightmap pass take one pass instead of two.
**Gate:** V9XGLP's multitexture checks, then Half-Life `-gl` again.

### 5. Glide

Glide works against `describe` and the same draws:

- one level, mip NONE;
- chroma key as alpha 0 plus alpha test GREATER;
- blend factors including DESTALPHA and INVDESTALPHA, which a 16-bit
  target cannot honour and `d3d_hsw.c` already refuses;
- clears through `iface->clear`, which step 1 makes tiled-safe.

**Gate:** the Glide probe (`scripts\build-glide-probe.ps1`), then a
title: Diablo II or NFS II SE, which are installed on the netbook and
A8U4I5 (memory notes), not the 4570.

### 6. 3DCIF

- **Census first:** whether 3DCIF titles bring their own Z buffer as raw
  memory, and how it was allocated. A raw depth buffer that is not this
  engine's tiled placement cannot be its depth buffer at all.
- The options, decided by the census, not now:
  - accept a raw depth only when its address and pitch match a Z surface
    this HAL placed (found by address), or
  - draw raw-depth 3DCIF without hardware depth and say so.
- **Gate:** the 3DCIF probe, then Assault Rigs or Wipeout, which are on
  A8U4I5.

## What this does not cover

- **Texture upload cost.** Every upload is a CPU Lock through the
  aperture, as for Direct3D. Game 2's measured split
  (`docs\issues\2026-10-17-haswell-3dmark99-flicker-and-game2.md`) says
  the per-batch submit is the larger cost and it is unchanged here.
- **A software fallback for Haswell.** The CPU rasterizer cannot read a
  Y-tiled Z buffer, so a fallback would have to draw without depth or
  detile. It is left refused and counted.
- 8-bit palettised textures beyond what the front ends convert on the
  CPU, stencil, depth bias, and lines: the ABI carries none of them.

## Order

1, then 2, then 3, then 4, 5 and 6 in any order. Step 1 is host-only and
fixes defects that exist today, so it is worth doing first whatever
happens to the rest.
