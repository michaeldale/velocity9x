/*
 * AMD Evergreen (Cedar) 3D command streams. See include\velocity9x\eg_3d.h
 * for the contract and the records each sequence ran in.
 */
#include "velocity9x/eg_3d.h"

/* evergreen_gpu_init's registers (evergreend.h) not named in the header. */
#define EG_HDP_MISC_CNTL        0x2f4cul
#define EG_CGTS_SYS_TCC_DISABLE 0x3f90ul
#define EG_CGTS_USER_SYS_TCC    0x3f94ul
#define EG_CP_QUEUE_THRESHOLDS  0x8760ul
#define EG_CP_MEQ_THRESHOLDS    0x8764ul
#define EG_CP_PERFMON_CNTL      0x87fcul
#define EG_VGT_CACHE_INVALIDATE 0x88c4ul
#define EG_VGT_GS_VERTEX_REUSE  0x88d4ul
#define EG_VGT_NUM_INSTANCES    0x8974ul
#define EG_PA_CL_ENHANCE        0x8a14ul
#define EG_PA_SU_LINE_STIPPLE   0x8a60ul
#define EG_PA_SC_LINE_STIPPLE   0x8b10ul
#define EG_PA_SC_FORCE_EOV      0x8b24ul
#define EG_PA_SC_FIFO_SIZE      0x8bccul
#define EG_SQ_GPR_MGMT_1        0x8c04ul
#define EG_SQ_GPR_MGMT_2        0x8c08ul
#define EG_SQ_GPR_MGMT_3        0x8c0cul
#define EG_SQ_THREAD_MGMT       0x8c18ul
#define EG_SQ_THREAD_MGMT_2     0x8c1cul
#define EG_SQ_STACK_MGMT_1      0x8c20ul
#define EG_SQ_STACK_MGMT_2      0x8c24ul
#define EG_SQ_STACK_MGMT_3      0x8c28ul
#define EG_SQ_MS_FIFO_SIZES     0x8cf0ul
#define EG_SX_EXPORT_SIZES      0x900cul
#define EG_SX_DEBUG_1           0x9058ul
#define EG_SPI_CONFIG_CNTL      0x9100ul
#define EG_SPI_CONFIG_CNTL_1    0x913cul
#define EG_CGTS_TCC_DISABLE     0x9148ul
#define EG_CGTS_USER_TCC        0x914cul
#define EG_TA_CNTL_AUX          0x9508ul
#define EG_GB_ADDR_CONFIG       0x98f8ul
#define EG_GB_BACKEND_MAP       0x98fcul
#define EG_CB_PERF_CTR0_SEL_0   0x9a20ul
#define EG_SMX_SAR_CTL0         0xa008ul
#define EG_SMX_DC_CTL0          0xa020ul

/*
 * Cedar's numbers in evergreen_gpu_init (1 SE, 2 pipes, 2 SIMDs, 1
 * backend, 256 GPRs, 192 threads, 256 stack entries, 4 SX sets, export
 * sizes 128/32/96, FIFOs 40h/30h/130h), worked through evergreend.h's
 * field macros. Each was written by V9XEG3D on M-BXMFA8.
 */
#define EG_CEDAR_GB_ADDR_CONFIG 0x02010001ul /* CEDAR_GB_ADDR_CONFIG_GOLDEN */
#define EG_GRBM_CNTL_VALUE      0x000000fful /* GRBM_READ_TIMEOUT(0xff) */
#define EG_CP_QUEUE_VALUE       0x00002b16ul /* ROQ_IB1_START(16h) | ROQ_IB2_START(2Bh) */
#define EG_CP_MEQ_VALUE         0x00000030ul /* STQ_SPLIT(30h) */
/* DISABLE_CUBE_ANISO | SYNC_GRADIENT | SYNC_WALKER | SYNC_ALIGNER */
#define EG_TA_CNTL_AUX_VALUE    0x07000002ul
#define EG_ENABLE_NEW_SMX       0x00010000ul
#define EG_SMX_SETS_KEEP        0xfffffc01ul /* NUMBER_OF_SETS cleared */
#define EG_SMX_SETS_CEDAR       0x00000008ul /* NUMBER_OF_SETS(4) */
#define EG_SMX_SAR_VALUE        0x00010000ul
/* COLOR (128/4-1) | POSITION (32/4-1) << 8 | SMX (96/4-1) << 16 */
#define EG_SX_EXPORT_VALUE      0x0017071ful
/* SC_PRIM 40h | SC_HIZ_TILE 30h << 12 | SC_EARLYZ_TILE 130h << 20 */
#define EG_PA_SC_FIFO_VALUE     0x13030040ul
#define EG_SPI_CNTL_1_VALUE     0x00000004ul /* VTX_DONE_DELAY(4) */
/* CACHE_FIFO_SIZE(16) | FETCH_FIFO_HIWATER(4) | DONE_FIFO_HIWATER(E0h) |
 * ALU_UPDATE_FIFO_HIWATER(8) */
#define EG_SQ_MS_FIFO_VALUE     0x08e00410ul
/* SQ_CONFIG: the priorities and VC_ENABLE cleared (Cedar has no vertex
 * cache), EXPORT_SRC_C | VS_PRIO(1) | GS_PRIO(2) | ES_PRIO(3) set. */
#define EG_SQ_CONFIG_KEEP       0x00fffffeul
#define EG_SQ_CONFIG_SET        0xe4000002ul
/* (256 - 8) * 12/32 = 93 PS, * 6/32 = 46 VS, 4 clause temporaries; * 4/32
 * = 31 GS and ES; * 3/32 = 23 HS and LS. */
#define EG_SQ_GPR_1_VALUE       0x402e005dul
#define EG_SQ_GPR_2_VALUE       0x001f001ful
#define EG_SQ_GPR_3_VALUE       0x00170017ul
/* 96 PS threads; ((192 - 96) / 6 / 8) * 8 = 16 for each other stage. */
#define EG_SQ_THREAD_VALUE      0x10101060ul
#define EG_SQ_THREAD_2_VALUE    0x00001010ul
/* 256 / 6 = 42 stack entries for every stage. */
#define EG_SQ_STACK_VALUE       0x002a002aul
/* FORCE_EOV_MAX_CLK_CNT(4095) | FORCE_EOV_MAX_REZ_CNT(255) */
#define EG_PA_SC_EOV_VALUE      0x00ff0ffful
/* CACHE_INVALIDATION(TC_ONLY) | AUTO_INVLD_EN(ES_AND_GS_AUTO) */
#define EG_VGT_CACHE_VALUE      0x000000c1ul
#define EG_HDP_FLUSH_INVALIDATE 0x00000001ul
/* CLIP_VTX_REORDER_ENA | NUM_CLIP_SEQ(3) */
#define EG_PA_CL_ENHANCE_VALUE  0x00000007ul

