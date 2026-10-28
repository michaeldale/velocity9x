/*
 * AMD Carrizo, PCI 1002:9874 rev 88 (Radeon R7E, GCN 3), the GPU in the
 * GX-420GI APU.
 *
 * The physical target is the remote-help machine M-XP7053 since 2026-10-28:
 * an HP board, subsystem 103C:8158. The VBE reports 48 MiB and its
 * framebuffer at E0000000 (BAR0); the video memory is a carve-out of system
 * RAM (docs\decisions\2026-10-28-m-xp7053-carrizo-vbe-first-look.md).
 *
 * Carrizo shares nothing with Evergreen below PCI/VGA except the display
 * controller's register layout, and every engine it has - the command
 * processor, the RLC and both SDMA engines - runs AMD microcode before it
 * does anything (docs\plans\ati-carrizo-gfx8.md). Until the plan's register
 * probe has read this chip, it carries no hook: the VBE 4F02h mode set
 * programs it, 4F01h reports where the framebuffer landed, 4F00h reports
 * its size, and the CPU draws. The BIOS lists no 8 bpp mode; the runtime
 * scan hides the family's 8 bpp baseline rows.
 */
#include "velocity9x/hw16.h"

/*
 * Not static: the per-object audit resolves this symbol by name out of the
 * link map to prove this chip's module is actually in the family image.
 */
const V9X_HW16_DEVICE v9x_vi_carrizo_device = {
    0x1002u, 0x9874u,
    "AMD Radeon R7E (Carrizo)",
    "1002", "9874",
    "ati-vi-unavailable-v1",
    "vbe-lfb",
    "none",
    "not-advertised",
    0,
    0
};
