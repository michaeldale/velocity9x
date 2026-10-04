# A8U4I5: SiS 6326 page flips, 2026-10-05

- Machine: A8U4I5, SiS 6326 card 2 (rev 0Bh), boot 230, desktop
  800x600x16. V9XDDP switched to exclusive 640x480x16.
- Build: working tree on `c162be2`, with the SiS display start in
  `eng_sis6326.c` and V9X_DD_ENGINE_CAP_FLIP claimed by `sis6326_hw16.c`.
- Record: [2026-10-05 SiS 6326 page flips](../../decisions/2026-10-05-sis6326-page-flips.md).

| File | What it is |
|---|---|
| `B230-V9XDD.INI`, `B230-V9XDDT.TXT` | V9XDDP's report and step log: `Flip20Ms=0x148` (328 ms), `FlipMaxMs=0x11`, `FlipPixelOk=0` |
| `B230-V9XSNAP.INI` | V9XTRACE after the run, with the current `V9XTRACE.EXE`: `FlipHandled=23`, `FlipDeclined=0`, `EngineIdleTimeouts=0` |

Before this build (boot 228's snapshot was stale; see the record),
V9XDDP read `Flip20Ms=0` on every SiS run: boots 208, 209 and 228.
