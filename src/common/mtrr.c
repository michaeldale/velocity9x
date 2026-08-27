/*
 * The write-combining decision. Pure arithmetic and pure policy: see
 * include\velocity9x\mtrr.h for why none of it lives in the mini-VDD.
 */
#include "velocity9x/mtrr.h"

/* The address bits of a PHYSBASE/PHYSMASK value; the low 12 carry the memory
 * type and the valid bit. */
#define V9X_MTRR_ADDR_MASK ((v9x_u32)0xFFFFF000ul)

v9x_u32 v9x_mtrr_window(v9x_u32 base, v9x_u32 bytes)
{
    v9x_u32 window;

    if (base == 0ul || bytes == 0ul) {
        return 0ul;
    }
    /*
     * Walk down from the largest block that fits until one also divides the
     * base. Descending rather than ascending because the answer wanted is the
     * largest such block, and because starting at 0x80000000 bounds the loop
     * at 32 iterations however absurd the inputs are.
     */
    window = (v9x_u32)0x80000000ul;
    while (window != 0ul) {
        if (window <= bytes && (base % window) == 0ul) {
            return window;
        }
        window >>= 1;
    }
    return 0ul;
}

v9x_u32 v9x_mtrr_physmask(v9x_u32 size)
{
    /* A usable size is a power of two of at least one page. */
    if (size < (v9x_u32)0x1000ul || (size & (size - 1ul)) != 0ul) {
        return 0ul;
    }
    return (((v9x_u32)0ul - size) & V9X_MTRR_ADDR_MASK) |
           V9X_MTRR_PHYSMASK_VALID;
}

/*
 * Does variable range r cover any of [target, target + size)?
 *
 * A range whose PHYSBASE sits above 4 GiB is skipped rather than refused: it
 * cannot reach anything a 32-bit driver maps. Everything else is compared as
 * a half-open interval, with the two ways a range can run past the top of the
 * address space - a zero low mask, or a base plus size that wraps - both
 * treated as "extends to 4 GiB", which is what they mean.
 */
static v9x_u16 v9x_mtrr_range_overlaps(const struct v9x_mtrr_state *state,
                                       v9x_u16 index,
                                       v9x_u32 target,
                                       v9x_u32 size)
{
    v9x_u32 range_base;
    v9x_u32 range_mask;
    v9x_u32 range_size;
    v9x_u32 range_end;
    v9x_u32 target_end;

    if ((state->mask[index] & V9X_MTRR_PHYSMASK_VALID) == 0ul) {
        return V9X_FALSE;
    }
    if ((state->high_bits & (v9x_u16)(1u << index)) != 0u) {
        return V9X_FALSE;
    }

    range_base = state->base[index] & V9X_MTRR_ADDR_MASK;
    range_mask = state->mask[index] & V9X_MTRR_ADDR_MASK;
    target_end = target + size;
    if (target_end < target) {
        /* The window runs to the top of the address space. Saturating rather
         * than wrapping matters: a wrapped end compares below every range
         * base and would report the window as clashing with nothing at all. */
        target_end = (v9x_u32)0xFFFFFFFFul;
    }

    if (range_mask == 0ul) {
        /* At least 4 GiB wide, so it covers the target if it starts at or
         * below it - and a range that wide always starts at zero. */
        return (v9x_u16)(range_base < target_end ? V9X_TRUE : V9X_FALSE);
    }

    range_size = ((v9x_u32)0ul - range_mask);
    range_end = range_base + range_size;
    if (range_end < range_base) {
        /* Wrapped: treat the range as running to the top of the space. */
        return (v9x_u16)(range_base < target_end ? V9X_TRUE : V9X_FALSE);
    }
    if (range_base < target_end && target < range_end) {
        return V9X_TRUE;
    }
    return V9X_FALSE;
}

v9x_u16 v9x_mtrr_plan_wc(const struct v9x_mtrr_state *state,
                         v9x_u32 aperture_base,
                         v9x_u32 aperture_bytes,
                         struct v9x_mtrr_plan *plan)
{
    v9x_u16 count;
    v9x_u16 index;
    v9x_u32 window;

