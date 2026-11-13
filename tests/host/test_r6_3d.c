#include <stdio.h>
#include <string.h>

#include "velocity9x/r6_3d.h"
#include "velocity9x/r6_shader.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

#define BUFFER_DWORDS 2048u
static v9x_u32 buffer[BUFFER_DWORDS];

/* xf86-video-ati 6.14.6 r600_state.h PACK3: RADEON_CP_PACKET3 | cmd << 8 |
 * ((num - 1) & 3FFFh) << 16. */
#define XF86_PACK3(cmd, num) \
    (0xc0000000ul | ((v9x_u32)(cmd) << 8) | \
     ((((v9x_u32)(num) - 1ul) & 0x3ffful) << 16))

static v9x_u32 op_of(v9x_u32 header)
{
    return (header >> 8) & 0xfful;
}

static v9x_u32 body_of(v9x_u32 header)
{
    return ((header >> 16) & 0x3ffful) + 1ul;
}

/* Every dword a type-3 header whose body lies inside the stream, or a
 * type-2 pad. */
static int walks(const v9x_u32 *dwords, v9x_u32 count)
{
    v9x_u32 at = 0ul;

    while (at < count) {
        if (dwords[at] == V9X_R6_PACKET2) {
            ++at;
            continue;
        }
        if ((dwords[at] >> 30) != 3ul) {
            return 0;
        }
        at += 1ul + body_of(dwords[at]);
    }
    return at == count;
}

/* r600_reg.h's range start for a SET_* opcode, or all ones. */
static v9x_u32 range_start(v9x_u32 op)
{
    switch (op) {
    case V9X_R6_P3_SET_CONFIG_REG:  return V9X_R6_SET_CONFIG_START;
    case V9X_R6_P3_SET_CONTEXT_REG: return V9X_R6_SET_CONTEXT_START;
    case V9X_R6_P3_SET_ALU_CONST:   return V9X_R6_SET_ALU_CONST_START;
    case V9X_R6_P3_SET_RESOURCE:    return V9X_R6_SET_RESOURCE_START;
    case V9X_R6_P3_SET_SAMPLER:     return V9X_R6_SET_SAMPLER_START;
    case V9X_R6_P3_SET_CTL_CONST:   return V9X_R6_SET_CTL_CONST_START;
    case V9X_R6_P3_SET_LOOP_CONST:  return V9X_R6_SET_LOOP_CONST_START;
    case V9X_R6_P3_SET_BOOL_CONST:  return V9X_R6_SET_BOOL_CONST_START;
    default:                        return 0xfffffffful;
    }
}

/* The dword index of the nth (from 0) value written to reg by a SET_*
 * packet, or count. */
static v9x_u32 find_nth(const v9x_u32 *dwords, v9x_u32 count, v9x_u32 reg,
                        v9x_u32 nth)
{
    v9x_u32 at = 0ul;
    v9x_u32 start;
    v9x_u32 first;
    v9x_u32 values;

    while (at < count) {
        if (dwords[at] == V9X_R6_PACKET2) {
            ++at;
            continue;
        }
        start = range_start(op_of(dwords[at]));
        if (start != 0xfffffffful) {
            first = start + dwords[at + 1ul] * 4ul;
            values = body_of(dwords[at]) - 1ul;
            if (reg >= first && reg < first + values * 4ul) {
                if (nth == 0ul) {
                    return at + 2ul + (reg - first) / 4ul;
                }
                --nth;
            }
        }
        at += 1ul + body_of(dwords[at]);
    }
    return count;
}

static v9x_u32 find_reg(const v9x_u32 *dwords, v9x_u32 count, v9x_u32 reg)
{
    return find_nth(dwords, count, reg, 0ul);
}

/* The first packet of opcode op at or after from, or count. */
static v9x_u32 find_op(const v9x_u32 *dwords, v9x_u32 count, v9x_u32 op,
                       v9x_u32 from)
{
    v9x_u32 at = 0ul;

    while (at < count) {
        if (dwords[at] == V9X_R6_PACKET2) {
            ++at;
            continue;
        }
        if (at >= from && op_of(dwords[at]) == op) {
            return at;
        }
        at += 1ul + body_of(dwords[at]);
    }
    return count;
}

