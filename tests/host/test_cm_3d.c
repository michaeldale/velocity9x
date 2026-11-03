#include <stdio.h>

#include "velocity9x/cm_3d.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

/*
 * cayman_gpu_init (Linux ni.c) for 9901, worked by hand from its config
 * (ni.c:902-972: 1 SE, 2 tile pipes, 6 SIMDs, 2 backends, 2 texture
 * channel caches, 8 SX sets, export sizes 256/64/192, FIFOs 40h/30h/130h,
 * 2 CF instructions) and nid.h's fields, in Linux's order.
 */
static const struct v9x_eg_reg_update expected[V9X_CM_TRINITY_GPU_INIT_COUNT] = {
    { 0x8000ul, 0ul, 0x000000fful },          /* GRBM_CNTL: READ_TIMEOUT(FFh) */
    { 0x98f8ul, 0ul, 0x12010001ul },          /* GB_ADDR_CONFIG, ARUBA golden */
    { 0x98fcul, 0ul, 0x00000010ul },          /* GB_BACKEND_MAP */
    { 0x9148ul, 0ul, 0xfffc0000ul },          /* CGTS_TCC_DISABLE: 2 TCCs */
    { 0x3f90ul, 0ul, 0xfffc0000ul },          /* CGTS_SYS_TCC_DISABLE */
    { 0x3f94ul, 0ul, 0xfffc0000ul },          /* CGTS_USER_SYS_TCC_DISABLE */
    { 0x914cul, 0ul, 0xfffc0000ul },          /* CGTS_USER_TCC_DISABLE */
    { 0x8764ul, 0ul, 0x00006030ul },          /* CP_MEQ_THRESHOLDS 30h, 60h */
    { 0x9058ul, 0xfffffffful, 0x00010000ul }, /* SX_DEBUG_1 |= NEW_SMX */
    { 0xa020ul, 0xfffffc01ul, 0x00000010ul }, /* SMX_DC_CTL0 sets = 8 */
    { 0x913cul, 0ul, 0x00000104ul },          /* SPI_CONFIG_CNTL_1 */
    { 0x89b4ul, 0ul, 0ul },                   /* VGT_OFFCHIP_LDS_BASE */
    { 0x8e10ul, 0ul, 0ul },                   /* SQ_LSTMP_RING_BASE */
    { 0x8e18ul, 0ul, 0ul },                   /* SQ_HSTMP_RING_BASE */
    { 0x8c50ul, 0ul, 0ul },                   /* SQ_ESTMP_RING_BASE */
    { 0x8c58ul, 0ul, 0ul },                   /* SQ_GSTMP_RING_BASE */
    { 0x8c60ul, 0ul, 0ul },                   /* SQ_VSTMP_RING_BASE */
    { 0x8c68ul, 0ul, 0ul },                   /* SQ_PSTMP_RING_BASE */
    { 0x9508ul, 0ul, 0x00000002ul },          /* TA_CNTL_AUX: DISABLE_CUBE_ANISO */
    { 0x900cul, 0ul, 0x002f0f3ful },          /* SX_EXPORT_BUFFER_SIZES 63/15/47 */
    { 0x8bccul, 0ul, 0x13030040ul },          /* PA_SC_FIFO_SIZE 40h/30h/130h */
    { 0x8974ul, 0ul, 0x00000001ul },          /* VGT_NUM_INSTANCES */
    { 0x87fcul, 0ul, 0ul },                   /* CP_PERFMON_CNTL */
    { 0x8cf0ul, 0ul, 0x08e00420ul },          /* SQ_MS_FIFO_SIZES, cache 32 */
    { 0x8c04ul, 0ul, 0x40000000ul },          /* SQ_GPR_RESOURCE_MGMT_1: 4 temps */
    { 0x8c00ul, 0ul, 0x00000043ul },          /* SQ_CONFIG: VC, SRC_C, CS2 1 */
    { 0x8d8cul, 0ul, 0x00000100ul },          /* SQ_DYN_GPR_CNTL_PS_FLUSH_REQ */
    { 0x8b24ul, 0ul, 0x00ff0ffful },          /* PA_SC_FORCE_EOV_MAX_CNTS */
    { 0x88c4ul, 0ul, 0x000000c2ul },          /* VGT_CACHE_INVALIDATION */
    { 0x88d4ul, 0ul, 0x00000010ul },          /* VGT_GS_VERTEX_REUSE */
    { 0x8b10ul, 0ul, 0ul },                   /* PA_SC_LINE_STIPPLE_STATE */
    { 0x9a20ul, 0ul, 0ul },                   /* CB_PERF_CTR0_SEL_0 */
    { 0x9a24ul, 0ul, 0ul },
    { 0x9a28ul, 0ul, 0ul },
    { 0x9a2cul, 0ul, 0ul },
    { 0x9a30ul, 0ul, 0ul },
    { 0x9a34ul, 0ul, 0ul },
    { 0x9a38ul, 0ul, 0ul },
    { 0x9a3cul, 0ul, 0ul },                   /* CB_PERF_CTR3_SEL_1 */
    { 0x2f4cul, 0xfffffffful, 0x00000001ul }, /* HDP_MISC_CNTL |= FLUSH_INV */
    { 0x8a14ul, 0ul, 0x00000007ul }           /* PA_CL_ENHANCE */
};

