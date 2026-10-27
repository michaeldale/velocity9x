/*
 * The PAT write-combining decision. Pure policy: see include\velocity9x\pat.h
 * for why none of it lives in the mini-VDD.
 */
#include "velocity9x/pat.h"

v9x_u16 v9x_pat_plan_wc(v9x_u16 mtrr_reason, v9x_u16 cpu_flags,
                        v9x_u32 pat_high, v9x_u32 linear, v9x_u32 bytes,
                        struct v9x_pat_plan *plan)
{
    v9x_u32 entry;

    if (plan == 0) {
        return V9X_FALSE;
    }
    plan->reason = V9X_PAT_MTRR_ROUTE;
    plan->msr_write = 0u;
    plan->old_high = 0ul;
    plan->new_high = 0ul;

    if (mtrr_reason != V9X_MTRR_ALREADY_COVERED &&
        mtrr_reason != V9X_MTRR_NO_SLOT) {
        return V9X_FALSE;
    }

    /* The capability ladder, in the order that makes each step legal. */
    if ((cpu_flags & V9X_PAT_CPU_CPUID) == 0u) {
        plan->reason = V9X_PAT_NO_CPUID;
        return V9X_FALSE;
    }
    if ((cpu_flags & V9X_PAT_CPU_MSR) == 0u) {
        plan->reason = V9X_PAT_NO_MSR;
        return V9X_FALSE;
    }
    if ((cpu_flags & V9X_PAT_CPU_PAT) == 0u) {
        plan->reason = V9X_PAT_NO_PAT;
        return V9X_FALSE;
    }

    /*
     * The range: whole pages, in the shared arena, not running past the top
     * of the address space. A wrapped end is refused rather than saturated:
     * unlike the MTRR window, every page here is edited one by one, and a
     * range that wraps names pages this driver never mapped.
     */
    if (bytes == 0ul || (linear % V9X_PAT_PAGE_BYTES) != 0ul ||
        (bytes % V9X_PAT_PAGE_BYTES) != 0ul ||
        linear < V9X_PAT_LINEAR_FLOOR ||
        linear + bytes < linear) {
        plan->reason = V9X_PAT_BAD_RANGE;
        return V9X_FALSE;
    }

    /* Entry 4 at its power-on WB (nobody uses it) or already WC (ours,
     * kept across a Windows restart without a CPU reset). */
    entry = pat_high & V9X_PAT_ENTRY4_MASK;
    if (entry != V9X_PAT_TYPE_WB && entry != V9X_PAT_TYPE_WC) {
        plan->reason = V9X_PAT_ENTRY_IN_USE;
        return V9X_FALSE;
    }

    plan->reason = V9X_PAT_OK;
    plan->old_high = pat_high;
    plan->new_high = (pat_high & ~V9X_PAT_ENTRY4_MASK) | V9X_PAT_TYPE_WC;
    plan->msr_write = (v9x_u16)(entry != V9X_PAT_TYPE_WC
                                    ? V9X_TRUE : V9X_FALSE);
    return V9X_TRUE;
}

#if !defined(_M_I86)
v9x_u32 v9x_pat_pte_wc(v9x_u32 pte)
{
    return (pte | V9X_PAT_PTE_PAT) & ~(V9X_PAT_PTE_PCD | V9X_PAT_PTE_PWT);
}
#endif
