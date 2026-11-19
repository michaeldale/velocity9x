/*
 * Trinity (the A10's HD 7660D): the engine clock to set, chosen from the
 * APU's own VBIOS (docs\decisions\2026-11-19-trinity-engine-clock-
 * 200mhz.md).
 *
 * Pure: it reads an AtomBIOS image through velocity9x\atombios.h's
 * accessors and decides; tn_clock_apply.c runs the VBIOS's SetEngineClock
 * to apply it. Clocks are in the VBIOS's 10 kHz units. There is no memory
 * clock to choose: the APU's memory is the system's.
 *
 * The target is the highest engine clock of any PowerPlay state's levels
 * whose VDDC index is no higher than the boot clock's: nothing here drives
 * the SMU's voltage, and SetEngineClock sets no voltage. The boot clock is
 * FirmwareInfo's default engine clock, and its VDDC index is the index of
 * a level at that clock. M-D7TFW4 boots at 200 MHz, index 0; its levels
 * are 200 and 304 MHz at index 0, 608 at 2 and 760 at 3, so 304 MHz.
 */
#ifndef VELOCITY9X_TN_CLOCKS_H
#define VELOCITY9X_TN_CLOCKS_H

#include "velocity9x/atombios.h"

/* FirmwareInfo v2.x: the default engine clock at 8 (atombios.h
 * ATOM_FIRMWARE_INFO_V2_2). */
#define V9X_TN_FW_DEFAULT_SCLK  8ul
#define V9X_TN_FW_MIN_BYTES     12ul

/* PowerPlay v6 (pptable.h ATOM_PPLIB_POWERPLAYTABLE): the state and
 * clock-info array offsets at 9 and 11; a Sumo clock-info entry
 * (ATOM_PPLIB_SUMO_CLOCK_INFO) has the engine clock's low word and high
 * byte at 0 and the VDDC index at 3. */
#define V9X_TN_PP_FORMAT_REV    6ul
#define V9X_TN_PP_STATE_ARRAY   9ul
#define V9X_TN_PP_CLOCK_ARRAY   11ul
#define V9X_TN_PP_CLOCK_MIN     4ul
#define V9X_TN_PP_VDDC_INDEX    3ul

struct v9x_tn_clock_plan {
    v9x_u32 default_sclk;
    v9x_u32 boot_vddc_index;
    /* Levels read, and those at or below the boot VDDC index. */
    v9x_u32 levels;
    v9x_u32 levels_allowed;
    v9x_u32 sclk;
};

/*
 * The target. OK with sclk at least the default; UNSUPPORTED (sclk the
 * default where it was read) when a table is missing, of another
 * revision, out of the image, or no level is at the default clock.
 */
v9x_status v9x_tn_clocks_plan(const struct v9x_atom *atom,
                              struct v9x_tn_clock_plan *plan);

#endif
