# Rage Pro class: table fog and 8-bit paletted textures

Date: 2026-10-08

Status: proposed; research only, nothing coded or measured. Prompted by a
Vogons user's notes (Rage XL/XC table fog, PAL8 through GLDirect 3.0 and
dgVoodoo 1.31, XL filtering). This plan separates what source code shows
from what forum users report, and orders the work so the cheap measurement
that decides the most comes first.

## What the tree does today

- **Fog.** Vertex fog only. The Mach64 class sets `ALPHA_FOG_EN` = 2 with the
  factor in specular alpha and the colour in `DP_FOG_CLR`
  (`src/chipsets/ati/mach64_draw.c:298-303`, `:417`). Raster caps are
  `ZTEST | FOGVERTEX` (`src/display32/d3d/d3d_mach64.c:174-175`). The core
  handles `FOGENABLE` and `FOGCOLOR` and ignores `FOGTABLEMODE`,
  `FOGTABLESTART/END/DENSITY` (`d3d_core.c:1818-1825`). Nothing advertises
  `D3DPRASTERCAPS_FOGTABLE`. Fog with blending and fog with specular are
  refused (`mach64_policy.c:411`, `:419`); the composite refuses both
  (`:333`).
- **Paletted textures.** None. The Mach64 and IIC format lists are 565, 1555
  and 4444 (`d3d_mach64.c:238-249`, `d3d_rage2.c:351`). DirectDraw palette
  callbacks are empty (`ddhal_core.c:2245-2247`). The ICD returns null for
  `glColorTableEXT` on purpose, because GLQuake enables paletted textures on
  any non-null answer (`gl_icd.c:3268-3291`). The Glide plan converts `P_8`
  on the CPU (`docs/plans/glide-2x-wrapper.md:77-84`); `grTexDownloadTable`
  only logs a checksum today (`src/glide/glide_dll.c:827-848`).
- **Chip split.** `v9x_m64_chip_class` puts every Pro, LT Pro, XL, XC and
  Mobility ID in one class (`mach64_engine.c:31-57`). Only the Rage XL PCI
  (4752, A8U4I5) and the Mobility-M (4C4D, Gateway) have been measured. No
  original Rage Pro is recorded as on hand.

## What the sources say

### Confirmed from source

- **Registers** (`atiregs.h` in xf86-video-mach64, its 3D section taken from
  the Mesa mach64 DRI driver):
  - Texel format is `DP_PIX_WIDTH` bits 31:28. **CI8 = 2**, 1555 = 3,
    565 = 4, 8888 = 6, 332 = 7, 4444 = 15. There is no 3D CI4 code.
  - Rage Pro: `TEX_PALETTE_INDEX` at byte 0x340 (dword 0xD0) and
    `TEX_PALETTE` at 0x37C (0xDF). On the Rage II (IIC) 0x37C is
    `TEX_PAL_WR` and 0x340 is `S_X_INC2`, so the upload path differs by
    generation.
  - No fog-table register exists. No 3D register is marked XL-only.
- **Mesa mach64**: vertex fog only, "can't fog if blending is on", the same
  rule our policy measured. It maps `GL_COLOR_INDEX` to CI8 but never writes
  `TEX_PALETTE` and never exposes a paletted extension. **No open driver has
  run a paletted texture on this chip.** The palette entry format, how
  `TEX_PALETTE_INDEX` auto-increments, and whether the palette survives a
  mode set or a 2D blit are unknown.
- **ATI's multitexture note** (archived devrel pages): 8-bit palettized is a
  Rage Pro texture format; two composite textures must have the same depth
  and, if both paletted, share one palette.
- **dgVoodoo 1.50b3 source** (1.31 is binary-only, so the "ATi@FOG" setting
  the user names cannot be read): Glide table fog is interpolated per vertex
  from `oow` through the game's 64-entry table and written to specular alpha
  (`DX7Drawing.cpp`); `FOGTABLEMODE` is never set. It is plain D3D vertex
  fog, which is why Glide games fog on a Rage Pro through it while native
  D3D table-fog games do not. Glide P8 textures use the driver's
  `DDPF_PALETTEINDEXED8` format when one is enumerated (`DX7Texturing`,
  `Texture.cpp`).

### Forum reports, not verified

