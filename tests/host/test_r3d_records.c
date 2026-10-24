#include <stdio.h>
#include <string.h>
#include "../../src/display32/r3d/r3d_records.h"

static unsigned int failures;
#define CHECK(e) do { if (!(e)) { ++failures; \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
} } while (0)

typedef struct capture {
    v9x_u32 calls;
    v9x_u32 sizes[8];
    v9x_u32 used;
    V9X_R3D_VERTEX vertices[600];
    int refuse;
    int retry_invalid;
} CAPTURE;

static int sink(void *user, const V9X_R3D_VERTEX *vertices,
                v9x_u32 triangles)
{
    CAPTURE *c = (CAPTURE *)user;
    v9x_u32 i;
    c->sizes[c->calls++] = triangles;
    for (i = 0ul; i < triangles * 3ul; ++i) {
        c->vertices[c->used++] = vertices[i];
    }
    if (c->retry_invalid) {
        for (i = 0ul; i < triangles * 3ul; ++i) {
            if (vertices[i].color == 0xbadul) { return -1; }
        }
    }
    return c->calls != (v9x_u32)c->refuse;
}

/* The second unit's coordinates as the sink finds them: beside the run's
 * storage, at the batch's offset into it. */
static const V9X_R3D_VERTEX *uv1_storage_base;
static const float *uv1_run;
static float uv1_seen[600 * 2];
static v9x_u32 uv1_used;
static v9x_u32 uv1_outside;

static int uv1_sink(void *user, const V9X_R3D_VERTEX *vertices,
                    v9x_u32 triangles)
{
    v9x_u32 at;
    v9x_u32 i;

    (void)user;
    if (vertices < uv1_storage_base || vertices >= uv1_storage_base + 192) {
        ++uv1_outside;
        return 1;
    }
    at = (v9x_u32)(vertices - uv1_storage_base);
    for (i = 0ul; i < triangles * 3ul * 2ul; ++i) {
        uv1_seen[uv1_used++] = uv1_run[at * 2ul + i];
    }
    return 1;
}

/* A run with texcoords1: each vertex's pair travels with it through a
 * list, a fan's repeated hub and a list longer than the run, which goes
 * through the storage in pieces rather than past it, so the sink always
 * finds the pairs beside the vertices it is given. */
static void test_texcoords1(void)
{
    V9X_R3D_VERTEX storage[192];
    float storage_uv1[192 * 2];
    V9X_R3D_VERTEX input[195];
    float input_uv1[195 * 2];
    V9X_R3D_RECORDS run;
    v9x_u32 i;

    memset(input, 0, sizeof(input));
    for (i = 0ul; i < 195ul; ++i) {
        input[i].color = i;
        input_uv1[i * 2ul] = (float)i;
        input_uv1[i * 2ul + 1ul] = (float)i + 0.5f;
    }
    run.vertices = storage;
    run.texcoords1 = storage_uv1;
    run.capacity = 64ul;
    run.pending = 0ul;
    run.record_count = 0ul;
    run.batch = uv1_sink;
    run.user = 0;
    uv1_storage_base = storage;
    uv1_run = storage_uv1;
    uv1_used = 0ul;
    uv1_outside = 0ul;

    CHECK(v9x_r3d_records_append_list_uv1(&run, input, input_uv1, 2ul));
    CHECK(v9x_r3d_records_append_fan_uv1(&run, input + 6, input_uv1 + 12,
                                         4ul));
    CHECK(v9x_r3d_records_flush(&run));
    CHECK(uv1_used == 4ul * 3ul * 2ul);
    for (i = 0ul; i < 6ul; ++i) {
        CHECK(uv1_seen[i * 2ul] == (float)i);
        CHECK(uv1_seen[i * 2ul + 1ul] == (float)i + 0.5f);
    }
    /* The fan's triangles: hub, k, k + 1. */
    CHECK(uv1_seen[12] == 6.0f && uv1_seen[14] == 7.0f &&
          uv1_seen[16] == 8.0f);
    CHECK(uv1_seen[18] == 6.0f && uv1_seen[20] == 8.0f &&
          uv1_seen[22] == 9.0f && uv1_seen[23] == 9.5f);

    uv1_used = 0ul;
    CHECK(v9x_r3d_records_append_list_uv1(&run, input, input_uv1, 65ul));
    CHECK(v9x_r3d_records_flush(&run));
    CHECK(uv1_outside == 0ul);
    CHECK(uv1_used == 65ul * 3ul * 2ul);
    for (i = 0ul; i < 195ul; ++i) {
        CHECK(uv1_seen[i * 2ul] == (float)i);
    }
}

