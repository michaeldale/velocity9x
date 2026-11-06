# The VBE family refuses modes without a usable linear frame buffer

Date: 2026-11-06. Feature gap, found in a feature review. Status: open,
nothing written, nothing measured.

## Gap

`v9x_vbe_mode_admit` (`src/common/vbe_modes.c`) refuses a mode without the
linear attribute (`V9X_VBE_ADMIT_NON_LINEAR`) or with a base below
`V9X_VBE_MIN_PHYS_BASE` unless the family finds its aperture itself. So the
generic family cannot drive:

- VBE 1.2 BIOSes and TSRs, which have no linear frame buffer;
- ISA and VLB cards whose BIOS offers banked modes only;
- boards that place the frame buffer above 4 GiB (Above 4G decoding,
  Resizable BAR), which Win9x cannot map.

## Feature

A fallback to the 64 KiB window at A000h with VBE 4F05h bank switching,
chosen per mode when no usable linear frame buffer exists. The DIB engine
cannot draw through a window, so this needs either a system-memory shadow
of the screen with dirty-rectangle copies through the window, or a
non-DIB-engine draw path. DirectDraw would be limited (no primary lock in
video memory); that limit has to be published, not discovered.

The admit rule change and the bank arithmetic are host-testable; the bank
switch itself is a BIOS call (or the 4F0Ah protected-mode entry).

## Acceptance

One guest with a VBE 1.2 card (86Box) reaches the desktop at 8 and 16 bpp
on the banked path, and the boot record names the path taken.
