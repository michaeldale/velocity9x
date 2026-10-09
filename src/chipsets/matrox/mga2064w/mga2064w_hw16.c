/*
 * Matrox Millennium, MGA-2064W, PCI 102B:0519, with a TI TVP3026 RAMDAC.
 *
 * The card in A8U4I5 from 2026-10-09: revision 01, subsystem 0000, BIOS
 * VBE 2.0. Configuration Manager places its 16 KiB control aperture
 * (MGABASE1) in BAR0 and its framebuffer (MGABASE2) in BAR1, the reverse
 * of the Millennium II; every linear-framebuffer mode the BIOS advertises
 * reports its PhysBasePtr at the BAR1 base
 * (docs\decisions\2026-09-10-the-2064w-is-drivable-by-the-vbe-path.md).
 *
 * Tier-0, both hooks NULL: the VBE sets modes and 4F01h reports the
 * framebuffer, and the CPU draws. No MGA register is written.
 */
#include "velocity9x/hw16.h"

/* Not static: resolved by name in the link map by the per-object audit. */
const V9X_HW16_DEVICE v9x_mga2064w_device = {
    0x102bu, 0x0519u,
    "Matrox Millennium MGA-2064W",
    "102B", "0519",
    "matrox-mga2064w-unavailable-v1",
    "vbe-lfb",
    0,
    0,
    0,
    0,
    /* MGABASE2 is BAR1 on this chip. No hook in this family reads a BAR at
     * tier-0 - the BIOS reports the framebuffer - so this is stated for the
     * engine hook that will, not consulted today. */
    1u
};
