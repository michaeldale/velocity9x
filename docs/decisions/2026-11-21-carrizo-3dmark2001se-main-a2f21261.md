# Carrizo R7E on main a2f21261: 3DMark 2001 SE scores 807

Date: 2026-11-21. Machine: M-XP7053 (AMD GX-420GI, Radeon R7E
`1002:9874`, GCN 3), over the relay; boot 79 before, boot 80 after.
Evidence:
[`../probe/carrizo-main-a2f21261-m-xp7053-2026-11-21/`](../probe/carrizo-main-a2f21261-m-xp7053-2026-11-21/).

Michael asked for a 3DMark 2001 SE run on the latest driver.

## What ran

1. **Before:** the machine had the released 0.59.0 `ati` files
   (`6899f1cd`).
2. **Build:** the `ati` package from main `a2f21261`, in a clean worktree
   (`build-all-packages.ps1 -Family ati`). The main checkout had an
   uncommitted `d3d_core.c` change, which is not in this build. No gate
   was run on this build.
3. **Install:** the eight installed files by WININIT rename and `reset`.
   The staging folder was empty afterwards and all eight matched the
   package by hash (`deploy.log`).
4. **Run (boot 80):** 3DMark 2001 SE's benchmark with its saved settings:
   1024x768, 32-bit colour, 32-bit textures, D3D Software T&L, no
   antialiasing, game detail Low. V9XTRACE snapshots either side. No
   `VSync` key set, so vsync was on.

## Result

- Score window after 6.1 minutes: **807** 3D marks, with 7 of 7 game
  tests, 6 of 6 theoretical tests and 4 of 4 DX8 feature tests. 3DMark
  notes that the run did not use default settings.
- `EngineIdleTimeouts` 0 before and after.
- No `M64Policy*` counter changed.
- 3,475 flips.
- `DrawsIntoPresented` 9 and `FilterMagSeen` 6h after the run. The
  snapshot before was taken before the HAL had loaded, so both started
  at 0. Not looked into.

The last recorded score here was 724 on 0.54.0
([record](2026-11-17-carrizo-0.54.0-modulate-alpha-and-ring.md)).
Recorded, not compared.

## Not covered

- One run.
- No frame grabs, and nobody watched the screen.
- The probes were not run on this build.

The machine was left on the `a2f21261` files, not a released build.
