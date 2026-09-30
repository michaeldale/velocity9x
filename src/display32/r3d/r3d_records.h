#ifndef VELOCITY9X_R3D_RECORDS_H
#define VELOCITY9X_R3D_RECORDS_H

#include "r3d.h"

/* Caller-owned storage, at most 64 triangles. Sink >0 means accepted, 0
 * refuses without retry, and -1 guarantees no submission and requests replay
 * at original record boundaries. A replayed record is never retried again. */
typedef struct v9x_r3d_records {
    V9X_R3D_VERTEX *vertices;
    v9x_u32 capacity;
    v9x_u32 pending;
    V9X_R3D_BATCH_FN batch;
    void *user;
    v9x_u32 record_count;
    v9x_u32 record_ends[64];
} V9X_R3D_RECORDS;

int v9x_r3d_records_flush(V9X_R3D_RECORDS *run);
int v9x_r3d_records_append_list(V9X_R3D_RECORDS *run,
                               const V9X_R3D_VERTEX *vertices,
                               v9x_u32 triangles);
int v9x_r3d_records_append_fan(V9X_R3D_RECORDS *run,
                              const V9X_R3D_VERTEX *vertices,
                              v9x_u32 count);

#endif
