#define SetCursor V9xUserSetCursor
#include <windows.h>
#undef SetCursor

#include "velocity9x/build.h"
#include "velocity9x/diagpaths.h"
#include "velocity9x/intel_gma.h"
#include "velocity9x/intel16.h"

#define V9X_P4_SECTION "IntelRing"
/* The arm transaction's own file, not SYSTEM.INI; see diagpaths.h. */
#define V9X_P4_INI     V9X_DIAG_INTELARM_TXT
#define V9X_P4_GUARD   0xa5a5a5a5ul
#define V9X_P4_COLOR   0x55aa33ccul

extern WORD v9x_intel_boot_arm_latch;
extern DWORD v9x_intel_boot_arm_crc;
extern char v9x_intel_boot_arm_token[64];
extern DWORD v9x_i9xx_first[V9X_I9XX_SNAPSHOT_DWORDS];
extern DWORD v9x_i9xx_gtt_hash_a;
extern DWORD v9x_i9xx_gtt_hash_b;
extern DWORD v9x_i9xx_bsm;
extern DWORD v9x_i9xx_gmadr_bar2;
extern DWORD v9x_i9xx_ring_memory_value;
extern DWORD v9x_i9xx_ring_stage_fail;
extern DWORD v9x_i9xx_ring_stage_read;
extern DWORD v9x_i9xx_ring_exec_head;
extern DWORD v9x_i9xx_ring_exec_tail;
extern DWORD v9x_i9xx_ring_exec_elapsed;
extern DWORD v9x_i9xx_ring_exec_polls;
extern DWORD v9x_i9xx_ring_exec_failure;
extern DWORD v9x_i9xx_ring_diag_value;
extern WORD v9x_i9xx_event_count;
extern WORD v9x_i9xx_event_dropped;
extern DWORD v9x_i9xx_event_value;
extern WORD FAR PASCAL V9xPciReadIntelRevision(void);
extern WORD FAR PASCAL V9xMiniI9xxRingStage(DWORD physical, WORD index,
                                             DWORD word);
extern WORD FAR PASCAL V9xMiniI9xxRingMemory(DWORD offset);
extern WORD FAR PASCAL V9xMiniI9xxRingExecute(DWORD crc, WORD step);
extern WORD FAR PASCAL V9xMiniI9xxRingDiag(WORD index);
extern WORD FAR PASCAL V9xMiniI9xxEventDword(WORD event, WORD field);
extern DWORD FAR PASCAL V9xGmadrRead(DWORD offset);
extern WORD FAR PASCAL V9xGmadrWrite(DWORD offset, DWORD value);

#ifdef V9X_I9XX_FIRST_WRITE_EXECUTOR
static void v9x_p4_hex(char *text, DWORD value)
{
    static const char digits[] = "0123456789ABCDEF";
    short shift;
    for (shift = 28; shift >= 0; shift -= 4) {
        text[(28 - shift) / 4] = digits[(value >> shift) & 15ul];
    }
    text[8] = '\0';
}

static WORD v9x_p4_set(const char *section, const char *key,
                       const char *value, const char *path)
{
    char check[96];
    WORD length;
    if (!WritePrivateProfileString(section, key, value, path)) { return 0u; }
    /* Durability hint only; the read-back is the verification. A failing
     * flush aborted the whole token transaction once. See intel_boot16.c. */
    (void)WritePrivateProfileString(0, 0, 0, path);
    length = (WORD)GetPrivateProfileString(section, key, "", check,
                                           sizeof(check), path);
    return length < sizeof(check) - 1u && v9x_intel_str_equal(check, value) != 0u;
}

static WORD v9x_p4_ring(const char *key, const char *value)
{
    return v9x_p4_set(V9X_P4_SECTION, key, value, V9X_DIAG_INTELRNG_TXT);
}

static WORD v9x_p4_ring_hex(const char *key, DWORD value)
{
    char text[9];
    v9x_p4_hex(text, value);
    return v9x_p4_ring(key, text);
}

static WORD v9x_p4_profile(const char *key, const char *value)
{
    return v9x_p4_set("Velocity9x", key, value, V9X_P4_INI);
}

static WORD v9x_p4_read_profile(const char *key, char *value, WORD capacity)
{
    WORD length;
    if (capacity < 2u) { return 0u; }
    length = (WORD)GetPrivateProfileString("Velocity9x", key, "", value,
                                           capacity, V9X_P4_INI);
    return length < capacity - 1u;
}