const struct v9x_eg_reg_update
    v9x_eg_cedar_gpu_init[V9X_EG_CEDAR_GPU_INIT_COUNT] = {
    { V9X_EG_GRBM_CNTL, 0ul, EG_GRBM_CNTL_VALUE },
    { EG_GB_ADDR_CONFIG, 0ul, EG_CEDAR_GB_ADDR_CONFIG },
    { EG_GB_BACKEND_MAP, 0ul, 0ul },
    { EG_CGTS_SYS_TCC_DISABLE, 0ul, 0ul },
    { EG_CGTS_TCC_DISABLE, 0ul, 0ul },
    { EG_CGTS_USER_SYS_TCC, 0ul, 0ul },
    { EG_CGTS_USER_TCC, 0ul, 0ul },
    { EG_CP_QUEUE_THRESHOLDS, 0ul, EG_CP_QUEUE_VALUE },
    { EG_CP_MEQ_THRESHOLDS, 0ul, EG_CP_MEQ_VALUE },
    { EG_TA_CNTL_AUX, 0ul, EG_TA_CNTL_AUX_VALUE },
    { EG_SX_DEBUG_1, 0xfffffffful, EG_ENABLE_NEW_SMX },
    { EG_SMX_DC_CTL0, EG_SMX_SETS_KEEP, EG_SMX_SETS_CEDAR },
    { EG_SMX_SAR_CTL0, 0ul, EG_SMX_SAR_VALUE },
    { EG_SX_EXPORT_SIZES, 0ul, EG_SX_EXPORT_VALUE },
    { EG_PA_SC_FIFO_SIZE, 0ul, EG_PA_SC_FIFO_VALUE },
    { EG_VGT_NUM_INSTANCES, 0ul, 1ul },
    { EG_SPI_CONFIG_CNTL, 0ul, 0ul },
    { EG_SPI_CONFIG_CNTL_1, 0ul, EG_SPI_CNTL_1_VALUE },
    { EG_CP_PERFMON_CNTL, 0ul, 0ul },
    { EG_SQ_MS_FIFO_SIZES, 0ul, EG_SQ_MS_FIFO_VALUE },
    { V9X_EG_SQ_CONFIG, EG_SQ_CONFIG_KEEP, EG_SQ_CONFIG_SET },
    { EG_SQ_GPR_MGMT_1, 0ul, EG_SQ_GPR_1_VALUE },
    { EG_SQ_GPR_MGMT_2, 0ul, EG_SQ_GPR_2_VALUE },
    { EG_SQ_GPR_MGMT_3, 0ul, EG_SQ_GPR_3_VALUE },
    { EG_SQ_THREAD_MGMT, 0ul, EG_SQ_THREAD_VALUE },
    { EG_SQ_THREAD_MGMT_2, 0ul, EG_SQ_THREAD_2_VALUE },
    { EG_SQ_STACK_MGMT_1, 0ul, EG_SQ_STACK_VALUE },
    { EG_SQ_STACK_MGMT_2, 0ul, EG_SQ_STACK_VALUE },
    { EG_SQ_STACK_MGMT_3, 0ul, EG_SQ_STACK_VALUE },
    { V9X_EG_SQ_DYN_GPR_FLUSH, 0ul, 0ul },
    { EG_PA_SC_FORCE_EOV, 0ul, EG_PA_SC_EOV_VALUE },
    { EG_VGT_CACHE_INVALIDATE, 0ul, EG_VGT_CACHE_VALUE },
    { EG_VGT_GS_VERTEX_REUSE, 0ul, 16ul },
    { EG_PA_SU_LINE_STIPPLE, 0ul, 0ul },
    { EG_PA_SC_LINE_STIPPLE, 0ul, 0ul },
    { EG_CB_PERF_CTR0_SEL_0 + 0x00ul, 0ul, 0ul },
    { EG_CB_PERF_CTR0_SEL_0 + 0x04ul, 0ul, 0ul },
    { EG_CB_PERF_CTR0_SEL_0 + 0x08ul, 0ul, 0ul },
    { EG_CB_PERF_CTR0_SEL_0 + 0x0cul, 0ul, 0ul },
    { EG_CB_PERF_CTR0_SEL_0 + 0x10ul, 0ul, 0ul },
    { EG_CB_PERF_CTR0_SEL_0 + 0x14ul, 0ul, 0ul },
    { EG_CB_PERF_CTR0_SEL_0 + 0x18ul, 0ul, 0ul },
    { EG_CB_PERF_CTR0_SEL_0 + 0x1cul, 0ul, 0ul },
    { EG_HDP_MISC_CNTL, 0xfffffffful, EG_HDP_FLUSH_INVALIDATE },
    { EG_PA_CL_ENHANCE, 0ul, EG_PA_CL_ENHANCE_VALUE }
};

/*
 * evergreen_blit_shaders.c (Linux v3.9; Copyright 2010 Advanced Micro
 * Devices, Inc.; MIT licence, reproduced in that file): Evergreen's
 * default context state, little-endian, as V9XEG3D sends it.
 */
