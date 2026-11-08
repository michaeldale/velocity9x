# The GM45 on W9QI3DAB: read-only audit of issue 22's HP t5740

Date: 2026-11-08
Status: **read once each over the relay, nothing written to the device**
except the copies the agent's `get` makes. No driver change, no MMIO read
by us, no reboot.

Captures: `docs\probe\w9qi3dab-gl40-audit-2026-11-08\`.

| | |
|---|---|
| Machine | issue 22's reporter's HP t5740, computer `W9QI3DAB`, remote help `M-6B21TJ` (agent 0.15.0), registered 2026-11-07 |
| CPU | Atom N280, 1.66 GHz, CPUID family 6 model 28 stepping 2 |
| Memory | 2,076,344,320 bytes reported by Windows |
| BIOS | American Megatrends 03/22/11 |
| IGD | `8086:2A42` rev `09` (GMA 4500MHD, GM45/GL40), SUBSYS `2A42:8086` |
| Monitor | `MONITOR\VSCE343` |
| OS | Windows 98 SE 4.10.2222 A, DirectX 4.09.00.0904 (6.1), IE 6 |
| Disk | FAT32 C:, 15.9 GB, through SweetLow's ATA driver |
| Network | NETGEAR WG111v3 USB wireless |
| Display driver | Velocity9x `intel-gma`, `[V9x.Install.gma-4500mhd-gm45]`, 1024x768x16 |

## What it settles

- **The GPU is the one the memory inferred.** The live devnode is
  `PCI\VEN_8086&DEV_2A42&SUBSYS_2A428086&REV_09` at 00:02.0, bound to
  `Display\0001`, `V9xFamily=intel-gma`. `M-6B21TJ` is the machine behind
  report `V9X-EXR8BE` and
  [`2026-11-05-gm45-gl40-field-report.md`](2026-11-05-gm45-gl40-field-report.md).
- **It runs the 0.40.0 release, not 0.35.0.** `v9xdisp.drv` hashes
  `6EE180B8...` (SHA-256), which matches `velocity9x-0.40.0-intel-gma.zip`
  and none of 0.37.0 to 0.43.0's other packages. `V9XSYNC.INI` and the
  `INTEL*.TXT` captures carry `5977e375`, the 0.40.0 changelog commit.
  `V9XSNAP.INI` is still the 0.35.0 one from the report (`a4221eee`); no
  snapshot has been taken since.
- `V9XBOOT.INI`: `Stage=enable-ok`, aperture from BAR2 at `d0000000`,
  1024x768x16, pitch 2112, 5:6:5. VBE 3.0, 1023 x 64 KiB; the driver uses
  16 MiB. The mode list now has 33 BIOS modes with 1280x800, 1360x768,
  1366x768 and 1440x900; still no 1920x1080. `V9XSYNC.INI`
  `Generation=72`, every mode kept.
- `V9XHW.INI`: `Direct3D=hardware-gen4`, `Direct3DMode=hardware`,
  `EngineStatusEnable=d3d-claimed`, `EngineCaps=00440614` (the 0.35.0
  report had `00443614`; the bits that differ were not decoded here).
- `INTELMM.TXT` (read by the driver at Enable): `Result=PASS`, pipe A at
  1024x768 (`HTOTAL 053F03FF`, `VTOTAL 032502FF`), plane A `94000000`,
  16 bpp, stride `0840`. Pipe B is programmed 640x480 with plane B
  disabled; which connector pipe B belongs to was not read.
- `[Velocity9x] VSync=2` is still in `SYSTEM.INI`, so the flicker the
  first report put down to VSync off still has VSync off.
- The 00:02.1 function (`PCI\CC_0380`) has an orphan `Display\0002`
  entry, `MSDISP.INF [PCIVID]`, tagged `V9xFamily=vbe`, with no live
  devnode in the inventory. `Display\0000` is an orphan `vga.drv` entry.
  Neither was touched.

## The owner's question: Forsaken renders dark

Chat message 161 (2026-11-08 02:19Z): Forsaken renders "in complete
darkness" under both its software renderer and Direct3D 6; Half-Life
draws normally. The game is at `C:\Games\Forsaken` (`ForsakenSW.exe`,
`ForsakenHW.exe`, `Forsaken.exe` launcher, `Player.cfg`).

**Reproduced later the same day. The cause is not gamma:** Gen4 refuses
colour-keyed draws that write depth, and the Direct3D path drops them.
See [`2026-11-08-gen4-keyed-depth-write-draws-dropped-forsaken.md`](../issues/2026-11-08-gen4-keyed-depth-write-draws-dropped-forsaken.md).
The lead below, written before that run, is a dead hypothesis: `intel-gma` has no `load_gamma` hook (only `ati_hw16.c` does), so
`DdiGammaRamp` returns 0 and `C1_GAMMA_RAMP` is not claimed on any Intel
part at any depth. A game that brightens through `IDirectDrawGammaControl`
or `SetDeviceGammaRamp` gets no gamma on this driver. That both renderers
are affected fits a cause on the primary surface rather than in the HAL,
but it fits other causes too (an 8-bpp palette path, a `Player.cfg`
gamma value). Related gap: [`2026-11-06-no-gamma-ramp-at-8bpp-or-on-vbe.md`](../issues/2026-11-06-no-gamma-ramp-at-8bpp-or-on-vbe.md).

## Left on the guest

Nothing new. `C:\V9XDIAG\` already held the driver's own captures.

## Not measured

- Forsaken itself: which mode and depth each renderer sets, whether it
  calls a gamma interface, and what `Player.cfg` holds.
- A V9XTRACE snapshot while a Direct3D process is alive.
- `V9XGL.LOG` and `V9XGLIDE.LOG` (56 KB and 185 KB) were listed, not read.
- Any 0.41.0 to 0.43.0 build on this machine.
- What pipe B drives, and why it is programmed at 640x480.
