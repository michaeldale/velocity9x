# 32-bit textures the engine does not publish are refused at creation, not drawn black

Date: 2026-11-28

## The defect, as five issue records saw it

V9XD32's T22 creates an 8x8 A8R8G8B8 and an X8R8G8B8 video-memory
texture, gives each a DX5 handle and draws it with DECAL. On five
engines every call returned `S_OK` and the judged pixels came back
black, or white on the ViRGE:

| Engine | Published 32-bit layouts | T22A | T22X | Issue |
|---|---|---|---|---|
| Rage 128 Pro | ARGB8888 | pass | black | [2026-11-27](../issues/2026-11-27-rage128pro-v9xd32-x8r8g8b8-texture-draws-black.md) |
| SiS 6326 | none (ARGB8888 sampled, unpublished) | pass | black | [2026-11-27](../issues/2026-11-27-sis6326-v9xd32-x8r8g8b8-texture-draws-black.md) |
| Voodoo4 (VSA-100) | none | black | black | [2026-11-27](../issues/2026-11-27-voodoo4-v9xd32-32bit-textures-draw-black.md) |
| Rage XL (Mach64) | none | black | black | [2026-11-26](../issues/2026-11-26-rage-xl-v9xd32-add-blend-and-32bit-textures.md) |
| RIVA 128 | none | black | black | [2026-11-17](../issues/2026-11-17-riva128-t22-32bit-textures-draw-black.md) |
| ViRGE (86Box) | none | white | white | this record |

Read from each engine's `describe_caps` (`d3d_r128.c`, `d3d_sis6326.c`,
`d3d_tdfx.c`, `d3d_mach64.c`, `d3d_nv3.c`, `d3d_virge.c`): every layout
that drew wrong is one the engine does not publish. Every engine that
draws a 32-bit layout publishes it (Evergreen, R600, RS690, GCN, Gen3,
Gen4, Haswell, the GT218/G96 and the software engine publish both).
`V9xHalCanCreateSurface` answered `DD_OK` to everything, so DirectDraw
created the unpublished texture, the runtime gave it a handle, and the
engine had no texel format for it.

## The change

`src/common/texfmt.c` (host-tested, `tests/host/test_texfmt.c`) says
whether a requested layout is a 32-bit RGB one that matches none of the
published `texture_formats` (bit count, the three masks, and alpha:
an ARGB8888 entry is not an XRGB8888 one). `V9xHalCanCreateSurface`
answers `DDERR_INVALIDPIXELFORMAT` for such a texture in video memory.
It judges only 32-bit RGB: 16-bit layouts an engine samples unpublished
(the SiS's RGB555) and FourCC, palette, luminance and bump formats are
untouched, and nothing is judged where no 3D engine published a list.
The one working path this removes is the SiS's unpublished ARGB8888,
which no application that enumerates formats would ask for.

## Measured

86Box ViRGE/DX VM (`WIN98-86BOX`, port 9869), the s3 package from this
tree installed by `update-associated-driver.ps1` (boot 710; DRV, VxD and
HAL hash-checked), V9XD32 `/bpp16`
([evidence](../probe/unpublished-32bit-textures-virge-2026-11-28/)):

| | Before (boot 709) | After (boot 710) |
|---|---|---|
| `T22ACreateHr`, `T22XCreateHr` | 0, 0 | 88760091h, 88760091h |
| T22 | both drawn white (`7FFF`), `T22AOk=0`, `T22XOk=0` | skipped |
| Other failures | T20 ModAlpha, T21 | T20 ModAlpha, T21 (unchanged) |
| `Failures` | 4 | 2 |

The G96 (M-Z2T02D, boot 21) publishes both layouts and is unaffected by
construction; its V9XD32 was not rerun on this build.

## Not run

The five cards above: none is fitted where it can be reached now. On
each, T22 should now be skipped (`CreateHr` 88760091h) and the issue
closed by that run; the Rage 128 Pro keeps T22A (it publishes
ARGB8888), the SiS 6326 loses it (sampled but unpublished).