static const v9x_u32 v9x_eg_default_context[] = {
    0xc0016900ul, 0x0000023bul, 0x00000000ul,
    0xc0066900ul, 0x00000240ul, 0x00000000ul, 0x00000000ul, 0x00000000ul,
    0x00000000ul, 0x00000000ul, 0x00000000ul,
    0xc0046900ul, 0x00000247ul, 0x00000000ul, 0x00000000ul, 0x00000000ul,
    0x00000000ul,
    0xc0026900ul, 0x00000010ul, 0x00000000ul, 0x00000000ul,
    0xc0016900ul, 0x00000200ul, 0x00000000ul,
    0xc0066900ul, 0x00000000ul, 0x00000060ul, 0x00000000ul, 0x00000000ul,
    0x0000002aul, 0x00000000ul, 0x00000000ul,
    0xc0026900ul, 0x0000000aul, 0x00000000ul, 0x00000000ul,
    0xc0016900ul, 0x000002dcul, 0x0000aa00ul,
    0xc0016900ul, 0x00000080ul, 0x00000000ul,
    0xc00d6900ul, 0x00000083ul, 0x0000fffful, 0x00000000ul, 0x20002000ul,
    0x00000000ul, 0x20002000ul, 0x00000000ul, 0x20002000ul, 0x00000000ul,
    0x20002000ul, 0xaaaaaaaaul, 0x00000000ul, 0x0000000ful, 0x0000000ful,
    0xc0226900ul, 0x00000094ul,
    0x80000000ul, 0x20002000ul, 0x80000000ul, 0x20002000ul,
    0x80000000ul, 0x20002000ul, 0x80000000ul, 0x20002000ul,
    0x80000000ul, 0x20002000ul, 0x80000000ul, 0x20002000ul,
    0x80000000ul, 0x20002000ul, 0x80000000ul, 0x20002000ul,
    0x80000000ul, 0x20002000ul, 0x80000000ul, 0x20002000ul,
    0x80000000ul, 0x20002000ul, 0x80000000ul, 0x20002000ul,
    0x80000000ul, 0x20002000ul, 0x80000000ul, 0x20002000ul,
    0x80000000ul, 0x20002000ul, 0x80000000ul, 0x20002000ul,
    0x00000000ul, 0x3f800000ul,
    0xc0016900ul, 0x000000d4ul, 0x00000000ul,
    0xc0026900ul, 0x00000292ul, 0x00000000ul, 0x00000000ul,
    0xc0106900ul, 0x00000300ul, 0x00000000ul, 0x00000000ul, 0x00000005ul,
    0x3f800000ul, 0x3f800000ul, 0x3f800000ul, 0x3f800000ul, 0x00000000ul,
    0x00000000ul, 0x00000000ul, 0x00000000ul, 0x00000000ul, 0x00000000ul,
    0x00000000ul, 0x00000000ul, 0xfffffffful,
    0xc00d6900ul, 0x00000202ul, 0x00cc0010ul, 0x00000210ul, 0x00010000ul,
    0x00000004ul, 0x00000100ul, 0x00000000ul, 0x00000000ul, 0x00000000ul,
    0x00000000ul, 0x00000000ul, 0x00000000ul, 0x00000000ul, 0x00000000ul,
    0xc0066900ul, 0x000002deul, 0x00000000ul, 0x00000000ul, 0x00000000ul,
    0x00000000ul, 0x00000000ul, 0x00000000ul,
    0xc0016900ul, 0x00000229ul, 0x00000000ul,
    0xc0016900ul, 0x0000022aul, 0x00000000ul,
    0xc0096900ul, 0x00000100ul, 0x00fffffful, 0x00000000ul, 0x00000000ul,
    0x00000000ul, 0x00000000ul, 0x00000000ul, 0x00000000ul, 0x00000000ul,
    0x00000000ul,
    0xc0026900ul, 0x000002a8ul, 0x00000000ul, 0x00000000ul,
    0xc0026900ul, 0x000002adul, 0x00000000ul, 0x00000000ul,
    0xc0116900ul, 0x00000280ul, 0x00000000ul, 0x00000000ul, 0x00000008ul,
    0x00000000ul, 0x00000000ul, 0x00000000ul, 0x00000000ul, 0x00000000ul,
    0x00000000ul, 0x00000000ul, 0x00000000ul, 0x00000000ul, 0x00000000ul,
    0x00000000ul, 0x00000000ul, 0x00000000ul, 0x00000000ul,
    0xc0016900ul, 0x000002a1ul, 0x00000000ul,
    0xc0016900ul, 0x000002a5ul, 0x00000000ul,
    0xc0016900ul, 0x000002d5ul, 0x00000000ul,
    0xc0026900ul, 0x000002e5ul, 0x00000000ul, 0x00000000ul,
    0xc0016900ul, 0x000001e0ul, 0x00000000ul,
    0xc0016900ul, 0x000001b1ul, 0x00000000ul,
    0xc0016900ul, 0x00000187ul, 0x00000000ul,
    0xc0016900ul, 0x00000191ul, 0x00000100ul,
    0xc00b6900ul, 0x000001b3ul, 0x20000001ul, 0x00000000ul, 0x00000000ul,
    0x00000000ul, 0x00000000ul, 0x00100000ul, 0x00000000ul, 0x00000000ul,
    0x00000000ul, 0x00000000ul, 0x00000000ul,
    0xc0026900ul, 0x00000316ul, 0x0000000eul, 0x00000010ul
};
#define EG_DEFAULT_CONTEXT_DWORDS \
    (sizeof(v9x_eg_default_context) / sizeof(v9x_eg_default_context[0]))

/* Context registers a batch writes (evergreend.h). */
#define EG_PA_SC_SCREEN_SCISSOR_TL  0x28030ul
#define EG_DB_Z_INFO                0x28040ul
#define EG_PA_SC_WINDOW_SCISSOR_TL  0x28204ul
#define EG_PA_SC_GENERIC_SCISSOR_TL 0x28240ul
#define EG_SX_ALPHA_TEST_CONTROL    0x28410ul
#define EG_SX_ALPHA_REF             0x28438ul
#define EG_SPI_VS_OUT_ID_0          0x2861cul
#define EG_SPI_PS_INPUT_CNTL_0      0x28644ul
#define EG_SPI_VS_OUT_CONFIG        0x286c4ul
#define EG_SPI_PS_IN_CONTROL_0      0x286ccul
#define EG_SPI_BARYC_CNTL           0x286e0ul
#define EG_CB_TARGET_MASK           0x28238ul
#define EG_CB_BLEND0_CONTROL        0x28780ul
#define EG_DB_DEPTH_CONTROL         0x28800ul
#define EG_PA_CL_VTE_CNTL           0x28818ul
#define EG_SQ_PGM_START_PS          0x28840ul
#define EG_SQ_PGM_START_VS          0x2885cul
#define EG_PA_SU_VTX_CNTL           0x28c08ul
#define EG_CB_COLOR0_BASE           0x28c60ul

/* Fields. */
#define EG_WINDOW_OFFSET_DISABLE    0x80000000ul
#define EG_SCISSOR_MAX              16384ul
#define EG_CB_SOURCE_FORMAT_NORM    0x01000000ul /* SOURCE_FORMAT(1) << 24 */
#define EG_CB_NON_DISP_TILING_ORDER 0x00000010ul
#define EG_CB_COLOR0_DWORDS         15ul
/* CB_TARGET_MASK's target-0 nibble: x, y, z and w are B, G, R and A in
 * the memory order the PS exports (eg_shader.c). */
#define EG_CB_WRITE_RGBA            0x0000000ful
#define EG_CB_WRITE_RGB             0x00000007ul
#define EG_DB_Z_INFO_DWORDS         8ul
#define EG_DB_FORMAT_Z_16           1ul
#define EG_PS_EXPORTS_COLOR         2ul
#define EG_SPI_SEMANTIC_BITS        8ul          /* SEMANTIC_n per byte */
#define EG_SPI_PS_INPUT_DEFAULT_1   0x00000100ul /* DEFAULT_VAL(1) */
#define EG_SPI_PERSP_GRADIENT_ENA   0x10000000ul
#define EG_SPI_LINEAR_GRADIENT_ENA  0x20000000ul
#define EG_SPI_PERSP_CENTER_ENA     0x00000001ul
#define EG_SPI_LINEAR_CENTER_ENA    0x00010000ul
/* VTX_XY_FMT and VTX_Z_FMT: x, y and z already divided. VTX_W0_FMT stays
 * clear: with it clear the position's w, Direct3D's rhw, weights the
 * perspective barycentrics correctly, every texel of V9XEG3D /d3dpersp
 * exact; set, the weighting came out inverted (M-BXMFA8, 2026-10-27,
 * docs\decisions\2026-10-27-cedar-d3d-streams.md). */
