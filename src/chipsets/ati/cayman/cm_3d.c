/*
 * AMD Cayman 3D engine configuration. See include\velocity9x\cm_3d.h for
 * the contract and its sources.
 */
#include "velocity9x/cm_3d.h"

/* Registers, Linux nid.h. */
#define CM_GRBM_CNTL                0x8000ul
#define CM_GB_ADDR_CONFIG           0x98f8ul
#define CM_GB_BACKEND_MAP           0x98fcul
#define CM_CGTS_TCC_DISABLE         0x9148ul
#define CM_CGTS_SYS_TCC_DISABLE     0x3f90ul
#define CM_CGTS_USER_SYS_TCC        0x3f94ul
#define CM_CGTS_USER_TCC            0x914cul
#define CM_CP_MEQ_THRESHOLDS        0x8764ul
#define CM_SX_DEBUG_1               0x9058ul
#define CM_SMX_DC_CTL0              0xa020ul
#define CM_SPI_CONFIG_CNTL_1        0x913cul
#define CM_VGT_OFFCHIP_LDS_BASE     0x89b4ul
#define CM_SQ_LSTMP_RING_BASE       0x8e10ul
#define CM_SQ_HSTMP_RING_BASE       0x8e18ul
#define CM_SQ_ESTMP_RING_BASE       0x8c50ul
#define CM_SQ_GSTMP_RING_BASE       0x8c58ul
#define CM_SQ_VSTMP_RING_BASE       0x8c60ul
#define CM_SQ_PSTMP_RING_BASE       0x8c68ul
#define CM_TA_CNTL_AUX              0x9508ul
#define CM_SX_EXPORT_SIZES          0x900cul
#define CM_PA_SC_FIFO_SIZE          0x8bccul
#define CM_VGT_NUM_INSTANCES        0x8974ul
#define CM_CP_PERFMON_CNTL          0x87fcul
#define CM_SQ_MS_FIFO_SIZES         0x8cf0ul
#define CM_SQ_GPR_MGMT_1            0x8c04ul
#define CM_SQ_CONFIG                0x8c00ul
#define CM_SQ_DYN_GPR_FLUSH         0x8d8cul
#define CM_PA_SC_FORCE_EOV          0x8b24ul
#define CM_VGT_CACHE_INVALIDATE     0x88c4ul
#define CM_VGT_GS_VERTEX_REUSE      0x88d4ul
#define CM_PA_SC_LINE_STIPPLE       0x8b10ul
#define CM_CB_PERF_CTR0_SEL_0       0x9a20ul
#define CM_HDP_MISC_CNTL            0x2f4cul
#define CM_PA_CL_ENHANCE            0x8a14ul

/*
 * cayman_gpu_init's numbers for 9901 (ni.c:902-972) through nid.h's
 * fields.
 */
/* GRBM_READ_TIMEOUT(0xff) */
#define CM_GRBM_CNTL_VALUE          0x000000fful
/* 0xffff0000 with bits 16 and 17 cleared: max_texture_channel_caches 2. */
#define CM_CGTS_TCC_VALUE           0xfffc0000ul
/* MEQ1_START(0x30) | MEQ2_START(0x60) */
#define CM_CP_MEQ_VALUE             0x00006030ul
#define CM_ENABLE_NEW_SMX           0x00010000ul
/* NUMBER_OF_SETS(0x1ff) cleared, NUMBER_OF_SETS(sx_num_of_sets 8) set. */
#define CM_SMX_SETS_KEEP            0xfffffc01ul
#define CM_SMX_SETS_TRINITY         0x00000010ul
/* VTX_DONE_DELAY(4) | CRC_SIMD_ID_WADDR_DISABLE */
#define CM_SPI_CNTL_1_VALUE         0x00000104ul
#define CM_DISABLE_CUBE_ANISO       0x00000002ul
/* COLOR (256/4-1) | POSITION (64/4-1) << 8 | SMX (192/4-1) << 16 */
#define CM_SX_EXPORT_VALUE          0x002f0f3ful
/* SC_PRIM 40h | SC_HIZ_TILE 30h << 12 | SC_EARLYZ_TILE 130h << 20 */
#define CM_PA_SC_FIFO_VALUE         0x13030040ul
/* CACHE_FIFO_SIZE(16 * sq_num_cf_insts 2) | FETCH_FIFO_HIWATER(4) |
 * DONE_FIFO_HIWATER(E0h) | ALU_UPDATE_FIFO_HIWATER(8) */
