# RIVA 128ZX: Unreal Tournament in Direct3D, translucency drawn as an add

Date: 2026-11-04

Machine: M-7V636C (remote help), RIVA 128ZX 8 MiB, Windows 98 first
edition, boots 34-35, desktop 800x600x16 at 5:5:5. Plan:
`docs/plans/nvidia-riva128-d3d.md`, step 6. Files:
`docs/probe/m7v636c-riva128-games-2026-11-04/ut-*` and `b35-V9XDDT.TXT`.

## Setup

The owner's Unreal Tournament (`C:\Games\Unreal Tournament`) was set to
Glide, for the Voodoo 1. Its `UnrealTournament.ini` was saved to the host,
switched to `D3DDrv.D3DRenderDevice` with `StartupFullscreen=False`, and
put back unchanged afterwards (hash checked). Its D3D section already
named the RIVA 128/128ZX. The intro flyby (no input) runs windowed at
640x480. Killing UT leaves a recovery dialog on the next start; its "Run
Unreal Tournament" button is at (400,338).

## Boot 34 (88363dc5)

- 31,547 batches drawn, 382,348 triangles, no engine timeout
  (`ut-before-SNAP.INI`).
- **5,488 refused for the blend**, last pair 0x00020004: ONE,
  INVSRCCOLOR, UT's translucency. Dropped, so the sky layers, the
  billboard glow and the other effects were missing
  (`ut-before-6-shots.png`).

## ONE/INVSRCCOLOR as the add

The chip scales the destination by the source alpha or the destination
colour, never by the source colour per channel, so src + dst x (1 - src)
has no exact form. Michael chose the approximation (2026-11-04), as for
the Matrox HAL's additive key: the saturating add, src + dst. It is the
same where either side is black, and brighter where both are bright. Not
added to the blend caps.

## Boot 35

- V9XDDP unchanged: 216 of 227.
- UT: 32,603 batches drawn, 4,807 of them blended (from 160), **137
  refused** (5 V9XDDP's, 132 ONE/SRCALPHA, last pair 0x00020005), no
  timeout (`ut-after-SNAP.INI`).
- The purple sky layer over the city and the "Do you have what it takes"
  billboard now draw (`ut-after-6-shots.png`); the rest of the intro
  matches the first run.

## Not done

- ONE/SRCALPHA (src + dst x As), 132 batches, still refused. The chip has
  no destination factor from the source alpha with the source kept whole.
- Whether the add looks too bright anywhere: the frames sampled show no
  bright-on-bright translucency to judge.
- Fullscreen, and UT in play rather than the intro.