static WORD v9x_p4_capture_errors(const char *prefix, DWORD *values)
{
    WORD index;
    char key[16];
    for (index = 0u; index < 7u; ++index) {
        v9x_intel_str_copy(key, prefix);
        key[v9x_intel_str_length(prefix)] = (char)('0' + index);
        key[v9x_intel_str_length(prefix) + 1u] = '\0';
        if (!V9xMiniI9xxRingDiag(index)) { return 0u; }
        if (values != 0) { values[index] = v9x_i9xx_ring_diag_value; }
        if (!v9x_p4_ring_hex(key, v9x_i9xx_ring_diag_value)) { return 0u; }
    }
    return 1u;
}

static WORD v9x_p4_mmio_dump(const char *prefix, const DWORD *values)
{
    WORD index;
    char key[12];
    for (index = 0u; index < V9X_I9XX_SNAPSHOT_DWORDS; ++index) {
        v9x_intel_str_copy(key, prefix);
        key[v9x_intel_str_length(prefix)] = (char)('0' + index / 10u);
        key[v9x_intel_str_length(prefix) + 1u] = (char)('0' + index % 10u);
        key[v9x_intel_str_length(prefix) + 2u] = '\0';
        if (!v9x_p4_ring_hex(key, values[index])) { return 0u; }
    }
    return 1u;
}

static WORD v9x_p4_latest_event(WORD kind)
{
    WORD field;
    if (v9x_i9xx_event_count == 0u ||
        v9x_i9xx_event_count > V9X_I9XX_EVENT_MAX ||
        v9x_i9xx_event_dropped != 0u ||
        !V9xMiniI9xxEventDword(v9x_i9xx_event_count - 1u,
                                V9X_I9XX_EVENT_KIND) ||
        v9x_i9xx_event_value != kind ||
        !V9xMiniI9xxEventDword(v9x_i9xx_event_count - 1u,
                                V9X_I9XX_EVENT_FLAGS) ||
        (v9x_i9xx_event_value & V9X_I9XX_EVENT_REQUIRED) !=
            V9X_I9XX_EVENT_REQUIRED) { return 0u; }
    for (field = V9X_I9XX_EVENT_PGTBL_CTL;
         field <= V9X_I9XX_EVENT_GTT_HASH_B; ++field) {
        DWORD expected;
        if (field == V9X_I9XX_EVENT_PGTBL_CTL) {
            expected = 0x7ffc0001ul;
        } else if (field == V9X_I9XX_EVENT_HWS_PGA) {
            expected = 0x1ffff000ul;
        } else if (field >= V9X_I9XX_EVENT_GTT_HASH_A) {
            expected = 0x4d8707c5ul;
        } else { expected = 0ul; }
        if (!V9xMiniI9xxEventDword(v9x_i9xx_event_count - 1u, field) ||
            v9x_i9xx_event_value != expected) { return 0u; }
    }
    return 1u;
}

static void v9x_p4_uncertain(const char *result)
{
    v9x_intel_boot_arm_latch = 0u;
    (void)v9x_p4_capture_errors("FailErr", 0);
    (void)v9x_p4_ring("Result", result);
    /* No write to HEAD, START, CTL or TAIL here. Keep IntelInFlight. */
}

static void v9x_p4_clean_refusal(const char *reason)
{
    char last[96];
    v9x_intel_boot_arm_latch = 0u;
    if (!v9x_p4_ring("Result", reason)) { return; }
    v9x_intel_str_copy(last, "refused:");
    v9x_intel_str_append(last, reason);
    v9x_intel_str_append(last, ":");
    v9x_intel_str_append(last, v9x_intel_boot_arm_token);
    if (!v9x_p4_profile("IntelLastResult", last) ||
        !v9x_p4_profile("IntelEnableThisBoot", "0")) { return; }
    (void)v9x_p4_profile("IntelInFlight", "");
}

/*
 * Preflight reason codes, published as PreconditionCode. Twenty-odd
 * conditions behind one boolean cost a boot on 2026-09-13 because the file
 * said only PRECONDITION-REFUSED; a machine with no serial port cannot afford
 * that. The names live in docs\specifications\hardware-diagnostics.md rather
 * than in the driver, which is close to its 64K code segment limit.
 */
