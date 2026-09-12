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

v9x_u32 v9x_i9xx_crc32_dwords(const v9x_u32 *stream,
                               v9x_u32 dword_count)
{
    v9x_u32 crc = 0xfffffffful;
    v9x_u32 index;
    if (stream == 0) { return 0ul; }
    for (index = 0ul; index < dword_count; ++index) {
        v9x_u32 value = stream[index];
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
