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

static char v9x_release_upper(char value)
{
    if (value >= 'a' && value <= 'z') {
        return (char)(value - 'a' + 'A');
    }
    return value;
}

/* Equal without case over exactly length bytes of left, and right ends
 * there too. */
static v9x_u16 v9x_release_span_equals(const char *left, v9x_u32 length,
                                       const char *right)
{
    v9x_u32 index;

    for (index = 0u; index < length; ++index) {
        if (right[index] == '\0' ||
            v9x_release_upper(left[index]) != v9x_release_upper(right[index])) {
            return V9X_FALSE;
        }
    }
    return right[length] == '\0' ? V9X_TRUE : V9X_FALSE;
}

static v9x_u32 v9x_release_length(const char *text)
{
    v9x_u32 length = 0u;

    while (text[length] != '\0') {
        ++length;
    }
    return length;
}

/* Copy a name that fits, else leave output empty. */
static v9x_u16 v9x_release_copy_name(char *output, v9x_u32 capacity,
                                     const char *name)
{
    v9x_u32 index;

    if (capacity == 0u) {
        return V9X_FALSE;
    }
    output[0] = '\0';
    if (v9x_release_length(name) >= capacity) {
        return V9X_FALSE;
    }
    for (index = 0u; name[index] != '\0'; ++index) {
        output[index] = name[index];
    }
    output[index] = '\0';
    return V9X_TRUE;
}

v9x_u16 v9x_release_pci_id(const char *hardware_key,
                           char pci_id[V9X_RELEASE_PCI_ID_MAX])
{
    /* '#' is one hex digit. */
    static const char pattern[] = "PCI\\VEN_####&DEV_####";
    v9x_u32 index;
    char next;

    pci_id[0] = '\0';
    for (index = 0u; pattern[index] != '\0'; ++index) {
        char value = v9x_release_upper(hardware_key[index]);

        if (pattern[index] == '#' ? v9x_release_hex_value(value) < 0
                                  : value != pattern[index]) {
            pci_id[0] = '\0';
            return V9X_FALSE;
        }
        pci_id[index] = value;
    }
    /* The device ID ends here: one more hex digit would be another ID. */
    next = hardware_key[index];
    if (next != '\0' && next != '&' && next != '\\') {
        pci_id[0] = '\0';
        return V9X_FALSE;
    }
    pci_id[index] = '\0';
    return V9X_TRUE;
}

/* Whether a HardwareIds value, start..end, lists pci_id as one whole
 * comma-separated entry. */
static v9x_u16 v9x_release_lists(const char *text, v9x_u32 start,
                                 v9x_u32 end, const char *pci_id)
{
    while (start < end) {
        v9x_u32 entry_end = start;
        v9x_u32 trimmed;

        while (entry_end < end && text[entry_end] != ',') {
            ++entry_end;
        }
        while (start < entry_end && text[start] == ' ') {
            ++start;
        }
        for (trimmed = entry_end; trimmed > start && text[trimmed - 1u] == ' ';
             --trimmed) {
        }
        if (v9x_release_span_equals(text + start, trimmed - start, pci_id)) {
            return V9X_TRUE;
        }
        start = entry_end + 1u;
    }
    return V9X_FALSE;
}

v9x_u16 v9x_release_family_for(const char *text, v9x_u32 signed_length,
                               const char *installed, const char *pci_id,
                               char *family, v9x_u32 capacity)
{
    static const char prefix[] = "[Family.";
    static const char key[] = "HardwareIds";
    char current[V9X_RELEASE_FAMILY_MAX];
    char chosen[V9X_RELEASE_FAMILY_MAX];
    v9x_u32 matches = 0u;
    v9x_u32 position = 0u;

    if (!v9x_release_copy_name(family, capacity, installed)) {
        return V9X_FALSE;
    }
    if (!v9x_release_span_equals(installed, v9x_release_length(installed),
                                 V9X_RELEASE_FALLBACK_FAMILY) ||
        pci_id[0] == '\0') {
        return V9X_FALSE;
    }

    current[0] = '\0';
    chosen[0] = '\0';
    while (position < signed_length) {
        v9x_u32 start = position;
        v9x_u32 end;
        v9x_u32 index;

        while (position < signed_length && text[position] != '\n') {
            ++position;
        }
        end = position;
        if (position < signed_length) {
            ++position;
        }
        while (end > start &&
               (text[end - 1u] == '\r' || text[end - 1u] == ' ')) {
            --end;
        }

        /* [Family.<name>]: remember the name while it is a plain one that
         * is not the fallback, which never claims a card from itself. */
        if (end > start && text[start] == '[') {
            v9x_u32 name_length = end - start - (sizeof(prefix) - 1u) - 1u;

            current[0] = '\0';
            if (end - start <= sizeof(prefix) ||
                text[end - 1u] != ']' ||
                !v9x_release_span_equals(text + start, sizeof(prefix) - 1u,
                                         "[family.") ||
                name_length >= sizeof(current)) {
                continue;
            }
            for (index = 0u; index < name_length; ++index) {
                current[index] = text[start + sizeof(prefix) - 1u + index];
            }
            current[name_length] = '\0';
            if (!v9x_release_plain_name(current) ||
                v9x_release_span_equals(current, name_length,
                                        V9X_RELEASE_FALLBACK_FAMILY)) {
                current[0] = '\0';
            }
            continue;
        }
        if (current[0] == '\0' || end - start <= sizeof(key) - 1u ||
            !v9x_release_span_equals(text + start, sizeof(key) - 1u, key) ||
            text[start + sizeof(key) - 1u] != '=') {
            continue;
        }
        if (v9x_release_lists(text, start + sizeof(key), end, pci_id)) {
            ++matches;
            (void)v9x_release_copy_name(chosen, sizeof(chosen), current);
        }
    }

    /* Exactly one family names the card, and its name fits. */
    if (matches != 1u || !v9x_release_copy_name(family, capacity, chosen)) {
        (void)v9x_release_copy_name(family, capacity, installed);
        return V9X_FALSE;
    }
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
