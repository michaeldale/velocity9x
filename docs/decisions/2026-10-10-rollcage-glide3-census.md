# What Rollcage asks of Glide 3: a census of the demo's Glide build

Status: Measured. Start-up, the game's own settings dialog and two unattended
3D runs are covered; a race driven by a player is not.

Date: 2026-10-10. Machine: A8U4I5, ATI Rage XL PCI on Velocity9x 0.15.0
`ati` (build `11d348f`), boot 377. Plan:
[glide-3x-wrapper.md](../plans/glide-3x-wrapper.md), Phase 0. Evidence:
[docs/probe/a8u4i5-rollcage-glide3-census-2026-10-10/](../probe/a8u4i5-rollcage-glide3-census-2026-10-10/README.md).
Companion record: [2026-10-10-diablo2-glide3-census.md](2026-10-10-diablo2-glide3-census.md).

## Context

The Rollcage demo (Psygnosis, 1999) is the second Glide 3 title after
Diablo II. Its Glide build imports 42 Glide 3 functions. Diablo II imports
30 of the same and none of the other 12: `grClipWindow`, `grCullMode`,
`grDepthBufferFunction`, `grDepthBufferMode`, `grDrawTriangle`,
`grFogColorValue`, `grFogMode`, `grQueryResolutions`, `grRenderBuffer`,
`grTexClampMode`, `grTexMipMapMode` and `grTexTextureMemRequired`. From
those imports the brief expected a depth buffer, fog from a per-vertex
source, mipmaps, front-buffer drawing and LFB use. This record is what the
census DLL measured against those expectations.

### How the demo picks a renderer