    if (plan == 0) {
        return V9X_FALSE;
    }
    plan->reason = V9X_MTRR_NO_CPUID;
    plan->slot = 0u;
    plan->base = 0ul;
    plan->size = 0ul;
    if (state == 0) {
        return V9X_FALSE;
    }

    /* The capability ladder, in the order that makes each step legal. */
    if ((state->cpu_flags & V9X_MTRR_CPU_CPUID) == 0u) {
        plan->reason = V9X_MTRR_NO_CPUID;
        return V9X_FALSE;
    }
    if ((state->cpu_flags & V9X_MTRR_CPU_MSR) == 0u) {
        plan->reason = V9X_MTRR_NO_MSR;
        return V9X_FALSE;
    }
    if ((state->cpu_flags & V9X_MTRR_CPU_MTRR) == 0u) {
        plan->reason = V9X_MTRR_NO_MTRR;
        return V9X_FALSE;
    }
    if ((state->cap & V9X_MTRR_CAP_WC) == 0ul) {
        plan->reason = V9X_MTRR_NO_WC;
        return V9X_FALSE;
    }

    count = (v9x_u16)(state->cap & V9X_MTRR_CAP_VCNT_MASK);
    if (count == 0u) {
        plan->reason = V9X_MTRR_NO_VCNT;
        return V9X_FALSE;
    }
    /* A CPU reporting more pairs than ring 0 read is reasoned about only as
     * far as it was read. That direction is safe: an unread pair can only
     * make this code decline, never make it write somewhere it should not. */
    if (count > state->range_count) {
        count = state->range_count;
    }
    if (count > V9X_MTRR_RANGE_MAX) {
        count = V9X_MTRR_RANGE_MAX;
    }
    if (count == 0u) {
        plan->reason = V9X_MTRR_NO_VCNT;
        return V9X_FALSE;
    }

    /*
     * The two facts that make an uncovered aperture both worth changing and
     * safe to change. With MTRRs off, nothing they say is in effect and
     * enabling them would re-type the whole address space; with a default
     * type other than UC, an address no range covers is already cached and
     * the BIOS is describing memory in a way this rule does not model.
     */
    if ((state->def_type & V9X_MTRR_DEF_ENABLE) == 0ul) {
        plan->reason = V9X_MTRR_DISABLED;
        return V9X_FALSE;
    }
    if ((state->def_type & V9X_MTRR_DEF_TYPE_MASK) != V9X_MTRR_TYPE_UC) {
        plan->reason = V9X_MTRR_DEFAULT_NOT_UC;
        return V9X_FALSE;
    }

    if (aperture_base == 0ul || aperture_bytes == 0ul) {
        plan->reason = V9X_MTRR_NO_APERTURE;
        return V9X_FALSE;
    }
    if (aperture_base < V9X_MTRR_APERTURE_FLOOR) {
        plan->reason = V9X_MTRR_APERTURE_LOW;
        return V9X_FALSE;
    }

    window = v9x_mtrr_window(aperture_base, aperture_bytes);
    if (window < V9X_MTRR_MIN_WINDOW) {
        plan->reason = V9X_MTRR_WINDOW_SMALL;
        return V9X_FALSE;
    }

    for (index = 0u; index < count; ++index) {
        if (v9x_mtrr_range_overlaps(state, index, aperture_base, window)) {
            plan->reason = V9X_MTRR_ALREADY_COVERED;
            return V9X_FALSE;
        }
    }

    for (index = 0u; index < count; ++index) {
        if ((state->mask[index] & V9X_MTRR_PHYSMASK_VALID) == 0ul) {
            plan->reason = V9X_MTRR_OK;
            plan->slot = index;
            plan->base = aperture_base;
            plan->size = window;
            return V9X_TRUE;
        }
    }

    plan->reason = V9X_MTRR_NO_SLOT;
    return V9X_FALSE;
}
