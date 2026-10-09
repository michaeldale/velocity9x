# GLIDE3X.DLL's first frames: Rollcage and Diablo II on the Rage XL, 2026-10-10

Evidence for [2026-10-10-glide3-first-frames.md](../../decisions/2026-10-10-glide3-first-frames.md).

- **Machines:** A8U4I5 (P3, Windows 98 SE, ATI Rage XL PCI on Velocity9x
  0.15.0 `ati`, boot 377, agent 10.0.1.172); MICHAEL-NETBOOK (GMA 950, Gen3,
  boot 130, agent 10.0.1.254) for the Glide 2 gate.
- **DLLs:** GLIDE3X.DLL and GLIDE2X.DLL built from the tree that became the
  commit with this record (build ids `1ae5474-dirty`), copied into each
  game's folder.

| File | What |
|---|---|
| `RC-MENU.png`, `RC-RACE-GRID.png`, `RC-RACE.png` | Rollcage, first GLIDE3X.DLL: main menu, then Arcade with Enter: the start grid and the race. |
| `V9XGLD3-RC-RUN1.LOG` | That run's log: 1,437,354 triangles drawn, none refused. |
| `RC-FINAL-SCENE.png`, `V9XGLD3-RC-FINAL.LOG` | Rollcage on the final DLL: 1,104,450 drawn, none refused. Enter went in before the menu had come up, so this is a scene the game chose (probably attract mode); whether it is complete is not known. |
| `D2-TITLE.png`, `D2-MENU.png`, `D2-CHARACTER.png`, `D2-TOWN.png` | Diablo II after the chroma-key fix: title, main menu, character screen, the Rogue Encampment. |
| `V9XGLD3-D2-BEFORE-KEY-FIX.LOG` | Diablo II before the fix: the screen stayed black and 33,650 triangles were refused (`draw refused` lines: ARGB1555, MODULATE colour, fragment alpha). |
| `D2-FINAL-TOWN.png`, `V9XGLD3-D2-FINAL.LOG` | Diablo II on the final DLL: title to town; 1,923,489 drawn, 15,481 refused (translucent sprites, MODULATEALPHA, which the Mach64 cannot do). |
| `NB-NFS-A-PREFIX.png` | NFS II SE on the netbook, GLIDE2X.DLL before the key fix: HUD, mirror, rain, map. |
| `NB-NFS-B-KEYALPHA-ALL.png` | The first fix, texture alpha for every keyed format: the track map is gone. |
| `NB-NFS-C-KEYALPHA-NARROW.png` | The final fix, key alpha only for 565 and P_8: as A. |

All three NFS runs were driven the same way (Escape, Down four times at
1.5 s, Enter, 40 s); the race clock reads 0:30.2 to 0:30.3 in each.
