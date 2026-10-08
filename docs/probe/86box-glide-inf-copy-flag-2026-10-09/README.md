# 86Box Win98 SE: SetupX and GLIDE2X.DLL's copy flag, 2026-10-09

- Guest: 86Box `Win98SE-Fast-D3D`, agent rel-0.6.0f at 127.0.0.1:9878,
  boot 602. Its SYSTEM already held a 3dfx `GLIDE2X.DLL`, 2.56.00.0459.
- Record: [Glide packaging](../../decisions/2026-10-09-glide-packaging-copy-flag.md).
- Plan: [glide-2x-wrapper.md](../../plans/glide-2x-wrapper.md), Phase 5.

`GTEST.INF` carries only the line the family INFs carry,
`glide2x.dll,,,40`. Each case seeded `C:\WINDOWS\SYSTEM\GLIDE2X.DLL`,
put the 0.14.0 DLL and the INF in `C:\V9XDIAG`, ran
`RUNDLL.EXE setupx.dll,InstallHinfSection DefaultInstall 132
C:\V9XDIAG\GTEST.INF`, and fetched SYSTEM's file and `WININIT.INI`
(`run-case.ps1`).

| Case | SYSTEM before | Result |
|---|---|---|
| A0 | 3dfx 2.56.00.0459 (the guest's own) | Kept; no prompt; exit 0 in 189 ms |
| B | Velocity9x 0.13.0 (this tree, version bumped down) | Replaced at the next boot: `B-WININIT.INI` renames `glide2x.001` over it, and `.001` is the 0.14.0 DLL byte for byte; restart prompt (`B-RESTART-PROMPT.png`), answered No |
| A1 | 3dfx 2.61.00.0658 | Kept; nothing staged; no prompt |
| A2 | 3dfx 1.00.01.0106 | Kept; nothing staged; no prompt |

`WININIT.INI` was reset to `[rename]` after case B, and the guest's 2.56
was put back and verified by hash. `C:\WINDOWS\SYSTEM\GLIDE2X.001`,
`C:\V9XDIAG\GLIDE2X.DLL` and `C:\V9XDIAG\GTEST.INF` remain on the guest;
the agent has no delete verb.
