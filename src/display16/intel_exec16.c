#define SetCursor V9xUserSetCursor
#include <windows.h>
#undef SetCursor

#include <string.h>
#include "velocity9x/build.h"
#include "velocity9x/diagpaths.h"
#include "velocity9x/intel_gma.h"

#define V9X_P4_SECTION "IntelRing"
#define V9X_P4_INI     "SYSTEM.INI"
#define V9X_P4_GUARD   0xa5a5a5a5ul
#define V9X_P4_COLOR   0x55aa33ccul

extern WORD v9x_intel_boot_arm_latch;
extern DWORD v9x_intel_boot_arm_crc;
extern char v9x_intel_boot_arm_token[64];
extern DWORD v9x_i9xx_first[V9X_I9XX_SNAPSHOT_DWORDS];
extern DWORD v9x_i9xx_gtt_hash_a;
extern DWORD v9x_i9xx_gtt_hash_b;
extern DWORD v9x_i9xx_bsm;
extern DWORD v9x_i9xx_ring_memory_value;
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
extern void v9x_intel_publish_event(WORD kind, WORD context);
extern void v9x_intel_publish_mmio_fingerprint(void);
extern void v9x_intel_publish_gtt_inventory(void);

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
    if (!WritePrivateProfileString(section, key, value, path) ||
        !WritePrivateProfileString(0, 0, 0, path)) { return 0u; }
    length = (WORD)GetPrivateProfileString(section, key, "", check,
                                           sizeof(check), path);
    return length < sizeof(check) - 1u && strcmp(check, value) == 0;
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
        strcpy(key, prefix);
        key[strlen(prefix)] = (char)('0' + index);
        key[strlen(prefix) + 1u] = '\0';
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
        strcpy(key, prefix);
        key[strlen(prefix)] = (char)('0' + index / 10u);
        key[strlen(prefix) + 1u] = (char)('0' + index % 10u);
        key[strlen(prefix) + 2u] = '\0';
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
    strcpy(last, "refused:");
    strcat(last, reason);
    strcat(last, ":");
    strcat(last, v9x_intel_boot_arm_token);
    if (!v9x_p4_profile("IntelLastResult", last) ||
        !v9x_p4_profile("IntelEnableThisBoot", "0")) { return; }
    (void)v9x_p4_profile("IntelInFlight", "");
}

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
    if (!v9x_p4_read_profile("IntelInFlight", in_flight,
                              sizeof(in_flight)) ||
        !v9x_p4_read_profile("IntelEnableThisBoot", enabled,
                              sizeof(enabled)) ||
        !v9x_p4_read_profile("IntelArmCrc", crc_text,
                              sizeof(crc_text)) ||
        !v9x_p4_read_profile("IntelAccelDefault", accel_default,
                              sizeof(accel_default)) ||
        !v9x_p4_read_profile("IntelArmBuildId", arm_build,
                              sizeof(arm_build)) ||
        strcmp(arm_build, v9x_get_build_identity()->build_id) != 0 ||
        strcmp(accel_default, "0") != 0 ||
        v9x_i9xx_parse_crc_hex(crc_text, &crc) == V9X_FALSE) {
        return 0u;
    }
    request.token = v9x_intel_boot_arm_token;
    request.in_flight = in_flight;
    request.configured_crc = crc;
    request.packet_crc = v9x_i9xx_phase4_execution_crc(probe, blt);
    request.enable_this_boot = (WORD)(strcmp(enabled, "1") == 0);
    request.safe_mode = (WORD)(GetSystemMetrics(67) != 0);
    request.errata_gate = V9X_TRUE;
    request.vendor_id = 0x8086u;
    request.device_id = 0x27aeu;
    request.revision = V9xPciReadIntelRevision();
    request.phase = V9X_I9XX_PHASE4;
    if (v9x_i9xx_arm_evaluate(&request, &rejection) != V9X_STATUS_OK ||
        crc != v9x_intel_boot_arm_crc || flush_stable == 0u ||
        layout->reserve_offset != 0x00790000ul ||
        layout->reserve_physical != 0x7ff90000ul ||
        layout->scratch_offset != 0x007a1000ul ||
        v9x_i9xx_bsm != 0x7f800000ul ||
        v9x_i9xx_first[0] != 0x7ffc0001ul ||
        v9x_i9xx_first[1] != 0ul ||
        v9x_i9xx_first[2] != 0ul ||
        v9x_i9xx_first[3] != 0ul ||
        v9x_i9xx_first[4] != 0ul ||
        v9x_i9xx_gtt_hash_a != 0x4d8707c5ul ||
        v9x_i9xx_gtt_hash_b != 0x4d8707c5ul ||
        v9x_i9xx_event_count > V9X_I9XX_EVENT_MAX - 2u ||
        v9x_i9xx_event_dropped != 0u) { return 0u; }
    GetPrivateProfileString("IntelMmio", "Result", "", status,
                            sizeof(status), V9X_DIAG_INTELMM_TXT);
    if (strcmp(status, "PASS") != 0) { return 0u; }
    GetPrivateProfileString("IntelGtt", "Result", "", status,
                            sizeof(status), V9X_DIAG_INTELGTT_TXT);
    if (strcmp(status, "PASS") != 0 ||
        !V9xMiniI9xxRingDiag(4u) || v9x_i9xx_ring_diag_value != 0ul ||
        !V9xMiniI9xxRingDiag(6u) || v9x_i9xx_ring_diag_value != 0ul) {
        return 0u;
    }
    return 1u;
}