#define V9X_P4_PRE_OK           0u
#define V9X_P4_PRE_PROFILE      1u
#define V9X_P4_PRE_BUILD        2u
#define V9X_P4_PRE_ACCEL        3u
#define V9X_P4_PRE_CRC_TEXT     4u
#define V9X_P4_PRE_ARM          5u
#define V9X_P4_PRE_CRC_MATCH    6u
#define V9X_P4_PRE_FLUSH        7u
#define V9X_P4_PRE_LAYOUT       8u
#define V9X_P4_PRE_BSM          9u
#define V9X_P4_PRE_PGTBL       10u
#define V9X_P4_PRE_RING        11u
#define V9X_P4_PRE_GTT_HASH    12u
#define V9X_P4_PRE_EVENTS      13u
#define V9X_P4_PRE_MMIO_RESULT 14u
#define V9X_P4_PRE_GTT_RESULT  15u
#define V9X_P4_PRE_DIAG_READ   16u
#define V9X_P4_PRE_EIR         17u
#define V9X_P4_PRE_ESR         18u

static WORD v9x_p4_pre_rejection;

static WORD v9x_p4_preflight(const struct v9x_i9xx_sandbox_layout *layout,
                             const DWORD *probe, const DWORD *blt,
                             WORD flush_stable)
{
    struct v9x_i9xx_arm_request request;
    char in_flight[65];
    char enabled[8];
    char crc_text[16];
    char status[16];
    char accel_default[8];
    char arm_build[65];
    DWORD crc;
    WORD rejection;

    v9x_p4_pre_rejection = 0u;
    if (!v9x_p4_read_profile("IntelInFlight", in_flight,
                              sizeof(in_flight)) ||
        !v9x_p4_read_profile("IntelEnableThisBoot", enabled,
                              sizeof(enabled)) ||
        !v9x_p4_read_profile("IntelArmCrc", crc_text,
                              sizeof(crc_text)) ||
        !v9x_p4_read_profile("IntelAccelDefault", accel_default,
                              sizeof(accel_default)) ||
        !v9x_p4_read_profile("IntelArmBuildId", arm_build,
                              sizeof(arm_build))) {
        return V9X_P4_PRE_PROFILE;
    }
    if (v9x_intel_str_equal(arm_build,
                            v9x_intel_bridge_build_identity()->build_id) == 0u) {
        return V9X_P4_PRE_BUILD;
    }
    if (v9x_intel_str_equal(accel_default, "0") == 0u) {
        return V9X_P4_PRE_ACCEL;
    }
    if (v9x_i9xx_parse_crc_hex(crc_text, &crc) == V9X_FALSE) {
        return V9X_P4_PRE_CRC_TEXT;
    }
    request.token = v9x_intel_boot_arm_token;
    request.in_flight = in_flight;
    request.configured_crc = crc;
    request.packet_crc = v9x_i9xx_phase4_execution_crc(probe, blt);
    request.enable_this_boot = (WORD)(v9x_intel_str_equal(enabled, "1") != 0u);
    /*
     * Not GetSystemMetrics(SM_CLEANBOOT). That lives in USER, and GDI loads a
     * display driver before USER exists, so importing it made the whole module
     * unloadable: Windows silently used the INF's 4-bpp vga.drv row and this
     * driver never ran at all (2026-09-13, see docs\issues). Safe Mode forces
     * the standard VGA driver, so a boot that reaches this code is already not
     * a Safe Mode boot; the field stays in the contract for the host tests and
     * for any future caller that can answer it without USER.
     */
    request.safe_mode = V9X_FALSE;
    request.errata_gate = V9X_TRUE;
    request.vendor_id = 0x8086u;
    request.device_id = 0x27aeu;
    request.revision = V9xPciReadIntelRevision();
    request.phase = V9X_I9XX_PHASE4;
    if (v9x_i9xx_arm_evaluate(&request, &rejection) != V9X_STATUS_OK) {
        v9x_p4_pre_rejection = rejection;
        return V9X_P4_PRE_ARM;
    }
    if (crc != v9x_intel_boot_arm_crc) { return V9X_P4_PRE_CRC_MATCH; }
    if (flush_stable == 0u) { return V9X_P4_PRE_FLUSH; }
    if (layout->reserve_offset != 0x00790000ul ||
        layout->reserve_physical != 0x7ff90000ul ||
        layout->scratch_offset != 0x007a1000ul) {
        return V9X_P4_PRE_LAYOUT;
    }
    if (v9x_i9xx_bsm != 0x7f800000ul) { return V9X_P4_PRE_BSM; }
    if (v9x_i9xx_first[0] != 0x7ffc0001ul) { return V9X_P4_PRE_PGTBL; }
    if (v9x_i9xx_first[1] != 0ul || v9x_i9xx_first[2] != 0ul ||
        v9x_i9xx_first[3] != 0ul || v9x_i9xx_first[4] != 0ul) {
        return V9X_P4_PRE_RING;
    }
    if (v9x_i9xx_gtt_hash_a != 0x4d8707c5ul ||
        v9x_i9xx_gtt_hash_b != 0x4d8707c5ul) {
        return V9X_P4_PRE_GTT_HASH;
    }
    if (v9x_i9xx_event_count > V9X_I9XX_EVENT_MAX - 2u ||
        v9x_i9xx_event_dropped != 0u) {
        return V9X_P4_PRE_EVENTS;
    }
    GetPrivateProfileString("IntelMmio", "Result", "", status,
                            sizeof(status), V9X_DIAG_INTELMM_TXT);
    if (v9x_intel_str_equal(status, "PASS") == 0u) { return V9X_P4_PRE_MMIO_RESULT; }
    GetPrivateProfileString("IntelGtt", "Result", "", status,
                            sizeof(status), V9X_DIAG_INTELGTT_TXT);
    if (v9x_intel_str_equal(status, "PASS") == 0u) { return V9X_P4_PRE_GTT_RESULT; }

    /*
     * The diag table is EIR 20B0h (index 4), EMR 20B4h (index 5) and ESR
     * 20B8h (index 6), which is the order i915_reg.h defines them in. EIR and
     * ESR are latched error state and must be clear; EMR is the error *mask*
     * and reads FFFFFFFF on this machine, measured 2026-09-14. Checking 4 and
     * 6 was right all along. A guess on 2026-09-13 moved the second check to
     * index 5 on the theory that 5 was ESR; the capture that followed shows
     * index 5 is the all-ones mask, so that change would have refused a
     * healthy machine. Reverted, and the measured values are recorded in the
     * diagnostics spec so the next person does not have to guess either.
     */
    if (!V9xMiniI9xxRingDiag(4u)) { return V9X_P4_PRE_DIAG_READ; }
    if (v9x_i9xx_ring_diag_value != 0ul) { return V9X_P4_PRE_EIR; }
    if (!V9xMiniI9xxRingDiag(6u)) { return V9X_P4_PRE_DIAG_READ; }
    if (v9x_i9xx_ring_diag_value != 0ul) { return V9X_P4_PRE_ESR; }
    return V9X_P4_PRE_OK;
}

