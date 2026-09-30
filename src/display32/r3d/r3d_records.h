#ifndef VELOCITY9X_R3D_RECORDS_H
#define VELOCITY9X_R3D_RECORDS_H

#include "r3d.h"

/* Caller-owned storage; capacity and pending are triangle counts. A sink
 * refusal clears the submitted run, and subsequent records still draw. */
typedef struct v9x_r3d_records {
    V9X_R3D_VERTEX *vertices;
    v9x_u32 capacity;
    v9x_u32 pending;
    V9X_R3D_BATCH_FN batch;
    void *user;
} V9X_R3D_RECORDS;

int v9x_r3d_records_flush(V9X_R3D_RECORDS *run);
int v9x_r3d_records_append_list(V9X_R3D_RECORDS *run,
                               const V9X_R3D_VERTEX *vertices,
                               v9x_u32 triangles);
int v9x_r3d_records_append_fan(V9X_R3D_RECORDS *run,
                              const V9X_R3D_VERTEX *vertices,
                              v9x_u32 count);

#endif
