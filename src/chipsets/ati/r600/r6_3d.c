/*
 * ATI R6xx (RV630) 3D command streams. See include\velocity9x\r6_3d.h for
 * the contract and the sources.
 */
#include "velocity9x/r6_3d.h"
#include "velocity9x/r6_shader.h"
#include "velocity9x/eg_shader.h"

/*
 * r600_gpu_init's numbers for CHIP_RV630 (r600.c: 2 pipes, 2 tile pipes,
 * 3 SIMDs, 1 backend, 128 GPRs, 192 threads, 128 stack entries, 8
 * contexts), through r600d.h's field macros.
 */
#define R6_GRBM_CNTL_VALUE      0x000000fful /* GRBM_READ_TIMEOUT(0xff) */
#define R6_CP_QUEUE_VALUE       0x00002b16ul /* ROQ_IB1_START(16h) | ROQ_IB2_START(2Bh) */
#define R6_CP_MEQ_VALUE         0x40400000ul /* MEQ_END(40h) | ROQ_END(40h) */
/* DISABLE_CUBE_ANISO | SYNC_GRADIENT | SYNC_WALKER | SYNC_ALIGNER */
#define R6_TA_CNTL_AUX_INIT     0x07000002ul
/* SMX_EVENT_RELEASE, and ENABLE_NEW_SMX_ADDRESS for every family after
 * R600. */
#define R6_SX_DEBUG_1_SET       0x00010001ul
/* PREZ_MUST_WAIT_FOR_POSTZ_DONE: R600, RV630, RV610, RV620, RS780/880. */
#define R6_DB_DEBUG_INIT        0x80000000ul
/* DEPTH_FREE(4) | DEPTH_FLUSH(16) | DEPTH_PENDING_FREE(4) |
 * DEPTH_CACHELINE_FREE(16); xf86's default state writes the same. */
#define R6_DB_WATERMARKS_VALUE  0x01020204ul
/* R600 and RV630: DONE_FIFO_HIWATER cleared, then 4. */
#define R6_SQ_MS_FIFO_KEEP      0xff00fffful
#define R6_SQ_MS_FIFO_SET       0x00040000ul
/* SQ_CONFIG: the four priorities cleared; DX9_CONSTS | VC_ENABLE |
 * PS_PRIO(0) | VS_PRIO(1) | GS_PRIO(2) | ES_PRIO(3) set. */
#define R6_SQ_CONFIG_KEEP       0x00fffffful
#define R6_SQ_CONFIG_INIT       0xe4000005ul
/* NUM_PS_GPRS(44) | NUM_VS_GPRS(44) | NUM_CLAUSE_TEMP_GPRS(2) */
#define R6_SQ_GPR_1_INIT        0x202c002cul
/* NUM_GS_GPRS(18) | NUM_ES_GPRS(18) */
#define R6_SQ_GPR_2_INIT        0x00120012ul
/* NUM_PS_THREADS(79) | NUM_VS_THREADS(78) | NUM_GS_THREADS(4) |
 * NUM_ES_THREADS(31) */
#define R6_SQ_THREAD_INIT       0x1f044e4ful
/* 40 PS and VS stack entries; 32 GS, 16 ES. */
#define R6_SQ_STACK_1_VALUE     0x00280028ul
#define R6_SQ_STACK_2_VALUE     0x00100020ul
#define R6_VGT_CACHE_VC_AND_TC  0x00000002ul /* CACHE_INVALIDATION(VC_AND_TC) */
/* The multisample positions, S0_X ... S7_Y nibbles as r600_gpu_init
 * lists them. */
#define R6_AA_LOCS_2S           0x0000c44cul
#define R6_AA_LOCS_4S           0xa66a22eeul
#define R6_AA_LOCS_8S_WD0       0xea61c4beul
#define R6_AA_LOCS_8S_WD1       0x874b0016ul
/* VGT_GS_PER_ES: max_pipes * 16 = 32 (no RV630 addend); VGT_ES_PER_GS
 * 128, VGT_GS_PER_VS 2, VGT_GS_VERTEX_REUSE 16. */
#define R6_VGT_GS_PER_ES_VALUE  32ul
#define R6_VGT_ES_PER_GS_VALUE  128ul
#define R6_VGT_GS_PER_VS_VALUE  2ul
#define R6_VGT_GS_REUSE_VALUE   16ul
#define R6_TC_CNTL_RV630        0x00000080ul /* TC_L2_SIZE(4) */
/* CLIP_VTX_REORDER_ENA | NUM_CLIP_SEQ(3) */
#define R6_PA_CL_ENHANCE_VALUE  0x00000007ul
#define R6_PA_SC_ENHANCE_VALUE  0x00000ffful /* FORCE_EOV_MAX_CLK_CNT(4095) */

const struct v9x_r6_reg_update
    v9x_r6_rv630_gpu_init[V9X_R6_RV630_GPU_INIT_COUNT] = {
    { V9X_R6_GRBM_CNTL, 0ul, R6_GRBM_CNTL_VALUE },
    /* Placeholder: v9x_r6_3d_gpu_init writes the RAMCFG-derived value. */
    { V9X_R6_GB_TILING_CONFIG, 0ul, 0ul },
    { V9X_R6_CP_QUEUE_THRESHOLDS, 0ul, R6_CP_QUEUE_VALUE },
    { V9X_R6_CP_MEQ_THRESHOLDS, 0ul, R6_CP_MEQ_VALUE },
    { V9X_R6_TA_CNTL_AUX, 0ul, R6_TA_CNTL_AUX_INIT },
    { V9X_R6_SX_DEBUG_1, 0xfffffffful, R6_SX_DEBUG_1_SET },
    { V9X_R6_DB_DEBUG, 0ul, R6_DB_DEBUG_INIT },
    { V9X_R6_DB_WATERMARKS, 0ul, R6_DB_WATERMARKS_VALUE },
    { V9X_R6_PA_SC_MULTI_CHIP_CNTL, 0ul, 0ul },
    { V9X_R6_VGT_NUM_INSTANCES, 0ul, 0ul },
    { V9X_R6_SPI_CONFIG_CNTL, 0ul, 0ul },
    { V9X_R6_SPI_CONFIG_CNTL_1, 0ul, 0ul },
    { V9X_R6_SQ_MS_FIFO_SIZES, R6_SQ_MS_FIFO_KEEP, R6_SQ_MS_FIFO_SET },
    { V9X_R6_SQ_CONFIG, R6_SQ_CONFIG_KEEP, R6_SQ_CONFIG_INIT },
    { V9X_R6_SQ_GPR_RESOURCE_MGMT_1, 0ul, R6_SQ_GPR_1_INIT },
    { V9X_R6_SQ_GPR_RESOURCE_MGMT_2, 0ul, R6_SQ_GPR_2_INIT },
    { V9X_R6_SQ_THREAD_RESOURCE_MGMT, 0ul, R6_SQ_THREAD_INIT },
    { V9X_R6_SQ_STACK_RESOURCE_MGMT_1, 0ul, R6_SQ_STACK_1_VALUE },
    { V9X_R6_SQ_STACK_RESOURCE_MGMT_2, 0ul, R6_SQ_STACK_2_VALUE },
    { V9X_R6_VGT_CACHE_INVALIDATION, 0ul, R6_VGT_CACHE_VC_AND_TC },
    { V9X_R6_PA_SC_AA_SAMPLE_LOCS_2S, 0ul, R6_AA_LOCS_2S },
    { V9X_R6_PA_SC_AA_SAMPLE_LOCS_4S, 0ul, R6_AA_LOCS_4S },
    { V9X_R6_PA_SC_AA_SAMPLE_LOCS_8S_WD0, 0ul, R6_AA_LOCS_8S_WD0 },
    { V9X_R6_PA_SC_AA_SAMPLE_LOCS_8S_WD1, 0ul, R6_AA_LOCS_8S_WD1 },
    { V9X_R6_VGT_ES_PER_GS, 0ul, R6_VGT_ES_PER_GS_VALUE },
    { V9X_R6_VGT_GS_PER_ES, 0ul, R6_VGT_GS_PER_ES_VALUE },
    { V9X_R6_VGT_GS_PER_VS, 0ul, R6_VGT_GS_PER_VS_VALUE },
    { V9X_R6_VGT_GS_VERTEX_REUSE, 0ul, R6_VGT_GS_REUSE_VALUE },
    { V9X_R6_PA_SC_LINE_STIPPLE_STATE, 0ul, 0ul },
    { V9X_R6_TC_CNTL, 0ul, R6_TC_CNTL_RV630 },
    { V9X_R6_PA_CL_ENHANCE, 0ul, R6_PA_CL_ENHANCE_VALUE },
    { V9X_R6_PA_SC_ENHANCE, 0ul, R6_PA_SC_ENHANCE_VALUE },
    { V9X_R6_VC_ENHANCE, 0ul, 0ul }
};

/* RAMCFG's fields (r600d.h) and GB_TILING_CONFIG's (DCP_TILING_CONFIG's
 * field macros, which r600_gpu_init uses for both). */
#define R6_RAMCFG_BANKS_MASK    0x00000001ul /* NOOFBANK */
#define R6_RAMCFG_ROWS_MASK     0x0000001cul /* NOOFROWS */
#define R6_RAMCFG_ROWS_SHIFT    2
#define R6_RAMCFG_BURST_MASK    0x00000100ul /* BURSTLENGTH */
#define R6_RAMCFG_BURST_SHIFT   8
#define R6_ROW_TILING_MAX       3ul
#define R6_TILING_PIPE_2        0x00000002ul /* PIPE_TILING(1): 2 pipes */
#define R6_TILING_BANK_SHIFT    4
#define R6_TILING_GROUP_SHIFT   6
#define R6_TILING_ROW_SHIFT     8
#define R6_TILING_BANK_SWAPS_1  0x00000800ul /* BANK_SWAPS(1) */
#define R6_TILING_SPLIT_SHIFT   14

v9x_u32 v9x_r6_rv630_tiling_config(v9x_u32 ramcfg)
{
    v9x_u32 rows = (ramcfg & R6_RAMCFG_ROWS_MASK) >> R6_RAMCFG_ROWS_SHIFT;

    if (rows > R6_ROW_TILING_MAX) {
        rows = R6_ROW_TILING_MAX;
    }
    return R6_TILING_PIPE_2 |
           ((ramcfg & R6_RAMCFG_BANKS_MASK) << R6_TILING_BANK_SHIFT) |
           (((ramcfg & R6_RAMCFG_BURST_MASK) >> R6_RAMCFG_BURST_SHIFT) <<
            R6_TILING_GROUP_SHIFT) |
           (rows << R6_TILING_ROW_SHIFT) | R6_TILING_BANK_SWAPS_1 |
           (rows << R6_TILING_SPLIT_SHIFT);
}

v9x_u32 v9x_r6_pm4_header(v9x_u32 op, v9x_u32 body_dwords)
{
    return (3ul << 30) | ((op & 0xfful) << 8) |
           (((body_dwords - 1ul) & V9X_R6_PACKET3_BODY_MAX) << 16);
}

