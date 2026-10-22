# Ironlake on W9QI2F4F: Windows 98 boots on Gen5, and it is the chinaboard

Date: 2026-10-22
Status: **measured once each, nothing written to the device.** A
read-only audit from Windows: no Velocity9x driver installed, no MMIO
read, no reboot. It answers step 1 of the Order in
`docs\plans\intel-gen4-gen5-bringup.md` for Gen5.

Captures: `docs\probe\w9qi2f4f-ironlake-audit-2026-10-22\`.

| | |
|---|---|
| Machine | "myne"'s W9QI2F4F (network name `moo`), remote help `M-J0F0SJ` (agent 0.12.0, agent `BootCounter` 42) |
| Board | "BULLDOZER ZX-H55M Ver:1.1", AMI BIOS dated 08/17/2018; PCH devices `3Bxx` rev `06` (H55) |
| CPU | Core i3-540, 3.07 GHz, CPUID family 6 model 37 stepping 5 (Clarkdale) |
| Memory | 4 GiB by the survey's E820 map; Windows reports 2,147,479,552 bytes |
| IGD | `8086:0042` rev `18` (Ironlake desktop, Gen5), SUBSYS `8086:0042` |
| Host bridge | `8086:0040` rev `18` |
| VBIOS | "Intel(R)Ironlake Desktop PCI Accelerated SVGA BIOS", build 1931, 11/11/2009; VBT `$VBT IRONLAKE-DESK`, VBT version 100, BDB version 152 |
| OS | Windows 98 SE 4.10.2222 A, DirectX 4.06.03.0518, IE 5.0; an image built with 98QuickInstall |
| Disk | IDE disk on the PCH SATA functions (`3B20`, `3B26`) through SweetLow's ATA driver; FAT32 C: 10.4 GB |
| Network | RTL8136 (`10EC:8136`) through Oerg866's NDIS2 wrapper |
| Display driver | "Standard PCI Graphics Adapter (VGA)", `vga.drv`, 640x480x4 |

**The plan's gate - does Windows 98 SE install and boot on a Clarkdale
board - is passed.** The owner had already installed it.

## Same board as the 2026-09-28 survey

`docs\probe\references\chinaboard-ironlake-vgasurv-2026-09-28-b19d7a18.ini`
(DOS hostname "chinaboard-bulldozer") describes this machine:

- CPUID signature `00020655`, the IGD and host bridge IDs and revisions,
  and the PCI inventory, RTL8136 included, all match;
- the survey's DOS date, 2018-08-17, is this BIOS's date, which reads as
  an RTC that was never set;
- the owner's `C:\VIDEOROM.BIN` is 52,224 bytes, the survey's
  `SizeBytes`;
- the 256-byte IGD config space read from Windows equals the DOS one
  except at 50h, 5Ch and FCh, which are all the stolen-memory size and
  what follows from it (below).

No serial number ties the two together, so "the same unit" rests on the
above, not on an identifier.

## Config space (V9XHSW.EXE, `V9XHSW.INI`)

The Haswell tool, build `3878a589`. Its only write is the CF8h address
select; it reads 00:00.0 and 00:02.0 of any Intel pair. Its `[Decode]`
section is the Haswell layout and **does not apply**: it reports
`Status=0002 Refused=0084` and reads GGC from the wrong offset. The raw
words, decoded with i915's tables (`intel-gtt.c`, `gen3_stolen_size`).
The Ironlake PRM (Intel's 2010 set, in the private research folder
`ironlake-2010-core-hd-graphics`) does not define these registers: Vol 1
Part 2 3.6.1 (p47) defers the stolen-memory layout to "the Device
Cspecs".

| Register | Now | 2026-09-28 (DOS) | Meaning now |
|---|---|---|---|
| GGC (host 52h) / mirror (IGD 52h) | `0380` / `0380` | `0350` (IGD) | GMS 8 = 128 MiB stolen; GGMS 3 = 2 MiB GTT; IVD 0 |
| BSM (IGD 5Ch) | `C8000000` | `BE000000` | stolen data at `C8000000` |
| TOLUD (host B0h, 16 bits) | `D000` | - | `D0000000`, 3.25 GiB |
| BAR0 GTTMMADR | `FBC00004` / `00000000` | same | 64-bit MMIO at `FBC00000` |
| BAR2 GMADR | `D000000C` / `00000000` | same | 64-bit prefetchable aperture at `D0000000` |
| MSAC (IGD 62h) | `02` | same | 256 MiB aperture |
| BAR4 | `0000DC01` | same | I/O at DC00h |
| ASLS (IGD FCh) | `C378E0E4` | `BB78E0E4` | OpRegion present |

Two independent numbers agree in both captures:

- now: BSM `C8000000` + 128 MiB = `D0000000` = TOLUD;
- 2026-09-28: GMS 5 = 32 MiB, BSM `BE000000` + 32 MiB = `C0000000`, where
  the survey's E820 reserved ranges end; and VBE reported 33,488,896
  bytes (511 x 64 KiB) of video memory.

So **the stolen size was raised from 32 MiB to 128 MiB in the BIOS**
between the survey and this install, and the memory map below it moved:
the OpRegion moved by exactly 128 MiB, from the survey's ACPI NVS range at
`BB78E000` to `C378E000`. Who changed the setting, and why, is not known.

The host bridge's A0h-AFh words are `20000080`, `C8000000`, `C7E00000`,
`C3800000`. A4h equals BSM and A8h is BSM less 2 MiB, which is where a
2 MiB GTT below stolen data would sit (Ivy Bridge's BGSM is the same
shape). That is an arithmetic observation, not a decode: these registers
were not checked against any document.

GGMS 3 gives a 2 MiB GTT, 512 Ki entries. Where it is mapped in BAR0 is
not measured. The GM965 keeps it at BAR0 + 512 KiB and a G45 at + 2 MiB
(`2026-10-20-gm965-tommohp-tier0-and-mmio.md`); the Haswell decode's
`GttBase=FBE00000` is that tool's assumption, not a reading.

## Display state

- The IGD's devnode `PCI\VEN_8086&DEV_0042&SUBSYS_00428086&REV_18` is
  bound to `Display\0002`: `MSDISP.INF [VGA]`, matched on `PCI\CC_0300`,
  `vga.drv` at mode `4,640,480`. `Display\0000` and `Display\0001` are
  identical entries with no devnode (`DISPCLS.REG`, `IGDENUM.REG`).
- `INF\OTHER` holds no display INF. No Velocity9x package has been
  installed.
- `CONFIG.SYS` is empty, `AUTOEXEC.BAT` is `net start`, `MSDOS.SYS` boots
  straight to the GUI. There is no `WININIT.INI`, so nothing is pending.
- `BOOTLOG.TXT`: VDD and VFLATD load and initialise; the only
  `DEVICEINITFAILED` is VPD, the printer port.

**The agent's screenshot is not a desktop** (`desktop-640x480x4.png`):
the top third shows five colour bars, the rest is noise. On the Ivy
Bridge at the same 640x480x4 the same agent captured a clean desktop.
Whether the monitor shows the same thing, or only the capture is wrong,
is not known; the owner has not been asked.

## Outputs, from the VBT (`VIDEOROM.BIN`)

The image sums to `03h` mod 256, the same residue the DOS survey reported
for the shadowed ROM. Its first KiB equals the survey's dump except at
offset 17h (`64h` in DOS, `B1h` here). The Ivy Bridge image differed from
its survey at the same offset. Beyond the first KiB there is nothing to
compare against.

The BDB's general-definitions block (block 2, 7 children of 33 bytes)
describes one output beside the CRT:

- CRT DDC on GMBUS pin 2;
- child handle `0004`, device type `60D2` = `DEVICE_TYPE_HDMI`, DVO port 2
  (HDMI port C), DDC pin 4.

Decoded against i915's `intel_vbt_defs.h`. On 2026-09-28 the DDC answer
was a Samsung Odyssey G5 (EDID `4C2D:750D`, preferred 2560x1440 at
241.5 MHz) - beyond the 1280x1024 the BIOS then described. Which monitor
and which connector are in use now is not recorded.

## Not part of the display

The PCH's EHCI controller `8086:3B34` (class `0C0320`) is bound to
"xHCI98 USB 3.x eXtensible Host Controller" and has problem code 10. An
xHCI driver on an EHCI function would explain that; not investigated.

## Left on the guest

`C:\V9XDIAG\` holds `V9XHSW.EXE`, `V9XHSW.INI`, `DISPCLS.REG` and
`IGDENUM.REG`. The agent cannot delete files, so they stay. Nothing else
was changed.

## Not measured

- Tier-0 `vbe` on this machine, the plan's step 2: which modes the BIOS
  describes now that stolen is 128 MiB, and the 832-byte 800x600x8 pitch.
- Any MMIO. `V9XG4MM` lists 965-family device IDs only and would refuse
  `0042`; it would also need Ironlake's display split, the CPU pipe and
  the PCH transcoder across FDI, before its display half means anything.
  PGTBL_CTL, the GTT's place in BAR0, the ring and the display state are
  all open.
- What the monitor shows, and whether the restart path works on this
  machine.
