# The Bochs/QEMU display interface is not used for modes or flips

Date: 2026-11-06. Feature gap, found in a feature review. Status: open,
nothing written, nothing measured.

## Gap

The VBE family binds the QEMU/Bochs std-vga id but drives it like any VBE
card: only the BIOS's mode list, and every DirectDraw Flip declined, so
full-screen DirectDraw presents by DirectDraw's own copy. No module sets
the display start (4F07h or otherwise).

## Feature

On the Bochs DISPI interface (index/data ports 01CEh/01CFh, present on
Bochs, QEMU std-vga and VirtualBox's adapter):

1. arbitrary resolutions within the adapter's limits, so a test guest can
   run the size a test needs;
2. page flips by moving the display start (the Y offset registers) to the
   back buffer, instead of declining;
3. optionally a display-start flip through VBE 4F07h for real cards whose
   BIOS supports it, which is the same feature in the generic family.

This would mainly speed up and widen emulator testing; it must stay off on
any card that is not the virtual adapter (detect by the DISPI ID register,
not the PCI id alone).

## Acceptance

On a QEMU guest, a custom 1280x800 mode sets, and V9XDDP's flip checks show
flips taken rather than declined.
