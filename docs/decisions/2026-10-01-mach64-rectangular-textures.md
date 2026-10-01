# The Mach64 samples rectangular textures at a row pitch of their width: measured exact, and Half-Life's squared copies gone

Date: 2026-10-01
Machine: Gateway SOLO2150, ATI Rage Mobility-M, 4 MiB, boots 82-83,
current ATI package with this HAL (ABI 2026100102, HAL only changed).
Instrument: `V9XTSHP.EXE`, `tools/diag/texshape_probe_win32.c`, built by
`scripts/build-texshape-probe.ps1`.
Evidence: [`../probe/mach64-rectangular-textures-2026-10-01/`](../probe/mach64-rectangular-textures-2026-10-01/)

## Why

The Mach64 path took only square textures. The ICD squared every
rectangular image, so a 256x64 texture took a 256x256 block, and Half-Life
on the 4 MiB card spent its texture memory on padding: 622,015 draws
through squared copies, 1,021 failed creates and 1,012 evictions in one
`mwd5` session ([record](2026-10-01-mach64-modulatealpha-where-it-cannot-show.md)).
The builder already encoded a rectangle in `TEX_SIZE_PITCH` (width, larger
edge and height log2), but with a row pitch of the larger edge, a rule
nothing had measured, and the policy refused every rectangle.

## The probe

Draws through the HAL's DrawPrimitives directly, under the Win16 mutex as
DDRAW holds it, onto a 64x64 RGB565 target, and compares every pixel:

- Nine single-level textures, nearest and COPY over the whole target:
  32x32 (control), 64x8, 8x64, 64x16, 16x64, 64x32, 32x64, 32x8, 8x32. Each
  texel's colour names its coordinates, and every edge divides 64, so a
  pixel centre lies inside one texel whatever the sampler's sub-texel
  convention. A wide texture reads the same under either pitch rule; a
  tall one does not.
- A 128x32 and a 32x128 chain of six levels, each level tagged, drawn
  MIPNEAREST at levels 1 to 4's own sizes; each draw is compared against
  every level's expected image.

On the HAL before this change (boot 82) the probe first failed its own
control: with `TEXTUREADDRESS=CLAMP` and WRAPU/WRAPV left unset, the 32x32
draw was refused on texture address (`M64Policy11`). With the address at
WRAP and WRAPU/WRAPV set to 0 the control was exact (0 of 4,096 pixels)
and every rectangle was refused on shape, which validated the probe
(`gateway-boot82-before-V9XTSHP.INI`). The same boot showed DirectDraw
padding texture rows to 64 bytes when the HAL does not place a texture
itself: an 8x64 texture got a 64-byte pitch.

## The change

- Builder (`mach64_engine.c`): row pitch is the width, and a chain's
  levels halve each edge to one texel on its own, sized by their own
  edges, with `TEX_n_OFF` by the larger edge as before.
- Placement (`v9x_d3d_mach64_create_surface`/`_create_chain`): the HAL
  places rectangles as it placed squares, at width*2 bytes a row.
- Chain walk (`v9x_d3d_mach64_chain`): rectangular levels accepted.
- Policy: each edge a power of two from 8 to 256; a chain up to
  log2(larger edge) + 1 levels.
- The render interface's description drops `V9X_R3D_ABI_HWTEX_SQUARE`
  for the Mach64, so the ICD squares only an edge under 8.
- `v9x_d3d_describe_draw` now zeroes `vertex_alpha_opaque`, which the
  previous commit left uninitialised on the Direct3D path.
- Host tests first, watched failing: a tall texture binds at pitch = width
  and refuses at pitch = larger edge, `TEX_SIZE_PITCH` 0x665 for 32x64, a
  64x16 chain to 1x1 on TEX_6..TEX_0, a level sized by its own edges at
  the end of VRAM; the policy's rectangles, out-of-range edges and chain
  length.

## Measured (boot 83): Result=PASS

| Case | Result |
|---|---|
| Nine single levels | 0 mismatched pixels each; tall 8x64, 16x64, 32x64 exact at pitch = width |
| 128x32 chain at 64x16, 32x8, 16x4, 8x2 | best level 1, 2, 3, 4; 0 mismatches each |
| 32x128 chain at 16x64, 8x32, 4x16, 2x8 | best level 1, 2, 3, 4; 0 mismatches each |
| Refusals, FIFO/idle timeouts, resets | 0 |

The tall textures settle the pitch: the larger-edge rule the builder held
would have read them at two to eight times their row length.

## Half-Life on it (`mwd5`, best of three)

| | before (boot 82) | rectangles (boot 83) |
|---|---|---|
| OpenGL fps | 7.00 | 7.05 |
| OpenGL draws through squared copies | 622,015 | **357** |
| OpenGL texture creates / failed / evictions | 2,177 / 1,021 / 1,012 | **1,124 / 480 / 477** |
| OpenGL texture upload | 29.8 MB | **22.4 MB** |
| Direct3D fps | 13.21 | 13.26 |
| Direct3D chain shape gaps (`MipGapShape`) | 25,615 | 17,834 |

The texture churn halved; the frame rate did not move, because on this
CPU the frame is the ICD's and the HAL's CPU work. The remaining 357
Direct3D shape refusals are edges outside 8 to 256.

## Found on the way, not changed

A HAL context starts with `texture_wrap = 1` (`v9x_d3d_context_create`),
which the Mach64 mapping reads as WRAPU/WRAPV set, so every textured draw
is refused on address until the application sets WRAPU or WRAPV itself;
Direct3D's default for both is FALSE. A rerun of the probe with CLAMP and
both set to 0 passed (`gateway-boot83-clamp-wrap-zero-V9XTSHP.INI`). The
field is the ViRGE's "either is set" flag (see the 2026-09-03 wrap-bit
record), so changing its default is a ViRGE change as well and is left
for its own measurement. It may account for some of Half-Life's 3,183
address refusals; nothing here measured that.

## Not established

- That Half-Life's frames are right; nobody watched.
- Edges above 256 and below 8, and bilinear or trilinear filtering of a
  rectangle; the probe samples nearest only.