/*
 * Stage failure detail. GTT-MIRROR-FAILED collapsed seven conditions into one
 * label and cost an armed boot on 2026-09-14, which is the third time that
 * shape of refusal has been unreadable. The distinction that matters most is
 * between "the VxD's own read-back of what it wrote differs", which is a
 * mapping fault, and "the VxD read it back correctly but the GMADR aperture
 * shows something else", which would be a real coherency finding about CPU
 * writes to stolen memory and the chipset write buffer the flush page drains.
 */
#define V9X_P4_STAGE_WRITE_REFUSED  1u
#define V9X_P4_STAGE_READ_REFUSED   2u
#define V9X_P4_STAGE_MEMORY_DIFFERS 3u
#define V9X_P4_STAGE_GMADR_DIFFERS  4u
#define V9X_P4_STAGE_GUARD_MEMORY   5u
#define V9X_P4_STAGE_GUARD_GMADR    6u

static WORD v9x_p4_stage_fail;
static DWORD v9x_p4_stage_index;
static DWORD v9x_p4_stage_expected;
static DWORD v9x_p4_stage_memory;
static DWORD v9x_p4_stage_gmadr;

static WORD v9x_p4_stage_guard(const struct v9x_i9xx_sandbox_layout *layout,
                               DWORD reserve_offset, DWORD aperture_offset)
{
    v9x_p4_stage_index = reserve_offset;
    v9x_p4_stage_expected = V9X_P4_GUARD;
    if (V9xMiniI9xxRingMemory(reserve_offset) != 0u) {
        v9x_p4_stage_memory = v9x_i9xx_ring_memory_value;
    }
    v9x_p4_stage_gmadr = V9xGmadrRead(aperture_offset);
    if (v9x_p4_stage_gmadr != V9X_P4_GUARD) {
        v9x_p4_stage_fail = V9X_P4_STAGE_GUARD_GMADR;
        return 0u;
    }
    (void)layout;
    return 1u;
}

