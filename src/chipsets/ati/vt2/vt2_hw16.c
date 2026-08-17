/*
 * ATI Mach64 VT2 (264VT2), PCI 1002:5654.
 *
 * The emulated development target. 86Box implements this part's 2D command
 * stream faithfully enough to validate pixel-exactness, but nothing about its
 * timing: GUI_STAT's busy bit reports the emulator's own host-side write queue
 * rather than the engine, FIFO_STAT is hardwired empty, and reads of engine
 * registers silently drain the queue first. So this chip is where the command
 * encoding gets proven and the Mobility is where the synchronisation does.
 *
 * Both hooks are NULL at tier-0: the VBE mode set leaves the linear aperture
 * enabled, and there is no engine to describe yet.
 */
#include "velocity9x/hw16.h"

/*
 * Not static: the per-object audit resolves this symbol by name out of the
 * link map to prove this chip's module is actually in the family image.
 */
const V9X_HW16_DEVICE v9x_mach64_vt2_device = {
    0x1002u, 0x5654u,
    "ATI Mach64 VT2 264VT2",
    "1002", "5654",
    "ati-mach64-unavailable-v1",
    "vbe-lfb",
    0,
    0,
    0,
    0
};
