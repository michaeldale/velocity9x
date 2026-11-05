# Tier-0 `vbe` on the Quadro FX 550 (NV43): enable-ok, GDI passes at 8 and 32 bpp

Date: 2026-11-05

Machine: M-J0F0SJ (W9QI2F4F), reached through the remote-help relay,
agent 0.13.1. The card and the machine before the install are in
`2026-11-05-w9qi2f4f-quadro-fx550-audit.md`. Files in
`docs/probe/w9qi2f4f-quadro-fx550-vbe-2026-11-05/`.

## Install

- **Package.** The signed 0.35.0 release, `velocity9x-0.35.0-vbe.zip`,
  unmodified (`Build=a4221eee`), pushed to `C:\V9XVBE`.
- **Route.** `CONTROL.EXE sysdm.cpl,,1`, then the Device Manager tree
  (clicked into first), Display adapters, the `(VGA)` entry on
  `PCI\VEN_10DE&DEV_014D`, Driver tab, Update Driver, "Display a list",
  Have Disk, `C:\V9XVBE`. One model offered: "Velocity9x VBE-generic
  display (any VESA VBE 2.0+ adapter)". No closer-match warning came up.
  Every step was keyed and checked with `V9XWND.EXE`'s window list,
  because the 4 bpp screenshots are noise.
- **WININIT.** Two plain Glide renames, no `NUL=` line
  (`WININIT-after-install.INI`).
- **Restart.** Windows' restart prompt declined; agent `reset`, job
  `nv43-vbe-boot1`: `BootCounter` 82 to 83, desktop ready.

## Boot 83

- `Stage=enable-ok` at 640x480x8, pitch 640. The agent's inventory reports
  "Velocity9x VBE-generic display" on `PCI\VEN_10DE&DEV_014D`.
- LFB `D8000000` from the VBE, BAR1 of the card (`Aperture=m=none bar=0
  pci=1 b=d8000000`); `DpmiMap=01000000:ok`, the 16 MiB tier-0 map.
- VBE 3.0, OEM software revision `0543` (the BIOS's 5.43), `TotalMemory`
  2048 x 64 KiB = 128 MiB; `V9XMODES.INI` `Vram=reported=134217728
  usable=16777216`.
- `V9XHW.INI`: "Generic VESA adapter (no chip-specific support)", PCI
  `10DE:014D`, acceleration none, Direct3D software.
- Modes: 32 listed, 32 queried, 29 cached and published. Every 16 bpp
  mode is 5:6:5. 8, 16 and 32 bpp to 1280x1024; 1600x1200 and 1400x1050 at
  8 and 16 bpp only; 2048x1536 at 32 bpp only. No 1920 or widescreen mode.
- **No EDID**: `VbeCache f=0407`, `Edid=none`, `Recommendation=none
  reason=no-edid`. The IGD's VBE also failed 4F15h on this machine
  (`2026-10-22-ironlake-edid-gmbus.md`), so the monitor or its cable
  stays the likely cause. Not checked with the owner.
- MTRRs: the BIOS's ranges are UC over `D8000000` (128 MiB) and
  `E0000000` (512 MiB); tier-0 `vbe` writes none (`s=0`). The
  `MtrrWrite=ok ... b=d0000001` key in `V9XBOOT.INI` is left over from the
  last Ironlake boot: the file is a profile and that key was not
  rewritten.
- V9XGDI `/auto`: PASS at 640x480x8. Desktop screenshot correct
  (`desktop-640x480x8.png`).

## 1024x768x32

- V9XMSW `/set:1024x768x32`: PASS (`ChangeResult=0`).
- `V9XBOOT.INI` after the switch: `Stage=enable-ok`, pitch 4096, 32 bpp,
  same LFB.
- V9XGDI `/auto`: PASS. Screenshot correct (`desktop-1024x768x32.png`).
  The machine is left at 1024x768x32.

## Side effect on the IGD install

The vbe install wrote `V9XDISP.DRV`, `V9XMINI.VXD`, `V9XHAL.DLL` and the
rest into `C:\WINDOWS\SYSTEM` over the Ironlake build's files of the same
names. The IGD devnode (`Display\0002`, `V9x.Install.hd-ironlake`) still
points at them. If the Quadro is removed, the IGD comes back on the vbe
binaries under intel-gma settings; reinstall intel-gma before using it.

## Not done

- A full V9XMSW cycle or depth run; V9XDDP and the 3D probes.
- The owner has not been asked how the picture looks on the monitor.
