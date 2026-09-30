#include "r3d_records.h"

int v9x_r3d_records_flush(V9X_R3D_RECORDS *run)
{
    v9x_u32 triangles = run->pending;
    run->pending = 0ul;
    if (triangles == 0ul) {
        return 1;
    }
    return run->batch(run->user, run->vertices, triangles);
}

int v9x_r3d_records_append_list(V9X_R3D_RECORDS *run,
                               const V9X_R3D_VERTEX *vertices,
                               v9x_u32 triangles)
{
    v9x_u32 i;
    int ok = 1;
    if (triangles > run->capacity - run->pending) {
        ok = v9x_r3d_records_flush(run);
    }
    /* An oversized record stands alone; the list sink handles its bound. */
    if (triangles > run->capacity) {
        if (!run->batch(run->user, vertices, triangles)) {
            ok = 0;
        }
        return ok;
    }
    for (i = 0ul; i < triangles * 3ul; ++i) {
        run->vertices[run->pending * 3ul + i] = vertices[i];
    }
    run->pending += triangles;
    return ok;
}

int v9x_r3d_records_append_fan(V9X_R3D_RECORDS *run,
                              const V9X_R3D_VERTEX *vertices,
                              v9x_u32 count)
{
    v9x_u32 apex;
    v9x_u32 triangles;
    int ok = 1;
    if (count < 3ul || run->capacity == 0ul) {
        return 0;
    }
    triangles = count - 2ul;
    if (triangles > run->capacity - run->pending) {
        ok = v9x_r3d_records_flush(run);
    }
    for (apex = 1ul; apex + 1ul < count; ++apex) {
        if (run->pending == run->capacity &&
            !v9x_r3d_records_flush(run)) {
            /* Preserve the old oversized fan path: a refused chunk ends
             * this record, while the caller can append later records. */
            return 0;
        }
        run->vertices[run->pending * 3ul] = vertices[0];
        run->vertices[run->pending * 3ul + 1ul] = vertices[apex];
        run->vertices[run->pending * 3ul + 2ul] = vertices[apex + 1ul];
        ++run->pending;
    }
    /* Do not join another record to an oversized fan's last chunk. */
    if (triangles > run->capacity && !v9x_r3d_records_flush(run)) {
        ok = 0;
    }
    return ok;
}
