#define SetCursor V9xUserSetCursor
#include <windows.h>
#undef SetCursor

#include "velocity9x/build.h"
#include "velocity9x/diagpaths.h"
#include "velocity9x/intel_gma.h"
#include "velocity9x/intel16.h"

#define V9X_I9XX_BOOT_INI     V9X_DIAG_INTELARM_TXT
#define V9X_I9XX_BOOT_SECTION "Velocity9x"

/* Never infer this from an on-disk key: it starts false on every driver load. */
WORD v9x_intel_boot_arm_latch;
/*
 * Which phase the consumed token claims, from IntelArmPhase.
 *
 * The latch above is deliberately phase-agnostic: it records that a valid
 * one-shot token was transferred, not what it authorises. Keeping those two
 * facts separate is what lets a phase ask "was this token issued for me?"
 * rather than assume it was.
 *
 * Zero when the key is absent, which is how every arm stick written before
 * 2026-09-15 reads. Zero matches no phase, so such a token authorises none by
 * default - the safe direction for a key that did not exist when they were
 * written.
 */
WORD v9x_intel_boot_arm_phase;
DWORD v9x_intel_boot_arm_crc;
char v9x_intel_boot_arm_token[64];
static const char *v9x_intel_boot_state = "NOT-RUN";

extern void FAR PASCAL V9xEnsureDiagDir(void);

const char *v9x_intel_boot_token_mover_state(void)
{
    return v9x_intel_boot_state;
}

/*
 * A boot marker written from inside I9XXCODE.
 *
 * Deliberately NOT routed through v9x_display_boot_mark. That would be a far
 * call back out into _TEXT, and an outbound crossing is one of the things
 * these markers exist to test - using it here would make the probe depend on
 * what it is probing. WritePrivateProfileString is a KERNEL import, so this
 * reaches the disk through an import thunk and no intra-module crossing at
 * all.
 */
static void v9x_intel_boot_mark(const char *stage)
{
#ifdef V9X_BOOT_TRACE
    WritePrivateProfileString("Velocity9x", "Stage", stage,
                              V9X_DIAG_BOOT_INI);
    /* Flush, for the same reason v9x_boot_trace does: the marker wanted is
     * the one written by a boot that did not finish. */
    WritePrivateProfileString(0, 0, 0, V9X_DIAG_BOOT_INI);
#else
    (void)stage;
#endif
}

static WORD v9x_intel_boot_read(const char *key, char *value, WORD capacity)
{
    WORD length;
    if (capacity < 2u) { return 0u; }
    value[0] = '\0';
    length = (WORD)GetPrivateProfileString(V9X_I9XX_BOOT_SECTION, key, "",
                                           value, capacity, V9X_I9XX_BOOT_INI);
    return length < capacity - 1u;
}

static WORD v9x_intel_boot_set(const char *key, const char *value)
{
    char check[96];

    if (!WritePrivateProfileString(V9X_I9XX_BOOT_SECTION, key, value,
                                   V9X_I9XX_BOOT_INI)) {
        return 0u;
    }
    /*
     * Flush for durability, but never judge the transaction by its result.
     * Measured on the netbook 2026-09-13: the first write to a new arm file
     * landed on disk and this call still reported failure, which aborted the
     * token transaction with TokenMover=IO-FAILED. The read-back below is the
     * verification; the flush is a hint.
     */
    (void)WritePrivateProfileString(0, 0, 0, V9X_I9XX_BOOT_INI);
    if (!v9x_intel_boot_read(key, check, sizeof(check))) {
        return 0u;
    }
    return v9x_intel_str_equal(check, value) != 0u;
}

/*
 * Retire a consumed token: record its outcome and clear IntelInFlight.
 *
 * This is the ONLY place either happens, and it is here rather than in the
 * phase that calls it because this unit owns the arm INI - the writer and its
 * read-back verification are already here, and a second copy elsewhere is a
 * second place for the one-shot property to be got wrong.
 *
 * A chained Phase 5 run reaches it exactly once, from the draw result. The
 * standalone Phase 4 path reaches it from its own. Every failure path in
 * either phase deliberately does NOT call it: a token that silently became
 * reusable after a hang would defeat the one-shot arm, so an unresolved run
 * leaves the token in flight and the next boot refuses with INCOMPLETE.
 *
 * Returns zero if either write failed, in which case the caller must assume
 * the token's state on disk is unknown and say so rather than claim a result.
 */
