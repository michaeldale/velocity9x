# ATI 3DCIF on Velocity9x: an ATI3DCIF.DLL over the render interface

Date: 2026-10-12

Status: Proposed. Nothing coded or measured. The first title is not yet
chosen. glrage (`C:\everything\glrage`) supplies the export table and
three known titles; GLCIF 0.05 supplies the header with constant values,
defaults and struct layouts. marxveix (VOGONS, by PM to Michael,
2026-10-12) and his page gona.mactar.hu/ATI3DCIF supply the title list,
the 555 requirement, chip support and a demo pack; none of it measured
here yet. The SDK 4.1, the demo pack, every ATI DLL version and the Tomb
Raider ATI patch are downloaded (Phase 0, step 2).

## Context

ATI's 3D Rage Common Interface (3DCIF, `ATI3DCIF.DLL`) is the Rage line's
native API, the way Glide is 3dfx's. A handful of 1996–98 titles shipped
ATI-specific renderers on it, usually bundled with Rage cards or released
as patches. ATI's DLL talks to ATI's own display driver, so it stops working
once Velocity9x drives a Rage, and it never worked on any other chip.

The Glide route applies unchanged in principle: a user-mode DLL that never
touches hardware and draws through `V9xRenderInterface`
(`include/velocity9x/r3d_abi.h`), exported by `V9XHAL.DLL`. Every engine
that serves the OpenGL ICD, Glide 2 and Glide 3 then serves 3DCIF too
([glide-2x-wrapper.md](glide-2x-wrapper.md),
[glide-3x-wrapper.md](glide-3x-wrapper.md)).

One difference shapes the whole design: **3DCIF does not own the
screen.** The application creates its own DirectDraw surfaces, locks
them, and hands 3DCIF raw pointers and pitches through context state
(`C3D_ERS_SURF_DRAW_PTR`, `_DRAW_PITCH`, `_DRAW_PF`, `_SURF_Z_PTR`,
`_Z_PITCH`). Texture levels are likewise pointers (`C3D_TMAP.apvLevels`).
Glide opened its own flip chain; a 3DCIF wrapper has to turn a pointer it
is given back into a surface the render interface can draw into.

glrage, an LGPL-3 OpenGL wrapper for ATI3DCIF and DirectDraw 2
(github.com/ata4/glrage), does not solve that: it also replaces the
application's `ddraw.dll`, ignores the draw and Z pointers, renders into
its own GL back buffer, and reconciles that with what the game wrote
through `Lock` at `Flip` time (`ddraw/DirectDrawSurface.cpp`, the
`isRendered` / dirty logic). Velocity9x cannot take that route: the
application's DirectDraw is our HAL, and replacing `ddraw.dll` is out of
the question. glrage does show that the games mix 3DCIF drawing with
their own `Lock` writes to the same surfaces, which the design must
allow.

GLCIF 0.05, the Tomb Raider-only wrapper glrage grew from, does the
same: its own `ddraw.dll` and an OpenGL window, with the draw pointer
ignored.

Proposed, to agree with Michael before Phase 0:
- **First engine:** the Rage XL on A8U4I5 (Mach64). It is what the titles
  were written for, and it has no software fallback, so every state the
  game uses must be one the engine takes or one the DLL lowers.
- **Second engine:** Gen3 on the netbook, which falls back to the software
  engine for refused draws.
- **A reference run is possible**, unlike Glide, but not on the Rage XL.
  marxveix reports, and gona's matrices agree, that stock 3DCIF does not
  support the Rage XL PCI (4752) or Rage XC PCI (4753); gona lists the
  XL as working on AGP only. A8U4I5's Rage XL is PCI. The reference run
  is therefore the Rage IIC (475A, supported) on A8U4I5 under ATI's
  driver. Whether to spend the boots is a decision for Phase 0.
- **What this buys beyond the wrapper:** stock 3DCIF never ran on the
  Rage XL PCI, XC PCI or the Rage Mobility (marxveix: Mobility parts run
  Direct3D and OpenGL but probably not CIF; gona contradicts itself on
  Mobility-L). Ours runs wherever the render interface does, including
  the Gateway's Rage Mobility and non-ATI chips.
