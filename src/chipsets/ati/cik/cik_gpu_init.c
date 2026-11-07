/*
 * The Mullins engine configuration list; see include\velocity9x\cik_gpu_init.h.
 *
 * Linux's gfx_v7_0_constants_init for CHIP_MULLINS, after
 * cik_init_golden_registers. Offsets are gfx_7_2_d.h, oss_2_0_d.h and
 * gmc_7_1_d.h dword indices times four; field positions gfx_7_2_sh_mask.h;
 * the chip's numbers gfx_v7_0_gpu_early_init's KABINI/MULLINS case. Left
 * out, as Carrizo's list leaves them out: DMIF_ADDR_CALC (display),
 * setup_rb's reads (the raster configuration, 0 for Mullins, goes in the
 * CP's preamble), and the compute and GDS VMID setup.
 */
#include "velocity9x/cik_gpu_init.h"

#define CIK_WRITE                   0xfffffffful

#define CIK_GRBM_CNTL               0x08000ul
#define CIK_GB_ADDR_CONFIG          0x098f8ul
#define CIK_HDP_ADDR_CONFIG         0x02f48ul
#define CIK_GB_TILE_MODE0           0x09910ul
#define CIK_GB_MACROTILE_MODE0      0x09990ul
#define CIK_CP_MEQ_THRESHOLDS       0x08764ul
#define CIK_GRBM_GFX_INDEX          0x30800ul
#define CIK_SH_STATIC_MEM_CONFIG    0x09604ul
#define CIK_SRBM_GFX_CNTL           0x00e44ul
#define CIK_SH_MEM_BASES            0x08c28ul
#define CIK_SH_MEM_APE1_BASE        0x08c2cul
#define CIK_SH_MEM_APE1_LIMIT       0x08c30ul
#define CIK_SH_MEM_CONFIG           0x08c34ul
#define CIK_SX_DEBUG_1              0x09060ul
#define CIK_TA_CNTL_AUX             0x09508ul
#define CIK_SPI_CONFIG_CNTL         0x09100ul
#define CIK_SQ_CONFIG               0x08c00ul
#define CIK_DB_DEBUG                0x09830ul
#define CIK_DB_DEBUG2               0x09834ul
#define CIK_DB_DEBUG3               0x09838ul
#define CIK_CB_HW_CONTROL           0x09a10ul
#define CIK_SPI_CONFIG_CNTL_1       0x0913cul
#define CIK_PA_SC_FIFO_SIZE         0x08bccul
#define CIK_VGT_NUM_INSTANCES       0x30934ul
#define CIK_CP_PERFMON_CNTL         0x36020ul
#define CIK_PA_SC_FORCE_EOV_MAX     0x08b24ul
#define CIK_VGT_CACHE_INVALIDATION  0x088c4ul
#define CIK_VGT_GS_VERTEX_REUSE     0x088d4ul
#define CIK_PA_SC_LINE_STIPPLE      0x30a04ul
#define CIK_PA_CL_ENHANCE           0x08a14ul
#define CIK_PA_SC_ENHANCE           0x08bf0ul
#define CIK_SPI_ARB_PRIORITY        0x0c700ul

/* GRBM_CNTL READ_TIMEOUT 7:0. */
#define CIK_GRBM_READ_TIMEOUT       0x000000fful
/* BONAIRE_GB_ADDR_CONFIG_GOLDEN 12010001h with ROW_SIZE (29:28) for a
 * 1 KiB row, gpu_early_init's APU answer unless a DIMM's bank mapping is
 * 11; MC_FUS_DRAMn_BANK_ADDR_MAPPING was not read on M-JJN1ZZ. The tile
 * table is generated for the same row. */
#define CIK_GB_ADDR_CONFIG_MULLINS  0x02010001ul
/* CP_MEQ_THRESHOLDS: MEQ1_START 30h (7:0), MEQ2_START 60h (15:8). */
#define CIK_MEQ_THRESHOLDS          (0x30ul | (0x60ul << 8))
#define CIK_GRBM_BROADCAST          0xe0000000ul
/* SH_STATIC_MEM_CONFIG: SWIZZLE_ENABLE 1 (bit 0), ELEMENT_SIZE 1 (2:1),
 * INDEX_STRIDE 3 (4:3). */
#define CIK_SH_STATIC_MEM           (1ul | (1ul << 1) | (3ul << 3))
#define CIK_VMIDS                   16ul
#define CIK_SRBM_VMID_SHIFT         4
/* SH_MEM_CONFIG: ALIGNMENT_MODE unaligned (3) at 3:2, DEFAULT_MTYPE NC
 * (1) at 6:4, APE1_MTYPE UC (3) at 9:7, PRIVATE_ATC 0. The same for every
 * VMID, unlike Carrizo's. */
