/*
 * The signed release file. See update_release.h.
 *
 * No C library: this links into V9XUPD.EXE, which has no runtime.
 */
#include "velocity9x/update_release.h"
#include "velocity9x/update_proto.h"
#include "velocity9x/ed25519.h"

#define V9X_RELEASE_SECTION    "Velocity9xRelease"
#define V9X_RELEASE_SIGNATURE  "[Signature]"
#define V9X_RELEASE_KEY_NAME   "Ed25519="
#define V9X_RELEASE_SIG_HEX    128u
/* "Family." and the longest family name, as a section name. */
#define V9X_RELEASE_SECTION_MAX (8u + V9X_RELEASE_FAMILY_MAX)

static v9x_s32 v9x_release_hex_value(char value)
{
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

static v9x_u16 v9x_release_hex_bytes(const char *hex, v9x_u8 *bytes,
                                     v9x_u32 count)
{
    v9x_u32 index;

    for (index = 0u; index < count; ++index) {
        v9x_s32 high = v9x_release_hex_value(hex[index * 2u]);
        v9x_s32 low;

        if (high < 0) {
            return V9X_FALSE;
        }
        low = v9x_release_hex_value(hex[index * 2u + 1u]);
        if (low < 0) {
            return V9X_FALSE;
        }
        bytes[index] = (v9x_u8)((high << 4) | low);
    }
    return hex[count * 2u] == '\0' ? V9X_TRUE : V9X_FALSE;
}

v9x_u16 v9x_release_hex_digest(const char *hex, v9x_u8 digest[32])
{
    return v9x_release_hex_bytes(hex, digest, 32u);
}

/* Whether text at position starts the given literal. */
static v9x_u16 v9x_release_at(const char *text, v9x_u32 length,
                              v9x_u32 position, const char *literal)
{
    v9x_u32 index;

    for (index = 0u; literal[index] != '\0'; ++index) {
        if (position + index >= length ||
            text[position + index] != literal[index]) {
            return V9X_FALSE;
        }
    }
    return V9X_TRUE;
}

v9x_u16 v9x_release_verify(const char *text, v9x_u32 length,
                           const v9x_u8 public_key[32],
                           v9x_u32 *signed_length)
{
    v9x_u32 position;
    v9x_u32 section = 0xFFFFFFFFul;
    v9x_u32 hex_start;
    v9x_u32 hex_length = 0u;
    char hex[V9X_RELEASE_SIG_HEX + 1u];
    v9x_u8 signature[64];

    for (position = 0u; position < length; ++position) {
        if (text[position] == '\0') {
            return V9X_FALSE;
        }
        /* [Signature] at the start of a line, exactly once. */
        if ((position == 0u || text[position - 1u] == '\n') &&
            v9x_release_at(text, length, position, V9X_RELEASE_SIGNATURE)) {
            if (section != 0xFFFFFFFFul) {
                return V9X_FALSE;
            }
            section = position;
        }
    }
    if (section == 0xFFFFFFFFul) {
        return V9X_FALSE;
    }

    /* The section line, its line end, then Ed25519=<hex> and at most one
     * line end: the whole remainder of the file, nothing else. */
    position = section + sizeof(V9X_RELEASE_SIGNATURE) - 1u;
    if (position < length && text[position] == '\r') {
        ++position;
    }
    if (position >= length || text[position] != '\n') {
        return V9X_FALSE;
    }
    ++position;
    if (!v9x_release_at(text, length, position, V9X_RELEASE_KEY_NAME)) {
        return V9X_FALSE;
    }
    hex_start = position + sizeof(V9X_RELEASE_KEY_NAME) - 1u;
    while (hex_start + hex_length < length &&
           text[hex_start + hex_length] != '\r' &&
           text[hex_start + hex_length] != '\n') {
        if (hex_length == V9X_RELEASE_SIG_HEX) {
            return V9X_FALSE;
        }
        hex[hex_length] = text[hex_start + hex_length];
        ++hex_length;
    }
    if (hex_length != V9X_RELEASE_SIG_HEX) {
        return V9X_FALSE;
    }
    hex[hex_length] = '\0';
    position = hex_start + hex_length;
    if (position < length && text[position] == '\r') {
        ++position;
    }
    if (position < length && text[position] == '\n') {
        ++position;
    }
    if (position != length) {
        return V9X_FALSE;
    }
    if (!v9x_release_hex_bytes(hex, signature, 64u)) {
        return V9X_FALSE;
    }
    if (!v9x_ed25519_verify(signature, public_key, (const v9x_u8 *)text,
                            section)) {
        return V9X_FALSE;
    }
    *signed_length = section;
    return V9X_TRUE;
}

/* A plain file name: letters, digits, dot, hyphen, underscore, and no
 * leading dot, so it cannot name a directory or climb out of one. */
static v9x_u16 v9x_release_plain_name(const char *name)
{
    v9x_u32 index;

    if (name[0] == '\0' || name[0] == '.') {
        return V9X_FALSE;
    }
    for (index = 0u; name[index] != '\0'; ++index) {
        char value = name[index];

        if (!((value >= 'A' && value <= 'Z') ||
              (value >= 'a' && value <= 'z') ||
              (value >= '0' && value <= '9') ||
              value == '.' || value == '-' || value == '_')) {
            return V9X_FALSE;
        }
    }
    return V9X_TRUE;
}

static v9x_u16 v9x_release_parse_u32(const char *text, v9x_u32 *value)
{
    v9x_u32 result = 0u;

    if (*text == '\0') {
        return V9X_FALSE;
    }
    for (; *text != '\0'; ++text) {
        if (*text < '0' || *text > '9' || result > 0x19999998ul) {
            return V9X_FALSE;
        }
        result = result * 10u + (v9x_u32)(*text - '0');
    }
    *value = result;
    return V9X_TRUE;
}

v9x_u16 v9x_release_read(const char *text, v9x_u32 signed_length,
                         const char *app, const char *family,
                         struct v9x_release_info *info)
{
    /* Room for the longest field read through it: 64 hex digits. */
    char value[72];
    char section[V9X_RELEASE_SECTION_MAX];
    v9x_u32 major;
    v9x_u32 minor;
    v9x_u32 patch;
    v9x_u32 index;

    if (!v9x_update_ini_value(text, signed_length, V9X_RELEASE_SECTION,
                              "Schema", value, sizeof(value)) ||
        value[0] != V9X_RELEASE_SCHEMA[0] || value[1] != '\0') {
        return V9X_FALSE;
    }
    if (!v9x_update_ini_value(text, signed_length, V9X_RELEASE_SECTION,
                              "App", value, sizeof(value))) {
        return V9X_FALSE;
    }
    for (index = 0u; app[index] != '\0' && value[index] == app[index];
         ++index) {
    }
    if (app[index] != '\0' || value[index] != '\0') {
        return V9X_FALSE;
    }
    if (!v9x_update_ini_value(text, signed_length, V9X_RELEASE_SECTION,
                              "Version", info->version,
                              sizeof(info->version)) ||
        !v9x_update_version_parse(info->version, &major, &minor, &patch)) {
        return V9X_FALSE;
    }
    if (!v9x_update_ini_value(text, signed_length, V9X_RELEASE_SECTION,
                              "Build", info->build, sizeof(info->build))) {
        info->build[0] = '\0';
    }

    /* [Family.<family>]: the family name is the INF's V9xFamily value, so
     * it is checked as a plain name before it becomes part of a lookup. */
    if (!v9x_release_plain_name(family)) {
        return V9X_FALSE;
    }
    section[0] = '\0';
    {
        static const char prefix[] = "Family.";
        v9x_u32 used = 0u;

        for (index = 0u; prefix[index] != '\0'; ++index) {
            section[used++] = prefix[index];
        }
        for (index = 0u; family[index] != '\0'; ++index) {
            if (used + 1u >= sizeof(section)) {
                return V9X_FALSE;
            }
            section[used++] = family[index];
        }
        section[used] = '\0';
    }
    if (!v9x_update_ini_value(text, signed_length, section, "File",
                              info->file, sizeof(info->file)) ||
        !v9x_release_plain_name(info->file)) {
        return V9X_FALSE;
    }
    if (!v9x_update_ini_value(text, signed_length, section, "Size", value,
                              sizeof(value)) ||
        !v9x_release_parse_u32(value, &info->size) || info->size == 0u) {
        return V9X_FALSE;
    }
    if (!v9x_update_ini_value(text, signed_length, section, "Sha256", value,
                              sizeof(value)) ||
        !v9x_release_hex_digest(value, info->sha256)) {
        return V9X_FALSE;
    }
    return V9X_TRUE;
}
