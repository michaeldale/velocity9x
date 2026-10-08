# Netbook: Need for Speed II SE on GLIDE2X.DLL, 2026-10-08/09

- Machine: MICHAEL-NETBOOK (GMA 950, Gen3), Velocity9x 0.13.0 set,
  `MaxPhysPage=40000`, boots 127-129, agent 10.0.1.248.
- Record: [NFS II SE races on Glide](../../decisions/2026-10-09-nfs2se-races-on-glide-gen3.md).
- Plan: [glide-2x-wrapper.md](../../plans/glide-2x-wrapper.md), Phase 4.

| File | Boot | What it is |
|---|---|---|
| `B127-MENU-512-RECORDS.png` | 127 | Main menu, Phase 3 DLL: left panel black (textures evicted from a 512-record table) |
| `B128-MENU.png` | 128 | Main menu with 4,096 records: complete |
| `B128-RACE-REFUSALS.png`, `B128-V9XGLIDE-REFUSALS.LOG` | 128 | First race: drawn, mirror black; 35,670 refused, 87,091 fog-dropped |
| `B129-V9XGLIDE-REFUSAL-STATE.LOG` | 129 | The refusals' state: ARGB1555 surface, MODULATE, blend, alpha test, scissor 537..634 |
| `B129-RACE.png`, `B129-V9XGLIDE.LOG` | 129 | Clip window applied to geometry: mirror and map drawn, nothing refused or fog-dropped |
| `B129-RACE-LATER.png` | 129 | Ten seconds later: a distant sky view without the HUD, not explained |
| `B129-DESKTOP-AFTER-WMCLOSE.png` | 129 | The desktop after `WCLOSE.EXE NFS2SEA.EXE` |
| `wclose.c` | - | Posts WM_CLOSE to a program's windows; how the game is ended without a reboot |

The game was driven by keyboard through the agent: Escape past the intro,
Down four times with 1.5 s between (faster taps are dropped at 5 frames
a second), Enter.
