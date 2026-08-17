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

/*
 * What the mini-VDD's cached BIOS answers said. Written by the API callers in
 * runtime.asm, read here.
 *
 * These live in C rather than in the assembly so that the assembly declares
 * them EXTRN and the linker checks the pair agree, which is the same
 * arrangement as the stage code.
 */
DWORD v9x_minivdd_base = 0ul;
WORD v9x_minivdd_bytes = 0u;
WORD v9x_minivdd_attr = 0u;
WORD v9x_minivdd_width = 0u;
WORD v9x_minivdd_height = 0u;
WORD v9x_minivdd_bpp = 0u;
WORD v9x_minivdd_model = 0u;
WORD v9x_minivdd_version = 0u;
WORD v9x_minivdd_total64k = 0u;
/* Diagnostic only: what the mini-VDD's init-time collection achieved. */
WORD v9x_minivdd_bufseg = 0u;
WORD v9x_minivdd_modes = 0u;
WORD v9x_minivdd_ctrl = 0u;
/* What the card was scanning at before tier-0 corrected it, for the record. */
WORD v9x_vbe_pitch_before = 0u;

/* runtime.asm: the mini-VDD API, which needs 32-bit registers. */
extern WORD FAR PASCAL V9xMiniVbeModeInfo(WORD mode);
extern WORD FAR PASCAL V9xMiniVbeController(void);
extern WORD FAR PASCAL V9xMiniVbeStatus(void);

WORD FAR PASCAL V9xHardwareStage(void)
{
    return v9x_hardware_stage_code;
}

/*
 * Non-zero when this family will work with whatever the PCI scan found.
 *
 * The scan always runs. It is what records which entry matched, and so which
 * chip's hooks the rest of the sequence calls; only whether a miss is fatal
 * depends on the family. A family whose code reads chip registers or a PCI BAR
 * needs the card to be one it names. A tier-0 family does not: it pokes
 * nothing, takes its aperture from 4F01h and its size from 4F00h, so the id
 * told it nothing it uses.
 *
 * This exists as one function because three call sites ask the same question -
 * the Enable entry point and ValidateMode in ddi.c, and the staged sequence
 * below - and they have to agree. They did not: fixing only the staged
 * sequence left Enable still refusing at ddi.c's own check, and left
 * ValidateMode rejecting every mode, which is how GDI ends up told that a
 * driver loaded fine and then refused everything it offered. Measured on an
 * ATI Mach64 VT2; see docs\issues\2026-08-16-tier0-defects-deferred.md D3.
 */
