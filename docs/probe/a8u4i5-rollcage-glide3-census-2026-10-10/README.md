# Rollcage demo Glide 3 census on A8U4I5, 2026-10-10

Evidence for [2026-10-10-rollcage-glide3-census.md](../../decisions/2026-10-10-rollcage-glide3-census.md).

- **Machine:** A8U4I5 (P3 1 GHz, 440BX, Windows 98 SE, DirectX 9.0c), ATI
  Rage XL PCI on the released Velocity9x 0.15.0 `ati` package (build
  `11d348f`), boot 377.
- **Game:** Rollcage demo (Psygnosis 1999), installed by its InstallShield
  setup to `C:\Program Files\Psygnosis\Rollcage` from `C:\RCDEMO`. The
  Glide build was started directly as `Glide\Rollcage.exe /DISPATCHER`
  with that folder as the working directory.
- **DLL:** the census `GLIDE3X.DLL` from `scripts/build-glide3.ps1`, copied
  into `Glide\`. Process 1 ran build `6561d08-dirty`, whose
  `grQueryResolutions` was a stub. Processes 2 and 3 ran `0320d07-dirty`,
  which adds the written `grQueryResolutions` committed with this record.
  The DLL draws nothing.

| File | What |
|---|---|
| `V9XGLD3.LOG` | The census log from the first Rollcage attach on. It holds three processes, split at each `attach` line. 1: start-up with the stub; the settings dialog had no resolution, and Cancel closed it. 2: Play pressed; about 90 s of drawing with no input, then the game shut Glide down and exited. 3: the process that attached 12 ms later; one Enter key, then `WCLOSE.EXE ROLLCAGE.EXE`. |
| `rollcage-glide-imports.txt` | The 42 Glide imports of `Glide\ROLLCAGE.EXE`, from its import table (pefile). |
| `psygnosis-hklm.reg` | `REGEDIT /e` of `HKLM\Software\Psygnosis` after setup. |
| `analyze_rc.py`, `analysis.txt` | The analysis per process and its output (`python analyze_rc.py > analysis.txt`). |

The launcher's renderer choice (`DataPC` byte 6) was read from a
disassembly of `DISK1\ROLLCAGE\ROLLCAGE.EXE`, not from a run.
