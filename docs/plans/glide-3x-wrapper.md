# Glide 3.x on Velocity9x: a glide3x.dll for Diablo II

Date: 2026-10-10

Status: Phase 0 in progress. The census DLL is built. On A8U4I5 (Rage XL)
it has measured Diablo II's menus
([2026-10-10-diablo2-glide3-census.md](../decisions/2026-10-10-diablo2-glide3-census.md))
and Rollcage's start-up and unattended 3D
([2026-10-10-rollcage-glide3-census.md](../decisions/2026-10-10-rollcage-glide3-census.md)).
Both games' in-game census is open.

## Context

Diablo II's Glide renderer, `D2Glide.dll`, needs `glide3x.dll`. Velocity9x
ships Glide 2 (`GLIDE2X.DLL`, [glide-2x-wrapper.md](glide-2x-wrapper.md)),
which left Glide 3 out of scope as a later milestone. Glide 3 is a
different API, not a revision: vertex layouts and vertex arrays instead of
`GrVertex`, `grGet`/`grGetString` instead of the hardware query structs,
and contexts from `grSstWinOpen`. It needs its own DLL.

The approach is the Glide 2 one: a user-mode DLL that draws through
`V9xRenderInterface` (`include/velocity9x/r3d_abi.h`) into DirectDraw
surfaces it owns, so every engine that serves the OpenGL ICD and Glide 2
serves Glide 3 too.

- **First game:** Diablo II, the 1.0 shareware demo, on A8U4I5.
- **Second game:** the Rollcage demo (Psygnosis 1999), also on A8U4I5. It
  adds a depth buffer, `grDrawTriangle` and the mode query.
- **First engine:** the Rage XL (Mach64). It has no software fallback for a
  draw it refuses, so every state the game uses must be one the engine
  takes, or the DLL must lower it to one.
- **No Voodoo reference run.** The census decides what is needed.

## Licence rule

As for Glide 2: 3dfx's `glide.h`, `sst1vid.h` and the Glide 3 reference may
be read for facts (prototypes, constants, struct layouts); nothing is
copied.

## Design

### Module layout: `src/glide3/`

- `glide3_entrypoints.psd1`: the 99 exports of 3dfx's `GLIDE3X.DLL`, name,
  argument bytes, return kind, and whether Diablo II imports it. Generated
  stubs come from `scripts/lib/glide3-exports.ps1`.
- `glide3_census.c`: Phase 0 only, replaced by the real DLL in Phase 2.
- The real DLL links the engine GLIDE2X.DLL draws with. It was split out
  of `glide_dll.c` into `src/glide/glide_core.c` (`glide_core.h`): the census
  log, texture engine and surface cache, batching, drawing, clears, window
  and LFB. `glide_dll.c` keeps only the Glide 2 exports, and
  `glide3_dll.c` will be the Glide 3 front end over the same core,
  alongside `glide_texfmt.c`, `glide_texmem.c`, `glide_state.c` and
  `glide_surface.c`. The core works in Glide 2's terms (GrVertex floats,
  Glide 2 LOD and aspect), so the Glide 3 front end converts into them. It
  adds a vertex-layout module with host tests: `grVertexLayout` state, and
  reading a vertex at any declared offset into a GrVertex.

### Identity

`grGet` reports one Voodoo3-class board: one TMU, 4 MiB of frame buffer and
4 MiB of texture memory, maximum texture 256, aspect 8:1. `grGetString`
reports "Voodoo3", "3Dfx Interactive", "Glide" and a version naming
Velocity9x. Diablo II accepted these in the census.

### Presentation

As Glide 2: a fullscreen exclusive DirectDraw flip chain. Diablo II closes
and reopens Glide at a new resolution (640x480 to 800x600) within one
process; `grSstWinClose` must release the chain completely so the reopen
gets a new mode.

### Logging

The real DLL keeps the census logging (rules as in `glide3_census.c`), so
the in-game census comes from the first run that shows frames.

## Phases

### 0. Census (Diablo II on A8U4I5)

1. Done: the census DLL, `scripts/build-glide3.ps1`, with an import and
   export audit (KERNEL32 and USER32 only; exactly the manifest's 99).
2. Done: the menus, recorded in the decision above.
3. Open: in-game. Either drive the game blind with the coordinates from a
   DirectDraw run, or take it from the Phase 3 DLL once frames show.
4. Done: Rollcage, the second Glide 3 title
   ([2026-10-10-rollcage-glide3-census.md](../decisions/2026-10-10-rollcage-glide3-census.md)).
   Its start-up, settings dialog and two unattended 3D runs are measured.
   It needs `grQueryResolutions` to start at all. Depth is a W-buffer from
   Q with one aux buffer; colour is float RGB; drawing is `GR_POLYGON`,
   `GR_TRIANGLES` and `grDrawTriangle`. It uses no mipmaps, no fog and no
   LFB. A player's race is taken from the real DLL, as for Diablo II.

**Gate:** check-tree green; the decision record and its evidence committed.

### 1. Pure logic with host tests

- Vertex layout: offsets, enable and disable, packed ARGB in RGBA order, Q0
  and ST0 scaling, and a quad fan to two triangles.
- `grGet` and `grGetString` answers as a table with a test.
- Texture memory arithmetic for Glide 3's LOD and aspect encoding
  (`grTexTextureMemRequired`, `grTexCalcMemRequired`).

### 2. Window, clear and swap

`grSstWinOpen` and `grSstWinClose` on the flip chain, including the 640x480
to 800x600 reopen; `grBufferClear` and `grBufferSwap`. Gate: Diablo II's
intro and title draw their clears and swap without a fault on the Rage XL.

### 3. Textured quads

P_8 textures with palettes, chroma key, the three blend modes, constant
alpha. Gate: the Diablo II menus draw correctly on the Rage XL; the census
log then covers the game.

### 4. In game

Whatever the in-game census adds. Gate: Diablo II's first town runs and
draws correctly on the Rage XL.

### 5. Packaging

`GLIDE3X.DLL` in the packages, installed with the same version-checked
copy as `GLIDE2X.DLL` so a 3dfx card's own is kept.

## Hazards already known

- **A8U4I5's DirectDraw screenshots** of Diablo II came back with a wrong
  palette and then stopped refreshing; the census itself shows nothing.
  Read the log, not the screen.
- **Escape on Diablo II's main menu quits the game.** The title screen needs
  a click first; "Single Player" is at about (400, 308) at 800x600.
- **End the game with `C:\V9XDIAG\WCLOSE.EXE II.EXE`**, never a restart
  while it holds the screen.
- **Each family installs `V9XDISP.DRV` under the same name.** Swapping the
  card means installing that card's package again.
