# Oland: 3DMark 2000 and 2001 SE run to a score through the HAL

Date: 2026-11-16

Status: **measured, one boot.** W9QI0DAD (Oland XT `1002:6610`, 2 GiB),
boot 51 (BootId `2026-11-16 22:29:46.98`), the same boot and the same
`636a2fcd-dirty` package as
[`2026-11-16-oland-direct3d-hal.md`](2026-11-16-oland-direct3d-hal.md).
No code changed for these runs. Step 5 of
[`../plans/ati-oland-gfx6-3d.md`](../plans/ati-oland-gfx6-3d.md), first
half. Evidence:
[`../probe/oland-w9qi0dad-2026-11-16/`](../probe/oland-w9qi0dad-2026-11-16/)
`3dmark2000-2`, `3dmark2001se-1`, `3dmark2000-1-killed-by-script`.

## Results

| Program | Settings (as 3DMark reports them) | Score | Time |
|---|---|---|---|
| 3DMark 2000 | 1024x768, 16-bit colour, 16-bit textures, Z16, triple buffer | 3808 | 3.0 min |
| 3DMark 2001 SE | 1024x768x32, T32, Z16, D3D software T&L, game detail Low | 1486 | 5.6 min |

Both from the score dialog's own text (`score-window.json`) and its
screenshot (`end.png`). 2001 SE says "not run using default settings":
the saved project on the box has Low game detail and 0 of 4 Pro tests.
Scores are recorded, not compared.

Each run's ten V9XTRACE grabs (`grabs-sheet.jpg`) render correctly: the
2000 helicopter scene with its alpha HUD and rotor blur, and 2001 SE's Car
Chase at low and high detail with headlights, smoke and the HUD.

## Counters (V9XTRACE snapshots)

| Counter | before 2001 SE | after 2001 SE |
|---|---|---|
| FlipHandled | 19,715 | 24,945 |
| EngineIdleTimeouts | 0 | 0 |
| BatchesEngineRefused | 2 | 2 |
| DrawsIntoPresented | 0 | 11 |

- The two refused batches date from before either benchmark ran.
  `M64TexOpRefused` is 100h, the refusal V9XD32 recorded. 3DMark
  added none.
- **DrawsIntoPresented 11 is unexplained.** Gen3 recorded the same count
  of 11 in 2001 SE (memory note, 2026-11-07). That suggests it comes from
  3DMark, not this chip, but nobody has measured it.
- No timeouts and no fault dialogs.

## Not a driver result

- **Run 1 was stopped by the script, not the driver.** `v9xctl cat`
  prints the file to the console, so the script read empty values and
  took the flip counter for stalled. It closed 3DMark after 1.8 minutes
  while flips were still rising (3,673 to 5,888). Its grabs are kept
  (`3dmark2000-1-killed-by-script`). The script now reads `cat -Json`.
  Run 2 was the same mistake, caught while the benchmark was still running.
  The fixed script was attached to it, so run 2 has no before snapshot.
- **The display shows as "Velocity9x VBE-generic display".** Both of
  W9QI0DAD's Velocity9x display keys (`Display\0001`, `0002`) were
  installed through `Velocity9x.Install.Manual` against `PCI\CC_0300`,
  not the `1002:6610` row. This is the INF's description only: the
  driver picks its backend from the PCI ID at run time, and the HAL ran
  the Oland mode. A clean install through the Oland row has not been
  tried.

## Next

Quake 2 and 3 through the ICD, and the Glide probes. The GL and Glide
DLLs on the box predate this branch and need deploying first.
