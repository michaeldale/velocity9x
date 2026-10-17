# HD 4600 mipmaps: chains placed as one Gen7 tree, every level probe reads its level

Date: 2026-10-18. Machine: the i5-4570 (`8086:0412`), boot 26, the HAL
replaced by WININIT rename. Follows step 4 of
[intel-haswell-glide-3dcif-opengl.md](../plans/intel-haswell-glide-3dcif-opengl.md),
whose record listed mipmaps as not covered. Evidence:
[`../probe/hsw-4570-mipmaps-2026-10-18/`](../probe/hsw-4570-mipmaps-2026-10-18/).

## The problem

`RENDER_SURFACE_STATE` has one base and one pitch for a whole chain, and
the sampler finds each level by a layout fixed in the part. DirectDraw's
heap placed every level as a surface of its own, so the engine sampled
level 0 alone. It published the mip filters anyway, as an honest
degradation. The OpenGL ICD already creates whole chains
(`v9x_gl_hwtex_create`, `DDSCAPS_MIPMAP | DDSCAPS_COMPLEX`), and so do
Direct3D applications, so the missing part was the engine's alone.

## What changed

- **`v9x_intel_hsw_miptree`** (`hsw_3d.c`, host-tested) lays out a
  chain:
  - Vol 5's 2D layout, with HALIGN 4 and VALIGN 2, the zero encodings
    `surface_2d` already wrote.
  - Level 1 goes below level 0, level 2 to the right of level 1, and each
    later level below the one before.
  - The pitch is the tree's width rounded up to 64 bytes. The rows are
    the deepest any level reaches.
  - The tests' offsets were worked by hand for 8x8, 256x256, 4x4 (where
    the tree is wider than level 0) and 8x2.
  - The rule is the one Mesa's `brw_miptree_layout_2d` states. It was a
    claim until the probe below.
- **`v9x_intel_hsw_surface_mips`** sets DW5 3:0, MIP Count, to the levels
  less one.
- **CreateSurface** places a video-memory `TEXTURE | MIPMAP` chain of
  16-bit levels, each edge halving down to 1, as one 4 KB-aligned block
  from DirectDraw's heap (`d3d_place.c`). Each level's `fpVidMem` points
  at its place in the tree. This is Gen3's scheme with Gen7's layout.
  Declines are counted with Gen3's reasons.
- **At bind:** `v9x_d3d_hsw_mip_levels` walks the attachments and accepts
  each level that sits at the tree's pitch and offset. A chain the heap
  placed still samples level 0 and is counted in `mip_gap_*`.
- **Sampler:**
  - With levels, MIN's choice between levels becomes the mip filter
    (NEAREST 1, LINEAR 3, Vol 2d), and MAX LOD is the last level in U4.8.
  - MAG stays within level 0.
  - The DirectX 5 filter names are read as Gen3's
    `v9x_d3d_i9xx_filter` settles them.
- **Gates:** run-checks green. The layout and mip-count tests failed
  first (at link), then passed.

## Measured

**V9XGLP**, boot 26 against boot 23: `Result` still PASS, 30 Ok keys at
1, none at 0. Every key that names a sampled level moved from level 0 to
the level its ratio selects:

| keys | boot 23 | boot 26 |
|---|---|---|
| `MipQuad64` / `16` / `4` (64x64 chain, quads 64, 16, 4) | red, red, red | red, **blue**, **cyan**: levels 0, 2, 4 |
| `NonSqMipL0` / `L2` (64x16 chain) | `00FF00`, `00FF00` | `00FF00`, **`39C300`** (level 2's `3CC300` in 565) |
| `SgisMip{Off,W2,W64,W2x8}{64,32,16,8,4}` | L0 everywhere | **L0 L1 L2 L3 L4** |
| `SgisMipLin*`, `SgisMipU1Lin*` | L0 | **L0 L1 L2 L3 L4** |
| `SgisMipPersp*` (eye depth 2) | L0 | **L0 L1 L2 L3 L4** |

The layout, the mip count, the mip filter on both units, and perspective
LOD all agree with the reference levels. So the Vol 5/Mesa rule holds on
this part for chains up to 64 square and 64x16. Larger chains are
exercised only by the games below.

**V9XR3DP:** no Ok keys changed.

**Half-Life mwd5, 640x480 full screen, steady-state runs:**

| renderer | before (step 4; c960e9ed's record) | boot 26 | chains placed | gaps, declines |
|---|---|---|---|---|
| `-gl` | 67.7, 67.4 | 67.5, 67.1 | 123 | 0, 0 |
| `-d3d` | 59.3 | 59.2, 59.1 | 70 more | 0, 0 |

- `MipDraws` rose by 76,576 over the OpenGL session and 51,432 over the
  Direct3D one. `MipLevelsMax=8` means a 256 texture was sampled to its
  last level.
- Both frames drew textured and lit
  (`hl-gl-mwd5-frame.png`, `hl-d3d-mwd5-frame.png`), checked by agent
  screenshot, not on the monitor.
- The frame rate is unchanged within run-to-run spread.

## Not covered

- **Plain textures:** single-level textures stay where DirectDraw's heap
  puts them, as before.
- **Chains placed by other means:** a chain the application builds with
  `AddAttachedSurface`, or one CreateSurface declines, samples level 0.
- **Non-16-bit chains:** none of this engine's formats are other sizes.