#define EG_VTE_XY_Z_DIVIDED         0x00000300ul
/* PIX_CENTER_HALF and ROUND_MODE(2), the default state's 5. */
#define EG_VTX_CNTL_ROUND           0x00000004ul
#define EG_VTX_CNTL_CENTRE_HALF     0x00000001ul
#define EG_TEX_DIM_2D               1ul
#define EG_TEX_PITCH_SHIFT          6
#define EG_TEX_WIDTH_SHIFT          18
#define EG_TEX_ARRAY_MODE_SHIFT     28
#define EG_TEX_SWIZZLE_XYZW         ((0ul << 16) | (1ul << 19) | (2ul << 22) | (3ul << 25))
/* DST_SEL_W 5: the constant 1 (SQ_SEL_1). */
#define EG_TEX_SWIZZLE_XYZ1         ((0ul << 16) | (1ul << 19) | (2ul << 22) | (5ul << 25))
#define EG_TEX_VALID_TEXTURE        0x80000000ul /* TYPE(2) << 30 */
#define EG_VTX_VALID_BUFFER         0xc0000000ul /* TYPE(3) << 30 */
#define EG_VTX_SWIZZLE_XYZW         ((0ul << 3) | (1ul << 6) | (2ul << 9) | (3ul << 12))
#define EG_VTX_STRIDE_SHIFT         8
/* The first VS fetch resource, as Linux 3.9's set_vtx_resource names it. */
#define EG_VTX_RESOURCE_SLOT        0x580ul
#define EG_SAMPLER_CLAMP_Z_SHIFT    6
#define EG_SAMPLER_MAG_SHIFT        9
#define EG_SAMPLER_MIN_SHIFT        11
#define EG_SAMPLER_MIP_SHIFT        15
#define EG_SAMPLER_MAX_LOD_SHIFT    12
#define EG_LOD_ONE                  256ul /* 4.8 fixed point */
#define EG_BLEND_ENABLE             0x40000000ul
#define EG_DEPTH_Z_ENABLE           0x00000002ul
#define EG_DEPTH_Z_WRITE            0x00000004ul
#define EG_ALPHA_TEST_ENABLE        0x00000008ul
#define EG_DI_PT_TRILIST            4ul
#define EG_DI_INDEX_SIZE_16_BIT     0ul
#define EG_DI_SRC_SEL_AUTO_INDEX    2ul
#define EG_SYNC_POLL_INTERVAL       10ul
#define EG_SYNC_ALL                 0xfffffffful
#define EG_ALIGN_256                0xfful

static v9x_u32 v9x_eg_packet3(v9x_u32 op, v9x_u32 body_dwords)
{
    return (3ul << 30) | ((op & 0xfful) << 8) |
           (((body_dwords - 1ul) & V9X_EG_PACKET3_BODY_MAX) << 16);
}

void v9x_eg_pm4_begin(struct v9x_eg_pm4 *stream, v9x_u32 *buffer,
                      v9x_u32 capacity)
{
    stream->dwords = buffer;
    stream->count = 0ul;
    stream->capacity = capacity;
    stream->overflow = 0ul;
}

void v9x_eg_pm4_put(struct v9x_eg_pm4 *stream, v9x_u32 value)
{
    if (stream->count >= stream->capacity) {
        stream->overflow = 1ul;
        return;
    }
    stream->dwords[stream->count++] = value;
}

static void v9x_eg_pm4_set_range(struct v9x_eg_pm4 *stream, v9x_u32 op,
                                 v9x_u32 start, v9x_u32 end, v9x_u32 offset,
                                 const v9x_u32 *values, v9x_u32 count)
{
    v9x_u32 index;

    if (count == 0ul || count >= V9X_EG_PACKET3_BODY_MAX ||
        offset < start || (offset & 3ul) != 0ul ||
        offset + count * 4ul > end) {
        stream->overflow = 1ul;
        return;
    }
    v9x_eg_pm4_put(stream, v9x_eg_packet3(op, count + 1ul));
    v9x_eg_pm4_put(stream, (offset - start) >> 2);
    for (index = 0ul; index < count; ++index) {
        v9x_eg_pm4_put(stream, values[index]);
    }
}

void v9x_eg_pm4_set_config(struct v9x_eg_pm4 *stream, v9x_u32 offset,
                           const v9x_u32 *values, v9x_u32 count)
{
    v9x_eg_pm4_set_range(stream, V9X_EG_P3_SET_CONFIG_REG,
                         V9X_EG_SET_CONFIG_START, V9X_EG_SET_CONFIG_END,
                         offset, values, count);
}

void v9x_eg_pm4_set_context(struct v9x_eg_pm4 *stream, v9x_u32 offset,
                            const v9x_u32 *values, v9x_u32 count)
{
    v9x_eg_pm4_set_range(stream, V9X_EG_P3_SET_CONTEXT_REG,
                         V9X_EG_SET_CONTEXT_START, V9X_EG_SET_CONTEXT_END,
                         offset, values, count);
}

void v9x_eg_pm4_set_config1(struct v9x_eg_pm4 *stream, v9x_u32 offset,
                            v9x_u32 value)
{
    v9x_eg_pm4_set_config(stream, offset, &value, 1ul);
}

void v9x_eg_pm4_set_context1(struct v9x_eg_pm4 *stream, v9x_u32 offset,
                             v9x_u32 value)
{
    v9x_eg_pm4_set_context(stream, offset, &value, 1ul);
}

void v9x_eg_pm4_surface_sync(struct v9x_eg_pm4 *stream, v9x_u32 actions,
                             v9x_u32 size, v9x_u32 address)
{
    v9x_eg_pm4_put(stream, v9x_eg_packet3(V9X_EG_P3_SURFACE_SYNC, 4ul));
    v9x_eg_pm4_put(stream, actions);
    if (size == EG_SYNC_ALL) {
        v9x_eg_pm4_put(stream, EG_SYNC_ALL);
        v9x_eg_pm4_put(stream, 0ul);
    } else {
        v9x_eg_pm4_put(stream, (size + EG_ALIGN_256) >> 8);
        v9x_eg_pm4_put(stream, address >> 8);
    }
    v9x_eg_pm4_put(stream, EG_SYNC_POLL_INTERVAL);
}

void v9x_eg_pm4_pad(struct v9x_eg_pm4 *stream)
{
    while ((stream->count % V9X_EG_CP_ALIGN_DWORDS) != 0ul &&
           stream->overflow == 0ul) {
        v9x_eg_pm4_put(stream, V9X_EG_PACKET2);
    }
}

void v9x_eg_3d_me_initialize(struct v9x_eg_pm4 *stream)
{
    /* evergreen_cp_start: MAX_HW_CONTEXTS - 1 = 3, DEVICE_ID(1). */
    v9x_eg_pm4_put(stream, v9x_eg_packet3(V9X_EG_P3_ME_INITIALIZE, 6ul));
    v9x_eg_pm4_put(stream, 0x1ul);
    v9x_eg_pm4_put(stream, 0x0ul);
    v9x_eg_pm4_put(stream, 3ul);
    v9x_eg_pm4_put(stream, 1ul << 16);
    v9x_eg_pm4_put(stream, 0ul);
    v9x_eg_pm4_put(stream, 0ul);
}

