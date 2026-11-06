# VMware SVGA II is not supported

Date: 2026-11-06. Feature gap, found in a feature review. Status: open,
low priority, nothing written, nothing measured.

## Gap

No family claims the VMware SVGA II adapter (`15AD:0405`), which VMware
and QEMU's `vmware-svga` provide. Under VMware a Win98 guest falls back to
VGA or to the VBE family's BIOS modes.

## Feature

A `vmware` family on the SVGA II register interface: mode set to any size
at 8/16/32 bpp, the command FIFO for screen updates, host-accelerated
copies and fills for GDI and DirectDraw, and the host-drawn hardware
pointer. No 3D (SVGA3D is a separate project).

## Acceptance

A VMware (or QEMU `-vga vmware`) guest reaches the desktop on the family at
two depths, with DirectDraw fills and copies on the FIFO and V9XDDP run.