- ATI driver 4.11.2598 and `W98_RXL_4_12_2647` give the Rage XL partial,
  partly broken table fog (Thief 2 about half the time); the same driver on
  a Pro, Pro Turbo or LT Pro gives none. **Same driver, different result per
  chip: most likely ATI emulated table fog per vertex and gated it on chip
  ID.** No one has posted the caps to prove `FOGTABLE` was set.
- Older ATI drivers enumerate PAL8; newer ones dropped it. GLDirect 1.0-3.0
  pass D3D PAL8 through to GL paletted textures; 4 and 5 do not.
- The Rage Pro and LT Pro cannot bilinear-filter alpha-blended textures; the
  XL can ("bulky" explosions in Half-Life, NFS5). Several users agree; no
  register-level explanation found.
- A 32 MB AGP aperture helping some ATI OpenGL builds concerns ATI's ICD and
  AGP texturing. Our A8U4I5 card is PCI and our textures are local. Not
  pursued.

## Plan

### Step 0: caps from ATI's own driver (one boot, no code)

On A8U4I5, swap to ATI's XL driver (the native baseline route from
`docs/decisions/2026-10-02-rage-iic-native-baseline-a8u4i5.md`) and record
with dxview or our HAL probe:

1. `dpcTriCaps.dwRasterCaps`: is `FOGTABLE` set? `WFOG`? `FOGRANGE`?
2. The texture format list: is there a `PALETTEINDEXED8` entry?
3. If there is a second ATI driver version on hand, the same for it.

This decides whether "XL table fog" is something ATI advertised (and so
emulated, there being no register) or a game-side fallback. Record it in a
`docs/decisions/` file either way.

### Step 1: table fog, emulated per vertex in the core

Table fog is policy, not hardware: a factor per vertex from depth, the
table mode and start/end/density, written into the specular alpha that
vertex fog already consumes. That is pure arithmetic and belongs in
host-testable C beside the core, not in `d3d_mach64.c`.

1. A pure function, `v9x_d3d_fog_table_factor(mode, start, end, density,
   depth)` returning 0..255, for `LINEAR`, `EXP` and `EXP2`. Host tests
   first: linear endpoints and midpoint, clamping outside start..end, EXP and
   EXP2 at density 0 and at known values, depth = start = end.
2. The core records `FOGTABLEMODE`, `FOGTABLESTART`, `FOGTABLEEND`,
   `FOGTABLEDENSITY` (floats in DWORDs) and, when the mode is not NONE and
   fog is enabled, overwrites each vertex's specular alpha before the chip
   map. Table fog takes precedence over the application's vertex fog, as
   the D3D spec orders it.
3. Advertise `D3DPRASTERCAPS_FOGTABLE` on every engine that already
   advertises `FOGVERTEX`. Per the same-design rule this is the whole Mach64
   class, the IIC and any other chip whose fog reads specular alpha, not the
   XL alone. Chips whose fog is folded on the CPU (`d3d_core.c:2031`) get it
   through the same factor.

Open question to settle with a probe, not by reading: which depth. DX5/6
table fog is defined on device Z; with `WFOG` it is eye-space W (1/rhw).
Games set start/end in eye units (hundreds) while z is 0..1, so a Z
interpretation fogs everything. Candidate rule: advertise `WFOG` and use
1/rhw when rhw is meaningful, Z otherwise. Measure it against the software
reference on two titles before choosing.

Known limit, inherited: fog is refused with blending, so translucent
surfaces stay unfogged. That is the hardware (IIC measured, Mesa agrees),
not this change.

Gate: host tests; then on the Rage XL, a HAL probe scene with each mode
against the software engine's output, and one table-fog title. Thief 2 is
the reported one; whether it or another table-fog title is installed on any
test machine is unknown, so a short census comes first.

### Step 2: PAL8 textures on the Rage Pro class

Hardware, so probe first. The register mechanics (palette index, data
writes, entry format) go in `src/chipsets/ati/`; which palette is loaded
when is policy in host-testable C.

1. **Probe** (HAL probe scenes on the XL, decision doc):
   - Upload a 256-entry palette through `TEX_PALETTE_INDEX` / `TEX_PALETTE`;
     find the entry format (8888? 565?) and whether the index increments.
   - Draw a CI8 texture with `DP_PIX_WIDTH` bits 31:28 = 2; check nearest,
     bilinear (filtered on the index or on the colour?), and alpha.
   - Whether the palette survives a 2D blit, a flip, a mode set and a
     DOS-box round trip (or not: then it is reloaded on every context
     switch, which we must know before relying on it).
   - The composite with two CI8 textures on one palette, since ATI says they
     must share one.