WORD v9x_hardware_acceptable(void)
{
    if (V9xHardwarePresent() != 0u) {
        return 1u;
    }
    return v9x_hw16.pci_match_optional != 0u ? 1u : 0u;
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
/*
 * The tier-0 sub-reason, beside the coarse stage code.
 *
 * v9x_trace_hardware_failure writes "fail-hardware-aperture" for anything that
 * refuses here, which is the right granularity for the stage contract the
 * boot-trace tooling matches on but the wrong granularity to act on: a BIOS
 * that reports a stride we cannot use is a different problem from a BIOS call
 * that never ran. Separate key, so neither overwrites the other.
 */
static void v9x_write_ini_key(const char FAR *key, const char FAR *value)
{
    WritePrivateProfileString("Velocity9x", key, value, "C:\\V9XBOOT.INI");
}

static void v9x_vbe_trace(const char FAR *detail)
{
    v9x_write_ini_key("VbeDetail", detail);
}

/* Decimal, appending at "at" and returning the new position. */
static WORD v9x_append_decimal(char *text, WORD at, WORD value)
{
    char digits[6];
    WORD count = 0u;

    do {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u && count < 6u);
    while (count != 0u) {
        text[at++] = digits[--count];
    }
    return at;
}

/*
 * A real-mode addressable buffer for the buffered VBE calls, as
 * (selector << 16) | real-mode segment, or 0.
 *
 * UNRESOLVED - and deliberately left refusing rather than guessing again.
 * Both mechanisms tried so far are measured failures on an 86Box Mach64 VT2:
 *
 *   - DPMI 0100h returns failure under Windows' DPMI host, which is what
 *     Microsoft's guidance implies when it tells applications to use
 *     GlobalDosAlloc instead. Trace: VbeDetail=4f01-no-dos-buffer.
 *   - GlobalDosAlloc from this path took the guest down with a fatal
 *     exception 0D before Enable reached its own trace point, so the driver
 *     did not merely fail, it faulted.
 *
 * DPMI 0100h is what is wired up, because failing cleanly is worth more than
 * crashing: tier-0 refuses at stage 3, Windows falls back to VGA, and the
 * boot trace says why. The tier is inert on cards needing 4F01h until this is
 * solved - most likely by doing the call from the mini-VDD at ring 0, where
 * neither the DPMI host nor the global heap is in the way.
 *
 * See docs\issues\2026-08-16-tier0-defects-deferred.md D4.
 */

/*
 * Record what the mini-VDD's collection achieved, next to the refusal reason.
 *
 * An empty cache has two very different causes - it never got a V86 buffer, or
 * it got one and the BIOS refused every call - and the difference decides
 * whether the bug is in the allocation or in the nested-execution call. Writing
 * the three numbers out is how the guest answers that instead of the host
 * guessing, which this defect has already cost twice.
 */
static void v9x_vbe_trace_cache(void)
{
    char text[24];
    WORD at = 0u;

    if (V9xMiniVbeStatus() == 0u) {
        v9x_write_ini_key("VbeCache", "no-api");
        return;
    }
    /* "seg=NNNNN modes=N ctrl=N", decimal, built by hand: no sprintf here. */
    text[at++] = 's'; text[at++] = '=';
    at = v9x_append_decimal(text, at, v9x_minivdd_bufseg);
    text[at++] = ' '; text[at++] = 'm'; text[at++] = '=';
    at = v9x_append_decimal(text, at, v9x_minivdd_modes);
    text[at++] = ' '; text[at++] = 'c'; text[at++] = '=';
    at = v9x_append_decimal(text, at, v9x_minivdd_ctrl);
    text[at] = '\0';
    v9x_write_ini_key("VbeCache", text);
}

/*
 * Stage 9 for a family with no post_mode_set hook: make the card scan the
 * surface out at the stride we are going to draw with.
 *
 * A mode set does not settle this. The BIOS may accept 4F02h and leave the CRTC
 * scanning at a stride of its own choosing, and 4F01h will not say so - it
 * reports what the mode is defined as, not what the hardware was left
 * programmed with. Draw at one stride while the card scans at another and the
 * picture is shredded while every check inside the driver still agrees with
 * itself: GDI writes and reads through the same wrong number, so a framebuffer
 * grab looks perfect and only the monitor disagrees. That is exactly how a
 * six-mode matrix passed on a Mach64 while the screen was garbage.
 *
 * So ask, and if it disagrees, set it and ask again. Refusing on a stride that
 * cannot be made to match is the same bargain as the rest of tier-0: a legible
 * stage 9 failure beats a display that looks broken with nothing recorded.
 *
 * The Millennium II family has forced this through its own hook since long
 * before tier-0 existed. This is the chip-agnostic version of the same lesson.
 */
static WORD v9x_vbe_default_pitch(void)
{
    WORD width;
    WORD height;
    WORD bpp;
    WORD pitch;

    if (v9x_selected_mode_geometry(&width, &height, &bpp, &pitch) == 0u) {
        v9x_vbe_trace("pitch-no-mode");
        return 0u;
    }
    if (v9x_vbe_get_scan_line() == 0u) {
        /* No 4F06h at all. Nothing can be verified, so nothing is claimed:
         * carry on and let the aperture step decide. Older BIOSes that lack it
         * generally have not repurposed the stride either. */
        v9x_vbe_trace("pitch-no-4f06");
        return 1u;
    }
    if (v9x_vbe_scan_bytes == pitch) {
        return 1u;
    }

    /* It disagrees. Ask for the geometry width in pixels, which is what the
     * family's packed pitch was computed from. */
    v9x_vbe_pitch_before = v9x_vbe_scan_bytes;
    if (v9x_vbe_set_scan_line_pixels(width) == 0u) {
        v9x_vbe_trace("pitch-set-refused");
        return 0u;
    }
    if (v9x_vbe_scan_bytes != pitch) {
        v9x_vbe_trace("pitch-unsettable");
        return 0u;
    }
    v9x_vbe_trace("pitch-corrected");
    return 1u;
}

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
        v9x_vbe_trace("no-mode-selected");
        return 0ul;
    }
    /*
     * The bare mode number: 4F01h describes a mode, not a mode plus the
     * family's linear and no-clear request bits.
     *
     * The answer comes from the mini-VDD, which collected it at ring 0 during
     * its own init. Ring 3 cannot do this call: the DPMI host will not allocate
     * a DOS block for the buffer, and given a buffer that works the simulated
     * interrupt faults. See D4 in the deferred-defects issue.
     */
    {
        WORD answered = V9xMiniVbeModeInfo(v9x_active_vbe_mode);
        if (answered != 1u) {
            /* Two very different faults. "no-api" means the VxD is absent, or
             * present but not ours, so look at the mini-VDD's load and its id.
             * "no-mode" means it is ours and answered, but its init-time BIOS
             * query produced nothing for this mode, so look at the collection. */
            v9x_vbe_trace(answered == 2u ? "minivdd-no-mode"
                                         : "minivdd-no-api");
            v9x_vbe_trace_cache();
            return 0ul;
        }
    }
    mode.attributes = v9x_minivdd_attr;
    mode.bytes_per_scan_line = v9x_minivdd_bytes;
    /* The mini-VDD reports the 2.0 stride only. A VBE 3.0 linear stride would
     * need its own cache slot; until one exists, say "not reported" rather
     * than pass this one off as it. */
    mode.lin_bytes_per_scan_line = 0u;
    mode.width = v9x_minivdd_width;
    mode.height = v9x_minivdd_height;
    mode.bits_per_pixel = v9x_minivdd_bpp;
    mode.memory_model = v9x_minivdd_model;
    mode.phys_base = v9x_minivdd_base;

    /* Same judgement the block parser applies, on the same rule. */
    if (v9x_vbe_mode_summary_is_drivable(&mode) == 0u) {
        v9x_vbe_trace("4f01-mode-rejected");
        return 0ul;
    }
    if (v9x_vbe_mode_matches(&mode, width, height, bpp, pitch) == 0u) {
        v9x_vbe_trace("stride-disagrees");
        return 0ul;
    }

    /*
     * VRAM only sizes the off-screen heap, and dd16.c has a floor to fall back
     * on, so a BIOS with a broken 4F00h costs some off-screen surfaces rather
     * than the whole enable. Clamped to what the family actually maps: the
     * DirectDraw heap runs to linear_base + vram_bytes - 1, and that has to
     * stay inside the mapping however much memory the card claims.
     */
    if (V9xMiniVbeController() != 0u) {
        DWORD visible_bytes;

        controller.version = v9x_minivdd_version;
        controller.total_memory_bytes = (DWORD)v9x_minivdd_total64k * 65536ul;

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

    v9x_vbe_trace("ok");
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
    if (v9x_hardware_acceptable() == 0u) {
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
    } else if (v9x_hw16.read_aperture == 0) {
        /* NULL hook, tier-0: use the chip-agnostic default, same rule as the
         * aperture below. */
        v9x_hardware_stage_code = 9u;
        if (v9x_vbe_default_pitch() == 0u) {
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
