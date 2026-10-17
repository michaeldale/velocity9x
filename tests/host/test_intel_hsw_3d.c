#include <stdio.h>

#include "velocity9x/intel_hsw_3d.h"

static unsigned int failures = 0u;

#define CHECK(expression) do { \
    if (!(expression)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #expression); \
        ++failures; \
    } \
} while (0)

/* Expected headers are worked by hand from HSW PRM Vol 2b's DWord 0
 * tables: type 3 at 31:29, subtype 28:27, opcode 26:24, sub-opcode 23:16,
 * length 7:0. */
static void test_headers(void)
{
    v9x_u32 header;

    CHECK(v9x_intel_hsw_3d_header(V9X_HSW_CMD_VS, 6ul, &header) ==
          V9X_STATUS_OK && header == 0x78100004ul);
    CHECK(v9x_intel_hsw_3d_header(V9X_HSW_CMD_PIPE_CONTROL, 5ul, &header) ==
          V9X_STATUS_OK && header == 0x7a000003ul);
    CHECK(v9x_intel_hsw_3d_header(V9X_HSW_CMD_3DPRIMITIVE, 7ul, &header) ==
          V9X_STATUS_OK && header == 0x7b000005ul);
    CHECK(v9x_intel_hsw_3d_header(V9X_HSW_CMD_PIPELINE_SELECT, 1ul,
                                  &header) == V9X_STATUS_OK &&
          header == 0x69040000ul);
    CHECK(v9x_intel_hsw_3d_header(V9X_HSW_CMD_STATE_BASE_ADDRESS, 10ul,
                                  &header) == V9X_STATUS_OK &&
          header == 0x61010008ul);
    CHECK(v9x_intel_hsw_3d_header(V9X_HSW_CMD_PUSH_CONSTANT_ALLOC_PS, 2ul,
                                  &header) == V9X_STATUS_OK &&
          header == 0x79160000ul);
    CHECK(v9x_intel_hsw_3d_header(V9X_HSW_CMD_SBE, 14ul, &header) ==
          V9X_STATUS_OK && header == 0x781f000cul);
    /* Fixed-length commands refuse any other length. */
    CHECK(v9x_intel_hsw_3d_header(V9X_HSW_CMD_VS, 5ul, &header) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_intel_hsw_3d_header(V9X_HSW_CMD_COUNT, 2ul, &header) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

static void test_state(void)
{
    v9x_u32 surface[8];
    v9x_u32 blend[2];
    v9x_u32 depth[3];
    v9x_u32 sampler_out[4];
    struct v9x_hsw_blend b;
    struct v9x_hsw_sampler s;

    /* A 640x480 B8G8R8A8 target at 1 MB, pitch 2560. */
    CHECK(v9x_intel_hsw_surface_2d(surface, V9X_HSW_FMT_B8G8R8A8_UNORM,
                                   0x00100000ul, 640u, 480u, 2560ul) ==
          V9X_STATUS_OK);
    CHECK(surface[0] == 0x23000000ul);
    CHECK(surface[1] == 0x00100000ul);
    CHECK(surface[2] == 0x01df027ful);
    CHECK(surface[3] == 0x000009fful);
    /* Identity channel selects, not the all-zero SCS_ZERO default. */
    CHECK(surface[7] == 0x09770000ul);
    CHECK(v9x_intel_hsw_surface_2d(surface, V9X_HSW_FMT_B5G6R5_UNORM,
                                   0ul, 0u, 480u, 1280ul) ==
          V9X_STATUS_INVALID_ARGUMENT);

    b.enable = 1u;
    b.color_function = 0u;
    b.src_factor = 0x14u;
    b.dst_factor = 0x15u;
    b.independent_alpha = 0u;
    b.alpha_function = 0u;
    b.src_alpha_factor = 0u;
    b.dst_alpha_factor = 0u;
    b.write_disable = 0u;
    b.alpha_test = 1u;
    b.alpha_function_test = 6u;
    CHECK(v9x_intel_hsw_blend(blend, &b) == V9X_STATUS_OK);
    CHECK(blend[0] == 0x80000295ul);
    CHECK(blend[1] == 0x0001c000ul);
    b.src_factor = 0x20u;
    CHECK(v9x_intel_hsw_blend(blend, &b) == V9X_STATUS_INVALID_ARGUMENT);

    CHECK(v9x_intel_hsw_depth_stencil(depth, 1u, 1u, 1u) == V9X_STATUS_OK);
    CHECK(depth[0] == 0ul && depth[1] == 0ul && depth[2] == 0x8c000000ul);

    s.mip_filter = 0u;
    s.mag_filter = 1u;
    s.min_filter = 1u;
    s.min_lod = 0u;
    s.max_lod = 0x100u;
    s.border_color_pointer = 0x40ul;
    s.dw3 = 0ul;
    CHECK(v9x_intel_hsw_sampler(sampler_out, &s) == V9X_STATUS_OK);
    CHECK(sampler_out[0] == 0x00024000ul);
    CHECK(sampler_out[1] == 0x00010000ul);
    s.border_color_pointer = 0x41ul;
    CHECK(v9x_intel_hsw_sampler(sampler_out, &s) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

/* A whole minimal 3D batch: everything the builders emit must pass the
 * checker, and the checker must catch each kind of damage. */
static void test_batch_and_check(void)
{
    static v9x_u32 buffer[256];
    struct v9x_hsw_batch batch;
    struct v9x_hsw_check check;
    struct v9x_hsw_vertex_element elements[2];
    v9x_u32 saved;
    v9x_u32 used;

    v9x_intel_hsw_batch_init(&batch, buffer, 256ul);
    CHECK(v9x_intel_hsw_pipeline_select_3d(&batch) == V9X_STATUS_OK);
    CHECK(v9x_intel_hsw_state_base_address(&batch, 0ul, 0x00100000ul,
              0x00200000ul, 0ul, 0x00300000ul, 0xfffff000ul,
              0xfffff000ul, 0xfffff000ul, 0xfffff000ul) == V9X_STATUS_OK);
    CHECK(v9x_intel_hsw_push_constant_alloc(&batch,
              V9X_HSW_CMD_PUSH_CONSTANT_ALLOC_PS, 0u, 8u) == V9X_STATUS_OK);
    CHECK(v9x_intel_hsw_urb(&batch, V9X_HSW_CMD_URB_VS, 2u, 1u, 64u) ==
          V9X_STATUS_OK);
    CHECK(v9x_intel_hsw_stage_disabled(&batch, V9X_HSW_CMD_VS) ==
          V9X_STATUS_OK);
    CHECK(v9x_intel_hsw_stage_disabled(&batch, V9X_HSW_CMD_HS) ==
          V9X_STATUS_OK);
    CHECK(v9x_intel_hsw_stage_disabled(&batch, V9X_HSW_CMD_TE) ==
          V9X_STATUS_OK);
    CHECK(v9x_intel_hsw_stage_disabled(&batch, V9X_HSW_CMD_DS) ==
          V9X_STATUS_OK);
    CHECK(v9x_intel_hsw_stage_disabled(&batch, V9X_HSW_CMD_GS) ==
          V9X_STATUS_OK);
    CHECK(v9x_intel_hsw_stage_disabled(&batch, V9X_HSW_CMD_STREAMOUT) ==
          V9X_STATUS_OK);
    CHECK(v9x_intel_hsw_stage_disabled(&batch, V9X_HSW_CMD_PS) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_intel_hsw_drawing_rectangle(&batch, 0u, 0u, 639u, 479u) ==
          V9X_STATUS_OK);
    CHECK(v9x_intel_hsw_state_pointer(&batch, V9X_HSW_CMD_BLEND_PTRS,
                                      0x41ul) == V9X_STATUS_OK);
    CHECK(v9x_intel_hsw_vertex_buffer(&batch, 0u, 32u, 0x00400000ul,
                                      0x0040005ful) == V9X_STATUS_OK);
    elements[0].buffer = 0u;
    elements[0].format = V9X_HSW_FMT_R32G32B32A32_FLOAT;
    elements[0].offset = 0u;
    elements[0].component[0] = V9X_HSW_VFCOMP_STORE_SRC;
    elements[0].component[1] = V9X_HSW_VFCOMP_STORE_SRC;
    elements[0].component[2] = V9X_HSW_VFCOMP_STORE_SRC;
    elements[0].component[3] = V9X_HSW_VFCOMP_STORE_SRC;
    elements[1] = elements[0];
    elements[1].offset = 16u;
    CHECK(v9x_intel_hsw_vertex_elements(&batch, elements, 2u) ==
          V9X_STATUS_OK);
    CHECK(v9x_intel_hsw_3dprimitive(&batch, V9X_HSW_PRIM_TRILIST, 3ul,
                                    0ul) == V9X_STATUS_OK);
    CHECK(v9x_intel_hsw_pipe_control(&batch,
              V9X_HSW_PC_CS_STALL | V9X_HSW_PC_WRITE_IMMEDIATE |
              V9X_HSW_PC_GGTT, 0x00001000ul, 0x12345678ul) ==
          V9X_STATUS_OK);
    CHECK(v9x_intel_hsw_mi_store_data_imm(&batch, 0x00001004ul, 1ul) ==
          V9X_STATUS_OK);
    CHECK(v9x_intel_hsw_mi_batch_buffer_end(&batch) == V9X_STATUS_OK);
    if ((batch.used & 1ul) != 0ul) {
        CHECK(v9x_intel_hsw_mi_noop(&batch) == V9X_STATUS_OK);
    }
    CHECK(batch.overflow == 0u);

    /* Spot fields: VERTEX_BUFFER_STATE DW0, the first element, the
     * primitive's topology and instance count. */
    CHECK(buffer[1] == 0x61010008ul);
    CHECK(v9x_intel_hsw_3d_check(buffer, batch.used, &check) ==
          V9X_STATUS_OK);
    CHECK(check.result == V9X_HSW_CHECK_OK);
    /* 18 commands; a padding MI_NOOP after the end is not counted. */
    CHECK(check.commands == 18ul);

    used = batch.used;
    /* A wrong length on 3DSTATE_VS, which starts at dword 15:
     * PIPELINE_SELECT 1 + SBA 10 + push alloc 2 + URB 2. */
    saved = buffer[15];
    CHECK(saved == 0x78100004ul);
    buffer[15] = 0x78100005ul;
    CHECK(v9x_intel_hsw_3d_check(buffer, used, &check) ==
          V9X_STATUS_INVALID_STATE);
    CHECK(check.result == V9X_HSW_CHECK_BAD_LENGTH && check.at == 15ul);
    buffer[15] = 0x78ee0004ul; /* unknown sub-opcode */
    CHECK(v9x_intel_hsw_3d_check(buffer, used, &check) ==
          V9X_STATUS_INVALID_STATE);
    CHECK(check.result == V9X_HSW_CHECK_UNKNOWN_COMMAND);
    buffer[15] = saved;

    /* No end, odd count, truncated tail. */
    CHECK(v9x_intel_hsw_3d_check(buffer, 15ul, &check) ==
          V9X_STATUS_INVALID_STATE);
    CHECK(check.result == V9X_HSW_CHECK_NO_END);
    CHECK(v9x_intel_hsw_3d_check(buffer, 14ul, &check) ==
          V9X_STATUS_INVALID_STATE);
    CHECK(check.result == V9X_HSW_CHECK_TRUNCATED);

    /* Overflow leaves no half command behind. */
    v9x_intel_hsw_batch_init(&batch, buffer, 4ul);
    CHECK(v9x_intel_hsw_pipeline_select_3d(&batch) == V9X_STATUS_OK);
    CHECK(v9x_intel_hsw_3dprimitive(&batch, V9X_HSW_PRIM_TRILIST, 3ul,
                                    0ul) == V9X_STATUS_INSUFFICIENT_MEMORY);
    CHECK(batch.used == 1ul && batch.overflow == 1u);
}

static void test_qword_rule(void)
{
    v9x_u32 dw[4];
    struct v9x_hsw_check check;

    dw[0] = 0x69040000ul;   /* PIPELINE_SELECT */
    dw[1] = 0x05000000ul;   /* MI_BATCH_BUFFER_END */
    CHECK(v9x_intel_hsw_3d_check(dw, 2ul, &check) == V9X_STATUS_OK);
    dw[1] = 0ul;            /* MI_NOOP */
    dw[2] = 0x05000000ul;
    CHECK(v9x_intel_hsw_3d_check(dw, 3ul, &check) ==
          V9X_STATUS_INVALID_STATE);
    CHECK(check.result == V9X_HSW_CHECK_NOT_QWORD);
    dw[3] = 0ul;
    CHECK(v9x_intel_hsw_3d_check(dw, 4ul, &check) == V9X_STATUS_OK);
}

/* Phase B1's ring contents (docs\plans\intel-haswell-direct3d.md): a
 * store, then a 16x16 32 bpp fill at pitch field 256 and a second store,
 * padded to a qword. */
static void test_phase_b1_stream(void)
{
    v9x_u32 ring[24];
    struct v9x_hsw_batch batch;
    struct v9x_hsw_check check;

    v9x_intel_hsw_batch_init(&batch, ring, 22ul);
    CHECK(v9x_intel_hsw_mi_store_data_imm(&batch, 0x01801000ul,
                                          0xc0de0001ul) == V9X_STATUS_OK);
    CHECK(batch.used == 4ul);
    CHECK(ring[0] == 0x10400002ul && ring[2] == 0x01801000ul);
    CHECK(v9x_intel_hsw_xy_color_blt(&batch, 4u, 256u, 0u, 0u, 16u, 16u,
                                     0x01802000ul, 0x11223344ul) ==
          V9X_STATUS_OK);
    CHECK(ring[4] == 0x54300004ul);
    CHECK(ring[5] == 0x03f00100ul);
    CHECK(ring[6] == 0ul && ring[7] == 0x00100010ul);
    CHECK(ring[8] == 0x01802000ul && ring[9] == 0x11223344ul);
    CHECK(v9x_intel_hsw_mi_store_data_imm(&batch, 0x01801004ul,
                                          0xc0de0002ul) == V9X_STATUS_OK);
    CHECK(v9x_intel_hsw_mi_noop(&batch) == V9X_STATUS_OK);
    CHECK(v9x_intel_hsw_mi_noop(&batch) == V9X_STATUS_OK);
    CHECK(batch.used == 16ul && batch.overflow == 0u);

    /* The checker accepts it once terminated. */
    CHECK(v9x_intel_hsw_mi_batch_buffer_end(&batch) == V9X_STATUS_OK);
    CHECK(v9x_intel_hsw_mi_noop(&batch) == V9X_STATUS_OK);
    CHECK(v9x_intel_hsw_3d_check(ring, batch.used, &check) ==
          V9X_STATUS_OK);

    CHECK(v9x_intel_hsw_xy_color_blt(&batch, 4u, 256u, 16u, 0u, 16u, 16u,
                                     0ul, 0ul) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

/* Phase B2's blits: fills at each depth, and an 8 bpp copy. */
static void test_blits_by_depth(void)
{
    v9x_u32 ring[32];
    struct v9x_hsw_batch batch;
    struct v9x_hsw_check check;

    v9x_intel_hsw_batch_init(&batch, ring, 32ul);
    CHECK(v9x_intel_hsw_xy_color_blt(&batch, 1u, 640u, 8u, 4u, 24u, 12u,
                                     0x00000000ul, 0x000000abul) ==
          V9X_STATUS_OK);
    /* 8 bpp: no byte mask, depth 00b, ROP F0h. */
    CHECK(ring[0] == 0x54000004ul && ring[1] == 0x00f00280ul);
    CHECK(ring[2] == 0x00040008ul && ring[3] == 0x000c0018ul);
    CHECK(v9x_intel_hsw_xy_color_blt(&batch, 2u, 1280u, 0u, 0u, 1u, 1u,
                                     0ul, 0xf800ul) == V9X_STATUS_OK);
    CHECK(ring[6] == 0x54000004ul && ring[7] == 0x01f00500ul);
    CHECK(v9x_intel_hsw_xy_color_blt(&batch, 3u, 1920u, 0u, 0u, 1u, 1u,
                                     0ul, 0ul) ==
          V9X_STATUS_INVALID_ARGUMENT);

    /* Copy 16x8 from (100,50) to (0,0), 8 bpp, both pitches 640. */
    CHECK(v9x_intel_hsw_xy_src_copy_blt(&batch, 1u, 640u, 0u, 0u, 16u, 8u,
                                        0x00000000ul, 640u, 100u, 50u,
                                        0x00000000ul) == V9X_STATUS_OK);
    CHECK(ring[12] == 0x54c00006ul && ring[13] == 0x00cc0280ul);
    CHECK(ring[14] == 0ul && ring[15] == 0x00080010ul);
    CHECK(ring[17] == 0x00320064ul && ring[18] == 0x00000280ul);
    CHECK(v9x_intel_hsw_xy_src_copy_blt(&batch, 4u, 2560u, 0u, 0u, 1u, 1u,
                                        0ul, 2560u, 0u, 0u, 0ul) ==
          V9X_STATUS_OK);
    CHECK(ring[20] == 0x54f00006ul && ring[21] == 0x03cc0a00ul);
    CHECK(batch.used == 28ul);
    CHECK(v9x_intel_hsw_mi_batch_buffer_end(&batch) == V9X_STATUS_OK);
    CHECK(v9x_intel_hsw_mi_noop(&batch) == V9X_STATUS_OK);
    CHECK(v9x_intel_hsw_3d_check(ring, batch.used, &check) ==
          V9X_STATUS_OK);
    CHECK(check.commands == 5ul);
}

/* Each init-state offset meets its pointer's alignment (Vol 2b). */
typedef char v9x_hsw_init_offsets_aligned[
    (V9X_HSW_INIT_SF_CLIP_VIEWPORT & 63ul) == 0ul &&
    (V9X_HSW_INIT_BLEND & 63ul) == 0ul &&
    (V9X_HSW_INIT_DEPTH_STENCIL & 63ul) == 0ul &&
    (V9X_HSW_INIT_CC & 63ul) == 0ul &&
    (V9X_HSW_INIT_CC_VIEWPORT & 31ul) == 0ul &&
    (V9X_HSW_INIT_SCISSOR & 31ul) == 0ul &&
    (V9X_HSW_INIT_SAMPLER & 31ul) == 0ul &&
    (V9X_HSW_INIT_BINDING_TABLE & 31ul) == 0ul &&
    (V9X_HSW_INIT_NULL_SURFACE & 31ul) == 0ul ? 1 : -1];

/*
 * Phase C: MI_BATCH_BUFFER_START and the render engine's initialising
 * batch (docs\plans\intel-haswell-direct3d.md). Expected values worked
 * from Vol 2b and Vol 2d by hand: the start is opcode 31h with length 0
 * and no flags; the NULL surface is SURFTYPE_NULL (7) at 31:29 with
 * B8G8R8A8_UNORM (0C0h) at 26:18.
 */
static void test_phase_c_init(void)
{
    v9x_u32 dw[512];
    v9x_u32 image[V9X_HSW_INIT_STATE_BYTES / 4ul];
    struct v9x_hsw_batch batch;
    struct v9x_hsw_check check;
    v9x_u32 at;
    v9x_u32 index;
    v9x_u32 primitives;

    v9x_intel_hsw_batch_init(&batch, dw, 512ul);
    CHECK(v9x_intel_hsw_mi_batch_buffer_start(&batch, 0x01822000ul) ==
          V9X_STATUS_OK);
    CHECK(batch.used == 2ul && dw[0] == 0x18800000ul &&
          dw[1] == 0x01822000ul);
    CHECK(v9x_intel_hsw_mi_batch_buffer_start(&batch, 0x01822002ul) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(batch.used == 2ul);
    CHECK(v9x_intel_hsw_mi_batch_buffer_end(&batch) == V9X_STATUS_OK);
    CHECK(v9x_intel_hsw_mi_noop(&batch) == V9X_STATUS_OK);
    CHECK(v9x_intel_hsw_3d_check(dw, batch.used, &check) == V9X_STATUS_OK &&
          check.commands == 2ul);
    /* Any flag (PPGTT 8, non-privileged 13, second level 22) is refused. */
    dw[0] = 0x18800100ul;
    CHECK(v9x_intel_hsw_3d_check(dw, batch.used, &check) ==
              V9X_STATUS_INVALID_STATE &&
          check.result == V9X_HSW_CHECK_UNKNOWN_COMMAND);
    dw[0] = 0x18c00000ul;
    CHECK(v9x_intel_hsw_3d_check(dw, batch.used, &check) ==
          V9X_STATUS_INVALID_STATE);

    CHECK(v9x_intel_hsw_init_state(image, V9X_HSW_INIT_STATE_BYTES / 4ul) ==
          V9X_STATUS_OK);
    CHECK(image[V9X_HSW_INIT_CC_VIEWPORT / 4ul] == 0ul &&
          image[V9X_HSW_INIT_CC_VIEWPORT / 4ul + 1ul] == 0x3f800000ul);
    CHECK(image[V9X_HSW_INIT_NULL_SURFACE / 4ul] == 0xe3000000ul);
    CHECK(image[V9X_HSW_INIT_BINDING_TABLE / 4ul] ==
          V9X_HSW_INIT_NULL_SURFACE);
    CHECK(image[V9X_HSW_INIT_BLEND / 4ul] == 0ul &&
          image[V9X_HSW_INIT_CC / 4ul] == 0ul);
    CHECK(v9x_intel_hsw_init_state(image, 8ul) ==
          V9X_STATUS_INVALID_ARGUMENT);

    v9x_intel_hsw_batch_init(&batch, dw, 512ul);
    CHECK(v9x_intel_hsw_init_workload(&batch, 0x01824000ul, 0x2000ul,
                                      0x01821010ul, 0xc0de0031ul) ==
          V9X_STATUS_OK);
    CHECK(batch.overflow == 0u);
    CHECK(v9x_intel_hsw_3d_check(dw, batch.used, &check) == V9X_STATUS_OK);
    /* PIPELINE_SELECT 3D, one PIPE_CONTROL, then STATE_BASE_ADDRESS with
     * the state region as every base but the indirect one. */
    CHECK(dw[0] == 0x69040000ul);
    CHECK(dw[1] == 0x7a000003ul);
    CHECK(dw[6] == 0x61010008ul && dw[7] == 0x01824001ul &&
          dw[8] == 0x01824001ul && dw[9] == 0x01824001ul &&
          dw[10] == 0x00000001ul && dw[11] == 0x01824001ul);
    CHECK(dw[13] == 0x01826001ul && dw[15] == 0x01826001ul);
    /* It ends: breadcrumb PIPE_CONTROL, MI_BATCH_BUFFER_END, padding. */
    at = batch.used - 1ul;
    if (dw[at] == 0ul) {
        --at;
    }
    CHECK(dw[at] == 0x05000000ul);
    CHECK(at >= 5ul && dw[at - 5ul] == 0x7a000003ul &&
          dw[at - 4ul] == (V9X_HSW_PC_CS_STALL | V9X_HSW_PC_WRITE_IMMEDIATE |
                           V9X_HSW_PC_GGTT) &&
          dw[at - 3ul] == 0x01821010ul && dw[at - 2ul] == 0xc0de0031ul);
    /* No draw. */
    primitives = 0ul;
    for (index = 0ul; index < batch.used; ++index) {
        if (dw[index] == 0x7b000005ul) {
            ++primitives;
        }
    }
    CHECK(primitives == 0ul);
    /* The breadcrumb is a qword write; the bases are page aligned. */
    v9x_intel_hsw_batch_init(&batch, dw, 512ul);
    CHECK(v9x_intel_hsw_init_workload(&batch, 0x01824000ul, 0x2000ul,
                                      0x01821014ul, 1ul) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_intel_hsw_init_workload(&batch, 0x01824800ul, 0x2000ul,
                                      0x01821010ul, 1ul) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_intel_hsw_init_workload(&batch, 0x01824000ul, 0x100ul,
                                      0x01821010ul, 1ul) ==
          V9X_STATUS_INVALID_ARGUMENT);
    v9x_intel_hsw_batch_init(&batch, dw, 32ul);
    CHECK(v9x_intel_hsw_init_workload(&batch, 0x01824000ul, 0x2000ul,
                                      0x01821010ul, 1ul) ==
          V9X_STATUS_INSUFFICIENT_MEMORY);
}

unsigned int v9x_run_intel_hsw_3d_tests(void)
{
    failures = 0u;
    test_phase_c_init();
    test_headers();
    test_state();
    test_batch_and_check();
    test_qword_rule();
    test_phase_b1_stream();
    test_blits_by_depth();
    return failures;
}
