/*
 * Velocity9x update-server protocol: the pure half. See update_proto.h.
 *
 * No C library: V9XUPD.EXE links without a runtime, so the few string
 * helpers this needs are the static loops below.
 */
#include "velocity9x/update_proto.h"

/* Crockford base-32, as the server draws report codes from it. */
static const char v9x_update_crockford[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

#define V9X_UPDATE_REPORT_CODE_PREFIX "V9X-"
#define V9X_UPDATE_REPORT_CODE_CHARS  6u

static char v9x_update_lower(char value)
{
    if (value >= 'A' && value <= 'Z') {
        return (char)(value - 'A' + 'a');
    }
    return value;
}

static v9x_u16 v9x_update_is_space(char value)
{
    if (value == ' ' || value == '\t' || value == '\r') {
        return V9X_TRUE;
    }
    return V9X_FALSE;
}

static v9x_u16 v9x_update_is_digit(char value)
{
    if (value >= '0' && value <= '9') {
        return V9X_TRUE;
    }
    return V9X_FALSE;
}

/* Equal without case over exactly length bytes of left, and right ends
 * there too. */
static v9x_u16 v9x_update_span_equals(const char *left, v9x_u32 length,
                                      const char *right)
{
    v9x_u32 index;

    for (index = 0u; index < length; ++index) {
        if (right[index] == '\0') {
            return V9X_FALSE;
        }
        if (v9x_update_lower(left[index]) != v9x_update_lower(right[index])) {
            return V9X_FALSE;
        }
    }
    if (right[length] != '\0') {
        return V9X_FALSE;
    }
    return V9X_TRUE;
}

v9x_u16 v9x_update_url_encode(const char *text, char *output,
                              v9x_u32 capacity)
{
    static const char hex[] = "0123456789ABCDEF";
    v9x_u32 used = 0u;
    v9x_u8 value;

    if (capacity == 0u) {
        return V9X_FALSE;
    }
    output[0] = '\0';
    for (; *text != '\0'; ++text) {
        value = (v9x_u8)*text;
        /* RFC 3986 2.3 unreserved: ALPHA / DIGIT / "-" / "." / "_" / "~". */
        if ((value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') ||
            (value >= '0' && value <= '9') || value == '-' || value == '.' ||
            value == '_' || value == '~') {
            if (used + 1u >= capacity) {
                output[0] = '\0';
                return V9X_FALSE;
            }
            output[used++] = (char)value;
            continue;
        }
        if (used + 3u >= capacity) {
            output[0] = '\0';
            return V9X_FALSE;
        }
        output[used++] = '%';
        output[used++] = hex[value >> 4];
        output[used++] = hex[value & 0x0Fu];
    }
    output[used] = '\0';
    return V9X_TRUE;
}

v9x_u16 v9x_update_ini_value(const char *text, v9x_u32 length,
                             const char *section, const char *key,
                             char *output, v9x_u32 capacity)
{
    v9x_u32 position = 0u;
    v9x_u16 in_section = V9X_FALSE;

    if (capacity == 0u) {
        return V9X_FALSE;
    }
    output[0] = '\0';
    while (position < length) {
        v9x_u32 start = position;
        v9x_u32 end;
        v9x_u32 equals;

        while (position < length && text[position] != '\n') {
            ++position;
        }
        end = position;
        if (position < length) {
            ++position;
        }
        while (start < end && v9x_update_is_space(text[start])) {
            ++start;
        }
        while (end > start && v9x_update_is_space(text[end - 1u])) {
            --end;
        }
        if (start == end) {
            continue;
        }

        if (text[start] == '[') {
            in_section = V9X_FALSE;
            if (text[end - 1u] == ']' &&
                v9x_update_span_equals(text + start + 1u,
                                       end - start - 2u, section)) {
                in_section = V9X_TRUE;
            }
            continue;
        }
        if (!in_section) {
            continue;
        }

        for (equals = start; equals < end && text[equals] != '='; ++equals) {
        }
        if (equals == end) {
            continue;
        }
        {
            v9x_u32 key_end = equals;
            v9x_u32 value_start = equals + 1u;
            v9x_u32 index;

            while (key_end > start && v9x_update_is_space(text[key_end - 1u])) {
                --key_end;
            }
            if (!v9x_update_span_equals(text + start, key_end - start, key)) {
                continue;
            }
            while (value_start < end &&
                   v9x_update_is_space(text[value_start])) {
                ++value_start;
            }
            if (end - value_start + 1u > capacity) {
                return V9X_FALSE;
            }
            for (index = 0u; index < end - value_start; ++index) {
                output[index] = text[value_start + index];
            }
            output[index] = '\0';
            return V9X_TRUE;
        }
    }
    return V9X_FALSE;
}

v9x_u16 v9x_update_url_split(const char *url, struct v9x_update_url *url_out)
{
    static const char scheme[] = "http://";
    v9x_u32 index;
    v9x_u32 host_length = 0u;
    v9x_u32 path_length = 0u;
    v9x_u32 port = V9X_UPDATE_PORT;

    for (index = 0u; scheme[index] != '\0'; ++index) {
        if (v9x_update_lower(url[index]) != scheme[index]) {
            return V9X_FALSE;
        }
    }
    url += index;

    while (*url != '\0' && *url != ':' && *url != '/') {
        char value = *url;

        if (!((value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') ||
              v9x_update_is_digit(value) || value == '.' || value == '-')) {
            return V9X_FALSE;
        }
        if (host_length + 1u >= V9X_UPDATE_HOST_MAX) {
            return V9X_FALSE;
        }
        url_out->host[host_length++] = value;
        ++url;
    }
    if (host_length == 0u) {
        return V9X_FALSE;
    }
    url_out->host[host_length] = '\0';

    if (*url == ':') {
        v9x_u32 digits = 0u;

        ++url;
        port = 0u;
        while (v9x_update_is_digit(*url)) {
            port = port * 10u + (v9x_u32)(*url - '0');
            if (++digits > 5u || port > 65535u) {
                return V9X_FALSE;
            }
            ++url;
        }
        if (digits == 0u || port == 0u) {
            return V9X_FALSE;
        }
    }
    url_out->port = (v9x_u16)port;

    if (*url == '\0') {
        url_out->path[0] = '/';
        url_out->path[1] = '\0';
        return V9X_TRUE;
    }
    if (*url != '/') {
        return V9X_FALSE;
    }
    while (*url != '\0') {
        /* Printable, non-space ASCII only: the path goes into a request
         * line verbatim, where a space or CR would end it early. */
        if ((v9x_u8)*url <= 0x20u || (v9x_u8)*url >= 0x7Fu) {
            return V9X_FALSE;
        }
        if (path_length + 1u >= V9X_UPDATE_PATH_MAX) {
            return V9X_FALSE;
        }
        url_out->path[path_length++] = *url;
        ++url;
    }
    url_out->path[path_length] = '\0';
    return V9X_TRUE;
}

v9x_u16 v9x_update_http_head(const char *raw, v9x_u32 length,
                             v9x_u32 *status, v9x_u32 *body_offset,
                             v9x_u32 *content_length)
{
    static const char version[] = "http/1.";
    static const char length_name[] = "content-length";
    v9x_u32 index;
    v9x_u32 head_end = 0u;
    v9x_u32 position;
    v9x_u32 code = 0u;

    /* The head ends at the first empty line; accept bare LF as well as
     * CRLF, as RFC 7230 3.5 recommends a recipient does. */
    for (index = 0u; index < length; ++index) {
        if (raw[index] != '\n') {
            continue;
        }
        if (index + 1u < length && raw[index + 1u] == '\n') {
            head_end = index + 2u;
            break;
        }
        if (index + 2u < length && raw[index + 1u] == '\r' &&
            raw[index + 2u] == '\n') {
            head_end = index + 3u;
            break;
        }
    }
    /* "HTTP/1.x NNN" and its line end at least, so the reads below stay
     * inside the head. */
    if (head_end < 14u) {
        return V9X_FALSE;
    }

    for (index = 0u; version[index] != '\0'; ++index) {
        if (v9x_update_lower(raw[index]) != version[index]) {
            return V9X_FALSE;
        }
    }
    /* "HTTP/1.x NNN": the minor digit, one space, three digits. */
    if (!v9x_update_is_digit(raw[index]) || raw[index + 1u] != ' ') {
        return V9X_FALSE;
    }
    for (position = index + 2u; position < index + 5u; ++position) {
        if (!v9x_update_is_digit(raw[position])) {
            return V9X_FALSE;
        }
        code = code * 10u + (v9x_u32)(raw[position] - '0');
    }
    *status = code;
    *body_offset = head_end;
    *content_length = V9X_UPDATE_NO_LENGTH;

    position = 0u;
    while (position < head_end) {
        v9x_u32 start = position;
        v9x_u32 name_length = 0u;

        while (position < head_end && raw[position] != '\n') {
            ++position;
        }
        ++position;
        while (start + name_length < position &&
               raw[start + name_length] != ':') {
            ++name_length;
        }
        if (start + name_length >= position ||
            name_length != sizeof(length_name) - 1u) {
            continue;
        }
        for (index = 0u; index < name_length; ++index) {
            if (v9x_update_lower(raw[start + index]) != length_name[index]) {
                break;
            }
        }
        if (index != name_length) {
            continue;
        }
        index = start + name_length + 1u;
        while (index < position && raw[index] == ' ') {
            ++index;
        }
        if (index >= position || !v9x_update_is_digit(raw[index])) {
            return V9X_FALSE;
        }
        {
            v9x_u32 value = 0u;

            while (index < position && v9x_update_is_digit(raw[index])) {
                /* 0x19999999 * 10 + 9 is the last value that fits. */
                if (value > 0x19999998ul) {
                    return V9X_FALSE;
                }
                value = value * 10u + (v9x_u32)(raw[index] - '0');
                ++index;
            }
            *content_length = value;
        }
    }
    return V9X_TRUE;
}

v9x_u16 v9x_update_report_code_valid(const char *code)
{
    static const char prefix[] = V9X_UPDATE_REPORT_CODE_PREFIX;
    v9x_u32 index;

    for (index = 0u; prefix[index] != '\0'; ++index) {
        if (code[index] != prefix[index]) {
            return V9X_FALSE;
        }
    }
    code += index;
    for (index = 0u; index < V9X_UPDATE_REPORT_CODE_CHARS; ++index) {
        const char *allowed = v9x_update_crockford;

        while (*allowed != '\0' && *allowed != code[index]) {
            ++allowed;
        }
        if (code[index] == '\0' || *allowed == '\0') {
            return V9X_FALSE;
        }
    }
    if (code[index] != '\0') {
        return V9X_FALSE;
    }
    return V9X_TRUE;
}

v9x_u16 v9x_update_version_parse(const char *text, v9x_u32 *major,
                                 v9x_u32 *minor, v9x_u32 *patch)
{
    v9x_u32 parts[3];
    v9x_u32 count = 0u;

    if (*text == 'v' || *text == 'V') {
        ++text;
    }
    for (;;) {
        v9x_u32 value = 0u;
        v9x_u32 digits = 0u;

        while (v9x_update_is_digit(*text)) {
            value = value * 10u + (v9x_u32)(*text - '0');
            if (++digits > 5u || value > 65535u) {
                return V9X_FALSE;
            }
            ++text;
        }
        if (digits == 0u) {
            return V9X_FALSE;
        }
        parts[count++] = value;
        if (*text == '\0') {
            break;
        }
        if (*text != '.' || count == 3u) {
            return V9X_FALSE;
        }
        ++text;
    }
    if (count < 2u) {
        return V9X_FALSE;
    }
    *major = parts[0];
    *minor = parts[1];
    *patch = (count == 3u) ? parts[2] : 0u;
    return V9X_TRUE;
}

v9x_s32 v9x_update_version_compare(const char *left, const char *right)
{
    v9x_u32 left_parts[3];
    v9x_u32 right_parts[3];
    v9x_u32 index;

    if (!v9x_update_version_parse(left, &left_parts[0], &left_parts[1],
                                  &left_parts[2]) ||
        !v9x_update_version_parse(right, &right_parts[0], &right_parts[1],
                                  &right_parts[2])) {
        return -2;
    }
    for (index = 0u; index < 3u; ++index) {
        if (left_parts[index] < right_parts[index]) {
            return -1;
        }
        if (left_parts[index] > right_parts[index]) {
            return 1;
        }
    }
    return 0;
}
