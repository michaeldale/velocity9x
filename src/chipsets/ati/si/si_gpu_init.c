/*
 * The Oland engine configuration list; see include\velocity9x\si_gpu_init.h.
 *
 * Linux's gfx_v6_0_constants_init for CHIP_OLAND, after
 * si_init_golden_registers. Offsets are gfx_6_0_d.h, oss_1_0_d.h and
 * gmc_6_0_d.h dword indices times four; field positions gfx_6_0_sh_mask.h;
 * the chip's numbers constants_init's CHIP_OLAND case (one shader engine,
 * one SH, two RBs, four tile pipes).
 *
 * Left out, as Mullins' list leaves its equivalents out: SRBM_INT_CNTL and
 * _ACK (interrupts), BIF_FB_EN (the BIOS leaves 3, read and write on),
 * DMIF_ADDR_CONFIG and DMIF_ADDR_CALC (display), and the reads Linux writes
 * straight back (SX_DEBUG_1, HDP_HOST_PATH_CNTL). setup_tcc writes only
 * when a TCC is fused off; W9QI0DAD's CGTS_TCC_DISABLE is read by the
 * probe that runs this list, not assumed here.
 */
#include "velocity9x/si_gpu_init.h"

#define SI_WRITE                    0xfffffffful

#define SI_GRBM_CNTL                0x08000ul
#define SI_GRBM_GFX_INDEX           0x0802cul
#define SI_GB_ADDR_CONFIG           0x098f8ul
#define SI_HDP_ADDR_CONFIG          0x02f48ul
#define SI_DMA0_TILING_CONFIG       0x0d0b8ul
#define SI_DMA1_TILING_CONFIG       0x0d8b8ul
#define SI_GB_TILE_MODE0            0x09910ul
#define SI_PA_SC_RASTER_CONFIG      0x28350ul
#define SI_SPI_STATIC_THREAD_MGMT_3 0x090e8ul
#define SI_CP_QUEUE_THRESHOLDS      0x08760ul
#define SI_CP_MEQ_THRESHOLDS        0x08764ul
#define SI_SPI_CONFIG_CNTL_1        0x0913cul
#define SI_PA_SC_FIFO_SIZE          0x08bccul
#define SI_VGT_NUM_INSTANCES        0x08974ul
#define SI_CP_PERFMON_CNTL          0x087fcul
#define SI_SQ_CONFIG                0x08c00ul
#define SI_PA_SC_FORCE_EOV_MAX      0x08b24ul
#define SI_VGT_CACHE_INVALIDATION   0x088c4ul
#define SI_VGT_GS_VERTEX_REUSE      0x088d4ul
#define SI_PA_SC_LINE_STIPPLE_STATE 0x08b10ul
#define SI_CB_PERFCOUNTER0_SELECT0  0x09a20ul
#define SI_CB_PERFCOUNTER_SELECTS   8ul
#define SI_PA_CL_ENHANCE            0x08a14ul

/* GRBM_CNTL READ_TIMEOUT 7:0. */
#define SI_GRBM_READ_TIMEOUT        0x000000fful
/* VERDE_GB_ADDR_CONFIG_GOLDEN 12010002h, which constants_init uses for
 * Oland, with ROW_SIZE (29:28) 2 for a 4 KiB row: W9QI0DAD's
 * MC_ARB_RAMCFG 000012A1h has NOOFCOLS (7:6) 2, so 4 * 2^(8+2) bytes.
 * NUM_SHADER_ENGINES (13:12) stays 0, one engine. The tile table is
 * generated for the same row. */
#define SI_GB_ADDR_CONFIG_OLAND     0x22010002ul
/* GRBM_GFX_INDEX: SE_BROADCAST (31), SH_BROADCAST (29) and
 * INSTANCE_BROADCAST (30) for all; SE 0, SH 0 with instance broadcast for
 * setup_spi's per-SH write. */