void v9x_r6_pm4_begin(struct v9x_r6_pm4 *stream, v9x_u32 *buffer,
                      v9x_u32 capacity)
{
    stream->dwords = buffer;
    stream->count = 0ul;
    stream->capacity = capacity;
    stream->overflow = 0ul;
}

void v9x_r6_pm4_put(struct v9x_r6_pm4 *stream, v9x_u32 value)
{
    if (stream->count >= stream->capacity) {
        stream->overflow = 1ul;
        return;
    }
    stream->dwords[stream->count++] = value;
}

/* PACK0's ranges, in r600_state.h's order. */
struct v9x_r6_set_range {
    v9x_u32 op;
    v9x_u32 start;
    v9x_u32 end;
};

static const struct v9x_r6_set_range v9x_r6_set_ranges[] = {
    { V9X_R6_P3_SET_CONFIG_REG, V9X_R6_SET_CONFIG_START,
      V9X_R6_SET_CONFIG_END },
    { V9X_R6_P3_SET_CONTEXT_REG, V9X_R6_SET_CONTEXT_START,
      V9X_R6_SET_CONTEXT_END },
    { V9X_R6_P3_SET_ALU_CONST, V9X_R6_SET_ALU_CONST_START,
      V9X_R6_SET_ALU_CONST_END },
    { V9X_R6_P3_SET_RESOURCE, V9X_R6_SET_RESOURCE_START,
      V9X_R6_SET_RESOURCE_END },
    { V9X_R6_P3_SET_SAMPLER, V9X_R6_SET_SAMPLER_START,
      V9X_R6_SET_SAMPLER_END },
    { V9X_R6_P3_SET_CTL_CONST, V9X_R6_SET_CTL_CONST_START,
      V9X_R6_SET_CTL_CONST_END },
    { V9X_R6_P3_SET_LOOP_CONST, V9X_R6_SET_LOOP_CONST_START,
      V9X_R6_SET_LOOP_CONST_END },
    { V9X_R6_P3_SET_BOOL_CONST, V9X_R6_SET_BOOL_CONST_START,
      V9X_R6_SET_BOOL_CONST_END }
};
#define R6_SET_RANGE_COUNT \
    (sizeof(v9x_r6_set_ranges) / sizeof(v9x_r6_set_ranges[0]))

void v9x_r6_pm4_set(struct v9x_r6_pm4 *stream, v9x_u32 offset,
                    const v9x_u32 *values, v9x_u32 count)
{
    const struct v9x_r6_set_range *range = 0;
    v9x_u32 index;

    if (count == 0ul || count >= V9X_R6_PACKET3_BODY_MAX ||
        (offset & 3ul) != 0ul) {
        stream->overflow = 1ul;
        return;
    }
    for (index = 0ul; index < R6_SET_RANGE_COUNT; ++index) {
        range = &v9x_r6_set_ranges[index];
        if (offset >= range->start && offset < range->end) {
            break;
        }
    }
    if (index == R6_SET_RANGE_COUNT || range == 0 ||
        count > (range->end - offset) / 4ul) {
        stream->overflow = 1ul;
        return;
    }
    v9x_r6_pm4_put(stream, v9x_r6_pm4_header(range->op, count + 1ul));
    v9x_r6_pm4_put(stream, (offset - range->start) >> 2);
    for (index = 0ul; index < count; ++index) {
        v9x_r6_pm4_put(stream, values[index]);
    }
}

void v9x_r6_pm4_set1(struct v9x_r6_pm4 *stream, v9x_u32 offset,
                     v9x_u32 value)
{
    v9x_r6_pm4_set(stream, offset, &value, 1ul);
}

#define R6_SYNC_POLL_INTERVAL   10ul
#define R6_ALIGN_256            0xfful

void v9x_r6_pm4_surface_sync(struct v9x_r6_pm4 *stream, v9x_u32 actions,
                             v9x_u32 size, v9x_u32 address)
{
    v9x_r6_pm4_put(stream, v9x_r6_pm4_header(V9X_R6_P3_SURFACE_SYNC, 4ul));
    v9x_r6_pm4_put(stream, actions);
    v9x_r6_pm4_put(stream, size == V9X_R6_SYNC_ALL ? V9X_R6_SYNC_ALL :
                           (size + R6_ALIGN_256) >> 8);
    v9x_r6_pm4_put(stream, address >> 8);
    v9x_r6_pm4_put(stream, R6_SYNC_POLL_INTERVAL);
}

void v9x_r6_pm4_pad(struct v9x_r6_pm4 *stream)
{
    while ((stream->count % V9X_R6_CP_ALIGN_DWORDS) != 0ul &&
           stream->overflow == 0ul) {
        v9x_r6_pm4_put(stream, V9X_R6_PACKET2);
    }
}

/* r600_cp_start: radeon_ring_write(0x1), 0x3 on R6xx (0x0 from RV770 on),
 * max_hw_contexts - 1, PACKET3_ME_INITIALIZE_DEVICE_ID(1), 0, 0. */
#define R6_ME_INIT_WORD2        0x3ul
#define R6_RV630_HW_CONTEXTS    8ul
#define R6_ME_INIT_DEVICE_ID    0x00010000ul

void v9x_r6_3d_me_initialize(struct v9x_r6_pm4 *stream)
{
    v9x_r6_pm4_put(stream,
                   v9x_r6_pm4_header(V9X_R6_P3_ME_INITIALIZE,
                                     V9X_R6_ME_INITIALIZE_DWORDS - 1ul));
    v9x_r6_pm4_put(stream, 0x1ul);
    v9x_r6_pm4_put(stream, R6_ME_INIT_WORD2);
    v9x_r6_pm4_put(stream, R6_RV630_HW_CONTEXTS - 1ul);
    v9x_r6_pm4_put(stream, R6_ME_INIT_DEVICE_ID);
    v9x_r6_pm4_put(stream, 0ul);
    v9x_r6_pm4_put(stream, 0ul);
}

void v9x_r6_3d_gpu_init(struct v9x_r6_pm4 *stream,
                        const v9x_u32 *read_values, v9x_u32 tiling_config)
{
    const struct v9x_r6_reg_update *update;
    v9x_u32 index;
    v9x_u32 value;

    for (index = 0ul; index < V9X_R6_RV630_GPU_INIT_COUNT; ++index) {
        update = &v9x_r6_rv630_gpu_init[index];
        /* The CP's own queue thresholds are not written through its ring:
         * Linux writes them from r600_gpu_init, which r600_startup runs
         * with the CP halted, and the BIOS leaves both at Linux's values
         * near enough (M-NZHWBR: 2C16h, 40400000h). Written here, the ME
         * stalled before the first stage marker after this block
         * (V9XR63D runs 1-3, 2026-11-13). */
        if (update->offset == V9X_R6_CP_QUEUE_THRESHOLDS ||
            update->offset == V9X_R6_CP_MEQ_THRESHOLDS) {
            continue;
        }
        value = (read_values[index] & update->keep) | update->set;
        if (update->offset == V9X_R6_GB_TILING_CONFIG) {
            value = tiling_config;
        }
        v9x_r6_pm4_set1(stream, update->offset, value);
    }
}

/* Context registers (r600_reg_auto_r6xx.h, r600_reg_r6xx.h; the R7xx_
 * names are r600_reg_r7xx.h's, which xf86's default state writes on R6xx
 * too). */
#define R6_DB_DEPTH_INFO            0x28010ul
#define R6_DB_STENCIL_CLEAR         0x28028ul
#define R6_PA_SC_SCREEN_SCISSOR_TL  0x28030ul
#define R6_CB_COLOR0_BASE           0x28040ul
#define R6_CB_COLOR0_SIZE           0x28060ul
#define R6_CB_COLOR0_VIEW           0x28080ul
#define R6_CB_COLOR0_INFO           0x280a0ul
#define R6_CB_COLOR0_TILE           0x280c0ul
#define R6_CB_COLOR0_FRAG           0x280e0ul
#define R6_CB_COLOR7_FRAG           0x280fcul
#define R6_CB_COLOR0_MASK           0x28100ul
#define R6_PA_SC_WINDOW_OFFSET      0x28200ul
#define R6_PA_SC_WINDOW_SCISSOR_TL  0x28204ul
#define R6_PA_SC_CLIPRECT_RULE      0x2820cul
#define R6_PA_SC_CLIPRECT_0_TL      0x28210ul
#define R6_PA_SC_EDGERULE           0x28230ul
#define R6_CB_TARGET_MASK           0x28238ul
#define R6_CB_SHADER_MASK           0x2823cul
#define R6_PA_SC_GENERIC_SCISSOR_TL 0x28240ul
#define R6_PA_SC_VPORT_SCISSOR_0_TL 0x28250ul
#define R6_SX_MISC                  0x28350ul
#define R6_VGT_MAX_VTX_INDX         0x28400ul
#define R6_SX_ALPHA_TEST_CONTROL    0x28410ul
#define R6_DB_STENCILREFMASK        0x28430ul
#define R6_SPI_VS_OUT_ID_0          0x28614ul
#define R6_SPI_PS_INPUT_CNTL_0      0x28644ul
#define R6_SPI_VS_OUT_CONFIG        0x286c4ul
#define R6_SPI_THREAD_GROUPING      0x286c8ul
#define R6_SPI_PS_IN_CONTROL_0      0x286ccul
#define R6_SPI_INPUT_Z              0x286d8ul
#define R6_CB_BLEND0_CONTROL        0x28780ul
#define R6_CB_SHADER_CONTROL        0x287a0ul
#define R6_DB_DEPTH_CONTROL         0x28800ul
#define R6_CB_COLOR_CONTROL         0x28808ul
#define R6_DB_SHADER_CONTROL        0x2880cul
#define R6_PA_CL_CLIP_CNTL          0x28810ul
#define R6_SQ_PGM_START_PS          0x28840ul
#define R6_SQ_PGM_RESOURCES_PS      0x28850ul
#define R6_SQ_PGM_EXPORTS_PS        0x28854ul
#define R6_SQ_PGM_START_VS          0x28858ul
#define R6_SQ_PGM_RESOURCES_VS      0x28868ul
#define R6_SQ_PGM_START_FS          0x28894ul
#define R6_SQ_PGM_RESOURCES_FS      0x288a4ul
#define R6_SQ_ESGS_RING_ITEMSIZE    0x288a8ul
#define R6_SQ_PGM_CF_OFFSET_PS      0x288ccul
#define R6_SQ_PGM_CF_OFFSET_VS      0x288d0ul
#define R6_SQ_PGM_CF_OFFSET_FS      0x288dcul
#define R6_PA_SU_POINT_SIZE         0x28a00ul
#define R6_PA_SC_LINE_STIPPLE       0x28a0cul
#define R6_PA_SC_MPASS_PS_CNTL      0x28a48ul
#define R6_PA_SC_MODE_CNTL          0x28a4cul
#define R6_VGT_PRIMITIVEID_EN       0x28a84ul
#define R6_VGT_MULTI_PRIM_IB_RESET_EN 0x28a94ul
#define R6_VGT_INSTANCE_STEP_RATE_0 0x28aa0ul
#define R6_VGT_STRMOUT_EN           0x28ab0ul
#define R6_VGT_STRMOUT_BUFFER_EN    0x28b20ul
#define R6_PA_SC_LINE_CNTL          0x28c00ul
#define R6_PA_SC_AA_CONFIG          0x28c04ul
#define R6_CB_CLRCMP_CONTROL        0x28c30ul
#define R6_PA_SC_AA_MASK            0x28c48ul
#define R6_VGT_VERTEX_REUSE_BLOCK   0x28c58ul
#define R6_VGT_OUT_DEALLOC_CNTL     0x28c5cul
#define R6_DB_RENDER_CONTROL        0x28d0cul
#define R6_DB_ALPHA_TO_MASK         0x28d44ul
#define R6_PA_SU_POLY_OFFSET_DB_FMT 0x28df8ul
/* The PS's ALU constant 0, and the VS's first vertex resource
 * (SQ_VTX_RESOURCE_vs = 160, 28 bytes each). */