`DISK1\ROLLCAGE\ROLLCAGE.EXE` (40,960 bytes) imports no Glide and no
Direct3D; it is a launcher. Disassembly shows it reading the binary value
`DataPC` (0x19C0 bytes) under `Software\Psygnosis\Rollcage\BootSys`. When
byte 6 of that value is non-zero it changes into `Glide\`, otherwise into
`Direct3D\`, and starts that folder's `Rollcage.exe /DISPATCHER`. Which
hive holds the key was not resolved: the root is a variable at the call
site. Setup wrote only `HKLM\Software\Psygnosis\Rollcage\Installer`
(`Destination`, `Source`, `Language`); no `BootSys` key existed afterwards.
Starting `Glide\Rollcage.exe /DISPATCHER` directly works and skips the
launcher.

Each build opens its own settings dialog before the game: language,
Device / Resolution, "Z Buffered Rendering" (on by default) and "Triple
Display Buffer" (off).

### Install on A8U4I5

1. A8U4I5 fetched the demo zip (25,918,163 bytes) from Michael's file
   server with the agent's `download` verb.
2. 7-Zip 4.57 on the guest extracted it to `C:\RCDEMO`.
3. `DISK1\SETUP.EXE` (InstallShield) installed the demo to
   `C:\Program Files\Psygnosis\Rollcage`. Setup reported "Error loading
   DSETUP.DLL": that DLL sits beside the top-level `SETUP.EXE`, not in
   `DISK1`. Dismissing it let setup finish, with DirectX 9.0c already
   present.

The census `GLIDE3X.DLL` was copied into `Glide\`. No other `GLIDE3X.DLL`
exists in `C:\WINDOWS` or `C:\WINDOWS\SYSTEM`.

## What the census measured

| Area | Measured |
|---|---|
| Mode query | `grQueryResolutions` is the first call after `grGlideInit`, made twice: with a null output for the size, then to fill the list. The template is any resolution, 60 Hz, 2 colour buffers, 1 aux buffer. **With the stub returning zero, the settings dialog's resolution list was empty**, so Play could not start Glide. Once the census answered (640x480 and 800x600), the list showed only "640 x 480 with 16 bits per pixel". |
| Board checks | `grGet` for NUM_BOARDS, WDEPTH_MIN_MAX, NUM_TMU and BITS_RGBA. It accepted the census board (one TMU, 4 MiB frame buffer, 4 MiB texture memory). `grGetString` is not imported. |
| Window | `grSstWinOpen` at 640x480 (0x7), 60 Hz, colour format RGBA (2), origin upper left, **2 colour buffers, 1 aux buffer**. One open per process, closed by `grSstWinClose(1)` before `grGlideShutdown`. |
| Coordinates | `grCoordinateSpace(WINDOW)`. |
| Vertex layout | XY at 0, **RGB as three floats** at 8 (0 to 255), **Q at 20** with Q0 also at 20, ST0 at 24 with ST1 also at 24. Stride 32. Z, W, PARGB, FOG_EXT and ST2 explicitly disabled. No A. |
| Depth | `grDepthBufferMode(WBUFFER)` from Q. Depth function LESS once, then LEQUAL. `grDepthMask` toggles inside each frame: writes off for the additive passes. `grBufferClear` writes depth 0xFFFF. |
| Drawing | `grDrawVertexArrayContiguous` with **`GR_POLYGON`** (3 to 6 vertices; not a fan as in Diablo II). `grDrawVertexArray` with **`GR_TRIANGLES`**, six pointers each. `grDrawTriangle`. In process 2 that was 1,662,847 polygons, 1,509,804 arrays and 312,800 triangles over 2,794 swaps: about 2,000 triangles a frame. |
| Textures | All `GR_TEXFMT_P_8`, **one LOD each, 256 texels** (Glide 3 LOD 8 to 8), aspect 1:1 to 8:1. Every distinct GrTexInfo is logged and none has a second level. `grTexMipMapMode(0, DISABLE, FXFALSE)` once. Clamp mode CLAMP in s and t. Filter bilinear for minification and magnification. |
| Palettes | `grTexDownloadTable(PALETTE)` before every `grTexSource`: 208,644 of each in process 2, about 75 a frame. |
| Texture memory | `grTexTextureMemRequired` called before downloads; textures packed at the sizes the census returned (64 KiB steps for 256x256), from 0 up to 2 MiB in the 4 MiB TMU. `grTexMinAddress` and `grTexMaxAddress` queried. |
| Colour | Texture times iterated (`SCALE_OTHER`, factor LOCAL, local iterated, other texture), or iterated alone (`LOCAL`, local iterated). |
| Alpha | `grAlphaCombine(SCALE_OTHER, ONE, CONSTANT, TEXTURE, FXFALSE)`: texture alpha. Set once. |
| Texture combine | `grTexCombine(0, LOCAL, NONE, LOCAL, NONE, FXFALSE, FXFALSE)`: decal, before every bind. |
| Blending | ONE/ZERO (opaque), ONE/ONE (additive), and **ZERO/ONE_MINUS_SRC_COLOR**. That third one differs from Diablo II's ZERO/SRC_COLOR. |
| Chroma key | On, keying black. |
| Fog | `grFogColorValue(0x3C467F00)` once. `grFogMode` 6,308 times across both processes, **always GR_FOG_DISABLE**. |
| Culling | `grCullMode(DISABLE)` once. |
| Clip window | Full screen, plus each frame a sub-window at x 425 to 597 whose bottom edge sweeps from -7 to 121: an animated panel. **One window has a negative bottom (y max -7), an empty window**, and the clip code must accept it. |
| Render buffer | `grRenderBuffer(BACK)` once; nothing is drawn to the front buffer. |
| Gamma | `guGammaCorrectionRGB(1.375, 1.375, 1.375)` once. |
| Not called | `grLfbLock`, `grLfbUnlock` and `grColorMask`, though imported. |

### Two processes, no input

After Play, the first process drew a 3D scene cleared to 0x18389C00 for
2,794 swaps (about 90 s) with no input. It then closed its window, shut
Glide down and exited. A second `ROLLCAGE.EXE` attached 12 ms later and
drew a scene cleared to 0x00000800. One Enter key went to it, then it was
closed with `WCLOSE.EXE`. Both processes made the same set-up calls and
show the same state vocabulary.

No frame was seen: the census draws nothing. The Direct3D build at the
same point showed a sky with clouds in an agent screenshot, and other
screenshots came back black or partly drawn. These runs are most likely
the demo's attract mode, but that is not established.

## Hypotheses the log disproves or settles

1. **"Fog from a per-vertex source"**: disproved for what ran. The layout
   carries neither FOG_EXT nor A, and fog is disabled every time it is set.
   Whether a player's race turns fog on is open.
2. **"Mipmaps"**: disproved. Every texture is a single 256 level and
   mipmapping is set to DISABLE. `grTexMipMapMode` and `grTexClampMode` are
   one-off set-up calls.
3. **"Possibly drawing to the front buffer"**: disproved; BACK only.
4. **"grLfbLock for HUD, movies or screenshots"**: not called in either
   process.
5. **"Depth from GR_PARAM_Z or Q"**: settled as Q with a W-buffer, and one
   aux buffer requested.
6. **"grTexTextureMemRequired must return real sizes"**: confirmed in
   effect. The game lays textures out at the sizes returned.
7. **"grQueryResolutions answered with the flip chain's modes"**:
   confirmed as essential. Without an answer the game cannot start, and it
   offers only 640x480 even when 800x600 is listed.
8. **"Vertex arrays as Diablo II used them"**: different. Rollcage uses
   `GR_POLYGON` and `GR_TRIANGLES`, both kinds of array, and
   `grDrawTriangle`, and its colours are float RGB, not packed ARGB.

## Options

- **Drive a player race next, on the census.** The front end's key
  sequence is unknown and the screen is blank, so this would be blind.
- **Take the race from the real DLL.** Its logging comes from the census,
  and once frames show, the menus can be driven from screenshots. This is
  what the plan already does for Diablo II's in-game census.

## Decision

Take the race from the real DLL. The front end and two minutes of 3D
drawing already define the core Rollcage needs. What a player's race may
add (fog, LFB, other blends) is checked when the DLL draws.

## Consequences

- The Glide 3 front end has to read float RGB and packed ARGB colours
  (Diablo II uses packed), Q as both W and the texture's q, and ST0 at an
  offset shared with ST1. Its draw entries need `GR_POLYGON`,
  `GR_TRIANGLE_FAN`, `GR_TRIANGLES` and `grDrawTriangle`.
- W-buffering from Q, LESS and LEQUAL, a depth mask that changes within a
  frame, and a clear at depth 0xFFFF are all in Glide 2's depth path
  already. The aux buffer has to be created when one is requested.
- `grQueryResolutions` must answer, or the game cannot start.
- A palette load before every bind is the case Glide 2's palette-keyed
  conversion cache was built for (NFS II SE).
- Bilinear filtering and the ZERO/ONE_MINUS_SRC_COLOR blend need checking
  against the Mach64 tables on the Rage XL. It has no fallback for a draw
  it refuses.
- A clip window with a negative or empty extent must cull, not wrap.