- **First targets: the demos, not a game.** marxveix's demo pack
  (`ATi3DCInterFace.7z`, his "3dcif information package", no install
  needed) and the Rage Pro SDK 4.1 samples (explosion, texture
  composite, specular, blending, clamping, the Knight demo and its VQ
  variant). gona lists which need 16-bit, 256+ colours or 32-bit.
- **Then titles**, from marxveix, gona and glrage (none installed on a
  test machine yet):
  - Tomb Raider and Unfinished Business with Eidos's ATI patch
    (`trati.zip`, `tombub_debugged_ati_patch.zip` on infania's gona
    mirror). Needs a DirectSound device to start, the ATI files in the
    game folder, and a 555 surface (below). gona: fails with several older
    DLLs; 4.10.2640 in the game folder fixes some cases.
  - Croc 1.0, whose graphics setting "Rage II" selects 3DCIF.
  - MechWarrior 2, ATI 3D RAGE Edition (3D Xpression bundle; an
    unofficial `Mech2_ATi_files.7z` on the mirror).
  - Assault Rigs and Wipeout, 3D Rage Editions (glrage).
  - Formula 1 in the ATI Xpert@Play bundle (`f1atipro.zip` is a texture
    corruption fix for it).
  - Out of scope: Actua Soccer, the one DOS 3DCIF title; DOS CIF is
    linked into the executable and drives the chip itself.

Not in scope: the RAGE PRO extensions of later SDK revisions unless the
census shows a title using them, and any change to the Mach64 engine
itself.

## Licence and sources

The ATI SDK-C02700 Rev 1.30, *3D RAGE Windows 95 Programmer's Guide*
(old.vgamuseum.info), is the authority for every API-level constant. It is
cited as `CIF Lnnnn` in `docs/specifications/ati-rage2-3d-engine.md` from a
text copy `r/cif.txt` that is not in this repository and was not found on
this machine on 2026-10-12; it has to be fetched again and its location
recorded in the Phase 0 record. The SDK's `ATI3DCIF.H` and ATI's shipped
DLL may be read for facts (prototypes, struct layouts, the export table);
nothing is copied.

glrage is LGPL-3 and falls under the same rule as Open Glide: read for
facts, nothing copied. It does not carry `ATI3DCIF.H` (it includes
`ragesdk/include/ATI3DCIF.H` from the SDK, absent from its tree).

GLCIF 0.05 (`C:\Users\michael\Downloads\glcif_0.05-source\glcif-code-94a8ce15c4764a3923fecb19f06a62d294a9f573`,
seven files, no licence file) does carry an `ati3dcif.h`. Its comments
("Rage3", "AKO", "DAG") read as ATI's own SDK header from the Rage Pro
era, not a reconstruction, so it is ATI's copyright: read for values and
layouts, nothing copied, and the copy kept outside this repository. Where
it and the SDK guide disagree, the guide wins.

Two further sources, both named by marxveix and gona: the **ATI 3D Rage
Pro SDK 4.1** on the VOGONS Vintage Driver Library (header, samples,
likely documentation; the one SDK gona mentions), and gona's
`all_ATI3DCIF_DLL.zip`, every shipped DLL from 4.02.0017 to 4.12.2696,
whose export tables settle the export list across versions. gona's
matrices (driver x DLL x chip x title) are the reference for behaviour
on real chips.

Further sources from marxveix (VOGONS, 2026-10-13), read 2026-10-14 on
the Wayback Machine; facts that bear on the design:
- ATI's *3D RAGE White Papers* (`atitech.ca/dr/cifwhite.html`, Oct 1997),
  a digest of the guide's chapters 1-2 and 4:
  - Between `RenderBegin` and `RenderEnd` the chip is in 3D mode and
    the application "will not be able to perform 2D operations, such as
    blitting, rectangle fills, or page flipping"; `RenderBegin` saves
    the 2D engine state and `RenderEnd` restores it. ATI recommends one
    3D block a frame. So a title's own `Lock`, `Blt` and `Flip` fall
    outside the block, and draining at `RenderEnd` covers them.
  - The viewport's top-left corner is the origin of the vertex
    coordinates, and moving it moves that origin. Its width cannot
    exceed the screen's. The scissor's origin is always the draw
    surface's top-left.
  - The Z buffer "must have the same pitch in pixels and height in scan
    lines as the drawing surface", be in video memory, and be 8-byte
    aligned.
  - Textures must be in video memory on PCI cards. On AGP, a DirectX 5
    or later DirectDraw may place them in system memory (the Q1 1997
    developer newsletter), so `apvLevels` can point at system memory.
  - Texture coordinates are "homogeneous (s, t, and inverse w)"; flat
    shading takes the last vertex's colour; fog takes its factor from
    vertex alpha, which is why fog and alpha blending exclude each
    other.
  - 3DCIF "cannot be used with Direct3D within the same application".
