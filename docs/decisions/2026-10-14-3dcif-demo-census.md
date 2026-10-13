# What ATI's 3DCIF demos ask of ATI3DCIF.DLL: a census on the Rage IIC

Date: 2026-10-14

Status: Measured on A8U4I5, boots 453 and 454. Phase 1 of
[ati-3dcif-wrapper.md](../plans/ati-3dcif-wrapper.md). Evidence:
[docs/probe/a8u4i5-rage-iic-3dcif-census-2026-10-14/](../probe/a8u4i5-rage-iic-3dcif-census-2026-10-14/README.md).

## Method

The census `ATI3DCIF.DLL` (c61e09d8, build `cif-census-1`) was copied
into each demo's own folder on A8U4I5. The machine runs the Rage IIC AGP
on the Velocity9x `ati` package (0c26021), 1024x768x16, 565. The DLL
exports ATI's 19 entry points in both forms, logs every call to
`C:\V9XDIAG\V9XCIF.LOG`, and draws nothing. `GetInfo` reports ASIC id
0x401 and Rage PRO caps less `LOAD_OBJECT` and tiling. Each demo was
started detached, left 12 to 45 s, then sent Escape.

Every demo loaded the census copy from its own folder (the `module`
line), found the `_lib` pointers and called through them.

## What each demo did

| Demo | Frames | Draws | Notes |
|---|---|---|---|
| Cubes | 1218 | 2436 `RenderPrimList`, 3 and 6 vertices | Two 1555 textures, Gouraud, texture times colour, fog on some draws |
| Pixie | 1570 | 836,447 `RenderPrimStrip` of 3 | Z buffer, mipmaps to 512x512, chroma key, alpha blending |
| Fairy | 1106 | 598,606 `RenderPrimStrip` of 3 | The same calls as Pixie |
| Laputa | 1 | 183 `RenderPrimStrip` of 3 | **Stops after its first `RenderEnd`**, twice (boots 453 and 454). See below |
| ATi-3D | 877 | 1754 `RenderPrimList` | Three draw pointers (a three-surface chain); a CI8 texture with a palette |
| Knight DEMO | 542 | 568,452 `RenderPrimList` | Rage PRO composite, RGB4444, RGB332, CI8, tiling on |
| Knight MORPH | 894 | 545,398 `RenderPrimList` | As DEMO; composite factor stepping 1 to 15 |
| Knight VQ | 684 | 1,428,792 `RenderPrimList` | VQ textures (`fmt=20`, codebook palettes `kind=4`) to 1024x256 |

The Knight demos ran to their drawing calls because the census reports
ASIC id 0x401. Under ATI's own driver on the same card they refuse
([2026-10-13-rage-iic-3dcif-native-reference.md](2026-10-13-rage-iic-3dcif-native-reference.md)).

Pixie, Fairy and Laputa first load the DLL, call `Init`, `GetInfo` and
`Term`, unload it, and load it again (two `attach` lines per run). The
others load it once.

## Answers to the plan's census questions

- **Entry points used:** `Init`, `Term`, `GetInfo`, `TextureReg`,
  `TextureUnreg`, `TexturePaletteCreate`, `TexturePaletteDestroy`,
  `ContextCreate`, `ContextDestroy`, `ContextSetState`, `RenderBegin`,
  `RenderEnd`, `RenderPrimList`, `RenderPrimStrip`. Never called:
  `TexturePaletteAnimate`, `LoadObject`, `RenderSwitch`,
  `RenderPrimMesh` (although ATi-3D and specular import it),
  `RenderIndexedPrim`.
- **One context**, one `RenderBegin`/`RenderEnd` block a frame, as the
  white paper recommends.
- **Vertex type:** only `C3D_EV_VTCF` (3). No demo sets
  `C3D_EV_TLVERTEX`.
- **Primitives:** only `C3D_EPRIM_TRI` (1): strips of exactly 3
  vertices (Pixie, Fairy, Laputa) and lists of 3, 6, 12 and 18.
- **Vertex values** (decoded from the logged words):
  - x, y are screen pixels from the surface's top-left (0 to 640, 0 to
    480).
  - z is in Z-buffer units, not normalised: Laputa uses 1.0 and 2.0,
    Pixie 229 to 477, the Knight sky about 65531. Cubes, with Z off,
    sends about -0.9.
  - w is 1/w: Laputa sends 1.0 for flat sprites; Pixie sends about
    0.003 to 0.008, with s and t of the same order, so s and t arrive
    already multiplied by w. The white paper's "homogeneous (s, t, and
    inverse w)".
  - r, g, b, a are floats from 0 to 255. Cubes and ATi-3D send alpha 0
    with blending off. Some Knight vertices carry small negative colour
    values (about -1.9) with solid shading; unexplained.