static WORD v9x_p4_stage(const struct v9x_i9xx_sandbox_layout *layout,
                          const DWORD *probe, const DWORD *blt)
{
    DWORD index;
    DWORD expected;

    v9x_p4_stage_fail = 0u;
    v9x_p4_stage_index = 0ul;
    v9x_p4_stage_expected = 0ul;
    v9x_p4_stage_memory = 0ul;
    v9x_p4_stage_gmadr = 0ul;
    /* Three markers, not twenty-two. Enough to locate a hang to a phase of
     * this step without paying a flushed profile write per dword. */
    /*
     * Declare each dword to the mini-VDD, which still refuses anything outside
     * the reviewed stream, then put the bytes there through the GMADR
     * aperture. The aperture is the CPU's path into stolen memory; the
     * physical mapping is kept only so its read-back can be recorded, since
     * measuring that it does not stick is the finding of 2026-09-14.
     */
    (void)v9x_p4_ring("IntentStep", "stage-write");
    for (index = 0ul; index < 10ul; ++index) {
        expected = index < 2ul ? probe[index] : blt[index - 2ul];
        /* The aperture address of the reserve, not its BSM address: the
         * mini-VDD maps and verifies through the same path the GPU fetches
         * from, because the BSM one is not routed to the CPU at all. */
        if (V9xMiniI9xxRingStage(v9x_i9xx_gmadr_bar2 + layout->ring_offset,
                                  (WORD)index, expected) == 0u ||
            V9xGmadrWrite(layout->ring_offset + index * 4ul, expected) == 0u) {
            v9x_p4_stage_fail = V9X_P4_STAGE_WRITE_REFUSED;
            v9x_p4_stage_index = index;
            v9x_p4_stage_expected = expected;
            return 0u;
        }
    }
    for (index = 0ul; index < 1024ul; ++index) {
        if (V9xGmadrWrite(layout->scratch_offset + index * 4ul,
                          V9X_P4_GUARD) == 0u) {
            v9x_p4_stage_fail = V9X_P4_STAGE_WRITE_REFUSED;
            v9x_p4_stage_index = 0x11000ul;
            v9x_p4_stage_expected = V9X_P4_GUARD;
            return 0u;
        }
    }
    (void)v9x_p4_ring("IntentStep", "stage-mirror");
    for (index = 0ul; index < 10ul; ++index) {
        expected = index < 2ul ? probe[index] : blt[index - 2ul];
        v9x_p4_stage_index = index;
        v9x_p4_stage_expected = expected;
        /* Recorded, not required: see the comment above the write loop. */
        if (V9xMiniI9xxRingMemory(index * 4ul) != 0u) {
            v9x_p4_stage_memory = v9x_i9xx_ring_memory_value;
        }
        v9x_p4_stage_gmadr =
            V9xGmadrRead(layout->ring_offset + index * 4ul);
        if (v9x_p4_stage_gmadr != expected) {
            v9x_p4_stage_fail = V9X_P4_STAGE_GMADR_DIFFERS;
            return 0u;
        }
    }
    (void)v9x_p4_ring("IntentStep", "stage-guard");
    if (!v9x_p4_stage_guard(layout, 0x11000ul, layout->scratch_offset) ||
        !v9x_p4_stage_guard(layout, 0x11ffcul,
                            layout->scratch_offset + 0xffcul)) {
        return 0u;
    }
    return 1u;
}

static WORD v9x_p4_step(WORD step, DWORD crc)
{
    char key[12];
    char *p = key;
    WORD success;
    DWORD free_bytes;
    DWORD head = step == 6u || step == 8u ? 0ul : 8ul;
    DWORD needed = step == 7u ? V9X_I9XX_RING_BYTES - 8ul :
                   step == 9u ? 32ul : 8ul;
    *p++ = 'S'; *p++ = (char)('0' + step / 10u);
    *p++ = (char)('0' + step % 10u);
    *p = '\0';
    /* The marker goes down before the precheck, not after it. A step that
     * refused its own arithmetic used to return with nothing written, which
     * on a machine whose only trace is this file is the same as a hang. */
    if (!v9x_p4_ring("IntentStep", key)) { return 0u; }
    if (step >= 6u && step <= 9u &&
        (v9x_i9xx_ring_free_space(head, head, V9X_I9XX_RING_BYTES,
                                   &free_bytes) != V9X_STATUS_OK ||
         free_bytes < needed)) {
        v9x_intel_str_copy(p, "Result");
        (void)v9x_p4_ring(key, "NO-RING-SPACE");
        return 0u;
    }
    success = V9xMiniI9xxRingExecute(crc, step);
    v9x_intel_str_copy(p, "Result");
    if (!v9x_p4_ring(key, success != 0u ? "PASS" : "FAIL")) { return 0u; }
    v9x_intel_str_copy(p, "Head");
    if (!v9x_p4_ring_hex(key, v9x_i9xx_ring_exec_head)) { return 0u; }
    v9x_intel_str_copy(p, "Tail");
    if (!v9x_p4_ring_hex(key, v9x_i9xx_ring_exec_tail)) { return 0u; }
    v9x_intel_str_copy(p, "Ms");
    if (!v9x_p4_ring_hex(key, v9x_i9xx_ring_exec_elapsed)) { return 0u; }
    v9x_intel_str_copy(p, "Polls");
    if (!v9x_p4_ring_hex(key, v9x_i9xx_ring_exec_polls)) { return 0u; }
    v9x_intel_str_copy(p, "Failure");
    if (!v9x_p4_ring_hex(key, v9x_i9xx_ring_exec_failure)) { return 0u; }
    if (!V9xMiniI9xxRingDiag(7u)) { return 0u; }
    v9x_intel_str_copy(p, "Ctl");
    if (!v9x_p4_ring_hex(key, v9x_i9xx_ring_diag_value)) { return 0u; }
    if (!V9xMiniI9xxRingDiag(8u)) { return 0u; }
    v9x_intel_str_copy(p, "Start");
    if (!v9x_p4_ring_hex(key, v9x_i9xx_ring_diag_value)) { return 0u; }
    return success;
}

