# Rage XL PCI: page flips through the Mach64 CRTC, watched on the monitor

Date: 2026-10-04. Machine: A8U4I5 with the ATI 3D Rage XL PCI
(`1002:4752`, 8 MB) on a CRT. Evidence:
`docs/probe/a8u4i5-rage-xl-pci-2026-10-03/flip/`. Michael watched the
monitor and recorded it (`2026-10-04 10-56-00.mkv`, 77 s, kept by him;
contact sheets in the folder).

## Before

Only the Rage IIC declared `V9X_DD_ENGINE_CAP_FLIP`. On the Mach64
class every flip was declined and DirectDraw copied the back buffer to
the front unsynced: `FlipDeclined=61145` over a 3DMark 99 run, and
3DMark reported "VSync Off". The reporter's Rage XL AGP flickered in
DxDiag's and DX7's full-screen tests
(`docs/issues/2026-10-03-mach64-class-flips-declined-full-screen-flickers.md`).

## The change

The Rage IIC's flip, unchanged. It writes `CRTC_OFF_PITCH` inside the
vertical blank, read from `CRTC_VLINE` (`m64_scanout.c`, measured with
ATIRX `/crtc`). The Rage XL has the same Mach64 CRTC registers at the
same block-0 offsets of its 4 KiB register window. `m64_scanout.c`
accepts the Mach64 engine type as well as the Rage IIC's, still gated on
the cap. The run below had it on the Rage XL PCI's record alone; the
shared Rage Pro-class hook now declares `FLIP` and `VBLANK` for every
part it binds (see the end). Boot snapshot: `EngineCaps=0x1C`.

## Measured

3DMark 99 Max at 640x480x16, triple buffering, the same settings as
`2026-10-03-rage-xl-pci-benchmarks-a8u4i5.md`:

| | Copy (2026-10-03) | Flip |
|---|---|---|
| FlipHandled / FlipDeclined | 0 / 61,145 | **4,209 / 0** |
| FlipStillDrawing (status polls answered "not yet") | - | 23,838 |
| Refresh rate, as 3DMark reports it | VSync Off | **59 Hz** |
| Engine refusals | 23,776 | 0 |
| 3DMarks / CPU | 1225 / 13700 | 1117 / 13627 (recorded, not compared) |
| Game 1 / Game 2 | 14.0 / 10.9 fps | 12.7 / 10.0 fps |

On the monitor, Michael: "looks good". In the recording, Game 1 and the
title screens show no tearing, black frames or shifted pictures. Game 2's
world draws mostly black, with white squares where sprites should be. He
believes that predates this change. Nothing here records Game 2 before
it, so that is filed on its own
(`docs/issues/2026-10-04-mach64-3dmark-game2-dark-world-white-sprites.md`).
The engine refused nothing in this run, so those draws reach the chip.

## Not established

- DxDiag's and DX7's full-screen tests, which the reporter saw flicker,
  were not run with this build.
- The rest of the class. Since this record, the flip is enabled for the
  whole Rage Pro class through the shared hook (`v9x_mobility_fill_engine`):
  Rage XL/XC, Rage Pro, LT Pro and the Mobility parts, the Gateway's
  included. It is the same CRTC, and a fix measured on one part applies to
  parts of the same design, with an override for any that misbehaves
  (Michael, 2026-10-04). Only `4752` has been watched.
- The flip-completion timing, a single run. The state machine releases
  the old buffer when the blank ends, as on the Rage IIC, where a write
  applied at once. Whether the Rage XL latches at once too was not
  separately measured; the picture showed no torn frame.
