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

#include "velocity9x/diagpaths.h"
#include "velocity9x/hw16.h"
#include "velocity9x/vbe16.h"
#include "velocity9x/vbe_cache.h"
#include "velocity9x/mtrr.h"

extern WORD v9x_active_vbe_mode;
extern WORD v9x_vbe_mode_flags;
extern WORD v9x_map_pages_hi;
extern WORD v9x_map_pages_lo;

/* ddi.c. The mode row being enabled, valid before the PDEVICE exists. */
extern WORD v9x_selected_mode_geometry(WORD FAR *width, WORD FAR *height,
                                       WORD FAR *bpp, WORD FAR *pitch);

/* Chip-agnostic primitives that remain in runtime.asm. */
extern WORD FAR PASCAL V9xHardwarePresent(void);
/* runtime.asm: INT 1Ah AX=B101h, so a family's register identification can be
 * offered to a machine with no PCI without also offering it a foreign card that
 * happens to be on a PCI bus. */
extern WORD FAR PASCAL V9xPciBiosPresent(void);
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
WORD v9x_minivdd_lin_bytes = 0u;
WORD v9x_minivdd_attr = 0u;
WORD v9x_minivdd_mode_number = 0u;
WORD v9x_minivdd_width = 0u;
WORD v9x_minivdd_height = 0u;
WORD v9x_minivdd_bpp = 0u;
WORD v9x_minivdd_significant = 0u;
WORD v9x_minivdd_model = 0u;
WORD v9x_minivdd_record_flags = 0u;
WORD v9x_minivdd_red = 0u;
WORD v9x_minivdd_green = 0u;
WORD v9x_minivdd_blue = 0u;
WORD v9x_minivdd_rsvd = 0u;
WORD v9x_minivdd_version = 0u;
WORD v9x_minivdd_total64k = 0u;
DWORD v9x_minivdd_capabilities = 0ul;
WORD v9x_minivdd_oem_revision = 0u;
/* Diagnostic only: what the mini-VDD's init-time collection achieved. */
WORD v9x_minivdd_bufseg = 0u;
WORD v9x_minivdd_listed = 0u;
WORD v9x_minivdd_queried = 0u;
WORD v9x_minivdd_cached = 0u;
WORD v9x_minivdd_probed = 0u;
WORD v9x_minivdd_status = 0u;

/*
 * The memory-type registers the mini-VDD read at init, one call's worth at a
 * time. Filled by the runtime.asm helpers below; nothing here writes an MTRR.
 */
WORD v9x_minivdd_mtrr_flags = 0u;
WORD v9x_minivdd_mtrr_count = 0u;
DWORD v9x_minivdd_mtrr_cap = 0ul;
DWORD v9x_minivdd_mtrr_deftype = 0ul;
DWORD v9x_minivdd_mtrr_base = 0ul;
DWORD v9x_minivdd_mtrr_mask = 0ul;
WORD v9x_minivdd_mtrr_high = 0u;
/* One 16-byte EDID chunk, as the four dwords the API hands back. */
DWORD v9x_minivdd_edid0 = 0ul;
DWORD v9x_minivdd_edid1 = 0ul;
DWORD v9x_minivdd_edid2 = 0ul;
DWORD v9x_minivdd_edid3 = 0ul;
/* What the card was scanning at before tier-0 corrected it, for the record. */
WORD v9x_vbe_pitch_before = 0u;

/* runtime.asm: the mini-VDD API, which needs 32-bit registers. */
extern WORD FAR PASCAL V9xMiniVbeModeInfo(WORD mode);
extern WORD FAR PASCAL V9xMiniVbeController(void);
extern WORD FAR PASCAL V9xMiniVbeStatus(void);
extern WORD FAR PASCAL V9xMiniVbeModeAt(WORD index);
extern WORD FAR PASCAL V9xMiniVbeModeMasks(WORD index);
extern WORD FAR PASCAL V9xMiniMtrrInfo(void);
extern WORD FAR PASCAL V9xMiniMtrrRange(WORD index);

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
/*
 * Why the last call refused, or 0 when it accepted.
 *
 * ValidateMode gates every mode on this function's answer and, until this
 * existed, recorded nothing when the answer was no - so a driver that loaded
 * cleanly and then rejected its whole mode list left a boot trace reading
 * `libmain` and no way to tell which half of the check had failed. GDI asks
 * ValidateMode before it ever calls Enable, so the Enable path's
 * fail-hardware-* stages never run in that case and cannot say either.
 *
 *   1  no identify_without_pci hook, so the PCI scan was the only route
 *   2  the hook exists but a PCI BIOS is present, so it was not offered
 *   3  no PCI BIOS, the hook ran and did not recognise the card
 *
 * Numbered rather than named to match v9x_hardware_stage_code above, which the
 * assembly half also writes. Measured on the 486 VLB Trio64; see
 * docs\handoffs\2026-08-21-vlb-first-driver-run.md.
 */
