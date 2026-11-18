#ifndef VELOCITY9X_R3D_RECORDS_H
#define VELOCITY9X_R3D_RECORDS_H

#include "r3d.h"

/* The longest run: one record end per triangle at most. It was 64, the
 * one engine bound there was, and a 512-triangle run of one-triangle
 * records wrote past record_ends (V9XD32 T13 on the Oland, boot 100). */
#define V9X_R3D_RECORDS_MAX 512u

/* Caller-owned storage, at most V9X_R3D_RECORDS_MAX triangles (a larger
 * capacity is refused). Sink >0 means accepted, 0
 * refuses without retry, and -1 guarantees no submission and requests replay
 * at original record boundaries. A replayed record is never retried again.
 *
 * texcoords1 is the second texture unit's storage, two floats a vertex
 * beside `vertices`, written only by the _uv1 appends: a sink finds a
 * batch's pairs at the batch's offset into `vertices`. */
typedef struct v9x_r3d_records {
    V9X_R3D_VERTEX *vertices;
    v9x_u32 capacity;
    v9x_u32 pending;
    V9X_R3D_BATCH_FN batch;
    void *user;
    v9x_u32 record_count;
    v9x_u32 record_ends[V9X_R3D_RECORDS_MAX];
    float *texcoords1;
} V9X_R3D_RECORDS;

int v9x_r3d_records_flush(V9X_R3D_RECORDS *run);
int v9x_r3d_records_append_list(V9X_R3D_RECORDS *run,
                               const V9X_R3D_VERTEX *vertices,
                               v9x_u32 triangles);
int v9x_r3d_records_append_fan(V9X_R3D_RECORDS *run,
                              const V9X_R3D_VERTEX *vertices,
                              v9x_u32 count);

/* The same with each vertex's second-unit pair, two floats a vertex in
 * uv1, copied into run->texcoords1. A list longer than the run goes through
 * the storage a run at a time, so every batch's pairs are beside it. */
int v9x_r3d_records_append_list_uv1(V9X_R3D_RECORDS *run,
                                    const V9X_R3D_VERTEX *vertices,
                                    const float *uv1, v9x_u32 triangles);
int v9x_r3d_records_append_fan_uv1(V9X_R3D_RECORDS *run,
                                   const V9X_R3D_VERTEX *vertices,
                                   const float *uv1, v9x_u32 count);

#endif
