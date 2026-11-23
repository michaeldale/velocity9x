/*
 * When an appended diagnostic log is rolled over. See
 * include\velocity9x\diag_log.h for the logs it covers and why.
 */
#include "velocity9x/diag_log.h"

int v9x_diag_log_must_roll(v9x_u32 size, v9x_u32 adding, v9x_u32 limit)
{
    if (size == 0ul) {
        return 0;
    }

    /* Compared as limit - size so that size + adding cannot wrap. */
    if (size >= limit) {
        return 1;
    }

    return adding > limit - size;
}

int v9x_diag_log_old_path(const char *path, char *out, v9x_u32 capacity)
{
    static const char extension[] = V9X_DIAG_LOG_OLD_EXTENSION;
    v9x_u32 dot = 0ul;
    v9x_u32 length = 0ul;
    v9x_u32 index;
    int has_dot = 0;

    if (capacity == 0ul) {
        return 0;
    }
    out[0] = '\0';

    /* The last '.' after the last separator is the extension's start. */
    for (length = 0ul; path[length] != '\0'; ++length) {
        if (path[length] == '\\' || path[length] == '/') {
            has_dot = 0;
        } else if (path[length] == '.') {
            dot = length;
            has_dot = 1;
        }
    }
    if (!has_dot) {
        return 0;
    }

    /* The same name already: renaming it onto itself would destroy it. */
    for (index = 0ul; extension[index] != '\0'; ++index) {
        char c = path[dot + 1ul + index];

        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
        if (c != extension[index]) {
            break;
        }
    }
    if (extension[index] == '\0' && path[dot + 1ul + index] == '\0') {
        return 0;
    }

    if (dot + 1ul + (v9x_u32)(sizeof(extension) - 1u) + 1ul > capacity) {
        return 0;
    }
    for (index = 0ul; index <= dot; ++index) {
        out[index] = path[index];
    }
    for (index = 0ul; extension[index] != '\0'; ++index) {
        out[dot + 1ul + index] = extension[index];
    }
    out[dot + 1ul + index] = '\0';
    return 1;
}
