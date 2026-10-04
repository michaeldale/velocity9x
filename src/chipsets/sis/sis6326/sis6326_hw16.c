/*
 * SiS 6326, PCI 1039:6326.
 *
 * Measured on two boards in A8U4I5 on 2026-10-04
 * (docs\decisions\2026-10-04-sis6326-first-survey.md):
 *
 *     revision C3, subsystem 63261039, BIOS 1.06 (1997), 4 MiB EDO
 *     revision 0B, subsystem 63261569, BIOS 1.28q (1999), 4 MiB SGRAM
 *
 * Both are AGP with a 4 MiB BAR0 framebuffer and a 64 KiB BAR1 register
 * window. The C3 board locks that machine under SiS's own driver and is
 * parked; the 0B board is the bring-up target.
 *
 * Tier-0, both hooks NULL: the VBE sets modes and reports the framebuffer,
 * and the CPU draws. No SiS register is written, so the extension lock
 * (SR05) is left however the BIOS left it.
 */
#include "velocity9x/hw16.h"

/* Not static: resolved by name in the link map by the per-object audit. */
const V9X_HW16_DEVICE v9x_sis6326_device = {
    0x1039u, 0x6326u,
    "SiS 6326",
    "1039", "6326",
    "sis-6326-unavailable-v1",
    "vbe-lfb",
    0,
    0,
    0,
    0
};
