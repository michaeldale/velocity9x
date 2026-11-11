# M-BXMFA8's GeForce 210 under the `vbe` package: first look

Date: 2026-11-11

Phase 0 of [docs/plans/nvidia-gt218.md](../plans/nvidia-gt218.md). Read
from the guest over the relay at boot 81 (signed `vbe` package,
`enable-ok` at 1024x768x16). Nothing was written to the card.

Evidence (gitignored): `build/driver-results/m-bxmfa8-gt218-boot81/` —
V9XBOOT.INI, V9XHW.INI, V9XMODES.INI, V9XDDH.INI, `enum-pci-0A65.json`
(HKLM\Enum\PCI instance), `cm-enum-0A65.json` (HKEY_DYN_DATA Config Manager
node, with the raw Allocation), `inventory.json`.

## Identity

From the Windows PCI enumerator, not from config space read by our code:

- Display: `PCI\VEN_10DE&DEV_0A65&SUBSYS_140110B0&REV_A2`, instance
  `000800`, class 030000. Subsystem vendor 10B0 is Gainward.
- HDMI audio: `PCI\VEN_10DE&DEV_0BE3&SUBSYS_140110B0&REV_A1`, instance
  `010800`, no driver (Config Manager problem 1Ch).
- Dell Q45/ICH10 (8086:2E20 host bridge).

## Resources Windows allocated

Decoded from the Config Manager Allocation. The order of the memory
ranges is the order Config Manager lists them; which BAR number each one
is has not been read from config space.

| Range | Size | Prefetchable flag | Sources' role (nouveau) |
|---|---|---|---|
| `FD000000-FDFFFFFF` | 16 MiB | no | BAR0, registers |
| `D0000000-DFFFFFFF` | 256 MiB | yes | BAR1, VRAM aperture |
| `CE000000-CFFFFFFF` | 32 MiB | yes | BAR3, RAMIN aperture |
| I/O `DC00-DC7F` | 128 B | — | BAR5 |
| `FE000000-FE07FFFF` | 512 KiB | — | expansion ROM |

Also the VGA legacy ranges (I/O 3B0-3BB, 3C0-3DF, memory A0000-BFFFF) and
`C0000-CE5FF` (the shadowed video BIOS, 58 KiB).

## VBE

- VBE 3.0, OEM software revision `7018`, capabilities `00000001`.
- Total memory 224 x 64 KiB = **14 MiB**. The card's real memory size is
  not known yet; the sources put it in PFB, read in Phase 2.
- 29 modes listed, 26 published (V9XMODES `Reasons=19,...,7,...`).
  320x200 to 1280x1024 at 8, 16 and 32 bpp, plus
  1280x800 at 8 and 32. No 1600 or 1920 mode, no 1280x800x16.
- 16 bpp is 5:6:5 in every mode; 32 bpp is x8r8g8b8.
- Every mode's PhysBasePtr is **`CF000000`**.
- No EDID through 4F15h.

## What this disputes

The sources would put a linear framebuffer in BAR1 (the 256 MiB VRAM
aperture). The VBE's `CF000000` is instead 16 MiB into the 32 MiB range
at `CE000000`, the range nouveau calls the RAMIN (BAR3) aperture. Two
readings fit and neither is checked:

1. Config Manager's order is not BAR order, and `CE000000` is BAR1 sized
   at 32 MiB by a BIOS that shrank it.
2. The VBE BIOS maps its 16 MiB LFB window through BAR3's VM.

Phase 2 reads config space and the BAR1/BAR3 VM state to decide it. Until
then nothing here hard-codes which BAR the framebuffer is in; the tier-0
claim takes the aperture from VBE 4F01h, as the RIVA row does.

The VBE's 14 MiB is 2 MiB short of the 16 MiB window it publishes; the
`vbe` package's MTRR covers 8 MiB of it write-combining.
