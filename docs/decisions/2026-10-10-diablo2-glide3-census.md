# What Diablo II asks of Glide 3: a census of its menus

Date: 2026-10-10. Machine: A8U4I5, ATI Rage XL PCI on Velocity9x 0.15.0
`ati` (build `11d348f`), boot 377. Plan:
[glide-3x-wrapper.md](../plans/glide-3x-wrapper.md), Phase 0. Evidence:
[docs/probe/a8u4i5-d2-glide3-census-2026-10-10/](../probe/a8u4i5-d2-glide3-census-2026-10-10/README.md).

## Method

A census `GLIDE3X.DLL` exports all 99 Glide 3 entry points of 3dfx's own
`GLIDE3X.DLL` (names and argument bytes from its export table). It logs
every call to `C:\V9XDIAG\V9XGLD3.LOG` and draws nothing. It reports a
Voodoo3-class board with one TMU, 4 MiB of frame buffer and 4 MiB of
texture memory. Copied into the Diablo II shareware folder and started
with `-3dfx`, it ran the game's intro, title, main menu and class screen.

## What the demo imports

`D2Glide.dll` imports 36 Glide 3 functions; `D2VidTst.exe` imports 10 of
the same (setup and queries only). Every name and argument size matches
3dfx's export table.

## What the menus did

| Area | Measured |
|---|---|
| Board checks | `grGet` for boards, max texture size and aspect, FB count, TMU count, texture alignment, UMA, gamma table entries and bits. `grGetString` for hardware, vendor, renderer and version. It accepted the census board. |
| Window | `grSstWinOpen` three times: 640x480 twice (start-up and intro), then 800x600 for the menus, after a `grSstWinClose`. Colour format RGBA, origin upper left, two colour buffers, no aux buffer. |
| Coordinates | `grCoordinateSpace(WINDOW)`. |
| Vertex layout | XY at 0, packed ARGB at 8, Q0 at 12, ST0 at 16. Stride 28. |
| Drawing | Only `grDrawVertexArrayContiguous`, always `GR_TRIANGLE_FAN` with 4 vertices: textured quads. About 55 a frame. The menu backdrop is 256x256 tiles. |
| Textures | All `GR_TEXFMT_P_8`, one LOD each (no mipmaps), 16 to 256 texels, aspect from 1:8 to 4:1. Palettes through `grTexDownloadTable(GR_TEXTABLE_PALETTE)`. Point sampling. About 3,500 downloads in 45 s: textures are re-downloaded as screens change. |
| Colour | Texture times iterated colour (`SCALE_OTHER`, factor `LOCAL`, local iterated, other texture). |
| Alpha | Constant alpha, or none. |
| Blending | Opaque (ONE, ZERO), additive (ONE, ONE), and multiply (ZERO, SRC_COLOR). |
| Chroma key | Enabled, keying black. |
| Depth | No depth buffer; depth writes off. Colour writes on, alpha writes off. |
| Per frame | One `grBufferClear` to black and `grBufferSwap(1)`. Dither switches between off and 4x4. |
| Gamma | `grLoadGammaTable` with 256 entries. `guGammaCorrectionRGB` was imported but not called. |
| Not called | `grLfbLock`/`grLfbUnlock`, `grDrawPoint`, `grDrawLine`, `grDrawVertexArray`, `grTexCombine` beyond set-up, `grFinish` beyond shutdown. |

## What this does not cover

The game itself. Blind clicks reached the class screen but not a game: the
census shows nothing, and the DirectDraw run used to map the screens came
back with a wrong palette and then stopped refreshing. In-game drawing
(lighting, the automap, lines, LFB use, other blend modes) is unmeasured.
The plan takes it from the real DLL, which keeps this logging, once frames
are visible.

## What it means for the design

- Nearly everything maps onto what `GLIDE2X.DLL` already does: paletted
  textures expanded in the DLL, chroma key as keyed alpha plus the alpha
  test, the three blend modes, and quads as two triangles each.
- New for Glide 3: the vertex layout and vertex arrays, `grGet` and
  `grGetString` answers, contexts, a close and reopen at a new resolution
  inside one process, and RGBA as the packed colour order.
- The Rage XL has no software fallback for a draw it refuses, so the
  multiply blend (ZERO, SRC_COLOR) needs checking against the Mach64
  blend table before anything is claimed for this card.