#define SI_GRBM_BROADCAST           0xe0000000ul
#define SI_GRBM_SE0_SH0             0x40000000ul
/* gfx_v6_0_raster_config, CHIP_OLAND: RB_YSEL (bit 7) 1. Both RBs are
 * enabled (CC_RB_BACKEND_DISABLE 0 on W9QI0DAD), which is num_rb_pipes,
 * so setup_rb writes it once, broadcast. */
#define SI_RASTER_CONFIG_OLAND      0x00000080ul
/* setup_spi: the lowest active CU's bit cleared in
 * SPI_STATIC_THREAD_MGMT_3. All six CUs are active, so bit 0. */
#define SI_SPI_FIRST_CU             0x00000001ul
/* CP_QUEUE_THRESHOLDS: ROQ_IB1_START 16h (7:0), ROQ_IB2_START 2Bh (15:8). */
#define SI_QUEUE_THRESHOLDS         (0x16ul | (0x2bul << 8))
/* CP_MEQ_THRESHOLDS: MEQ1_START 30h (7:0), MEQ2_START 60h (15:8). */
#define SI_MEQ_THRESHOLDS           (0x30ul | (0x60ul << 8))
/* SPI_CONFIG_CNTL_1: VTX_DONE_DELAY 4 at 3:0. */
#define SI_SPI_CONFIG_CNTL_1_VALUE  4ul
/* PA_SC_FIFO_SIZE: frontend 20h (5:0), backend 40h (14:6), HiZ tile 30h
 * (20:15), early-Z tile 130h (31:23): Oland's numbers. */
#define SI_PA_SC_FIFO_SIZE_VALUE    ((0x20ul << 0) | (0x40ul << 6) | \
                                     (0x30ul << 15) | (0x130ul << 23))
/* PA_SC_FORCE_EOV_MAX_CNTS: CLK_CNT 4095 (15:0), REZ_CNT 255 (31:16). */
#define SI_FORCE_EOV_MAX            (4095ul | (255ul << 16))
/* VGT_CACHE_INVALIDATION: VC_AND_TC (2) at 1:0, ES_AND_GS_AUTO (3) at
 * 7:6. */
#define SI_VGT_CACHE_INVAL_VALUE    (2ul | (3ul << 6))
#define SI_VGT_GS_VERTEX_REUSE_VAL  16ul
/* PA_CL_ENHANCE: CLIP_VTX_REORDER_ENA (bit 0), NUM_CLIP_SEQ 3 (2:1). */
#define SI_PA_CL_ENHANCE_VALUE      (1ul | (3ul << 1))

struct si_ops_out {
    struct v9x_vi_reg_seq *ops;
    v9x_u32 count;
    v9x_u32 capacity;
};

static void si_op(struct si_ops_out *out, v9x_u32 offset, v9x_u32 clear,
                  v9x_u32 set)
{
    if (out->count < out->capacity) {
        out->ops[out->count].offset = offset;
        out->ops[out->count].clear = clear;
        out->ops[out->count].set = set;
    }
    ++out->count;
}

static void si_sequence(struct si_ops_out *out,
                        const struct v9x_vi_reg_seq *table, v9x_u32 count)
{
    v9x_u32 index;

    for (index = 0ul; index < count; ++index) {
        si_op(out, table[index].offset, table[index].clear,
              table[index].set);
    }
}