WORD v9x_identify_reason = 0u;

/*
 * What a family's identify_without_pci hook last saw, reported by the trace
 * when reason 3 says the hook ran and declined. Knowing it declined is not
 * enough to fix anything: the s3 hook reads an id the DOS survey had already
 * measured as 88h/11h on the same card, so the interesting question is what it
 * reads instead under Windows.
 *
 * All three are family-defined. The s3 hook fills them with the id it read
 * from CR2D/CR2E, the CRTC index port it read through - a mono 3B4h here would
 * itself be the bug - and the CR38/CR39 lock bytes as it found them, which
 * says whether something re-locked the extended registers after POST.
 *
 * Owned here rather than by the chipset so ddi.c can report them without
 * depending on any one family, the same way ddi.c owns v9x_pci_device and the
 * chipset objects extern-declare it.
 */
WORD v9x_identify_read = 0u;
WORD v9x_identify_locked_read = 0u;
WORD v9x_identify_port = 0u;
WORD v9x_identify_locks = 0u;

WORD v9x_hardware_acceptable(void)
{
    if (V9xHardwarePresent() != 0u) {
        v9x_identify_reason = 0u;
        return 1u;
    }
    /*
     * No PCI match. Two different situations reach here and they want opposite
     * answers.
     *
     * A machine with no PCI BIOS is a VESA Local Bus or ISA one: there was no
     * configuration space for the scan to have matched in, so a family that can
     * identify its own silicon should be allowed to try, and the hook sets
     * v9x_pci_match itself so everything downstream that asks "which chip" gets
     * the same answer the scan would have given.
     *
     * A machine that *has* a PCI BIOS and still did not match is our package
     * bound to somebody else's card. Reading that card's extended registers is
     * the one thing not to do - the hook is a narrow second route to
     * recognising our own hardware, not a licence to probe a stranger's - so
     * the identification is not offered there at all, and the family's
     * pci_match_optional flag decides as it always did.
     */
    if (v9x_hw16.identify_without_pci == 0) {
        v9x_identify_reason = 1u;
    } else if (V9xPciBiosPresent() != 0u) {
        v9x_identify_reason = 2u;
    } else if (v9x_hw16.identify_without_pci() < v9x_hw16.device_count) {
        v9x_identify_reason = 0u;
        return 1u;
    } else {
        v9x_identify_reason = 3u;
    }
    if (v9x_hw16.pci_match_optional != 0u) {
        v9x_identify_reason = 0u;
        return 1u;
    }
    return 0u;
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
/* runtime.asm: create V9X_DIAG_DIR once, before the first diagnostic write.
 * WritePrivateProfileString will not create it and fails silently if it is
 * missing, which would turn every diagnostic below into a no-op. */
extern void FAR PASCAL V9xEnsureDiagDir(void);

static void v9x_write_ini_key(const char FAR *key, const char FAR *value)
{
    V9xEnsureDiagDir();
    WritePrivateProfileString("Velocity9x", key, value, V9X_DIAG_BOOT_INI);
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

static WORD v9x_append_hex16(char *text, WORD at, WORD value)
{
    static const char digits[] = "0123456789abcdef";

    text[at++] = digits[(value >> 12) & 0x0fu];
    text[at++] = digits[(value >> 8) & 0x0fu];
    text[at++] = digits[(value >> 4) & 0x0fu];
    text[at++] = digits[value & 0x0fu];
    return at;
}

static WORD v9x_append_hex32(char *text, WORD at, DWORD value)
{
    at = v9x_append_hex16(text, at, (WORD)(value >> 16));
    return v9x_append_hex16(text, at, (WORD)value);
}

static void v9x_vbe_mode_key(char *key, WORD index)
{
    static const char digits[] = "0123456789abcdef";
    WORD at = 0u;

    key[at++] = 'V'; key[at++] = 'b'; key[at++] = 'e'; key[at++] = 'M';
    key[at++] = 'o'; key[at++] = 'd'; key[at++] = 'e';
    key[at++] = digits[(index >> 4) & 0x0fu];
    key[at++] = digits[index & 0x0fu];
    key[at] = '\0';
}

/* One API-v2 record, kept diagnostic-only during Stage 1. */
static void v9x_vbe_trace_record(WORD index)
{
    static char key[10];
    static char text[128];
    WORD at = 0u;

    if (V9xMiniVbeModeAt(index) == 0u ||
        V9xMiniVbeModeMasks(index) == 0u) {
        return;
    }
    v9x_vbe_mode_key(key, index);
    text[at++] = 'm'; text[at++] = '=';
    at = v9x_append_hex16(text, at, v9x_minivdd_mode_number);
    text[at++] = ' '; text[at++] = 'a'; text[at++] = '=';
    at = v9x_append_hex16(text, at, v9x_minivdd_attr);
    text[at++] = ' '; text[at++] = 'g'; text[at++] = '=';
    at = v9x_append_decimal(text, at, v9x_minivdd_width);
    text[at++] = 'x';
    at = v9x_append_decimal(text, at, v9x_minivdd_height);
    text[at++] = 'x';
    at = v9x_append_decimal(text, at, v9x_minivdd_bpp);
    text[at++] = ' '; text[at++] = 's'; text[at++] = '=';
    at = v9x_append_decimal(text, at, v9x_minivdd_bytes);
    text[at++] = '/';
    at = v9x_append_decimal(text, at, v9x_minivdd_lin_bytes);
    text[at++] = ' '; text[at++] = 'b'; text[at++] = '=';
    at = v9x_append_hex32(text, at, v9x_minivdd_base);
    text[at++] = ' '; text[at++] = 'm'; text[at++] = 'm'; text[at++] = '=';
    at = v9x_append_decimal(text, at, v9x_minivdd_model);
    text[at++] = ' '; text[at++] = 's'; text[at++] = 'i'; text[at++] = 'g';
    text[at++] = '=';
    at = v9x_append_decimal(text, at, v9x_minivdd_significant);
    text[at++] = ' '; text[at++] = 'r'; text[at++] = 'g'; text[at++] = 'b';
    text[at++] = '=';
    at = v9x_append_hex16(text, at, v9x_minivdd_red);
    text[at++] = ',';
    at = v9x_append_hex16(text, at, v9x_minivdd_green);
    text[at++] = ',';
    at = v9x_append_hex16(text, at, v9x_minivdd_blue);
    text[at++] = ',';
    at = v9x_append_hex16(text, at, v9x_minivdd_rsvd);
    text[at++] = ' '; text[at++] = 'f'; text[at++] = '=';
    at = v9x_append_hex16(text, at, v9x_minivdd_record_flags);
    text[at] = '\0';
    v9x_write_ini_key(key, text);
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
    static char text[80];
    static char key[10];
    static WORD traced = 0u;
    WORD at = 0u;
    WORD count;
    WORD index;

    if (traced != 0u) {
        return;
    }
    traced = 1u;

    /* Clear the previous boot's generation even when this boot has no API. */
    V9xEnsureDiagDir();
    WritePrivateProfileString("Velocity9x", "VbeController", 0,
                              V9X_DIAG_BOOT_INI);
    for (index = 0u; index < V9X_VBE_CACHE_MAX; ++index) {
        v9x_vbe_mode_key(key, index);
        WritePrivateProfileString("Velocity9x", key, 0,
                                  V9X_DIAG_BOOT_INI);
    }

    if (V9xMiniVbeStatus() == 0u) {
        v9x_write_ini_key("VbeCache", "no-api");
        return;
    }
    /* Bounded v2 counts plus the exact status bits, built without sprintf. */
    text[at++] = 's'; text[at++] = '=';
    at = v9x_append_decimal(text, at, v9x_minivdd_bufseg);
    text[at++] = ' '; text[at++] = 'l'; text[at++] = '=';
    at = v9x_append_decimal(text, at, v9x_minivdd_listed);
    text[at++] = ' '; text[at++] = 'q'; text[at++] = '=';
    at = v9x_append_decimal(text, at, v9x_minivdd_queried);
    text[at++] = ' '; text[at++] = 'c'; text[at++] = '=';
    at = v9x_append_decimal(text, at, v9x_minivdd_cached);
    text[at++] = ' '; text[at++] = 'p'; text[at++] = '=';
    at = v9x_append_decimal(text, at, v9x_minivdd_probed);
    text[at++] = ' '; text[at++] = 'f'; text[at++] = '=';
    at = v9x_append_hex16(text, at, v9x_minivdd_status);
    text[at] = '\0';
    v9x_write_ini_key("VbeCache", text);

    if (V9xMiniVbeController() != 0u) {
        at = 0u;
        text[at++] = 'v'; text[at++] = '=';
        at = v9x_append_hex16(text, at, v9x_minivdd_version);
        text[at++] = ' '; text[at++] = 'm'; text[at++] = 'e'; text[at++] = 'm';
        text[at++] = '=';
        at = v9x_append_decimal(text, at, v9x_minivdd_total64k);
        text[at++] = ' '; text[at++] = 'c'; text[at++] = 'a'; text[at++] = 'p';
        text[at++] = 's'; text[at++] = '=';
        at = v9x_append_hex32(text, at, v9x_minivdd_capabilities);
        text[at++] = ' '; text[at++] = 'r'; text[at++] = 'e'; text[at++] = 'v';
        text[at++] = '=';
        at = v9x_append_hex16(text, at, v9x_minivdd_oem_revision);
        text[at] = '\0';
        v9x_write_ini_key("VbeController", text);
    }

    if ((v9x_minivdd_status & (V9X_VBE_ST_CTRL_VALID |
                               V9X_VBE_ST_LIST_VALID)) ==
        (V9X_VBE_ST_CTRL_VALID | V9X_VBE_ST_LIST_VALID)) {
        count = v9x_minivdd_cached > V9X_VBE_CACHE_MAX
                    ? V9X_VBE_CACHE_MAX : v9x_minivdd_cached;
    } else {
        count = 0u;
    }
    for (index = 0u; index < count; ++index) {
        v9x_vbe_trace_record(index);
    }
}

/*
 * Record whether the framebuffer aperture could be made write-combining, and
 * what would be written if it were.
 *
 * Nothing is written. This is Stage A of docs\plans\tier0-quality.md: the
 * mini-VDD reads the memory-type registers, the host-tested policy in
 * src\common\mtrr.c decides from them, and the answer goes in the boot INI so
 * that what the rules conclude on every machine this project can reach is
 * known before any of them acts. An MTRR is global CPU state and a wrong range
 * corrupts memory that has nothing to do with this driver, so the evidence
 * comes first.
 *
 * The aperture handed to the policy is the one the driver actually mapped and
 * draws through, not the BIOS's claim about some mode: v9x_map_physical_base
 * is what stage 4 mapped, and v9x_vbe_vram_bytes is what dd16.c hands out.
 */
static void v9x_mtrr_trace(void)
{
    static char text[80];
    static WORD traced = 0u;
    struct v9x_mtrr_state state;
    struct v9x_mtrr_plan plan;
    WORD at = 0u;
    WORD index;

    if (traced != 0u) {
        return;
    }
    traced = 1u;

    V9xEnsureDiagDir();
    if (V9xMiniMtrrInfo() == 0u) {
        v9x_write_ini_key("Mtrr", "no-api");
        return;
    }

    for (index = 0u; index < V9X_MTRR_RANGE_MAX; ++index) {
        state.base[index] = 0ul;
        state.mask[index] = 0ul;
    }
    state.cpu_flags = v9x_minivdd_mtrr_flags;
    state.cap = v9x_minivdd_mtrr_cap;
    state.def_type = v9x_minivdd_mtrr_deftype;
    state.high_bits = 0u;
    state.range_count = v9x_minivdd_mtrr_count > V9X_MTRR_RANGE_MAX
                            ? V9X_MTRR_RANGE_MAX : v9x_minivdd_mtrr_count;
    for (index = 0u; index < state.range_count; ++index) {
        if (V9xMiniMtrrRange(index) == 0u) {
            /* The count and the pairs came from the same read, so a refusal
             * here is a contract disagreement. Believe the smaller number. */
            state.range_count = index;
            break;
        }
        state.base[index] = v9x_minivdd_mtrr_base;
        state.mask[index] = v9x_minivdd_mtrr_mask;
        if (v9x_minivdd_mtrr_high != 0u) {
            state.high_bits |= (WORD)(1u << index);
        }
    }

    (void)v9x_mtrr_plan_wc(&state, v9x_map_physical_base, v9x_vbe_vram_bytes,
                           &plan);

    /* cpu flags, MTRRCAP, DEF_TYPE, pairs read, then the decision. */
    text[at++] = 'c'; text[at++] = 'p'; text[at++] = 'u'; text[at++] = '=';
    at = v9x_append_hex16(text, at, state.cpu_flags);
    text[at++] = ' '; text[at++] = 'c'; text[at++] = 'a'; text[at++] = 'p';
    text[at++] = '=';
    at = v9x_append_hex32(text, at, state.cap);
    text[at++] = ' '; text[at++] = 'd'; text[at++] = 'e'; text[at++] = 'f';
    text[at++] = '=';
    at = v9x_append_hex32(text, at, state.def_type);
    text[at++] = ' '; text[at++] = 'n'; text[at++] = '=';
    at = v9x_append_decimal(text, at, state.range_count);
    text[at++] = ' '; text[at++] = 'r'; text[at++] = '=';
    at = v9x_append_decimal(text, at, plan.reason);
    text[at++] = ' '; text[at++] = 's'; text[at++] = '=';
    at = v9x_append_decimal(text, at, plan.slot);
    text[at++] = ' '; text[at++] = 'b'; text[at++] = '=';
    at = v9x_append_hex32(text, at, plan.base);
    text[at++] = ' '; text[at++] = 'z'; text[at++] = '=';
    at = v9x_append_hex32(text, at, plan.size);
    text[at] = '\0';
    v9x_write_ini_key("Mtrr", text);

    /* The pairs themselves, so a refusal can be re-derived off the machine
     * rather than taken on trust from the reason code above. */
    for (index = 0u; index < state.range_count; ++index) {
        static char key[10];
        WORD keyat = 0u;

        key[keyat++] = 'M'; key[keyat++] = 't'; key[keyat++] = 'r';
        key[keyat++] = 'r';
        keyat = v9x_append_decimal(key, keyat, index);
        key[keyat] = '\0';

        at = 0u;
        at = v9x_append_hex32(text, at, state.base[index]);
        text[at++] = ' ';
        at = v9x_append_hex32(text, at, state.mask[index]);
        if ((state.high_bits & (WORD)(1u << index)) != 0u) {
            text[at++] = ' '; text[at++] = 'h'; text[at++] = 'i';
        }
        text[at] = '\0';
        WritePrivateProfileString("Velocity9x", key, text, V9X_DIAG_BOOT_INI);
    }
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

/*
 * Turn a claimed video-memory size into one the DirectDraw heap can be built
 * on, for whichever source claimed it.
 *
 * Two callers, and they used to be one: the tier-0 4F00h path below, and the
 * native read_video_memory hook in V9xHardwareEnable. Both need the same two
 * corrections applied in the same order, and both corrections exist because
 * the heap arithmetic in dd16.c is unsigned:
 *
 *   fpEnd        = base + vram_bytes - 1
 *   dwVidMemTotal = vram_bytes - visible_bytes
 *
 * Ceiling: clamp to what the family actually maps. The heap has to stay inside
 * the mapping however much memory the card claims to hold.
 *
 * Floor: a source reporting less than the mode it just set actually displays is
 * wrong, and believing it puts fpEnd before fpStart and underflows
 * dwVidMemTotal to about 4.29 GB - a heap the size of the address space,
 * starting past the end of the framebuffer. Flooring at the visible bytes
 * rather than at a fixed size is the conservative choice: off-screen memory
 * that has not been proven would hand DirectDraw surfaces aliasing the visible
 * framebuffer.
 *
 * Rounded up to the next 64 KiB by masking rather than complementing, because
 * ~ on a 16-bit int would not widen the way this needs.
 */
static DWORD v9x_vram_usable_bytes(DWORD claimed_bytes, WORD pitch, WORD height)
{
    DWORD mapped_bytes = (((DWORD)v9x_map_pages_hi << 16) |
                          (DWORD)v9x_map_pages_lo) + 1ul;
    DWORD visible_bytes = (DWORD)pitch * (DWORD)height;
    DWORD usable = claimed_bytes > mapped_bytes ? mapped_bytes : claimed_bytes;

    if (usable < visible_bytes) {
        usable = (visible_bytes + 0xfffful) & 0xffff0000ul;
    }
    return usable;
}

static DWORD v9x_vbe_default_aperture(void)
{
    struct v9x_vbe_mode_summary mode;
    struct v9x_vbe_controller_summary controller;
    WORD width;
    WORD height;
    WORD bpp;
    WORD pitch;

    if (v9x_selected_mode_geometry(&width, &height, &bpp, &pitch) == 0u) {
        v9x_vbe_trace("no-mode-selected");
        return 0ul;
    }
    v9x_vbe_trace_cache();
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
    /* Registers, not a BIOS block, so start from a defined summary: the API
     * reports a subset of the fields and the rest must read as "not reported"
     * rather than as whatever was on the stack. */
    v9x_vbe_mode_summary_clear(&mode);
    mode.attributes = v9x_minivdd_attr;
    mode.bytes_per_scan_line = v9x_minivdd_bytes;
    /* The retained by-mode operation deliberately keeps its v1 register shape:
     * active static-table lookup needs the legacy stride and aperture only.
     * Indexed v2 diagnostics carry the linear stride and channel layout. */
    mode.lin_bytes_per_scan_line = 0u;
    mode.width = v9x_minivdd_width;
    mode.height = v9x_minivdd_height;
    mode.bits_per_pixel = v9x_minivdd_bpp;
    mode.memory_model = v9x_minivdd_model;
    mode.phys_base = v9x_minivdd_base;
    /* Derivable from what the API did report, and only from the depth here:
     * with no channel fields, this is the palettized or 5:6:5 answer. */
    mode.significant_depth = v9x_vbe_summary_significant_depth(&mode);

    /* Same judgement the block parser applies, on the same rule. */
    if (v9x_vbe_mode_summary_is_drivable(&mode) == 0u) {
        v9x_vbe_trace("4f01-mode-rejected");
        return 0ul;
    }
    if (v9x_vbe_mode_matches(&mode, width, height, bpp, pitch) == 0u) {
        v9x_vbe_trace("stride-disagrees");
        return 0ul;
    }

    if (V9xMiniVbeController() != 0u) {
        /* Registers again, so start defined before copying all v2 fields. */
        v9x_vbe_controller_summary_clear(&controller);
        controller.version = v9x_minivdd_version;
        controller.total_memory_bytes = (DWORD)v9x_minivdd_total64k * 65536ul;
        controller.capabilities = v9x_minivdd_capabilities;
        controller.oem_software_rev = v9x_minivdd_oem_revision;

        /*
         * VRAM only sizes the off-screen heap, and dd16.c has a floor to fall
         * back on, so a BIOS with a broken 4F00h costs some off-screen surfaces
         * rather than the whole enable.
         */
        v9x_vbe_vram_reported = controller.total_memory_bytes;
        v9x_vbe_vram_bytes = v9x_vram_usable_bytes(controller.total_memory_bytes,
                                                   pitch, height);
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
    WORD mapped;

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

    /*
     * Size the off-screen heap from the chip, for a family that can read it.
     *
     * After the aperture enable, not before: on the S3 families the size code
     * sits behind the extended-register locks the sequence above has just been
     * through, and reading it here means the registers are in the state the
     * rest of the enable left them in.
     *
     * Not a failure path. A size code this driver cannot decode returns 0 and
     * leaves the variable alone, so the family falls back to dd16.c's default
     * exactly as it did before the hook existed - an undecodable card keeps
     * working, it just does not get the correction.
     *
     * Tier-0 families have no hook here and fill this in from 4F00h during the
     * aperture read instead, so the two paths cannot fight over the variable.
     */
    if (v9x_hw16.read_video_memory != 0) {
        DWORD claimed = v9x_hw16.read_video_memory();
        WORD width;
        WORD height;
        WORD bpp;
        WORD pitch;

        if (claimed != 0ul &&
            v9x_selected_mode_geometry(&width, &height, &bpp, &pitch) != 0u) {
            v9x_vbe_vram_reported = claimed;
            v9x_vbe_vram_bytes = v9x_vram_usable_bytes(claimed, pitch, height);
        }
    }

    /* Stages 4 to 7, the reuse decision and the unwind on failure belong to
     * the mapping helper; it advances the stage code itself. */
    v9x_hardware_stage_code = 4u;
    v9x_map_physical_base = base;
    mapped = V9xMapAperture();
    if (mapped != 0u) {
        /* After the mapping, and only on success: the aperture handed to the
         * write-combining policy has to be the one the driver actually draws
         * through, which is not known until here. Diagnostic only. */
        v9x_mtrr_trace();
    }
    return mapped;
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