#define R6_SQ_ALU_CONSTANT_PS_0     0x30000ul
#define R6_SQ_VTX_RESOURCE_VS_0     (0x38000ul + 160ul * 28ul)
#define R6_SQ_VTX_BASE_VTX_LOC      0x3cff0ul

/* r600_gpu_init's context registers: the inactive quad pipes of 8
 * (CC_GC_SHADER_PIPE_CONFIG 15:8) give the active ones, four dealloc and
 * four - 2 reuse entries each. M-NZHWBR's 00F8FC00h leaves 2 pipes: 8, 6. */
#define R6_MAX_PIPES                8ul
#define R6_INACTIVE_QD_PIPES_MASK   0x0000ff00ul
#define R6_INACTIVE_QD_PIPES_SHIFT  8
#define R6_DEALLOC_DIST_MASK        0x7ful
#define R6_VTX_REUSE_DEPTH_MASK     0xfful
#define R6_CB_COLOR_BASES           8ul
#define R6_PS_IN_NUM_INTERP_2       2ul

/* xf86's default-state values (r6xx_accel.c, pre-RV770 branches). */
#define R6_CONTEXT_CONTROL_LOAD     0x80000000ul
#define R6_CONTEXT_CONTROL_SHADOW   0x80000000ul
/* SQ_CONFIG: VC_ENABLE | DX9_CONSTS | ALU_INST_PREFER_VECTOR |
 * VS_PRIO(1) | GS_PRIO(2) | ES_PRIO(3). */
#define R6_SQ_CONFIG_XF86           0xe400000dul
/* RV630: 84 PS, 36 VS, 4 temp GPRs; 144 PS, 40 VS, 4 GS, 4 ES threads;
 * 40/40/32/16 stack entries. */
#define R6_SQ_GPR_1_XF86            0x40240054ul
#define R6_SQ_THREAD_XF86           0x04042890ul
/* GRADIENT_CREDIT(3) | TD_FIFO_CREDIT(28) */
#define R6_TA_CNTL_AUX_XF86         0x07000003ul
/* "0x82000000 ?" in xf86; r600_gpu_init writes 80000000h for the RV630
 * and is overridden by this, as xf86 overrides it on a running card. */
#define R6_DB_DEBUG_XF86            0x82000000ul
/* STENCIL_COMPRESS_DISABLE | DEPTH_COMPRESS_DISABLE, then
 * FORCE_SHADER_Z_ORDER. */
#define R6_DB_RENDER_CONTROL_XF86   0x00000060ul
#define R6_DB_RENDER_OVERRIDE_XF86  0x00000040ul
/* ALPHA_TO_MASK_OFFSET0..3 all 2. */
#define R6_DB_ALPHA_TO_MASK_XF86    0x0000aa00ul
/* Z_ORDER(1) (EARLY_Z_THEN_LATE_Z by xf86's comment) | DUAL_EXPORT_ENABLE */
#define R6_DB_SHADER_CONTROL_XF86   0x00000210ul
#define R6_CB_CLRCMP_FCN_SRC        0x01000000ul /* CLRCMP_FCN_SEL(1) */
#define R6_CB_OUTPUT0_ENABLE        0x0000000ful
#define R6_CB_RT0_ENABLE            0x00000001ul
#define R6_PA_SC_CLIP_RULE_ALL      0x0000fffful
#define R6_CLIP_RECTS               4ul
#define R6_VPORT_SCISSORS           16ul
#define R6_SCISSOR_PAIR_STRIDE      8ul
#define R6_SCISSOR_8192             0x20002000ul /* 8192 | 8192 << 16 */
#define R6_WINDOW_OFFSET_DISABLE    0x80000000ul
/* WALK_ORDER_ENABLE | FORCE_EOV_CNTDWN_ENABLE */
#define R6_PA_SC_MPASS_XF86         0x00004010ul
/* ROUND_MODE(2) | PIX_CENTER | QUANT_MODE(5): "Round to Even, fixed
 * point 1/256". */
#define R6_PA_SU_VTX_CNTL_XF86      0x0000002dul
#define R6_FLOAT_ONE                0x3f800000ul
#define R6_PA_CL_CLIP_DISABLE       0x00010000ul
#define R6_PA_SU_SC_FACE            0x00000004ul
#define R6_PA_CL_VTX_XY_FMT         0x00000100ul
/* SEMANTIC_0(0) | SEMANTIC_1(1) */
#define R6_SPI_VS_OUT_ID_XF86       0x00000100ul
/* SEMANTIC(n) | DEFAULT_VAL(1) | SEL_CENTROID */
#define R6_SPI_PS_INPUT_XF86        0x00000900ul
#define R6_VGT_MAX_VTX_INDX_XF86    0x00fffffful
#define R6_PA_SU_LINE_WIDTH_1       8ul          /* PA_SU_LINE_CNTL WIDTH(8) */

/* The longest run of zeros the default state writes. */
#define R6_ZERO_RUN_MAX             17u
static const v9x_u32 v9x_r6_zeros[R6_ZERO_RUN_MAX] = { 0ul };