#define CM_SQ_MS_FIFO_VALUE         0x08e00420ul
/* NUM_CLAUSE_TEMP_GPRS(4): Cayman sets no other split; its GPRs are
 * allocated dynamically (DYN_GPR_ENABLE below). */
#define CM_SQ_GPR_1_VALUE           0x40000000ul
/* VC_ENABLE | EXPORT_SRC_C | GFX_PRIO(0) | CS1_PRIO(0) | CS2_PRIO(1) */
#define CM_SQ_CONFIG_VALUE          0x00000043ul
#define CM_DYN_GPR_ENABLE           0x00000100ul
/* FORCE_EOV_MAX_CLK_CNT(4095) | FORCE_EOV_MAX_REZ_CNT(255) */
#define CM_PA_SC_EOV_VALUE          0x00ff0ffful
/* CACHE_INVALIDATION(VC_AND_TC) | AUTO_INVLD_EN(ES_AND_GS_AUTO) */
#define CM_VGT_CACHE_VALUE          0x000000c2ul
#define CM_HDP_FLUSH_INVALIDATE     0x00000001ul
/* CLIP_VTX_REORDER_ENA | NUM_CLIP_SEQ(3) */
#define CM_PA_CL_ENHANCE_VALUE      0x00000007ul

/* r6xx_remap_render_backend's fields: 4 bits each from Evergreen on, and
 * the 8-bit backend masks it works over. */
#define CM_RB_FIELD_BITS            4u
#define CM_RB_MASK_BITS             8u
#define CM_RB_MASK_ALL              0x000000fful
#define CM_TILING_PIPE_LOG2_MAX     3ul

static v9x_u32 v9x_cm_count_bits(v9x_u32 value)
{
    v9x_u32 count = 0ul;

    while (value != 0ul) {
        count += value & 1ul;
        value >>= 1;
    }
    return count;
}

v9x_u32 v9x_cm_backend_map(v9x_u32 tiling_pipe_log2, v9x_u32 max_rb,
                           v9x_u32 total_max_rb, v9x_u32 disabled_rb_mask)
{
    v9x_u32 widened;
    v9x_u32 pipes;
    v9x_u32 disabled_count;
    v9x_u32 required;
    v9x_u32 ratio;
    v9x_u32 remain;
    v9x_u32 mask;
    v9x_u32 data = 0ul;
    v9x_u32 index;
    v9x_u32 repeat;

    if (max_rb == 0ul || max_rb > CM_RB_MASK_BITS ||
        total_max_rb > CM_RB_MASK_BITS ||
        tiling_pipe_log2 > CM_TILING_PIPE_LOG2_MAX) {
        return 0ul;
    }

    /* Backends past the chip's count count as disabled, unless that would
     * leave none. */
    widened = disabled_rb_mask |
              ((CM_RB_MASK_ALL << max_rb) & CM_RB_MASK_ALL);
    if ((widened & CM_RB_MASK_ALL) != CM_RB_MASK_ALL) {
        disabled_rb_mask = widened;
    }

    pipes = 1ul << tiling_pipe_log2;
    disabled_count = v9x_cm_count_bits(disabled_rb_mask);
    if (disabled_count >= total_max_rb) {
        return 0ul;
    }
    required = total_max_rb - disabled_count;
    if (pipes < required) {
        return 0ul;
    }
    ratio = pipes / required;
    remain = pipes - ratio * required;

    /* From the highest backend down, each enabled one takes ratio pipes,
     * and the first few one more while the remainder lasts. */
    mask = 1ul << (max_rb - 1ul);
    for (index = 0ul; index < max_rb; ++index) {
        if ((mask & disabled_rb_mask) == 0ul) {
            for (repeat = 0ul; repeat < ratio; ++repeat) {
                data = (data << CM_RB_FIELD_BITS) | (max_rb - index - 1ul);
            }
            if (remain != 0ul) {
                data = (data << CM_RB_FIELD_BITS) | (max_rb - index - 1ul);
                --remain;
            }
        }
        mask >>= 1;
    }
    return data;
}