static WORD v9x_p4_verify_scratch(DWORD scratch_offset)
{
    DWORD index;
    DWORD actual;
    DWORD expected;
    for (index = 0ul; index < 1024ul; ++index) {
        expected = index >= 64ul && index < 128ul ? V9X_P4_COLOR :
                                                    V9X_P4_GUARD;
        actual = V9xGmadrRead(scratch_offset + index * 4ul);
        if (actual != expected) {
            (void)v9x_p4_ring_hex("FirstMismatch", index * 4ul);
            (void)v9x_p4_ring_hex("MismatchActual", actual);
            return 0u;
        }
    }
    return 1u;
}
#endif

/* Called after the no-write plan, on the first Enable only. The source is
 * compiled into Intel builds but the call and VxD register stores are both
 * absent until V9X_I9XX_FIRST_WRITE_EXECUTOR is positively defined. */
void v9x_intel_phase4_maybe_run(
    const struct v9x_i9xx_sandbox_layout *layout,
    const DWORD *probe, const DWORD *blt, WORD flush_stable)
{
#ifdef V9X_I9XX_FIRST_WRITE_EXECUTOR
    struct v9x_i9xx_phase4_sequence sequence;
    DWORD crc;
    WORD clean_pass;
    WORD blit_pass;
    WORD index;
    WORD precondition;
    DWORD pre_mmio[V9X_I9XX_SNAPSHOT_DWORDS];
    DWORD pre_errors[7];
    DWORD post_errors[7];
    char crc_text[9];
    char last[96];
    if (v9x_intel_boot_arm_latch == 0u) { return; }
    v9x_i9xx_phase4_sequence_begin(&sequence);
    precondition = v9x_p4_preflight(layout, probe, blt, flush_stable);
    if (precondition != V9X_P4_PRE_OK) {
        /* Say which one, and dump the read-only error registers with it, so a
         * refusal on a blind machine is a finding rather than another boot. */
        (void)v9x_p4_ring_hex("PreconditionCode", precondition);
        (void)v9x_p4_ring_hex("PreconditionArmReject", v9x_p4_pre_rejection);
        (void)v9x_p4_capture_errors("RefErr", 0);
        /*
         * What the failing check actually saw. On 2026-09-14 code 8 fired
         * while the layout this same function had just published matched the
         * expected constants exactly, and there was no way to tell which of
         * the two views was wrong. A refusal that names a check but not its
         * operands is still a trip to the machine.
         */
        (void)v9x_p4_ring_hex("RefReserveOffset", layout->reserve_offset);
        (void)v9x_p4_ring_hex("RefReservePhysical", layout->reserve_physical);
        (void)v9x_p4_ring_hex("RefScratchOffset", layout->scratch_offset);
        (void)v9x_p4_ring_hex("RefBsm", v9x_i9xx_bsm);
        (void)v9x_p4_ring_hex("RefPgtbl", v9x_i9xx_first[0]);
        (void)v9x_p4_ring_hex("RefGttHashA", v9x_i9xx_gtt_hash_a);
        (void)v9x_p4_ring_hex("RefEventCount", v9x_i9xx_event_count);
        v9x_p4_clean_refusal("PRECONDITION-REFUSED");
        return;
    }
    if (!v9x_p4_ring("Access", "armed-hardware-write") ||
        !v9x_p4_ring("ErrataGate", "1") ||
        !v9x_p4_ring("IntentBuildId", v9x_intel_bridge_build_identity()->build_id) ||
        !v9x_p4_ring("Result", "ARMED-IN-PROGRESS")) {
        v9x_p4_uncertain("ARM-LOG-FAILED"); return;
    }
    crc = v9x_i9xx_phase4_execution_crc(probe, blt);
    for (index = 0u; index < V9X_I9XX_SNAPSHOT_DWORDS; ++index) {
        pre_mmio[index] = v9x_i9xx_first[index];
    }
    if (!v9x_p4_mmio_dump("PreM", pre_mmio)) {
        v9x_p4_uncertain("PRE-MMIO-LOG-FAILED"); return;
    }
    v9x_p4_hex(crc_text, crc);
    if (v9x_i9xx_phase4_sequence_commit(&sequence,
            V9X_I9XX_P4_PREFLIGHT) != V9X_STATUS_OK ||
        !v9x_p4_ring("Intent", v9x_intel_boot_arm_token) ||
        !v9x_p4_ring("IntentCrc", crc_text) ||
        !v9x_p4_ring("IntentStep", "stage") ||
        !v9x_p4_profile("IntelEnableThisBoot", "0")) {
        v9x_p4_uncertain("INTENT-FAILED"); return;
    }
    if (v9x_i9xx_phase4_sequence_commit(&sequence,
            V9X_I9XX_P4_INTENT) != V9X_STATUS_OK) {
        v9x_p4_uncertain("ORDER-FAILED"); return;
    }
    if (!v9x_p4_stage(layout, probe, blt)) {
        (void)v9x_p4_ring_hex("StageFail", v9x_p4_stage_fail);
        (void)v9x_p4_ring_hex("StageIndex", v9x_p4_stage_index);
        (void)v9x_p4_ring_hex("StageExpected", v9x_p4_stage_expected);
        (void)v9x_p4_ring_hex("StageMemory", v9x_p4_stage_memory);
        (void)v9x_p4_ring_hex("StageGmadr", v9x_p4_stage_gmadr);
        (void)v9x_p4_ring_hex("StageVxdFail", v9x_i9xx_ring_stage_fail);
        (void)v9x_p4_ring_hex("StagePhysRead", v9x_i9xx_ring_stage_read);
        v9x_p4_uncertain("GTT-MIRROR-FAILED"); return;
    }
    if (!v9x_p4_ring("StageMirror", "PASS")) {
        v9x_p4_uncertain("STAGE-LOG-FAILED"); return;
    }
    if (v9x_i9xx_phase4_sequence_commit(&sequence,
            V9X_I9XX_P4_STAGE) != V9X_STATUS_OK) {
        v9x_p4_uncertain("ORDER-FAILED"); return;
    }
    v9x_intel_publish_event(7u, 0u);
    if (!v9x_p4_latest_event(7u) ||
        !v9x_p4_capture_errors("PreErr", pre_errors) ||
        !v9x_p4_ring("PreSnapshot", "PASS") ||
        !v9x_p4_ring("IntentStep", "execute")) {
        v9x_p4_uncertain("PRE-SNAPSHOT-FAILED"); return;
    }
    if (v9x_i9xx_phase4_sequence_commit(&sequence,
            V9X_I9XX_P4_PRE_SNAPSHOT) != V9X_STATUS_OK) {
        v9x_p4_uncertain("ORDER-FAILED"); return;
    }
    if (!v9x_p4_step(5u, crc)) {
        v9x_p4_uncertain("PROGRAM-FAILED"); return;
    }
    if (v9x_i9xx_phase4_sequence_commit(&sequence,
            V9X_I9XX_P4_PROGRAM) != V9X_STATUS_OK) {
        v9x_p4_uncertain("ORDER-FAILED"); return;
    }
    if (!v9x_p4_step(6u, crc)) {
        v9x_p4_uncertain("PROBE-TIMEOUT"); return;
    }
    if (v9x_i9xx_phase4_sequence_commit(&sequence,
            V9X_I9XX_P4_PROBE_DRAINED) != V9X_STATUS_OK) {
        v9x_p4_uncertain("ORDER-FAILED"); return;
    }
    if (!v9x_p4_step(7u, crc)) {
        v9x_p4_uncertain("WRAP-TIMEOUT"); return;
    }
    if (v9x_i9xx_phase4_sequence_commit(&sequence,
            V9X_I9XX_P4_WRAP_DRAINED) != V9X_STATUS_OK) {
        v9x_p4_uncertain("ORDER-FAILED"); return;
    }
    if (!v9x_p4_step(8u, crc)) {
        v9x_p4_uncertain("REPROBE-TIMEOUT"); return;
    }
    if (v9x_i9xx_phase4_sequence_commit(&sequence,
            V9X_I9XX_P4_REPROBE_DRAINED) != V9X_STATUS_OK) {
        v9x_p4_uncertain("ORDER-FAILED"); return;
    }
    if (!v9x_p4_step(9u, crc)) {
        v9x_p4_uncertain("BLT-TIMEOUT"); return;
    }
    if (v9x_i9xx_phase4_sequence_commit(&sequence,
            V9X_I9XX_P4_BLT_DRAINED) != V9X_STATUS_OK) {
        v9x_p4_uncertain("ORDER-FAILED"); return;
    }
    blit_pass = v9x_p4_verify_scratch(layout->scratch_offset);
    clean_pass = blit_pass;
    if (!v9x_p4_ring("ScratchGuard", blit_pass != 0u ? "PASS" : "FAIL")) {
        v9x_p4_uncertain("VERIFY-LOG-FAILED"); return;
    }
    if (!v9x_p4_ring("S10Result", blit_pass != 0u ? "PASS" : "FAIL")) {
        v9x_p4_uncertain("VERIFY-LOG-FAILED"); return;
    }
    if (v9x_i9xx_phase4_sequence_commit(&sequence,
            V9X_I9XX_P4_VERIFY) != V9X_STATUS_OK) {
        v9x_p4_uncertain("ORDER-FAILED"); return;
    }
    if (!v9x_p4_step(11u, crc)) {
        v9x_p4_uncertain("TEARDOWN-FAILED"); return;
    }
    if (v9x_i9xx_phase4_sequence_commit(&sequence,
            V9X_I9XX_P4_TEARDOWN) != V9X_STATUS_OK) {
        v9x_p4_uncertain("ORDER-FAILED"); return;
    }
    v9x_intel_publish_event(8u, 0u);
    if (!v9x_p4_latest_event(8u)) {
        v9x_p4_uncertain("POST-EVENT-FAILED"); return;
    }
    if (!v9x_p4_capture_errors("PostErr", post_errors)) {
        v9x_p4_uncertain("POST-DIAG-FAILED"); return;
    }
    for (index = 0u; index < 7u; ++index) {
        if (pre_errors[index] != post_errors[index]) { clean_pass = 0u; }
    }
    v9x_intel_publish_mmio_fingerprint();
    v9x_intel_publish_gtt_inventory();
    if (!v9x_p4_mmio_dump("PostM", v9x_i9xx_first)) {
        v9x_p4_uncertain("POST-MMIO-LOG-FAILED"); return;
    }
    for (index = 0u; index < V9X_I9XX_SNAPSHOT_DWORDS; ++index) {
        if (v9x_i9xx_first[index] != pre_mmio[index]) { clean_pass = 0u; }
    }
    if (v9x_i9xx_first[0] != 0x7ffc0001ul ||
        v9x_i9xx_first[1] != 0ul || v9x_i9xx_first[2] != 0ul ||
        v9x_i9xx_first[3] != 0ul || v9x_i9xx_first[4] != 0ul ||
        v9x_i9xx_gtt_hash_a != 0x4d8707c5ul ||
        v9x_i9xx_gtt_hash_b != 0x4d8707c5ul) { clean_pass = 0u; }
    if (!v9x_p4_ring("PostSnapshot", clean_pass != 0u ? "PASS" : "FAIL") ||
        !v9x_p4_ring("S12Result", clean_pass != 0u ? "PASS" : "FAIL")) {
        v9x_p4_uncertain("POST-LOG-FAILED"); return;
    }
    if (v9x_i9xx_phase4_sequence_commit(&sequence,
            V9X_I9XX_P4_POST_SNAPSHOT) != V9X_STATUS_OK) {
        v9x_p4_uncertain("ORDER-FAILED"); return;
    }
    if (!v9x_p4_ring("Result", clean_pass != 0u ? "PASS" :
        (blit_pass == 0u ? "BLIT-MISMATCH" : "POST-MISMATCH"))) {
        v9x_intel_boot_arm_latch = 0u;
        return;
    }
    v9x_intel_str_copy(last, clean_pass != 0u ? "pass:" :
        (blit_pass == 0u ? "fail:blit-mismatch:" : "fail:post-mismatch:"));
    v9x_intel_str_append(last, v9x_intel_boot_arm_token);
    if (!v9x_p4_profile("IntelLastResult", last) ||
        !v9x_p4_profile("IntelInFlight", "")) {
        v9x_intel_boot_arm_latch = 0u;
        return;
    }
    v9x_intel_boot_arm_latch = 0u;
#else
    (void)layout; (void)probe; (void)blt; (void)flush_stable;
#endif
}