#define EG_PREAMBLE_BEGIN_CLEAR 0x20000000ul
#define EG_PREAMBLE_END_CLEAR   0x30000000ul
/* SET_CTL_CONST at offset 0: SQ_VTX_BASE_VTX_LOC and SQ_VTX_START_INST_LOC. */
#define EG_CTL_CONST_VTX_BASE   0ul
/* SET_ALU_CONST's slot Linux clears (0xbc4), and its value. */
#define EG_ALU_CONST_CLEAR_OP   0x6ful
#define EG_ALU_CONST_CLEAR_SLOT 0x00000bc4ul
#define EG_VGT_REUSE_BLOCK      0x00000316ul
#define EG_VGT_REUSE_VALUE      0x0000000eul
#define EG_VGT_DEALLOC_VALUE    0x00000010ul

void v9x_eg_3d_preamble(struct v9x_eg_pm4 *stream)
{
    v9x_u32 index;
    v9x_u32 values[2];

    v9x_eg_pm4_put(stream, v9x_eg_packet3(V9X_EG_P3_PREAMBLE_CNTL, 1ul));
    v9x_eg_pm4_put(stream, EG_PREAMBLE_BEGIN_CLEAR);
    for (index = 0ul; index < EG_DEFAULT_CONTEXT_DWORDS; ++index) {
        v9x_eg_pm4_put(stream, v9x_eg_default_context[index]);
    }
    v9x_eg_pm4_put(stream, v9x_eg_packet3(V9X_EG_P3_PREAMBLE_CNTL, 1ul));
    v9x_eg_pm4_put(stream, EG_PREAMBLE_END_CLEAR);
    v9x_eg_pm4_put(stream, v9x_eg_packet3(V9X_EG_P3_CLEAR_STATE, 1ul));
    v9x_eg_pm4_put(stream, 0ul);
    /* SQ_VTX_BASE_VTX_LOC */
    v9x_eg_pm4_put(stream, v9x_eg_packet3(V9X_EG_P3_SET_CTL_CONST, 3ul));
    v9x_eg_pm4_put(stream, EG_CTL_CONST_VTX_BASE);
    v9x_eg_pm4_put(stream, 0ul);
    v9x_eg_pm4_put(stream, 0ul);
    /* Clear consts: Linux's 0xc0036f00 packet, SET_CTL_CONST of three
     * words at 0xbc4. */
    v9x_eg_pm4_put(stream, v9x_eg_packet3(EG_ALU_CONST_CLEAR_OP, 4ul));
    v9x_eg_pm4_put(stream, EG_ALU_CONST_CLEAR_SLOT);
    v9x_eg_pm4_put(stream, 0xfffffffful);
    v9x_eg_pm4_put(stream, 0xfffffffful);
    v9x_eg_pm4_put(stream, 0xfffffffful);
    /* VGT_VERTEX_REUSE_BLOCK_CNTL, VGT_OUT_DEALLOC_CNTL */
    values[0] = EG_VGT_REUSE_VALUE;
    values[1] = EG_VGT_DEALLOC_VALUE;
    v9x_eg_pm4_set_context(stream,
                           V9X_EG_SET_CONTEXT_START + EG_VGT_REUSE_BLOCK * 4ul,
                           values, 2ul);
}

/* CONTEXT_CONTROL's LOAD_ENABLE and SHADOW_ENABLE words. */
#define EG_CONTEXT_CONTROL_LOAD   0x80000000ul
#define EG_CONTEXT_CONTROL_SHADOW 0x80000000ul
/* SQ_LDS_RESOURCE_MGMT for Cedar's set_default_state. */
#define EG_SQ_LDS_VALUE           0x10001000ul
/* Sampler 0 as Linux leaves it: CLAMP_X and CLAMP_Y CLAMP_LAST_TEXEL. */
#define EG_SAMPLER_DEFAULT_WORD0  0x00000012ul
#define EG_MODE_CONTROL_DX10      1ul

void v9x_eg_3d_default_state(struct v9x_eg_pm4 *stream)
{
    v9x_u32 sq[11];
    v9x_u32 index;

    v9x_eg_pm4_put(stream, v9x_eg_packet3(V9X_EG_P3_CLEAR_STATE, 1ul));
    v9x_eg_pm4_put(stream, 0ul);
    v9x_eg_pm4_set_config1(stream, V9X_EG_SQ_DYN_GPR_FLUSH, 0ul);
    v9x_eg_pm4_set_config1(stream, V9X_EG_SQ_LDS_RESOURCE_MGMT,
                           EG_SQ_LDS_VALUE);
    sq[0] = EG_SQ_CONFIG_SET;
    sq[1] = EG_SQ_GPR_1_VALUE;
    sq[2] = EG_SQ_GPR_2_VALUE;
    sq[3] = EG_SQ_GPR_3_VALUE;
    sq[4] = 0ul;
    sq[5] = 0ul;
    sq[6] = EG_SQ_THREAD_VALUE;
    sq[7] = EG_SQ_THREAD_2_VALUE;
    sq[8] = EG_SQ_STACK_VALUE;
    sq[9] = EG_SQ_STACK_VALUE;
    sq[10] = EG_SQ_STACK_VALUE;
    v9x_eg_pm4_set_config(stream, V9X_EG_SQ_CONFIG, sq, 11ul);
    v9x_eg_pm4_put(stream, v9x_eg_packet3(V9X_EG_P3_CONTEXT_CONTROL, 2ul));
    v9x_eg_pm4_put(stream, EG_CONTEXT_CONTROL_LOAD);
    v9x_eg_pm4_put(stream, EG_CONTEXT_CONTROL_SHADOW);
    v9x_eg_pm4_put(stream, v9x_eg_packet3(V9X_EG_P3_SET_CTL_CONST, 3ul));
    v9x_eg_pm4_put(stream, EG_CTL_CONST_VTX_BASE);
    v9x_eg_pm4_put(stream, 0ul);
    v9x_eg_pm4_put(stream, 0ul);
    v9x_eg_pm4_put(stream, v9x_eg_packet3(V9X_EG_P3_SET_SAMPLER, 4ul));
    v9x_eg_pm4_put(stream, 0ul);
    v9x_eg_pm4_put(stream, EG_SAMPLER_DEFAULT_WORD0);
    v9x_eg_pm4_put(stream, 0ul);
    v9x_eg_pm4_put(stream, 0ul);
    v9x_eg_pm4_put(stream, v9x_eg_packet3(V9X_EG_P3_MODE_CONTROL, 1ul));
    v9x_eg_pm4_put(stream, EG_MODE_CONTROL_DX10);
    for (index = 0ul; index < EG_DEFAULT_CONTEXT_DWORDS; ++index) {
        v9x_eg_pm4_put(stream, v9x_eg_default_context[index]);
    }
}

v9x_u32 v9x_eg_3d_depth_control(v9x_u32 enable, v9x_u32 write, v9x_u32 func)
{
    return (enable != 0ul ? EG_DEPTH_Z_ENABLE : 0ul) |
           (write != 0ul ? EG_DEPTH_Z_WRITE : 0ul) | ((func & 7ul) << 4);
}