#define CIK_SH_MEM_CONFIG_ALL       ((3ul << 2) | (1ul << 4) | (3ul << 7))
#define CIK_SH_MEM_APE1_BASE_VALUE  1ul
/* SPI_CONFIG_CNTL: read, then set bits 25:24. */
#define CIK_SPI_CONFIG_SET          0x03000000ul
/* DB_DEBUG2 and DB_DEBUG3, CB_HW_CONTROL: gfx_v7_0's read-modify-writes. */
#define CIK_DB_DEBUG2_CLEAR         0xf00ffffful
#define CIK_DB_DEBUG2_SET           0x00000400ul
#define CIK_DB_DEBUG3_CLEAR         0x0002021cul
#define CIK_DB_DEBUG3_SET           0x00020200ul
#define CIK_CB_HW_CONTROL_CLEAR     0x00010000ul
#define CIK_CB_HW_CONTROL_SET       0x00018208ul
/* SPI_CONFIG_CNTL_1: VTX_DONE_DELAY 4 at 3:0. */
#define CIK_SPI_CONFIG_CNTL_1_VALUE 4ul
/* PA_SC_FIFO_SIZE: frontend 20h (5:0), backend 100h (14:6), HiZ tile
 * 30h (20:15), early-Z tile 130h (31:23), as Carrizo's. */
#define CIK_PA_SC_FIFO_SIZE_VALUE   ((0x20ul << 0) | (0x100ul << 6) | \
                                     (0x30ul << 15) | (0x130ul << 23))
/* PA_SC_FORCE_EOV_MAX_CNTS: CLK_CNT 4095 (15:0), REZ_CNT 255 (31:16). */
#define CIK_FORCE_EOV_MAX           (4095ul | (255ul << 16))
/* VGT_CACHE_INVALIDATION: VC_AND_TC (2) at 1:0, ES_AND_GS_AUTO (3,
 * cikd.h) at 7:6. */
#define CIK_VGT_CACHE_INVAL_VALUE   (2ul | (3ul << 6))
#define CIK_VGT_GS_VERTEX_REUSE_VAL 16ul
/* PA_CL_ENHANCE: CLIP_VTX_REORDER_ENA (bit 0), NUM_CLIP_SEQ 3 (2:1). */
#define CIK_PA_CL_ENHANCE_VALUE     (1ul | (3ul << 1))
/* PA_SC_ENHANCE: ENABLE_PA_SC_OUT_OF_ORDER (bit 0). */
#define CIK_PA_SC_ENHANCE_VALUE     1ul
/* SPI_ARB_PRIORITY: PIPE_ORDER_TS0-3 (2:0, 5:3, 8:6, 11:9) each 2. */
#define CIK_SPI_ARB_CLEAR           0x00000ffful
#define CIK_SPI_ARB_SET             (2ul | (2ul << 3) | (2ul << 6) | (2ul << 9))

struct cik_ops_out {
    struct v9x_vi_reg_seq *ops;
    v9x_u32 count;
    v9x_u32 capacity;
};

static void cik_op(struct cik_ops_out *out, v9x_u32 offset, v9x_u32 clear,
                   v9x_u32 set)
{
    if (out->count < out->capacity) {
        out->ops[out->count].offset = offset;
        out->ops[out->count].clear = clear;
        out->ops[out->count].set = set;
    }
    ++out->count;
}

static void cik_sequence(struct cik_ops_out *out,
                         const struct v9x_vi_reg_seq *table, v9x_u32 count)
{
    v9x_u32 index;

    for (index = 0ul; index < count; ++index) {
        cik_op(out, table[index].offset, table[index].clear,
               table[index].set);
    }
}

