# Diablo II Glide 3 census on A8U4I5, 2026-10-10

Evidence for [2026-10-10-diablo2-glide3-census.md](../../decisions/2026-10-10-diablo2-glide3-census.md).

- **Machine:** A8U4I5 (P3 1 GHz, 440BX, Windows 98 SE), ATI Rage XL PCI on the
  released Velocity9x 0.15.0 `ati` package (build `11d348f`), boot 377.
- **Game:** Diablo II 1.0 shareware demo, `C:\Program Files\Diablo II
  Shareware`, started as `Diablo II.exe -3dfx`.
- **DLL:** the census `GLIDE3X.DLL` from `scripts/build-glide3.ps1`, copied
  into the game folder (build `6561d08-dirty`; run 1 without, runs 2 and 3
  with, the window sizing in `grSstWinOpen`). It draws nothing.

| File | What |
|---|---|
| `V9XGLD3-run1-intro-menu.log` | First run: start, intro, title and menu screens for about 45 s, then closed with `WCLOSE.EXE`. |
| `V9XGLD3-run2-escape-quit.log` | Second run: Escape at about 15 s quit the game from its menu. |
| `V9XGLD3-run3-menu-clicks.log` | Third run: blind clicks that reached the class screen but not the game. |
| `d2glide-imports.txt` | The 36 Glide imports of the demo's `D2Glide.dll`, from its import names. |
| `analyze_g3.py`, `analysis.txt` | The analysis over the three logs, and its output (`python analyze_g3.py`). |

The game was never visible: the census owns no display. Its menu
coordinates were worked out from a DirectDraw run, whose screenshots on
this card came back with a wrong palette and then stopped refreshing.