static v9x_u32 float_of(float value)
{
    v9x_u32 bits;

    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static void test_packets(void)
{
    struct v9x_r6_pm4 stream;
    v9x_u32 values[4];

    /* r600_state.h PACK3, and r600d.h PACKET3(op, n) with n the body
     * less one. */
    CHECK(v9x_r6_pm4_header(V9X_R6_P3_SET_CONFIG_REG, 2ul) ==
          XF86_PACK3(0x68, 2));
    CHECK(v9x_r6_pm4_header(V9X_R6_P3_SET_CONFIG_REG, 2ul) == 0xc0016800ul);
    CHECK(v9x_r6_pm4_header(V9X_R6_P3_ME_INITIALIZE, 6ul) == 0xc0054400ul);

    /* r600_state.h PACK0: SET_CONFIG_REG at (reg - 8000h) >> 2, as
     * r600_ring_test writes SCRATCH_REG0 (r600.c). */
    v9x_r6_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    v9x_r6_pm4_set1(&stream, V9X_R6_SCRATCH_REG0, 0xdeadbeeful);
    CHECK(stream.overflow == 0ul && stream.count == 3ul);
    CHECK(buffer[0] == 0xc0016800ul && buffer[1] == 0x140ul &&
          buffer[2] == 0xdeadbeeful);

    /* Each range: a context register (CB_COLOR0_BASE 28040h), the PS's
     * ALU constant 0 (SQ_ALU_CONSTANT0_0 30000h), the VS's first vertex
     * resource (SQ_VTX_CONSTANT_WORD0_0 + 160 * 28 = 39180h), and
     * SQ_VTX_BASE_VTX_LOC (3CFF0h) through SET_CTL_CONST, r600_reg.h's
     * SET_*_offset values. */
    v9x_r6_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    v9x_r6_pm4_set1(&stream, 0x28040ul, 1ul);
    CHECK(buffer[0] == XF86_PACK3(0x69, 2) && buffer[1] == 0x10ul);
    v9x_r6_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    memset(values, 0, sizeof(values));
    v9x_r6_pm4_set(&stream, 0x30000ul, values, 4ul);
    CHECK(buffer[0] == XF86_PACK3(0x6a, 5) && buffer[1] == 0ul &&
          stream.count == 6ul);
    v9x_r6_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    v9x_r6_pm4_set(&stream, 0x39180ul, values, 4ul);
    CHECK(buffer[0] == XF86_PACK3(0x6d, 5) && buffer[1] == 0x460ul);
    v9x_r6_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    v9x_r6_pm4_set(&stream, 0x3cff0ul, values, 2ul);
    CHECK(buffer[0] == XF86_PACK3(0x6f, 3) && buffer[1] == 0ul);

    /* Outside every range (RAMCFG, an MMIO-only register), misaligned, a
     * run past its range's end, or no values: nothing written. */
    v9x_r6_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    v9x_r6_pm4_set1(&stream, V9X_R6_RAMCFG, 0ul);
    CHECK(stream.overflow != 0ul && stream.count == 0ul);
    v9x_r6_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    v9x_r6_pm4_set1(&stream, 0x8502ul, 0ul);
    CHECK(stream.overflow != 0ul && stream.count == 0ul);
    v9x_r6_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    v9x_r6_pm4_set(&stream, 0xabfcul, values, 2ul);
    CHECK(stream.overflow != 0ul && stream.count == 0ul);
    v9x_r6_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    v9x_r6_pm4_set(&stream, 0x8500ul, values, 0ul);
    CHECK(stream.overflow != 0ul && stream.count == 0ul);

    /* A full stream writes nothing past capacity. */
    v9x_r6_pm4_begin(&stream, buffer, 2ul);
    v9x_r6_pm4_set1(&stream, V9X_R6_SCRATCH_REG0, 0ul);
    CHECK(stream.overflow != 0ul && stream.count == 2ul);

    /* r600_cp_set_surface_sync: sync type, (size + 255) >> 8, mc_addr >> 8,
     * 10; size FFFFFFFFh stays all ones. */
    v9x_r6_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    v9x_r6_pm4_surface_sync(&stream, V9X_R6_SYNC_SH_ACTION, 512ul,
                            0xef105000ul);
    CHECK(buffer[0] == XF86_PACK3(0x43, 4) && buffer[1] == 0x08000000ul &&
          buffer[2] == 2ul && buffer[3] == 0x00ef1050ul && buffer[4] == 10ul);
    v9x_r6_pm4_surface_sync(&stream, V9X_R6_SYNC_TC_ACTION, V9X_R6_SYNC_ALL,
                            0ul);
    CHECK(buffer[7] == 0xfffffffful);

    /* Type-2 padding (R600CPFlushIndirect: "fill up to multiple of 16
     * dwords" with CP_PACKET2, 80000000h). */
    v9x_r6_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    v9x_r6_pm4_set1(&stream, V9X_R6_SCRATCH_REG0, 0ul);
    v9x_r6_pm4_pad(&stream);
    CHECK(stream.count == 16ul && buffer[3] == 0x80000000ul &&
          buffer[15] == 0x80000000ul);

    /* r600_cp_start: PACKET3(ME_INITIALIZE, 5), 1, 3, max_hw_contexts - 1
     * (8 on the RV630, r600_gpu_init), DEVICE_ID(1) = 1 << 16, 0, 0. */
    v9x_r6_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    v9x_r6_3d_me_initialize(&stream);
    CHECK(stream.count == V9X_R6_ME_INITIALIZE_DWORDS);
    CHECK(buffer[0] == 0xc0054400ul && buffer[1] == 1ul && buffer[2] == 3ul &&
          buffer[3] == 7ul && buffer[4] == 0x00010000ul && buffer[5] == 0ul &&
          buffer[6] == 0ul);
}

/* The header's offsets, each beside Linux r600d.h's value for that name. */
static const v9x_u32 offset_checks[][2] = {
    { V9X_R6_GRBM_CNTL, 0x8000ul },
    { V9X_R6_GRBM_STATUS, 0x8010ul },
    { V9X_R6_GRBM_SOFT_RESET, 0x8020ul },
    { V9X_R6_WAIT_UNTIL, 0x8040ul },
    { V9X_R6_SCRATCH_REG0, 0x8500ul },
    { V9X_R6_CP_STAT, 0x8680ul },
    { V9X_R6_CP_ME_CNTL, 0x86d8ul },
    { V9X_R6_CP_RB_RPTR, 0x8700ul },
    { V9X_R6_CP_QUEUE_THRESHOLDS, 0x8760ul },
    { V9X_R6_CP_MEQ_THRESHOLDS, 0x8764ul },
    { V9X_R6_VGT_CACHE_INVALIDATION, 0x88c4ul },
    { V9X_R6_VGT_GS_PER_ES, 0x88c8ul },
    { V9X_R6_VGT_ES_PER_GS, 0x88ccul },
    { V9X_R6_VGT_GS_VERTEX_REUSE, 0x88d4ul },
    { V9X_R6_VGT_GS_PER_VS, 0x88e8ul },
    { V9X_R6_CC_GC_SHADER_PIPE_CONFIG, 0x8950ul },
    { V9X_R6_VGT_PRIMITIVE_TYPE, 0x8958ul },
    { V9X_R6_VGT_NUM_INSTANCES, 0x8974ul },
    { V9X_R6_PA_CL_ENHANCE, 0x8a14ul },
    { V9X_R6_PA_SC_LINE_STIPPLE_STATE, 0x8b10ul },
    { V9X_R6_PA_SC_MULTI_CHIP_CNTL, 0x8b20ul },
    { V9X_R6_PA_SC_AA_SAMPLE_LOCS_2S, 0x8b40ul },
    { V9X_R6_PA_SC_AA_SAMPLE_LOCS_4S, 0x8b44ul },
    { V9X_R6_PA_SC_AA_SAMPLE_LOCS_8S_WD0, 0x8b48ul },
    { V9X_R6_PA_SC_AA_SAMPLE_LOCS_8S_WD1, 0x8b4cul },
    { V9X_R6_PA_SC_ENHANCE, 0x8bf0ul },
    { V9X_R6_SQ_CONFIG, 0x8c00ul },
    { V9X_R6_SQ_GPR_RESOURCE_MGMT_1, 0x8c04ul },
    { V9X_R6_SQ_GPR_RESOURCE_MGMT_2, 0x8c08ul },
    { V9X_R6_SQ_THREAD_RESOURCE_MGMT, 0x8c0cul },
    { V9X_R6_SQ_STACK_RESOURCE_MGMT_1, 0x8c10ul },
    { V9X_R6_SQ_STACK_RESOURCE_MGMT_2, 0x8c14ul },
    { V9X_R6_SQ_MS_FIFO_SIZES, 0x8cf0ul },
    { V9X_R6_SX_DEBUG_1, 0x9054ul },
    { V9X_R6_SPI_CONFIG_CNTL, 0x9100ul },
    { V9X_R6_SPI_CONFIG_CNTL_1, 0x913cul },
    { V9X_R6_TA_CNTL_AUX, 0x9508ul },
    { V9X_R6_TC_CNTL, 0x9608ul },
    { V9X_R6_VC_ENHANCE, 0x9714ul },
    { V9X_R6_DB_DEBUG, 0x9830ul },
    { V9X_R6_DB_WATERMARKS, 0x9838ul },
    { V9X_R6_GB_TILING_CONFIG, 0x98f0ul },
    { V9X_R6_CC_RB_BACKEND_DISABLE, 0x98f4ul },
    { V9X_R6_RAMCFG, 0x2408ul },
    /* Not in r600d.h: xf86's r600_reg_r7xx.h
     * R7xx_SQ_DYN_GPR_CNTL_PS_FLUSH_REQ. */
    { V9X_R6_SQ_DYN_GPR_CNTL_PS_FLUSH, 0x8d8cul }
};
#define OFFSET_CHECKS (sizeof(offset_checks) / sizeof(offset_checks[0]))

/* The table entry for offset, or 0. */
static const struct v9x_r6_reg_update *entry_of(v9x_u32 offset)
{
    v9x_u32 index;

    for (index = 0ul; index < V9X_R6_RV630_GPU_INIT_COUNT; ++index) {
        if (v9x_r6_rv630_gpu_init[index].offset == offset) {
            return &v9x_r6_rv630_gpu_init[index];
        }
    }
    return 0;
}

static void test_gpu_init(void)
{
    struct v9x_r6_pm4 stream;
    v9x_u32 reads[V9X_R6_RV630_GPU_INIT_COUNT];
    const struct v9x_r6_reg_update *e;
    v9x_u32 index;
    v9x_u32 at;
    v9x_u32 written;

    /* Offsets, against r600d.h's. R6xx's SX_DEBUG_1 is 9054h (Evergreen's
     * 9058h) and its SQ_THREAD_RESOURCE_MGMT 8C0Ch (Evergreen's 8C18h). */
    for (index = 0ul; index < OFFSET_CHECKS; ++index) {
        CHECK(offset_checks[index][0] == offset_checks[index][1]);
    }

    /* Every entry inside SET_CONFIG_REG's range, each register once. */
    for (index = 0ul; index < V9X_R6_RV630_GPU_INIT_COUNT; ++index) {
        e = &v9x_r6_rv630_gpu_init[index];
        CHECK(e->offset >= V9X_R6_SET_CONFIG_START &&
              e->offset < V9X_R6_SET_CONFIG_END);
        CHECK(entry_of(e->offset) == e);
    }

    /* r600.c r600_gpu_init, CHIP_RV630 (around line 2021) and its
     * family-specific branches. */
    e = entry_of(V9X_R6_GRBM_CNTL);
    CHECK(e != 0 && e->keep == 0ul && e->set == 0xfful);
    e = entry_of(V9X_R6_CP_QUEUE_THRESHOLDS);
    CHECK(e != 0 && e->set == ((0x16ul << 0) | (0x2bul << 8)));
    e = entry_of(V9X_R6_CP_MEQ_THRESHOLDS);
    CHECK(e != 0 && e->set == ((0x40ul << 16) | (0x40ul << 24)));
    e = entry_of(V9X_R6_TA_CNTL_AUX);
    CHECK(e != 0 && e->set == ((1ul << 1) | (1ul << 24) | (1ul << 25) |
                               (1ul << 26)));
    /* SX_DEBUG_1 |= SMX_EVENT_RELEASE | ENABLE_NEW_SMX_ADDRESS */
    e = entry_of(V9X_R6_SX_DEBUG_1);
    CHECK(e != 0 && e->keep == 0xfffffffful && e->set == 0x00010001ul);
    /* DB_DEBUG PREZ_MUST_WAIT_FOR_POSTZ_DONE for RV630. */
    e = entry_of(V9X_R6_DB_DEBUG);
    CHECK(e != 0 && e->set == 0x80000000ul);
    e = entry_of(V9X_R6_DB_WATERMARKS);
    CHECK(e != 0 && e->set == (4ul | (16ul << 20) | (16ul << 5) |
                               (4ul << 15)));
    /* SQ_MS_FIFO_SIZES: tmp &= ~DONE_FIFO_HIWATER(0xff); |= (4). */
    e = entry_of(V9X_R6_SQ_MS_FIFO_SIZES);
    CHECK(e != 0 && e->keep == ~(0xfful << 16) && e->set == (4ul << 16));
    /* SQ_CONFIG: &= ~(PS|VS|GS|ES_PRIO(3)); |= DX9_CONSTS | VC_ENABLE |
     * VS_PRIO(1) | GS_PRIO(2) | ES_PRIO(3). */
    e = entry_of(V9X_R6_SQ_CONFIG);
    CHECK(e != 0 && e->keep == ~((3ul << 24) | (3ul << 26) | (3ul << 28) |
                                 (3ul << 30)));
    CHECK(e != 0 && e->set == ((1ul << 2) | (1ul << 0) | (1ul << 26) |
                               (2ul << 28) | (3ul << 30)));
    /* The RV630/RV635 branch: 44/44/2 GPRs, 18/18, threads 79/78/4/31,
     * stacks 40/40 and 32/16. */
    e = entry_of(V9X_R6_SQ_GPR_RESOURCE_MGMT_1);
    CHECK(e != 0 && e->set == (44ul | (44ul << 16) | (2ul << 28)));
    e = entry_of(V9X_R6_SQ_GPR_RESOURCE_MGMT_2);
    CHECK(e != 0 && e->set == (18ul | (18ul << 16)));
    e = entry_of(V9X_R6_SQ_THREAD_RESOURCE_MGMT);
    CHECK(e != 0 && e->set == (79ul | (78ul << 8) | (4ul << 16) |
                               (31ul << 24)));
    e = entry_of(V9X_R6_SQ_STACK_RESOURCE_MGMT_1);
    CHECK(e != 0 && e->set == (40ul | (40ul << 16)));
    e = entry_of(V9X_R6_SQ_STACK_RESOURCE_MGMT_2);
    CHECK(e != 0 && e->set == (32ul | (16ul << 16)));
    e = entry_of(V9X_R6_VGT_CACHE_INVALIDATION);
    CHECK(e != 0 && e->set == 2ul);
    /* The sample positions, S0_X(0xc) | S0_Y(0x4) | S1_X(0x4) |
     * S1_Y(0xc) and so on. */
    e = entry_of(V9X_R6_PA_SC_AA_SAMPLE_LOCS_2S);
    CHECK(e != 0 && e->set == (0xcul | (0x4ul << 4) | (0x4ul << 8) |
                               (0xcul << 12)));
    e = entry_of(V9X_R6_PA_SC_AA_SAMPLE_LOCS_4S);
    CHECK(e != 0 && e->set == (0xeul | (0xeul << 4) | (0x2ul << 8) |
                               (0x2ul << 12) | (0xaul << 16) |
                               (0x6ul << 20) | (0x6ul << 24) |
                               (0xaul << 28)));
    e = entry_of(V9X_R6_PA_SC_AA_SAMPLE_LOCS_8S_WD0);
    CHECK(e != 0 && e->set == (0xeul | (0xbul << 4) | (0x4ul << 8) |
                               (0xcul << 12) | (0x1ul << 16) |
                               (0x6ul << 20) | (0xaul << 24) |
                               (0xeul << 28)));
    e = entry_of(V9X_R6_PA_SC_AA_SAMPLE_LOCS_8S_WD1);
    CHECK(e != 0 && e->set == (0x6ul | (0x1ul << 4) | (0xbul << 16) |
                               (0x4ul << 20) | (0x7ul << 24) |
                               (0x8ul << 28)));
    /* VGT_GS_PER_ES = max_pipes (2) * 16. */
    e = entry_of(V9X_R6_VGT_GS_PER_ES);
    CHECK(e != 0 && e->set == 32ul);
    e = entry_of(V9X_R6_VGT_ES_PER_GS);
    CHECK(e != 0 && e->set == 128ul);
    e = entry_of(V9X_R6_VGT_GS_PER_VS);
    CHECK(e != 0 && e->set == 2ul);
    e = entry_of(V9X_R6_VGT_GS_VERTEX_REUSE);
    CHECK(e != 0 && e->set == 16ul);
    /* TC_CNTL TC_L2_SIZE(4) for RV630/RV635. */
    e = entry_of(V9X_R6_TC_CNTL);
    CHECK(e != 0 && e->set == (4ul << 5));
    e = entry_of(V9X_R6_PA_CL_ENHANCE);
    CHECK(e != 0 && e->set == (1ul | (3ul << 1)));
    e = entry_of(V9X_R6_PA_SC_ENHANCE);
    CHECK(e != 0 && e->set == 4095ul);
    /* Linux writes VGT_NUM_INSTANCES 0 here (Evergreen's writes 1). */
    e = entry_of(V9X_R6_VGT_NUM_INSTANCES);
    CHECK(e != 0 && e->set == 0ul);

    /* GB_TILING_CONFIG from M-NZHWBR's RAMCFG A9h: the 8A12h its BIOS left
     * (docs\decisions\2026-11-13-rv630-register-probe.md). Then the row
     * clamp at 3 and the burst-length group size. */
    CHECK(v9x_r6_rv630_tiling_config(0x000000a9ul) == 0x00008a12ul);
    CHECK(v9x_r6_rv630_tiling_config(0x0000001cul) ==
          (0x2ul | (3ul << 8) | 0x800ul | (3ul << 14)));
    CHECK(v9x_r6_rv630_tiling_config(0x00000100ul) ==
          (0x2ul | (1ul << 6) | 0x800ul));

    /* The stream: one SET_CONFIG_REG per entry, read-modify-write applied,
     * GB_TILING_CONFIG the computed value, and no write of the CP's two
     * queue thresholds, which stalled the ME through its own ring on
     * M-NZHWBR (V9XR63D runs 1-3). Reads are a pattern. */
    for (index = 0ul; index < V9X_R6_RV630_GPU_INIT_COUNT; ++index) {
        reads[index] = 0x5a5a5a5aul + index;
    }
    v9x_r6_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    v9x_r6_3d_gpu_init(&stream, reads, 0x00008a12ul);
    CHECK(stream.overflow == 0ul);
    CHECK(stream.count == (V9X_R6_RV630_GPU_INIT_COUNT - 2ul) * 3ul);
    CHECK(walks(buffer, stream.count));
    CHECK(find_reg(buffer, stream.count, V9X_R6_CP_QUEUE_THRESHOLDS) >=
          stream.count);
    CHECK(find_reg(buffer, stream.count, V9X_R6_CP_MEQ_THRESHOLDS) >=
          stream.count);
    written = 0ul;
    for (index = 0ul; index < V9X_R6_RV630_GPU_INIT_COUNT; ++index) {
        e = &v9x_r6_rv630_gpu_init[index];
        if (e->offset == V9X_R6_CP_QUEUE_THRESHOLDS ||
            e->offset == V9X_R6_CP_MEQ_THRESHOLDS) {
            continue;
        }
        CHECK(buffer[written * 3ul] == 0xc0016800ul);
        CHECK(buffer[written * 3ul + 1ul] == (e->offset - 0x8000ul) >> 2);
        if (e->offset == V9X_R6_GB_TILING_CONFIG) {
            CHECK(buffer[written * 3ul + 2ul] == 0x00008a12ul);
        } else {
            CHECK(buffer[written * 3ul + 2ul] ==
                  ((reads[index] & e->keep) | e->set));
        }
        ++written;
    }
    at = find_reg(buffer, stream.count, V9X_R6_SQ_CONFIG);
    CHECK(at < stream.count && buffer[at] ==
          (((0x5a5a5a5aul + 13ul) & 0x00fffffful) | 0xe4000005ul));
}

static void test_default_state(void)
{
    struct v9x_r6_pm4 stream;
    v9x_u32 index;
    v9x_u32 at;

    v9x_r6_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    /* M-NZHWBR's CC_GC_SHADER_PIPE_CONFIG 00F8FC00h. */
    v9x_r6_3d_default_state(&stream, 0x00f8fc00ul, 0xef105000ul);
    CHECK(stream.overflow == 0ul);
    CHECK(walks(buffer, stream.count));

    /* r600_start_3d before RV770: START_3D_CMDBUF, 0; CONTEXT_CONTROL
     * 80000000h, 80000000h. */
    CHECK(buffer[0] == XF86_PACK3(0x24, 1) && buffer[1] == 0ul);
    CHECK(buffer[2] == XF86_PACK3(0x28, 2) && buffer[3] == 0x80000000ul &&
          buffer[4] == 0x80000000ul);

    /* r600_gpu_init: 8 - popcount(FCh) = 2 active pipes: DEALLOC 8,
     * REUSE 6. Offsets r600d.h. */
    at = find_reg(buffer, stream.count, 0x28c5cul);
    CHECK(at < stream.count && buffer[at] == 8ul);
    at = find_reg(buffer, stream.count, 0x28c58ul);
    CHECK(at < stream.count && buffer[at] == 6ul);
    for (index = 0ul; index < 8ul; ++index) {
        at = find_reg(buffer, stream.count, 0x28040ul + index * 4ul);
        CHECK(at < stream.count && buffer[at] == 0ul);
    }

    /* r600_sq_setup with r600_set_default_state's CHIP_FAMILY_RV630
     * numbers (r6xx_accel.c 887-902): VC_ENABLE | DX9_CONSTS |
     * ALU_INST_PREFER_VECTOR | priorities 0,1,2,3; GPRs 84/36/4, 0/0;
     * threads 144/40/4/4; stacks 40/40, 32/16. */
    at = find_reg(buffer, stream.count, V9X_R6_SQ_CONFIG);
    CHECK(at < stream.count && buffer[at - 2ul] == XF86_PACK3(0x68, 7));
    CHECK(at < stream.count &&
          buffer[at] == (1ul | 4ul | 8ul | (1ul << 26) | (2ul << 28) |
                         (3ul << 30)));
    CHECK(at < stream.count &&
          buffer[at + 1ul] == (84ul | (36ul << 16) | (4ul << 28)));
    CHECK(at < stream.count && buffer[at + 2ul] == 0ul);
    CHECK(at < stream.count &&
          buffer[at + 3ul] == (144ul | (40ul << 8) | (4ul << 16) |
                               (4ul << 24)));
    CHECK(at < stream.count && buffer[at + 4ul] == (40ul | (40ul << 16)));
    CHECK(at < stream.count && buffer[at + 5ul] == (32ul | (16ul << 16)));

    /* The pre-RV770 branch (r6xx_accel.c 994-1005). */
    at = find_reg(buffer, stream.count, V9X_R6_TA_CNTL_AUX);
    CHECK(at < stream.count && buffer[at] == (3ul | (28ul << 22)));
    at = find_reg(buffer, stream.count, V9X_R6_DB_DEBUG);
    CHECK(at < stream.count && buffer[at] == 0x82000000ul);
    at = find_reg(buffer, stream.count, V9X_R6_SQ_DYN_GPR_CNTL_PS_FLUSH);
    CHECK(at < stream.count && buffer[at] == 0ul);
    /* SQ_VTX_BASE_VTX_LOC through SET_CTL_CONST. */
    at = find_reg(buffer, stream.count, 0x3cff0ul);
    CHECK(at < stream.count && buffer[at - 2ul] == XF86_PACK3(0x6f, 3));
    /* DB_RENDER_CONTROL: STENCIL_ and DEPTH_COMPRESS_DISABLE, then
     * FORCE_SHADER_Z_ORDER before RV770. */
    at = find_reg(buffer, stream.count, 0x28d0cul);
    CHECK(at < stream.count && buffer[at] == ((1ul << 5) | (1ul << 6)) &&
          buffer[at + 1ul] == (1ul << 6));
    at = find_reg(buffer, stream.count, 0x2880cul);
    CHECK(at < stream.count && buffer[at] == ((1ul << 4) | (1ul << 9)));
    at = find_reg(buffer, stream.count, 0x28d44ul);
    CHECK(at < stream.count && buffer[at] == 0xaa00ul);
    at = find_reg(buffer, stream.count, 0x28c30ul);
    CHECK(at < stream.count && buffer[at] == (1ul << 24));
    at = find_reg(buffer, stream.count, 0x2823cul);
    CHECK(at < stream.count && buffer[at] == 0xful);
    at = find_reg(buffer, stream.count, 0x2820cul);
    CHECK(at < stream.count && buffer[at] == 0xfffful);
    /* PA_SC_EDGERULE 0 before RV770. */
    at = find_reg(buffer, stream.count, 0x28230ul);
    CHECK(at < stream.count && buffer[at] == 0ul);

    /* Four clip rectangles and sixteen viewport scissors, (0,0)-(8192,
     * 8192), the scissors with WINDOW_OFFSET_DISABLE. */
    for (index = 0ul; index < 4ul; ++index) {
        at = find_reg(buffer, stream.count, 0x28210ul + index * 8ul);
        CHECK(at < stream.count && buffer[at] == 0ul &&
              buffer[at + 1ul] == 0x20002000ul);
    }
    for (index = 0ul; index < 16ul; ++index) {
        at = find_reg(buffer, stream.count, 0x28250ul + index * 8ul);
        CHECK(at < stream.count && buffer[at] == 0x80000000ul &&
              buffer[at + 1ul] == 0x20002000ul);
    }

    /* PA_SC_MPASS_PS_CNTL: WALK_ORDER_ENABLE | FORCE_EOV_CNTDWN_ENABLE. */
    at = find_reg(buffer, stream.count, 0x28a4cul);
    CHECK(at < stream.count && buffer[at] == 0ul);
    at = find_nth(buffer, stream.count, 0x28a4cul, 1ul);
    CHECK(at < stream.count && buffer[at] == (1ul << 4 | 1ul << 14));
    /* PA_SU_VTX_CNTL: ROUND_MODE(2) | PIX_CENTER | QUANT_MODE(5), and the
     * guard-band adjusts at 1.0f. */
    at = find_reg(buffer, stream.count, 0x28c08ul);
    CHECK(at < stream.count &&
          buffer[at] == ((2ul << 1) | 1ul | (5ul << 3)));
    CHECK(at < stream.count && buffer[at + 1ul] == float_of(1.0f) &&
          buffer[at + 4ul] == float_of(1.0f));
    at = find_reg(buffer, stream.count, 0x28810ul);
    CHECK(at < stream.count && buffer[at] == (1ul << 16) &&
          buffer[at + 1ul] == (1ul << 2) && buffer[at + 2ul] == (1ul << 8));
    /* SPI defaults: SEMANTIC_1(1), inputs n with DEFAULT_VAL(1) and
     * SEL_CENTROID. */
    at = find_reg(buffer, stream.count, 0x28614ul);
    CHECK(at < stream.count && buffer[at] == 0x100ul);
    at = find_reg(buffer, stream.count, 0x28644ul);
    CHECK(at < stream.count && buffer[at] == 0x900ul &&
          buffer[at + 1ul] == 0x901ul);
    /* The fetch shader at the address given. */
    at = find_reg(buffer, stream.count, 0x28894ul);
    CHECK(at < stream.count && buffer[at] == 0x00ef1050ul);
    at = find_reg(buffer, stream.count, 0x288a4ul);
    CHECK(at < stream.count && buffer[at] == 0ul);
    /* VGT_MAX_VTX_INDX FFFFFFh; PA_SU_LINE_CNTL WIDTH(8). */
    at = find_reg(buffer, stream.count, 0x28400ul);
    CHECK(at < stream.count && buffer[at] == 0x00fffffful);
    at = find_reg(buffer, stream.count, 0x28a08ul);
    CHECK(at < stream.count && buffer[at] == 8ul);
    /* SX_MISC is written by gpu_init's context part and at the end. */
    CHECK(find_nth(buffer, stream.count, 0x28350ul, 1ul) < stream.count);
}

/* M-NZHWBR's window layout, as V9XR63D lays it out. */
#define WINDOW_GPU  0xef100000ul

static void base_solid(struct v9x_r6_3d_solid *solid)
{
    solid->target_gpu = WINDOW_GPU + 0x7000ul;
    solid->target_pitch = 64ul;
    solid->target_height = 8ul;
    solid->vs_gpu = WINDOW_GPU + 0x5000ul;
    solid->ps_gpu = WINDOW_GPU + 0x5200ul;
    solid->vertex_gpu = WINDOW_GPU + 0x5400ul;
    solid->colour = 0xff3366ccul;
}

static void test_solid(void)
{
    struct v9x_r6_pm4 stream;
    struct v9x_r6_3d_solid solid;
    v9x_u32 at;
    v9x_u32 at2;
    v9x_u32 draw;

    base_solid(&solid);
    v9x_r6_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    CHECK(v9x_r6_3d_solid(&solid, &stream) == V9X_STATUS_OK);
    CHECK(walks(buffer, stream.count));

    /* Scissors at 64x8: generic and window with WINDOW_OFFSET_DISABLE,
     * screen without (r6xx_accel.c 767-823). */
    at = find_reg(buffer, stream.count, 0x28240ul);
    CHECK(at < stream.count && buffer[at] == 0x80000000ul &&
          buffer[at + 1ul] == (64ul | (8ul << 16)));
    at = find_reg(buffer, stream.count, 0x28030ul);
    CHECK(at < stream.count && buffer[at] == 0ul &&
          buffer[at + 1ul] == (64ul | (8ul << 16)));
    at = find_reg(buffer, stream.count, 0x28204ul);
    CHECK(at < stream.count && buffer[at] == 0x80000000ul &&
          buffer[at + 1ul] == (64ul | (8ul << 16)));

    /* r600_vs_setup and r600_ps_setup (r600_exa.c 226-241): an SH sync of
     * 512 bytes before each start; NUM_GPRS 2 and 1, the PS with
     * UNCACHED_FIRST_INST and export mode 2. SQ_PGM_START_VS 28858h,
     * RESOURCES_VS 28868h, START_PS 28840h, RESOURCES_PS 28850h,
     * EXPORTS_PS 28854h (r600d.h). */
    at = find_reg(buffer, stream.count, 0x28858ul);
    CHECK(at < stream.count && buffer[at] == (WINDOW_GPU + 0x5000ul) >> 8);
    CHECK(at >= 7ul && buffer[at - 7ul] == XF86_PACK3(0x43, 4) &&
          buffer[at - 6ul] == (1ul << 27) && buffer[at - 5ul] == 2ul &&
          buffer[at - 4ul] == (WINDOW_GPU + 0x5000ul) >> 8);
    at = find_reg(buffer, stream.count, 0x28868ul);
    CHECK(at < stream.count && buffer[at] == 2ul);
    at = find_reg(buffer, stream.count, 0x28840ul);
    CHECK(at < stream.count && buffer[at] == (WINDOW_GPU + 0x5200ul) >> 8);
    at = find_reg(buffer, stream.count, 0x28850ul);
    CHECK(at < stream.count && buffer[at] == (1ul | (1ul << 28)));
    at = find_reg(buffer, stream.count, 0x28854ul);
    CHECK(at < stream.count && buffer[at] == 2ul);

    /* r600_set_render_target: CB_COLOR0_BASE, then SURFACE_BASE_UPDATE
     * 2 << id at once (the rv6xx workaround). */
    at = find_reg(buffer, stream.count, 0x28040ul);
    CHECK(at < stream.count && buffer[at] == (WINDOW_GPU + 0x7000ul) >> 8);
    CHECK(at < stream.count && buffer[at + 1ul] == XF86_PACK3(0x73, 1) &&
          buffer[at + 2ul] == 2ul);
    /* CB_COLOR0_SIZE: pitch / 8 - 1, (64 * 8) / 64 - 1 << 10. */
    at = find_reg(buffer, stream.count, 0x28060ul);
    CHECK(at < stream.count && buffer[at] == (7ul | (7ul << 10)));
    /* CB_COLOR0_INFO: FORMAT COLOR_8_8_8_8 (1Ah) << 2, ARRAY_MODE 0,
     * COMP_SWAP 1 << 16, BLEND_CLAMP 1 << 20, SOURCE_FORMAT 1 << 27
     * (R600PrepareSolid for 32 bpp). */
    at = find_reg(buffer, stream.count, 0x280a0ul);
    CHECK(at < stream.count &&
          buffer[at] == ((0x1aul << 2) | (1ul << 16) | (1ul << 20) |
                         (1ul << 27)));
    at = find_reg(buffer, stream.count, 0x280c0ul);
    CHECK(at < stream.count && buffer[at] == (WINDOW_GPU + 0x7000ul) >> 8);
    at = find_reg(buffer, stream.count, 0x28238ul);
    CHECK(at < stream.count && buffer[at] == 0xful);
    /* CB_COLOR_CONTROL R600_ROP[GXcopy] = RADEON_ROP3_S, no blend. */
    at = find_reg(buffer, stream.count, 0x28808ul);
    CHECK(at < stream.count && buffer[at] == 0x00cc0000ul);
    at = find_reg(buffer, stream.count, 0x28780ul);
    CHECK(at < stream.count && buffer[at] == 0ul);

    /* r600_set_spi(0, 0). */
    at = find_reg(buffer, stream.count, 0x286c4ul);
    CHECK(at < stream.count && buffer[at] == 0ul);
    at = find_reg(buffer, stream.count, 0x286ccul);
    CHECK(at < stream.count && buffer[at] == 0ul);

    /* The colour as R600PrepareSolid computes it: (float)r / 255 and so
     * on, R, G, B, A, in SET_ALU_CONST at the PS's constant 0. */
    at = find_reg(buffer, stream.count, 0x30000ul);
    CHECK(at < stream.count && buffer[at - 2ul] == XF86_PACK3(0x6a, 5));
    CHECK(at < stream.count && buffer[at] == float_of((float)0x33 / 255));
    CHECK(at < stream.count &&
          buffer[at + 1ul] == float_of((float)0x66 / 255));
    CHECK(at < stream.count &&
          buffer[at + 2ul] == float_of((float)0xcc / 255));
    CHECK(at < stream.count && buffer[at + 3ul] == float_of(1.0f));

    /* r600_set_vtx_resource: a VC sync over the vertices (RV630 is not in
     * the TC-only list), then base, size - 1, stride 8 << 8,
     * MEM_REQUEST_SIZE 1, 0, 0, VALID_BUFFER << 30. */
    at = find_reg(buffer, stream.count, 0x39180ul);
    CHECK(at < stream.count && buffer[at - 2ul] == XF86_PACK3(0x6d, 8));
    CHECK(at < stream.count && buffer[at] == WINDOW_GPU + 0x5400ul &&
          buffer[at + 1ul] == 23ul && buffer[at + 2ul] == (8ul << 8) &&
          buffer[at + 3ul] == 1ul && buffer[at + 4ul] == 0ul &&
          buffer[at + 5ul] == 0ul && buffer[at + 6ul] == 0xc0000000ul);
    CHECK(at >= 7ul && buffer[at - 7ul] == XF86_PACK3(0x43, 4) &&
          buffer[at - 6ul] == (1ul << 24) && buffer[at - 5ul] == 1ul);

    /* r600_draw_auto: VGT_PRIMITIVE_TYPE DI_PT_RECTLIST, INDEX_TYPE 16-bit,
     * NUM_INSTANCES 1, DRAW_INDEX_AUTO 3 indices, DI_SRC_SEL_AUTO_INDEX. */
    at = find_reg(buffer, stream.count, V9X_R6_VGT_PRIMITIVE_TYPE);
    CHECK(at < stream.count && buffer[at] == 0x11ul);
    CHECK(at < stream.count && buffer[at + 1ul] == XF86_PACK3(0x2a, 1) &&
          buffer[at + 2ul] == 0ul);
    CHECK(at < stream.count && buffer[at + 3ul] == XF86_PACK3(0x2f, 1) &&
          buffer[at + 4ul] == 1ul);
    CHECK(at < stream.count && buffer[at + 5ul] == XF86_PACK3(0x2d, 2) &&
          buffer[at + 6ul] == 3ul && buffer[at + 7ul] == 2ul);
    draw = at + 5ul;

    /* r600_finish_op: EVENT_WRITE CACHE_FLUSH_AND_INV_EVENT (16h), WAIT_UNTIL
     * WAIT_3D_IDLE | WAIT_3D_IDLECLEAN, then CB_ACTION | CB0_DEST_BASE
     * over 64 * 8 * 4 bytes of the target. */
    at = find_op(buffer, stream.count, V9X_R6_P3_EVENT_WRITE, draw);
    CHECK(at < stream.count && buffer[at] == XF86_PACK3(0x46, 1) &&
          buffer[at + 1ul] == 0x16ul);
    at2 = find_reg(buffer, stream.count, V9X_R6_WAIT_UNTIL);
    CHECK(at2 < stream.count && at2 > at &&
          buffer[at2] == ((1ul << 15) | (1ul << 17)));
    at = find_op(buffer, stream.count, V9X_R6_P3_SURFACE_SYNC, at2);
    CHECK(at < stream.count && buffer[at + 1ul] == ((1ul << 25) | (1ul << 6)) &&
          buffer[at + 2ul] == 8ul &&
          buffer[at + 3ul] == (WINDOW_GPU + 0x7000ul) >> 8 &&
          at + 5ul == stream.count);

    /* Refused: an unaligned target, a pitch not a multiple of 8, none. */
    base_solid(&solid);
    solid.target_gpu += 4ul;
    v9x_r6_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    CHECK(v9x_r6_3d_solid(&solid, &stream) == V9X_STATUS_INVALID_ARGUMENT &&
          stream.count == 0ul);
    base_solid(&solid);
    solid.target_pitch = 60ul;
    CHECK(v9x_r6_3d_solid(&solid, &stream) == V9X_STATUS_INVALID_ARGUMENT);
    base_solid(&solid);
    solid.target_height = 0ul;
    CHECK(v9x_r6_3d_solid(&solid, &stream) == V9X_STATUS_INVALID_ARGUMENT);
    base_solid(&solid);
    v9x_r6_pm4_begin(&stream, buffer, 40ul);
    CHECK(v9x_r6_3d_solid(&solid, &stream) ==
          V9X_STATUS_INSUFFICIENT_MEMORY);

    /* The fence: SET_CONFIG_REG SCRATCH_REG0 (r600_ring_test's form). */
    v9x_r6_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    v9x_r6_3d_fence(&stream, 0x3d3d3d3dul);
    CHECK(stream.count == 3ul && buffer[0] == 0xc0016800ul &&
          buffer[1] == 0x140ul && buffer[2] == 0x3d3d3d3dul);
}

static void test_vertices(void)
{
    v9x_u32 out[V9X_R6_3D_VERTEX_DWORDS];
    v9x_u32 value;

    /* (float)v, as R600Solid writes vb[] = (float)x1 and so on. */
    for (value = 0ul; value <= 9000ul; ++value) {
        CHECK(v9x_r6_3d_float_bits(value) == float_of((float)value));
    }
    CHECK(v9x_r6_3d_float_bits(16777216ul) == float_of(16777216.0f));

    /* R600Solid's order: (x1, y1), (x1, y2), (x2, y2). */
    v9x_r6_3d_rect_vertices(8ul, 2ul, 56ul, 6ul, out);
    CHECK(out[0] == float_of(8.0f) && out[1] == float_of(2.0f));
    CHECK(out[2] == float_of(8.0f) && out[3] == float_of(6.0f));
    CHECK(out[4] == float_of(56.0f) && out[5] == float_of(6.0f));
}

/*
 * xf86-video-ati 6.14.6 r600_shader.h's macros, as written there (lines
 * 164-185, 244-252, 303-313), with cpu_to_le32 the identity, and the
 * enumerators of r600_reg_auto_r6xx.h / r600_reg_r6xx.h they are given.
 */
#define X_I_COUNT(x)            ((x) ? ((x) - 1) : 0)
#define X_ELEM_SIZE(x)          ((x) ? ((x) - 1) : 0)
#define X_BURST_COUNT(x)        ((x) ? ((x) - 1) : 0)
#define X_MEGA_FETCH_COUNT(x)   ((x) ? ((x) - 1) : 0)
#define X_CF_DWORD1(pc, cf_const, cond, count, call_count, eop, vpm, \
                    cf_inst, wqm, b) \
    ((v9x_u32)(((pc) << 0) | ((cf_const) << 3) | ((cond) << 8) | \
     (((count) & 7) << 10) | (((count) >> 3) << 19) | \
     ((call_count) << 13) | ((eop) << 21) | ((vpm) << 22) | \
     ((cf_inst) << 23) | ((wqm) << 30) | ((v9x_u32)(b) << 31)))