2. **DDI.** Enumerate `DDPF_PALETTEINDEXED8` as a texture format; implement
   the DirectDraw `SetPalette` / palette `SetEntries` callbacks for texture
   surfaces (`ddhal_core.c:2245`) so the HAL sees which palette a texture
   uses and when its entries change.
3. **Policy** (host-tested): one hardware palette, many textures. Track the
   palette identity and a version bumped by `SetEntries`; reload only when
   the bound texture's palette differs from the loaded one. A batch that
   switches palette per texture is a reload per switch; count them.
   Refuse a composite of two textures with different palettes.
4. **Consumers.** GLDirect 3.0 and dgVoodoo 1.x pick PAL8 up with no further
   work. Our Glide wrapper can download P_8 as CI8 instead of converting on
   the CPU, halving texture memory on 8 MB and 4 MB boards; that is a change
   to `glide-2x-wrapper.md`'s Phase on textures, and waits for this step's
   probe. Our ICD exposing `GL_EXT_paletted_texture` /
   `GL_EXT_shared_texture_palette` is a separate decision: the GLQuake
   behaviour at `gl_icd.c:3268` is why it is off, and Quake 2 would switch to
   8-bit uploads. Not in this plan.

Gate: host tests for the palette policy; probe scenes exact against a CPU
lookup; one PAL8 title on the XL. The reported one is Driver; FF8 on the
IIC. Installed or not: unknown, census first.

### Step 3 (later, separate): the Rage IIC

The IIC uses `TEX_PAL_WR` at 0x37C, a different path, and the BRender ATI
note lists PAL8 and fog as Rage II features. Only after Step 2 works on the
Pro class, and only if the IIC card is still on hand (not recorded since the
2026-10-03 swap).

### Not planned

- **Pro versus XL alpha-texture filtering.** We set `BILINEAR_TEX_EN`
  without regard to alpha, so on the XL we presumably filter. Without an
  original Rage Pro on hand there is nothing to measure, and the user's
  report is that both looks are wanted. If a Pro turns up, record what it
  does with a bilinear 4444 texture before changing anything.
- **AGP aperture size.** Concerns ATI's ICD, not ours.

## Order and cost

| Step | Machine | Code | Boots |
|---|---|---|---|
| 0 caps | A8U4I5, ATI driver | none | 1-2 |
| 1 table fog | host, then A8U4I5 | core + pure module | 1-2 |
| 2 PAL8 probe | A8U4I5 | probe scenes | 1-3 |
| 2 PAL8 DDI + policy | host, then A8U4I5 | ddhal, mach64, pure module | 1-2 |
| 3 IIC | if the card exists | rage2 | unknown |

Step 1 does not depend on Step 0's answer, but Step 0 tells us whether the
XL behaviour users remember was ATI's emulation; if it was, matching it is
the compatibility target.

## Sources

- xf86-video-mach64 `atiregs.h` (NetBSD mirror):
  https://ftp.kaist.ac.kr/NetBSD/NetBSD-release-10/xsrc/external/mit/xf86-video-mach64/dist/src/atiregs.h
- Mesa mach64 DRI driver, `src/mesa/drivers/dri/mach64/` (7.10.2 mirror):
  https://github.com/ps3dev/mesa-7.10.2-PS3
- ATI, Multitexturing with the Rage Pro (archived):
  https://web.archive.org/web/20001014235612/http://www.ati.com/na/pages/resource_centre/dev_rel/multex/index.html
- ATI developer relations index (archived; no Rage Pro register reference
  or fog material there):
  https://web.archive.org/web/20000229042406/http://www.ati.com/na/pages/resource_centre/dev_rel/devrel.html
- dgVoodoo source (1.50b3; 1.31 binary only): https://github.com/dege-diosg/dgVoodoo
- Vogons: https://www.vogons.org/viewtopic.php?p=1188029 (XL table fog,
  PAL8 by driver), https://www.vogons.org/viewtopic.php?p=1244487 (same
  driver, XL fogs, Pro does not),
  https://www.vogons.org/viewtopic.php?t=80075&start=100 (GLDirect PAL8,
  XL filtering), https://www.vogons.org/viewtopic.php?p=1352764 (NFS5 on Pro
  vs XL), https://www.vogons.org/viewtopic.php?p=1439040 (BRender ATI note,
  Rage II), https://vintage3d.org/rage3.php
