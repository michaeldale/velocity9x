# RIVA 128ZX: Half-Life in Direct3D, and SRCALPHA/ONE on the chip's add

Date: 2026-11-04

Machine: M-7V636C (remote help), RIVA 128ZX 8 MiB, Windows 98 first
edition, boots 26-27, desktop 800x600x16 at 5:5:5. Plan:
`docs/plans/nvidia-riva128-d3d.md`, step 6. Files:
`docs/probe/m7v636c-riva128-games-2026-11-04/`.

## The first run (boot 26, 5fc56040)

The owner's Half-Life (Sierra install in `C:\Games\Half-Life`) was already
set to Direct3D, 640x480x16 (HKCU `EngineType` 3). Started windowed, so the
agent's screenshots see the frames:
`hl.exe -window -w 640 -h 480 -console -condebug +map c1a0`. The window
came up at 400x300 regardless of `-w`/`-h`.

- It drew `c1a0` on the HAL: 213,784 batches, 3,155,356 triangles, Z on
  205,513, no engine timeout (`hl-before-SNAP.INI`). 4.6 fps windowed.
- **54,849 batches refused**, 54,050 of them for the blend
  (`M64Policy07`), last pair 0x00050002: SRCALPHA, ONE. A refused batch is
  dropped, so a fifth of Half-Life's batches were not drawn. The other 792
  refusals were opaque A4R4G4B4 under the alpha test (policy slot 11), as
  in V9XDDP.

## SRCALPHA/ONE

The chip's add (CONFIG ROP=ADD) is the saturating `src + dst` with no
factor (envytools' `nv03_pgraph_zpoint_rop`, and the probe's Z case). The
source is texel x vertex colour, so with the vertex colour first scaled by
the vertex alpha, the add is Direct3D's `src x alpha + dst`, provided:

- every texel's alpha is 1 (otherwise Direct3D also scales by the texel
  alpha, which the add cannot). The HAL reads level 0 once after each
  Unlock or Blt and refuses the draw if any texel has a partial alpha.
  Untextured draws use the white texture, alpha 1 throughout;
- fog is off: the chip mixes the fog colour in after the vertex colour,
  so it would add unscaled. The builder refuses that state.

`nv3_3d.c` gains `V9X_NV3_BLEND_ADD_SRC_ALPHA` (A1R5G5B5 read as X1R5G5B5,
`words.premultiply`, RGB x A / 255 rounded per vertex), with host tests.
`d3d_nv3.c` maps the pair and keeps the per-texture alpha check.

## Boot 27 (this build)

- V9XDDP: 592 batches drawn, 7 refused, as on boot 26. The 227 `Ok` keys
  match boot 26 exactly, 200 non-zero (`b27-V9XDDT.TXT`). V9XDDP has no
  SRCALPHA/ONE check, so this says only that nothing regressed.
- Half-Life, same start: 721,378 batches drawn, 10,068,641 triangles,
  **2,211 refused** (0.3%), none for the blend. 2,204 are the opaque
  A4R4G4B4 under the alpha test; the rest are V9XDDP's 7
  (`hl-after-SNAP.INI`; the counters run from boot, both runs included).
- Screenshots: the view from the lobby door is the same before and after
  (`hl-door-before-after.png`); it holds nothing visibly additive. Walking
  forward (`+forward` through the console) draws the lobby: the Black Mesa
  board, the scientist and guard, the desk and monitor, lightmaps, all in
  the right colours (`hl-lobby.png`). 5-7 fps windowed.

## Not seen

- Whether an additive draw now looks right: no screenshot shows one that
  can be compared. The counters say they are drawn, not how they look.
- Fullscreen, where the game flips: screenshots are blind there, and the
  owner would have to look.
- Opaque A4R4G4B4 under the alpha test, still refused (2,204 batches).
  What Half-Life draws with it is not known.