const struct v9x_eg_reg_update
    v9x_cm_trinity_gpu_init[V9X_CM_TRINITY_GPU_INIT_COUNT] = {
    { CM_GRBM_CNTL, 0ul, CM_GRBM_CNTL_VALUE },
    { CM_GB_ADDR_CONFIG, 0ul, V9X_CM_ARUBA_GB_ADDR_CONFIG },
    /* v9x_cm_backend_map(1, 2, 8, 0): 9901 has two backends per SE
     * (ni.c:921), so Linux takes the remap branch, not the IGP one-backend
     * one; the fuses read none disabled. */
    { CM_GB_BACKEND_MAP, 0ul, V9X_CM_TRINITY_BACKEND_MAP },
    { CM_CGTS_TCC_DISABLE, 0ul, CM_CGTS_TCC_VALUE },
    { CM_CGTS_SYS_TCC_DISABLE, 0ul, CM_CGTS_TCC_VALUE },
    { CM_CGTS_USER_SYS_TCC, 0ul, CM_CGTS_TCC_VALUE },
    { CM_CGTS_USER_TCC, 0ul, CM_CGTS_TCC_VALUE },
    { CM_CP_MEQ_THRESHOLDS, 0ul, CM_CP_MEQ_VALUE },
    { CM_SX_DEBUG_1, 0xfffffffful, CM_ENABLE_NEW_SMX },
    { CM_SMX_DC_CTL0, CM_SMX_SETS_KEEP, CM_SMX_SETS_TRINITY },
    { CM_SPI_CONFIG_CNTL_1, 0ul, CM_SPI_CNTL_1_VALUE },
    /* "need to be explicitly zero-ed" (ni.c). */
    { CM_VGT_OFFCHIP_LDS_BASE, 0ul, 0ul },
    { CM_SQ_LSTMP_RING_BASE, 0ul, 0ul },
    { CM_SQ_HSTMP_RING_BASE, 0ul, 0ul },
    { CM_SQ_ESTMP_RING_BASE, 0ul, 0ul },
    { CM_SQ_GSTMP_RING_BASE, 0ul, 0ul },
    { CM_SQ_VSTMP_RING_BASE, 0ul, 0ul },
    { CM_SQ_PSTMP_RING_BASE, 0ul, 0ul },
    { CM_TA_CNTL_AUX, 0ul, CM_DISABLE_CUBE_ANISO },
    { CM_SX_EXPORT_SIZES, 0ul, CM_SX_EXPORT_VALUE },
    { CM_PA_SC_FIFO_SIZE, 0ul, CM_PA_SC_FIFO_VALUE },
    { CM_VGT_NUM_INSTANCES, 0ul, 1ul },
    { CM_CP_PERFMON_CNTL, 0ul, 0ul },
    { CM_SQ_MS_FIFO_SIZES, 0ul, CM_SQ_MS_FIFO_VALUE },
    { CM_SQ_GPR_MGMT_1, 0ul, CM_SQ_GPR_1_VALUE },
    { CM_SQ_CONFIG, 0ul, CM_SQ_CONFIG_VALUE },
    { CM_SQ_DYN_GPR_FLUSH, 0ul, CM_DYN_GPR_ENABLE },
    { CM_PA_SC_FORCE_EOV, 0ul, CM_PA_SC_EOV_VALUE },
    { CM_VGT_CACHE_INVALIDATE, 0ul, CM_VGT_CACHE_VALUE },
    { CM_VGT_GS_VERTEX_REUSE, 0ul, 16ul },
    { CM_PA_SC_LINE_STIPPLE, 0ul, 0ul },
    { CM_CB_PERF_CTR0_SEL_0 + 0x00ul, 0ul, 0ul },
    { CM_CB_PERF_CTR0_SEL_0 + 0x04ul, 0ul, 0ul },
    { CM_CB_PERF_CTR0_SEL_0 + 0x08ul, 0ul, 0ul },
    { CM_CB_PERF_CTR0_SEL_0 + 0x0cul, 0ul, 0ul },
    { CM_CB_PERF_CTR0_SEL_0 + 0x10ul, 0ul, 0ul },
    { CM_CB_PERF_CTR0_SEL_0 + 0x14ul, 0ul, 0ul },
    { CM_CB_PERF_CTR0_SEL_0 + 0x18ul, 0ul, 0ul },
    { CM_CB_PERF_CTR0_SEL_0 + 0x1cul, 0ul, 0ul },
    { CM_HDP_MISC_CNTL, 0xfffffffful, CM_HDP_FLUSH_INVALIDATE },
    { CM_PA_CL_ENHANCE, 0ul, CM_PA_CL_ENHANCE_VALUE }
};