- ATI support note 3023 (1999) lists the chips 3DCIF supports: Rage I,
  II, II+, IIc, PRO and LT PRO; not the Rage 128. Its title list:
  WipeOut, Assault Rigs, MechWarrior II, Formula 1.
- ATI support note 2514: Wipeout and Assault Rigs fault reading
  `FFFFFFFF` when "some component of the 3D RAGE hardware and/or
  software" is missing. They fault rather than report.
- Psygnosis's Formula 1 Rage PRO patch page: the patch replaces
  `F1ATI.EXE` and applies only to CD part number 180-G3D014-100. It
  needs ATI's 4.35 or later drivers and fixes texture corruption that
  those drivers caused.
- The guide dates `C3D_3DCIFINFO`'s growth: one caps word in 4.02.0217
  (32 bytes), caps 2 to 5 in 4.03.0039 (48), caps2 defined in
  4.03.2511. `GetInfo` therefore sees 28, 32 or 48.
- ATI's driver chooses 555 or 565 with `SYSTEM.INI` `[Display]
  five6five=1`, read at boot; 555 is the default. Per marxveix it
  applies to Direct3D and 3DCIF alike, not to OpenGL. Reported on
  VOGONS, not checked here.
- None of these pages gives `u32ASICID` values.

## What 3DCIF is

From glrage and GLCIF's header on 2026-10-12; the SDK guide confirms each
before it is coded.

- **Exports** (`ati3dcif/ati3dcif.def`, "exports from ATI3DCIF.DLL
  version 4.12.2696"): 17 entry points, each exported twice, as
  `ATI3DCIF_<name>_lib` and as `__stdcall`-decorated
  `_ATI3DCIF_<name>@n`:
  `Init@0`, `Term@0`, `GetInfo@4`; `TextureReg@8`, `TextureUnreg@4`;
  `TexturePaletteCreate@12`, `TexturePaletteDestroy@4`,
  `TexturePaletteAnimate@16`; `ContextCreate@0`, `ContextDestroy@4`,
  `ContextSetState@12`; `RenderBegin@4`, `RenderEnd@0`, `RenderSwitch@4`;
  `RenderPrimStrip@8`, `RenderPrimList@8`, `RenderPrimMesh@12`. glrage
  leaves out `LoadObject@12` and `RenderIndexedPrim@12`, which ATI's DLL
  also exports; ours exports all 19 pairs, logging the two glrage omits.
- **State** (GLCIF `ati3dcif.h`, the `C3D_ERSID` enum, with a default
  per id): one `ContextSetState(hrc, id, pdata)`, ids 0 to 36 in this
  order: `C3D_ERS_FG_CLR`, `VERTEX_TYPE` (default `C3D_EV_VTCF`),
  `PRIM_TYPE` (`TRI`), `SOLID_CLR`, `SHADE_MODE` (smooth), `TMAP_EN`,
  `TMAP_SELECT`, `TMAP_LIGHT`, `TMAP_PERSP_COR` (`C3D_ETPC_THREE`),
  `TMAP_FILTER` (`MINPNT_MAG2BY2`), `TMAP_TEXOP`, `ALPHA_SRC` (`ONE`),
  `ALPHA_DST` (`ZERO`), `SURF_DRAW_PTR` = 13, `SURF_DRAW_PITCH`,
  `SURF_DRAW_PF`, `SURF_VPORT` (all four default to the desktop surface),
  `FOG_EN`, `DITHER_EN` (on), `Z_CMP_FNC` (`ALWAYS`), `Z_MODE` (off),
  `SURF_Z_PTR` = 21, `SURF_Z_PITCH`, `SURF_SCISSOR`, then the "RAGE3"
  (Rage Pro) additions 24 to 36: `COMPOSITE_EN`, `_SELECT`, `_FNC`,
  `_FACTOR`, `_FILTER`, `_FACTOR_ALPHA`, `LOD_BIAS_LEVEL`,
  `ALPHA_DST_TEST_ENABLE`, `_FNC`, `ALPHA_DST_WRITE_SELECT`,
  `ALPHA_DST_REFERENCE`, `SPECULAR_EN`, `ENHANCED_COLOR_RANGE_EN`.
  Composite is the Rage Pro two-texture path already measured in
  [2026-10-07-rage-pro-composite-multitexture.md](../decisions/2026-10-07-rage-pro-composite-multitexture.md).
  A context created and drawn with no surface state set draws to the
  desktop; the DLL must honour that default, not fault.
- **`GetInfo`** fills `C3D_3DCIFINFO`, whose `u32Size` the client sets:
  `u32FrameBuffBase` and `u32OffScreenHeap` (host pointers),
  `u32OffScreenSize`, `u32TotalRAM`, `u32ASICID`, `u32ASICRevision`, and
  five capability words. `CAPS1` flags fog, point, rect, Z buffer, CI4,
  CI8, `LoadObject`, dither control, enhanced perspective, scissor;
  `CAPS2` flags the Rage Pro's composite, clamp, destination-alpha blend
  and texture tiling. The header notes that build 17 and earlier had no
  caps words, so a client may check the size first.
- **Vertices:** two live types. `C3D_EV_VTCF` (3) is ten floats: x, y, z;
  s, t, w; r, g, b, a. `C3D_EV_TLVERTEX` (4) is Direct3D's `D3DTLVERTEX`
  (sx, sy, sz, rhw, BGRA colour, specular, tu, tv) followed by a
  `composite` rhw, tu, tv for the second texture. Types 0 to 2 (VF, VCF,
  VTF) are marked obsolete. TLVERTEX maps onto `V9X_R3D_ABI_VERTEX`
  almost as it stands.
- **Primitives:** `C3D_EPRIM_LINE` 0, `TRI` 1, `QUAD` 2, `RECT` 3,
  `POINT` 4, drawn as a strip (contiguous vertices), a list (array of
  vertex pointers) or a mesh.
- **Errors:** `C3D_EC_OK` 0 to `C3D_EC_CHIPCAPABILITY` 8.
- **Structures are versioned by size.** `C3D_TMAP` grew the Rage Pro
  fields `bClampS`, `bClampT`, `bAlphaBlend` and `eTexTiling` after
  `htxpalTexPalette`; GLCIF accepts a `u32Size` four bytes short of its
  own. The DLL accepts every size the census shows and reads only the
  fields that size covers.
- **Formats:** draw surfaces 1555, 565, 8888, 332, Y8, YUV422 (`C3D_EPF_*`);
  textures CI4, CI8, 1555, 565, 8888, 332, Y8, YUV422, 4444 and VQ
  (`C3D_ETF_*`). VQ is a 2x2-block codebook (`C3D_CODEBOOKENTRY`: four
  16-bit texels); the Knight VQ demo uses it, and the DLL can decode it
  to 565 or 1555 at `TextureReg` with no engine support. glrage inverts the 1555 alpha bit for OpenGL; which sense
  the render interface expects is checked, not assumed.
- **Textures:** glrage reads `apvLevels` once, at `TextureReg`, and its
  three titles run that way, so registered textures are evidently not
  rewritten in place by them. The census checks it for ours.
- **Hardware limits** the API reflects on the Rage II line: 16-bit Z,
  power-of-two textures to 1024, fog excludes alpha blending (CIF, cited
  in the Rage II specification and `2026-10-02-rage-iic-register-survey.md`).

## Design

### Module layout: `src/cif/`

As `src/glide/`. Platform files, the only ones including `<windows.h>`,
added to the `check-tree.ps1` allowlist:
- `cif_dll.c`: DLL entry, the exports, contexts, the log
  `C:\V9XDIAG\V9XCIF.LOG` with the census rules from `glide_core.c`.
- `cif_surface.c`: resolving an application pointer to a DirectDraw
  surface (below), and texture surfaces.

Pure logic, compiled into the host tests:
- `cif_vertex.c`: `C3D_EV_VTCF` and `C3D_EV_TLVERTEX` to
  `V9X_R3D_ABI_VERTEX`. For VTCF the conventions (pixel centre, whether
  `w` is W or 1/W, S/T as s/w or s, colour range) come from the guide
  and are pinned by tests built from census vertices.
- `cif_state.c`: the `C3D_ERS_*` state to `V9X_R3D_ABI_STATE`: Z, blend,
  texture lighting, filter, chroma key, fog. A state the interface cannot
  express is logged once per run and drawn with the closest mapping.
- `cif_texfmt.c`: 3DCIF texel formats to the interface's 565, 1555 and
  4444, only those the census shows. `glide_texfmt.c` already converts
  P_8 with a palette; sharing it changes external symbols, so that is
  raised before it is done (CLAUDE.md, "C").

### Pointers back to surfaces

The application passes a draw pointer, a Z pointer and texture-level
pointers that it obtained by locking surfaces it created on our HAL. The
DLL has to name those surfaces to the render interface. Options, in the
order to try:

1. **Replace the application's DirectDraw**, as glrage does. Ruled out:
   on Velocity9x the application's DirectDraw is our HAL and must stay so.
2. **Ask the HAL.** A render-interface entry that maps a linear address
   in the HAL's video-memory heap to the surface that contains it. The HAL
   already knows every surface it created. This is an ABI change in
   `r3d_abi.h` and is agreed before it is coded.
3. **Shadow and copy.** Draw into DLL-owned surfaces and copy to the
   application's at `RenderEnd`. Correct but costs a full-frame copy and
   breaks a title that reads its own Z buffer; the fallback only.

`GetInfo` makes option 2 cheaper than a surface search. The API itself
hands the client the framebuffer's host address, so a draw or Z pointer
minus the aperture base is a video-memory offset, and with the pitch and
pixel format that is enough to describe a render target, whichever
surface it belongs to. The ABI change could then be "render target at
offset, pitch, format" rather than "find the surface". Whether titles
derive their pointers from `Lock` or from `u32FrameBuffBase` is a census
question: GLCIF reports a base of 0 and Tomb Raider still runs, so that
title at least does not depend on it.

Option 2 also covers the games' own `Lock` writes between 3DCIF draws:
the engine draws into the surface the game locks, so nothing has to be
reconciled at `Flip`, provided the engine is drained before the lock.

Textures are simpler: copy the levels into a DLL-owned texture surface at
`TextureReg`, as glrage does, and re-read only if the census shows a
title rewriting a registered texture.

### 555 and 565

marxveix: ATI's default 16-bit layout is 555. Most CIF titles and demos
also work at 565, but not all: the Knight demo and Tomb Raider need 555,
and Tomb Raider's colours are wrong at 565. The title gets whatever
16-bit layout the mode set gives, so this is a driver question, not a
DLL one:
- The layout choice already exists: `v9x_highcolor_resolve`
  (`src/common/vbe_modes.c`) picks 555 when the engine wants it, and only
  the ViRGE does today (`v9x_dd_engine_wants_555`,
  `src/display16/dd16.c`).
- The Mach64 engine refuses non-565 targets
  (`src/chipsets/ati/mach64_policy.c`: "XRGB1555 waits for its own
  scene"). Drawing to 555 is unmeasured on the Rage XL and IIC.
- Making the ATI family's 16-bit layout 555 changes it for every
  Direct3D and OpenGL title on ATI, and Gen3 is measured as 565-only.
  Options: 555 on the ATI family alone, once the Mach64 draws to it; or
  565 everywhere and the DLL telling a 555-only title apart (it sets
  `SURF_DRAW_PF` to 1555 while the surface is 565). Which one is a
  decision for Michael after the census shows what the titles set.

### Game-folder DLLs and versions

Patches ship their own `ATI3DCIF.DLL` in the game folder (the Tomb
Raider: Unfinished Business patch carries five DLLs; gona's fix for Tomb
Raider is a 4.10.2640 copy beside the game). Windows loads the game
folder's copy first, so ours is only used once that copy is renamed. The
census records which DLL actually loaded. gona also shows titles failing
on older DLL versions (texture blending absent before 4.10.2640, Rage
XL needing 4.11.2692), so our version resource and `GetInfo` answers
should look like a late 4.1x build.

### Presentation

None. The application flips its own chain through DirectDraw on our HAL.
The DLL only has to have drained the engine before the application locks
or flips; the HAL's `Lock` and `Flip` already wait, which Phase 2 verifies
rather than assumes.

### Identity

`ATI3DCIF_GetInfo` reports a Rage II-class device with the limits in the
Rage II specification's section 8, as named constants citing the guide,
unless the census shows a title needing Rage Pro caps. For reference,
GLCIF satisfies Tomb Raider's ATI patch with ASIC id 1, revision 1, 16 MiB
total, a 4 MiB offscreen heap at address 0, `CAPS1` Z buffer and CI8, and
`CAPS2` clamp and destination-alpha blend.

### Defaults

No INI keys and no V9X3D verbs (memory: no gated defaults). The DLL rides
on the engine `Direct3D=` selects.

## Phases

### 0. Sources and a title

1. Fetch SDK-C02700 again, record where it lives, and check each constant
   the DLL will use from GLCIF's header against it. Check glrage's export list
   against the export table of an ATI `ATI3DCIF.DLL` (4.12.2696 or the
   version on the Rage XL driver disk).
2. Done 2026-10-12: the downloads, each in its own directory under
   `C:\Users\michael\Downloads\3dcif\`, outside the repository. Listed,
   not extracted or run.

   | Directory | File | Size | Source | Contents |
   |---|---|---|---|---|
   | `sdk41` | `3d_rage_pro_sdk_4.1.zip` | 18.0 MB | vogonsdrivers.com, file 497 | `ragesdk/include/ATI3DCIF.H`, `help/SDKAPI.HLP`, `docs/agp.doc`, `docs/vq_v3.doc`, samples with source, `LICENSE.TXT` (not yet read) |
   | `demopack` | `ATi3DCInterFace.7z` | 17.4 MB | hingetee.ee/library | marxveix's pack: SDK 4.1 again, plus an older `InterFace` tree with the Knight Demo, VQ and Morph sources; 561 files |
   | `alldlls` | `all_ATI3DCIF_DLL.zip` | 1.2 MB | gona.mactar.hu/ATI3DCIF/drivers | ATI's `ATI3DCIF.DLL`, 15 versions 4.02.0017 to 4.12.2696, including 4.11.2691, which gona's table omits |
   | `tombraider-ati` | `trati.zip` | 354 KB | infania.net/misc/gona/3D/files | `tombati.exe`, five codec and player DLLs, a readme; no `ATI3DCIF.DLL` |
   | `rage3gl` | `ati-rage3gl-win9x.7z` | 29.6 MB | hingetee.ee/library (2026-10-14) | marxveix's OpenGL pack, not 3DCIF: ATI Rage PRO OpenGL drivers `OGL1045` to `OGL1107` (about 33 builds), MCD builds `MGL1005` to `MGL1036`, SciTech GLDirect 3.00 and 5.02, `OpenGL ATIDBG`, a GL demo, a D3D/OGL benchmark, Rage Pro Tweaker; SHA-256 `b53197e0...cd150480`. Listed, not extracted |

   The SDK header replaces GLCIF's copy as the source for values, once
   its licence is read. Croc 1.0 needs no patch.

   The SDK guide (step 1) is in the SDK zip: `ragesdk/docs/ragesdk.pdf`
   is SDK-C02700 Rev 1.30, with `help/SDKAPI.HLP` beside it (the demo
   pack's `ATi3DCIF Guide` folder holds the same two files).

   Staged on A8U4I5 on 2026-10-13 (boot 448, Rage IIC AGP on the ati
   package from 0c26021 by WININIT rename, enable-ok, 1024x768x16, 565):
   `C:\3DCIF\PACK` (demo pack, includes SDK 4.1 under `Install`),
   `C:\3DCIF\DLLS\<version>\ATI3DCIF.DLL` (all 15),
   `C:\3DCIF\TRATI` (the Tomb Raider patch; the game itself is not on
   the machine), `C:\V9XATI17` (the package), and
   `C:\3DCIF\SWAP\TOATI.REG` / `TOV9X.REG`: `REGEDIT /s` one, then warm
   restart, to switch `Display\0006` between ATI's 4.11.2611 driver
   (files still in SYSTEM) and ours. SYSTEM holds ATI's own
   `ATI3DCIF.DLL`, version resource 4.11.2692, 179,200 bytes, whose bytes
   match none of gona's 15. The machine has no sound device, so Tomb
   Raider will not start there as it stands.
3. First runs are the demos, a 565 one before the Knight demo (555). The
   first title after them, with Michael: Croc 1.0 or Tomb Raider (needs a
   DirectSound device on the test machine and 555).
4. Decide whether to take a native reference run on A8U4I5's Rage IIC
   under ATI's driver (one card swap and boot each way; the Rage XL PCI
   has no stock CIF to compare against).

**Gate:** a decision record with the sources, the title and the export
list.

### 1. Census

1. `src/cif/cif_entrypoints.psd1` with every export; a census DLL from
   `scripts/build-cif.ps1`, adapted from `build-glide3.ps1`, with the
   KERNEL32/USER32 import audit and the manifest export check.
2. Copy it into the game directory, not SYSTEM. Run the menus and one
   level on A8U4I5's Rage XL. Collect `V9XCIF.LOG`.
3. A decision record: calls and counts; vertex types and primitives; every
   state id and value used; texture formats, sizes, palettes; whether
   textures are rewritten after registration; the draw and Z pointers and
   how they relate to the surfaces the title created (compare against a
   `V9XTRACE` of the HAL's surface creation); what the title does on a
   failure return; the ABI gaps.

**Gate:** check-tree green; record and evidence in
`docs/probe/a8u4i5-<title>-cif-census-<date>/`.

Done for the demos 2026-10-14 on the Rage IIC (not the Rage XL; the
IIC is the card fitted):
[2026-10-14-3dcif-demo-census.md](../decisions/2026-10-14-3dcif-demo-census.md).
Laputa stops after one frame under the census; open.

### 2. Pure logic with host tests

`cif_vertex.c`, `cif_state.c`, `cif_texfmt.c`, each with a test in
`tests/host/test_cif_*.c` written first from census values and seen to
fail.

**Gate:** `build-host.ps1` and `run-checks.ps1` green.

Done 2026-10-14: `src/cif/cif_vertex.c`, `cif_state.c`, `cif_texfmt.c`
(constants in `cif_api.h`), with `tests/host/test_cif_vertex.c`,
`test_cif_state.c` and `test_cif_texture.c` written first; against stubs
they failed 314 checks. The tests found one real bug before it shipped:
C3D_EZCMP and D3DCMPFUNC order EQUAL/LEQUAL and GREATER/GEQUAL
differently, so an offset of one swaps two pairs; the map is a table.
Conventions taken without a measurement, for Phase 3's probe to check:
- no pixel-centre offset;
- a non-MIP filter on a mipmapped texture picks the nearest level;
- the chroma key is compared in the texel's own precision;
- the viewport and scissor rectangles' bottom and right are exclusive
  corners (the guide contradicts itself; no demo sets either);
- the VQ codebook's 16-bit format is left to the caller.

Tiled (Rage PRO) textures and quadrangle strips are not converted.

### 3. Pointers, clears and triangles, plus a probe

The pointer-to-surface choice from the design, agreed and built. A probe,
`tools/diag/cif_probe_win32.c`, that creates its own DirectDraw chain as a
3DCIF title does, draws flat, Gouraud, textured, blended and depth-tested
primitives through our DLL, locks the back buffer and logs the pixels.

**Gate:** probe readback matches between the software-engine guest
(port 9878) and the Rage XL, differences listed and explained; the same on
the netbook's Gen3.

Progress 2026-10-14:
- **The render interface takes targets by address (ABI 5).** The draw's
  `raw_target` and `raw_depth` were agreed with Michael. The address
  check (`v9x_r3d_validate_raw`) was written test-first, and
  `d3d_core.c`'s `v9x_d3d_set_target_raw` repeats the offscreen
  branch's refusals. The HAL keeps no list of the surfaces it created:
  the statement in "Pointers back to surfaces" that it does was wrong.
- **The DLL is built.** `src/cif/cif_dll.c` and `cif_surface.c`
  replace the census and keep its logging. Textures are copied at
  `TextureReg` and converted on first use into one-level surfaces of
  the DLL's own. `RenderEnd` waits for the engine.
- **The engines take D3D's texture ops only.** An unlit texture with
  the fragment's alpha is drawn as MODULATE by a white vertex, and a
  texture whose alpha the draw does not read as an opaque 565 copy.
- **Cubes draws on the Rage IIC** (boot 456):
  [probe README](../probe/a8u4i5-rage-iic-3dcif-phase3-2026-10-14/README.md).
  Its background shows the 555 problem: it clears with a 555 colour.
- Not yet: the probe, mipmaps, VQ and tiled textures, 555 targets,
  meshes, TLVERTEX.

### 4. The title on the Rage XL

Menus and one level, then a return to the desktop. Evidence: the log,
`V9XTRACE` before and after, screenshots or in-DLL frame dumps, fps if the
game reports it (recorded, not compared). Each visible defect fixed with a
test first or filed in `docs/issues/`.

**Gate:** a decision record with captures and a probe README.

### 5. Packaging

`ATI3DCIF.DLL` in `[SourceDisksFiles]` and the system-directory copy, with
the same version-checked copy flag as `GLIDE2X.DLL`
([2026-10-09-glide-packaging-copy-flag.md](../decisions/2026-10-09-glide-packaging-copy-flag.md)),
so ATI's own DLL is kept where it is newer. Whether that is the right rule
on the ATI family needs thought: under Velocity9x, ATI's DLL cannot work,
so keeping it there is keeping a broken file. Decide in this phase.
`V9XCOPY.BAT` and the floppy space (ATI had 47 KB free after Glide 3) are
checked before anything is added. CHANGELOG under "Unreleased".

**Gate:** `run-checks.ps1`, a package build, a Have Disk install on a
machine.

## Hazards already known

- **A8U4I5 has no DOS mode** and a crashed fullscreen application may hold
  `V9XHAL.DLL` until reboot. Deploy with WININIT renames, never deletes.
- **Card swaps** on A8U4I5 can leave `BitsPerPixel=4` so `vga.drv` loads;
  set 16 by .reg (memory).
- **The Mach64 refuses** some states (translucent keyed sprites in Diablo
  II, 0.8 %). A 3DCIF title written for this chip should stay inside what
  it takes, but that is a hypothesis for the census.
- **The pointer model** may expose engine-drain gaps that Glide, which
  owned its surfaces, never hit: the application can lock a surface the
  engine is still drawing into without going through our DLL.
- **The SDK guide is absent locally.** GLCIF's header gives values but
  not semantics (coordinate conventions, what `RenderBegin` locks); no
  convention is coded from a header comment alone.
- **glrage notes low subpixel vertex accuracy** in Assault Rigs and
  Wipeout ("wobbly polygons") at high resolutions. At the titles' own
  640x480 that may be the games' integer vertices, not a wrapper defect;
  compare against the native reference run before chasing it.

- **32-bit desktops** break textures and menus in some CIF titles under
  ATI's own driver (gona). Run at 16 bits.
- **Vsync:** marxveix notes ATI's DX6 drivers lost the vsync-off switch
  the DX5 ones had (`atiddraw.ini`, `atidbg.ini`, SYSTEM.INI). Velocity9x
  already has its own (`VSync=`, [vsync-off-setting.md](vsync-off-setting.md));
  nothing to do here.

## Related request, not in this plan

marxveix asked whether our `GLIDE3X.DLL` could work on ATI's stock
driver, for his ati2ddad driver packs, with credit in the readme. As
built it cannot: it draws through `V9xRenderInterface`, which only
`V9XHAL.DLL` exports. On a stock driver it would need a different back
end (DirectDraw and Direct3D immediate mode) and would no longer be
Velocity9x's render path. That is a separate decision for Michael, and
so is whether to ship our `ATI3DCIF.DLL` to stock-driver users.

## Verification summary

Each phase ends at a named gate: check-tree, then build-host, then
run-checks, then evidence from A8U4I5 or the netbook. End to end: the
chosen title runs its 3DCIF path on the Rage XL through our
`ATI3DCIF.DLL`, with log, captures and a decision record committed, and the
desktop survives the exit.
