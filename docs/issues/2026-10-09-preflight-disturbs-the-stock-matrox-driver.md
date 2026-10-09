# V9XSTAGE under MGAPDX64 leaves Matrox's driver without its DirectDraw HAL

Date: 2026-10-09. A8U4I5, MGA-2064W, boot 354, Microsoft's inbox
`MGAPDX64.DRV` active. Status: open; workaround is a warm restart.

## What happened

1. `V9XDDP` under MGAPDX64: `Result=COMPLETE`, `GblNoHardware=0`,
   `GblHalVidMemTotal=0x00715A00`, `GblNumModes=0x63` (99).
2. `V9XSTAGE.EXE` from the matrox package, the preflight the A8U4I5 notes
   ask for before any install. It loads `V9XDISP.DRV` inactively through
   `V9X16LD /quiet`; the agent's 60 s exec timeout ended it, and the DRV
   wrote `Stage=fail-validate-no-identify-hook`.
3. Agent screenshots then returned noise, different on each capture, with a
   vertically repeating structure
   (`docs/probe/a8u4i5-mga2064w-native-2026-10-09/screenshot-after-preflight.png`).
4. `V9XDDP` again: `GblNoHardware=1`, `GblHalVidMemTotal=0`,
   `GblNumModes=0x1A` (26) (`V9XDD-after-preflight.INI`, a newer probe
   build than step 1's).
5. A warm restart (boot 355) restored a clean screenshot.

## Not established

- Which part disturbs it: the Win16 load of a second display DRV into GDI's
  task, its DriverInit, `V9XPROBE.VXD`, or the exec timeout killing
  `V9XSTAGE` while its message box was up.
- Whether the monitor showed damage or only GDI readback did; nobody was
  looking.
- Whether other vendors' stock drivers react the same way. The preflight
  has passed under `vga.drv` and Velocity9x's own drivers before.

## Consequence

Run `V9XSTAGE` only where the active driver is `vga.drv` or Velocity9x, or
follow it with a warm restart before measuring anything under the stock
driver.
