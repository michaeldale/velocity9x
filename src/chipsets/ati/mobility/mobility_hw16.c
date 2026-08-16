/*
 * ATI Rage Mobility-M AGP (Mach64 "LM"), PCI 1002:4C4D.
 *
 * The physical target: a Gateway Solo 2150, subsystem 107B:2150, revision 0x64,
 * BIOS "ATI MACH64 SDRAM BIOS 4.216", part MACH64LMPCIMTSDU. Its display is a
 * fixed 1024x768 LG LP141XA panel with no EDID - the panel identity was decoded
 * out of the captured video BIOS rather than read from the monitor.
 *
 * Three values are known in advance and are worth asserting the first time this
 * chip is probed, because each proves a different register window is live:
 *
 *     CONFIG_CHIP_ID   low word  == 0x4C4D   ('LM')
 *     HORZ_PANEL_SIZE            == 127      (1024 = (127 + 1) * 8)
 *     VERT_PANEL_SIZE            == 767
 *     CFG_MEM_TYPE_T             == 4        (SDRAM, per the BIOS string)
 *
 * Both hooks are NULL at tier-0. When they are filled in, note that this part
 * is >= 264VTB, so it decodes video memory with the four-bit CTL_MEM_SIZEB
 * table, not the three-bit CTL_MEM_SIZE one its VT2 sibling uses - the two
 * disagree for every code >= 2, and code 3 means 4 MiB on a VT and 2 MiB here.
 * See docs\decisions\2026-08-16-ati-mach64-hardware-audit.md.
 */
#include "velocity9x/hw16.h"

/* Not static: resolved by name in the link map by the per-object audit. */
const V9X_HW16_DEVICE v9x_rage_mobility_device = {
    0x1002u, 0x4c4du,
    "ATI Rage Mobility-M AGP",
    "1002", "4C4D",
    "ati-mach64-unavailable-v1",
    "vbe-lfb",
    0,
    0,
    0,
    0
};
