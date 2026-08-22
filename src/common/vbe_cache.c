/*
 * What a consumer may believe about the mini-VDD's VBE cache.
 *
 * The mini-VDD reports counts and flags; this file decides what they permit.
 * It is separate from vbe_parse.c because the subject is different: that file
 * judges one BIOS answer, this one judges the collection as a whole - how many
 * records may be read, and whether the scan is sound enough to contradict a
 * mode the family shipped a table row for.
 *
 * Both questions are here rather than at the call sites because both are
 * re-validations of numbers that crossed an ABI (invariant 4 of
 * docs\plans\dynamic-vbe-pipeline.md), and a re-validation that lives in the
 * consumer gets skipped by the next consumer.
 *
 * No BIOS, no allocation, no OS.
 */
#include "velocity9x/vbe_cache.h"

v9x_u16 v9x_vbe_scan_usable_count(v9x_u16 status, v9x_u16 cached_count)
{
    /*
     * A collection that never ran, a controller block that was not credible,
     * or a mode list the walk could not trust contributes nothing - whatever
     * count came with it. The records may individually look fine; the point is
     * that their provenance does not.
     */
    if ((status & V9X_VBE_ST_COLLECT_OFF) != 0u) {
        return 0u;
    }
    if ((status & V9X_VBE_ST_CTRL_VALID) == 0u) {
        return 0u;
    }
    if ((status & V9X_VBE_ST_LIST_VALID) == 0u) {
        return 0u;
    }
    /*
     * And a count is clamped rather than trusted. The mini-VDD bounds its own
     * cache, so a figure above the bound means the two halves disagree about
     * the contract - which is a reason to read fewer records, never more.
     */
    if (cached_count > V9X_VBE_CACHE_MAX) {
        return V9X_VBE_CACHE_MAX;
    }
    return cached_count;
}

v9x_u16 v9x_vbe_scan_may_contradict(v9x_u16 status)
{
    /*
     * Hiding a family baseline row asks more of the scan than admitting a new
     * mode does: a wrong admission offers a mode that fails to set, but a wrong
     * hide takes away a mode that works, and on a machine whose baseline is all
     * it has that is the difference between a usable desktop and none.
     *
     * So the scan must have been complete as well as valid. A truncated list, a
     * full cache or a refused query all mean "there may be modes I did not see",
     * and a row this scan does not corroborate might be one of them.
     */
    if (v9x_vbe_scan_usable_count(status, V9X_VBE_CACHE_MAX) == 0u) {
        return V9X_FALSE;
    }
    if ((status & V9X_VBE_ST_LIST_TERM) == 0u) {
        return V9X_FALSE;
    }
    if ((status & (V9X_VBE_ST_LIST_OVERFLOW | V9X_VBE_ST_LIST_UNREACHED |
                   V9X_VBE_ST_LIST_FLAGGED | V9X_VBE_ST_CACHE_FULL |
                   V9X_VBE_ST_QUERY_FAILED)) != 0u) {
        return V9X_FALSE;
    }
    return V9X_TRUE;
}
