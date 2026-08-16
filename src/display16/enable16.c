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
extern WORD v9x_map_pages_hi;
extern WORD v9x_map_pages_lo;

/* ddi.c. The mode row being enabled, valid before the PDEVICE exists. */
extern WORD v9x_selected_mode_geometry(WORD FAR *width, WORD FAR *height,
                                       WORD FAR *bpp, WORD FAR *pitch);

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

/*
 * VRAM as the BIOS reported it, or 0 when nothing asked.
 *
 * Only the tier-0 path fills this in: a family with a read_aperture hook knows
 * its own memory size and never calls 4F00h, so its images leave this zero and
 * dd16.c keeps the size it always used.
 */
DWORD v9x_vbe_vram_bytes = 0ul;

/*
 * The raw 4F00h answer, before the floor below is applied.
 *
 * publish_diagnostics reports this rather than the usable figure on purpose.
 * "This BIOS claims 512 KiB" is exactly the fact a bug report from an untested
 * card needs, and a corrected number would hide it. The Rage Mobility really
 * does answer 512 KiB from a Windows DOS box, and 4 MiB from real DOS.
 */
DWORD v9x_vbe_vram_reported = 0ul;

WORD FAR PASCAL V9xHardwareStage(void)
{
    return v9x_hardware_stage_code;
}

/*
 * Stage 3 for a family with no read_aperture hook: ask the BIOS.
 *
 * This is the tier-0 backend. Nothing here is chip-specific, which is the
 * point - a family whose hooks are all NULL reaches this and gets a working
 * linear framebuffer out of VBE alone.
 *
 * The mode agreement check is a refusal, not an adaptation. GDI and the
 * registry have already agreed on the pitch in the family's table, and the
 * PDEVICE is built from it, so a BIOS reporting a different stride would put
 * every scan line in the wrong place. Refusing at stage 3 leaves a legible
 * boot trace instead; adapting would leave a display that looks broken with
 * no failure recorded anywhere.
 */
static DWORD v9x_vbe_default_aperture(void)
{
    struct v9x_vbe_mode_summary mode;
    struct v9x_vbe_controller_summary controller;
    WORD width;
    WORD height;
    WORD bpp;
    WORD pitch;
    DWORD mapped_bytes;

    if (v9x_selected_mode_geometry(&width, &height, &bpp, &pitch) == 0u) {
        return 0ul;
    }
    /* The bare mode number: 4F01h describes a mode, not a mode plus the
     * family's linear and no-clear request bits. */
    if (v9x_vbe_read_mode_info(v9x_active_vbe_mode, &mode) == 0u) {
        return 0ul;
    }
    if (v9x_vbe_mode_matches(&mode, width, height, bpp, pitch) == 0u) {
        return 0ul;
    }

    /*
     * VRAM only sizes the off-screen heap, and dd16.c has a floor to fall back
     * on, so a BIOS with a broken 4F00h costs some off-screen surfaces rather
     * than the whole enable. Clamped to what the family actually maps: the
     * DirectDraw heap runs to linear_base + vram_bytes - 1, and that has to
     * stay inside the mapping however much memory the card claims.
     */
    if (v9x_vbe_read_controller_info(&controller) != 0u) {
        DWORD visible_bytes;

        mapped_bytes = (((DWORD)v9x_map_pages_hi << 16) |
                        (DWORD)v9x_map_pages_lo) + 1ul;
        v9x_vbe_vram_reported = controller.total_memory_bytes;
        v9x_vbe_vram_bytes = controller.total_memory_bytes > mapped_bytes
                                 ? mapped_bytes
                                 : controller.total_memory_bytes;

        /*
         * Floor the usable size at what the mode being set actually displays.
         *
         * A BIOS that reports less memory than the mode it just accepted is
         * lying, and believing it corrupts arithmetic downstream rather than
         * merely losing off-screen surfaces: dd16.c computes the heap as
         * fpStart = base + visible_bytes and fpEnd = base + vram_bytes - 1, so
         * an under-report puts fpEnd *before* fpStart, and dwVidMemTotal =
         * vram_bytes - visible_bytes underflows the DWORD to about 4.29 GB -
         * a heap the size of the address space, starting past the end of the
         * framebuffer. ddhal_core.c then bounds-checks every blit against the
         * same wrong number.
         *
         * Flooring at the visible bytes rather than at dd16.c's 4 MiB fallback
         * is the conservative choice: on an unknown card, believing in
         * off-screen memory that has not been proven hands DirectDraw surfaces
         * aliasing the visible framebuffer. Tier-0 draws with the CPU and
         * barely uses the off-screen heap, so an empty heap costs almost
         * nothing, whereas a ceiling that is too high costs corruption.
         *
         * Rounded up to the next 64 KiB by masking rather than complementing,
         * because ~ on a 16-bit int would not widen the way this needs.
         */
        visible_bytes = (DWORD)pitch * (DWORD)height;
        if (v9x_vbe_vram_bytes < visible_bytes) {
            v9x_vbe_vram_bytes = (visible_bytes + 0xfffful) & 0xffff0000ul;
        }
    }

    return mode.phys_base;
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
    const V9X_HW16_DEVICE *device;

    v9x_hardware_stage_code = 1u;
    if (V9xHardwarePresent() == 0u) {
        return 0u;
    }
    /* Read after the present check, not before: that is the call that runs the
     * PCI scan and so decides which chip's hooks the rest of this sequence
     * uses. */
    device = v9x_hw16_active_device();

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
    /* NULL means "ask the BIOS", which is the whole of the tier-0 backend.
     * Either way a zero base is the same stage 3 refusal. */
    base = v9x_hw16.read_aperture != 0 ? v9x_hw16.read_aperture()
                                       : v9x_vbe_default_aperture();
    if (base == 0ul) {
        return 0u;
    }

    if (device != 0 && device->enable_aperture != 0) {
        v9x_hardware_stage_code = 8u;
        if (device->enable_aperture() == 0u) {
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
    const V9X_HW16_DEVICE *device = v9x_hw16_active_device();

    if (v9x_vbe_set_mode(v9x_active_vbe_mode, v9x_vbe_mode_flags) == 0u) {
        return 0u;
    }
    if (v9x_hw16.post_mode_set != 0) {
        return v9x_hw16.post_mode_set();
    }
    if (device != 0 && device->enable_aperture != 0) {
        return device->enable_aperture();
    }
    return 1u;
}
