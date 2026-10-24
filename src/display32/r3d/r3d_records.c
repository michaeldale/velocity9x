#include "r3d_records.h"

int v9x_r3d_records_flush(V9X_R3D_RECORDS *run)
{
    v9x_u32 triangles = run->pending;
    v9x_u32 records = run->record_count;
    v9x_u32 record, first = 0ul;
    int accepted;
    run->pending = 0ul;
    run->record_count = 0ul;
    if (triangles == 0ul) {
        return 1;
    }
    accepted = run->batch(run->user, run->vertices, triangles);
    if (accepted < 0 && records > 1ul) {
        /* Only an atomic pre-submit rejection may request this. Other
         * failures can have drawn a prefix; replay would draw it twice. */
        for (record = 0ul; record < records; ++record) {
            v9x_u32 end = run->record_ends[record];
            (void)run->batch(run->user, run->vertices + first * 3ul,
                             end - first);
            first = end;
        }
    }
    return accepted > 0;
}

/* One vertex, and its second-unit pair when there is one, into slot `at`
 * of the run's storage. */
static void v9x_r3d_records_put(V9X_R3D_RECORDS *run, v9x_u32 at,
                                const V9X_R3D_VERTEX *vertex,
                                const float *uv1)
{
    run->vertices[at] = *vertex;
    if (uv1 != 0) {
        run->texcoords1[at * 2ul] = uv1[0];
        run->texcoords1[at * 2ul + 1ul] = uv1[1];
    }
}

static int v9x_r3d_records_list(V9X_R3D_RECORDS *run,
                                const V9X_R3D_VERTEX *vertices,
                                const float *uv1, v9x_u32 triangles)
{
    v9x_u32 i;
    int ok = 1;
    if (run->pending == 0ul) { run->record_count = 0ul; }
    if (triangles > run->capacity - run->pending) {
        ok = v9x_r3d_records_flush(run);
    }
    /* An oversized record stands alone; the list sink handles its bound.
     * With pairs it cannot: the sink finds them only beside the storage,
     * so the list goes through it a run at a time, each piece a record. */
    if (triangles > run->capacity) {
        if (uv1 != 0) {
            while (triangles != 0ul) {
                v9x_u32 piece = triangles > run->capacity ? run->capacity
                                                          : triangles;
                if (!v9x_r3d_records_list(run, vertices, uv1, piece)) {
                    ok = 0;
                }
                vertices += piece * 3ul;
                uv1 += piece * 3ul * 2ul;
                triangles -= piece;
            }
            return ok;
        }
        if (run->batch(run->user, vertices, triangles) <= 0) {
            ok = 0;
        }
        return ok;
    }
    for (i = 0ul; i < triangles * 3ul; ++i) {
        v9x_r3d_records_put(run, run->pending * 3ul + i, &vertices[i],
                            uv1 != 0 ? uv1 + i * 2ul : 0);
    }
    run->pending += triangles;
    if (triangles != 0ul) {
        run->record_ends[run->record_count++] = run->pending;
    }
    return ok;
}

static int v9x_r3d_records_fan(V9X_R3D_RECORDS *run,
                               const V9X_R3D_VERTEX *vertices,
                               const float *uv1, v9x_u32 count)
{
    v9x_u32 apex;
    v9x_u32 triangles;
    v9x_u32 at;
    int ok = 1;
    if (count < 3ul || run->capacity == 0ul) {
        return 0;
    }
    triangles = count - 2ul;
    if (run->pending == 0ul) { run->record_count = 0ul; }
    if (triangles > run->capacity - run->pending) {
        ok = v9x_r3d_records_flush(run);
    }
    for (apex = 1ul; apex + 1ul < count; ++apex) {
        if (run->pending == run->capacity) {
            run->record_ends[run->record_count++] = run->pending;
            if (!v9x_r3d_records_flush(run)) {
                /* Preserve the old oversized fan path: a refused chunk ends
                 * this record, while the caller can append later records. */
                return 0;
            }
        }
        at = run->pending * 3ul;
        v9x_r3d_records_put(run, at, &vertices[0], uv1);
        v9x_r3d_records_put(run, at + 1ul, &vertices[apex],
                            uv1 != 0 ? uv1 + apex * 2ul : 0);
        v9x_r3d_records_put(run, at + 2ul, &vertices[apex + 1ul],
                            uv1 != 0 ? uv1 + (apex + 1ul) * 2ul : 0);
        ++run->pending;
    }
    run->record_ends[run->record_count++] = run->pending;
    /* Do not join another record to an oversized fan's last chunk. */
    if (triangles > run->capacity && !v9x_r3d_records_flush(run)) {
        ok = 0;
    }
    return ok;
}

int v9x_r3d_records_append_list(V9X_R3D_RECORDS *run,
                               const V9X_R3D_VERTEX *vertices,
                               v9x_u32 triangles)
{
    return v9x_r3d_records_list(run, vertices, 0, triangles);
}

int v9x_r3d_records_append_fan(V9X_R3D_RECORDS *run,
                              const V9X_R3D_VERTEX *vertices,
                              v9x_u32 count)
{
    return v9x_r3d_records_fan(run, vertices, 0, count);
}

int v9x_r3d_records_append_list_uv1(V9X_R3D_RECORDS *run,
                                    const V9X_R3D_VERTEX *vertices,
                                    const float *uv1, v9x_u32 triangles)
{
    return v9x_r3d_records_list(run, vertices, uv1, triangles);
}

int v9x_r3d_records_append_fan_uv1(V9X_R3D_RECORDS *run,
                                   const V9X_R3D_VERTEX *vertices,
                                   const float *uv1, v9x_u32 count)
{
    return v9x_r3d_records_fan(run, vertices, uv1, count);
}