static void test_backend_map(void)
{
    /* The maps the Evergreen tables carry, which drew on their cards:
     * Juniper's four pipes and four backends (EG_JUNIPER_BACKEND_MAP),
     * Cedar's two pipes and one backend (0). Evergreen's total is 8 too
     * (EVERGREEN_MAX_BACKENDS). */
    CHECK(v9x_cm_backend_map(2ul, 4ul, 8ul, 0ul) == 0x00003210ul);
    CHECK(v9x_cm_backend_map(1ul, 1ul, 8ul, 0ul) == 0x00000000ul);
    /* Trinity 9901: two pipes, two of Cayman's eight backends. */
    CHECK(v9x_cm_backend_map(1ul, 2ul, 8ul, 0ul) ==
          V9X_CM_TRINITY_BACKEND_MAP);
    /* One of the two fused off: the other takes both pipes. */
    CHECK(v9x_cm_backend_map(1ul, 2ul, 8ul, 0x2ul) == 0x00000000ul);
    CHECK(v9x_cm_backend_map(1ul, 2ul, 8ul, 0x1ul) == 0x00000011ul);
    /* Fewer pipes than backends: refused. */
    CHECK(v9x_cm_backend_map(0ul, 2ul, 8ul, 0ul) == 0ul);
}

static void test_gpu_init(void)
{
    v9x_u32 index;

    for (index = 0ul; index < V9X_CM_TRINITY_GPU_INIT_COUNT; ++index) {
        CHECK(v9x_cm_trinity_gpu_init[index].offset == expected[index].offset);
        CHECK(v9x_cm_trinity_gpu_init[index].keep == expected[index].keep);
        CHECK(v9x_cm_trinity_gpu_init[index].set == expected[index].set);
    }
    /* Not Evergreen's: no SQ thread, stack or GPR splits past MGMT_1 and
     * no CP_QUEUE_THRESHOLDS, which Cayman's gpu_init does not write. */
    for (index = 0ul; index < V9X_CM_TRINITY_GPU_INIT_COUNT; ++index) {
        CHECK(v9x_cm_trinity_gpu_init[index].offset != 0x8c18ul);
        CHECK(v9x_cm_trinity_gpu_init[index].offset != 0x8c20ul);
        CHECK(v9x_cm_trinity_gpu_init[index].offset != 0x8760ul);
    }
}

#define STREAM_MAX 600u
static v9x_u32 cm_words[STREAM_MAX];
static v9x_u32 eg_words[STREAM_MAX];

static v9x_u32 header_values(v9x_u32 header)
{
    return ((header >> 16) & 0x3ffful) + 1ul;
}

static void test_default_context(void)
{
    v9x_u32 at = 0ul;
    v9x_u32 packets = 0ul;

    /* Whole type-3 packets, end to end: each header's count is the words
     * that follow it. */
    while (at < V9X_CM_DEFAULT_CONTEXT_DWORDS) {
        CHECK((v9x_cm_default_context[at] >> 30) == 3ul);
        at += 1ul + header_values(v9x_cm_default_context[at]);
        ++packets;
    }
    CHECK(at == V9X_CM_DEFAULT_CONTEXT_DWORDS);
    CHECK(packets > 10ul);
    /* Linux 3.9's first packet, SET_CONTEXT_REG of six from 0 with
     * DB_RENDER_CONTROL 60h, and its last, VGT_VERTEX_REUSE_BLOCK_CNTL. */
    CHECK(v9x_cm_default_context[0] == 0xc0066900ul);
    CHECK(v9x_cm_default_context[1] == 0ul);
    CHECK(v9x_cm_default_context[2] == 0x00000060ul);
    CHECK(v9x_cm_default_context[241] == 0xc0026900ul);
    CHECK(v9x_cm_default_context[242] == 0x00000316ul);
    CHECK(v9x_cm_default_context[243] == 0x0000000eul);
    CHECK(v9x_cm_default_context[244] == 0x00000010ul);
}

