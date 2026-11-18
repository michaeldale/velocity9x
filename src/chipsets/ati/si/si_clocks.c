/*
 * Southern Islands clock targets from the VBIOS (velocity9x\si_clocks.h).
 */
#include "velocity9x/si_clocks.h"

/* An engine or memory clock: a low word and a high byte, 10 kHz units. */
static v9x_u32 v9x_si_clock24(const struct v9x_atom *atom, v9x_u32 at)
{
    return v9x_atom_u16(atom, at) | (v9x_atom_u8(atom, at + 2ul) << 16);
}

v9x_status v9x_si_clocks_plan(const struct v9x_atom *atom, v9x_u32 profile,
                              struct v9x_si_clock_plan *plan)
{
    v9x_u32 fw;
    v9x_u32 pp;
    v9x_u32 clocks;
    v9x_u32 clock_entries;
    v9x_u32 clock_bytes;
    v9x_u32 states;
    v9x_u32 state_count;
    v9x_u32 state;
    v9x_u32 at;
    v9x_u32 levels;
    v9x_u32 level;
    v9x_u32 entry;
    v9x_u32 vddc;
    v9x_u32 sclk;
    v9x_u32 mclk;

    if (atom == 0 || plan == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    plan->default_sclk = 0ul;
    plan->default_mclk = 0ul;
    plan->boot_vddc = 0ul;
    plan->levels = 0ul;
    plan->levels_allowed = 0ul;
    plan->sclk = 0ul;
    plan->mclk = 0ul;

    fw = v9x_atom_data_table(atom, V9X_ATOM_DATA_FIRMWARE_INFO);
    if (fw == 0ul || v9x_atom_u8(atom, fw + 2ul) != 2ul ||
        v9x_atom_u16(atom, fw) < V9X_SI_FW_MIN_BYTES ||
        fw + V9X_SI_FW_MIN_BYTES > atom->rom_bytes) {
        return V9X_STATUS_UNSUPPORTED;
    }
    plan->default_sclk = v9x_atom_u32(atom, fw + V9X_SI_FW_DEFAULT_SCLK) &
                         0x00fffffful;
    plan->default_mclk = v9x_atom_u32(atom, fw + V9X_SI_FW_DEFAULT_MCLK) &
                         0x00fffffful;
    plan->boot_vddc = v9x_atom_u16(atom, fw + V9X_SI_FW_BOOT_VDDC);
    plan->sclk = plan->default_sclk;
    plan->mclk = plan->default_mclk;
    if (plan->default_sclk == 0ul || plan->default_mclk == 0ul ||
        plan->boot_vddc == 0ul) {
        return V9X_STATUS_UNSUPPORTED;
    }
    if (profile == V9X_SI_CLOCKS_BIOS) {
        return V9X_STATUS_OK;
    }
    if (profile != V9X_SI_CLOCKS_BOOT_VOLTAGE) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    pp = v9x_atom_data_table(atom, V9X_ATOM_DATA_POWERPLAY_INFO);
    if (pp == 0ul || v9x_atom_u8(atom, pp + 2ul) != V9X_SI_PP_FORMAT_REV ||
        pp + v9x_atom_u16(atom, pp) > atom->rom_bytes) {
        return V9X_STATUS_UNSUPPORTED;
    }
    clocks = pp + v9x_atom_u16(atom, pp + V9X_SI_PP_CLOCK_ARRAY);
    states = pp + v9x_atom_u16(atom, pp + V9X_SI_PP_STATE_ARRAY);
    clock_entries = v9x_atom_u8(atom, clocks);
    clock_bytes = v9x_atom_u8(atom, clocks + 1ul);
    if (clock_bytes < V9X_SI_PP_CLOCK_MIN ||
        clocks + 2ul + clock_entries * clock_bytes > atom->rom_bytes) {
        return V9X_STATUS_UNSUPPORTED;
    }

    /* Every level of every state (ATOM_PPLIB_STATE_V2: a level count, the
     * non-clock index, then a clock-info index a level). Entries no state
     * names are not levels: W9QI0DAD's VBIOS has two, at 500 MHz engine
     * and 1125 MHz memory. */
    state_count = v9x_atom_u8(atom, states);
    at = states + 1ul;
    for (state = 0ul; state < state_count; ++state) {
        levels = v9x_atom_u8(atom, at);
        if (at + 2ul + levels > atom->rom_bytes) {
            return V9X_STATUS_UNSUPPORTED;
        }
        for (level = 0ul; level < levels; ++level) {
            entry = v9x_atom_u8(atom, at + 2ul + level);
            if (entry >= clock_entries) {
                return V9X_STATUS_UNSUPPORTED;
            }
            entry = clocks + 2ul + entry * clock_bytes;
            sclk = v9x_si_clock24(atom, entry);
            mclk = v9x_si_clock24(atom, entry + 3ul);
            vddc = v9x_atom_u16(atom, entry + 6ul);
            ++plan->levels;
            /* VDDC 0 or a leakage id (FF01h and up) is not a voltage. */
            if (vddc == 0ul || vddc > plan->boot_vddc) {
                continue;
            }
            ++plan->levels_allowed;
            if (sclk > plan->sclk) {
                plan->sclk = sclk;
            }
            if (mclk > plan->mclk) {
                plan->mclk = mclk;
            }
        }
        at += 2ul + levels;
    }
    return V9X_STATUS_OK;
}
