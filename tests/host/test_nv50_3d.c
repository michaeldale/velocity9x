#include <stdio.h>

#include "velocity9x/nv50_3d.h"

/*
 * The GT218 Tesla 3D setup push (src\chipsets\nvidia\gt218\nv50_3d.c)
 * against xf86-video-nouveau's NVAccelInitNV50TCL as
 * docs\specifications\gt218-tesla-3d-first-triangle.md lists it, and the
 * EXA programs it carries against nv50_accel.c's words.
 */

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

#define SCRATCH 0x08300000ul

static v9x_u32 words[V9X_NV50_3D_SETUP_WORDS_MAX];

/* The value of the first method header for method, or 0xffffffff. Each
 * header is followed by its count's data words. */
static v9x_u32 find_header(const struct v9x_nv50_push *push, v9x_u32 method,
                           v9x_u32 *data_at)
{
    v9x_u32 at = 0ul;

    while (at < push->count) {
        v9x_u32 header = push->words[at];
        v9x_u32 count = (header >> 18) & 0x7fful;
        v9x_u32 found = (header & 0x1ffcul);

        if (found == method) {
            *data_at = at + 1ul;
            return header;
        }
        at += 1ul + count;
    }
    return 0xfffffffful;
}

static void test_setup(void)
{
    struct v9x_nv50_push push;
    struct v9x_nv50_3d_scratch scratch;
    v9x_u32 header;
    v9x_u32 at = 0ul;
    v9x_u32 index;

    v9x_nv50_3d_scratch_at(SCRATCH, &scratch);
    CHECK(scratch.vp == SCRATCH + 0x0000ul);
    CHECK(scratch.fp == SCRATCH + 0x1000ul);
    CHECK(scratch.tic == SCRATCH + 0x2000ul);
    CHECK(scratch.tsc == SCRATCH + 0x3000ul);
    CHECK(scratch.vp_constants == SCRATCH + 0x4000ul);
    CHECK(scratch.fp_constants == SCRATCH + 0x4100ul);

    v9x_nv50_push_init(&push, words, V9X_NV50_3D_SETUP_WORDS_MAX);
    v9x_nv50_push_bind_3d(&push);
    v9x_nv50_push_3d_setup(&push, &scratch);
    CHECK(push.overflow == 0ul);

    /* The bind: subchannel 1, method 0, the 3D object's handle. */
    CHECK(words[0] == V9X_NV50_PUSH_HEADER(V9X_NV50_SUBC_3D, 0ul, 1ul));
    CHECK(words[1] == V9X_NV50_HANDLE_3D);

    /* Every method after the bind is on the 3D subchannel. */
    at = 2ul;
    while (at < push.count) {
        CHECK(((words[at] >> 13) & 7ul) == V9X_NV50_SUBC_3D);
        at += 1ul + ((words[at] >> 18) & 0x7fful);
    }
    CHECK(at == push.count);

    header = find_header(&push, 0x1558ul, &at);
    CHECK(header != 0xfffffffful && words[at] == 1ul);
    /* DMA_ZETA and the ten after it, then DMA_COLOR(0..7): the VM DMA
     * object each time (EXA's fifo->vram). */
    header = find_header(&push, 0x0184ul, &at);
    CHECK(((header >> 18) & 0x7fful) == 11ul);
    for (index = 0ul; index < 11ul; ++index) {
        CHECK(words[at + index] == V9X_NV50_HANDLE_DMA_VM);
    }
    header = find_header(&push, 0x01c0ul, &at);
    CHECK(((header >> 18) & 0x7fful) == 8ul);
    for (index = 0ul; index < 8ul; ++index) {
        CHECK(words[at + index] == V9X_NV50_HANDLE_DMA_VM);
    }
    /* DMA_NOTIFY is left out: EXA binds a null object we do not have. */
    CHECK(find_header(&push, 0x0180ul, &at) == 0xfffffffful);

    header = find_header(&push, 0x121cul, &at);
    CHECK(words[at] == 1ul);
    header = find_header(&push, 0x192cul, &at);
    CHECK(words[at] == 0ul);
    header = find_header(&push, 0x1574ul, &at);
    CHECK(((header >> 18) & 0x7fful) == 3ul);
    CHECK(words[at] == 0ul && words[at + 1ul] == scratch.tic &&
          words[at + 2ul] == 0x800ul);
    header = find_header(&push, 0x155cul, &at);
    CHECK(words[at + 1ul] == scratch.tsc && words[at + 2ul] == 0ul);
    header = find_header(&push, 0x13bcul, &at);
    CHECK(words[at] == 0x54ul);
    header = find_header(&push, 0x1650ul, &at);
    CHECK(words[at] == 3ul && words[at + 1ul] == 0x33ul);
    header = find_header(&push, 0x0f7cul, &at);
    CHECK(words[at] == 0ul && words[at + 1ul] == scratch.vp);
    header = find_header(&push, 0x0fa4ul, &at);
    CHECK(words[at] == 0ul && words[at + 1ul] == scratch.fp);
    header = find_header(&push, 0x16bcul, &at);
    CHECK(words[at] == 0x41400100ul && words[at + 1ul] == 0x05040302ul);
    header = find_header(&push, 0x1988ul, &at);
    CHECK(words[at] == 0x08040404ul && words[at + 1ul] == 8ul);
    header = find_header(&push, 0x0ff4ul, &at);
    CHECK(words[at] == (8192ul << 16) && words[at + 1ul] == (8192ul << 16));
}

/* nv50_accel.c: the vertex program is 30 instructions of two words, and
 * PFP_S three, the last of them one word long (constant checks at compile
 * time, where Open Watcom does not call them unreachable). */
typedef char test_nv50_3d_vp_words[(V9X_NV50_3D_EXA_VP_WORDS == 60ul) ? 1 : -1];
typedef char test_nv50_3d_fp_words[(V9X_NV50_3D_EXA_FP_S_WORDS == 6ul) ? 1 : -1];

static void test_programs(void)
{
    /* The first instruction "mov b32 o[0x0] a[0x0]", the last an exit. */
    CHECK(v9x_nv50_3d_exa_vp[0] == 0x10000001ul);
    CHECK(v9x_nv50_3d_exa_vp[1] == 0x0423c788ul);
    CHECK(v9x_nv50_3d_exa_vp[58] == 0xc0950215ul);
    CHECK(v9x_nv50_3d_exa_vp[59] == 0x00000789ul);
    /* PFP_S: interp, rcp, interp, interp, exit texauto. */
    CHECK(v9x_nv50_3d_exa_fp_s[0] == 0x80000000ul);
    CHECK(v9x_nv50_3d_exa_fp_s[5] == 0x0000c785ul);
}

unsigned int v9x_run_nv50_3d_tests(void)
{
    failures = 0u;
    test_setup();
    test_programs();
    return failures;
}
