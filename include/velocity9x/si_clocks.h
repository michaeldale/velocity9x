/*
 * Southern Islands (Oland): the engine and memory clocks to set, chosen from
 * the card's own VBIOS (docs\plans\ati-si-clocks-from-the-vbios.md).
 *
 * Pure: it reads an AtomBIOS image through velocity9x\atombios.h's
 * accessors and decides; vi_cp.c runs the VBIOS's SetEngineClock and
 * SetMemoryClock to apply it. Clocks are in the VBIOS's 10 kHz units,
 * voltages in mV.
 *
 * The profile is the choice a later Display Properties setting makes:
 *   V9X_SI_CLOCKS_BIOS          the VBIOS's default clocks, as it booted;
 *   V9X_SI_CLOCKS_BOOT_VOLTAGE  the highest engine clock and the highest
 *       memory clock of any PowerPlay state's levels whose VDDC is no
 *       higher than the boot VDDC: the most the card is specified for at
 *       the voltage it runs, since nothing here drives its SVI2 regulator.
 * W9QI0DAD's Oland boots at 300/299 MHz and 900 mV; its states list 400 MHz
 * engine and 1000 MHz memory at 900 mV, and more only above it
 * (docs\decisions\2026-11-18-oland-clocks-left-by-the-bios.md).
 */
#ifndef VELOCITY9X_SI_CLOCKS_H
#define VELOCITY9X_SI_CLOCKS_H

#include "velocity9x/atombios.h"

#define V9X_SI_CLOCKS_BIOS          0ul
#define V9X_SI_CLOCKS_BOOT_VOLTAGE  1ul

/* FirmwareInfo v2.x: default engine and memory clocks at 8 and 12, the
 * boot VDDC at 46 (atombios.h ATOM_FIRMWARE_INFO_V2_2). */
#define V9X_SI_FW_DEFAULT_SCLK  8ul
#define V9X_SI_FW_DEFAULT_MCLK  12ul
#define V9X_SI_FW_BOOT_VDDC     46ul
#define V9X_SI_FW_MIN_BYTES     48ul

/* PowerPlay v6 (pptable.h ATOM_PPLIB_POWERPLAYTABLE): the state, clock-info
 * and non-clock array offsets at 9, 11 and 13; an SI clock-info entry
 * (ATOM_PPLIB_SI_CLOCK_INFO) has the engine clock's low word and high byte
 * at 0, the memory clock's at 3, VDDC at 6. */
#define V9X_SI_PP_FORMAT_REV    6ul
#define V9X_SI_PP_STATE_ARRAY   9ul
#define V9X_SI_PP_CLOCK_ARRAY   11ul
#define V9X_SI_PP_CLOCK_MIN     8ul

struct v9x_si_clock_plan {
    v9x_u32 default_sclk;
    v9x_u32 default_mclk;
    v9x_u32 boot_vddc;
    /* Levels read, and those at or below the boot VDDC. */
    v9x_u32 levels;
    v9x_u32 levels_allowed;
    v9x_u32 sclk;
    v9x_u32 mclk;
};

/*
 * The targets for `profile`. OK with sclk and mclk at least the defaults;
 * UNSUPPORTED (and the defaults where they were read) when a table is
 * missing, of another revision, or out of the image.
 */
v9x_status v9x_si_clocks_plan(const struct v9x_atom *atom, v9x_u32 profile,
                              struct v9x_si_clock_plan *plan);

#endif
