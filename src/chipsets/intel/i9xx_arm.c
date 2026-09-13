#include "velocity9x/intel_gma.h"

static v9x_u16 v9x_i9xx_token_length(const char *text)
{
    v9x_u16 length = 0u;
    if (text == 0) { return 0u; }
    while (text[length] != '\0' && length <= V9X_I9XX_ARM_TOKEN_MAX) {
        char ch = text[length];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' ||
              ch == '-')) {
            return 0u;
        }
        ++length;
    }
    if (length == 0u || length > V9X_I9XX_ARM_TOKEN_MAX) { return 0u; }
    return length;
}

static v9x_u16 v9x_i9xx_token_equal(const char *left, const char *right,
                                     v9x_u16 length)
{
    v9x_u16 index;
    if (left == 0 || right == 0 || right[length] != '\0') {
        return V9X_FALSE;
    }
    for (index = 0u; index < length; ++index) {
        if (left[index] != right[index]) { return V9X_FALSE; }
    }
    return V9X_TRUE;
}

v9x_u16 v9x_i9xx_token_valid(const char *text)
{
    return v9x_i9xx_token_length(text) != 0u;
}

v9x_u16 v9x_i9xx_parse_crc_hex(const char *text, v9x_u32 *value)
{
    v9x_u32 parsed = 0ul;
    v9x_u16 index;
    if (text == 0 || value == 0) { return V9X_FALSE; }
    *value = 0ul;
    for (index = 0u; index < 8u; ++index) {
        char ch = text[index];
        v9x_u16 digit;
        if (ch >= '0' && ch <= '9') { digit = (v9x_u16)(ch - '0'); }
        else if (ch >= 'A' && ch <= 'F') {
            digit = (v9x_u16)(ch - 'A' + 10);
        } else if (ch >= 'a' && ch <= 'f') {
            digit = (v9x_u16)(ch - 'a' + 10);
        } else { return V9X_FALSE; }
        parsed = (parsed << 4) | digit;
    }
    if (text[8] != '\0' || parsed == 0ul) { return V9X_FALSE; }
    *value = parsed;
    return V9X_TRUE;
}

static v9x_u32 v9x_i9xx_crc32_update(v9x_u32 crc, v9x_u32 value)
{
    v9x_u16 byte_index;
    for (byte_index = 0u; byte_index < 4u; ++byte_index) {
        v9x_u16 bit;
        crc ^= value & 0xfful;
        for (bit = 0u; bit < 8u; ++bit) {
            crc = (crc >> 1) ^
                ((crc & 1ul) != 0ul ? 0xedb88320ul : 0ul);
        }
        value >>= 8;
    }
    return crc;
}

v9x_u32 v9x_i9xx_crc32_dwords(const v9x_u32 *stream,
                               v9x_u32 dword_count)
{
    v9x_u32 crc = 0xfffffffful;
    v9x_u32 index;
    if (stream == 0) { return 0ul; }
    for (index = 0ul; index < dword_count; ++index) {
        crc = v9x_i9xx_crc32_update(crc, stream[index]);
    }
    return crc ^ 0xfffffffful;
}

v9x_u32 v9x_i9xx_phase4_execution_crc(const v9x_u32 *probe,
                                        const v9x_u32 *blt)
{
    v9x_u32 crc = 0xfffffffful;
    v9x_u32 index;
    if (probe == 0 || blt == 0) { return 0ul; }
    for (index = 0ul; index < 2ul; ++index) {
        crc = v9x_i9xx_crc32_update(crc, probe[index]);
    }
    for (index = 0ul; index <
         (V9X_I9XX_RING_BYTES - V9X_I9XX_RING_GUARD_BYTES) / 4ul;
         ++index) {
        crc = v9x_i9xx_crc32_update(crc, V9X_I9XX_MI_NOOP);
    }
    for (index = 0ul; index < 2ul; ++index) {
        crc = v9x_i9xx_crc32_update(crc, probe[index]);
    }
    for (index = 0ul; index < 8ul; ++index) {
        crc = v9x_i9xx_crc32_update(crc, blt[index]);
    }
    return crc ^ 0xfffffffful;
}

v9x_status v9x_i9xx_arm_evaluate(
    const struct v9x_i9xx_arm_request *request, v9x_u16 *rejection)
{
    v9x_u16 token_length;
    if (rejection == 0) { return V9X_STATUS_INVALID_ARGUMENT; }
    *rejection = V9X_I9XX_ARM_REJECT_NONE;
    if (request == 0) { return V9X_STATUS_INVALID_ARGUMENT; }
    if (request->enable_this_boot != 1u) {
        *rejection = V9X_I9XX_ARM_REJECT_DISABLED;
        return V9X_STATUS_INVALID_STATE;
    }
    if (request->safe_mode != V9X_FALSE) {
        *rejection = V9X_I9XX_ARM_REJECT_SAFE_MODE;
        return V9X_STATUS_INVALID_STATE;
    }
    if (request->errata_gate != V9X_TRUE) {
        *rejection = V9X_I9XX_ARM_REJECT_ERRATA;
        return V9X_STATUS_UNSUPPORTED;
    }
    if (request->vendor_id != 0x8086u || request->device_id != 0x27aeu ||
        request->revision != 0x0003u) {
        *rejection = V9X_I9XX_ARM_REJECT_IDENTITY;
        return V9X_STATUS_UNSUPPORTED;
    }
    if (request->phase != V9X_I9XX_PHASE4) {
        *rejection = V9X_I9XX_ARM_REJECT_PHASE;
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    token_length = v9x_i9xx_token_length(request->token);
    if (token_length == 0u ||
        v9x_i9xx_token_equal(request->token, request->in_flight,
                             token_length) == V9X_FALSE) {
        *rejection = V9X_I9XX_ARM_REJECT_TOKEN;
        return V9X_STATUS_INVALID_STATE;
    }
    if (request->packet_crc == 0ul ||
        request->packet_crc != request->configured_crc) {
        *rejection = V9X_I9XX_ARM_REJECT_CRC;
        return V9X_STATUS_INVALID_STATE;
    }
    return V9X_STATUS_OK;
}

void v9x_i9xx_phase4_sequence_begin(struct v9x_i9xx_phase4_sequence *state)
{
    if (state == 0) { return; }
    state->completed_step = 0u;
    state->poisoned = V9X_FALSE;
}

v9x_status v9x_i9xx_phase4_sequence_commit(
    struct v9x_i9xx_phase4_sequence *state, v9x_u16 step)
{
    if (state == 0) { return V9X_STATUS_INVALID_ARGUMENT; }
    if (state->poisoned != V9X_FALSE ||
        step < V9X_I9XX_P4_PREFLIGHT ||
        step > V9X_I9XX_P4_POST_SNAPSHOT ||
        step != state->completed_step + 1u) {
        state->poisoned = V9X_TRUE;
        return V9X_STATUS_INVALID_STATE;
    }
    state->completed_step = step;
    return V9X_STATUS_OK;
}

void v9x_i9xx_phase4_sequence_poison(
    struct v9x_i9xx_phase4_sequence *state)
{
    if (state != 0) { state->poisoned = V9X_TRUE; }
}