static void test_me_initialize(void)
{
    struct v9x_eg_pm4 stream;

    v9x_eg_pm4_begin(&stream, cm_words, STREAM_MAX);
    v9x_cm_3d_me_initialize(&stream);
    CHECK(stream.count == 7ul);
    /* PACKET3(ME_INITIALIZE, 5), 1, 0, 8 contexts - 1, DEVICE_ID(1), 0, 0. */
    CHECK(cm_words[0] == 0xc0054400ul);
    CHECK(cm_words[1] == 1ul && cm_words[2] == 0ul);
    CHECK(cm_words[3] == 7ul);
    CHECK(cm_words[4] == 0x00010000ul);
    CHECK(cm_words[5] == 0ul && cm_words[6] == 0ul);
}

static void test_preamble(void)
{
    struct v9x_eg_pm4 cm;
    struct v9x_eg_pm4 eg;
    v9x_u32 index;
    /* After the context: PREAMBLE_CNTL end (2), CLEAR_STATE (2), the
     * vertex base (4), the constant clear (5), the reuse block (4). */
    const v9x_u32 tail = 17ul;

    v9x_eg_pm4_begin(&cm, cm_words, STREAM_MAX);
    v9x_cm_3d_preamble(&cm);
    v9x_eg_pm4_begin(&eg, eg_words, STREAM_MAX);
    v9x_eg_3d_preamble(&eg);
    CHECK(cm.overflow == 0ul && eg.overflow == 0ul);
    CHECK(cm.count == 2ul + V9X_CM_DEFAULT_CONTEXT_DWORDS + tail);
    /* PREAMBLE_CNTL begin, as Evergreen's. */
    CHECK(cm_words[0] == eg_words[0] && cm_words[1] == eg_words[1]);
    for (index = 0ul; index < V9X_CM_DEFAULT_CONTEXT_DWORDS; ++index) {
        CHECK(cm_words[2ul + index] == v9x_cm_default_context[index]);
    }
    /* cayman_cp_start's packets after the clear state are
     * evergreen_cp_start's. */
    for (index = 0ul; index < tail; ++index) {
        CHECK(cm_words[cm.count - tail + index] ==
              eg_words[eg.count - tail + index]);
    }
}

static void test_default_state(void)
{
    struct v9x_eg_pm4 cm;
    struct v9x_eg_pm4 eg;
    v9x_u32 index;

    v9x_eg_pm4_begin(&cm, cm_words, STREAM_MAX);
    v9x_cm_3d_default_state(&cm);
    v9x_eg_pm4_begin(&eg, eg_words, STREAM_MAX);
    v9x_eg_3d_default_state_tail(&eg, v9x_cm_default_context,
                                 V9X_CM_DEFAULT_CONTEXT_DWORDS);
    CHECK(cm.overflow == 0ul && eg.overflow == 0ul);
    /* CLEAR_STATE, then straight into the tail: no SET_CONFIG_REG of the
     * SQ registers between. */
    CHECK(cm_words[0] == 0xc0001200ul && cm_words[1] == 0ul);
    CHECK(cm.count == 2ul + eg.count);
    for (index = 0ul; index < eg.count; ++index) {
        CHECK(cm_words[2ul + index] == eg_words[index]);
    }
    CHECK((cm_words[2] & 0x0000ff00ul) == 0x00002800ul); /* CONTEXT_CONTROL */
}

static void test_coher_cntl2(void)
{
    struct v9x_eg_pm4 stream;

    v9x_eg_pm4_begin(&stream, cm_words, STREAM_MAX);
    v9x_cm_3d_coher_cntl2(&stream);
    /* Linux: PACKET3(SET_CONFIG_REG, 1), (0x85e8 - 0x8000) >> 2, 0. */
    CHECK(stream.count == 3ul);
    CHECK(cm_words[0] == 0xc0016800ul);
    CHECK(cm_words[1] == 0x0000017aul);
    CHECK(cm_words[2] == 0ul);
}

unsigned int v9x_run_cm_3d_tests(void)
{
    failures = 0u;
    test_backend_map();
    test_gpu_init();
    test_default_context();
    test_me_initialize();
    test_preamble();
    test_default_state();
    test_coher_cntl2();
    return failures;
}