- **Surface state:**
  - `SURF_DRAW_PTR` is set inside each `RenderBegin` block, alternating
    between two pointers (three for ATi-3D): the application's flip
    chain, set every frame.
  - `SURF_DRAW_PITCH` is set only by the Knight demos, to 640 (pixels).
    Every other demo leaves it at the default.
  - `SURF_DRAW_PF` and `SURF_VPORT` are **never set** by any demo. The
    pixel format and viewport come from the guide's default, "Desktop
    Surface". In a 640x480 exclusive mode that has to mean the current
    primary: 640 pixels a row, the mode's 16-bit layout. So the 555 or
    565 question is decided by the mode, as marxveix said, not by any
    state a title sends.
  - `SURF_Z_PITCH` is 640 (0x280) wherever set: pixels, as the white
    paper says.
- **Pointers:** every draw, Z and texture-level pointer is in
  `D4xxxxxx`, the shared-arena mapping of video memory. The two draw
  pointers differ by 0x96000 (614,400 bytes, one 640x480x16 surface),
  and the Z buffer starts 0x96000 above the higher one (ATi-3D's three
  surfaces likewise). The pointers are evidently consecutive surfaces in
  one linear mapping, so a pointer minus the aperture's linear base
  should be a video-memory offset; the base itself was not read here.
  The plan's option 2, "render target at offset, pitch, format", fits
  every demo. No texture level was in system memory, though the card is
  AGP.
- **Textures:**
  - Formats used: CI8 (2), RGB1555 (3), RGB332 (7), RGB4444 (15) and
    VQ (20). No 565, 8888, CI4, Y8 or YUV.
  - Sizes from 8x8 to 1024x256.
  - The `C3D_TMAP` size is 72 for the CEBIT demos and Cubes, 88 for
    ATi-3D and the Knight demos (the Rage PRO fields present).
  - Mipmaps up to 10 or 11 levels.
  - Chroma key used by every Pixie, Fairy and Laputa texture.
  - Palettes: 256 entries with flags byte 0x40
    (`C3D_LOAD_PALETTE_ENTRY`); VQ codebooks.
  - **No texture was rewritten after registration**: 0 checksum changes
    logged. Each texture is rechecked on its first four selections and
    every 256th after; the number of rechecks was not counted.
- **States seen with more than their default:**
  - shade: solid, flat, smooth;
  - texture lighting: none, modulate;
  - perspective: 0, 1, 3, 7, 9;
  - filter: 0, 1, 2, 3, 4;
  - texel op: none, chroma key, alpha;
  - blend source: ONE, SRCALPHA, INVSRCALPHA, DSTCLR;
  - blend destination: ZERO, SRCCLR, INVSRCCLR, SRCALPHA, INVSRCALPHA;
  - Z modes: off and test-and-write;
  - Z compare: NEVER, LESS, LEQUAL, GEQUAL;
  - fog on and off;
  - dither on and off;
  - composite on with function BLEND, filter 2 and factors 1 to 15.
  - Never set: LOD bias, destination-alpha test, specular, enhanced
    colour range.
- **Call rate:** the Knight demos make about 2,350 (MORPH) to 7,300
  (VQ) `ContextSetState` calls a frame (VQ: 4.98 million in 684
  frames). Setting state has to be cheap; recomputing the engine state
  per call would not be.

## Laputa stops after one frame

Laputa draws one frame and then makes no further call into the DLL,
twice. Two `V9XTRACE` snapshots 52 s apart show no flip and no
DirectDraw progress in between (`FlipHandled` 3894 both times). It does
not exit and ignores Escape, `WCLOSE.EXE` and Alt+F4; a warm restart
cleared it. Under ATI's driver and DLL it runs.

Not established: what it waits on. Candidates are something only ATI's
DLL provides (a frame drawn, or a DirectDraw behaviour of ATI's HAL),
our HAL, or WINMM (its music; the machine has no sound device, but it
had none under ATI's driver either). It needs a trace of its own before
any guess is coded.

## Other runs

- ATi-3D crashed once at `ATI-3D.EXE+35C6`, a call through a null COM
  pointer (`mov ecx,[eax]` with EAX 0), while the hung Laputa still
  held DirectDraw. Rerun alone on a fresh boot it ran 877 frames and
  exited. The crash belongs to the hung Laputa, not to the DLL.
- The first Knight runs, made behind the same hung Laputa, called
  `Init`, `GetInfo` and `Term` and exited. The reruns are the ones
  tabulated.

## What it means for the design

- The target is set by pointer per frame and inherits pitch, format and
  viewport from the current mode. The wrapper needs the primary's pitch
  and layout, and a way to name a surface by its video-memory offset.
  That is an ABI change to agree (plan, option 2).
- The vertex conversion is VTCF only for these demos: screen x and y,
  z in 16-bit units, 1/w, s and t premultiplied, colour 0 to 255.
- Texture conversion needs CI8 with a palette, 1555, 332, 4444 and VQ
  decoding. Registration-time copies suffice for these demos.
- Composite (two textures) and VQ matter only to the Knight demos, which
  need a Rage PRO identity to run at all. The IIC cannot composite; that
  is the identity decision again.
- Laputa's stall is the open defect to investigate before the wrapper
  claims these demos.

Gates: check-tree; the analysis reproduces from the full logs, which are
kept outside the repository (14.5 MB); the excerpt and analysis are
committed.
