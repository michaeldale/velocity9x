# The MGA-2164W on the matrox family: first boot, engine PASS at 8/16/32 bpp

Date: 2026-10-10. A8U4I5 (10.0.1.172), boots 379-382. A physical Matrox
Millennium II, `PCI\VEN_102B&DEV_051B&SUBSYS_1200102B&REV_00`, fitted in
place of the Rage XL. Package: `matrox` built from `afb538c` (the tree was
dirty only by uncommitted files under `docs/issues/`). Evidence in
`docs/probe/a8u4i5-mga2164w-first-boot-2026-10-10/`.

The 2164W joined the `matrox` family in `caf78d6` and had not run on this
path. Its August record found corrupt surfaces with the Velocity9x mini-VDD
of the time ([boundary](../specifications/matrox-millennium2-bringup.md));
this family installs `V9XMINI.VXD`, so that was the first check.

## Before the install

- Windows 98 SE has no inbox driver for `051B`: it bound "Standard PCI
  Graphics Adapter (VGA)" at `Display\0012` (`enum-pci-before.reg`). No
  native Matrox baseline was taken.
- The preflight failed. `V9XSTAGE` reported that V9XDISP.DRV "did not load
  and unload cleanly"; `V9X16LD` run alone showed the step: "A supported
  mode was rejected" - `ValidateMode` refused one of 640x480, 800x600 or
  1024x768 at 8 or 16 bpp (`preflight-v9x16ld.png`). The same message
  appeared on this machine after the Rage XL swap of boot 377, where the
  stale Matrox module was the suspect. Here no Velocity9x driver was
  loaded (vga.drv at 4 bpp), so that explanation does not fit this case.
  Cause not established. The install proceeded anyway and the driver
  validates all three resolutions at both depths once it is the display
  driver (below). The preflight is wrong in this state, or `ValidateMode`
  depends on state that exists only after an install. Not investigated.

## Install

Update Driver > Have Disk > `C:\V9XM2`. The INF offered one model,
"Velocity9x Matrox Millennium II MGA-2164W". Setup copied the files and
left a WININIT.INI with two plain renames (GLIDE2X, GLIDE3X), no `NUL=`
lines (`WININIT-after-install.INI`). Warm restart. Installed DRV, VXD,
HAL, SETP, GL, GLIDE2X and GLIDE3X sizes match the package.

## First boot (380)

    Stage=enable-ok
    Surface=pitch=640 bpp=8 dwb=640 dds=640 w=640 h=480 debpp=8
    VbeController=v=0200 mem=64 caps=00000001 rev=0101   (4 MiB)
    Aperture=m=1 bar=0 pci=1 b=dc000000
    MgaCrtcExt3=80

- **The framebuffer is BAR0**, the reverse of the 2064W, as the chip
  module's per-chip BAR order expects. The engine hook mapped the other
  BAR and the engine ran (below). The control BAR's address is not in
  these logs.
- **4 MiB** by VBE 4F00h (`mem=64`), `VbeVramBytes=4194304`.
- **CRTCEXT3 is 80h at 640x480x8**: mgamode set by the BIOS, scale bits 0.
  The 2064W read 81h at 800x600x16; the two were measured in different
  modes, so this is not a difference between the chips.
- The BIOS lists 24 modes; 19 are cached and the table publishes 14
  (`b380-V9XMODES.INI`). Like the 2064W's, this BIOS pads some rows:
  800x600x8 at 1024 bytes, 1600x1200x8 at 1664, 800x600x16 at 1920.

## Checks

| Boot | Mode | Test | Result |
| --- | --- | --- | --- |
| 380 | 640x480x8 | `V9XGDI /auto` | PASS |
| 380 | 640x480x8 | `V9XGDI /accel` | PASS: 233 fills, 132 copies, 84 text ops on the engine, compared PASS, no FIFO or sync timeouts |
| 380 | 800x600x16 | `V9XGDI /accel`, same session | FAIL, `Error=poisoned-before-run` |
| 381 | 800x600x16 | `V9XDDP` | COMPLETE: `GblNoHardware=0`, 2.9 MiB off-screen, fill and four overlap copies pixel-correct, flip caps advertised, `FlipPixelOk=0` |
| 381 | 800x600x16 | `V9XGDI /accel` | PASS: 219 fills, 132 copies, 84 text, 48 clipped |
| 381 | 14 modes | `V9XMSW /set` | PASS for all, each DIB stride equal to the mode's (`modes-v9xmsw.txt`) |
| 382 | 800x600x32 | `V9XGDI /accel` | PASS: 200 fills, 132 copies, 84 text, 48 clipped |

The boot-380 16 bpp FAIL is the harness working as designed: `/accel`
deliberately injects an engine timeout, the poison latches for the session
and survives a mode switch (`gdi_accel.c`), so the second run started with
the engine disabled. The 2064W record runs each depth on its own boot for
the same reason. Run at 16 bpp on a fresh boot, it passed.

## What this does and does not establish

- **No surface corruption in any readback.** DirectDraw fills and copies
  and the GDI engine-versus-CPU comparisons all match at 8, 16 and 32 bpp
  with `V9XMINI.VXD` in place. The August corruption did not reproduce on
  this path.
- **The picture on the monitor was not observed.** Every check reads back
  through the driver's own stride, and screenshots do the same. A padded
  row with the wrong scanout pitch would pass all of it. Owed: a look at
  the monitor at 800x600x8, 800x600x16 and 1600x1200x8, the padded modes.
- Page flips are advertised, but this run does not show them working:
  `FlipPixelOk=0` is what real flips read in this probe.
- Direct3D stays on the software path; the chip has no 3D engine.
