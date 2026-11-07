# A first install on two Eee PCs stayed at 16 colours

Date: 2026-11-07
Status: open. A direct install works on our GMA 950 (measured below).
The reporter's files are owed.

Source: a VOGONS post (2026-11-07,
<https://www.vogons.org/viewtopic.php?p=1455551#p1455551>). The poster
used the `intel-gma` package on an Eee PC 901 (945GSE, GMA 950
`8086:27AE`) and an Eee PC 701 (910GML, GMA 900 `8086:2592`). On both
machines, installing it directly "would remain stuck at 16 colors".
Installing the `vbe` package first and then switching to `intel-gma`
worked. No version and no diagnostic files were given.

## What 16 colours means

A good first boot is 640x480 at 256 colours (`docs/INSTALL.md`, step 7).
16 colours is the INF's `MODES\4\640,480` row, which hands the display to
Windows' `vga.drv`. Windows uses that row when the stored depth
(`HKCC\Display\Settings` `BitsPerPixel`) is 4, which it is after our
driver fails a boot. So the report means the driver did not start on
the first boot after a direct install.

## Hypotheses

1. **Installed on the IGD's second PCI function.** Not confirmed.
   Measured 2026-09-12 on the netbook (the same 945GSE as the Eee 901,
   `docs/decisions/2026-09-12-intel-phase1-physical-capture.md`):
   - the IGD's function 1 (`8086:27A6`, class 0380) shows as an
     "(Unknown Device)";
   - a Have Disk install forced onto it gave `Stage=libmain`,
     `VbeDetail=minivdd-no-api` and the 4-bpp `vga.drv` desktop.

   A fresh Windows 98 offers to find a driver for that device, and
   pointing it at the package is a natural mistake. The VBE-first route
   goes through Display adapters, which lands on function 0, so the
   later switch is on the right device too. This fits every part of the
   report. The 910GML's function 1 is `8086:2792`.
2. **Windows kept 4 bpp after a plain driver change.** This was seen
   once, on 86Box after a change from Windows' own S3 driver
   (`docs/decisions/2026-11-06-trio64vplus-have-disk.md`). But installs
   over `vga.drv` on a real Trio3D and on the 86Box BX-Voodoo3 came up
   at 8 bpp (`2026-09-02-vbe-tier0-on-a-real-trio3d.md`,
   `2026-11-07-vbe-write-combining.md`). It also does not explain why
   the VBE-first route works.
3. **A first-boot failure particular to `intel-gma` on these machines.**
   Nothing points to one yet. `V9XBOOT.INI` from a failed first boot
   would show it.

## Measured on the netbook (2026-11-07, boots 280-282)

The netbook (MICHAEL-NETBOOK, W9QI2BE2, 945GSE, the Eee PC 901's
chipset) was put back on Windows' driver and given the released 0.39.0
`intel-gma` package the way a new user would install it. Evidence:
[`../probe/netbook-direct-install-from-vga-2026-11-07/`](../probe/netbook-direct-install-from-vga-2026-11-07/).

- **Before (boot 280).** Function 0 was bound to Velocity9x
  (`Display\0001`, main `5d95df34`), at 1024x576x16. Function 1
  (`8086:27A6`) was bound to Microsoft's `MSDISP.INF [PCIVID]`
  "Standard PCI Graphics Adapter" through `PCI\CC_0380` (`Display\0002`).
  So on this install, function 1 is no longer an "(Unknown Device)": it
  sits under Display adapters.
- **To VGA.** Device Manager, Update Driver, Show all hardware,
  "Standard PCI Graphics Adapter (VGA)". Windows set
  `HKCC\Display\Settings` to `BitsPerPixel=4`, `640,480` straight away,
  before any restart. The VGA driver's key kept our `V9xFamily` and
  `V9xSyncGeneration` values. Boot 281 came up on `vga.drv` at
  640x480x4.
- **What a new user sees.** Display adapters lists two entries,
  "Standard PCI Graphics Adapter" (function 1) **first**, then
  "Standard PCI Graphics Adapter (VGA)" (function 0)
  (`boot281-devmgr-two-standard-adapters.png`).
- **Direct install on function 0.** Update Driver, Have Disk
  `C:\V9X039I`, which offered "Velocity9x Intel GMA 950 (945GSE)" alone.
  Before the restart, `BitsPerPixel` was already `8` and
  `DEFAULT,Mode` `8,640,480`. No `WININIT.INI` was queued. After the
  agent's `reset`, boot 282 wrote `Stage=enable-ok`, `VbeDetail=ok`,
  640x480x8, and the desktop drew.
- `V9XMSW /set:1024x576x16` then passed, and the netbook was left on
  0.39.0 at 1024x576x16.

So on the GMA 950 a direct install on the right device works, and the
class installer moves the stored depth off 4 bpp. Hypothesis 2 does not
hold there, and hypothesis 3 does not reproduce on this machine. The
Eee PC 901's BIOS and the 701's GMA 900 were not tested. Hypothesis 1
gains weight: the two entries' names differ only by "(VGA)", and the
wrong one is listed first.

Unrelated, seen on boot 280 as Device Manager opened: MPREXE faulted
in USER.EXE at 0004:1E66 (`boot280-mprexe-gpf-unrelated.png`).

## Done

The two-function warning, which only the internal runbook carried, is
now in the user-facing install steps: `packaging/win98se/INSTALL.TXT`
step 3, and `docs/INSTALL.md`'s install-time gotchas. Both now name
the two Display adapters entries as well as the "(Unknown Device)".
There is no driver or INF change.

## Owed

From the reporter, on a fresh direct install:
- Device Manager's entries for the graphics chip, and which one
  Velocity9x was installed on;
- `C:\V9XDIAG\V9XBOOT.INI` from the 16-colour boot.

If hypothesis 1 holds, consider having the INF claim function 1 with an
install that loads no driver, so the New Hardware wizard settles it
harmlessly. That needs measuring on the netbook first: a second Display
class devnode could change how Windows 98 handles multiple monitors.