void v9x_r6_3d_default_state(struct v9x_r6_pm4 *stream,
                             v9x_u32 cc_gc_shader_pipe_config,
                             v9x_u32 fs_gpu)
{
    v9x_u32 values[R6_ZERO_RUN_MAX];
    v9x_u32 inactive;
    v9x_u32 pipes;
    v9x_u32 index;

    /* r600_start_3d: START_3D_CMDBUF before RV770, then CONTEXT_CONTROL. */
    v9x_r6_pm4_put(stream,
                   v9x_r6_pm4_header(V9X_R6_P3_START_3D_CMDBUF, 1ul));
    v9x_r6_pm4_put(stream, 0ul);
    v9x_r6_pm4_put(stream,
                   v9x_r6_pm4_header(V9X_R6_P3_CONTEXT_CONTROL, 2ul));
    v9x_r6_pm4_put(stream, R6_CONTEXT_CONTROL_LOAD);
    v9x_r6_pm4_put(stream, R6_CONTEXT_CONTROL_SHADOW);

    /* r600_gpu_init's context registers, in its order. Linux writes them
     * by MMIO before the CP runs; here they are the first context state.
     * SPI_PS_IN_CONTROL_0's two interpolants are overwritten by the draw's
     * r600_set_spi. */
    pipes = R6_MAX_PIPES;
    inactive = (cc_gc_shader_pipe_config & R6_INACTIVE_QD_PIPES_MASK) >>
               R6_INACTIVE_QD_PIPES_SHIFT;
    while (inactive != 0ul) {
        pipes -= inactive & 1ul;
        inactive >>= 1;
    }
    v9x_r6_pm4_set1(stream, R6_VGT_OUT_DEALLOC_CNTL,
                    (pipes * 4ul) & R6_DEALLOC_DIST_MASK);
    v9x_r6_pm4_set1(stream, R6_VGT_VERTEX_REUSE_BLOCK,
                    (pipes * 4ul - 2ul) & R6_VTX_REUSE_DEPTH_MASK);
    v9x_r6_pm4_set1(stream, R6_VGT_STRMOUT_EN, 0ul);
    v9x_r6_pm4_set1(stream, R6_SX_MISC, 0ul);
    v9x_r6_pm4_set1(stream, R6_PA_SC_MODE_CNTL, 0ul);
    v9x_r6_pm4_set1(stream, R6_PA_SC_AA_CONFIG, 0ul);
    v9x_r6_pm4_set1(stream, R6_PA_SC_LINE_STIPPLE, 0ul);
    v9x_r6_pm4_set1(stream, R6_SPI_INPUT_Z, 0ul);
    v9x_r6_pm4_set1(stream, R6_SPI_PS_IN_CONTROL_0, R6_PS_IN_NUM_INTERP_2);
    v9x_r6_pm4_set1(stream, R6_CB_COLOR7_FRAG, 0ul);
    v9x_r6_pm4_set(stream, R6_CB_COLOR0_BASE, v9x_r6_zeros,
                   R6_CB_COLOR_BASES);

    /* r600_sq_setup with xf86's RV630 numbers. They differ from
     * r600_gpu_init's split (44/44/2 GPRs, 18 GS and ES; 79/78/4/31
     * threads), which this replaces as xf86 replaces the kernel's. */
    values[0] = R6_SQ_CONFIG_XF86;
    values[1] = R6_SQ_GPR_1_XF86;
    values[2] = 0ul;
    values[3] = R6_SQ_THREAD_XF86;
    values[4] = R6_SQ_STACK_1_VALUE;
    values[5] = R6_SQ_STACK_2_VALUE;
    v9x_r6_pm4_set(stream, V9X_R6_SQ_CONFIG, values, 6ul);

    /* The unused depth buffer (xf86 relocates it to its shader buffer;
     * format 0 is invalid, so nothing reads it). */
    v9x_r6_pm4_set1(stream, R6_DB_DEPTH_INFO, 0ul);

    v9x_r6_pm4_set1(stream, V9X_R6_TA_CNTL_AUX, R6_TA_CNTL_AUX_XF86);
    v9x_r6_pm4_set1(stream, V9X_R6_VC_ENHANCE, 0ul);
    v9x_r6_pm4_set1(stream, V9X_R6_SQ_DYN_GPR_CNTL_PS_FLUSH, 0ul);
    v9x_r6_pm4_set1(stream, V9X_R6_DB_DEBUG, R6_DB_DEBUG_XF86);
    v9x_r6_pm4_set1(stream, V9X_R6_DB_WATERMARKS, R6_DB_WATERMARKS_VALUE);

    v9x_r6_pm4_set(stream, R6_SQ_VTX_BASE_VTX_LOC, v9x_r6_zeros, 2ul);
    /* SQ_ESGS_RING_ITEMSIZE .. SQ_GS_VERT_ITEMSIZE */
    v9x_r6_pm4_set(stream, R6_SQ_ESGS_RING_ITEMSIZE, v9x_r6_zeros, 9ul);

    /* DB */
    v9x_r6_pm4_set1(stream, R6_DB_DEPTH_CONTROL, 0ul);
    values[0] = R6_DB_RENDER_CONTROL_XF86;
    values[1] = R6_DB_RENDER_OVERRIDE_XF86;
    v9x_r6_pm4_set(stream, R6_DB_RENDER_CONTROL, values, 2ul);
    v9x_r6_pm4_set1(stream, R6_DB_ALPHA_TO_MASK, R6_DB_ALPHA_TO_MASK_XF86);
    v9x_r6_pm4_set1(stream, R6_DB_SHADER_CONTROL, R6_DB_SHADER_CONTROL_XF86);
    /* DB_STENCIL_CLEAR, DB_DEPTH_CLEAR */
    v9x_r6_pm4_set(stream, R6_DB_STENCIL_CLEAR, v9x_r6_zeros, 2ul);
    /* DB_STENCILREFMASK, DB_STENCILREFMASK_BF, SX_ALPHA_REF */
    v9x_r6_pm4_set(stream, R6_DB_STENCILREFMASK, v9x_r6_zeros, 3ul);

    /* CB_CLRCMP_CONTROL (use CLRCMP_FCN_SRC), _SRC, _DST, _MSK */
    values[0] = R6_CB_CLRCMP_FCN_SRC;
    values[1] = 0ul;
    values[2] = 0ul;
    values[3] = 0ul;
    v9x_r6_pm4_set(stream, R6_CB_CLRCMP_CONTROL, values, 4ul);
    v9x_r6_pm4_set1(stream, R6_CB_SHADER_MASK, R6_CB_OUTPUT0_ENABLE);
    v9x_r6_pm4_set1(stream, R6_CB_SHADER_CONTROL, R6_CB_RT0_ENABLE);
    /* SX_ALPHA_TEST_CONTROL, CB_BLEND_RED, _GREEN, _BLUE, _ALPHA */
    v9x_r6_pm4_set(stream, R6_SX_ALPHA_TEST_CONTROL, v9x_r6_zeros, 5ul);

    v9x_r6_pm4_set1(stream, R6_PA_SC_WINDOW_OFFSET, 0ul);
    v9x_r6_pm4_set1(stream, R6_PA_SC_EDGERULE, 0ul);
    v9x_r6_pm4_set1(stream, R6_PA_SC_CLIPRECT_RULE, R6_PA_SC_CLIP_RULE_ALL);

    /* The clip rectangles, (0, 0)-(8192, 8192): the clip rule makes them
     * always visible. */
    values[0] = 0ul;
    values[1] = R6_SCISSOR_8192;
    for (index = 0ul; index < R6_CLIP_RECTS; ++index) {
        v9x_r6_pm4_set(stream,
                       R6_PA_SC_CLIPRECT_0_TL +
                       index * R6_SCISSOR_PAIR_STRIDE, values, 2ul);
    }
    values[0] = R6_WINDOW_OFFSET_DISABLE;
    for (index = 0ul; index < R6_VPORT_SCISSORS; ++index) {
        v9x_r6_pm4_set(stream,
                       R6_PA_SC_VPORT_SCISSOR_0_TL +
                       index * R6_SCISSOR_PAIR_STRIDE, values, 2ul);
    }

    values[0] = 0ul;
    values[1] = R6_PA_SC_MPASS_XF86;
    v9x_r6_pm4_set(stream, R6_PA_SC_MPASS_PS_CNTL, values, 2ul);

    /* PA_SC_LINE_CNTL, PA_SC_AA_CONFIG, PA_SU_VTX_CNTL, the four guard-band
     * adjusts at 1.0, PA_SC_AA_SAMPLE_LOCS_MCTX, _8S_WD1_M. */
    values[0] = 0ul;
    values[1] = 0ul;
    values[2] = R6_PA_SU_VTX_CNTL_XF86;
    values[3] = R6_FLOAT_ONE;
    values[4] = R6_FLOAT_ONE;
    values[5] = R6_FLOAT_ONE;
    values[6] = R6_FLOAT_ONE;
    values[7] = 0ul;
    values[8] = 0ul;
    v9x_r6_pm4_set(stream, R6_PA_SC_LINE_CNTL, values, 9ul);
    v9x_r6_pm4_set1(stream, R6_PA_SC_AA_MASK, 0xfffffffful);

    /* PA_CL_CLIP_CNTL, PA_SU_SC_MODE_CNTL, PA_CL_VTE_CNTL, PA_CL_VS_OUT_CNTL,
     * PA_CL_NANINF_CNTL: clipping off, x and y already in screen space. */
    values[0] = R6_PA_CL_CLIP_DISABLE;
    values[1] = R6_PA_SU_SC_FACE;
    values[2] = R6_PA_CL_VTX_XY_FMT;
    values[3] = 0ul;
    values[4] = 0ul;
    v9x_r6_pm4_set(stream, R6_PA_CL_CLIP_CNTL, values, 5ul);
    v9x_r6_pm4_set(stream, R6_PA_SU_POLY_OFFSET_DB_FMT, v9x_r6_zeros, 6ul);

    /* SPI */
    v9x_r6_pm4_set1(stream, R6_SPI_THREAD_GROUPING, 0ul);
    v9x_r6_pm4_set1(stream, R6_SPI_VS_OUT_ID_0, R6_SPI_VS_OUT_ID_XF86);
    values[0] = R6_SPI_PS_INPUT_XF86;
    values[1] = R6_SPI_PS_INPUT_XF86 | 1ul;
    v9x_r6_pm4_set(stream, R6_SPI_PS_INPUT_CNTL_0, values, 2ul);
    /* SPI_INPUT_Z, SPI_FOG_CNTL, SPI_FOG_FUNC_SCALE, SPI_FOG_FUNC_BIAS */
    v9x_r6_pm4_set(stream, R6_SPI_INPUT_Z, v9x_r6_zeros, 4ul);

    /* r600_fs_setup of a cleared configuration. */
    v9x_r6_pm4_set1(stream, R6_SQ_PGM_START_FS, fs_gpu >> 8);
    v9x_r6_pm4_set1(stream, R6_SQ_PGM_RESOURCES_FS, 0ul);
    v9x_r6_pm4_set1(stream, R6_SQ_PGM_CF_OFFSET_FS, 0ul);

    /* VGT: VGT_MAX_VTX_INDX, _MIN_, VGT_INDX_OFFSET,
     * VGT_MULTI_PRIM_IB_RESET_INDX. */
    values[0] = R6_VGT_MAX_VTX_INDX_XF86;
    values[1] = 0ul;
    values[2] = 0ul;
    values[3] = 0ul;
    v9x_r6_pm4_set(stream, R6_VGT_MAX_VTX_INDX, values, 4ul);
    v9x_r6_pm4_set1(stream, R6_VGT_PRIMITIVEID_EN, 0ul);
    v9x_r6_pm4_set1(stream, R6_VGT_MULTI_PRIM_IB_RESET_EN, 0ul);
    v9x_r6_pm4_set(stream, R6_VGT_INSTANCE_STEP_RATE_0, v9x_r6_zeros, 2ul);
    /* PA_SU_POINT_SIZE through VGT_GS_MODE, seventeen registers, the line
     * one pixel wide. */
    for (index = 0ul; index < R6_ZERO_RUN_MAX; ++index) {
        values[index] = 0ul;
    }
    values[2] = R6_PA_SU_LINE_WIDTH_1;
    v9x_r6_pm4_set(stream, R6_PA_SU_POINT_SIZE, values, R6_ZERO_RUN_MAX);
    /* VGT_STRMOUT_EN, VGT_REUSE_OFF, VGT_VTX_CNT_EN */
    v9x_r6_pm4_set(stream, R6_VGT_STRMOUT_EN, v9x_r6_zeros, 3ul);
    v9x_r6_pm4_set1(stream, R6_VGT_STRMOUT_BUFFER_EN, 0ul);
    v9x_r6_pm4_set1(stream, R6_SX_MISC, 0ul);
}

/* Draw-time fields (r600_reg_auto_r6xx.h). */
#define R6_COLOR_8_8_8_8            0x1aul
#define R6_CB_FORMAT_SHIFT          2
#define R6_CB_ARRAY_LINEAR_GENERAL  0ul
#define R6_CB_ARRAY_MODE_SHIFT      8
#define R6_CB_COMP_SWAP_ARGB        0x00010000ul /* COMP_SWAP(1) << 16 */
#define R6_CB_BLEND_CLAMP           0x00100000ul
#define R6_CB_SOURCE_FORMAT         0x08000000ul
#define R6_CB_SLICE_SHIFT           10
#define R6_CB_TILE_PIXELS           64ul
#define R6_CB_WRITE_ARGB            0x0000000ful /* B 4 | G 2 | R 1 | A 8 */
#define R6_ROP3_COPY                0x00cc0000ul /* RADEON_ROP3_S, GXcopy */
#define R6_SURFACE_BASE_COLOR0      0x00000002ul /* COLOR0_BASE: 2 << 0 */
#define R6_PS_EXPORTS_COLOR         2ul          /* ps_conf.export_mode */
#define R6_PGM_UNCACHED_FIRST_INST  0x10000000ul
#define R6_VTX_STRIDE_SHIFT         8
#define R6_VTX_MEM_REQUEST_SIZE_1   1ul
#define R6_VTX_VALID_BUFFER         0xc0000000ul /* TYPE(3) << 30 */
#define R6_DI_PT_RECTLIST           0x11ul
#define R6_DI_INDEX_SIZE_16_BIT     0ul
#define R6_DI_SRC_SEL_AUTO_INDEX    2ul
#define R6_CACHE_FLUSH_AND_INV      0x16ul
/* WAIT_3D_IDLE | WAIT_3D_IDLECLEAN */
#define R6_WAIT_3D_IDLE_CLEAN       0x00028000ul
#define R6_UNORM8_MAX               255ul

/* Where R, G, B and A sit in an ARGB8888 pixel. */
static const v9x_u32 v9x_r6_argb_shifts[4] = { 16ul, 8ul, 0ul, 24ul };

static int v9x_r6_3d_aligned(v9x_u32 gpu)
{
    return (gpu & R6_ALIGN_256) == 0ul;
}

static void v9x_r6_3d_scissor(struct v9x_r6_pm4 *stream, v9x_u32 offset,
                              v9x_u32 tl, v9x_u32 br)
{
    v9x_u32 values[2];

    values[0] = tl;
    values[1] = br;
    v9x_r6_pm4_set(stream, offset, values, 2ul);
}

v9x_status v9x_r6_3d_solid(const struct v9x_r6_3d_solid *solid,
                           struct v9x_r6_pm4 *stream)
{
    v9x_u32 values[7];
    v9x_u32 br;
    v9x_u32 height8;
    v9x_u32 channel;

    if (solid == 0 || stream == 0 ||
        !v9x_r6_3d_aligned(solid->target_gpu) ||
        !v9x_r6_3d_aligned(solid->vs_gpu) ||
        !v9x_r6_3d_aligned(solid->ps_gpu) ||
        !v9x_r6_3d_aligned(solid->vertex_gpu) ||
        solid->target_pitch == 0ul || (solid->target_pitch & 7ul) != 0ul ||
        solid->target_pitch > V9X_R6_3D_PITCH_MAX ||
        solid->target_height == 0ul ||
        solid->target_height > V9X_R6_3D_PITCH_MAX) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    /* R600PrepareSolid: the generic, screen and window scissors at the
     * target's width and height. */
    br = solid->target_pitch | (solid->target_height << 16);
    v9x_r6_3d_scissor(stream, R6_PA_SC_GENERIC_SCISSOR_TL,
                      R6_WINDOW_OFFSET_DISABLE, br);
    v9x_r6_3d_scissor(stream, R6_PA_SC_SCREEN_SCISSOR_TL, 0ul, br);
    v9x_r6_3d_scissor(stream, R6_PA_SC_WINDOW_SCISSOR_TL,
                      R6_WINDOW_OFFSET_DISABLE, br);

