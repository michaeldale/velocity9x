/*
 * The trace snapshot's identity rules. See include\velocity9x\diag_identity.h.
 *
 * No runtime string routines: the HAL links none.
 */
#include "velocity9x/diag_identity.h"

void v9x_diag_copy_text(char *out, const char *source, v9x_u32 bytes)
{
    v9x_u32 index = 0ul;

    if (out == 0 || bytes == 0ul) {
        return;
    }

    if (source != 0) {
        while (index + 1ul < bytes && source[index] != '\0') {
            out[index] = source[index];
            ++index;
        }
    }
    while (index < bytes) {
        out[index] = '\0';
        ++index;
    }
}

void v9x_diag_base_name(char *out, const char *path, v9x_u32 bytes)
{
    const char *base = path;
    const char *at;

    if (path != 0) {
        for (at = path; *at != '\0'; ++at) {
            if (*at == '\\' || *at == '/' || *at == ':') {
                base = at + 1;
            }
        }
    }
    v9x_diag_copy_text(out, base, bytes);
}

static char v9x_diag_upper(char value)
{
    if (value >= 'a' && value <= 'z') {
        return (char)(value - 'a' + 'A');
    }
    return value;
}

/*
 * Whether a stored name is `name`. Compared over the stored field only,
 * because a long name was cut to fit it, and the program has to match its
 * own row again rather than take a second one.
 */
static int v9x_diag_same_name(const char *stored, const char *name)
{
    v9x_u32 index;

    for (index = 0ul; index + 1ul < V9X_DIAG_PROCESS_NAME_BYTES; ++index) {
        if (v9x_diag_upper(stored[index]) != v9x_diag_upper(name[index])) {
            return 0;
        }
        if (stored[index] == '\0') {
            return 1;
        }
    }
    return 1;
}

void v9x_diag_note_process(struct v9x_diag_identity *identity,
                           const char *name, v9x_u32 kind,
                           v9x_u32 uptime_ms)
{
    struct v9x_diag_process *process = 0;
    v9x_u32 index;

    if (identity == 0 || name == 0 || name[0] == '\0') {
        return;
    }

    for (index = 0ul; index < identity->process_count &&
                      index < (v9x_u32)V9X_DIAG_PROCESS_SLOTS; ++index) {
        if (v9x_diag_same_name(identity->processes[index].name, name)) {
            process = &identity->processes[index];
            break;
        }
    }

    if (process == 0) {
        /* Full: counted, never written over the first programs, which are
         * the ones a report most often asks about. */
        if (identity->process_count >= (v9x_u32)V9X_DIAG_PROCESS_SLOTS) {
            ++identity->process_unrecorded;
            return;
        }
        process = &identity->processes[identity->process_count];
        ++identity->process_count;
        v9x_diag_copy_text(process->name, name,
                           (v9x_u32)V9X_DIAG_PROCESS_NAME_BYTES);
        process->d3d_contexts = 0ul;
        process->gl_describes = 0ul;
        process->first_uptime_ms = uptime_ms;
    }

    if (kind == V9X_DIAG_PROCESS_D3D) {
        ++process->d3d_contexts;
    } else if (kind == V9X_DIAG_PROCESS_GL) {
        ++process->gl_describes;
    }
}