v9x_u32 v9x_si_gpu_init_ops(struct v9x_vi_reg_seq *ops, v9x_u32 capacity)
{
    struct si_ops_out out;
    v9x_u32 index;

    if (ops == 0) {
        return 0ul;
    }
    out.ops = ops;
    out.count = 0ul;
    out.capacity = capacity;

    /* si_init_golden_registers, CHIP_OLAND. */
    si_sequence(&out, v9x_si_oland_golden, V9X_SI_OLAND_GOLDEN_COUNT);
    si_sequence(&out, v9x_si_oland_golden_rlc,
                V9X_SI_OLAND_GOLDEN_RLC_COUNT);
    si_sequence(&out, v9x_si_oland_mgcg_cgcg_init,
                V9X_SI_OLAND_MGCG_CGCG_COUNT);

    /* gfx_v6_0_constants_init. */
    si_op(&out, SI_GRBM_CNTL, SI_WRITE, SI_GRBM_READ_TIMEOUT);
    si_op(&out, SI_GB_ADDR_CONFIG, SI_WRITE, SI_GB_ADDR_CONFIG_OLAND);
    si_op(&out, SI_HDP_ADDR_CONFIG, SI_WRITE, SI_GB_ADDR_CONFIG_OLAND);
    si_op(&out, SI_DMA0_TILING_CONFIG, SI_WRITE, SI_GB_ADDR_CONFIG_OLAND);
    si_op(&out, SI_DMA1_TILING_CONFIG, SI_WRITE, SI_GB_ADDR_CONFIG_OLAND);
    for (index = 0ul; index < V9X_SI_TILE_MODES; ++index) {
        si_op(&out, SI_GB_TILE_MODE0 + index * 4ul, SI_WRITE,
              v9x_si_oland_tile_modes[index]);
    }

    /* setup_rb: all RBs on, so one broadcast write. */
    si_op(&out, SI_GRBM_GFX_INDEX, SI_WRITE, SI_GRBM_BROADCAST);
    si_op(&out, SI_PA_SC_RASTER_CONFIG, SI_WRITE, SI_RASTER_CONFIG_OLAND);

    /* setup_spi, SE 0 SH 0. */
    si_op(&out, SI_GRBM_GFX_INDEX, SI_WRITE, SI_GRBM_SE0_SH0);
    si_op(&out, SI_SPI_STATIC_THREAD_MGMT_3, SI_SPI_FIRST_CU, 0ul);
    si_op(&out, SI_GRBM_GFX_INDEX, SI_WRITE, SI_GRBM_BROADCAST);

    si_op(&out, SI_CP_QUEUE_THRESHOLDS, SI_WRITE, SI_QUEUE_THRESHOLDS);
    si_op(&out, SI_CP_MEQ_THRESHOLDS, SI_WRITE, SI_MEQ_THRESHOLDS);
    si_op(&out, SI_SPI_CONFIG_CNTL_1, SI_WRITE, SI_SPI_CONFIG_CNTL_1_VALUE);
    si_op(&out, SI_PA_SC_FIFO_SIZE, SI_WRITE, SI_PA_SC_FIFO_SIZE_VALUE);
    si_op(&out, SI_VGT_NUM_INSTANCES, SI_WRITE, 1ul);
    si_op(&out, SI_CP_PERFMON_CNTL, SI_WRITE, 0ul);
    si_op(&out, SI_SQ_CONFIG, SI_WRITE, 0ul);
    si_op(&out, SI_PA_SC_FORCE_EOV_MAX, SI_WRITE, SI_FORCE_EOV_MAX);
    si_op(&out, SI_VGT_CACHE_INVALIDATION, SI_WRITE,
          SI_VGT_CACHE_INVAL_VALUE);
    si_op(&out, SI_VGT_GS_VERTEX_REUSE, SI_WRITE, SI_VGT_GS_VERTEX_REUSE_VAL);
    si_op(&out, SI_PA_SC_LINE_STIPPLE_STATE, SI_WRITE, 0ul);
    for (index = 0ul; index < SI_CB_PERFCOUNTER_SELECTS; ++index) {
        si_op(&out, SI_CB_PERFCOUNTER0_SELECT0 + index * 4ul, SI_WRITE, 0ul);
    }
    si_op(&out, SI_PA_CL_ENHANCE, SI_WRITE, SI_PA_CL_ENHANCE_VALUE);

    return out.count <= capacity ? out.count : 0ul;
}