#define X_CF_ALU_DWORD0(addr, kb0, kb1, km0) \
    ((v9x_u32)(((addr) << 0) | ((kb0) << 22) | ((kb1) << 26) | \
     ((v9x_u32)(km0) << 30)))
#define X_CF_ALU_DWORD1(km1, ka0, ka1, count, uw, cf_inst, wqm, b) \
    ((v9x_u32)(((km1) << 0) | ((ka0) << 2) | ((ka1) << 10) | \
     ((count) << 18) | ((uw) << 25) | ((cf_inst) << 26) | \
     ((wqm) << 30) | ((v9x_u32)(b) << 31)))
#define X_EXP_DWORD0(array_base, type, rw_gpr, rr, index_gpr, es) \
    ((v9x_u32)(((array_base) << 0) | ((type) << 13) | ((rw_gpr) << 15) | \
     ((rr) << 22) | ((index_gpr) << 23) | ((v9x_u32)(es) << 30)))
#define X_EXP_DWORD1_SWIZ(sx, sy, sz, sw, el, bc, eop, vpm, cf_inst, wqm, \
                          b) \
    ((v9x_u32)(((sx) << 0) | ((sy) << 3) | ((sz) << 6) | ((sw) << 9) | \
     ((el) << 16) | ((bc) << 17) | ((eop) << 21) | ((vpm) << 22) | \
     ((cf_inst) << 23) | ((wqm) << 30) | ((v9x_u32)(b) << 31)))
