# Glide Phase 2: V9XGLIDP on Gen3 and the software engine, 2026-10-08

- Machines: MICHAEL-NETBOOK (GMA 950, Gen3 engine), Velocity9x 0.13.0
  set, boot 127, agent 10.0.1.248; `Win98SE-Fast-D3D` (86Box, Celeron 533,
  `vbe` package, `Direct3D=2`, software engine), boot 602, port 9878.
- Record: [Glide Phase 2](../../decisions/2026-10-08-glide-phase2-flip-chain.md).
- Plan: [glide-2x-wrapper.md](../../plans/glide-2x-wrapper.md), Phase 2.

| File | Boot | What it is |
|---|---|---|
| `NB-B127-V9XGLIDP-RUN1.INI`, `NB-B127-V9XGLIDE-RUN1.LOG` | 127 | Netbook, first run: 13 of 14; the line drew nothing |
| `NB-B127-V9XGLIDP-RUN2.INI`, `NB-B127-V9XGLIDE-RUN2.LOG` | 127 | Netbook, the line moved to y..y+1: 14 of 14 |
| `NB-B127-DESKTOP-AFTER.png` | 127 | The 1024x576 desktop after `grSstWinClose` |
| `SOFT-B602-V9XGLIDP.INI`, `SOFT-B602-V9XGLIDE.LOG` | 602 | Software engine, the same DLL and probe: 14 of 14 |
| `SOFT-B602-DESKTOP-AFTER.png` | 602 | The 1024x768 desktop after `grSstWinClose` |

Every pixel read back is the same on both engines. The DLL and probe are
build 21d6e8b-dirty: the tree that became the Phase 2 commit.