static WORD v9x_p4_stage(const struct v9x_i9xx_sandbox_layout *layout,
                          const DWORD *probe, const DWORD *blt)
{
    DWORD index;
    DWORD expected;
    for (index = 0ul; index < 10ul; ++index) {
        expected = index < 2ul ? probe[index] : blt[index - 2ul];
        if (V9xMiniI9xxRingStage(layout->reserve_physical, (WORD)index,
                                  expected) == 0u) { return 0u; }
    }
    for (index = 0ul; index < 10ul; ++index) {
        expected = index < 2ul ? probe[index] : blt[index - 2ul];
        if (V9xMiniI9xxRingMemory(index * 4ul) == 0u ||
            v9x_i9xx_ring_memory_value != expected ||
            V9xGmadrRead(layout->ring_offset + index * 4ul) != expected) {
            return 0u;
        }
    }
    if (V9xMiniI9xxRingMemory(0x11000ul) == 0u ||
        v9x_i9xx_ring_memory_value != V9X_P4_GUARD ||
        V9xGmadrRead(layout->scratch_offset) != V9X_P4_GUARD ||
        V9xMiniI9xxRingMemory(0x11ffcul) == 0u ||
        v9x_i9xx_ring_memory_value != V9X_P4_GUARD ||
        V9xGmadrRead(layout->scratch_offset + 0xffcul) != V9X_P4_GUARD) {
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
    if (step >= 6u && step <= 9u &&
        (v9x_i9xx_ring_free_space(head, head, V9X_I9XX_RING_BYTES,
                                   &free_bytes) != V9X_STATUS_OK ||
         free_bytes < needed)) { return 0u; }
    *p++ = 'S'; *p++ = (char)('0' + step / 10u);
    *p++ = (char)('0' + step % 10u);
    *p = '\0';
    if (!v9x_p4_ring("IntentStep", key)) { return 0u; }
    success = V9xMiniI9xxRingExecute(crc, step);
    strcpy(p, "Result");
    if (!v9x_p4_ring(key, success != 0u ? "PASS" : "FAIL")) { return 0u; }
    strcpy(p, "Head");
    if (!v9x_p4_ring_hex(key, v9x_i9xx_ring_exec_head)) { return 0u; }
    strcpy(p, "Tail");
    if (!v9x_p4_ring_hex(key, v9x_i9xx_ring_exec_tail)) { return 0u; }
    strcpy(p, "Ms");
    if (!v9x_p4_ring_hex(key, v9x_i9xx_ring_exec_elapsed)) { return 0u; }
    strcpy(p, "Polls");
    if (!v9x_p4_ring_hex(key, v9x_i9xx_ring_exec_polls)) { return 0u; }
    strcpy(p, "Failure");
    if (!v9x_p4_ring_hex(key, v9x_i9xx_ring_exec_failure)) { return 0u; }
    if (!V9xMiniI9xxRingDiag(7u)) { return 0u; }
    strcpy(p, "Ctl");
    if (!v9x_p4_ring_hex(key, v9x_i9xx_ring_diag_value)) { return 0u; }
    if (!V9xMiniI9xxRingDiag(8u)) { return 0u; }
    strcpy(p, "Start");
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
    DWORD pre_mmio[V9X_I9XX_SNAPSHOT_DWORDS];
    DWORD pre_errors[7];
    DWORD post_errors[7];
    char crc_text[9];
    char last[96];
    if (v9x_intel_boot_arm_latch == 0u) { return; }
    v9x_i9xx_phase4_sequence_begin(&sequence);
    if (!v9x_p4_preflight(layout, probe, blt, flush_stable)) {
        v9x_p4_clean_refusal("PRECONDITION-REFUSED");
        return;
    }
    if (!v9x_p4_ring("Access", "armed-hardware-write") ||
        !v9x_p4_ring("ErrataGate", "1") ||
        !v9x_p4_ring("IntentBuildId", v9x_get_build_identity()->build_id) ||
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
    strcpy(last, clean_pass != 0u ? "pass:" :
        (blit_pass == 0u ? "fail:blit-mismatch:" : "fail:post-mismatch:"));
    strcat(last, v9x_intel_boot_arm_token);
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
