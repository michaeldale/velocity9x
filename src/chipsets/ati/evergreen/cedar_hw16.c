/*
 * AMD Radeon HD 5450, PCI 1002:68F9 (Evergreen "Cedar", PCIe).
 *
 * The physical target is the remote-help machine M-BXMFA8 since 2026-10-26:
 * subsystem 1028:010E, revision 00, HDMI audio at function 1 (1002:AA68).
 * Windows assigned a 256 MiB framebuffer window at D0000000 (BAR0), a
 * 128 KiB register window at FEAE0000 (BAR2) and 256 I/O ports at D000
 * (BAR4); BAR0 and BAR2 are 64-bit
 * (docs\decisions\2026-10-26-m-bxmfa8-radeon-68f9-first-look.md).
 *
 * Evergreen shares nothing with the Mach64 or the Rage 128 below PCI/VGA,
 * and it has no fixed-function 2D engine at all: copies and fills are for
 * the async DMA engine, everything else for the shader core
 * (docs\plans\ati-evergreen-cedar.md). Until the plan's register probe has
 * read this card, the chip carries no hook: the VBE 4F02h mode set programs
 * it, 4F01h reports where the framebuffer landed, 4F00h reports its size
 * (16 MiB, less than the board carries), and the CPU draws.
 */
#include "velocity9x/hw16.h"

/*
 * Not static: the per-object audit resolves this symbol by name out of the
 * link map to prove this chip's module is actually in the family image.
 */
const V9X_HW16_DEVICE v9x_evergreen_cedar_device = {
    0x1002u, 0x68F9u,
    "AMD Radeon HD 5450 (Cedar)",
    "1002", "68F9",
    "ati-evergreen-unavailable-v1",
    "vbe-lfb",
    0,
    0,
    0,
    0
};
