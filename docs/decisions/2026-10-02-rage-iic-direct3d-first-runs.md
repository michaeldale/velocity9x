# Rage IIC Direct3D: first runs through the HAL

Date: 2026-10-02. Machine: A8U4I5 (10.0.1.172), ATI 3D Rage IIC AGP
`1002:4757` rev `7A`, Win98SE, desktop 1024x768x16.
Plan: [ati-rage-iic-hardware-3d.md](../plans/ati-rage-iic-hardware-3d.md),
Phase 5. Evidence:
`docs/probe/a8u4i5-rage-iic-registers-2026-10-02/phase5-b139`, `-b140`,
`-b141` (V9XDD.INI and V9XDD2.INI, the probe's report in two files - read
the last value of each key; V9XSNA7.INI, the V9XTRACE snapshot after it).

## Second boot of the Phase 4 scenes

A8U4I5 dropped off the network after the boot-136 runs and came back as
boot 138 (Michael: DOS mode is the suspect, so nothing here opens a DOS
box). All nine scene sets (`/tex /texprec /textri /textri2 /texbil
/texlod /texbil2 /texbil3 /texmix`) reproduced boot 136's pixels exactly;
the only differences were the two invalid scenes since removed from the
runner and the engine's idle GUI_STAT read before a run
(`BOOT138-ATIRX-*.TXT`).

## Deploy

`dbc0c2e`'s V9XDISP.DRV, V9XHAL.DLL and V9XMINI.VXD by plain WININIT
renames from 8.3 names in `C:\` (no `NUL=` lines), warm restart. Boot
139's V9XHW.INI: `Direct3D=hardware-rage2`, `Direct3DMode=hardware`. The
replaced files are kept in `build\a8u4i5-backup-06f760b`.

## Boot 139: 3 of 566 batches reached the engine

`M64Draws=3`, `M64Refused=563`, `M64RefuseLast=7` (emit), no FIFO or idle
timeout. The Rage II's FIFO is the pre-VTB 16-entry one and
`v9x_m64_reserve` refuses more than 16 entries at once; d3d_rage2.c handed
whole state and trapezoid streams (up to 36 dwords each) to one
`v9x_m64_emit_batch`. The scene runner had always emitted in eights. Fixed
by emitting in chunks of 8.

## Boot 140: the engine draws

`M64Draws=444`, `M64Triangles=447` (380 textured, 50 depth, 119 blend, 4
fog), `M64Refused=122`, all by policy, FIFO and idle timeouts and resets 0.
152 of the probe's 219 verdicts pass, among them the triangle, shape and
sub-pixel scenes, Z compare and write mask, perspective (`PerspOk`), every
texture size's halves test at 8-256 in 565/1555/4444, modulate, decal,
alpha decal, bilinear, 1555/4444 alpha, the three alpha-blend curves, vertex
alpha, depth fog, colour key, and 81 of the texture-matrix cells.

One defect: `D3DFogTex` drew an opaque ARGB1555 texel unfogged at every
factor (all three reads `0x07C0`). An alpha texture sets TEX_MAP_AEN, and
with it the blend unit's source alpha - here the fog factor - is the
texel's (Phase 4 A1); `/texmix`'s fog scenes had used a 565 texture. Fixed
in the policy: under fog TEX_MAP_AEN is dropped, and a draw that needs it
(the alpha mask, alpha decal) is refused as fog-with-texture.

## Boot 141

`D3DFogTexOk=1` (green `0x07C0`, a mix `0x03EF`, blue `0x001F` at fog
255/128/0); 153 of 219 pass; no other verdict moved.

## The 66 that fail, each accounted for

| Verdicts | Why |
|---|---|
| 18 `TexM_*_lin` | TEXTUREMIN LINEAR with TEXTUREMAG NEAREST: the pair the chip draws nothing for when magnifying (Phase 4 B4); refused (`M64Policy10=81`) |
| 18 `TexM_*_trilin`, `D3DMipmapLevelSelect`, `D3DTrilinearBlend`, `Chain*` (7), `MipGap`, `MipLadder`, `MipTri`, `MipTriDegraded` | Mip-mapping: not measured, so only level 0 is sampled |
| `D3DSpecularGouraud`, `D3DSpecularTex` | No specular; refused (`M64Policy17=2`) |
| `AlphaCurveF1-4` | The probe's private alpha-force state; refused (`M64Policy19=36`) |
| `Tex8Clamp` | CLAMP sampled at u = 1.25; the chip only wraps (`M64Policy11=1`) |
| `D3DBaseTexture`, `D3DTiledTexture`, `D3DTiledNegative` | Read `0x07C0` where the probe expects `0x07E0` exactly: the 1555 green reaches the target zero-extended (Phase 4 F2). The wrap at u = 2 and at negative u samples the right texel |
| `Mixed*` (4), `D3DZP*` (2) | Not run: they need `/mixed` and `/zprivate` |
| `FlipPixel` | GDI `GetPixel` after a flip; not a Direct3D path, and the report writes no raw values to say more |

`M64Unrenderable=57`: triangles the setup skipped as inexpressible
(slivers steeper than the colour interpolators' range, or a 1/w that is not
positive). Which scenes they came from is not recorded.

## Final Reality, boot 141

Standard run, Direct3D on-board, no sound, driven by agent input. It
completed: **3.33 Reality Marks** (2D 6.72, 3D 1.28, bus 4.03;
`phase5-b141/FR-RESULTS.png`). Recorded, not compared: no frame was
presented. Every screenshot of the 3D tests was black, `FlipHandled=0`,
`FlipDeclined=3667` - the flips are declined and nothing presents them
([issue](../issues/2026-10-02-rage-iic-flips-never-presented.md)). The
engine itself (`phase5-b141/V9XSNA7-AFTER-FR.INI`): 559,842 batches,
1,641,004 triangles, every one textured and nearly all depth-tested,
266,661 blended, **no refusal** and no FIFO or idle timeout or reset;
13,056 pieces skipped as inexpressible and 41,717 as covering no pixel.

## Boots 142-144: flips, then the depth compare

- **Boot 142** (`1348660`, the CRTC flip): every flip handled, none
  declined, 20 flips in 331 ms (vsync-paced). Michael at the monitor:
  Final Reality's 2D tests fine but its 3D black, 3DMark 99 missing most
  textures - with every draw accepted. A read-only `ATIRX /vramdump`
  mid-Robots found the front, back and Z buffers untouched (Z all 0xFFFF)
  and `Z_CNTL=0x31`: Direct3D's LESSEQUAL mapped to the chip's EQUAL. The
  two number their compares in different orders; `func - 1` was right
  only for the five V9XDDP's Z scenes happen to use. `V9XDDP /bigtarget`
  (a 500x400 triangle on 640x480) passed the same boot, ruling out size.
- **Boot 143** (`ede7979`, an explicit compare table): the dump shows a
  complete, correctly textured and perspective-correct Robots frame in
  the displayed buffer (`phase5-b143/FR-ROBOTS-FRONT-FROM-VRAM.png`),
  `Z_CNTL=0x121`. Michael: Final Reality "looking good", 3DMark 99 "also
  looking good".
- **Boot 144** (the machine restarted after boot 143's runs; not by the
  agent): 163,476 batches and 507,626 triangles in its first minutes, no
  refusal, no FIFO or idle timeout or reset, 362 flips handled and none
  declined (`phase5-b143/V9XSNA7-AFTER-FR-3DMARK.INI`).

## Not yet run

3DMark 99's score (not yet read); `/mixed` and `/zprivate`; what the
26,145 inexpressible and 77,736 empty pieces of boot 144 were.