    /* r600_vs_setup and r600_ps_setup: the shader cache flushed over each
     * shader, then its start, resources and CF offset. */
    v9x_r6_pm4_surface_sync(stream, V9X_R6_SYNC_SH_ACTION,
                            V9X_R6_3D_SHADER_SYNC_BYTES, solid->vs_gpu);
    v9x_r6_pm4_set1(stream, R6_SQ_PGM_START_VS, solid->vs_gpu >> 8);
    v9x_r6_pm4_set1(stream, R6_SQ_PGM_RESOURCES_VS, V9X_R6_SOLID_VS_GPRS);
    v9x_r6_pm4_set1(stream, R6_SQ_PGM_CF_OFFSET_VS, 0ul);
    v9x_r6_pm4_surface_sync(stream, V9X_R6_SYNC_SH_ACTION,
                            V9X_R6_3D_SHADER_SYNC_BYTES, solid->ps_gpu);
    v9x_r6_pm4_set1(stream, R6_SQ_PGM_START_PS, solid->ps_gpu >> 8);
    v9x_r6_pm4_set1(stream, R6_SQ_PGM_RESOURCES_PS,
                    V9X_R6_SOLID_PS_GPRS | R6_PGM_UNCACHED_FIRST_INST);
    v9x_r6_pm4_set1(stream, R6_SQ_PGM_EXPORTS_PS, R6_PS_EXPORTS_COLOR);
    v9x_r6_pm4_set1(stream, R6_SQ_PGM_CF_OFFSET_PS, 0ul);

    /* r600_set_render_target. The base, then SURFACE_BASE_UPDATE: xf86's
     * "rv6xx workaround" for every family after R600 and before RV770.
     * CMASK and FMASK point at the target, as xf86's relocations make
     * them ("we don't use those"). The height rounds up to 8 for the
     * slice. */
    v9x_r6_pm4_set1(stream, R6_CB_COLOR0_BASE, solid->target_gpu >> 8);
    v9x_r6_pm4_put(stream,
                   v9x_r6_pm4_header(V9X_R6_P3_SURFACE_BASE_UPDATE, 1ul));
    v9x_r6_pm4_put(stream, R6_SURFACE_BASE_COLOR0);
    v9x_r6_pm4_set1(stream, R6_CB_COLOR0_TILE, solid->target_gpu >> 8);
    v9x_r6_pm4_set1(stream, R6_CB_COLOR0_FRAG, solid->target_gpu >> 8);
    height8 = (solid->target_height + 7ul) & ~7ul;
    v9x_r6_pm4_set1(stream, R6_CB_COLOR0_SIZE,
                    ((solid->target_pitch >> 3) - 1ul) |
                    (((solid->target_pitch * height8) / R6_CB_TILE_PIXELS -
                      1ul) << R6_CB_SLICE_SHIFT));
    v9x_r6_pm4_set1(stream, R6_CB_COLOR0_VIEW, 0ul);
    v9x_r6_pm4_set1(stream, R6_CB_COLOR0_MASK, 0ul);
    v9x_r6_pm4_set1(stream, R6_CB_COLOR0_INFO,
                    (R6_COLOR_8_8_8_8 << R6_CB_FORMAT_SHIFT) |
                    (R6_CB_ARRAY_LINEAR_GENERAL << R6_CB_ARRAY_MODE_SHIFT) |
                    R6_CB_COMP_SWAP_ARGB | R6_CB_BLEND_CLAMP |
                    R6_CB_SOURCE_FORMAT);
    v9x_r6_pm4_set1(stream, R6_CB_TARGET_MASK, R6_CB_WRITE_ARGB);
    v9x_r6_pm4_set1(stream, R6_CB_COLOR_CONTROL, R6_ROP3_COPY);
    v9x_r6_pm4_set1(stream, R6_CB_BLEND0_CONTROL, 0ul);

    /* r600_set_spi(0, 0): one VS export, no interpolants. */
    v9x_r6_pm4_set1(stream, R6_SPI_VS_OUT_CONFIG, 0ul);
    v9x_r6_pm4_set(stream, R6_SPI_PS_IN_CONTROL_0, v9x_r6_zeros, 3ul);

    /* The PS's constant 0: R, G, B, A as fractions of 255, as
     * R600PrepareSolid computes (float)r / 255 from the ARGB pixel. */
    for (channel = 0ul; channel < 4ul; ++channel) {
        values[channel] = v9x_eg_unorm_float_bits(
            (solid->colour >> v9x_r6_argb_shifts[channel]) & R6_UNORM8_MAX,
            R6_UNORM8_MAX);
    }
    v9x_r6_pm4_set(stream, R6_SQ_ALU_CONSTANT_PS_0, values, 4ul);

    /* r600_finish_op's r600_set_vtx_resource: the vertex cache synced over
     * the vertices (RV630 has one), then the VS's resource 0: base, size
     * in bytes - 1, stride 8, MEM_REQUEST_SIZE 1, a valid buffer. */
    v9x_r6_pm4_surface_sync(stream, V9X_R6_SYNC_VC_ACTION,
                            V9X_R6_3D_VERTEX_DWORDS * 4ul,
                            solid->vertex_gpu);
    values[0] = solid->vertex_gpu;
    values[1] = V9X_R6_3D_VERTEX_DWORDS * 4ul - 1ul;
    values[2] = V9X_R6_3D_VERTEX_STRIDE << R6_VTX_STRIDE_SHIFT;
    values[3] = R6_VTX_MEM_REQUEST_SIZE_1;
    values[4] = 0ul;
    values[5] = 0ul;
    values[6] = R6_VTX_VALID_BUFFER;
    v9x_r6_pm4_set(stream, R6_SQ_VTX_RESOURCE_VS_0, values, 7ul);

    /* r600_draw_auto: a RECTLIST of the three vertices. */
    v9x_r6_pm4_set1(stream, V9X_R6_VGT_PRIMITIVE_TYPE, R6_DI_PT_RECTLIST);
    v9x_r6_pm4_put(stream, v9x_r6_pm4_header(V9X_R6_P3_INDEX_TYPE, 1ul));
    v9x_r6_pm4_put(stream, R6_DI_INDEX_SIZE_16_BIT);
    v9x_r6_pm4_put(stream, v9x_r6_pm4_header(V9X_R6_P3_NUM_INSTANCES, 1ul));
    v9x_r6_pm4_put(stream, 1ul);
    v9x_r6_pm4_put(stream, v9x_r6_pm4_header(V9X_R6_P3_DRAW_INDEX_AUTO, 2ul));
    v9x_r6_pm4_put(stream,
                   V9X_R6_3D_VERTEX_DWORDS * 4ul / V9X_R6_3D_VERTEX_STRIDE);
    v9x_r6_pm4_put(stream, R6_DI_SRC_SEL_AUTO_INDEX);

    /* r600_wait_3d_idle_clean, then the CB synced over the target, in
     * r600_finish_op's order. */
    v9x_r6_pm4_put(stream, v9x_r6_pm4_header(V9X_R6_P3_EVENT_WRITE, 1ul));
    v9x_r6_pm4_put(stream, R6_CACHE_FLUSH_AND_INV);
    v9x_r6_pm4_set1(stream, V9X_R6_WAIT_UNTIL, R6_WAIT_3D_IDLE_CLEAN);
    v9x_r6_pm4_surface_sync(stream,
                            V9X_R6_SYNC_CB_ACTION | V9X_R6_SYNC_CB0_DEST_BASE,
                            solid->target_pitch * solid->target_height * 4ul,
                            solid->target_gpu);
    return stream->overflow != 0ul ? V9X_STATUS_INSUFFICIENT_MEMORY
                                   : V9X_STATUS_OK;
}

/* A single's exponent bias and its 23 stored significand bits. */
#define R6_FLOAT_EXPONENT_BIAS  127ul
#define R6_FLOAT_MANTISSA_BITS  23ul
#define R6_FLOAT_MANTISSA_MASK  0x007ffffful

v9x_u32 v9x_r6_3d_float_bits(v9x_u32 value)
{
    v9x_u32 exponent = 0ul;

    if (value == 0ul) {
        return 0ul;
    }
    while ((value >> exponent) > 1ul) {
        ++exponent;
    }
    /* The leading one at bit exponent, the bits below it moved to 22 and
     * down. Above 2^24 the low bits are truncated, not rounded. */
    return ((R6_FLOAT_EXPONENT_BIAS + exponent) << R6_FLOAT_MANTISSA_BITS) |
           ((exponent <= R6_FLOAT_MANTISSA_BITS ?
             value << (R6_FLOAT_MANTISSA_BITS - exponent) :
             value >> (exponent - R6_FLOAT_MANTISSA_BITS)) &
            R6_FLOAT_MANTISSA_MASK);
}

void v9x_r6_3d_rect_vertices(v9x_u32 x1, v9x_u32 y1, v9x_u32 x2, v9x_u32 y2,
                             v9x_u32 *out)
{
    out[0] = v9x_r6_3d_float_bits(x1);
    out[1] = v9x_r6_3d_float_bits(y1);
    out[2] = v9x_r6_3d_float_bits(x1);
    out[3] = v9x_r6_3d_float_bits(y2);
    out[4] = v9x_r6_3d_float_bits(x2);
    out[5] = v9x_r6_3d_float_bits(y2);
}

void v9x_r6_3d_fence(struct v9x_r6_pm4 *stream, v9x_u32 value)
{
    v9x_r6_pm4_set1(stream, V9X_R6_SCRATCH_REG0, value);
}

/*
 * The batch state's registers and fields, each in xf86's
 * r600_reg_auto_r6xx.h and Mesa r600's r600d.h at the same offset and
 * position (the host test holds them to both).
 */
#define R6_DB_DEPTH_SIZE            0x28000ul
#define R6_DB_DEPTH_BASE            0x2800cul
#define R6_PA_SC_VPORT_ZMIN_0       0x282d0ul
#define R6_SX_ALPHA_REF             0x28438ul
#define R6_PA_CL_VTE_CNTL           0x28818ul
#define R6_PA_SU_VTX_CNTL           0x28c08ul
#define R6_DB_RENDER_OVERRIDE       0x28d10ul
#define R6_DB_HTILE_SURFACE         0x28d24ul
/* DB_SRESULTS_COMPARE_STATE0, _STATE1, DB_PRELOAD_CONTROL. */
#define R6_DB_SRESULTS_COMPARE_STATE0 0x28d28ul
#define R6_DB_SRESULTS_DWORDS       3ul
#define R6_DB_PREFETCH_LIMIT        0x28d34ul
/* The PS's texture resource 0 (SQ_TEX_RESOURCE_ps 0, 28 bytes each) and
 * sampler 0 (SQ_TEX_SAMPLER_WORD_ps 0, 12 bytes each). */