#define X_ALU_DWORD0(s0, s0r, s0e, s0n, s1, s1r, s1e, s1n, im, ps, last) \
    ((v9x_u32)(((s0) << 0) | ((s0r) << 9) | ((s0e) << 10) | \
     ((s0n) << 12) | ((s1) << 13) | ((s1r) << 22) | ((s1e) << 23) | \
     ((s1n) << 25) | ((im) << 26) | ((ps) << 29) | \
     ((v9x_u32)(last) << 31)))
#define X_R6XX_ALU_DWORD1_OP2(s0a, s1a, uem, up, wm, fm, omod, alu_inst, bs, \
                              dst_gpr, dr, de, clamp) \
    ((v9x_u32)(((s0a) << 0) | ((s1a) << 1) | ((uem) << 2) | ((up) << 3) | \
     ((wm) << 4) | ((fm) << 5) | ((omod) << 6) | ((alu_inst) << 8) | \
     ((bs) << 18) | ((dst_gpr) << 21) | ((dr) << 28) | ((de) << 29) | \
     ((v9x_u32)(clamp) << 31)))
#define X_VTX_DWORD0(vtx_inst, ft, fwq, buffer_id, src_gpr, sr, ssx, mfc) \
    ((v9x_u32)(((vtx_inst) << 0) | ((ft) << 5) | ((fwq) << 7) | \
     ((buffer_id) << 8) | ((src_gpr) << 16) | ((sr) << 23) | \
     ((ssx) << 24) | ((v9x_u32)(mfc) << 26)))
