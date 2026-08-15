/*
 * The staged hardware enable sequence.
 *
 * Lifted out of V9XHARDWAREENABLE in runtime.asm. The order it runs in encodes
 * two hard-won fixes and must not be rearranged:
 *
 *   VDD pre-mode -> PCI find -> 4F02h -> pitch -> aperture -> chip pokes ->
 *   DPMI map -> VDD register
 *
 * Three things are preserved verbatim from the assembly:
 *
 *   - Stage code numbering. v9x_trace_hardware_failure maps these to the
 *     fail-hardware-* markers the boot-trace tooling matches on, so the codes
 *     are a published contract rather than an implementation detail. Code 1 is
 *     kept even though it is unreachable - V9xHardwarePresent short-circuits
 *     first and both paths call the same PCI find - so the numbering does not
 *     shift under the rest.
 *   - Selector reuse across Enable cycles: if a selector is already live and
 *     the physical base has not moved, the same selector is returned.
 *   - Disable never frees the selector. The DIB Engine caches it inside the
 *     PDEVICE it builds and does not reacquire it on a later Enable, so a
 *     changed value leaves the engine writing through a descriptor that has
 *     been returned to the LDT. See
 *     docs/issues/2026-08-14-hellbender-dibeng-gpf.md.
 *
 * The DPMI selector and mapping stages stay in one chip-agnostic assembly
 * helper: they need 32-bit registers, and this file is compiled for 8086.
 */
#include <windows.h>

#include "velocity9x/hw16.h"
#include "velocity9x/vbe16.h"

extern WORD v9x_active_vbe_mode;
extern WORD v9x_vbe_mode_flags;

/* Chip-agnostic primitives that remain in runtime.asm. */
extern WORD FAR PASCAL V9xHardwarePresent(void);
extern WORD FAR PASCAL V9xMapAperture(void);

/*
 * Shared with the assembly helper.
 *
 * The stage code is written from both sides - here for stages 1 to 3, 8 and 9,
 * and by the mapping helper for 4 to 7 - so the whole numbered sequence stays
 * one variable rather than two that could drift.
 */
WORD v9x_hardware_stage_code = 0u;
DWORD v9x_map_physical_base = 0ul;

WORD FAR PASCAL V9xHardwareStage(void)
{
    return v9x_hardware_stage_code;
}

/*
 * Set the family's mode and hand back a mapped framebuffer selector, or 0.
 *
 * Every failure leaves v9x_hardware_stage_code at the stage that refused,
 * which is what the boot trace reports.
 */
WORD FAR PASCAL V9xHardwareEnable(void)
{
    DWORD base;

    v9x_hardware_stage_code = 1u;
    if (V9xHardwarePresent() == 0u) {
        return 0u;
    }

    v9x_hardware_stage_code = 2u;
    if (v9x_vbe_set_mode(v9x_active_vbe_mode, v9x_vbe_mode_flags) == 0u) {
        return 0u;
    }

    if (v9x_hw16.post_mode_set != 0) {
        v9x_hardware_stage_code = 9u;
        if (v9x_hw16.post_mode_set() == 0u) {
            return 0u;
        }
    }

    v9x_hardware_stage_code = 3u;
    /* A family with no read_aperture hook has nothing to map until VBE 4F01h
     * lands with the tier-0 backend at phase 9. */
    base = v9x_hw16.read_aperture != 0 ? v9x_hw16.read_aperture() : 0ul;
    if (base == 0ul) {
        return 0u;
    }

    if (v9x_hw16.enable_aperture != 0) {
        v9x_hardware_stage_code = 8u;
        if (v9x_hw16.enable_aperture() == 0u) {
            return 0u;
        }
    }

    /* Stages 4 to 7, the reuse decision and the unwind on failure belong to
     * the mapping helper; it advances the stage code itself. */
    v9x_hardware_stage_code = 4u;
    v9x_map_physical_base = base;
    return V9xMapAperture();
}

/*
 * Re-establish the mode after a full-screen DOS box, without touching the
 * selector or the mapping.
 *
 * Returns the result of the last step that ran, matching the assembly this
 * replaced: a failed mode set stops there, and otherwise the family's own
 * follow-up decides.
 */
WORD FAR PASCAL V9xHardwareReset(void)
{
    if (v9x_vbe_set_mode(v9x_active_vbe_mode, v9x_vbe_mode_flags) == 0u) {
        return 0u;
    }
    if (v9x_hw16.post_mode_set != 0) {
        return v9x_hw16.post_mode_set();
    }
    if (v9x_hw16.enable_aperture != 0) {
        return v9x_hw16.enable_aperture();
    }
    return 1u;
}