/* One pair of factors for colour and alpha, added. Disabled, the word
 * still says ONE, ZERO: what the CB does with blending off. */
v9x_u32 v9x_eg_3d_blend(v9x_u32 enable, v9x_u32 src, v9x_u32 dst)
{
    if (enable == 0ul) {
        src = V9X_EG_BLEND_ONE;
        dst = V9X_EG_BLEND_ZERO;
    }
    return (src & 0x1ful) | ((dst & 0x1ful) << 8) |
           ((src & 0x1ful) << 16) | ((dst & 0x1ful) << 24) |
           (enable != 0ul ? EG_BLEND_ENABLE : 0ul);
}

v9x_u32 v9x_eg_3d_alpha_test(v9x_u32 enable, v9x_u32 func)
{
    return (func & 7ul) | (enable != 0ul ? EG_ALPHA_TEST_ENABLE : 0ul);
}

/* No mip filter and CLAMP_Z at the last texel, Linux's choice for a 2D
 * texture. A nonzero filter argument is bilinear, zero point. */
v9x_u32 v9x_eg_3d_sampler(v9x_u32 mag_bilinear, v9x_u32 min_bilinear,
                          v9x_u32 clamp_u, v9x_u32 clamp_v)
{
    return (clamp_u & 7ul) | ((clamp_v & 7ul) << 3) |
           (V9X_EG_TEX_CLAMP_LAST_TEXEL << EG_SAMPLER_CLAMP_Z_SHIFT) |
           ((mag_bilinear != 0ul ? 1ul : 0ul) << EG_SAMPLER_MAG_SHIFT) |
           ((min_bilinear != 0ul ? 1ul : 0ul) << EG_SAMPLER_MIN_SHIFT);
}

/* Bytes a texel of a sampled format takes, or 0. */
static v9x_u32 v9x_eg_3d_texel_bytes(v9x_u32 format)
{
    if (format == V9X_EG_COLOR_5_6_5 || format == V9X_EG_COLOR_1_5_5_5 ||
        format == V9X_EG_COLOR_4_4_4_4) {
        return 2ul;
    }
    return format == V9X_EG_COLOR_8_8_8_8 ? 4ul : 0ul;
}

v9x_u32 v9x_eg_3d_texture_pitch(v9x_u32 format, v9x_u32 width)
{
    v9x_u32 bytes = v9x_eg_3d_texel_bytes(format);
    v9x_u32 row;

    if (bytes == 0ul) {
        return 0ul;
    }
    row = (width * bytes + V9X_EG_TEXTURE_ROW_ALIGN - 1ul) &
          ~(V9X_EG_TEXTURE_ROW_ALIGN - 1ul);
    return row / bytes;
}

/* A chain's pitch alignment in texels: MAX(64, 256 / bytes per texel). */
#define EG_MIP_PITCH_BYTES 256ul
#define EG_MIP_PITCH_MIN   64ul

static v9x_u32 v9x_eg_3d_mip_palign(v9x_u32 bytes)
{
    v9x_u32 palign = EG_MIP_PITCH_BYTES / bytes;

    return palign > EG_MIP_PITCH_MIN ? palign : EG_MIP_PITCH_MIN;
}

int v9x_eg_3d_mip_layout(v9x_u32 format, v9x_u32 width, v9x_u32 height,
                         v9x_u32 levels, v9x_u32 *offsets,
                         v9x_u32 *pitches, v9x_u32 *total_bytes)
{
    v9x_u32 bytes = v9x_eg_3d_texel_bytes(format);
    v9x_u32 palign;
    v9x_u32 offset = 0ul;
    v9x_u32 level;
    v9x_u32 w = width;
    v9x_u32 h = height;
    v9x_u32 longest = width > height ? width : height;
    v9x_u32 possible = 1ul;

    if (bytes == 0ul || width == 0ul || height == 0ul || levels == 0ul ||
        levels > V9X_EG_MIP_LEVELS_MAX) {
        return 0;
    }
    while (longest > 1ul) {
        longest >>= 1;
        ++possible;
    }
    if (levels > possible) {
        return 0;
    }
    palign = v9x_eg_3d_mip_palign(bytes);
    for (level = 0ul; level < levels; ++level) {
        offsets[level] = offset;
        pitches[level] = (w + palign - 1ul) & ~(palign - 1ul);
        offset += pitches[level] * bytes * h;
        w = w > 1ul ? w >> 1 : 1ul;
        h = h > 1ul ? h >> 1 : 1ul;
    }
    *total_bytes = offset;
    return 1;
}

static int v9x_eg_3d_aligned(v9x_u32 gpu)
{
    return (gpu & EG_ALIGN_256) == 0ul;
}

static int v9x_eg_3d_state_valid(const struct v9x_eg_3d_state *state)
{
    if (!v9x_eg_3d_aligned(state->target_gpu) ||
        state->target_pitch == 0ul || (state->target_pitch & 7ul) != 0ul ||
        state->target_height == 0ul ||
        state->target_pitch > EG_SCISSOR_MAX ||
        state->target_height > EG_SCISSOR_MAX ||
        (state->target_format != V9X_EG_COLOR_5_6_5 &&
         state->target_format != V9X_EG_COLOR_8_8_8_8)) {
        return 0;
    }
    if (state->scissor_left >= state->scissor_right ||
        state->scissor_top >= state->scissor_bottom ||
        state->scissor_right > state->target_pitch ||
        state->scissor_bottom > state->target_height) {
        return 0;
    }
    if (state->has_depth != 0ul &&
        (!v9x_eg_3d_aligned(state->depth_gpu) || state->depth_pitch == 0ul ||
         (state->depth_pitch & 7ul) != 0ul || state->depth_height == 0ul ||
         (state->depth_height & 7ul) != 0ul ||
         state->depth_pitch < state->scissor_right ||
         state->depth_height < state->scissor_bottom)) {
        return 0;
    }
    if (!v9x_eg_3d_aligned(state->vs_gpu) ||
        !v9x_eg_3d_aligned(state->ps_gpu) ||
        state->vs_gprs == 0ul || state->ps_gprs == 0ul ||
        state->interpolants == 0ul ||
        state->interpolants > V9X_EG_INTERPOLANTS_MAX) {
        return 0;
    }
    if (state->has_texture != 0ul &&
        (!v9x_eg_3d_aligned(state->texture_gpu) ||
         state->texture_width == 0ul || state->texture_height == 0ul ||
         state->texture_pitch < state->texture_width ||
         state->texture_levels == 0ul ||
         state->texture_levels > V9X_EG_MIP_LEVELS_MAX ||
         (state->texture_levels == 1ul &&
          ((state->texture_pitch *
            v9x_eg_3d_texel_bytes(state->texture_format)) %
           V9X_EG_TEXTURE_ROW_ALIGN) != 0ul) ||
         (state->texture_levels > 1ul &&
          (!v9x_eg_3d_aligned(state->texture_mip_gpu) ||
           v9x_eg_3d_texel_bytes(state->texture_format) == 0ul ||
           (state->texture_pitch %
            v9x_eg_3d_mip_palign(
                v9x_eg_3d_texel_bytes(state->texture_format))) != 0ul)) ||
         state->texture_width > EG_SCISSOR_MAX ||
         state->texture_height > EG_SCISSOR_MAX ||
         (state->texture_format != V9X_EG_COLOR_5_6_5 &&
          state->texture_format != V9X_EG_COLOR_1_5_5_5 &&
          state->texture_format != V9X_EG_COLOR_4_4_4_4 &&
          state->texture_format != V9X_EG_COLOR_8_8_8_8))) {
        return 0;
    }
    return state->vertex_stride != 0ul &&
           state->vertex_bytes >= state->vertex_stride;
}

