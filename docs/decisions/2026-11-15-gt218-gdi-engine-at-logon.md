# GT218: the engine channel comes up at logon, so GDI copies use it from the desktop on

Date: 2026-11-15

Status: implemented and measured on W9QI0DAD (GeForce 210 `10DE:0A65`,
FX-6300), one boot. Option B of
[nvidia-gt218-gdi-engine.md](../plans/nvidia-gt218-gdi-engine.md), chosen
by Michael over C (a bring-up in the 16-bit driver) on 2026-11-15.

## Why

GDI draws on the NV50 engine only once the HAL's channel exists (option A),
and the HAL builds it on its first engine operation. Until a DirectDraw
program ran in a boot, screen-to-screen copies and scrolls were CPU reads
through BAR1 at about 1,970 cycles a pixel; with the channel up they cost
about 6 at 256x256 and 5 for a scroll
([write-combining record](2026-11-15-gt218-write-combining.md), addendum).

## The change

- `tools\diag\settings_engine.c`: a new rundll32 entry point in
  `V9XSETP.DLL`, `V9xEngineStart`. It asks the display driver for its GDI
  counters (`V9X_GDIGETSTATS`) and returns unless the engine is the NV50,
  GDI's copy primitive is enabled and the session is not poisoned. Then it
  loads `DDRAW.DLL` at run time (the DLL's import audit allows only system
  DLLs, and the property sheet should not load DirectDraw), creates a
  DirectDraw object at the normal level, colour-fills one 64x64 offscreen
  surface in video memory, and releases everything. The fill is an engine
  operation, so the HAL builds the channel with the code already measured
  on the card. The outcome goes to `C:\V9XDIAG\V9XENG.INI`; nothing is
  shown.
- `scripts\lib\inf.ps1`: a family manifest with
  `Inf.StartEngineAtLogon = $true` gets
  `HKLM,...\Run,V9xEngineStart,,"rundll32.exe v9xsetp.dll,V9xEngineStart"`,
  beside the existing `V9xSyncModes` entry. Only the nvidia family sets it;
  its RIVA 128 row is skipped by the entry point's engine check.
- `build-settings-page.ps1` compiles the file and exports and checks the
  new name. `diagpaths.h` gains `V9X_DIAG_ENGINE_INI`.

No 16-bit or HAL code changed.

## On the card

Build `a0b71d84-dirty`, DRV/VXD/HAL/SETP by WININIT rename and `reset`,
all four hash-checked. The Run value was set with the agent's `reg -Set`,
since this was not a fresh INF install. Boot 20:

- `V9XENG.INI`: `Result=filled`, `Hr=0x00000000`, written at logon.
- dispbench `95c470c` DBTIME,DBGDI at 1024x768x16, run `20261115-205649`,
  before any program of ours touched DirectDraw. Cycles per pixel:

| | CPU only (`-192934`) | channel from V9XDDP (`-204208`) | channel from logon (`-205649`) |
|---|---:|---:|---:|
| BitBlt screen to screen 256x256 | 1,969.6 | 6.24 | 6.25 |
| BitBlt screen to screen 64x64 | 1,972.3 | 25.66 | 25.73 |
| BitBlt screen to screen 32x32 | 1,985.0 | 90.73 | 90.96 |
| ScrollDC | 1,969.7 | 4.99 | 4.99 |
| FillRect 64x64 (CPU, WC on) | 5.24 | 4.99 | 4.99 |

- After that: V9XDDP `Result=COMPLETE`, its `*Ok=` keys identical to the
  WC-on run of boot 14 (the HAL reuses the channel it built at logon);
  V9XGDI `/auto` PASS; `/accel` PASS (`InjectOperation=copy`,
  `PoisonedAfterInject=1`, `Compared=PASS`, 0 timeouts).

Gates: run-checks passed.

## Not settled

- One boot. Whether the Run entry ever runs before the display driver's
  first Enable has settled is not a question here (Run entries start after
  Explorer), but a logon that never happens (a machine left at the logon
  box) leaves the channel down until it does.
- After a mode change GDI still depends on the channel surviving it. On
  M-BXMFA8 it did (V9XMSW `/cycle`, `/depth`); the 32 bpp dispbench run on
  W9QI0DAD earlier stayed on the CPU for a reason not established, and
  this record does not change that.
- A fresh install through the INF (rather than the agent's registry write)
  has not been run, nor has V9XUPD applying an update that adds the line.