#define R6_SQ_TEX_RESOURCE_PS_0     0x38000ul
#define R6_SQ_TEX_SAMPLER_PS_0      0x3c000ul

/* DB_DEPTH_CONTROL: Z_ENABLE, Z_WRITE_ENABLE, ZFUNC 6:4. */
#define R6_DEPTH_Z_ENABLE           0x00000002ul
#define R6_DEPTH_Z_WRITE            0x00000004ul
#define R6_DEPTH_ZFUNC_SHIFT        4
#define R6_FUNC_MASK                7ul
/* CB_BLEND0_CONTROL: COLOR_SRCBLEND 4:0, COLOR_COMB_FCN 7:5 (0, DST_PLUS_
 * SRC), COLOR_DESTBLEND 12:8, ALPHA_ 20:16, 23:21 and 28:24. */
#define R6_BLEND_FACTOR_MASK        0x1ful
#define R6_BLEND_COLOR_DST_SHIFT    8
#define R6_BLEND_ALPHA_SRC_SHIFT    16
#define R6_BLEND_ALPHA_DST_SHIFT    24
/* CB_COLOR_CONTROL: PER_MRT_BLEND and TARGET_BLEND_ENABLE's target 0, as
 * r600_set_render_target sets them on every R6xx after the R600. */
#define R6_CB_PER_MRT_BLEND         0x00000080ul
#define R6_CB_TARGET0_BLEND         0x00000100ul
/* SX_ALPHA_TEST_CONTROL: ALPHA_FUNC 2:0, ALPHA_TEST_ENABLE 3. */
#define R6_ALPHA_TEST_ENABLE        0x00000008ul
/* SQ_TEX_SAMPLER_WORD0: CLAMP_Y 5:3, CLAMP_Z 8:6 (left WRAP, 0, as xf86),
 * XY_MAG_FILTER 11:9, XY_MIN_FILTER 14:12; WORD2's MC_COORD_TRUNCATE,
 * which xf86 sets for point filtering only. */
#define R6_SAMPLER_CLAMP_MASK       7ul
#define R6_SAMPLER_CLAMP_Y_SHIFT    3
#define R6_SAMPLER_MAG_SHIFT        9
#define R6_SAMPLER_MIN_SHIFT        12
#define R6_SAMPLER_FILTERS          0x00007e00ul
#define R6_SAMPLER_MC_TRUNCATE      0x00001000ul

/* The SPI: SEMANTIC_n a byte each in SPI_VS_OUT_ID_0; input n as the
 * default state sets it, DEFAULT_VAL(1) | SEL_CENTROID, and SEL_LINEAR
 * for linear interpolation; PERSP_ and LINEAR_GRADIENT_ENA. */
#define R6_SPI_SEMANTIC_BITS        8ul
#define R6_SPI_PS_INPUT_DEFAULT     0x00000900ul
#define R6_SPI_PS_INPUT_LINEAR      0x00001000ul
#define R6_SPI_PERSP_GRADIENT_ENA   0x10000000ul
#define R6_SPI_LINEAR_GRADIENT_ENA  0x20000000ul
#define R6_SPI_EXPORT_COUNT_SHIFT   1
#define R6_SPI_PS_IN_CONTROL_DWORDS 3ul
/* PA_CL_VTE_CNTL VTX_XY_FMT | VTX_Z_FMT: x, y and z already in window
 * space, w as given (Evergreen's measured value). PA_SU_VTX_CNTL:
 * ROUND_MODE(2) | QUANT_MODE(5), xf86's, and PIX_CENTER for centres at
 * +0.5. */
#define R6_VTE_XY_Z_WINDOW          0x00000300ul
#define R6_VTX_CNTL_ROUND           0x0000002cul
#define R6_VTX_CNTL_CENTRE_HALF     0x00000001ul

/* DB_SHADER_CONTROL: DUAL_EXPORT_ENABLE as xf86's default state; Z_ORDER
 * EARLY_Z_THEN_LATE_Z (1 << 4), or LATE_Z (0) when the PS may kill or
 * alpha test is on, as Mesa's r600_update_db_shader_control ("RE_Z ...
 * seems to cause lockup on r6xx/r7xx"); KILL_ENABLE. */
#define R6_DB_SHADER_DUAL_EXPORT    0x00000200ul
#define R6_DB_SHADER_EARLY_Z        0x00000010ul
#define R6_DB_SHADER_KILL           0x00000040ul
/* DB_RENDER_OVERRIDE: FORCE_HIZ_ENABLE, FORCE_HIS_ENABLE0 and 1 all
 * FORCE_DISABLE (2), as Mesa's r600_emit_db_misc_state sets them without
 * HTILE, and xf86's FORCE_SHADER_Z_ORDER kept. */
#define R6_DB_RENDER_OVERRIDE_VALUE 0x0000006aul
/* DB_DEPTH_INFO: FORMAT DEPTH_16, ARRAY_MODE 18:15 (R6xx's place for it;
 * Evergreen's DB_Z_INFO has it at 7:4). DB_DEPTH_SIZE: PITCH_TILE_MAX
 * 9:0, SLICE_TILE_MAX 29:10. SURFACE_BASE_UPDATE's DEPTH_BASE (xf86's
 * r600_reg.h, Mesa's SURFACE_BASE_UPDATE_DEPTH). */
#define R6_DB_FORMAT_DEPTH_16       1ul
#define R6_DB_ARRAY_MODE_SHIFT      15
#define R6_ARRAY_1D_TILED_THIN1     2ul
#define R6_DB_SLICE_SHIFT           10
#define R6_SURFACE_BASE_DEPTH       0x00000001ul
#define R6_DB_DEPTH_DWORDS          2ul
/* PA_SU_POLY_OFFSET_DB_FMT_CNTL (xf86 r600_reg_auto_r6xx.h, Mesa
 * R_028DF8): POLY_OFFSET_NEG_NUM_DB_BITS 7:0, (char)-16 for Z16. */
#define R6_PA_SU_POLY_OFFSET_DB_FMT_CNTL 0x28df8ul
#define R6_DB_BITS_Z16              0x000000f0ul

/* SQ_TEX_RESOURCE_WORD0: DIM 2:0, TILE_MODE 6:3, PITCH 18:8 (8-texel
 * units less one), TEX_WIDTH 31:19; WORD1 TEX_HEIGHT 12:0, DATA_FORMAT
 * 31:26; WORD4 REQUEST_SIZE(1) and DST_SEL Z, Y, X, W (xf86's swizzle for
 * a 32-bit source: R from byte 2, G 1, B 0, A 3); WORD6 TYPE. */
#define R6_TEX_DIM_2D               1ul
#define R6_TEX_TILE_MODE_SHIFT      3
#define R6_ARRAY_LINEAR_ALIGNED     1ul
#define R6_TEX_PITCH_SHIFT          8
#define R6_TEX_WIDTH_SHIFT          19
#define R6_TEX_FORMAT_SHIFT         26
#define R6_TEX_REQUEST_SIZE_1       0x00004000ul
/* SQ_SEL_Z << 16 | SQ_SEL_Y << 19 | SQ_SEL_X << 22 | SQ_SEL_W << 25 */
#define R6_TEX_SWIZZLE_ARGB         0x060a0000ul
/* d3d_order's swizzles, Evergreen's (eg_3d.c): X, Y, Z, W as stored, and
 * for a 565 texture W the constant 1 (SQ_SEL_1, 5), which has no fourth
 * field and would otherwise sample alpha 0. */
#define R6_TEX_SWIZZLE_XYZW         ((0ul << 16) | (1ul << 19) | \
                                     (2ul << 22) | (3ul << 25))
#define R6_TEX_SWIZZLE_XYZ1         ((0ul << 16) | (1ul << 19) | \
                                     (2ul << 22) | (5ul << 25))
#define R6_TEX_VALID_TEXTURE        0x80000000ul /* TYPE(2) << 30 */
#define R6_TEX_RESOURCE_DWORDS      7ul
#define R6_TEX_SAMPLER_DWORDS       3ul

#define R6_DI_PT_TRILIST            0x04ul
#define R6_CB_WRITE_RGB             0x00000007ul
#define R6_TEXEL_BYTES_8888         4ul
#define R6_TILE_EDGE                8ul
#define R6_TILE_PIXELS              64ul

v9x_u32 v9x_r6_3d_depth_control(v9x_u32 enable, v9x_u32 write, v9x_u32 func)
{
    return (enable != 0ul ? R6_DEPTH_Z_ENABLE : 0ul) |
           (write != 0ul ? R6_DEPTH_Z_WRITE : 0ul) |
           ((func & R6_FUNC_MASK) << R6_DEPTH_ZFUNC_SHIFT);
}

/* One pair of factors for colour and alpha, added. */
v9x_u32 v9x_r6_3d_blend(v9x_u32 enable, v9x_u32 src, v9x_u32 dst)
{
    if (enable == 0ul) {
        src = V9X_R6_BLEND_ONE;
        dst = V9X_R6_BLEND_ZERO;
    }
    src &= R6_BLEND_FACTOR_MASK;
    dst &= R6_BLEND_FACTOR_MASK;
    return src | (dst << R6_BLEND_COLOR_DST_SHIFT) |
           (src << R6_BLEND_ALPHA_SRC_SHIFT) |
           (dst << R6_BLEND_ALPHA_DST_SHIFT);
}

v9x_u32 v9x_r6_3d_alpha_test(v9x_u32 enable, v9x_u32 func)
{
    return (func & R6_FUNC_MASK) |
           (enable != 0ul ? R6_ALPHA_TEST_ENABLE : 0ul);
}

v9x_u32 v9x_r6_3d_sampler(v9x_u32 mag_bilinear, v9x_u32 min_bilinear,
                          v9x_u32 clamp_u, v9x_u32 clamp_v)
{
    return (clamp_u & R6_SAMPLER_CLAMP_MASK) |
           ((clamp_v & R6_SAMPLER_CLAMP_MASK) << R6_SAMPLER_CLAMP_Y_SHIFT) |
           ((mag_bilinear != 0ul ? 1ul : 0ul) << R6_SAMPLER_MAG_SHIFT) |
           ((min_bilinear != 0ul ? 1ul : 0ul) << R6_SAMPLER_MIN_SHIFT);
}

v9x_u32 v9x_r6_3d_texel_bytes(v9x_u32 format)
{
    switch (format) {
    case V9X_R6_COLOR_5_6_5:
    case V9X_R6_COLOR_1_5_5_5:
    case V9X_R6_COLOR_4_4_4_4:
        return 2ul;
    case V9X_R6_COLOR_8_8_8_8:
        return 4ul;
    }
    return 0ul;
}

v9x_u32 v9x_r6_3d_texture_pitch(v9x_u32 format, v9x_u32 width)
{
    v9x_u32 bytes = v9x_r6_3d_texel_bytes(format);
    v9x_u32 align;

    if (bytes == 0ul || width == 0ul) {
        return 0ul;
    }
    align = V9X_R6_GROUP_BYTES / bytes;
    if (align < V9X_R6_TEXTURE_PITCH_ALIGN) {
        align = V9X_R6_TEXTURE_PITCH_ALIGN;
    }
    return (width + align - 1ul) & ~(align - 1ul);
}

