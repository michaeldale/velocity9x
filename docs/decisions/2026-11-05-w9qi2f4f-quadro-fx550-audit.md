# W9QI2F4F now carries a Quadro FX 550 (NV43): read-only audit

Date: 2026-11-05
Status: **measured once, nothing written to the device.** A read-only
audit from Windows: registry exports, PCI config reads and an agent
screenshot. No driver installed, no MMIO read, no reboot.

Captures: `docs/probe/w9qi2f4f-quadro-fx550-audit-2026-11-05/`.

The owner of M-J0F0SJ (W9QI2F4F, the Clarkdale board of
`2026-10-22-ironlake-w9qi2f4f-audit.md`) fitted a Quadro FX 550 on
2026-11-05 and said in the remote-help chat that it is a test machine
and to go ahead. Agent 0.13.1, agent `BootCounter` 82; the last boot on
record in this repository is 76 (2026-10-27).

| | |
|---|---|
| Card | `10DE:014D` rev `A2`, SUBSYS `10DE:0349`: Quadro FX 550 |
| Slot | bus 1, device 0, behind the CPU's PCIe root port `8086:0041` (00:01.0) |
| VBIOS | at C0000h, `55AA`, 97 x 512 bytes; "Quadro FX 550 VGA BIOS", "Version 5.43.02.97.01", dated 03/13/06; board string "nv43 Board - q456-0" |
| Windows binding | `Display\0003`, `MSDISP.INF [VGA]`, `vga.drv` 640x480x4 |
| DirectX | 4.09.00.0904 (it was 4.08.00.0400 after the 22 Oct install; who upgraded it is not known) |

## Config space (V9XPCIW)

A scratch variant of `tools/diag/matrox_pci_survey_win32.c`, source kept
with the captures (`pci_survey_win32.c`, build `31f6efd4-pciw`). It
scans all 256 buses and dumps display functions, PCI bridges and
00:00.0. Its only write is the CF8h address select; `UnstableReads=0`.
V9XSURV was not used: it is a DOS tool and this machine takes no DOS
programs.

The card (01:00.0):

- Command `0007`: I/O, memory and bus master enabled.
- BAR0 `F8000000` (32-bit, non-prefetchable); BAR1 `D800000C` with BAR2
  `00000000` (64-bit prefetchable at `D8000000`); BAR3 `F7000004` with
  BAR4 `00000000` (64-bit at `F7000000`); ROM BAR `F6FE0000`, disabled.
  No BAR was sized. The bridge's windows above it are `F6F00000-FBFFFFFF`
  and prefetchable `D8000000-DFFFFFFF`.
- Interrupt line 10.
- Capability chain 60h (power management), 68h (MSI), 78h (PCI Express,
  endpoint).

**The link trained at x8, 2.5 GT/s, on a x16 port.** Link Capabilities
on both ends read x16 at 2.5 GT/s (card 84h `00012D01`, root port ACh
`02212D01`); Link Status reads `1081` on both (width 8, speed 1). Decoded
by the PCI Express Base 1.1 layout of these registers. Whether that is
the card, the slot or a seating problem is not known, and it does not
matter for a Windows 98 driver beyond bandwidth.

## The IGD is gone

- Host bridge GGC (52h) reads `0002` and DEVEN (54h) `00000003`, against
  `0380` and the IGD present on 22 Oct. Read with i915's Ironlake tables,
  GGC bit 1 is "IGD VGA disable" and DEVEN bit 3, now clear, is the IGD
  function. Not checked against an Intel document.
- TOLUD (B0h) reads `D800` where it read `D000`: 3.375 GiB, so the
  128 MiB of stolen memory went back to the system.
- No `8086:0042` devnode is present in `HKEY_DYN_DATA\Config Manager\Enum`.
  The registry still holds the Velocity9x Ironlake install: devnode
  `BUS_00&DEV_02&FUNC_00` bound to `Display\0002`
  (`V9x.Install.hd-ironlake`). It will come back if the card is removed.

So the BIOS disables the IGD when a PEG card is fitted, and nothing in
this measurement can run the Ironlake code until the card comes out.

## Stale state on the guest

- `C:\V9XDIAG\V9XBOOT.INI` is from the last Ironlake boot (1024x768x32,
  WC at `D0000000`), not from this one: `vga.drv` writes none.
- `C:\BOOTLOG.TXT` is from a logged boot that loaded `v9xdisp.drv` and
  `v9xmini.vxd`, so it predates the card too.
- `SYSTEM.INI` keeps `[Velocity9x.Direct3DDdi]`; `display.drv` is
  `pnpdrvr.drv`, the boot description "Standard PCI Graphics Adapter (VGA)".
- No `WININIT.INI` is pending.

## The 4 bpp screenshot is not the IGD

`desktop.png` shows the same five colour bars and noise as the 22 Oct
`desktop-640x480x4.png`. Then the IGD drove the screen; now the NV43 does.
That kills the reading that the 4 bpp garbage was Ironlake-specific. The
Ivy Bridge captured a clean 640x480x4 desktop with an older agent, so the
cause, in the agent's 4 bpp path or in this machine, is still open. What
the monitor shows has not been asked.

## What Velocity9x has for this card

None. The `nvidia` family lists the RIVA 128 (`12D2:0018`) only; `vbe`
binds any `PCI\CC_0300`. An NV43 would start at tier-0 `vbe`, the
step taken on the RIVA 128 (`2026-11-02-m-7v636c-riva128-first-look.md`).

## Not measured

- The VBE mode list, video memory size and EDID: they need V9XSURV (DOS)
  or a tier-0 `vbe` install.
- Any NV43 register (BAR0 MMIO).
- What is attached to which of the card's outputs.