#define X_VTX_DWORD1_GPR(dst_gpr, dr, dsx, dsy, dsz, dsw, ucf, fmt, nfa, \
                         fca, sma) \
    ((v9x_u32)(((dst_gpr) << 0) | ((dr) << 7) | ((dsx) << 9) | \
     ((dsy) << 12) | ((dsz) << 15) | ((dsw) << 18) | ((ucf) << 21) | \
     ((fmt) << 22) | ((v9x_u32)(nfa) << 28) | ((v9x_u32)(fca) << 30) | \
     ((v9x_u32)(sma) << 31)))
#define X_VTX_DWORD2(offset, es, cbns, mf) \
    ((v9x_u32)(((offset) << 0) | ((es) << 16) | ((cbns) << 18) | \
     ((mf) << 19)))
#define X_SQ_CF_INST_VTX         0x02
#define X_SQ_CF_INST_ALU         0x08
#define X_SQ_CF_INST_EXPORT_DONE 0x28
#define X_SQ_EXPORT_PIXEL        0
#define X_SQ_EXPORT_POS          1
#define X_SQ_EXPORT_PARAM        2
#define X_CF_POS0                60
#define X_SQ_SEL_X               0
#define X_SQ_SEL_Y               1
#define X_SQ_SEL_Z               2
#define X_SQ_SEL_W               3
#define X_SQ_SEL_0               4
#define X_SQ_SEL_1               5
#define X_FMT_32_32_FLOAT        30 /* the FMT_ enumeration's 30th */
#define X_SQ_NUM_FORMAT_SCALED   2
#define X_SQ_FORMAT_COMP_SIGNED  1
#define X_SQ_OP2_INST_MOV        0x19
#define X_ALU_SRC_CFILE_BASE     256