static void v9x_eg_3d_scissor(struct v9x_eg_pm4 *stream, v9x_u32 offset,
                              v9x_u32 tl, v9x_u32 br)
{
    v9x_u32 values[2];

    values[0] = tl;
    values[1] = br;
    v9x_eg_pm4_set_context(stream, offset, values, 2ul);
}

v9x_status v9x_eg_3d_build_state(const struct v9x_eg_3d_state *state,
                                 struct v9x_eg_pm4 *stream)
{
    v9x_u32 words[EG_CB_COLOR0_DWORDS];
    v9x_u32 index;
    v9x_u32 tl;
    v9x_u32 br;
    v9x_u32 slices;

    if (state == 0 || stream == 0 || !v9x_eg_3d_state_valid(state)) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    /* set_shaders: NUM_GPRS, and the PS exporting one colour. The shader
     * cache is flushed over everything: the VS and PS may lie apart. */
    words[0] = state->vs_gpu >> 8;
    words[1] = state->vs_gprs;
    words[2] = 0ul;
    v9x_eg_pm4_set_context(stream, EG_SQ_PGM_START_VS, words, 3ul);
    words[0] = state->ps_gpu >> 8;
    words[1] = state->ps_gprs;
    words[2] = 0ul;
    words[3] = EG_PS_EXPORTS_COLOR;
    v9x_eg_pm4_set_context(stream, EG_SQ_PGM_START_PS, words, 4ul);
    v9x_eg_pm4_surface_sync(stream, V9X_EG_SYNC_SH_ACTION, EG_SYNC_ALL, 0ul);

    /* The interpolants: export n carries semantic n and PS input n reads
     * it (input 0 keeps its reset value, semantic 0); linear or
     * perspective barycentrics, which INTERP reads from R0. */
    tl = 0ul;
    for (index = 1ul; index < state->interpolants; ++index) {
        tl |= index << (EG_SPI_SEMANTIC_BITS * index);
        v9x_eg_pm4_set_context1(stream,
                                EG_SPI_PS_INPUT_CNTL_0 + 4ul * index,
                                index | EG_SPI_PS_INPUT_DEFAULT_1);
    }
    v9x_eg_pm4_set_context1(stream, EG_SPI_VS_OUT_ID_0, tl);
    v9x_eg_pm4_set_context1(stream, EG_SPI_VS_OUT_CONFIG,
                            (state->interpolants - 1ul) << 1);
    v9x_eg_pm4_set_context1(stream, EG_SPI_PS_IN_CONTROL_0,
                            state->interpolants |
                            (state->perspective != 0ul ?
                             EG_SPI_PERSP_GRADIENT_ENA :
                             EG_SPI_LINEAR_GRADIENT_ENA));
    v9x_eg_pm4_set_context1(stream, EG_SPI_BARYC_CNTL,
                            state->perspective != 0ul ?
                            EG_SPI_PERSP_CENTER_ENA :
                            EG_SPI_LINEAR_CENTER_ENA);
    v9x_eg_pm4_set_context1(stream, EG_PA_CL_VTE_CNTL, EG_VTE_XY_Z_DIVIDED);
    v9x_eg_pm4_set_context1(stream, EG_PA_SU_VTX_CNTL,
                            EG_VTX_CNTL_ROUND |
                            (state->pixel_centre_half != 0ul ?
                             EG_VTX_CNTL_CENTRE_HALF : 0ul));

    /* set_tex_resource and sampler 0, the texture linear general: pitch a
     * multiple of 8 texels, swizzle XYZW, one level. The texture cache is
     * invalidated first: the CPU may have written the texels. */
    if (state->has_texture != 0ul) {
        v9x_eg_pm4_surface_sync(stream, V9X_EG_SYNC_TC_ACTION, EG_SYNC_ALL,
                                0ul);
        v9x_eg_pm4_put(stream, v9x_eg_packet3(V9X_EG_P3_SET_RESOURCE, 9ul));
        v9x_eg_pm4_put(stream, 0ul);
        v9x_eg_pm4_put(stream, EG_TEX_DIM_2D |
                       (((state->texture_pitch >> 3) - 1ul) <<
                        EG_TEX_PITCH_SHIFT) |
                       ((state->texture_width - 1ul) << EG_TEX_WIDTH_SHIFT));
        /* A chain is linear aligned, its levels where
         * v9x_eg_3d_mip_layout puts them; one level stays linear
         * general, whose 64-byte rows were measured. */
        v9x_eg_pm4_put(stream, (state->texture_height - 1ul) |
                       ((state->texture_levels > 1ul ?
                         V9X_EG_ARRAY_LINEAR_ALIGNED :
                         V9X_EG_ARRAY_LINEAR_GENERAL) <<
                        EG_TEX_ARRAY_MODE_SHIFT));
        v9x_eg_pm4_put(stream, state->texture_gpu >> 8);
        v9x_eg_pm4_put(stream, (state->texture_levels > 1ul ?
                                state->texture_mip_gpu :
                                state->texture_gpu) >> 8);
        /* A 565 texture has no fourth component, which samples as 0: its
         * W is the constant 1, so it is opaque, as Direct3D reads a
         * texture without alpha (V9XDDP's untagged texture blended away
         * under SRCALPHA, boot 21). */
        v9x_eg_pm4_put(stream, state->texture_format == V9X_EG_COLOR_5_6_5 ?
                               EG_TEX_SWIZZLE_XYZ1 : EG_TEX_SWIZZLE_XYZW);
        /* LAST_LEVEL, the chain's last; BASE_LEVEL 0. */
        v9x_eg_pm4_put(stream, state->texture_levels - 1ul);
        v9x_eg_pm4_put(stream, 0ul);
        v9x_eg_pm4_put(stream, state->texture_format | EG_TEX_VALID_TEXTURE);
        /* The sampler, its mip filter and its LOD range: MIN_LOD 0 and
         * MAX_LOD the last level, 4.8 fixed point (with 4.6 the sampler
         * read a quarter of the LOD, V9XEG3D /d3dmip, boot 23). */
        v9x_eg_pm4_put(stream, v9x_eg_packet3(V9X_EG_P3_SET_SAMPLER, 4ul));
        v9x_eg_pm4_put(stream, 0ul);
        v9x_eg_pm4_put(stream, state->sampler_word0 |
                               (state->texture_levels > 1ul ?
                                (state->mip_filter & 3ul) <<
                                EG_SAMPLER_MIP_SHIFT : 0ul));
        v9x_eg_pm4_put(stream, ((state->texture_levels - 1ul) *
                                EG_LOD_ONE) << EG_SAMPLER_MAX_LOD_SHIFT);
        v9x_eg_pm4_put(stream, 0ul);
    }

