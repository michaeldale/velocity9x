# M-6B21TJ game reports, 2026-11-11

GM45 HP t5740 (W9QI3DAB), boot 17, driver/HAL `0.45.0 03782be9`.
Record: [`../../issues/2026-11-11-gen4-app-alpha-test-depth-write-draws-dropped.md`](../../issues/2026-11-11-gen4-app-alpha-test-depth-write-draws-dropped.md).

| File | What |
|---|---|
| `hl-A.INI`, `hl-B.INI` | V9XTRACE before/after Half-Life Uplink `+map hldemo1` (D3D), census on |
| `inc-A.INI`, `inc-B.INI` | before/after Incoming's main menu |
| `inc-C.INI`, `inc-D.INI` | after Enter on Start Game (no new draws; game faulted) |
| `inc-grab0..3.png` | flip grabs of Incoming's menu |
| `inc-start-game-fault.png` | agent screenshot of the c0000005 dialog |
| `INCOMING.CFG` | the game's `SAVE\INCOMING.CFG` |
| `V9XHW.INI`, `V9XBOOT.INI`, `V9XRPT.INI`, `V9XSNAP.INI`, `V9XSNAL.TXT`, `V9XGL.LOG` | as found in `C:\V9XDIAG` before the runs; `V9XSNAP.INI` and `V9XGL.LOG` are the stale 0.35.0 (`a4221eee`) ones |