/* The formats a state's order allows: ARGB8888 alone in xf86's, and in
 * d3d_order RGB565 too for a target, every V9X_R6_COLOR_* for a texture. */
static int v9x_r6_3d_target_format_ok(const struct v9x_r6_3d_state *state)
{
    return state->target_format == V9X_R6_COLOR_8_8_8_8 ||
           (state->d3d_order != 0ul &&
            state->target_format == V9X_R6_COLOR_5_6_5);
}

static int v9x_r6_3d_texture_format_ok(const struct v9x_r6_3d_state *state)
{
    return state->texture_format == V9X_R6_COLOR_8_8_8_8 ||
           (state->d3d_order != 0ul &&
            v9x_r6_3d_texel_bytes(state->texture_format) != 0ul);
}

static int v9x_r6_3d_state_valid(const struct v9x_r6_3d_state *state)
{
    if (!v9x_r6_3d_aligned(state->target_gpu) ||
        state->target_pitch == 0ul || (state->target_pitch & 7ul) != 0ul ||
        state->target_pitch > V9X_R6_3D_PITCH_MAX ||
        state->target_height == 0ul ||
        state->target_height > V9X_R6_3D_PITCH_MAX ||
        !v9x_r6_3d_target_format_ok(state)) {
        return 0;
    }
    if (state->scissor_left >= state->scissor_right ||
        state->scissor_top >= state->scissor_bottom ||
        state->scissor_right > state->target_pitch ||
        state->scissor_bottom > state->target_height) {
        return 0;
    }
    if (state->has_depth != 0ul &&
        (!v9x_r6_3d_aligned(state->depth_gpu) || state->depth_pitch == 0ul ||
         (state->depth_pitch % V9X_R6_DEPTH_PITCH_ALIGN) != 0ul ||
         state->depth_height == 0ul ||
         (state->depth_height % R6_TILE_EDGE) != 0ul ||
         state->depth_pitch > V9X_R6_3D_PITCH_MAX ||
         state->depth_height > V9X_R6_3D_PITCH_MAX ||
         state->depth_pitch < state->scissor_right ||
         state->depth_height < state->scissor_bottom)) {
        return 0;
    }
    if (!v9x_r6_3d_aligned(state->vs_gpu) ||
        !v9x_r6_3d_aligned(state->ps_gpu) ||
        state->vs_gprs == 0ul || state->ps_gprs == 0ul ||
        state->interpolants == 0ul ||
        state->interpolants > V9X_R6_INTERPOLANTS_MAX) {
        return 0;
    }
    if (state->has_texture != 0ul &&
        (!v9x_r6_3d_aligned(state->texture_gpu) ||
         state->texture_width == 0ul || state->texture_height == 0ul ||
         !v9x_r6_3d_texture_format_ok(state) ||
         state->texture_pitch < state->texture_width ||
         v9x_r6_3d_texture_pitch(state->texture_format,
                                 state->texture_pitch) !=
             state->texture_pitch ||
         state->texture_pitch > V9X_R6_3D_PITCH_MAX ||
         state->texture_height > V9X_R6_3D_PITCH_MAX)) {
        return 0;
    }
    return state->vertex_stride != 0ul &&
           state->vertex_bytes >= state->vertex_stride;
}

/* r600_set_render_target for a linear general target, as v9x_r6_3d_solid
 * writes it (measured, M-NZHWBR, ARGB8888): the base and the rv6xx
 * SURFACE_BASE_UPDATE, CMASK and FMASK on the target, the size with the
 * height rounded up to 8, the format with COMP_SWAP 1 (xf86's order) or
 * SWAP_STD (d3d_order, Evergreen's), BLEND_CLAMP and SOURCE_FORMAT. */
static void v9x_r6_3d_render_target(struct v9x_r6_pm4 *stream,
                                    const struct v9x_r6_3d_state *state)
{
    v9x_u32 gpu = state->target_gpu;
    v9x_u32 pitch = state->target_pitch;
    v9x_u32 height = state->target_height;
    v9x_u32 height8 = (height + R6_TILE_EDGE - 1ul) & ~(R6_TILE_EDGE - 1ul);

    v9x_r6_pm4_set1(stream, R6_CB_COLOR0_BASE, gpu >> 8);
    v9x_r6_pm4_put(stream,
                   v9x_r6_pm4_header(V9X_R6_P3_SURFACE_BASE_UPDATE, 1ul));
    v9x_r6_pm4_put(stream, R6_SURFACE_BASE_COLOR0);
    v9x_r6_pm4_set1(stream, R6_CB_COLOR0_TILE, gpu >> 8);
    v9x_r6_pm4_set1(stream, R6_CB_COLOR0_FRAG, gpu >> 8);
    v9x_r6_pm4_set1(stream, R6_CB_COLOR0_SIZE,
                    ((pitch >> 3) - 1ul) |
                    (((pitch * height8) / R6_CB_TILE_PIXELS - 1ul) <<
                     R6_CB_SLICE_SHIFT));
    v9x_r6_pm4_set1(stream, R6_CB_COLOR0_VIEW, 0ul);
    v9x_r6_pm4_set1(stream, R6_CB_COLOR0_MASK, 0ul);
    v9x_r6_pm4_set1(stream, R6_CB_COLOR0_INFO,
                    (state->target_format << R6_CB_FORMAT_SHIFT) |
                    (R6_CB_ARRAY_LINEAR_GENERAL << R6_CB_ARRAY_MODE_SHIFT) |
                    (state->d3d_order != 0ul ? 0ul : R6_CB_COMP_SWAP_ARGB) |
                    R6_CB_BLEND_CLAMP | R6_CB_SOURCE_FORMAT);
}

/*
 * A read cache invalidated before the batch reads what the CPU wrote:
 * over all of memory (CP_COHER_SIZE all ones, base 0), as Mesa's
 * r600_flush_emit and Linux's r600_fence_ring_emit sync, or with
 * ranged_syncs over size bytes at gpu, as xf86's r600_set_vtx_resource
 * and r600_set_tex_resource do. M-NZHWBR's /modulate drew the previous
 * run's Gouraud colours from the same vertex address after xf86's ranged
 * VC sync (V9XR63D runs c and d). The whole-memory TC and VC sync that
 * ends every run (v9x_r6_3d_flush) had not cleared those lines either, so
 * this form is Mesa's and Linux's, not yet a measured cure.
 */
static void v9x_r6_3d_cache_sync(struct v9x_r6_pm4 *stream,
                                 const struct v9x_r6_3d_state *state,
                                 v9x_u32 actions, v9x_u32 size, v9x_u32 gpu)
{
    if (state->ranged_syncs != 0ul) {
        v9x_r6_pm4_surface_sync(stream, actions, size, gpu);
        return;
    }
    v9x_r6_pm4_surface_sync(stream, actions, V9X_R6_SYNC_ALL, 0ul);
}

v9x_status v9x_r6_3d_build_state(const struct v9x_r6_3d_state *state,
                                 struct v9x_r6_pm4 *stream)
{
    v9x_u32 values[R6_TEX_RESOURCE_DWORDS];
    v9x_u32 index;
    v9x_u32 ids;
    v9x_u32 input;
    v9x_u32 control;
    v9x_u32 tl;
    v9x_u32 br;

    if (state == 0 || stream == 0 || !v9x_r6_3d_state_valid(state)) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    /* r600_vs_setup and r600_ps_setup, as the solid draw: the shader
     * cache flushed over each shader, NUM_GPRS, the PS with
     * UNCACHED_FIRST_INST and one colour exported. */
    v9x_r6_pm4_surface_sync(stream, V9X_R6_SYNC_SH_ACTION,
                            V9X_R6_3D_SHADER_SYNC_BYTES, state->vs_gpu);
    v9x_r6_pm4_set1(stream, R6_SQ_PGM_START_VS, state->vs_gpu >> 8);
    v9x_r6_pm4_set1(stream, R6_SQ_PGM_RESOURCES_VS, state->vs_gprs);
    v9x_r6_pm4_set1(stream, R6_SQ_PGM_CF_OFFSET_VS, 0ul);
    v9x_r6_pm4_surface_sync(stream, V9X_R6_SYNC_SH_ACTION,
                            V9X_R6_3D_SHADER_SYNC_BYTES, state->ps_gpu);
    v9x_r6_pm4_set1(stream, R6_SQ_PGM_START_PS, state->ps_gpu >> 8);
    v9x_r6_pm4_set1(stream, R6_SQ_PGM_RESOURCES_PS,
                    state->ps_gprs | R6_PGM_UNCACHED_FIRST_INST);
    v9x_r6_pm4_set1(stream, R6_SQ_PGM_EXPORTS_PS, R6_PS_EXPORTS_COLOR);
    v9x_r6_pm4_set1(stream, R6_SQ_PGM_CF_OFFSET_PS, 0ul);

    /* The interpolants: VS parameter n carries semantic n, PS input n
     * reads it into GPR n. r600_set_spi writes NUM_INTERP alone; the
     * gradient enable is Mesa's (r600_update_ps_state sets
     * PERSP_GRADIENT_ENA always, LINEAR_ for a linear input), both
     * documented R6xx fields, so a choice between them is not left to a
     * reset value. */
    ids = 0ul;
    for (index = 0ul; index < state->interpolants; ++index) {
        ids |= index << (R6_SPI_SEMANTIC_BITS * index);
        input = index | R6_SPI_PS_INPUT_DEFAULT |
                (state->perspective != 0ul ? 0ul : R6_SPI_PS_INPUT_LINEAR);
        v9x_r6_pm4_set1(stream, R6_SPI_PS_INPUT_CNTL_0 + 4ul * index, input);
    }
    v9x_r6_pm4_set1(stream, R6_SPI_VS_OUT_ID_0, ids);
    v9x_r6_pm4_set1(stream, R6_SPI_VS_OUT_CONFIG,
                    (state->interpolants - 1ul) << R6_SPI_EXPORT_COUNT_SHIFT);
    values[0] = state->interpolants |
                (state->perspective != 0ul ? R6_SPI_PERSP_GRADIENT_ENA
                                           : R6_SPI_LINEAR_GRADIENT_ENA);
    values[1] = 0ul;
    values[2] = 0ul;
    v9x_r6_pm4_set(stream, R6_SPI_PS_IN_CONTROL_0, values,
                   R6_SPI_PS_IN_CONTROL_DWORDS);
    v9x_r6_pm4_set1(stream, R6_PA_CL_VTE_CNTL, R6_VTE_XY_Z_WINDOW);
    v9x_r6_pm4_set1(stream, R6_PA_SU_VTX_CNTL,
                    R6_VTX_CNTL_ROUND |
                    (state->pixel_centre_half != 0ul ?
                     R6_VTX_CNTL_CENTRE_HALF : 0ul));

