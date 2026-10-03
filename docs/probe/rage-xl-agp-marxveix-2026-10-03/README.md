# ATI 3D Rage XL AGP, a user's report (MarxVeix)

Received 2026-10-03. The guest's clock read January 2005, so file dates
inside are that machine's. Card `1002:474D`, 8 MB, Velocity9x 0.10.0
(`cfa3654`). The reporter tested Test Drive 5 (Direct3D) and Quake 1
and 2 (OpenGL): no crashes, and Quake 2 looked right. DxDiag's
full-screen DirectDraw and D3D tests, and DX7's full-screen test,
flicker.

| File | What it is |
|---|---|
| `test-drive-5-d3d.png`, `quake1-opengl.png`, `dxdiag-display.png` | the reporter's BMPs, converted |
| `V9XHW.INI`, `V9XBOOT.INI`, `V9XMODES.INI`, `V9XDDH.INI`, `V9XSYNC.INI` | 0.10.0, written at boot |
| `V9XSNA1-4.INI` | V9XTRACE `cfa3654`, taken 60-122 s after boot, before any game: every draw counter zero |
| `V9XGL.LOG` | the ICD's log across several sessions, no timestamps |
| `V9XSNAP.INI`, `V9XSURV.INI`, `V9XMSW.INI`, `V9XPAL.INI`, `V9XDD.INI` | tools of build `7e55fa9`, 2005-01-06/07, run in 16-colour VGA before the install: expected failures |

Issues filed from it, all dated 2026-10-03 in `docs/issues/`:
- `mach64-class-flips-declined-full-screen-flickers`;
- `opengl-software-interface-out-of-video-memory`;
- `mach64-refuses-full-screen-quad-on-bottom-edge`;
- `several-velocity9x-display-instances-block-mode-sync`;
- `dxdiag-shows-no-driver-version`.
