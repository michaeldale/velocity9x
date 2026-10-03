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

/*
 * The VT2's aliases, the VT3 and VT4: the same 2D engine and video, no 3D
 * engine, so the same tier-0 entry - VBE modes, CPU drawing, no hook. An entry
 * per id because the PCI scan matches one per id and each names its own part
 * in V9XHW.INI (trio_hw16.c says why at length). Bound from ATI's MACXW4 INF
 * list; neither has run anywhere.
 */
#define V9X_VT2_ALIAS_TAIL \
    "ati-mach64-unavailable-v1", \
    "vbe-lfb", \
    0, \
    0, \
    0, \
    0

const V9X_HW16_DEVICE v9x_mach64_vt3_device = {
    0x1002u, 0x5655u,
    "ATI Mach64 VT3 264VT3",
    "1002", "5655",
    V9X_VT2_ALIAS_TAIL
};

const V9X_HW16_DEVICE v9x_mach64_vt4_device = {
    0x1002u, 0x5656u,
    "ATI Mach64 VT4 264VT4",
    "1002", "5656",
    V9X_VT2_ALIAS_TAIL
};
