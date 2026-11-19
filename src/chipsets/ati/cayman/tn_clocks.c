/*
 * Trinity's engine-clock target from the VBIOS (velocity9x\tn_clocks.h).
 */
#include "velocity9x/tn_clocks.h"

/* An engine clock: a low word and a high byte, 10 kHz units. */
static v9x_u32 v9x_tn_clock24(const struct v9x_atom *atom, v9x_u32 at)
{
    return v9x_atom_u16(atom, at) | (v9x_atom_u8(atom, at + 2ul) << 16);
}

/*
 * Walks every level of every state (ATOM_PPLIB_STATE_V2: a level count,
 * the non-clock index, then a clock-info index a level). With count set it
 * finds the boot clock's VDDC index; without, the target. Entries no state
 * names are not levels. UNSUPPORTED for a state or index past the image or
 * the array, or, counting, no level at the boot clock.
 */
static v9x_status v9x_tn_clocks_walk(const struct v9x_atom *atom,
                                     v9x_u32 clocks, v9x_u32 states,
                                     struct v9x_tn_clock_plan *plan,
                                     int count)
{
    v9x_u32 clock_entries;
    v9x_u32 clock_bytes;
    v9x_u32 state_count;
    v9x_u32 state;
    v9x_u32 at;
    v9x_u32 levels;
    v9x_u32 level;
    v9x_u32 entry;
    v9x_u32 sclk;
    v9x_u32 vddc_index;
    int found = 0;

    clock_entries = v9x_atom_u8(atom, clocks);
    clock_bytes = v9x_atom_u8(atom, clocks + 1ul);
    if (clock_bytes < V9X_TN_PP_CLOCK_MIN ||
        clocks + 2ul + clock_entries * clock_bytes > atom->rom_bytes) {
        return V9X_STATUS_UNSUPPORTED;
    }
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
            sclk = v9x_tn_clock24(atom, entry);
            vddc_index = v9x_atom_u8(atom, entry + V9X_TN_PP_VDDC_INDEX);
            if (count) {
                /* First pass: the boot clock's VDDC index, the lowest of
                 * any level at it. */
                ++plan->levels;
                if (sclk == plan->default_sclk &&
                    (!found || vddc_index < plan->boot_vddc_index)) {
                    plan->boot_vddc_index = vddc_index;
                    found = 1;
                }
                continue;
            }
            if (vddc_index > plan->boot_vddc_index) {
                continue;
            }
            ++plan->levels_allowed;
            if (sclk > plan->sclk) {
                plan->sclk = sclk;
            }
        }
        at += 2ul + levels;
    }
    if (count && !found) {
        return V9X_STATUS_UNSUPPORTED;
    }
    return V9X_STATUS_OK;
}

v9x_status v9x_tn_clocks_plan(const struct v9x_atom *atom,
                              struct v9x_tn_clock_plan *plan)
{
    v9x_u32 fw;
    v9x_u32 pp;
    v9x_u32 clocks;
    v9x_u32 states;
    v9x_status status;

    if (atom == 0 || plan == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    plan->default_sclk = 0ul;
    plan->boot_vddc_index = 0ul;
    plan->levels = 0ul;
    plan->levels_allowed = 0ul;
    plan->sclk = 0ul;

    fw = v9x_atom_data_table(atom, V9X_ATOM_DATA_FIRMWARE_INFO);
    if (fw == 0ul || v9x_atom_u8(atom, fw + 2ul) != 2ul ||
        v9x_atom_u16(atom, fw) < V9X_TN_FW_MIN_BYTES ||
        fw + V9X_TN_FW_MIN_BYTES > atom->rom_bytes) {
        return V9X_STATUS_UNSUPPORTED;
    }
    plan->default_sclk = v9x_atom_u32(atom, fw + V9X_TN_FW_DEFAULT_SCLK) &
                         0x00fffffful;
    plan->sclk = plan->default_sclk;
    if (plan->default_sclk == 0ul) {
        return V9X_STATUS_UNSUPPORTED;
    }

    pp = v9x_atom_data_table(atom, V9X_ATOM_DATA_POWERPLAY_INFO);
    if (pp == 0ul || v9x_atom_u8(atom, pp + 2ul) != V9X_TN_PP_FORMAT_REV ||
        pp + v9x_atom_u16(atom, pp) > atom->rom_bytes) {
        return V9X_STATUS_UNSUPPORTED;
    }
    clocks = pp + v9x_atom_u16(atom, pp + V9X_TN_PP_CLOCK_ARRAY);
    states = pp + v9x_atom_u16(atom, pp + V9X_TN_PP_STATE_ARRAY);

    /* The boot clock's VDDC index first, then the levels at or below it. */
    status = v9x_tn_clocks_walk(atom, clocks, states, plan, 1);
    if (status != V9X_STATUS_OK) {
        return status;
    }
    return v9x_tn_clocks_walk(atom, clocks, states, plan, 0);
}
