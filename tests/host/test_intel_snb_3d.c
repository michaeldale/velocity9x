#include <stdio.h>

#include "velocity9x/intel_snb_3d.h"

static unsigned int failures = 0u;

#define CHECK(expression) do { \
    if (!(expression)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #expression); \
        ++failures; \
    } \
} while (0)

#define SNB_TEST_DWORDS 64u

static v9x_u32 snb_store[SNB_TEST_DWORDS];
static struct v9x_hsw_batch snb_batch;

static void snb_reset(void)
{
    v9x_u32 index;

    for (index = 0ul; index < SNB_TEST_DWORDS; ++index) {
        snb_store[index] = 0xdeadbeeful;
    }
    snb_batch.dw = snb_store;
    snb_batch.capacity = SNB_TEST_DWORDS;
    snb_batch.used = 0ul;
    snb_batch.overflow = 0u;
    snb_batch.pc_since_cs_stall = 0u;
}

/*
 * Every expected DW0 is worked by hand from gen6.xml: type 3 at 31:29,
 * subtype 28:27, opcode 26:24, sub-opcode 23:16, length (total - 2) 7:0.
 */
static void test_headers(void)
{
    v9x_u32 header;

    CHECK(v9x_intel_snb_3d_header(V9X_SNB_CMD_URB, 3ul, 0ul, &header) ==
          V9X_STATUS_OK && header == 0x78050001ul);
    CHECK(v9x_intel_snb_3d_header(V9X_SNB_CMD_SF, 20ul, 0ul, &header) ==
          V9X_STATUS_OK && header == 0x78130012ul);
    CHECK(v9x_intel_snb_3d_header(V9X_SNB_CMD_WM, 9ul, 0ul, &header) ==
          V9X_STATUS_OK && header == 0x78140007ul);
    CHECK(v9x_intel_snb_3d_header(V9X_SNB_CMD_CC_PTRS, 4ul, 0ul, &header) ==
          V9X_STATUS_OK && header == 0x780e0002ul);
    CHECK(v9x_intel_snb_3d_header(V9X_SNB_CMD_DEPTH_BUFFER, 7ul, 0ul,
                                  &header) == V9X_STATUS_OK &&
          header == 0x79050005ul);
    CHECK(v9x_intel_snb_3d_header(V9X_SNB_CMD_CLEAR_PARAMS, 2ul, 0ul,
                                  &header) == V9X_STATUS_OK &&
          header == 0x79100000ul);
    CHECK(v9x_intel_snb_3d_header(V9X_SNB_CMD_3DPRIMITIVE, 6ul, 0ul,
                                  &header) == V9X_STATUS_OK &&
          header == 0x7b000004ul);
    CHECK(v9x_intel_snb_3d_header(V9X_SNB_CMD_PIPE_CONTROL, 5ul, 0ul,
                                  &header) == V9X_STATUS_OK &&
          header == 0x7a000003ul);
    CHECK(v9x_intel_snb_3d_header(V9X_SNB_CMD_PIPELINE_SELECT, 1ul, 0ul,
                                  &header) == V9X_STATUS_OK &&
          header == 0x69040000ul);
    CHECK(v9x_intel_snb_3d_header(V9X_SNB_CMD_STATE_BASE_ADDRESS, 10ul, 0ul,
                                  &header) == V9X_STATUS_OK &&
          header == 0x61010008ul);
    CHECK(v9x_intel_snb_3d_header(V9X_SNB_CMD_MULTISAMPLE, 3ul, 0ul,
                                  &header) == V9X_STATUS_OK &&
          header == 0x790d0001ul);
    CHECK(v9x_intel_snb_3d_header(V9X_SNB_CMD_CONSTANT_PS, 5ul, 0x1000ul,
                                  &header) == V9X_STATUS_OK &&
          header == 0x78171003ul);

    /* Gen7's lengths are refused: its 3DPRIMITIVE is 7, its
     * CC_STATE_POINTERS 2. */
    CHECK(v9x_intel_snb_3d_header(V9X_SNB_CMD_3DPRIMITIVE, 7ul, 0ul,
                                  &header) == V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_intel_snb_3d_header(V9X_SNB_CMD_CC_PTRS, 2ul, 0ul, &header) ==
          V9X_STATUS_INVALID_ARGUMENT);
    /* DW0 fields outside 15:8 would land on the length or the opcode. */
    CHECK(v9x_intel_snb_3d_header(V9X_SNB_CMD_URB, 3ul, 0x10000ul,
                                  &header) == V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_intel_snb_3d_header(V9X_SNB_CMD_URB, 3ul, 0x80ul, &header) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_intel_snb_3d_header(V9X_SNB_CMD_COUNT, 3ul, 0ul, &header) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

static void test_urb_and_stages(void)
{
    snb_reset();
    CHECK(v9x_intel_snb_urb(&snb_batch, 64u, 2u) == V9X_STATUS_OK);
    CHECK(snb_batch.used == 3ul);
    CHECK(snb_store[0] == 0x78050001ul);
    CHECK(snb_store[1] == 0x00010040ul);   /* rows - 1 at 23:16, entries */
    CHECK(snb_store[2] == 0ul);            /* no GS entries */

    snb_reset();
    CHECK(v9x_intel_snb_urb(&snb_batch, 62u, 2u) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_intel_snb_urb(&snb_batch, 20u, 2u) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_intel_snb_urb(&snb_batch, 260u, 2u) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_intel_snb_urb(&snb_batch, 64u, 0u) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_intel_snb_urb(&snb_batch, 64u, 6u) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(snb_batch.used == 0ul);

    snb_reset();
    CHECK(v9x_intel_snb_vs_disabled(&snb_batch) == V9X_STATUS_OK);
    CHECK(snb_batch.used == 6ul && snb_store[0] == 0x78100004ul);
    CHECK(snb_store[1] == 0ul && snb_store[5] == 0ul);

    snb_reset();
    CHECK(v9x_intel_snb_gs_disabled(&snb_batch) == V9X_STATUS_OK);
    CHECK(snb_batch.used == 7ul && snb_store[0] == 0x78110005ul);
    CHECK(snb_store[1] == 0ul && snb_store[2] == 0ul && snb_store[3] == 0ul);
    CHECK(snb_store[4] == 1ul);            /* Dispatch GRF Start 1 */
    CHECK(snb_store[5] == 0x00000500ul);   /* Statistics 10, Rendering 8 */
    CHECK(snb_store[6] == 0ul);            /* Enable (bit 15) clear */

    snb_reset();
    CHECK(v9x_intel_snb_clip_screen(&snb_batch) == V9X_STATUS_OK);
    CHECK(snb_batch.used == 4ul && snb_store[0] == 0x78120002ul);
    CHECK(snb_store[1] == 0ul && snb_store[2] == 0x80008200ul &&
          snb_store[3] == 0ul);
}

static void test_sf_and_wm(void)
{
    v9x_u32 index;

    snb_reset();
    CHECK(v9x_intel_snb_sf(&snb_batch, 2u, 1u, 1u, V9X_HSW_DRAW_CULL_CW,
                           0x2ul) == V9X_STATUS_OK);
    CHECK(snb_batch.used == 20ul && snb_store[0] == 0x78130012ul);
    CHECK(snb_store[1] == 0x00800810ul);   /* 2 outputs, length 1, off 1 */
    CHECK(snb_store[2] == 0x00000001ul);   /* front winding CCW */
    CHECK(snb_store[3] == 0x60000000ul);   /* CULLMODE_BACK */
    CHECK(snb_store[17] == 0x2ul);         /* the flat mask */
    for (index = 4ul; index < 17ul; ++index) {
        CHECK(snb_store[index] == 0ul);
    }
    CHECK(snb_store[18] == 0ul && snb_store[19] == 0ul);

    snb_reset();
    CHECK(v9x_intel_snb_sf(&snb_batch, 1u, 1u, 1u, V9X_HSW_DRAW_CULL_NONE,
                           0ul) == V9X_STATUS_OK);
    CHECK(snb_store[2] == 0ul && snb_store[3] == 0x20000000ul);
    snb_reset();
    CHECK(v9x_intel_snb_sf(&snb_batch, 1u, 1u, 1u, V9X_HSW_DRAW_CULL_CCW,
                           0ul) == V9X_STATUS_OK);
    CHECK(snb_store[2] == 0ul && snb_store[3] == 0x60000000ul);

    snb_reset();
    CHECK(v9x_intel_snb_sf(&snb_batch, 33u, 1u, 1u, 0u, 0ul) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_intel_snb_sf(&snb_batch, 1u, 1u, 0u, 0u, 0ul) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_intel_snb_sf(&snb_batch, 1u, 1u, 1u, 3u, 0ul) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(snb_batch.used == 0ul);

    /* One SIMD8 kernel at 0x400, 3 binding entries, 1 sampler, 2
     * attributes, setup data from g4, perspective pixel barycentrics. */
    snb_reset();
    CHECK(v9x_intel_snb_wm(&snb_batch, 0x400ul, 3u, 1u, 2u, 4u, 1u, 0u) ==
          V9X_STATUS_OK);
    CHECK(snb_batch.used == 9ul && snb_store[0] == 0x78140007ul);
    CHECK(snb_store[1] == 0x00000400ul);
    CHECK(snb_store[2] == 0x080c0000ul);   /* 1 sampler (in fours: 1), 3 */
    CHECK(snb_store[3] == 0ul);
    CHECK(snb_store[4] == 0x00040000ul);   /* GRF start 4 at 22:16 */
    CHECK(snb_store[5] == 0x9e080001ul);   /* 79 << 25, dispatch, SIMD8 */
    CHECK(snb_store[6] == 0x00200400ul);   /* 2 attributes, bary 1 */
    CHECK(snb_store[7] == 0ul && snb_store[8] == 0ul);

    snb_reset();
    CHECK(v9x_intel_snb_wm(&snb_batch, 0x400ul, 3u, 1u, 2u, 4u, 1u, 1u) ==
          V9X_STATUS_OK);
    CHECK(snb_store[5] == 0x9e480001ul);   /* plus Kills Pixel, 22 */

    snb_reset();
    CHECK(v9x_intel_snb_wm(&snb_batch, 0x410ul, 3u, 1u, 2u, 4u, 1u, 0u) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_intel_snb_wm(&snb_batch, 0x400ul, 3u, 5u, 2u, 4u, 1u, 0u) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_intel_snb_wm(&snb_batch, 0x400ul, 3u, 1u, 64u, 4u, 1u, 0u) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_intel_snb_wm(&snb_batch, 0x400ul, 3u, 1u, 2u, 0x80u, 1u, 0u) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_intel_snb_wm(&snb_batch, 0x400ul, 3u, 1u, 2u, 4u, 0x40u, 0u) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(snb_batch.used == 0ul);
}

static void test_pointers_and_constants(void)
{
    snb_reset();
    CHECK(v9x_intel_snb_constant_ps(&snb_batch, 0x1620ul, 2u) ==
          V9X_STATUS_OK);
    CHECK(snb_batch.used == 5ul && snb_store[0] == 0x78171003ul);
    CHECK(snb_store[1] == 0x00001621ul);   /* pointer, read length - 1 */
    CHECK(snb_store[2] == 0ul && snb_store[3] == 0ul && snb_store[4] == 0ul);

    snb_reset();
    CHECK(v9x_intel_snb_constant_ps(&snb_batch, 0ul, 0u) == V9X_STATUS_OK);
    CHECK(snb_store[0] == 0x78170003ul && snb_store[1] == 0ul);
    CHECK(v9x_intel_snb_constant_ps(&snb_batch, 0x1630ul, 1u) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_intel_snb_constant_ps(&snb_batch, 0x1620ul, 33u) ==
          V9X_STATUS_INVALID_ARGUMENT);

    snb_reset();
    CHECK(v9x_intel_snb_binding_table_ptrs_ps(&snb_batch, 0x1340ul) ==
          V9X_STATUS_OK);
    CHECK(snb_store[0] == 0x78011002ul && snb_store[1] == 0ul &&
          snb_store[2] == 0ul && snb_store[3] == 0x1340ul);
    CHECK(v9x_intel_snb_sampler_ptrs_ps(&snb_batch, 0x340ul) ==
          V9X_STATUS_OK);
    CHECK(snb_store[4] == 0x78021002ul && snb_store[7] == 0x340ul);
    CHECK(v9x_intel_snb_viewport_ptrs(&snb_batch, 0x40ul, 0x60ul, 0x0ul) ==
          V9X_STATUS_OK);
    CHECK(snb_store[8] == 0x780d1c02ul && snb_store[9] == 0x40ul &&
          snb_store[10] == 0x60ul && snb_store[11] == 0ul);
    CHECK(v9x_intel_snb_cc_ptrs(&snb_batch, 0xc0ul, 0x100ul, 0x140ul) ==
          V9X_STATUS_OK);
    CHECK(snb_store[12] == 0x780e0002ul && snb_store[13] == 0xc1ul &&
          snb_store[14] == 0x101ul && snb_store[15] == 0x141ul);
    CHECK(snb_batch.used == 16ul);

    CHECK(v9x_intel_snb_binding_table_ptrs_ps(&snb_batch, 0x1350ul) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_intel_snb_viewport_ptrs(&snb_batch, 0x44ul, 0ul, 0ul) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_intel_snb_cc_ptrs(&snb_batch, 0xe0ul, 0x100ul, 0x140ul) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(snb_batch.used == 16ul);
}

static void test_depth_and_primitive(void)
{
    snb_reset();
    /* D24_UNORM_X8, 640x480, a 2560-byte Y-tiled pitch. */
    CHECK(v9x_intel_snb_depth_buffer(&snb_batch, V9X_HSW_DEPTH_D24_UNORM_X8,
                                     0x00200000ul, 2560ul, 640u, 480u) ==
          V9X_STATUS_OK);
    CHECK(snb_batch.used == 7ul && snb_store[0] == 0x79050005ul);
    /* 2D 31:29, tiled 27, Y walk 26, format 3 at 20:18, pitch - 1. */
    CHECK(snb_store[1] == 0x2c0c09fful);
    CHECK(snb_store[2] == 0x00200000ul);
    CHECK(snb_store[3] == 0x0ef89fc0ul);  /* 479 << 19 | 639 << 6 */
    CHECK(snb_store[4] == 0ul && snb_store[5] == 0ul && snb_store[6] == 0ul);

    snb_reset();
    CHECK(v9x_intel_snb_depth_buffer(&snb_batch, 0u, 0ul, 0ul, 0u, 0u) ==
          V9X_STATUS_OK);
    CHECK(snb_store[1] == 0xe0040000ul);  /* SURFTYPE_NULL, D32_FLOAT */
    CHECK(snb_store[2] == 0ul && snb_store[3] == 0ul);

    snb_reset();
    CHECK(v9x_intel_snb_depth_buffer(&snb_batch, 2u, 0x00200000ul, 2560ul,
                                     640u, 480u) ==
          V9X_STATUS_INVALID_ARGUMENT);   /* only D32F, D24X8, D16 */
    CHECK(v9x_intel_snb_depth_buffer(&snb_batch, V9X_HSW_DEPTH_D16_UNORM,
                                     0x00200100ul, 2560ul, 640u, 480u) ==
          V9X_STATUS_INVALID_ARGUMENT);   /* base not 4 KiB aligned */
    CHECK(snb_batch.used == 0ul);

    snb_reset();
    CHECK(v9x_intel_snb_clear_params(&snb_batch) == V9X_STATUS_OK);
    CHECK(snb_batch.used == 2ul && snb_store[0] == 0x79100000ul &&
          snb_store[1] == 0ul);
    CHECK(v9x_intel_snb_multisample(&snb_batch) == V9X_STATUS_OK);
    CHECK(snb_batch.used == 5ul && snb_store[2] == 0x790d0001ul &&
          snb_store[3] == 0ul && snb_store[4] == 0ul);

    snb_reset();
    CHECK(v9x_intel_snb_3dprimitive(&snb_batch, V9X_HSW_PRIM_TRILIST, 3ul,
                                    6ul, 0u) == V9X_STATUS_OK);
    CHECK(snb_batch.used == 6ul);
    CHECK(snb_store[0] == 0x7b001004ul);  /* TRILIST 4 at 14:10 */
    CHECK(snb_store[1] == 3ul && snb_store[2] == 6ul && snb_store[3] == 1ul);
    CHECK(snb_store[4] == 0ul && snb_store[5] == 0ul);
    CHECK(v9x_intel_snb_3dprimitive(&snb_batch, V9X_HSW_PRIM_TRISTRIP, 4ul,
                                    0ul, 1u) == V9X_STATUS_OK);
    CHECK(snb_store[6] == 0x7b009404ul);  /* random access 15, TRISTRIP */
    CHECK(v9x_intel_snb_3dprimitive(&snb_batch, 0x20u, 3ul, 0ul, 0u) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

static void test_pipe_control(void)
{
    snb_reset();
    CHECK(v9x_intel_snb_pipe_control(&snb_batch,
                                     V9X_HSW_PC_CS_STALL |
                                     V9X_HSW_PC_WRITE_IMMEDIATE |
                                     V9X_HSW_PC_GGTT,
                                     0x1040ul, 7ul) == V9X_STATUS_OK);
    CHECK(snb_batch.used == 5ul && snb_store[0] == 0x7a000003ul);
    CHECK(snb_store[1] == 0x00104000ul);  /* GGTT moved out of DW1 */
    CHECK(snb_store[2] == 0x00001044ul);  /* ... into DW2 bit 2 */
    CHECK(snb_store[3] == 7ul && snb_store[4] == 0ul);

    snb_reset();
    CHECK(v9x_intel_snb_pipe_control(&snb_batch, V9X_HSW_PC_DC_FLUSH, 0ul,
                                     0ul) == V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_intel_snb_pipe_control(&snb_batch, V9X_HSW_PC_CS_STALL, 0ul,
                                     0ul) == V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_intel_snb_pipe_control(&snb_batch,
                                     V9X_HSW_PC_DEPTH_STALL |
                                     V9X_HSW_PC_RT_CACHE_FLUSH, 0ul, 0ul) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_intel_snb_pipe_control(&snb_batch,
                                     V9X_HSW_PC_DEPTH_STALL |
                                     V9X_HSW_PC_DEPTH_CACHE_FLUSH, 0ul,
                                     0ul) == V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_intel_snb_pipe_control(&snb_batch,
                                     V9X_HSW_PC_WRITE_IMMEDIATE |
                                     V9X_HSW_PC_GGTT, 0x1044ul, 0ul) ==
          V9X_STATUS_INVALID_ARGUMENT);   /* not qword aligned */
    CHECK(snb_batch.used == 0ul);

    snb_reset();
    CHECK(v9x_intel_snb_post_sync_nonzero(&snb_batch, 0x1040ul) ==
          V9X_STATUS_OK);
    CHECK(snb_batch.used == 10ul);
    CHECK(snb_store[1] == (V9X_HSW_PC_CS_STALL |
                           V9X_HSW_PC_STALL_AT_SCOREBOARD));
    CHECK(snb_store[2] == 0ul);
    CHECK(snb_store[5] == 0x7a000003ul);
    CHECK(snb_store[6] == V9X_HSW_PC_WRITE_IMMEDIATE);
    CHECK(snb_store[7] == 0x00001044ul && snb_store[8] == 0ul);
}

static void test_indirect_state(void)
{
    v9x_u32 surface[6];
    v9x_u32 sf[8];
    v9x_u32 clip[4];

    CHECK(v9x_intel_snb_surface_2d(surface, V9X_HSW_FMT_B8G8R8X8_UNORM,
                                   0x00100000ul, 640u, 480u, 2560ul) ==
          V9X_STATUS_OK);
    CHECK(surface[0] == 0x23a40000ul);   /* 2D, format 0E9h at 26:18 */
    CHECK(surface[1] == 0x00100000ul);
    CHECK(surface[2] == 0x0ef89fc0ul);   /* 479 << 19 | 639 << 6 */
    CHECK(surface[3] == 0x00004ff8ul);   /* 2559 << 3, linear */
    CHECK(surface[4] == 0ul && surface[5] == 0ul);
    CHECK(v9x_intel_snb_surface_2d(surface, V9X_HSW_FMT_B8G8R8X8_UNORM,
                                   0x00100000ul, 0u, 480u, 2560ul) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_intel_snb_surface_2d(surface, V9X_HSW_FMT_B8G8R8X8_UNORM,
                                   0x00100000ul, 640u, 480u, 0x20001ul) ==
          V9X_STATUS_INVALID_ARGUMENT);

    v9x_intel_snb_sf_viewport(sf, 1ul, 2ul, 3ul, 4ul, 5ul, 6ul);
    CHECK(sf[0] == 1ul && sf[1] == 2ul && sf[2] == 3ul && sf[3] == 4ul &&
          sf[4] == 5ul && sf[5] == 6ul && sf[6] == 0ul && sf[7] == 0ul);
    v9x_intel_snb_clip_viewport(clip, 9ul, 10ul, 11ul, 12ul);
    CHECK(clip[0] == 9ul && clip[1] == 10ul && clip[2] == 11ul &&
          clip[3] == 12ul);
}

/* A batch too small for the command: refused, nothing half-written. */
static void test_overflow(void)
{
    snb_reset();
    snb_batch.capacity = 19ul;
    CHECK(v9x_intel_snb_sf(&snb_batch, 1u, 1u, 1u, 0u, 0ul) ==
          V9X_STATUS_INSUFFICIENT_MEMORY);
    CHECK(snb_batch.used == 0ul && snb_batch.overflow == 1u);
}

unsigned int v9x_run_intel_snb_3d_tests(void)
{
    failures = 0u;
    test_headers();
    test_urb_and_stages();
    test_sf_and_wm();
    test_pointers_and_constants();
    test_depth_and_primitive();
    test_pipe_control();
    test_indirect_state();
    test_overflow();
    return failures;
}
