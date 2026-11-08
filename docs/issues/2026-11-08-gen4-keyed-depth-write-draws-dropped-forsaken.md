# Gen4: colour-keyed draws that write depth are dropped, so Forsaken renders dark

Date: 2026-11-08. Status: fixed in source (two passes), measured on the
GM45 at the title and credits. Gameplay not run. See "Fix" below.

Machine: issue 22's reporter's HP t5740 (`M-6B21TJ`, W9QI3DAB), GM45
`8086:2A42`, on the 0.40.0 release (`5977e375`), 640x480x16 in game.
Captures: `docs\probe\w9qi3dab-gl40-audit-2026-11-08\forsaken\`. Machine
audit: [`../decisions/2026-11-08-w9qi3dab-gm45-gl40-audit.md`](../decisions/2026-11-08-w9qi3dab-gm45-gl40-audit.md).

## Report

The owner, by remote-help chat (2026-11-08): Forsaken renders "in complete
darkness" under both its software renderer and Direct3D; Half-Life draws
normally.

## What was measured

- **Both executables draw through our HAL.** `ForsakenSW.exe` creates Direct3D
  contexts on our driver (V9XTRACE `Process1=FORSAKENSW.EXE d3d-contexts=3`)
  and submits execute buffers (`D3dRenderPrimitiveCalls`). So "software"
  is not a separate path here, which explains why both renderers fail the
  same way.
- **The 2D screens draw.** The flip-path grab (`V9XTRACE -grab`) shows the
  legal screen and the colour-keyed credits text correct (`sw-legal.png`,
  `sw-credits.png`).
- **The 3D title scene does not.** `ForsakenSW.exe`'s title is a logo with
  saturated halos over a white, noisy lower half, the same in four grabs
  (`sw-title-sheet.png`). `ForsakenHW.exe`'s grab is black
  (`hw-title.png`).
- **Most of its batches are refused, reason 7.** Over 15 s of the
  software exe's title, `BatchesEngineRefused` and `M64Policy07` both rose
  by 140,170 (`sw-menu-A.INI` to `sw-menu-B.INI`). Over about 12 s of the
  HW exe, both rose by 166,087 (`hw-A.INI` to `hw-B.INI`). No other reason
  moved. Reason 7 is `V9X_D3D_G4_REFUSE_KEY`.
- **The state census names the draws** (`-omit:2147483648`;
  `sw-title-census.INI`, `hw-B.INI`; key layout in `d3d_state.h`):

  | Key | State | Fate |
  |---|---|---|
  | `24780912` | MODULATE, colour key, depth test, **depth write**, no blend | refused |
  | `24581152` | MODULATE, colour key, depth test, no write, blended | drawn |
  | `24481152` | MODULATE, colour key, no depth, blended | drawn |

  The software exe's title used only `24780912` (107,095 draws).
- Table fog is not involved. `FogTableBatches` did not move during the
  title. Its earlier values (linear, 0 to 1024) come from an earlier run.

## Cause

`v9x_d3d_g4_map` (`src/display32/d3d/d3d_g4.c`, the end of the function)
implements a colour key with `REPLACE_BLACK` plus an alpha test (GREATER 0),
then refuses any draw that has both the alpha test and depth writes. That
combination hung the GM965 in every kernel form tried
([`2026-10-21-gm965-direct3d.md`](../decisions/2026-10-21-gm965-direct3d.md),
boots 16-23). The comment there says such a draw "is refused and drawn by
the fallback". On the Direct3D DDI path that is false. No fallback exists
for Gen4: `v9x_r3d_fallback` lists only i9xx, MGA and Rage 128. The DDI
entry points count `batches_engine_refused` and answer `DD_OK`. So
Forsaken's opaque world geometry, every piece of it keyed and
depth-writing, is dropped. Only its blended passes reach the screen. The
title's blown-out look fits blended halos drawn without the base layer
under them, but that is not proven.

The same mapping is in HEAD (0.44.0 unreleased), so 0.41.0 to 0.43.0 have
the same defect. It affects every Gen4 part (965, GM965, G4x, GM45,
Ironlake through `d3d_g4.c`) and any title that keys opaque, depth-writing
geometry.

## Options, not chosen

1. Draw the keyed texels black: no alpha test, depth writes kept.
   Geometry appears, with black where the key should cut holes.
2. Two passes: colour with the alpha test and no depth write, then depth
   alone with colour writes masked and no alpha test. Colour is exact.
   Depth is written over the holes too, which hides what is drawn behind
   them later.
3. Lift the hang itself: post-shader depth (mode N) on Gen4. Parked in
   2026-10-21 after six variants.

## Not done

- No fix built or run.
- No in-game run: the game was driven only to the title and credits.
- Whether a keyed draw with depth writes also hangs on the GM45, which is
  G4x and not the GM965's 965 core, was not tested. The refusal was
  measured on the GM965 only.

## Fix (2026-11-08, option 2 with the passes reversed)

`v9x_d3d_g4_draw_keyed_depth` (`src/display32/d3d/d3d_g4.c`). The map
no longer refuses a keyed draw whose key alpha test meets depth writes.
It marks the draw for two passes. Each pass uses a combination the GM965
ran without hanging:

1. Depth only: the diffuse kernel with no texture, no alpha test, no
   blend, every colour channel's write disabled, and the draw's own depth
   test with writes on.
2. Colour: the keyed texture, its alpha test, depth writes off, and the
   depth test EQUAL. ALWAYS and NEVER keep their function.

Depth goes first because a colour pass without depth writes loses
occlusion between triangles in the same batch. Against the pre-pass's
depth, EQUAL picks the triangle that won each pixel. An application's
own alpha test with depth writes is still refused (reason 8). That case
was not part of this fix.

`run-checks.ps1` passed (host tests and every family package). The GM45
build is `0.44.0 e9d361b5-dirty`, the only change being this one. It was
installed on M-6B21TJ by WININIT rename, all eight files hash-checked,
boot 11. That build is left installed there.

| Run | Window | Keyed+Z draws (census `24780912`) | Refused | Engine timeouts |
|---|---|---|---|---|
| `ForsakenSW.exe` title (`fix-sw-A/B.INI`) | ~10 s | 42,533 | 0 | 0 |
| `ForsakenHW.exe` menu (`fix-hw-A/B.INI`) | ~10 s | 57,192 | 0 | 0 |

Before the fix the same runs refused 140,170 and 166,087 batches.

- `fix-hw-title.png`: the HW exe's main menu room. Walls, floor, pipes and
  the menu stack all draw, where 0.40.0 grabbed black.
- `fix-hw-credits.png`: the credits now draw over the Probe backdrop.
  0.40.0 drew them on black.
- `fix-sw-title.png`: the SW exe's title room draws, darker than the HW
  exe's, with sparse black speckles on the textures. The HW exe's frames,
  which draw the same keyed+Z state with EQUAL, have none. So the
  speckles are more likely the SW exe's own texture data (black texels
  keyed out) than missed EQUAL tests. Not proven.

Still not done: a level (Enter on the menu did not select, and the game
fell back to its credits), a comparison against another driver, any other
Gen4/Gen5 machine, and a hang check on the GM965 itself. The hang was
measured on the GM965, and the passes were built to avoid it there, but
they have not run there.

### Probe run on the GM45 (same day)

`run-release-checks.ps1 -Machine gm45`, with the probes rebuilt as
`e9d361b5-dirty`. The package was reinstalled (8 of 8 hash-checked,
boot 11 to 12). Evidence: `docs\probe\w9qi3dab-gl40-audit-2026-11-08\release-check-gm45\`.

| Check | Result |
|---|---|
| V9XD32 default | PASS, `Failures=0` |
| V9XD32 `/ddi5` | PASS, `Failures=0` |
| V9XGLP | PASS. The multitexture REPLACE and DECAL sub-checks read 0 (`SgisReplaceOk`, `SgisDecalOk`, `Arb*ReplaceOk`, `Arb*DecalOk`), as on Ironlake in the 0.32.0 checks (`780ddde9`) and on the GM965 on 2026-10-20 (`boot18-V9XGLP.INI`). Those are older Gen4/Gen5 failures, not caused by this change |
| V9XGLIDP | PASS, failures=0 |
| V9XR3DP | COMPLETE |

This is a regression check only. V9XD32's colour-key test (T20) draws
with depth off, so it never reaches the two-pass path. Forsaken is still
the only thing that has. There is no earlier probe run on this machine
to compare against.