WORD v9x_intel_boot_arm_retire(const char *result)
{
    char last[96];

    v9x_intel_str_copy(last, result);
    v9x_intel_str_append(last, ":");
    v9x_intel_str_append(last, v9x_intel_boot_arm_token);
    if (!v9x_intel_boot_set("IntelLastResult", last)) {
        return 0u;
    }
    if (!v9x_intel_boot_set("IntelInFlight", "")) {
        return 0u;
    }
    /*
     * In-flight first, then the flag. The order is the safety property, not
     * an accident: these are two separate profile writes and nothing makes
     * them atomic. Interrupted between them leaves IntelIncomplete=1 with an
     * empty IntelInFlight, which the armers read as still unresolved and
     * refuse - a false positive costing one V9XCOPY. The reverse order would
     * leave the flag clear with a token still in flight, which is a silent
     * bypass of the hang stop.
     */
    if (!v9x_intel_boot_set("IntelIncomplete", "0")) {
        return 0u;
    }
    v9x_intel_boot_arm_latch = 0u;
    return 1u;
}

/* Intel build only. Called from DriverInit immediately after
 * v9x_display_boot_log and before any Enable, so the `libmain` trace is
 * already on disk if this function is what stops the load. It transfers
 * authority, but can reach no GPU write. */
void V9X_I9XX_FAR v9x_intel_boot_arm_prepare(void)
{
    char in_flight[65];
    char arm_once[65];
    char crc_text[16];
#ifdef V9X_I9XX_FIRST_WRITE_EXECUTOR
    char build_text[65];
    char phase_text[8];
    char repeat_text[8];
#endif
    char last_result[96];
    DWORD crc = 0ul;

    /*
     * First statement, before anything else touches memory or calls out. If
     * this marker reaches disk, the inbound far call from _TEXT into this
     * segment worked.
     */
    v9x_intel_boot_mark("arm-in");

    v9x_intel_boot_arm_latch = 0u;
    v9x_intel_boot_arm_phase = 0u;
    v9x_intel_boot_arm_crc = 0ul;
    v9x_intel_boot_arm_token[0] = '\0';
    v9x_intel_boot_state = "IO-FAILED";
    V9xEnsureDiagDir();
    /* And if this one does, the outbound far call to runtime.asm worked too,
     * which leaves only arm_prepare's own body. */
    v9x_intel_boot_mark("arm-dir");
    if (!v9x_intel_boot_set("IntelEnableThisBoot", "0") ||
        !v9x_intel_boot_read("IntelInFlight", in_flight,
                             sizeof(in_flight))) {
        return;
    }
    /*
     * IntelInFlight is the authoritative value; IntelIncomplete exists only
     * because a DOS batch cannot tell an empty INI value from a non-empty one
     * with FIND. So the flag is re-derived from the authority on every boot.
     *
     * That also migrates arm files written before the flag existed: those
     * carry a token with no flag at all, and an armer testing only the flag
     * would read them as resolved and overwrite the record of a hang.
     */
    if (in_flight[0] != '\0') {
        (void)v9x_intel_boot_set("IntelIncomplete", "1");
        v9x_intel_str_copy(last_result, "incomplete-reset:");
        v9x_intel_str_append(last_result, in_flight);
        if (v9x_intel_boot_set("IntelLastResult", last_result)) {
            v9x_intel_boot_state = "INCOMPLETE";
        }
        return;
    }
    /* Empty, so the flag is false, and it is written even when it was absent:
     * a legacy file is normalised by the first boot that sees it clean. */
    if (!v9x_intel_boot_set("IntelIncomplete", "0")) {
        return;
    }
    if (!v9x_intel_boot_read("IntelArmOnce", arm_once,
                             sizeof(arm_once))) {
        return;
    }
    if (arm_once[0] == '\0') {
        v9x_intel_boot_state = "READY";
        return;
    }
    if (v9x_i9xx_token_valid(arm_once) == V9X_FALSE) {
        v9x_intel_boot_state = "BAD-TOKEN";
        return;
    }
    if (!v9x_intel_boot_read("IntelArmCrc", crc_text,
                             sizeof(crc_text)) ||
        v9x_i9xx_parse_crc_hex(crc_text, &crc) == V9X_FALSE) {
        v9x_intel_boot_state = "BAD-CRC";
        return;
    }
#ifndef V9X_I9XX_FIRST_WRITE_EXECUTOR
    /* Never consume a one-shot token in a no-write-only build. The executor
     * and its Enable-side arm check must ship in the same binary. */
    v9x_intel_boot_state = "EXECUTOR-ABSENT";
    return;
#else
    if (!v9x_intel_boot_read("IntelArmBuildId", build_text,
                             sizeof(build_text)) ||
        v9x_intel_str_equal(
            build_text, v9x_intel_bridge_build_identity()->build_id) == 0u) {
        v9x_intel_boot_state = "BAD-BUILD";
        return;
    }
    /*
     * Read the claimed phase before the token transaction, so a stick this
     * build cannot interpret refuses without consuming its one shot.
     *
     * An absent key is not an error: it is what a Phase 4 stick looks like,
     * and it leaves the phase at zero. A key that is present but says
     * something other than 4 or 5 is an error - it means the arming script and
     * this driver disagree about the vocabulary, and guessing which phase was
     * meant is exactly the kind of inference that costs an armed boot.
     */
    if (!v9x_intel_boot_read("IntelArmPhase", phase_text,
                             sizeof(phase_text))) {
        v9x_intel_boot_state = "BAD-PHASE";
        return;
    }
    if (v9x_intel_str_equal(phase_text, "4") != 0u) {
        v9x_intel_boot_arm_phase = V9X_I9XX_PHASE4;
    } else if (v9x_intel_str_equal(phase_text, "5") != 0u) {
        v9x_intel_boot_arm_phase = V9X_I9XX_PHASE5;
    } else if (phase_text[0] != '\0') {
        v9x_intel_boot_state = "BAD-PHASE";
        return;
    }
    /*
     * Repeat mode: the token is still transferred in-flight, but IntelArmOnce
     * is NOT cleared, so the same stick arms again on the next boot without
     * being re-armed.
     *
     * Requested 2026-09-15 to make the colour and geometry experiments cheap.
     * It removes only the "once" in one-shot. Everything else still gates every
     * boot - the errata gate, the build-id match, the combined-CRC match, the
     * PCI identity check and the Phase 4 replay - and each of those has caught
     * a real defect today; the stale D0478966 was caught by the CRC match.
     *
     * The in-flight transfer is kept deliberately, and it is what makes this
     * safe to hand to an operator. A run that completes retires the token,
     * clears in-flight, and the next boot re-arms from the surviving
     * IntelArmOnce. A run that HANGS leaves in-flight set, and the next boot
     * reports INCOMPLETE and refuses exactly as it always has. So successful
     * boots repeat freely and a configuration that hangs the machine stops the
     * loop rather than repeating it - which is the property worth keeping out
     * of the one-shot arm.
     *
     * Clearing it is one line in the arm file from DOS, or any V9XCOPY.
     */
    if (v9x_intel_boot_read("IntelArmRepeat", repeat_text,
                            sizeof(repeat_text)) &&
        v9x_intel_str_equal(repeat_text, "1") != 0u) {
        /* Flag first, for the reason given at the retirement. */
        if (!v9x_intel_boot_set("IntelIncomplete", "1") ||
            !v9x_intel_boot_set("IntelInFlight", arm_once) ||
            !v9x_intel_boot_set("IntelEnableThisBoot", "1")) {
            return;
        }
        v9x_intel_str_copy(v9x_intel_boot_arm_token, arm_once);
        v9x_intel_boot_arm_crc = crc;
        v9x_intel_boot_arm_latch = 1u;
        v9x_intel_boot_state = "ARMED-REPEAT";
        return;
    }

    /*
     * IntelIncomplete mirrors "IntelInFlight is not empty" as a value a DOS
     * batch can actually test. FIND matches substrings, so
     * "IntelInFlight=" matches the empty form and every non-empty one
     * alike - the armers' guard built on it was a no-op from the day it
     * was written, in both phases. This key is the driver stating the fact
     * plainly instead.
     */
    if (!v9x_intel_boot_set("IntelIncomplete", "1") ||
        !v9x_intel_boot_set("IntelInFlight", arm_once) ||
        !v9x_intel_boot_set("IntelArmOnce", "") ||
        !v9x_intel_boot_set("IntelEnableThisBoot", "1")) {
        return;
    }
    v9x_intel_str_copy(v9x_intel_boot_arm_token, arm_once);
    v9x_intel_boot_arm_crc = crc;
    v9x_intel_boot_arm_latch = 1u;
    v9x_intel_boot_state = "ARMED";
#endif
}
