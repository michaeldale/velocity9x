# Ivy Bridge on W9QI4E14: the survey's machine, read-only audit from Windows

Date: 2026-10-21
Status: **measured once each, nothing written to the device.** This was
an audit only: no Velocity9x driver installed, no MMIO read, no reboot.
It is the Ivy Bridge test machine for `docs\plans\intel-ivybridge-gen7.md`.

Captures: `docs\probe\w9qi4e14-ivb-hd4000-audit-2026-10-21\`.

| | |
|---|---|
| Machine | Daniel Tunem Arnestad's W9QI4E14, remote help `M-C71087` (agent 0.12.0) |
| Board | MSI MS-7798 (B75MA-P45); host bridge SUBSYS `1462:7798`; AMI BIOS 04.06.05, 07/30/12 |
| CPU | Core i5-3570K, 3.4 GHz, CPUID family 6 model 58 stepping 9 |
| Memory | 4 GiB installed (TOM, below); Windows reports 2,147,479,552 bytes |
| IGD | `8086:0162` rev `09` (HD Graphics 4000, GT2), SUBSYS `1462:2111` |
| Host bridge | `8086:0150` rev `09` |
| VBIOS | "Intel(R) Sandybridge/Ivybridge PCI Accelerated SVGA BIOS", build 2137; VBT `$VBT SNB/IVB-DESKTOP`, VBT version 100, BDB version 166 |
| Monitor | AOC 2769 (EDID `05E3:2769`, 1920x1080 preferred) |
| OS | Windows 98 SE 4.10.2222 A, DirectX 4.08.01.0901; an image built with 98QuickInstall |
| Disk | Toshiba MK2555GSX through RLoew's AHCI driver; FAT32 C: 16 GB |
| Network | RTL8168 through Oerg866's NDIS2 wrapper |
| Display driver | "Standard PCI Graphics Adapter (VGA)", `vga.drv`, 640x480x4 |

## Same machine as the 2026-10-19 survey

`docs\probe\references\msi-b75ma-p45-ivb-hd4000-vgasurv-2026-10-19-16503bf2.ini`
came from this computer:

- the IGD SUBSYS, the monitor's EDID and the board all match;
- the 256-byte IGD config space read from Windows equals the DOS one, except
  at offset FCh (ASLS);
- the owner's `C:\VIDEOROM.BIN` is 58,880 bytes, the survey's `SizeBytes`.

ASLS, the OpRegion pointer, was `D9414018` in DOS on 2026-10-19 and is
`D9415018` in Windows today. Nothing here says why: a different boot, or
a BIOS that places the OpRegion per boot. Read it rather than assume it.

## Config space (V9XHSW.EXE, `V9XHSW.INI`)

This is the Haswell tool, build `3878a589`. Its only write is the CF8h
address select. **The Haswell decode accepts the Ivy Bridge words without
a refusal** (`Status=0000 Refused=0000`). That is what the plan's GGC and
MSAC comparison predicted:

| Field | Value | Meaning |
|---|---|---|
| GGC (host 50h, IGD 50h mirror) | `0211` / `0211` | GMS 2 = 64 MiB stolen, GGMS 2 = 2 MiB GTT, IVD 0, locked |
| BDSM (IGD 5Ch, host B0h) | `DBA00001` | stolen data at `DBA00000` |
| BGSM (host B4h) | `DB800001` | GTT stolen at `DB800000` = BDSM - 2 MiB, as the decode requires |
| TOLUD (host BCh) | `DFA00001` | 3.49 GiB below 4 GiB |
| TOM (host A0h) | `0000_0001_0000_0001` | 4 GiB of DRAM |
| TOUUD (host A8h) | `0000_0001_1E60_0001` | remapped top `11E600000` |
| BAR0 GTTMMADR | `F7800004` / `00000000` | 64-bit MMIO at `F7800000`; GTT at `F7A00000`, 512 Ki entries |
| BAR2 GMADR | `E000000C` / `00000000` | 64-bit prefetchable aperture at `E0000000` |
| MSAC (IGD 62h) | `02` | 256 MiB aperture |
| BAR4 | `0000F001` | I/O at F000h |

The register offsets are the ones datasheet 326765 lists for the host
bridge (90h to BFh).

The BAR0 address is the same as the i5-4570's. The stolen size is twice
the 4570's: 64 MiB here, 32 MiB there. The B1 reserve the Haswell path
places at aperture `1800000h` lies inside stolen memory on both machines.

Windows' 2,147,479,552 bytes is 2 GiB less 4 KiB with 4 GiB fitted. That
looks like a ceiling in how the agent's figure is obtained, not a memory
limit. `SYSTEM.INI` sets neither `MaxPhysPage` nor a vcache limit, so
whatever lets Windows 98 boot with 4 GiB here was not found in this audit.

## Display state

- The IGD's devnode `PCI\VEN_8086&DEV_0162&SUBSYS_21111462&REV_09` is bound
  to `Display\0001`: `MSDISP.INF [VGA]`, matched on `PCI\CC_0300`,
  `vga.drv` at mode `4,640,480`. `Display\0000` is an identical second
  entry with no devnode (`DISPCLS.REG`, `IGDENUM.REG`).
- `INF\OTHER` holds no display INF. No Velocity9x package has been
  installed on this machine; `C:\extras\Velocity9x` holds only the agent
  zip.
- `CONFIG.SYS` and `AUTOEXEC.BAT` load nothing but `setver` and
  `net start`; `MSDOS.SYS` boots straight to the GUI. There is no
  `WININIT.INI`, so nothing is pending.

## Outputs, from the VBT (`VIDEOROM.BIN`)

The video BIOS image the owner saved checksums to zero. It differs from
the survey's 1 KiB DOS dump in one byte, offset 17h (`46h` here, `4Ah` in
DOS). How the image was taken is not known.

The BDB's general-definitions block (block 2, 8 children of 33 bytes)
describes one external output beside the CRT:

- CRT DDC on GMBUS pin 2;
- child handle `0004`, device type `68D2` = `DEVICE_TYPE_HDMI` with
  `NOT_HDMI_OUTPUT` set (a DVI-D), on DVO port 1 (port B), DDC pin 5.

Decoded against i915's `intel_vbt_defs.h`. Which of the two connectors
the AOC monitor is on is not recorded: that needs the display registers,
which this audit did not read.

## Left on the guest

`C:\V9XDIAG\` holds `V9XHSW.EXE`, `V9XHSW.INI`, `DISPCLS.REG` and
`IGDENUM.REG`. The agent cannot delete files, so they stay. Nothing else
was changed.

## Not measured

- Tier-0 `vbe` on this machine. It is the plan's next step.
- Any MMIO: forcewake (which IVB variant this part uses, `0xA188` with ack
  `0x130040` or legacy `0xA18C`), RC6 state, the display pipe, the plane
  registers, the GTT head.
- Whether Windows' restart path works on this machine.