static void test_shaders(void)
{
    v9x_u32 got[16];
    v9x_u32 want[16];
    v9x_u32 i = 0ul;
    v9x_u32 channel;

    /* R600_solid_vs, r600_shader.c 38-124, little-endian. */
    want[i++] = 4ul;
    want[i++] = X_CF_DWORD1(0, 0, 0, X_I_COUNT(1), 0, 0, 0,
                            X_SQ_CF_INST_VTX, 0, 1);
    want[i++] = X_EXP_DWORD0(X_CF_POS0, X_SQ_EXPORT_POS, 1, 0, 0,
                             X_ELEM_SIZE(0));
    want[i++] = X_EXP_DWORD1_SWIZ(X_SQ_SEL_X, X_SQ_SEL_Y, X_SQ_SEL_Z,
                                  X_SQ_SEL_W, 0, X_BURST_COUNT(1), 0, 0,
                                  X_SQ_CF_INST_EXPORT_DONE, 0, 1);
    want[i++] = X_EXP_DWORD0(0, X_SQ_EXPORT_PARAM, 0, 0, 0, X_ELEM_SIZE(0));
    want[i++] = X_EXP_DWORD1_SWIZ(X_SQ_SEL_X, X_SQ_SEL_Y, X_SQ_SEL_Z,
                                  X_SQ_SEL_W, 0, X_BURST_COUNT(0), 1, 0,
                                  X_SQ_CF_INST_EXPORT_DONE, 0, 0);
    want[i++] = 0ul;
    want[i++] = 0ul;
    want[i++] = X_VTX_DWORD0(0, 0, 0, 0, 0, 0, X_SQ_SEL_X,
                             X_MEGA_FETCH_COUNT(8));
    want[i++] = X_VTX_DWORD1_GPR(1, 0, X_SQ_SEL_X, X_SQ_SEL_Y, X_SQ_SEL_0,
                                 X_SQ_SEL_1, 0, X_FMT_32_32_FLOAT,
                                 X_SQ_NUM_FORMAT_SCALED,
                                 X_SQ_FORMAT_COMP_SIGNED, 0);
    want[i++] = X_VTX_DWORD2(0, 0, 0, 1);
    want[i++] = 0ul;
    CHECK(i == V9X_R6_SOLID_VS_DWORDS);
    CHECK(v9x_r6_shader_solid_vs(got) == V9X_R6_SOLID_VS_DWORDS);
    CHECK(memcmp(got, want, V9X_R6_SOLID_VS_DWORDS * sizeof(v9x_u32)) == 0);
    /* Two words worked by hand, so a shared mistake in the transcribed
     * macros would show: CF_INST_VTX with BARRIER, and the fetch's
     * DWORD1. */
    CHECK(got[1] == 0x81000000ul && got[9] == 0x67961001ul);

    /* R600_solid_ps, r600_shader.c 127-269. */
    i = 0ul;
    want[i++] = X_CF_ALU_DWORD0(2, 0, 0, 0);
    want[i++] = X_CF_ALU_DWORD1(0, 0, 0, X_I_COUNT(4), 0, X_SQ_CF_INST_ALU,
                                0, 1);
    want[i++] = X_EXP_DWORD0(0, X_SQ_EXPORT_PIXEL, 0, 0, 0, X_ELEM_SIZE(1));
    want[i++] = X_EXP_DWORD1_SWIZ(X_SQ_SEL_X, X_SQ_SEL_Y, X_SQ_SEL_Z,
                                  X_SQ_SEL_W, 0, X_BURST_COUNT(1), 1, 0,
                                  X_SQ_CF_INST_EXPORT_DONE, 0, 1);
    for (channel = 0ul; channel < 4ul; ++channel) {
        want[i++] = X_ALU_DWORD0(X_ALU_SRC_CFILE_BASE + 0, 0, channel, 0,
                                 0, 0, channel, 0, 0, 0,
                                 channel == 3ul ? 1 : 0);
        want[i++] = X_R6XX_ALU_DWORD1_OP2(0, 0, 0, 0, 1, 0, 0,
                                          X_SQ_OP2_INST_MOV, 0, 0, 0,
                                          channel, 1);
    }
    CHECK(i == V9X_R6_SOLID_PS_DWORDS);
    CHECK(v9x_r6_shader_solid_ps(got) == V9X_R6_SOLID_PS_DWORDS);
    CHECK(memcmp(got, want, V9X_R6_SOLID_PS_DWORDS * sizeof(v9x_u32)) == 0);
    CHECK(got[1] == 0xa00c0000ul && got[3] == 0x94200688ul &&
          got[5] == 0x80001910ul && got[10] == 0x81800d00ul &&
          got[11] == 0xe0001910ul);
}

unsigned int v9x_run_r6_3d_tests(void)
{
    failures = 0u;
    test_packets();
    test_gpu_init();
    test_default_state();
    test_solid();
    test_vertices();
    test_shaders();
    return failures;
}
