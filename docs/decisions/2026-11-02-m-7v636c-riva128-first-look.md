# M-7V636C: a RIVA 128ZX runs the vbe package at tier-0

Date: 2026-11-02

Machine: M-7V636C (computer SPACETUG), reached through the remote-help
relay; agent 0.13.1. Plan: `docs/plans/nvidia-riva128.md`, Phase 0.

## The machine before the install

- Pentium II 333 on an Intel 440LX (`8086:7180`), Windows 98 first
  edition (4.10.2222 A). Not a validated configuration.
- Display: NVIDIA's own driver, "NVidia RIVA 128/128ZX", dated 8-12-2023
  in Device Manager. The only display adapter present.
- Present this boot (`HKEY_DYN_DATA\Config Manager\Enum`, exported with
  `REGEDIT /E`): RIVA 128 `12D2:0018` subsystem `10B4:222A` revision 22,
  Voodoo Graphics `121A:0001`, Chromatic Mpact `110B:0002`, DEC 21140 NIC.
- `HKLM\Enum\PCI` also remembers cards no longer fitted: two other RIVA 128
  boards (`10B4:1B1E` and `2A15:54A3`, both revision 10), a Rage Pro
  `1002:4750` installed at some point with a Velocity9x INF, a
  `1002:5446`, two Trio64V+ and a Mystique. No `C:\V9XDIAG` existed.

## Survey

`V9XSURV /rom` (build `d9d7aeb9`), run detached in a Windows DOS box
through the agent, with Michael's go-ahead for this one run. The guest
stayed up (same boot, desktop ready). The report stays outside the
repository (`build\driver-results\V9XSURV-M-7V636C.INI`) because its EDID
block carries the monitor's serial number.

- STB Velocity 128 ZXV, VBE OEM string "STB Velocity 128 (RIVA ZXV)",
  product revision 210-0276-003, OEM software revision `011E`.
- AGP, bus 1. BAR0 `1F000000`, BAR1 `21000008` (prefetchable). Interrupt
  line 9. Capabilities pointer 44h (AGP).
- Video BIOS 32 KiB at C000, checksum good, PCIR names `12D2:0018`.
- VBE 3.0, `TotalMemory` 128 x 64 KiB = 8 MiB, protected-mode interface
  present, DPMS present.
- 46 modes, all described, all attributes `0F9F` or `0F1F` (the three
  4 bpp planar modes) or `0F8F` (one text mode). Every 16 bpp mode is 5:6:5.
  32 bpp goes to 1600x1200; 1920x1080 and 1920x1200 are 8 and 16 bpp.
- Every mode's PhysBasePtr read `FFFF0000` in the DOS box. The driver at
  boot, from the same BIOS, got `21000000`, so this is the DOS box, not
  the BIOS.
- EDID 1.3 over DDC: a Dell 1907FP, preferred 1280x1024.

## Install

- **Package.** The signed 0.34.0 release, `velocity9x-0.34.0-vbe.zip`,
  unmodified, pushed to `C:\V9XPKG`.
- **Route.** Device Manager, the RIVA's Driver tab, Update Driver,
  "Display a list", Have Disk, `C:\V9XPKG`. One model offered:
  "Velocity9x VBE-generic display". Windows warned that its current driver
  may be a closer match; accepted.
- **Restart.** Windows' restart prompt declined. No `WININIT.INI` was
  pending. Then `reset`: boot 9 to 10.
- Windows stopped at its network logon prompt ("Enter Network
  Password"). Escape continued without a network logon; no password was
  entered. Expect this on every boot.

## Boot 10

Files in `docs/probe/m7v636c-riva128-vbe-2026-11-02/`.

- `Stage=enable-ok`, 800x600x16, pitch 1600, 5:6:5 (`is555=0`).
- LFB `21000000` from the VBE (`Aperture=m=none bar=0 pci=1
  b=21000000`), `DpmiMap=01000000:ok`.
- `V9XHW.INI`: "Generic VESA adapter (no chip-specific support)", PCI
  `12D2:0018`, acceleration none, Direct3D not advertised, VRAM 8 MiB.
- Modes: 46 listed, 46 queried, 42 cached and published; 35 rows dynamic
  beyond the 7 baseline rows. EDID recommends 1280x1024; the driver stays
  at the configured mode.
- MTRRs: 8 variable pairs, 4 in use by the BIOS. Nothing write-combines
  the frame buffer (tier-0 `vbe` does not).
- V9XGDI: PASS (display writes, BitBlt and readback coherent). Desktop
  screenshot correct.

## Not done

- V9XMSW mode cycle.
- The owner has not been asked how the picture looks on the monitor.
