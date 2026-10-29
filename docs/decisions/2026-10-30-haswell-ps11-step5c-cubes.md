# Cube textures on the HD 4600: QPitch measured, cubes placed and sampled through Direct3D 8

Date: 2026-10-30. Step 5c of
[`../plans/haswell-ddi8-pixel-shaders.md`](../plans/haswell-ddi8-pixel-shaders.md).
Machine: the Dell i5-4570 (HD 4600), boots 129-132. Evidence:
[`../probe/hsw-4570-ps11-step5c-2026-10-30/`](../probe/hsw-4570-ps11-step5c-2026-10-30/).

## QPitch for a cube with mips (V9XHSWMM /ps11:3, boot 129)

Gen7 samples a cube as one surface of six faces stacked QPitch rows
apart. Step 5a measured only a single-level cube (Surface Array Spacing
LOD0, QPitch the side). With mips the spacing is FULL, and Gen7 has no
QPitch field: the sampler computes it. Mesa's rule for Gen7 and later is
h0 + h1 + 12 j, against 11 j before Gen7; nothing here had measured it.

`v9x_intel_hsw_cube_qpitch` and `v9x_intel_hsw_cubetree` encode the
12 j rule and are host-tested. The probe's run 3 draws step 5a's Cube
program from a 2x2 cube with two levels laid out by them: QPitch 28 rows,
144 rows in all, at a 16-byte pitch (QPitch counts rows, so the pitch does
not change it). Target 0 holds stage 1's sampler to LOD 0, target 1 to
LOD 1. Every texel names its face, level and place; every padding row
names its row.

| Target | Judged | Mismatches | Max error |
|---|---|---|---|
| CubeL0 | 1,053 | 0 | 0 |
| CubeL1 | 1,042 | 0 | 0 |

The test tells the two rules apart: at 11 j each face past +X would
start two rows earlier, in padding with alpha 0. The far corner pixel read
face 5 (-Z) at level 0, the most shifted face, correctly.

## What the runtime does with a cube (boots 130-131)

A measurement HAL (cube maps claimed, not committed) logged each cube's
CreateSurface list, CreateSurfaceEx attachments and any TEXBLT naming it
(V9XPS.LOG, `cube-*` lines). V9XD32 gained T19 Cube: a managed 8x8
cube of two levels from CreateCubeTexture, filled face by face through
LockRect, drawn from stage 1 by the Cube program.

- **Boot 130.** The runtime kept the managed cube in system memory and
  made no video-memory copy: no CreateSurface, no TEXBLT. The draw
  sampled nothing usable.
- **Hypothesis: DirectDraw puts a cube in video memory only when the
  driver answers `GUID_DDMoreSurfaceCaps` with DDSCAPS2_CUBEMAP.** This
  driver declined that GUID. Boot 131 answered it (40 bytes asked,
  DDSCAPS2_CUBEMAP, no heap restriction). The runtime then made the
  video-memory copy.
- **CreateSurface gets the whole cube in one call.** It holds 12 surfaces, face by face in D3DCUBEMAP_FACES order
  (+X -X +Y -Y +Z -Z), each face's top then its level 1; every surface
  names its face in DDSCAPS2 (400h << face), levels below a top carry
  DDSCAPS2_MIPMAPSUBLEVEL (10000h). The caps2 offset in
  DDRAWI_DDRAWSURFACE_MORE (34h), counted from the header as the handle's
  64h was, read those bits correctly.
- **CreateSurfaceEx names the root.** That is the +X top. Its attachments are, in order,
  -Z +Z -Y +Y -X and then its own level 1. Every face top carries
  DDSCAPS_MIPMAP, so the held copy's next-level walk took -Z as level 1.
- **No TEXBLT named either cube.** How the runtime fills the
  video-memory faces from its system-memory copy is not recorded. They
  arrive: T19 Cube matches on every face (below).

## The change

- **Placement.** `create_surface` places a cube's list as one block in the
  cubetree layout. It finds each surface's face from its DDSCAPS2 and its
  level from its edge. It declines a list it cannot account for, and one
  whose first surface is not +X's top. The engine remembers each placed
  cube by its +X top's offset, the surface a texture record carries.
  `destroy_surface` forgets it with the block.
- **Sampling.**
  - A texture at a placed cube's offset is a cube to the texture unit:
    SURFTYPE_CUBE with its levels (`v9x_intel_hsw_surface_cube_mips`)
    and every coordinate clamped.
  - A pixel shader's stage samples it with three coordinates. The kernel
    cache keys on the engine's cube stages, not the core's.
  - The fixed-function draw refuses a cube.
- **The held copy's next level** must have half the edge, so a face is
  never taken as a level.
- **Claims, on an engine with `draw_shader`:**
  - CUBEMAP, MIPCUBEMAP and CUBEMAP_POW2, with CubeTextureFilterCaps;
  - CUBETEXTURE on every texture format;
  - the GUID_DDMoreSurfaceCaps answer.

  The measurement switch is now `V9X_D3D_VTSS_ONE_PASS`, the
  ValidateTextureStageState one-pass answer alone.

## V9XD32 T19 with cubes (boot 132)

| Program | 565, tolerance 9 | X8R8G8B8, tolerance 2 |
|---|---|---|
| Push | pass | pass, error 1 |
| FourTex | pass | pass, error 0 |
| Kill | pass | pass, error 0 |
| Bem | pass | pass, error 0 |
| PushFog | pass | pass, error 1 |
| KillFog | pass | pass, error 1 |
| Cube | 1,053 judged, error 0 | error 0 |

T12-T16 passed in both runs. The first Cube run failed on every pixel
because the judge, not the driver, was wrong: T19's environment said no
stage was a cube, so the interpreter looked stage 1 up as 2D. The pixel
it named held a +X texel, the right face. The judge now takes the
program's cube stages.

## 3DMark 2001 SE stops: "device does not support bump normal maps"

With cubes claimed, a run of Nature and the Pixel Shader test (boot 132)
stops at once: "3DMark could not initialize your 3D accelerator",
`P_D3D::DRV_allocateMap - device does not support bump normal maps`.
Its system report lists environment and DOT3 bump mapping as N/A. Without
cube maps 3DMark called Nature unsupported and never got this far; boot
127's cube-claimed measurement "stopped before its score window", most
likely here. No bump texture format (V8U8 and its kind) is published.
That is step 5d's first item. Until it lands, claiming cubes stops
3DMark 2001 SE's runs that include Nature, the default benchmark
included.

## Not done, not checked

- **Which bump format 3DMark wants**, and whether the default benchmark
  stops as this selection did, are not measured.
- **Mip selection of a cube by its own LOD.** The probe held each
  level by the sampler's LOD clamp. T19 Cube magnifies and reads level 0.
  The mip filter between levels of a cube is not checked.
- **Cube faces filled by TEXBLT or a lock of a default-pool cube** are
  not exercised: T19's cube is managed.
- **Seamless filtering** across face edges, and TEXCOORDMODE_CUBE against
  CLAMP, are not compared. Direct3D 8 does not ask for seamless, and the
  pixels at face edges are not judged.
- **A cube in a 2D-texture shader stage, or a 2D texture in a cube
  stage**, takes whatever the texture is. Direct3D defines it that way;
  nothing measured exercises it.
