/*
 * ATI 3D Rage IIC AGP (Mach64 "GW", 264GT2C), PCI 1002:4757.
 *
 * The physical target is A8U4I5: subsystem 1002:4757, revision 0x7A, 4 MiB
 * SDRAM, BIOS "ATI MACH64 SDRAM BIOS 3.096", part MACH64GWPCIMTSDU. Measured
 * read-only under ATI's driver on 2026-10-02
 * (docs\decisions\2026-10-02-rage-iic-register-survey.md):
 *
 *     CONFIG_CHIP_ID   == 0x7A004757   ('GW', revision 7A)
 *     BAR2             4 KiB register window, block 0 at +400h
 *     MEM_CNTL code 7  == 4 MiB in the four-bit CTL_MEM_SIZEB table
 *     CFG_MEM_TYPE_T   == 4            (SDRAM)
 *
 * Tier-0, both hooks NULL: the VBE sets modes and reports the framebuffer,
 * and the CPU draws. No engine is claimed, deliberately. This is a Rage II
 * part with no triangle setup engine, so the Mobility's d3d_mach64.c cannot
 * serve it, and d3d_select.c picks that back-end from the engine type alone.
 * Stamping ATI_MACH64 here to get engine fills would hand it Direct3D
 * streams written for registers this chip does not have.
 */
#include "velocity9x/hw16.h"

/* Not static: resolved by name in the link map by the per-object audit. */
const V9X_HW16_DEVICE v9x_rage_iic_device = {
    0x1002u, 0x4757u,
    "ATI 3D Rage IIC AGP",
    "1002", "4757",
    "ati-mach64-unavailable-v1",
    "vbe-lfb",
    0,
    0,
    0,
    0
};
