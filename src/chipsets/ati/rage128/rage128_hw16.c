/*
 * ATI Rage 128 Pro, PCI 1002:5046 ("PF", AGP 4x).
 *
 * The physical target is A8U4I5 since 2026-10-22: subsystem 0018:1002,
 * revision 00, on the AGP bus (Enum \000800), bound by Windows to Standard
 * PCI VGA. Its resources as Windows assigned them: a 64 MiB prefetchable
 * framebuffer window at DC000000 and a 16 KiB register window at E1000000
 * (docs\plans\ati-rage128.md).
 *
 * The Rage 128 is not a Mach64. Its register file, 2D engine (GUI_* and
 * DP_*), CRTC and PLL are a new design, its register window is 16 KiB where
 * the Mach64's is 4 KiB, and it has no CONFIG_CHIP_ID at the Mach64's
 * offset. Nothing in mach64_engine.c, the Rage II code or the mini-VDD's
 * ATI MMIO map applies to it, so this chip carries no hook at all: the VBE
 * 4F02h mode set programs the card, 4F01h reports where the framebuffer
 * landed, 4F00h reports its size, and the CPU draws. The engine claim comes
 * with the plan's register probe, measured on this card.
 */
#include "velocity9x/hw16.h"

/*
 * Not static: the per-object audit resolves this symbol by name out of the
 * link map to prove this chip's module is actually in the family image.
 */
const V9X_HW16_DEVICE v9x_rage128_device = {
    0x1002u, 0x5046u,
    "ATI Rage 128 Pro",
    "1002", "5046",
    "ati-rage128-unavailable-v1",
    "vbe-lfb",
    0,
    0,
    0,
    0
};
