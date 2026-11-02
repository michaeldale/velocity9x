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

unsigned int v9x_run_cm_3d_tests(void)
{
    failures = 0u;
    test_backend_map();
    test_gpu_init();
    return failures;
}