unsigned int v9x_run_r3d_records_tests(void)
{
    V9X_R3D_VERTEX storage[192];
    V9X_R3D_VERTEX input[195];
    CAPTURE c = {0};
    V9X_R3D_RECORDS run;
    v9x_u32 i;
    failures = 0u;
    for (i = 0ul; i < 195ul; ++i) {
        input[i].sx = (float)i;
        input[i].sy = 0.0f;
        input[i].sz = 0.5f;
        input[i].rhw = 1.0f;
        input[i].color = i;
        input[i].specular = 0ul;
        input[i].tu = 0.0f;
        input[i].tv = 0.0f;
    }
    run.vertices = storage; run.capacity = 64ul; run.pending = 0ul;
    run.record_count = 0ul;
    run.batch = sink; run.user = &c;
    CHECK(v9x_r3d_records_flush(&run)); CHECK(c.calls == 0ul);
    /* An atomic pre-submit refusal may request original-record replay. The
     * whole bad record stays refused, including its otherwise valid second
     * triangle; records on both sides survive in order. */
    memset(&c, 0, sizeof(c)); c.retry_invalid = 1;
    input[6].color = 0xbadul;
    CHECK(v9x_r3d_records_append_list(&run, input, 2ul));
    CHECK(v9x_r3d_records_append_list(&run, input + 6, 2ul));
    CHECK(v9x_r3d_records_append_list(&run, input + 12, 1ul));
    CHECK(!v9x_r3d_records_flush(&run));
    CHECK(c.calls == 4ul);
    CHECK(c.sizes[0] == 5ul && c.sizes[1] == 2ul &&
          c.sizes[2] == 2ul && c.sizes[3] == 1ul);
    CHECK(c.vertices[15].color == 0ul && c.vertices[27].color == 12ul);
    input[6].color = 6ul;
    memset(&c, 0, sizeof(c));
    /* Ordinary failure can have emitted a prefix: never replay it. */
    c.refuse = 1;
    CHECK(v9x_r3d_records_append_list(&run, input, 2ul));
    CHECK(v9x_r3d_records_append_list(&run, input + 6, 2ul));
    CHECK(!v9x_r3d_records_flush(&run));
    CHECK(c.calls == 1ul && c.sizes[0] == 4ul);
    memset(&c, 0, sizeof(c));
    CHECK(v9x_r3d_records_append_list(&run, input, 1ul));
    CHECK(v9x_r3d_records_append_list(&run, input + 3, 1ul));
    CHECK(v9x_r3d_records_append_fan(&run, input + 6, 4ul));
    CHECK(v9x_r3d_records_append_list(&run, input + 10, 1ul));
    CHECK(c.calls == 0ul);
    CHECK(v9x_r3d_records_flush(&run));
    CHECK(c.calls == 1ul && c.sizes[0] == 5ul);
    CHECK(memcmp(c.vertices, input, 9u * sizeof(input[0])) == 0);
    for (i = 0ul; i < 9ul; ++i) CHECK(c.vertices[i].color == i);
    CHECK(c.vertices[9].color == 6ul);
    CHECK(c.vertices[10].color == 8ul && c.vertices[11].color == 9ul);
    CHECK(c.vertices[12].color == 10ul);
    c.calls = c.used = 0ul;
    CHECK(v9x_r3d_records_append_list(&run, input, 60ul));
    CHECK(v9x_r3d_records_append_list(&run, input + 180, 4ul));
    CHECK(v9x_r3d_records_append_list(&run, input + 192, 1ul));
    CHECK(v9x_r3d_records_flush(&run));
    CHECK(c.calls == 2ul && c.sizes[0] == 64ul && c.sizes[1] == 1ul);
    for (i = 0ul; i < 195ul; ++i) CHECK(c.vertices[i].color == i);
    CHECK(memcmp(c.vertices, input, sizeof(input)) == 0);
    c.calls = c.used = 0ul;
    CHECK(v9x_r3d_records_append_list(&run, input, 1ul));
    CHECK(v9x_r3d_records_append_list(&run, input, 65ul));
    CHECK(c.calls == 2ul && c.sizes[0] == 1ul && c.sizes[1] == 65ul);
    CHECK(run.pending == 0ul);
    c.calls = c.used = 0ul; c.refuse = 1;
    CHECK(v9x_r3d_records_append_list(&run, input, 1ul));
    CHECK(!v9x_r3d_records_append_fan(&run, input, 192ul));
    CHECK(c.calls == 4ul && c.sizes[0] == 1ul &&
          c.sizes[1] == 64ul && c.sizes[2] == 64ul && c.sizes[3] == 62ul);
    for (i = 0ul; i < 190ul; ++i) {
        CHECK(c.vertices[3ul + i * 3ul].color == 0ul);
        CHECK(c.vertices[4ul + i * 3ul].color == i + 1ul);
        CHECK(c.vertices[5ul + i * 3ul].color == i + 2ul);
    }
    CHECK(v9x_r3d_records_append_list(&run, input, 1ul));
    CHECK(v9x_r3d_records_flush(&run)); CHECK(c.calls == 5ul);
    c.calls = c.used = 0ul; c.refuse = 1;
    CHECK(v9x_r3d_records_append_list(&run, input, 64ul));
    CHECK(!v9x_r3d_records_append_list(&run, input, 1ul));
    CHECK(run.pending == 1ul);
    CHECK(v9x_r3d_records_flush(&run));
    CHECK(c.calls == 2ul && c.sizes[0] == 64ul && c.sizes[1] == 1ul);
    c.calls = c.used = 0ul;
    CHECK(!v9x_r3d_records_append_fan(&run, input, 192ul));
    CHECK(c.calls == 1ul && c.sizes[0] == 64ul && run.pending == 0ul);
    CHECK(v9x_r3d_records_append_list(&run, input, 1ul));
    CHECK(v9x_r3d_records_flush(&run)); CHECK(c.calls == 2ul);
    test_texcoords1();
    if (failures == 0u) puts("PASS: DrawPrimitives record merging");
    return failures;
}
