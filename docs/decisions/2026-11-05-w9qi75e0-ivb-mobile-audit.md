# Ivy Bridge mobile on W9QI75E0: read-only audit of a hybrid-graphics laptop

Date: 2026-11-05
Status: **measured once each, nothing written to the device.** Audit only:
no Velocity9x driver installed, no MMIO read, no reboot.

Captures: `docs\probe\w9qi75e0-audit-2026-11-05\`.

| | |
|---|---|
| Machine | Tom Rossell's W9QI75E0, remote help `M-ZJQ2DX` (agent 0.12.0), registered 2026-11-05 |
| Board | Lenovo, SUBSYS `17AA:5001` on IGD and dGPU, `17AA:5000` on the PCH; Phoenix BIOS 06/14/18. A desktop folder is named `E530`; the model was not confirmed |
| CPU | Core i7-3632QM, 2.2 GHz, CPUID family 6 model 58 stepping 9 |
| Memory | TOM says 16 GiB; Windows reports 2,147,479,552 bytes (the same ceiling as W9QI4E14) |
| Host bridge | `8086:0154` rev `09` (Ivy Bridge mobile) |
| IGD | `8086:0166` rev `09` (HD Graphics 4000 mobile, GT2), SUBSYS `17AA:5001` |
| dGPU | `10DE:0DE3` rev `A1` (GF108M) behind the PEG bridge `8086:0151`, problem 10 |
| VBIOS | "Intel(R) Sandybridge/Ivybridge PCI Accelerated SVGA BIOS", build 2137 (PC 14.34, 03/14/2012); VBT `$VBT SNB/IVB-MOBILE`, VBT version 100, BDB version 166 |
| Panel | `MONITOR\LGD033B` (LG Display), VBT panel type 2 = 1366x768 at 70.00 MHz |
| OS | Windows 98 SE 4.10.2222 A, DirectX 4.09.00.0904, IE 6; 98lite folder on C: |
| Disk | ST1000LM024 through RLoew's AHCI driver; FAT32 C: 64 GB |
| Network | RTL8168 through Oerg866's NDIS2 wrapper |
| Display driver | "Standard PCI Graphics Adapter (VGA)", `vga.drv`, 640x480x4 |

## What is new about it

- **`8086:0166` is not claimed by any family.** `intel-gma` claims
  `0162` only (`packaging\families\intel-gma\family.psd1`,
  `src\chipsets\intel\hsw\hsw_hw16.c`). This is the first mobile Ivy
  Bridge and the first IVB with an LVDS panel to reach the project.
- **It is a hybrid machine.** The GeForce sits behind the PEG bridge with
  no option ROM Windows can see: its devnode fails with "InitROM failing,
  no drv2 in registry". The IGD drives the panel. Nothing here says how the
  mux, if any, is set.
- The owner also runs TOMMOHP (`M-KYHT4Q`, GM965) and TOMMO33 (`M-BWCR7F`,
  Pineview). The desktop shows HDA driver work in progress (`hdadrv9q`,
  `HDAWORKS`, `HDATSR.EXE` in `AUTOEXEC.BAT`).

## Config space (V9XHSW.EXE, `V9XHSW.INI`)

V9XHSW build `31f6efd4-dirty` (rebuilt from main for this run; the dirty
part is another session's docs, no source change). Its only
write is the CF8h select. **The Haswell decode accepts the mobile words with
no refusal** (`Status=0000 Refused=0000`, `UnstableReads=0000`):

| Field | Value | Meaning |
|---|---|---|
| GGC (host 50h, IGD 50h mirror) | `0211` / `0211` | GMS 2 = 64 MiB stolen, GGMS 2 = 2 MiB GTT, IVD 0, locked |
| BDSM (IGD 5Ch, host B0h) | `9BA00001` | stolen data at `9BA00000` |
| BGSM (host B4h) | `9B800001` | GTT stolen at `9B800000` = BDSM - 2 MiB |
| TSEGMB (host B8h) | `9B000001` | |
| TOLUD (host BCh) | `9FA00001` | 2.49 GiB below 4 GiB |
| TOM (host A0h) | `0000_0004_0000_0001` | 16 GiB of DRAM |
| TOUUD (host A8h) | `0000_0004_5E60_0001` | |
| BAR0 GTTMMADR | `E1400004` / `00000000` | MMIO at `E1400000`; GTT at `E1600000`, 512 Ki entries |
| BAR2 GMADR | `D000000C` / `00000000` | aperture at `D0000000` |
| MSAC (IGD 62h) | `02` | 256 MiB aperture |
| BAR4 | `00006001` | I/O at 6000h |
| ASLS (IGD FCh) | `9AF66018` | OpRegion |
| Interrupt | line `0B`, pin A | |

GGC, the stolen size and the aperture are the same as W9QI4E14's; only the
addresses differ. The decode needs nothing new for this part.

## Display state

- The IGD devnode `PCI\VEN_8086&DEV_0166&SUBSYS_500117AA&REV_09` is bound
  to `Display\0001`: `MSDISP.INF [VGA]`, matched on `PCI\CC_0300`,
  `vga.drv` at `4,640,480`. The GeForce is `Display\0002`, the same
  section, failing. `Display\0000` is a third identical entry with no
  devnode (`DISPCLS.REG`, `PCIENUM.REG`).
- `INF\OTHER` holds USB, AHCI and NDIS2 INFs and no display INF. No
  Velocity9x package has been installed.
- `CONFIG.SYS` loads `setver` only. `AUTOEXEC.BAT` runs `net start` and
  `HDATSR.EXE`. `MSDOS.SYS` boots to the GUI with the boot menu enabled
  (`BootMulti=1`). There is no `WININIT.INI`, so nothing is pending.
- The agent screenshot at 4 bpp is clean, unlike Ironlake's
  (`desktop-640x480x4.png`).

## Outputs, from the VBT (`VIDEOROM.BIN`)

The owner's `C:\VIDEOROM.BIN` is 61,440 bytes: 120 blocks by the header
byte, and it checksums to zero over them. The PCIR says device `0106`
(Sandy Bridge) and an image length of 64 KiB, so, as on W9QI4E14, identify
the chip by its PCI id only. How the image was taken is not known.

Decoded against i915's `intel_vbt_defs.h` (general features, block 2,
blocks 40 to 42):

- integrated CRT support set, CRT DDC on GMBUS pin 2;
- child `0008`, type `1022` (internal LFP), DVO port 4 (LVDS), DDC pin 3;
- child `0004`, type `60D2` (HDMI, `NOT_HDMI_OUTPUT` clear), DVO port 1
  (port B), DDC pin 5;
- LFP options: panel type 2; its DTD is 1366x768, blanking 126 x 14,
  70.00 MHz.

The panel PnP id in the VBT entry is not LG's, so the VBT row is the
platform default, not this panel's EDID. Which pipe drives the LVDS was not
read.

## Left on the guest

`C:\V9XDIAG\` (created by this audit) holds `V9XHSW.EXE`, `V9XHSW.INI`,
`DISPCLS.REG` and `PCIENUM.REG`. The agent cannot delete files, so they
stay. Nothing else was changed.

## Not measured

- Tier-0 `vbe` on this machine, and a DOS VBE survey (mode list, LFB,
  EDID through 4F15h).
- Any MMIO: which pipe drives the LVDS, the panel power sequencer, the
  backlight PWM, forcewake, the GTT head.
- The GeForce: its BARs are assigned (`E0000000` 16 MiB, `A0000000`
  512 MiB, `C0000000` 32 MiB, I/O 5000h) but nothing was read from it.
- Whether `hd4000-ivb` runs on `0166` unchanged. The config decode says
  it should get as far as enable; the LVDS path on Gen7 has never run here.
- Whether Windows' restart path works on this machine.