v9x_u32 v9x_cik_gpu_init_ops(struct v9x_vi_reg_seq *ops, v9x_u32 capacity)
{
    struct cik_ops_out out;
    v9x_u32 index;
    v9x_u32 vmid;

    if (ops == 0) {
        return 0ul;
    }
    out.ops = ops;
    out.count = 0ul;
    out.capacity = capacity;

    /* cik_init_golden_registers, CHIP_MULLINS. */
    cik_sequence(&out, v9x_cik_kalindi_mgcg_cgcg_init,
                 V9X_CIK_KALINDI_MGCG_CGCG_COUNT);
    cik_sequence(&out, v9x_cik_godavari_golden,
                 V9X_CIK_GODAVARI_GOLDEN_COUNT);
    cik_sequence(&out, v9x_cik_kalindi_golden_common,
                 V9X_CIK_KALINDI_GOLDEN_COMMON_COUNT);
    cik_sequence(&out, v9x_cik_kalindi_golden_spm,
                 V9X_CIK_KALINDI_GOLDEN_SPM_COUNT);

    /* gfx_v7_0_constants_init. */
    cik_op(&out, CIK_GRBM_CNTL, CIK_WRITE, CIK_GRBM_READ_TIMEOUT);
    cik_op(&out, CIK_GB_ADDR_CONFIG, CIK_WRITE, CIK_GB_ADDR_CONFIG_MULLINS);
    cik_op(&out, CIK_HDP_ADDR_CONFIG, CIK_WRITE, CIK_GB_ADDR_CONFIG_MULLINS);
    for (index = 0ul; index < V9X_CIK_TILE_MODES; ++index) {
        cik_op(&out, CIK_GB_TILE_MODE0 + index * 4ul, CIK_WRITE,
               v9x_cik_mullins_tile_modes[index]);
    }
    /* tiling_mode_table_init leaves macrotile 7 as it was. */
    for (index = 0ul; index < V9X_CIK_MACROTILE_MODES; ++index) {
        if (index != 7ul) {
            cik_op(&out, CIK_GB_MACROTILE_MODE0 + index * 4ul, CIK_WRITE,
                   v9x_cik_mullins_macrotile_modes[index]);
        }
    }
    cik_op(&out, CIK_CP_MEQ_THRESHOLDS, CIK_WRITE, CIK_MEQ_THRESHOLDS);
    cik_op(&out, CIK_GRBM_GFX_INDEX, CIK_WRITE, CIK_GRBM_BROADCAST);
    cik_op(&out, CIK_SH_STATIC_MEM_CONFIG, CIK_WRITE, CIK_SH_STATIC_MEM);
    for (vmid = 0ul; vmid < CIK_VMIDS; ++vmid) {
        cik_op(&out, CIK_SRBM_GFX_CNTL, CIK_WRITE,
               vmid << CIK_SRBM_VMID_SHIFT);
        cik_op(&out, CIK_SH_MEM_CONFIG, CIK_WRITE, CIK_SH_MEM_CONFIG_ALL);
        cik_op(&out, CIK_SH_MEM_APE1_BASE, CIK_WRITE,
               CIK_SH_MEM_APE1_BASE_VALUE);
        cik_op(&out, CIK_SH_MEM_APE1_LIMIT, CIK_WRITE, 0ul);
        /* VMID 0's base is 0; the others take the shared aperture's top
         * bits, which nothing here uses: 0 for all. */
        cik_op(&out, CIK_SH_MEM_BASES, CIK_WRITE, 0ul);
    }
    cik_op(&out, CIK_SRBM_GFX_CNTL, CIK_WRITE, 0ul);
    cik_op(&out, CIK_SX_DEBUG_1, CIK_WRITE, 0x00000020ul);
    cik_op(&out, CIK_TA_CNTL_AUX, CIK_WRITE, 0x00010000ul);
    cik_op(&out, CIK_SPI_CONFIG_CNTL, 0ul, CIK_SPI_CONFIG_SET);
    cik_op(&out, CIK_SQ_CONFIG, CIK_WRITE, 1ul);
    cik_op(&out, CIK_DB_DEBUG, CIK_WRITE, 0ul);
    cik_op(&out, CIK_DB_DEBUG2, CIK_DB_DEBUG2_CLEAR, CIK_DB_DEBUG2_SET);
    cik_op(&out, CIK_DB_DEBUG3, CIK_DB_DEBUG3_CLEAR, CIK_DB_DEBUG3_SET);
    cik_op(&out, CIK_CB_HW_CONTROL, CIK_CB_HW_CONTROL_CLEAR,
           CIK_CB_HW_CONTROL_SET);
    cik_op(&out, CIK_SPI_CONFIG_CNTL_1, CIK_WRITE,
           CIK_SPI_CONFIG_CNTL_1_VALUE);
    cik_op(&out, CIK_PA_SC_FIFO_SIZE, CIK_WRITE, CIK_PA_SC_FIFO_SIZE_VALUE);
    cik_op(&out, CIK_VGT_NUM_INSTANCES, CIK_WRITE, 1ul);
    cik_op(&out, CIK_CP_PERFMON_CNTL, CIK_WRITE, 0ul);
    cik_op(&out, CIK_SQ_CONFIG, CIK_WRITE, 0ul);
    cik_op(&out, CIK_PA_SC_FORCE_EOV_MAX, CIK_WRITE, CIK_FORCE_EOV_MAX);
    cik_op(&out, CIK_VGT_CACHE_INVALIDATION, CIK_WRITE,
           CIK_VGT_CACHE_INVAL_VALUE);
    cik_op(&out, CIK_VGT_GS_VERTEX_REUSE, CIK_WRITE,
           CIK_VGT_GS_VERTEX_REUSE_VAL);
    cik_op(&out, CIK_PA_SC_LINE_STIPPLE, CIK_WRITE, 0ul);
    cik_op(&out, CIK_PA_CL_ENHANCE, CIK_WRITE, CIK_PA_CL_ENHANCE_VALUE);
    cik_op(&out, CIK_PA_SC_ENHANCE, CIK_WRITE, CIK_PA_SC_ENHANCE_VALUE);
    cik_op(&out, CIK_SPI_ARB_PRIORITY, CIK_SPI_ARB_CLEAR, CIK_SPI_ARB_SET);

    return out.count <= capacity ? out.count : 0ul;
}