    /* set_render_target, linear general with NON_DISP_TILING_ORDER, which
     * every linear colour buffer needs. SLICE_TILE_MAX counts 64-pixel
     * tiles, rounded up for a height that is not a multiple of 8. */
    slices = (state->target_pitch * state->target_height + 63ul) / 64ul;
    words[0] = state->target_gpu >> 8;
    words[1] = (state->target_pitch >> 3) - 1ul;
    words[2] = slices - 1ul;
    words[3] = 0ul;
    words[4] = (state->target_format << 2) |
               (V9X_EG_ARRAY_LINEAR_GENERAL << 8) | EG_CB_SOURCE_FORMAT_NORM;
    words[5] = EG_CB_NON_DISP_TILING_ORDER;
    words[6] = (state->target_pitch - 1ul) |
               ((state->target_height - 1ul) << 16);
    for (index = 7ul; index < EG_CB_COLOR0_DWORDS; ++index) {
        words[index] = 0ul;
    }
    v9x_eg_pm4_set_context(stream, EG_CB_COLOR0_BASE, words,
                           EG_CB_COLOR0_DWORDS);
    v9x_eg_pm4_set_context1(stream, EG_CB_TARGET_MASK,
                            state->write_alpha != 0ul ? EG_CB_WRITE_RGBA
                                                      : EG_CB_WRITE_RGB);
    v9x_eg_pm4_set_context1(stream, EG_CB_BLEND0_CONTROL,
                            state->blend_control);

    /* The depth buffer: Z16 1D tiled, no stencil, the stencil bases on
     * the Z base. Without one, DB_Z_INFO keeps the default state's
     * INVALID format and the test is off. */
    if (state->has_depth != 0ul) {
        words[0] = EG_DB_FORMAT_Z_16 | (V9X_EG_ARRAY_1D_TILED_THIN1 << 4);
        words[1] = 0ul;
        words[2] = state->depth_gpu >> 8;
        words[3] = state->depth_gpu >> 8;
        words[4] = state->depth_gpu >> 8;
        words[5] = state->depth_gpu >> 8;
        words[6] = ((state->depth_pitch >> 3) - 1ul) |
                   (((state->depth_height >> 3) - 1ul) << 11);
        words[7] = ((state->depth_pitch * state->depth_height) >> 6) - 1ul;
        v9x_eg_pm4_set_context(stream, EG_DB_Z_INFO, words,
                               EG_DB_Z_INFO_DWORDS);
    }
    v9x_eg_pm4_set_context1(stream, EG_DB_DEPTH_CONTROL,
                            state->has_depth != 0ul ?
                            state->depth_control : 0ul);
    v9x_eg_pm4_set_context1(stream, EG_SX_ALPHA_TEST_CONTROL,
                            state->alpha_test_control);
    v9x_eg_pm4_set_context1(stream, EG_SX_ALPHA_REF, state->alpha_ref);

    /* set_scissors: the screen scissor at the target, the generic and
     * window scissors at the draw's rectangle. */
    v9x_eg_3d_scissor(stream, EG_PA_SC_SCREEN_SCISSOR_TL, 0ul,
                      state->target_pitch | (state->target_height << 16));
    tl = state->scissor_left | (state->scissor_top << 16);
    br = state->scissor_right | (state->scissor_bottom << 16);
    v9x_eg_3d_scissor(stream, EG_PA_SC_GENERIC_SCISSOR_TL,
                      tl | EG_WINDOW_OFFSET_DISABLE, br);
    v9x_eg_3d_scissor(stream, EG_PA_SC_WINDOW_SCISSOR_TL,
                      tl | EG_WINDOW_OFFSET_DISABLE, br);

    /* set_vtx_resource, then the texture cache (Cedar has no vertex
     * cache) invalidated over the vertices. */
    v9x_eg_pm4_put(stream, v9x_eg_packet3(V9X_EG_P3_SET_RESOURCE, 9ul));
    v9x_eg_pm4_put(stream, EG_VTX_RESOURCE_SLOT);
    v9x_eg_pm4_put(stream, state->vertex_gpu);
    v9x_eg_pm4_put(stream, state->vertex_bytes - 1ul);
    v9x_eg_pm4_put(stream, state->vertex_stride << EG_VTX_STRIDE_SHIFT);
    v9x_eg_pm4_put(stream, EG_VTX_SWIZZLE_XYZW);
    v9x_eg_pm4_put(stream, 0ul);
    v9x_eg_pm4_put(stream, 0ul);
    v9x_eg_pm4_put(stream, 0ul);
    v9x_eg_pm4_put(stream, EG_VTX_VALID_BUFFER);
    v9x_eg_pm4_surface_sync(stream, V9X_EG_SYNC_TC_ACTION,
                            state->vertex_bytes, state->vertex_gpu);
    return stream->overflow != 0ul ? V9X_STATUS_INSUFFICIENT_MEMORY
                                   : V9X_STATUS_OK;
}

void v9x_eg_3d_draw(struct v9x_eg_pm4 *stream, v9x_u32 vertex_count)
{
    v9x_eg_pm4_set_config1(stream, V9X_EG_VGT_PRIMITIVE_TYPE,
                           EG_DI_PT_TRILIST);
    v9x_eg_pm4_put(stream, v9x_eg_packet3(V9X_EG_P3_INDEX_TYPE, 1ul));
    v9x_eg_pm4_put(stream, EG_DI_INDEX_SIZE_16_BIT);
    v9x_eg_pm4_put(stream, v9x_eg_packet3(V9X_EG_P3_NUM_INSTANCES, 1ul));
    v9x_eg_pm4_put(stream, 1ul);
    v9x_eg_pm4_put(stream, v9x_eg_packet3(V9X_EG_P3_DRAW_INDEX_AUTO, 2ul));
    v9x_eg_pm4_put(stream, vertex_count);
    v9x_eg_pm4_put(stream, EG_DI_SRC_SEL_AUTO_INDEX);
}

void v9x_eg_3d_flush(struct v9x_eg_pm4 *stream)
{
    v9x_eg_pm4_surface_sync(stream,
                            V9X_EG_SYNC_CB_ACTION | V9X_EG_SYNC_CB0_DEST_BASE |
                            V9X_EG_SYNC_DB_ACTION | V9X_EG_SYNC_DB_DEST_BASE |
                            V9X_EG_SYNC_TC_ACTION,
                            EG_SYNC_ALL, 0ul);
}

void v9x_eg_3d_fence(struct v9x_eg_pm4 *stream, v9x_u32 value)
{
    v9x_eg_pm4_set_config1(stream, V9X_EG_SCRATCH_REG0, value);
}