    /* r600_set_tex_resource and r600_set_tex_sampler as R600PrepareCopy
     * fills them for an untiled 32-bit source: the texture cache flushed
     * (over all of memory, or over the texture with ranged_syncs), linear
     * aligned, one level, xf86's ARGB swizzle; the sampler with
     * MC_COORD_TRUNCATE for point filtering only. */
    if (state->has_texture != 0ul) {
        v9x_r6_3d_cache_sync(stream, state, V9X_R6_SYNC_TC_ACTION,
                             state->texture_pitch * state->texture_height *
                             v9x_r6_3d_texel_bytes(state->texture_format),
                             state->texture_gpu);
        values[0] = R6_TEX_DIM_2D |
                    (R6_ARRAY_LINEAR_ALIGNED << R6_TEX_TILE_MODE_SHIFT) |
                    (((state->texture_pitch >> 3) - 1ul) <<
                     R6_TEX_PITCH_SHIFT) |
                    ((state->texture_width - 1ul) << R6_TEX_WIDTH_SHIFT);
        values[1] = (state->texture_height - 1ul) |
                    (state->texture_format << R6_TEX_FORMAT_SHIFT);
        values[2] = state->texture_gpu >> 8;
        values[3] = state->texture_gpu >> 8;
        if (state->d3d_order == 0ul) {
            values[4] = R6_TEX_REQUEST_SIZE_1 | R6_TEX_SWIZZLE_ARGB;
        } else if (state->texture_format == V9X_R6_COLOR_5_6_5 ||
                   state->texture_opaque != 0ul) {
            values[4] = R6_TEX_REQUEST_SIZE_1 | R6_TEX_SWIZZLE_XYZ1;
        } else {
            values[4] = R6_TEX_REQUEST_SIZE_1 | R6_TEX_SWIZZLE_XYZW;
        }
        values[5] = 0ul;
        values[6] = R6_TEX_VALID_TEXTURE;
        v9x_r6_pm4_set(stream, R6_SQ_TEX_RESOURCE_PS_0, values,
                       R6_TEX_RESOURCE_DWORDS);
        values[0] = state->sampler_word0;
        values[1] = 0ul;
        values[2] = (state->sampler_word0 & R6_SAMPLER_FILTERS) == 0ul ?
                    R6_SAMPLER_MC_TRUNCATE : 0ul;
        v9x_r6_pm4_set(stream, R6_SQ_TEX_SAMPLER_PS_0, values,
                       R6_TEX_SAMPLER_DWORDS);
    }

    /* The colour buffer, its write mask, ROP copy with target 0's
     * blending on or off, and the factors. */
    v9x_r6_3d_render_target(stream, state);
    v9x_r6_pm4_set1(stream, R6_CB_TARGET_MASK,
                    (state->write_alpha != 0ul ? R6_CB_WRITE_ARGB
                                               : R6_CB_WRITE_RGB) &
                    ~state->masked_channels);
    v9x_r6_pm4_set1(stream, R6_CB_COLOR_CONTROL,
                    R6_ROP3_COPY |
                    (state->blend_enable != 0ul ?
                     R6_CB_PER_MRT_BLEND | R6_CB_TARGET0_BLEND : 0ul));
    v9x_r6_pm4_set1(stream, R6_CB_BLEND0_CONTROL, state->blend_control);

    /* The DB: shader order, HiZ and HiS off, the depth buffer or none. */
    control = R6_DB_SHADER_DUAL_EXPORT;
    if (state->kill != 0ul) {
        control |= R6_DB_SHADER_KILL;
    } else if ((state->alpha_test_control & R6_ALPHA_TEST_ENABLE) == 0ul) {
        control |= R6_DB_SHADER_EARLY_Z;
    }
    v9x_r6_pm4_set1(stream, R6_DB_SHADER_CONTROL, control);
    v9x_r6_pm4_set1(stream, R6_DB_RENDER_OVERRIDE,
                    R6_DB_RENDER_OVERRIDE_VALUE);
    /* The DB context registers Mesa clears and xf86's default state never
     * writes: DB_HTILE_SURFACE (r600_emit_db_state without HTILE), and
     * DB_SRESULTS_COMPARE_STATE0 and 1 and DB_PRELOAD_CONTROL (Mesa's
     * initial context). Left as the card had them, a stencil-results
     * compare enable or an HTILE preload could reject fragments; V9XR63D
     * /depth's first run on M-NZHWBR drew nothing and wrote no depth. */
    v9x_r6_pm4_set1(stream, R6_DB_HTILE_SURFACE, 0ul);
    v9x_r6_pm4_set(stream, R6_DB_SRESULTS_COMPARE_STATE0, v9x_r6_zeros,
                   R6_DB_SRESULTS_DWORDS);
    if (state->has_depth != 0ul) {
        /* Mesa r600_emit_framebuffer_state's order: DB_DEPTH_SIZE and
         * _VIEW, DB_DEPTH_BASE and _INFO, DB_PREFETCH_LIMIT (height in
         * tiles less one), then SURFACE_BASE_UPDATE, which Mesa sends for
         * the depth base on every R6xx after the R600 as xf86 sends it for
         * the colour base. */
        values[0] = ((state->depth_pitch >> 3) - 1ul) |
                    (((state->depth_pitch * state->depth_height) /
                      R6_TILE_PIXELS - 1ul) << R6_DB_SLICE_SHIFT);
        values[1] = 0ul;
        v9x_r6_pm4_set(stream, R6_DB_DEPTH_SIZE, values, R6_DB_DEPTH_DWORDS);
        values[0] = state->depth_gpu >> 8;
        values[1] = R6_DB_FORMAT_DEPTH_16 |
                    (R6_ARRAY_1D_TILED_THIN1 << R6_DB_ARRAY_MODE_SHIFT);
        v9x_r6_pm4_set(stream, R6_DB_DEPTH_BASE, values, R6_DB_DEPTH_DWORDS);
        v9x_r6_pm4_set1(stream, R6_DB_PREFETCH_LIMIT,
                        state->depth_height / R6_TILE_EDGE - 1ul);
        v9x_r6_pm4_put(stream,
                       v9x_r6_pm4_header(V9X_R6_P3_SURFACE_BASE_UPDATE, 1ul));
        v9x_r6_pm4_put(stream, R6_SURFACE_BASE_DEPTH);
        /* The depth format's bits for the rasteriser, as Mesa's
         * r600_emit_polygon_offset sets them for Z16 (NEG_NUM_DB_BITS
         * -16); the default state left 0. Not shown to matter: every
         * fragment's Z reached the DB as 1.0 without it (V9XR63D runs c-f)
         * and this is one of the candidates. */
        v9x_r6_pm4_set1(stream, R6_PA_SU_POLY_OFFSET_DB_FMT_CNTL,
                        R6_DB_BITS_Z16);
    } else {
        v9x_r6_pm4_set1(stream, R6_DB_DEPTH_INFO, 0ul);
    }
    v9x_r6_pm4_set1(stream, R6_DB_DEPTH_CONTROL,
                    state->has_depth != 0ul ? state->depth_control : 0ul);
    /* The viewport's Z range, 0 to 1: the DB clamps Z to it, and neither
     * xf86's default state nor this one wrote it before. */
    values[0] = 0ul;
    values[1] = R6_FLOAT_ONE;
    v9x_r6_pm4_set(stream, R6_PA_SC_VPORT_ZMIN_0, values, 2ul);
    v9x_r6_pm4_set1(stream, R6_SX_ALPHA_TEST_CONTROL,
                    state->alpha_test_control);
    v9x_r6_pm4_set1(stream, R6_SX_ALPHA_REF, state->alpha_ref);

    /* The screen scissor at the target, the generic and window scissors at
     * the draw's rectangle. */
    v9x_r6_3d_scissor(stream, R6_PA_SC_SCREEN_SCISSOR_TL, 0ul,
                      state->target_pitch | (state->target_height << 16));
    tl = state->scissor_left | (state->scissor_top << 16) |
         R6_WINDOW_OFFSET_DISABLE;
    br = state->scissor_right | (state->scissor_bottom << 16);
    v9x_r6_3d_scissor(stream, R6_PA_SC_GENERIC_SCISSOR_TL, tl, br);
    v9x_r6_3d_scissor(stream, R6_PA_SC_WINDOW_SCISSOR_TL, tl, br);

    /* r600_set_vtx_resource: the vertex cache (with one, the texture
     * cache too, as Mesa's r600_flush_emit and Linux's r600_fence_ring_emit
     * invalidate both) synced, then the VS's resource 0. */
    control = V9X_R6_SYNC_TC_ACTION;
    if (state->vertex_cache != 0ul) {
        control = state->ranged_syncs != 0ul ? V9X_R6_SYNC_VC_ACTION :
                  V9X_R6_SYNC_VC_ACTION | V9X_R6_SYNC_TC_ACTION;
    }
    v9x_r6_3d_cache_sync(stream, state, control, state->vertex_bytes,
                         state->vertex_gpu);
    values[0] = state->vertex_gpu;
    values[1] = state->vertex_bytes - 1ul;
    values[2] = state->vertex_stride << R6_VTX_STRIDE_SHIFT;
    values[3] = R6_VTX_MEM_REQUEST_SIZE_1;
    values[4] = 0ul;
    values[5] = 0ul;
    values[6] = R6_VTX_VALID_BUFFER;
    v9x_r6_pm4_set(stream, R6_SQ_VTX_RESOURCE_VS_0, values, 7ul);
    return stream->overflow != 0ul ? V9X_STATUS_INSUFFICIENT_MEMORY
                                   : V9X_STATUS_OK;
}

void v9x_r6_3d_draw(struct v9x_r6_pm4 *stream, v9x_u32 vertex_count)
{
    v9x_r6_pm4_set1(stream, V9X_R6_VGT_PRIMITIVE_TYPE, R6_DI_PT_TRILIST);
    v9x_r6_pm4_put(stream, v9x_r6_pm4_header(V9X_R6_P3_INDEX_TYPE, 1ul));
    v9x_r6_pm4_put(stream, R6_DI_INDEX_SIZE_16_BIT);
    v9x_r6_pm4_put(stream, v9x_r6_pm4_header(V9X_R6_P3_NUM_INSTANCES, 1ul));
    v9x_r6_pm4_put(stream, 1ul);
    v9x_r6_pm4_put(stream, v9x_r6_pm4_header(V9X_R6_P3_DRAW_INDEX_AUTO, 2ul));
    v9x_r6_pm4_put(stream, vertex_count);
    v9x_r6_pm4_put(stream, R6_DI_SRC_SEL_AUTO_INDEX);
}

void v9x_r6_3d_flush(struct v9x_r6_pm4 *stream)
{
    v9x_r6_pm4_put(stream, v9x_r6_pm4_header(V9X_R6_P3_EVENT_WRITE, 1ul));
    v9x_r6_pm4_put(stream, R6_CACHE_FLUSH_AND_INV);
    v9x_r6_pm4_set1(stream, V9X_R6_WAIT_UNTIL, R6_WAIT_3D_IDLE_CLEAN);
    v9x_r6_pm4_surface_sync(stream,
                            V9X_R6_SYNC_CB_ACTION | V9X_R6_SYNC_CB0_DEST_BASE |
                            V9X_R6_SYNC_TC_ACTION | V9X_R6_SYNC_VC_ACTION,
                            V9X_R6_SYNC_ALL, 0ul);
}
