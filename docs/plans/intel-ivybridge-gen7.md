# Ivy Bridge (Gen7, HD 4000): parked, no hardware

Status: **parked 2026-10-20.** One survey, no machine to run builds on.
Nothing is implemented. `8086:0162` stays with the `vbe` family.

## Evidence

`docs\probe\references\msi-b75ma-p45-ivb-hd4000-vgasurv-2026-10-19-16503bf2.ini`:
an MSI B75MA-P45 with an i5-3570K, real DOS, build `16503bf2`, complete
report. The IGD is `8086:0162` rev 09, SUBSYS `1462:2111`, the only display
device.

- GGC `0211`: 64 MB stolen, 2 MB GTT, locked. BDSM `DBA00001`. MSAC `02`,
  256 MB aperture. BAR0 `F7800004`, BAR2 `E000000C`, both 64-bit below 4 GB.
- VBE 3.0 lists 15 usable modes up to 1920x1080 at 8, 16 and 32 bpp, with a
  linear framebuffer at `E0000000`. `0103` (800x600x8) has an 832-byte pitch.
  The 24 OEM modes return attributes 0.
- The VBIOS PCIR device id is `0102` (Sandy Bridge): the image serves both
  generations. Identify the chip by its PCI id only.
- `Block1Status=duplicate-of-block0`: a second Intel VBIOS that ignores the
  block number in `4F15h`, after the Ironlake chinaboard.
- `FlatPanelStatus=unsupported`, so this report does not exercise the `4F11h`
  fix in `docs\issues\2026-09-28-survey-4f11-writes-to-ds-zero.md`.

Tier-0 `vbe` has not been tried on this board.

## What a port would change

The documents and the full comparison are in
`C:\everything\claude\personal\intel driver research\ivybridge-research-notes.md`.
That folder holds the IVB PRM, the 3rd-gen Core and 7 Series PCH datasheets,
Mesa genxml and crocus, and Linux i915. Against the `hd4600-hsw` path:

- **Forcewake.** The acknowledge register is `0x130040`, not `0x130044`.
  Some parts use legacy `0xA18C` / `0x130090` instead; ECOBUS `0xA180` bit 5
  decides which (i915 `intel_uncore.c`). The PRM gives neither address.
- **GTT PTE.** Bits 11:4 hold address 39:32, bit 3 is GFDT and bits 2:1 are
  cacheability, where `00` is reserved (PRM Vol 1 Pt 2 §2.2.2.1).
- **Flip offset.** `0x701A4` is the tiled offset and is ignored for linear
  surfaces. Linear panning uses `PRI_LINOFF` at `0x70184`, in bytes (PRM
  Vol 3 Pt 3 §5.3).
- **SURFACE_STATE DW7.** The channel selects there are Haswell-only.
- **3DSTATE_PS DW4.** Max threads is at 31:24 and there is no sample-mask
  field.
- **IVB-only workarounds** (crocus, i915):
  - the VS flush before VS, URB_VS and CONSTANT_VS
  - a CS stall after the push-constant allocation
  - a CS stall on every fourth PIPE_CONTROL
  - the pre-HSW PIPE_CONTROL bit restrictions
  - the L3 settings
- **GGC.** GMS `11h` (1 GB) is valid on IVB (datasheet 326765). The HSW
  decode refuses it.

These carry over unchanged:

- the ring registers
- the blitter commands
- the MI commands in use
- the send encodings the kernels use
- the plane and pipe register addresses
- the scanline offset of +1
- D24_UNORM_X8

## To resume

This needs someone who can run builds on an Ivy Bridge and send logs back.
The decode, PTE and plane-split changes can be written and host-tested from
the PRM before the first boot. Everything the 4570 measured is re-measured,
not assumed:

- reserve placement
- the unwritable top megabyte of stolen memory
- the BIOS GTT mapping
- the blit pitch unit
- the hang cases
- which forcewake variant the part uses

The order follows `intel-haswell-bringup.md`: tier-0 `vbe` first.
