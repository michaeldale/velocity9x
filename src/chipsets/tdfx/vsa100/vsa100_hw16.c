/*
 * 3dfx VSA-100, PCI 121A:0009: every Voodoo4 and Voodoo5 board.
 *
 * Measured on a retail Voodoo4 4500 AGP in A8U4I5 on 2026-10-11, under 3dfx's
 * own 1.04.00 driver
 * (docs\decisions\2026-10-11-voodoo4-stock-driver-tiles-its-desktop-at-the-
 * top-of-memory.md): subsystem 0004121A, revision 01, one chip, 32 MiB SDRAM,
 * BAR0 registers and BAR1 framebuffer of 128 MiB each, BIOS 1.15.
 *
 * Tier-0, both hooks NULL: the VBE sets modes and reports the framebuffer,
 * and the CPU draws. No 3dfx register is written. framebuffer_bar is 1 for
 * the record; with no read_aperture hook the aperture comes from VBE 4F01h
 * and the field is not consulted.
 */
#include "velocity9x/hw16.h"

/* Not static: resolved by name in the link map by the per-object audit. */
const V9X_HW16_DEVICE v9x_vsa100_device = {
    0x121au, 0x0009u,
    "3dfx VSA-100",
    "121A", "0009",
    "tdfx-vsa100-unavailable-v1",
    "vbe-lfb",
    0,
    0,
    0,
    0,
    1u
};
