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

/*
 * xf86's TEX_DWORD0-2 (r600_shader.h lines 338-345), and the opcode and
 * format numbers the interpolated shaders add, each from xf86's
 * r600_reg_auto_r6xx.h / r600_reg_r6xx.h (X_) beside Mesa r600's
 * r600_isa.c R600 column and r600_formats.h (M_): two sources per value.
 */
#define X_TEX_DWORD0(ti, bfm, fwq, rid, src_gpr, sr, ac) \
    ((v9x_u32)(((ti) << 0) | ((bfm) << 5) | ((fwq) << 7) | ((rid) << 8) | \
     ((src_gpr) << 16) | ((sr) << 23) | ((ac) << 24)))
#define X_TEX_DWORD1(dst_gpr, dr, dsx, dsy, dsz, dsw, lb, ctx, cty, ctz, ctw) \
    ((v9x_u32)(((dst_gpr) << 0) | ((dr) << 7) | ((dsx) << 9) | \
     ((dsy) << 12) | ((dsz) << 15) | ((dsw) << 18) | ((lb) << 21) | \
     ((ctx) << 28) | ((cty) << 29) | ((ctz) << 30) | \
     ((v9x_u32)(ctw) << 31)))
#define X_TEX_DWORD2(ox, oy, oz, sid, ssx, ssy, ssz, ssw) \
    ((v9x_u32)(((ox) << 0) | ((oy) << 5) | ((oz) << 10) | ((sid) << 15) | \
     ((ssx) << 20) | ((ssy) << 23) | ((ssz) << 26) | \
     ((v9x_u32)(ssw) << 29)))
#define X_SQ_CF_INST_TEX         0x01
#define X_SQ_CF_INST_EXPORT      0x27
#define X_SQ_OP2_INST_MUL        0x01
#define X_SQ_TEX_INST_SAMPLE     0x10
/* r600_reg_r6xx.h's FMT_ enumeration: FMT_32_32_32_32 = 34, then
 * FMT_32_32_32_32_FLOAT. */
#define X_FMT_32_32_32_32_FLOAT  35
#define X_TEX_UNNORMALIZED       0
#define M_MUL                    0x01
#define M_MOV                    0x19
#define M_SAMPLE                 0x10
#define M_CF_TEX                 0x01
#define M_CF_VTX                 0x02
#define M_CF_ALU                 0x08
#define M_CF_EXPORT              0x27
#define M_CF_EXPORT_DONE         0x28
#define M_FMT_32_32_FLOAT        0x1e
#define M_FMT_32_32_32_32_FLOAT  0x23

/* xf86's value beside Mesa's; the TEX_WORD fields as xf86's macros place
 * them beside Mesa r600_sq.h's S_SQ_TEX_WORDn_* positions (SRC_GPR 16,
 * DST_SEL_X 9, COORD_TYPE_X 28, SAMPLER_ID 15, SRC_SEL_X 20, SRC_SEL_W
 * 29); the DB's coherency bits (r600_reg_r6xx.h, S_0085F0_*). */
static const v9x_u32 source_pairs[][2] = {
    { X_SQ_CF_INST_TEX, M_CF_TEX },
    { X_SQ_CF_INST_VTX, M_CF_VTX },
    { X_SQ_CF_INST_ALU, M_CF_ALU },
    { X_SQ_CF_INST_EXPORT, M_CF_EXPORT },
    { X_SQ_CF_INST_EXPORT_DONE, M_CF_EXPORT_DONE },
    { X_SQ_OP2_INST_MUL, M_MUL },
    { X_SQ_OP2_INST_MOV, M_MOV },
    { X_SQ_TEX_INST_SAMPLE, M_SAMPLE },
    { X_FMT_32_32_FLOAT, M_FMT_32_32_FLOAT },
    { X_FMT_32_32_32_32_FLOAT, M_FMT_32_32_32_32_FLOAT },
    { X_TEX_DWORD0(0, 0, 0, 0, 1, 0, 0), 1ul << 16 },
    { X_TEX_DWORD1(0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0),
      (1ul << 9) | (1ul << 28) },
    { X_TEX_DWORD2(0, 0, 0, 1, 1, 0, 0, 1),
      (1ul << 15) | (1ul << 20) | (1ul << 29) },
    { V9X_R6_SYNC_DB_ACTION, 1ul << 26 },
    { V9X_R6_SYNC_DB_DEST_BASE, 1ul << 14 }
};
#define SOURCE_PAIRS (sizeof(source_pairs) / sizeof(source_pairs[0]))

/* A vertex fetch as xf86 writes one: VTX_DWORD0, VTX_DWORD1_GPR, DWORD2,
 * pad. mega_count is MEGA_FETCH_COUNT's argument, bytes. */
static v9x_u32 x_fetch(v9x_u32 *want, v9x_u32 dst_gpr, v9x_u32 offset,
                       v9x_u32 mega_count, v9x_u32 mega, v9x_u32 format,
                       v9x_u32 sel_z, v9x_u32 sel_w)
{
    want[0] = X_VTX_DWORD0(0, 0, 0, 0, 0, 0, X_SQ_SEL_X,
                           X_MEGA_FETCH_COUNT(mega_count));
    want[1] = X_VTX_DWORD1_GPR(dst_gpr, 0, X_SQ_SEL_X, X_SQ_SEL_Y, sel_z,
                               sel_w, 0, format, X_SQ_NUM_FORMAT_SCALED,
                               X_SQ_FORMAT_COMP_SIGNED, 0);
    want[2] = X_VTX_DWORD2(offset, 0, 0, mega);
    want[3] = 0ul;
    return 4ul;
}

/* An export of one register, XYZW. */
static v9x_u32 x_export(v9x_u32 *want, v9x_u32 base, v9x_u32 type,
                        v9x_u32 gpr, v9x_u32 inst, v9x_u32 eop, v9x_u32 b)
{
    want[0] = X_EXP_DWORD0(base, type, gpr, 0, 0, X_ELEM_SIZE(0));
    want[1] = X_EXP_DWORD1_SWIZ(X_SQ_SEL_X, X_SQ_SEL_Y, X_SQ_SEL_Z,
                                X_SQ_SEL_W, 0, X_BURST_COUNT(1), eop, 0,
                                inst, 0, b);
    return 2ul;
}

/* Four slots of op, dst.c = src0.c op src1.c, clamped, as xf86's solid
 * (MOV) and composite (MUL) PSs write them. */
static v9x_u32 x_alu4(v9x_u32 *want, v9x_u32 op, v9x_u32 src0, v9x_u32 src1,
                      v9x_u32 dst)
{
    v9x_u32 channel;

    for (channel = 0ul; channel < 4ul; ++channel) {
        want[channel * 2ul] = X_ALU_DWORD0(src0, 0, channel, 0, src1, 0,
                                           channel, 0, 0, 0,
                                           channel == 3ul ? 1 : 0);
        want[channel * 2ul + 1ul] =
            X_R6XX_ALU_DWORD1_OP2(0, 0, 0, 0, 1, 0, 0, op, 0, dst, 0,
                                  channel, 1);
    }
    return 8ul;
}

/* A vertex clause of count fetches at dword first: no fetch reads its
 * index GPR (VTX_DWORD0 SRC_GPR, 22:16) after an earlier fetch of the
 * clause wrote it (VTX_DWORD1_GPR DST_GPR, 6:0). Every fetch indexes by
 * R0.x; every xf86 VS writes R0 with its last fetch. */
static int fetch_order_ok(const v9x_u32 *shader, v9x_u32 first,
                          v9x_u32 count)
{
    v9x_u32 written = 0ul;
    v9x_u32 index;
    v9x_u32 src;

    for (index = 0ul; index < count; ++index) {
        src = (shader[first + index * 4ul] >> 16) & 0x7ful;
        if ((written & (1ul << src)) != 0ul) {
            return 0;
        }
        written |= 1ul << (shader[first + index * 4ul + 1ul] & 0x1ful);
    }
    return 1;
}

/* No fetch of the clause asks for more than 32 bytes (VTX_DWORD0
 * MEGA_FETCH_COUNT, 31:26, at most 1Fh): Mesa's R600 fetches use 1Fh,
 * xf86's mega-fetches 24 bytes at most. (The first /modulate VS's 40, 27h,
 * was in place when M-NZHWBR drew wrong colours, but those were most
 * likely stale vertex-cache lines: a rule from the sources, not a
 * measured fault.) */
static int fetch_counts_ok(const v9x_u32 *shader, v9x_u32 first,
                           v9x_u32 count)
{
    v9x_u32 index;

    for (index = 0ul; index < count; ++index) {
        if ((shader[first + index * 4ul] >> 26) > 0x1ful) {
            return 0;
        }
    }
    return 1;
}

/* The clause's first fetch is a mega-fetch (VTX_DWORD2 MEGA_FETCH, bit
 * 19): a mini-fetch takes its data from the mega-fetch before it, and
 * every xf86 VS starts with one while Mesa sets it on every fetch. Three
 * mini-fetches alone halved green and blue on M-NZHWBR (V9XR63D runs e
 * and f). */
static int fetch_mega_first(const v9x_u32 *shader, v9x_u32 first,
                            v9x_u32 count)
{
    return count != 0ul && (shader[first + 2ul] & 0x00080000ul) != 0ul;
}

/* One ALU group of slots OP2 slots: no slot's DST_GPR (ALU_WORD1 27:21) is
 * a GPR any slot reads (ALU_WORD0 SRC0_SEL 8:0, SRC1_SEL 21:13, below
 * 128). */
static int alu_group_writes_unread(const v9x_u32 *alu, v9x_u32 slots)
{
    v9x_u32 read = 0ul;
    v9x_u32 index;
    v9x_u32 sel;

    for (index = 0ul; index < slots; ++index) {
        sel = alu[index * 2ul] & 0x1fful;
        if (sel < 32ul) {
            read |= 1ul << sel;
        }
        sel = (alu[index * 2ul] >> 13) & 0x1fful;
        if (sel < 32ul) {
            read |= 1ul << sel;
        }
    }
    for (index = 0ul; index < slots; ++index) {
        if ((read & (1ul << ((alu[index * 2ul + 1ul] >> 21) & 0x1ful))) !=
            0ul) {
            return 0;
        }
    }
    return 1;
}

static void test_interp_shaders(void)
{
    v9x_u32 got[V9X_R6_SHADER_DWORDS_MAX];
    v9x_u32 want[V9X_R6_SHADER_DWORDS_MAX];
    v9x_u32 i;

    /* Each value in both sources. */
    for (i = 0ul; i < SOURCE_PAIRS; ++i) {
        CHECK(source_pairs[i][0] == source_pairs[i][1]);
    }

    /* Gouraud VS: VTX ADDR(4) CNT(2); POS0 from R1; PARAM0 from R0, the
     * end; R1.xyzw MEGA(32) at 0, R0.xyzw MINI(16) at 16. */
    i = 0ul;
    want[i++] = 4ul;
    want[i++] = X_CF_DWORD1(0, 0, 0, X_I_COUNT(2), 0, 0, 0,
                            X_SQ_CF_INST_VTX, 0, 1);
    i += x_export(want + i, X_CF_POS0, X_SQ_EXPORT_POS, 1,
                  X_SQ_CF_INST_EXPORT_DONE, 0, 1);
    i += x_export(want + i, 0, X_SQ_EXPORT_PARAM, 0,
                  X_SQ_CF_INST_EXPORT_DONE, 1, 0);
    want[i++] = 0ul;
    want[i++] = 0ul;
    i += x_fetch(want + i, 1, 0, 32, 1, X_FMT_32_32_32_32_FLOAT,
                 X_SQ_SEL_Z, X_SQ_SEL_W);
    i += x_fetch(want + i, 0, 16, 16, 0, X_FMT_32_32_32_32_FLOAT,
                 X_SQ_SEL_Z, X_SQ_SEL_W);
    CHECK(i == V9X_R6_GOURAUD_VS_DWORDS);
    CHECK(v9x_r6_shader_gouraud_vs(got) == V9X_R6_GOURAUD_VS_DWORDS);
    CHECK(memcmp(got, want, i * sizeof(v9x_u32)) == 0);
    /* By hand: the first fetch's MEGA_FETCH_COUNT 31 << 26, and its DWORD1
     * (R1, X Y Z W, format 23h, scaled, signed). */
    CHECK(got[8] == 0x7c000000ul && got[9] == 0x68cd1001ul &&
          got[14] == 16ul);

    /* Gouraud PS: the solid PS's layout, R0 for constant 0. */
    i = 0ul;
    want[i++] = X_CF_ALU_DWORD0(2, 0, 0, 0);
    want[i++] = X_CF_ALU_DWORD1(0, 0, 0, X_I_COUNT(4), 0, X_SQ_CF_INST_ALU,
                                0, 1);
    i += x_export(want + i, 0, X_SQ_EXPORT_PIXEL, 0,
                  X_SQ_CF_INST_EXPORT_DONE, 1, 1);
    i += x_alu4(want + i, X_SQ_OP2_INST_MOV, 0, 0, 0);
    CHECK(i == V9X_R6_GOURAUD_PS_DWORDS);
    CHECK(v9x_r6_shader_gouraud_ps(got) == V9X_R6_GOURAUD_PS_DWORDS);
    CHECK(memcmp(got, want, i * sizeof(v9x_u32)) == 0);
    /* The solid PS's words but for source 0: GPR 0, not 256. */
    CHECK(got[4] == 0ul && got[5] == 0x80001910ul &&
          got[10] == 0x81800c00ul);

    /* Modulate VS: VTX ADDR(4) CNT(3); POS0 from R1; PARAM0 (EXPORT) from
     * R0; PARAM1 (EXPORT_DONE, the end) from R2; R1 MEGA(40) at 0,
     * R2.xy01 MINI(8) at 32, R0 MINI(16) at 16 - R0 last, as every xf86
     * VS fetches it. */
    i = 0ul;
    want[i++] = 4ul;
    want[i++] = X_CF_DWORD1(0, 0, 0, X_I_COUNT(3), 0, 0, 0,
                            X_SQ_CF_INST_VTX, 0, 1);
    i += x_export(want + i, X_CF_POS0, X_SQ_EXPORT_POS, 1,
                  X_SQ_CF_INST_EXPORT_DONE, 0, 1);
    i += x_export(want + i, 0, X_SQ_EXPORT_PARAM, 0, X_SQ_CF_INST_EXPORT,
                  0, 0);
    i += x_export(want + i, 1, X_SQ_EXPORT_PARAM, 2,
                  X_SQ_CF_INST_EXPORT_DONE, 1, 0);
    /* Each fetch as Mesa's R600 fetch shader writes one
     * (r600_create_vertex_fetch_shader, r600_bytecode_vtx_build):
     * mega_fetch_count 0x1F, MEGA_FETCH(1) before Cayman. */
    i += x_fetch(want + i, 1, 0, 32, 1, X_FMT_32_32_32_32_FLOAT,
                 X_SQ_SEL_Z, X_SQ_SEL_W);
    i += x_fetch(want + i, 2, 32, 32, 1, X_FMT_32_32_FLOAT, X_SQ_SEL_0,
                 X_SQ_SEL_1);
    i += x_fetch(want + i, 0, 16, 32, 1, X_FMT_32_32_32_32_FLOAT,
                 X_SQ_SEL_Z, X_SQ_SEL_W);
    CHECK(i == V9X_R6_MODULATE_VS_DWORDS);
    CHECK(v9x_r6_shader_modulate_vs(got) == V9X_R6_MODULATE_VS_DWORDS);
    CHECK(memcmp(got, want, i * sizeof(v9x_u32)) == 0);
    /* By hand: MEGA_FETCH (bit 19) with offset 0, 32 and 16. */
    CHECK(got[5] == 0x13800688ul && got[8] == 0x7c000000ul &&
          got[10] == 0x00080000ul && got[14] == 0x00080020ul &&
          got[18] == 0x00080010ul);
    CHECK(fetch_order_ok(got, 8ul, 3ul));
    CHECK(fetch_counts_ok(got, 8ul, 3ul));
    CHECK(fetch_mega_first(got, 8ul, 3ul));

    /* Modulate PS: TEX ADDR(8) CNT(1); ALU ADDR(4) CNT(4); R2 to MRT 0,
     * the end; R2 = R1 * R0 clamped, xf86's composite MUL; SAMPLE R1 = t0
     * at R1.xy01, unnormalised, sampler 0. */
    i = 0ul;
    want[i++] = 8ul;
    want[i++] = X_CF_DWORD1(0, 0, 0, X_I_COUNT(1), 0, 0, 0,
                            X_SQ_CF_INST_TEX, 0, 1);
    want[i++] = X_CF_ALU_DWORD0(4, 0, 0, 0);
    want[i++] = X_CF_ALU_DWORD1(0, 0, 0, X_I_COUNT(4), 0, X_SQ_CF_INST_ALU,
                                0, 1);
    i += x_export(want + i, 0, X_SQ_EXPORT_PIXEL, 2,
                  X_SQ_CF_INST_EXPORT_DONE, 1, 1);
    want[i++] = 0ul;
    want[i++] = 0ul;
    i += x_alu4(want + i, X_SQ_OP2_INST_MUL, 1, 0, 2);
    want[i++] = X_TEX_DWORD0(X_SQ_TEX_INST_SAMPLE, 0, 0, 0, 1, 0, 0);
    want[i++] = X_TEX_DWORD1(1, 0, X_SQ_SEL_X, X_SQ_SEL_Y, X_SQ_SEL_Z,
                             X_SQ_SEL_W, 0, X_TEX_UNNORMALIZED,
                             X_TEX_UNNORMALIZED, X_TEX_UNNORMALIZED,
                             X_TEX_UNNORMALIZED);
    want[i++] = X_TEX_DWORD2(0, 0, 0, 0, X_SQ_SEL_X, X_SQ_SEL_Y,
                             X_SQ_SEL_0, X_SQ_SEL_1);
    want[i++] = 0ul;
    CHECK(i == V9X_R6_MODULATE_PS_DWORDS);
    CHECK(v9x_r6_shader_modulate_ps(got) == V9X_R6_MODULATE_PS_DWORDS);
    CHECK(memcmp(got, want, i * sizeof(v9x_u32)) == 0);
    /* By hand: TEX with BARRIER, MUL with WRITE and CLAMP, the sample, its
     * source swizzle X Y 0 1. */
    CHECK(got[1] == 0x80800000ul && got[9] == 0x80400110ul &&
          got[4] == 0x00010000ul && got[16] == 0x00010010ul &&
          got[17] == 0x000d1001ul && got[18] == 0xb0800000ul);
    /* No ALU slot writes a GPR its group reads. */
    CHECK(alu_group_writes_unread(got + 8, 4ul));

    /* The Z-export PS: ALU ADDR(4) CNT(5); EXPORT PIXEL 61 (CF_PIXEL_Z)
     * from R1; EXPORT_DONE PIXEL 0 from R0, the end; four MOVs R0 = R0,
     * then MOV R1.x = C0.x, the second group. */
    i = 0ul;
    want[i++] = X_CF_ALU_DWORD0(4, 0, 0, 0);
    want[i++] = X_CF_ALU_DWORD1(0, 0, 0, X_I_COUNT(5), 0, X_SQ_CF_INST_ALU,
                                0, 1);
    i += x_export(want + i, 61, X_SQ_EXPORT_PIXEL, 1, X_SQ_CF_INST_EXPORT,
                  0, 1);
    i += x_export(want + i, 0, X_SQ_EXPORT_PIXEL, 0,
                  X_SQ_CF_INST_EXPORT_DONE, 1, 1);
    want[i++] = 0ul;
    want[i++] = 0ul;
    i += x_alu4(want + i, X_SQ_OP2_INST_MOV, 0, 0, 0);
    want[i++] = X_ALU_DWORD0(X_ALU_SRC_CFILE_BASE + 0, 0, 0, 0, 0, 0, 0, 0,
                             0, 0, 1);
    want[i++] = X_R6XX_ALU_DWORD1_OP2(0, 0, 0, 0, 1, 0, 0,
                                      X_SQ_OP2_INST_MOV, 0, 1, 0, 0, 1);
    CHECK(i == V9X_R6_ZEXPORT_PS_DWORDS);
    CHECK(v9x_r6_shader_gouraud_zexport_ps(got) ==
          V9X_R6_ZEXPORT_PS_DWORDS);
    CHECK(memcmp(got, want, i * sizeof(v9x_u32)) == 0);
    /* By hand: export base 61 with type PIXEL 0 and R1 << 15; EXPORT
     * (27h) with BARRIER. */
    CHECK(got[2] == (61ul | (1ul << 15)) && got[3] == 0x93800688ul);

    /* The Z-constant VS: VTX ADDR(8) CNT(2); ALU ADDR(4) CNT(2); POS0
     * from R1; PARAM0 from R0, the end; MOV R1.z = C0.x, MOV R1.w = C0.y
     * (unclamped, one group); padding; the Gouraud fetches at 16. With
     * write_w zero, only the first MOV, closing the group. */
    i = 0ul;
    want[i++] = 8ul;
    want[i++] = X_CF_DWORD1(0, 0, 0, X_I_COUNT(2), 0, 0, 0,
                            X_SQ_CF_INST_VTX, 0, 1);
    want[i++] = X_CF_ALU_DWORD0(4, 0, 0, 0);
    want[i++] = X_CF_ALU_DWORD1(0, 0, 0, X_I_COUNT(2), 0, X_SQ_CF_INST_ALU,
                                0, 1);
    i += x_export(want + i, X_CF_POS0, X_SQ_EXPORT_POS, 1,
                  X_SQ_CF_INST_EXPORT_DONE, 0, 1);
    i += x_export(want + i, 0, X_SQ_EXPORT_PARAM, 0,
                  X_SQ_CF_INST_EXPORT_DONE, 1, 0);
    want[i++] = X_ALU_DWORD0(X_ALU_SRC_CFILE_BASE + 0, 0, 0, 0, 0, 0, 0, 0,
                             0, 0, 0);
    want[i++] = X_R6XX_ALU_DWORD1_OP2(0, 0, 0, 0, 1, 0, 0,
                                      X_SQ_OP2_INST_MOV, 0, 1, 0, 2, 0);
    want[i++] = X_ALU_DWORD0(X_ALU_SRC_CFILE_BASE + 0, 0, 1, 0, 0, 0, 0, 0,
                             0, 0, 1);
    want[i++] = X_R6XX_ALU_DWORD1_OP2(0, 0, 0, 0, 1, 0, 0,
                                      X_SQ_OP2_INST_MOV, 0, 1, 0, 3, 0);
    while (i < 16ul) {
        want[i++] = 0ul;
    }
    i += x_fetch(want + i, 1, 0, 32, 1, X_FMT_32_32_32_32_FLOAT,
                 X_SQ_SEL_Z, X_SQ_SEL_W);
    i += x_fetch(want + i, 0, 16, 16, 0, X_FMT_32_32_32_32_FLOAT,
                 X_SQ_SEL_Z, X_SQ_SEL_W);
    CHECK(i == V9X_R6_ZCONST_VS_DWORDS);
    CHECK(v9x_r6_shader_gouraud_vs_zconst(1ul, got) ==
          V9X_R6_ZCONST_VS_DWORDS);
    CHECK(memcmp(got, want, i * sizeof(v9x_u32)) == 0);
    CHECK(fetch_order_ok(got, 16ul, 2ul) && fetch_counts_ok(got, 16ul, 2ul) &&
          fetch_mega_first(got, 16ul, 2ul));
    /* By hand: MOV R1.z (DST_ELEM 2 << 29 = 40000000h, R1 << 21). */
    CHECK(got[9] == 0x40201910ul && got[11] == 0x60201910ul);
    CHECK(v9x_r6_shader_gouraud_vs_zconst(0ul, got) ==
          V9X_R6_ZCONST_VS_DWORDS);
    CHECK(got[3] == want[3] - (1ul << 18) && got[8] == (want[8] | 0x80000000ul) &&
          got[10] == 0ul && got[11] == 0ul);

    /* The other VSs keep the rule too. */
    CHECK(v9x_r6_shader_solid_vs(got) == V9X_R6_SOLID_VS_DWORDS &&
          fetch_order_ok(got, 8ul, 1ul) && fetch_counts_ok(got, 8ul, 1ul) &&
          fetch_mega_first(got, 8ul, 1ul));
    CHECK(v9x_r6_shader_gouraud_vs(got) == V9X_R6_GOURAUD_VS_DWORDS &&
          fetch_order_ok(got, 8ul, 2ul) && fetch_counts_ok(got, 8ul, 2ul) &&
          fetch_mega_first(got, 8ul, 2ul));
}

/* xf86's NUM_FORMAT_ALL NORM and FORMAT_COMP_ALL unsigned (both 0), and
 * its TEX_NORMALIZED coordinate type (1). FMT_8_8_8_8 is the FMT_
 * enumeration's 26th. */
#define X_FMT_8_8_8_8            26
#define X_SQ_NUM_FORMAT_NORM     0
#define X_SQ_FORMAT_COMP_UNSIGNED 0
#define X_TEX_NORMALIZED         1

static void test_d3d_shaders(void)
{
    v9x_u32 got[V9X_R6_SHADER_DWORDS_MAX];
    v9x_u32 want[V9X_R6_SHADER_DWORDS_MAX];
    v9x_u32 textured;
    v9x_u32 i;

    /* The D3D VS: VTX ADDR(4) CNT(2 or 3); POS0 from R1; PARAM0 from R0
     * (EXPORT_DONE, the end, untextured; EXPORT textured); textured,
     * PARAM1 from R2, the end; R1 = sx sy sz rhw at 0, R2 = tu tv 0 1 at
     * 24, R0 = the D3DCOLOR at 16 as FMT_8_8_8_8 NORM unsigned - every
     * fetch a mega-fetch of 32, R0 last. */
    for (textured = 0ul; textured < 2ul; ++textured) {
        i = 0ul;
        want[i++] = 4ul;
        want[i++] = X_CF_DWORD1(0, 0, 0, X_I_COUNT(textured ? 3 : 2), 0, 0,
                                0, X_SQ_CF_INST_VTX, 0, 1);
        i += x_export(want + i, X_CF_POS0, X_SQ_EXPORT_POS, 1,
                      X_SQ_CF_INST_EXPORT_DONE, 0, 1);
        if (textured) {
            i += x_export(want + i, 0, X_SQ_EXPORT_PARAM, 0,
                          X_SQ_CF_INST_EXPORT, 0, 0);
            i += x_export(want + i, 1, X_SQ_EXPORT_PARAM, 2,
                          X_SQ_CF_INST_EXPORT_DONE, 1, 0);
        } else {
            i += x_export(want + i, 0, X_SQ_EXPORT_PARAM, 0,
                          X_SQ_CF_INST_EXPORT_DONE, 1, 0);
            want[i++] = 0ul;
            want[i++] = 0ul;
        }
        i += x_fetch(want + i, 1, 0, 32, 1, X_FMT_32_32_32_32_FLOAT,
                     X_SQ_SEL_Z, X_SQ_SEL_W);
        if (textured) {
            i += x_fetch(want + i, 2, 24, 32, 1, X_FMT_32_32_FLOAT,
                         X_SQ_SEL_0, X_SQ_SEL_1);
        }
        want[i++] = X_VTX_DWORD0(0, 0, 0, 0, 0, 0, X_SQ_SEL_X,
                                 X_MEGA_FETCH_COUNT(32));
        want[i++] = X_VTX_DWORD1_GPR(0, 0, X_SQ_SEL_X, X_SQ_SEL_Y,
                                     X_SQ_SEL_Z, X_SQ_SEL_W, 0,
                                     X_FMT_8_8_8_8, X_SQ_NUM_FORMAT_NORM,
                                     X_SQ_FORMAT_COMP_UNSIGNED, 0);
        want[i++] = X_VTX_DWORD2(16, 0, 0, 1);
        want[i++] = 0ul;
        CHECK(v9x_r6_shader_d3d_vs(textured, got) == i);
        CHECK(i <= V9X_R6_SHADER_DWORDS_MAX);
        CHECK(memcmp(got, want, i * sizeof(v9x_u32)) == 0);
        CHECK(fetch_order_ok(got, 8ul, textured ? 3ul : 2ul) &&
              fetch_counts_ok(got, 8ul, textured ? 3ul : 2ul) &&
              fetch_mega_first(got, 8ul, textured ? 3ul : 2ul));
    }
    /* By hand: the colour fetch's DWORD1, R0, X Y Z W, format 1Ah, NORM,
     * unsigned: 1Ah << 22 | the selects. */
    CHECK(got[17] == ((0x1aul << 22) | (0ul << 9) | (1ul << 12) |
                      (2ul << 15) | (3ul << 18)));

    /* NONE: the Gouraud PS. */
    i = v9x_r6_shader_gouraud_ps(want);
    CHECK(v9x_r6_shader_d3d_ps(V9X_R6_D3D_OP_NONE, got) == i &&
          memcmp(got, want, i * sizeof(v9x_u32)) == 0);
    /* MODULATE: the modulate PS but the sample normalised (COORD_TYPE_X
     * and _Y). */
    i = v9x_r6_shader_modulate_ps(want);
    want[17] = X_TEX_DWORD1(1, 0, X_SQ_SEL_X, X_SQ_SEL_Y, X_SQ_SEL_Z,
                            X_SQ_SEL_W, 0, X_TEX_NORMALIZED,
                            X_TEX_NORMALIZED, X_TEX_UNNORMALIZED,
                            X_TEX_UNNORMALIZED);
    CHECK(v9x_r6_shader_d3d_ps(V9X_R6_D3D_OP_MODULATE, got) == i &&
          memcmp(got, want, i * sizeof(v9x_u32)) == 0);
    /* REPLACE: TEX ADDR(4) CNT(1); R1 to MRT 0, the end; padding; SAMPLE
     * R1 = t0 at R1.xy01, normalised. */
    i = 0ul;
    want[i++] = 4ul;
    want[i++] = X_CF_DWORD1(0, 0, 0, X_I_COUNT(1), 0, 0, 0,
                            X_SQ_CF_INST_TEX, 0, 1);
    i += x_export(want + i, 0, X_SQ_EXPORT_PIXEL, 1,
                  X_SQ_CF_INST_EXPORT_DONE, 1, 1);
    while (i < 8ul) {
        want[i++] = 0ul;
    }
    want[i++] = X_TEX_DWORD0(X_SQ_TEX_INST_SAMPLE, 0, 0, 0, 1, 0, 0);
    want[i++] = X_TEX_DWORD1(1, 0, X_SQ_SEL_X, X_SQ_SEL_Y, X_SQ_SEL_Z,
                             X_SQ_SEL_W, 0, X_TEX_NORMALIZED,
                             X_TEX_NORMALIZED, X_TEX_UNNORMALIZED,
                             X_TEX_UNNORMALIZED);
    want[i++] = X_TEX_DWORD2(0, 0, 0, 0, X_SQ_SEL_X, X_SQ_SEL_Y,
                             X_SQ_SEL_0, X_SQ_SEL_1);
    want[i++] = 0ul;
    CHECK(v9x_r6_shader_d3d_ps(V9X_R6_D3D_OP_REPLACE, got) == i &&
          memcmp(got, want, i * sizeof(v9x_u32)) == 0);

    /* The lit VS textured: VTX ADDR(6) CNT(4); POS0 R1; PARAM0 R0, PARAM1
     * R2 (EXPORT); PARAM2 R3 (EXPORT_DONE, the end); fetches at 12: R1,
     * R2, R3 = specular at 20, R0 = colour at 16, both NORM unsigned. */
    for (textured = 0ul; textured < 2ul; ++textured) {
        v9x_u32 fetch = textured ? 12ul : 8ul;
        v9x_u32 count = textured ? 4ul : 3ul;

        i = v9x_r6_shader_d3d_vs_lit(textured, got);
        CHECK(i == fetch + count * 4ul && i <= V9X_R6_SHADER_DWORDS_MAX);
        CHECK(got[0] == fetch / 2ul &&
              got[1] == X_CF_DWORD1(0, 0, 0, X_I_COUNT(count), 0, 0, 0,
                                    X_SQ_CF_INST_VTX, 0, 1));
        /* The last export: PARAM (2 << 13) base 1 or 2 from R3. */
        CHECK(got[textured ? 8ul : 6ul] ==
                  ((textured ? 2ul : 1ul) | (2ul << 13) | (3ul << 15)));
        CHECK(fetch_order_ok(got, fetch, count) &&
              fetch_counts_ok(got, fetch, count) &&
              fetch_mega_first(got, fetch, count));
        /* The last two fetches: specular into R3 at 20, colour into R0 at
         * 16, format 1Ah NORM unsigned. */
        CHECK(got[i - 7ul] == ((0x1aul << 22) | 3ul | (1ul << 12) |
                               (2ul << 15) | (3ul << 18)) &&
              (got[i - 6ul] & 0xfffful) == 20ul);
        CHECK(got[i - 3ul] == ((0x1aul << 22) | (1ul << 12) | (2ul << 15) |
                               (3ul << 18)) &&
              (got[i - 2ul] & 0xfffful) == 16ul);
    }
}

/* The lit PS: the CF words as the layout says, the ALU clause's COUNT
 * covering its literal pairs, and the key's KILLGT. */
static void test_d3d_lit_ps(void)
{
    struct v9x_r6_d3d_ps ps;
    v9x_u32 got[V9X_R6_LIT_PS_DWORDS_MAX];
    v9x_u32 count;
    v9x_u32 tex_at;
    v9x_u32 at;
    v9x_u32 kills = 0ul;

    memset(&ps, 0, sizeof(ps));
    ps.op = V9X_R6_D3D_OP_MODULATE;
    ps.specular = 1ul;
    ps.fog = 1ul;
    ps.fog_literal[0] = 0x3f800000ul;
    ps.color_key = 1ul;
    ps.key[2] = 0x3f800000ul;
    ps.key_tolerance = 0x3c000000ul;
    count = v9x_r6_shader_d3d_ps_lit(&ps, got);
    CHECK(count <= V9X_R6_LIT_PS_DWORDS_MAX);
    /* ALU dwords: the key's group 6 + 4 literal, 2 MAX 4, KILLGT 2 + 2
     * literal; MUL 8; ADD 6; fog 6 + 4, MUL 6, 6 + 4: 58 from 8, so the
     * texture clause at 68 (on 16 bytes) and COUNT 29 (Mesa's ndw / 2,
     * literals included). EXPORT_DONE PIXEL 0 from R0 with BARRIER and
     * the end: 94200688h. */
    tex_at = 68ul;
    CHECK(count == tex_at + 4ul);
    CHECK(got[0] == tex_at / 2ul &&
          got[1] == X_CF_DWORD1(0, 0, 0, X_I_COUNT(1), 0, 0, 0,
                                X_SQ_CF_INST_TEX, 0, 1));
    CHECK(got[2] == 4ul &&
          got[3] == X_CF_ALU_DWORD1(0, 0, 0, X_I_COUNT(29), 0,
                                    X_SQ_CF_INST_ALU, 0, 1));
    CHECK(got[4] == 0ul && got[5] == 0x94200688ul);
    /* The sample: R1 = t0 at R1, normalised. */
    CHECK(got[tex_at] == 0x00010010ul &&
          got[tex_at + 1ul] == (0x30000000ul | 1ul | (1ul << 12) |
                                (2ul << 15) | (3ul << 18)));
    /* The key's first slot: ADD R3.x = R1.x - L.x (SRC1 253, NEG). */
    CHECK(got[8] == (1ul | (253ul << 13) | (1ul << 25)) &&
          got[9] == (0x10ul | (3ul << 21)));
    for (at = 8ul; at + 1ul < tex_at; at += 2ul) {
        if (((got[at + 1ul] >> 8) & 0x3fful) == 0x2dul &&
            (got[at] & 0x1fful) == 253ul) {
            ++kills;
            CHECK((got[at + 1ul] & 0x10ul) == 0ul &&
                  got[at + 2ul] == 0x3c000000ul);
        }
    }
    CHECK(kills == 1ul);

    /* Specular alone, untextured: no texture clause; ALU 3 slots, R1 the
     * specular, ADD R0.c = R0.c + R1.c clamped. */
    memset(&ps, 0, sizeof(ps));
    ps.op = V9X_R6_D3D_OP_NONE;
    ps.specular = 1ul;
    count = v9x_r6_shader_d3d_ps_lit(&ps, got);
    CHECK(count == 14ul);
    CHECK(got[0] == 4ul &&
          got[1] == X_CF_ALU_DWORD1(0, 0, 0, X_I_COUNT(3), 0,
                                    X_SQ_CF_INST_ALU, 0, 1) &&
          got[2] == 0ul && got[3] == 0x94200688ul);
    CHECK(got[8] == (0ul | (1ul << 13)) &&
          got[9] == (0x80000010ul));
}

/*
 * The state words and registers. Each expected value is written with
 * Mesa r600d.h's field positions (S_0288xx_*), beside xf86's names.
 */
static void test_state_words(void)
{
    /* S_028800_Z_ENABLE(1) | S_028800_Z_WRITE_ENABLE(1) |
     * S_028800_ZFUNC(FRAG_LESS 1). */
    CHECK(v9x_r6_3d_depth_control(1ul, 1ul, V9X_R6_FUNC_LESS) ==
          ((1ul << 1) | (1ul << 2) | (1ul << 4)));
    CHECK(v9x_r6_3d_depth_control(0ul, 0ul, V9X_R6_FUNC_ALWAYS) ==
          (7ul << 4));
    /* COLOR_SRCBLEND 0, COLOR_DESTBLEND 8, ALPHA_SRCBLEND 16,
     * ALPHA_DESTBLEND 24; BLEND_SRC_ALPHA 4, ONE_MINUS_SRC_ALPHA 5. */
    CHECK(v9x_r6_3d_blend(1ul, V9X_R6_BLEND_SRC_ALPHA,
                          V9X_R6_BLEND_ONE_MINUS_SRC_ALPHA) ==
          (4ul | (5ul << 8) | (4ul << 16) | (5ul << 24)));
    CHECK(v9x_r6_3d_blend(0ul, V9X_R6_BLEND_SRC_ALPHA,
                          V9X_R6_BLEND_ONE_MINUS_SRC_ALPHA) ==
          (1ul | (1ul << 16)));
    /* S_028410_ALPHA_FUNC(7) | S_028410_ALPHA_TEST_ENABLE(1). */
    CHECK(v9x_r6_3d_alpha_test(1ul, V9X_R6_FUNC_ALWAYS) == (7ul | 8ul));
    /* R600PrepareCopy's sampler: CLAMP_LAST_TEXEL in X and Y, WRAP in Z,
     * POINT: 12h. Bilinear: XY_MAG_FILTER at 9, XY_MIN_FILTER at 12. */
    CHECK(v9x_r6_3d_sampler(0ul, 0ul, V9X_R6_TEX_CLAMP_LAST_TEXEL,
                            V9X_R6_TEX_CLAMP_LAST_TEXEL) == 0x12ul);
    CHECK(v9x_r6_3d_sampler(1ul, 1ul, V9X_R6_TEX_WRAP, V9X_R6_TEX_WRAP) ==
          ((1ul << 9) | (1ul << 12)));
}

static void base_state(struct v9x_r6_3d_state *state)
{
    memset(state, 0, sizeof(*state));
    state->target_gpu = WINDOW_GPU + 0x7000ul;
    state->target_pitch = 64ul;
    state->target_height = 8ul;
    state->target_format = V9X_R6_COLOR_8_8_8_8;
    state->write_alpha = 1ul;
    state->scissor_right = 64ul;
    state->scissor_bottom = 8ul;
    state->blend_control = v9x_r6_3d_blend(0ul, 0ul, 0ul);
    state->alpha_test_control = v9x_r6_3d_alpha_test(0ul,
                                                     V9X_R6_FUNC_ALWAYS);
    state->perspective = 1ul;
    state->pixel_centre_half = 1ul;
    state->vs_gpu = WINDOW_GPU + 0x5000ul;
    state->vs_gprs = V9X_R6_GOURAUD_VS_GPRS;
    state->ps_gpu = WINDOW_GPU + 0x5200ul;
    state->ps_gprs = V9X_R6_GOURAUD_PS_GPRS;
    state->interpolants = 1ul;
    state->vertex_gpu = WINDOW_GPU + 0x5400ul;
    state->vertex_stride = V9X_R6_GOURAUD_STRIDE;
    state->vertex_bytes = 6ul * V9X_R6_GOURAUD_STRIDE;
    state->vertex_cache = 1ul;
}

static void test_build_state(void)
{
    struct v9x_r6_pm4 stream;
    struct v9x_r6_3d_state state;
    v9x_u32 at;

    /* Gouraud: one interpolant, no depth, no texture. */
    base_state(&state);
    v9x_r6_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    CHECK(v9x_r6_3d_build_state(&state, &stream) == V9X_STATUS_OK);
    CHECK(walks(buffer, stream.count));
    /* Shaders as the solid draw sets them: SQ_PGM_START_VS 28858h,
     * RESOURCES_VS 28868h, START_PS 28840h, RESOURCES_PS 28850h with
     * UNCACHED_FIRST_INST (bit 28), EXPORTS_PS 28854h 2. */
    at = find_reg(buffer, stream.count, 0x28858ul);
    CHECK(at < stream.count && buffer[at] == (WINDOW_GPU + 0x5000ul) >> 8);
    at = find_reg(buffer, stream.count, 0x28868ul);
    CHECK(at < stream.count && buffer[at] == 2ul);
    at = find_reg(buffer, stream.count, 0x28850ul);
    CHECK(at < stream.count && buffer[at] == (1ul | (1ul << 28)));
    /* SPI (r600d.h R_028644, R_028614, R_0286C4, R_0286CC):
     * S_028644_SEMANTIC(0) | DEFAULT_VAL(1) | SEL_CENTROID(1);
     * VS_EXPORT_COUNT(0); NUM_INTERP(1) | PERSP_GRADIENT_ENA(1), then
     * SPI_PS_IN_CONTROL_1 and SPI_INTERP_CONTROL_0 0, as r600_set_spi. */
    at = find_reg(buffer, stream.count, 0x28644ul);
    CHECK(at < stream.count && buffer[at] == ((1ul << 8) | (1ul << 11)));
    CHECK(find_reg(buffer, stream.count, 0x28648ul) == stream.count);
    at = find_reg(buffer, stream.count, 0x28614ul);
    CHECK(at < stream.count && buffer[at] == 0ul);
    at = find_reg(buffer, stream.count, 0x286c4ul);
    CHECK(at < stream.count && buffer[at] == 0ul);
    at = find_reg(buffer, stream.count, 0x286ccul);
    CHECK(at < stream.count && buffer[at] == (1ul | (1ul << 28)) &&
          buffer[at + 1ul] == 0ul && buffer[at + 2ul] == 0ul &&
          buffer[at - 2ul] == XF86_PACK3(0x69, 4));
    /* PA_CL_VTE_CNTL S_028818_VTX_XY_FMT(1) | VTX_Z_FMT(1); PA_SU_VTX_CNTL
     * PIX_CENTER_HALF | ROUND_MODE(2) | QUANT_MODE(5): xf86's 2Dh. */
    at = find_reg(buffer, stream.count, 0x28818ul);
    CHECK(at < stream.count && buffer[at] == ((1ul << 8) | (1ul << 9)));
    at = find_reg(buffer, stream.count, 0x28c08ul);
    CHECK(at < stream.count && buffer[at] == 0x2dul);
    /* No texture: no SET_RESOURCE at 38000h, no sampler. */
    CHECK(find_reg(buffer, stream.count, 0x38000ul) == stream.count);
    CHECK(find_reg(buffer, stream.count, 0x3c000ul) == stream.count);
    /* The colour buffer as the solid draw's, then all four channels, ROP
     * copy without blending, ONE ZERO. */
    at = find_reg(buffer, stream.count, 0x28040ul);
    CHECK(at < stream.count && buffer[at] == (WINDOW_GPU + 0x7000ul) >> 8 &&
          buffer[at + 1ul] == XF86_PACK3(0x73, 1) && buffer[at + 2ul] == 2ul);
    at = find_reg(buffer, stream.count, 0x280a0ul);
    CHECK(at < stream.count &&
          buffer[at] == ((0x1aul << 2) | (1ul << 16) | (1ul << 20) |
                         (1ul << 27)));
    at = find_reg(buffer, stream.count, 0x28060ul);
    CHECK(at < stream.count && buffer[at] == (7ul | (7ul << 10)));
    at = find_reg(buffer, stream.count, 0x28238ul);
    CHECK(at < stream.count && buffer[at] == 0xful);
    at = find_reg(buffer, stream.count, 0x28808ul);
    CHECK(at < stream.count && buffer[at] == 0x00cc0000ul);
    at = find_reg(buffer, stream.count, 0x28780ul);
    CHECK(at < stream.count && buffer[at] == 0x00010001ul);
    /* DB_SHADER_CONTROL S_02880C_Z_ORDER(EARLY_Z_THEN_LATE_Z) |
     * DUAL_EXPORT_ENABLE(1): the default state's 210h.
     * DB_RENDER_OVERRIDE S_028D10_FORCE_HIZ_ENABLE(2) | HIS_ENABLE0(2) |
     * HIS_ENABLE1(2) | FORCE_SHADER_Z_ORDER(1). No depth buffer:
     * DB_DEPTH_INFO DEPTH_INVALID, DB_DEPTH_CONTROL 0. */
    at = find_reg(buffer, stream.count, 0x2880cul);
    CHECK(at < stream.count && buffer[at] == ((1ul << 4) | (1ul << 9)));
    at = find_reg(buffer, stream.count, 0x28d10ul);
    CHECK(at < stream.count &&
          buffer[at] == (2ul | (2ul << 2) | (2ul << 4) | (1ul << 6)));
    at = find_reg(buffer, stream.count, 0x28010ul);
    CHECK(at < stream.count && buffer[at] == 0ul);
    CHECK(find_reg(buffer, stream.count, 0x28000ul) == stream.count);
    /* The DB registers no default state wrote, cleared as Mesa clears
     * them (r600d.h R_028D24_DB_HTILE_SURFACE, R_028D28_DB_SRESULTS_
     * COMPARE_STATE0, R_028D2C_..._STATE1, R_028D30_DB_PRELOAD_CONTROL;
     * xf86 r600_reg_r6xx.h / r600_reg_auto_r6xx.h the same offsets). */
    at = find_reg(buffer, stream.count, 0x28d24ul);
    CHECK(at < stream.count && buffer[at] == 0ul);
    at = find_reg(buffer, stream.count, 0x28d28ul);
    CHECK(at < stream.count && buffer[at] == 0ul &&
          buffer[at + 1ul] == 0ul && buffer[at + 2ul] == 0ul &&
          buffer[at - 2ul] == XF86_PACK3(0x69, 4));
    at = find_reg(buffer, stream.count, 0x28800ul);
    CHECK(at < stream.count && buffer[at] == 0ul);
    /* R_0282D0_PA_SC_VPORT_ZMIN_0 0, R_0282D4_PA_SC_VPORT_ZMAX_0 1.0. */
    at = find_reg(buffer, stream.count, 0x282d0ul);
    CHECK(at < stream.count && buffer[at] == 0ul &&
          buffer[at + 1ul] == float_of(1.0f));
    /* SX_ALPHA_TEST_CONTROL ALWAYS, off; SX_ALPHA_REF 0. */
    at = find_reg(buffer, stream.count, 0x28410ul);
    CHECK(at < stream.count && buffer[at] == 7ul);
    at = find_reg(buffer, stream.count, 0x28438ul);
    CHECK(at < stream.count && buffer[at] == 0ul);
    /* Scissors. */
    at = find_reg(buffer, stream.count, 0x28030ul);
    CHECK(at < stream.count && buffer[at] == 0ul &&
          buffer[at + 1ul] == (64ul | (8ul << 16)));
    at = find_reg(buffer, stream.count, 0x28240ul);
    CHECK(at < stream.count && buffer[at] == 0x80000000ul &&
          buffer[at + 1ul] == (64ul | (8ul << 16)));
    at = find_reg(buffer, stream.count, 0x28204ul);
    CHECK(at < stream.count && buffer[at] == 0x80000000ul);
    /* The vertex resource, stride 32, synced through the vertex cache;
     * the last packet of the state. */
    at = find_reg(buffer, stream.count, 0x39180ul);
    CHECK(at < stream.count && buffer[at] == WINDOW_GPU + 0x5400ul &&
          buffer[at + 1ul] == 6ul * 32ul - 1ul &&
          buffer[at + 2ul] == (32ul << 8) && buffer[at + 3ul] == 1ul &&
          buffer[at + 6ul] == 0xc0000000ul && at + 7ul == stream.count);
    /* The vertex and texture caches invalidated over all of memory
     * (Mesa r600_flush_emit, Linux r600_fence_ring_emit: VC_ACTION bit 24,
     * TC_ACTION bit 23, size FFFFFFFFh, base 0): a ranged VC sync left
     * the previous run's vertices in the cache on M-NZHWBR. */
    CHECK(buffer[at - 7ul] == XF86_PACK3(0x43, 4) &&
          buffer[at - 6ul] == ((1ul << 24) | (1ul << 23)) &&
          buffer[at - 5ul] == 0xfffffffful && buffer[at - 4ul] == 0ul);
    /* With ranged_syncs, xf86's form: VC alone over the 192 bytes. */
    state.ranged_syncs = 1ul;
    v9x_r6_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    CHECK(v9x_r6_3d_build_state(&state, &stream) == V9X_STATUS_OK);
    at = find_reg(buffer, stream.count, 0x39180ul);
    CHECK(at < stream.count && buffer[at - 6ul] == (1ul << 24) &&
          buffer[at - 5ul] == 1ul &&
          buffer[at - 4ul] == (WINDOW_GPU + 0x5400ul) >> 8);
    /* No vertex cache: the texture cache alone. */
    state.ranged_syncs = 0ul;
    state.vertex_cache = 0ul;
    v9x_r6_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    CHECK(v9x_r6_3d_build_state(&state, &stream) == V9X_STATUS_OK);
    at = find_reg(buffer, stream.count, 0x39180ul);
    CHECK(at < stream.count && buffer[at - 6ul] == (1ul << 23) &&
          buffer[at - 5ul] == 0xfffffffful);

    /* Depth: Z16 1D tiled, LESS with writes. */
    base_state(&state);
    state.has_depth = 1ul;
    state.depth_gpu = WINDOW_GPU + 0x9000ul;
    state.depth_pitch = 64ul;
    state.depth_height = 8ul;
    state.depth_control = v9x_r6_3d_depth_control(1ul, 1ul,
                                                  V9X_R6_FUNC_LESS);
    v9x_r6_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    CHECK(v9x_r6_3d_build_state(&state, &stream) == V9X_STATUS_OK);
    CHECK(walks(buffer, stream.count));
    /* S_028000_PITCH_TILE_MAX(64 / 8 - 1) | S_028000_SLICE_TILE_MAX(64 *
     * 8 / 64 - 1); DB_DEPTH_VIEW 0; DB_DEPTH_BASE; DB_DEPTH_INFO
     * S_028010_FORMAT(V_028010_DEPTH_16) |
     * S_028010_ARRAY_MODE(V_0280A0_ARRAY_1D_TILED_THIN1), the mode at bit
     * 15 (xf86 DB_DEPTH_INFO__ARRAY_MODE_shift 15); DB_PREFETCH_LIMIT 8 /
     * 8 - 1; SURFACE_BASE_UPDATE DEPTH_BASE (1). */
    at = find_reg(buffer, stream.count, 0x28000ul);
    CHECK(at < stream.count && buffer[at] == (7ul | (7ul << 10)) &&
          buffer[at + 1ul] == 0ul);
    at = find_reg(buffer, stream.count, 0x2800cul);
    CHECK(at < stream.count && buffer[at] == (WINDOW_GPU + 0x9000ul) >> 8 &&
          buffer[at + 1ul] == (1ul | (2ul << 15)));
    at = find_reg(buffer, stream.count, 0x28d34ul);
    CHECK(at < stream.count && buffer[at] == 0ul &&
          buffer[at + 1ul] == XF86_PACK3(0x73, 1) && buffer[at + 2ul] == 1ul);
    at = find_reg(buffer, stream.count, 0x28800ul);
    CHECK(at < stream.count && buffer[at] == 0x16ul);
    /* PA_SU_POLY_OFFSET_DB_FMT_CNTL S_028DF8_POLY_OFFSET_NEG_NUM_DB_BITS
     * ((char)-16), Mesa's for Z16. */
    at = find_reg(buffer, stream.count, 0x28df8ul);
    CHECK(at < stream.count && buffer[at] == 0xf0ul);
    /* Refused: a depth pitch not a multiple of 16 or a height not of 8. */
    state.depth_pitch = 72ul;
    CHECK(v9x_r6_3d_build_state(&state, &stream) ==
          V9X_STATUS_INVALID_ARGUMENT);
    state.depth_pitch = 64ul;
    state.depth_height = 12ul;
    CHECK(v9x_r6_3d_build_state(&state, &stream) ==
          V9X_STATUS_INVALID_ARGUMENT);

    /* Modulate: two interpolants and a 64x8 ARGB8888 texture. */
    base_state(&state);
    state.vs_gprs = V9X_R6_MODULATE_VS_GPRS;
    state.ps_gprs = V9X_R6_MODULATE_PS_GPRS;
    state.interpolants = 2ul;
    state.has_texture = 1ul;
    state.texture_gpu = WINDOW_GPU + 0x8000ul;
    state.texture_width = 64ul;
    state.texture_height = 8ul;
    state.texture_pitch = 64ul;
    state.texture_format = V9X_R6_COLOR_8_8_8_8;
    state.sampler_word0 = v9x_r6_3d_sampler(0ul, 0ul,
                                            V9X_R6_TEX_CLAMP_LAST_TEXEL,
                                            V9X_R6_TEX_CLAMP_LAST_TEXEL);
    state.vertex_stride = V9X_R6_MODULATE_STRIDE;
    state.vertex_bytes = 6ul * V9X_R6_MODULATE_STRIDE;
    v9x_r6_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    CHECK(v9x_r6_3d_build_state(&state, &stream) == V9X_STATUS_OK);
    CHECK(walks(buffer, stream.count));
    /* Inputs 0 and 1 (900h, 901h, the default state's), SEMANTIC_1(1),
     * VS_EXPORT_COUNT(1), NUM_INTERP(2). */
    at = find_reg(buffer, stream.count, 0x28648ul);
    CHECK(at < stream.count && buffer[at] == 0x901ul);
    at = find_reg(buffer, stream.count, 0x28614ul);
    CHECK(at < stream.count && buffer[at] == 0x100ul);
    at = find_reg(buffer, stream.count, 0x286c4ul);
    CHECK(at < stream.count && buffer[at] == 2ul);
    at = find_reg(buffer, stream.count, 0x286ccul);
    CHECK(at < stream.count && buffer[at] == (2ul | (1ul << 28)));
    /* r600_set_tex_resource for R600PrepareCopy's 32-bit source, id 0
     * (SET_RESOURCE, body offset 0): S_038000_DIM(2D 1) |
     * S_038000_TILE_MODE(LINEAR_ALIGNED 1) | S_038000_PITCH(64 / 8 - 1) |
     * S_038000_TEX_WIDTH(63); S_038004_TEX_HEIGHT(7) |
     * S_038004_DATA_FORMAT(FMT_8_8_8_8 1Ah); base and mip base / 256;
     * S_038010_REQUEST_SIZE(1) | DST_SEL_X(Z) | _Y(Y) | _Z(X) | _W(W);
     * LAST_LEVEL 0; S_038018_TYPE(VALID_TEXTURE 2). The texture cache
     * flushed over its 2 KiB first. */
    at = find_reg(buffer, stream.count, 0x38000ul);
    CHECK(at < stream.count && buffer[at - 2ul] == XF86_PACK3(0x6d, 8) &&
          buffer[at - 1ul] == 0ul);
    CHECK(at < stream.count &&
          buffer[at] == (1ul | (1ul << 3) | (7ul << 8) | (63ul << 19)));
    CHECK(at < stream.count && buffer[at + 1ul] == (7ul | (0x1aul << 26)));
    CHECK(at < stream.count &&
          buffer[at + 2ul] == (WINDOW_GPU + 0x8000ul) >> 8 &&
          buffer[at + 3ul] == (WINDOW_GPU + 0x8000ul) >> 8);
    CHECK(at < stream.count &&
          buffer[at + 4ul] == ((1ul << 14) | (2ul << 16) | (1ul << 19) |
                               (0ul << 22) | (3ul << 25)));
    CHECK(at < stream.count && buffer[at + 5ul] == 0ul &&
          buffer[at + 6ul] == (2ul << 30));
    CHECK(at >= 7ul && buffer[at - 7ul] == XF86_PACK3(0x43, 4) &&
          buffer[at - 6ul] == (1ul << 23) &&
          buffer[at - 5ul] == 0xfffffffful && buffer[at - 4ul] == 0ul);
    /* With ranged_syncs, xf86's: over the texture's 2 KiB. */
    state.ranged_syncs = 1ul;
    v9x_r6_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    CHECK(v9x_r6_3d_build_state(&state, &stream) == V9X_STATUS_OK);
    at = find_reg(buffer, stream.count, 0x38000ul);
    CHECK(at >= 7ul && buffer[at - 6ul] == (1ul << 23) &&
          buffer[at - 5ul] == 8ul &&
          buffer[at - 4ul] == (WINDOW_GPU + 0x8000ul) >> 8);
    state.ranged_syncs = 0ul;
    /* r600_set_tex_sampler: SET_SAMPLER (6Eh) at 0; WORD0 12h; WORD1 0
     * (LOD 0 to 0); WORD2 MC_COORD_TRUNCATE (bit 12) for point. */
    at = find_reg(buffer, stream.count, 0x3c000ul);
    CHECK(at < stream.count && buffer[at - 2ul] == XF86_PACK3(0x6e, 4) &&
          buffer[at - 1ul] == 0ul && buffer[at] == 0x12ul &&
          buffer[at + 1ul] == 0ul && buffer[at + 2ul] == (1ul << 12));
    /* Bilinear: no truncation. */
    state.sampler_word0 = v9x_r6_3d_sampler(1ul, 1ul, V9X_R6_TEX_WRAP,
                                            V9X_R6_TEX_WRAP);
    v9x_r6_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    CHECK(v9x_r6_3d_build_state(&state, &stream) == V9X_STATUS_OK);
    at = find_reg(buffer, stream.count, 0x3c008ul);
    CHECK(at < stream.count && buffer[at] == 0ul);
    /* Refused: a texture pitch not a multiple of 64. */
    state.texture_pitch = 96ul;
    v9x_r6_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    CHECK(v9x_r6_3d_build_state(&state, &stream) ==
          V9X_STATUS_INVALID_ARGUMENT && stream.count == 0ul);
    /* A 16-bit texture is refused in xf86's order, as is a 565 target. */
    state.texture_pitch = 128ul;
    state.texture_format = V9X_R6_COLOR_5_6_5;
    v9x_r6_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    CHECK(v9x_r6_3d_build_state(&state, &stream) ==
          V9X_STATUS_INVALID_ARGUMENT && stream.count == 0ul);

    /* Direct3D's order: a 565 target, SWAP_STD - S_0280A0_FORMAT(COLOR_5_6_5
     * 8) << 2, COMP_SWAP 0, BLEND_CLAMP (bit 20), SOURCE_FORMAT (bit 27) -
     * and a 565 texture read X, Y, Z, 1 (DST_SEL_W SQ_SEL_1 5), its pitch
     * 128 texels (256-byte group / 2 bytes), its sync over 2 bytes a texel. */
    state.d3d_order = 1ul;
    state.target_format = V9X_R6_COLOR_5_6_5;
    v9x_r6_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    CHECK(v9x_r6_3d_build_state(&state, &stream) == V9X_STATUS_OK);
    CHECK(walks(buffer, stream.count));
    at = find_reg(buffer, stream.count, 0x280a0ul);
    CHECK(at < stream.count &&
          buffer[at] == ((0x08ul << 2) | (1ul << 20) | (1ul << 27)));
    at = find_reg(buffer, stream.count, 0x38000ul);
    CHECK(at < stream.count &&
          buffer[at] == (1ul | (1ul << 3) | (15ul << 8) | (63ul << 19)) &&
          buffer[at + 1ul] == (7ul | (0x08ul << 26)) &&
          buffer[at + 4ul] == ((1ul << 14) | (0ul << 16) | (1ul << 19) |
                               (2ul << 22) | (5ul << 25)));
    /* 1555 and 8888 read X, Y, Z, W; the target's 8888 still SWAP_STD. */
    state.texture_format = V9X_R6_COLOR_1_5_5_5;
    state.target_format = V9X_R6_COLOR_8_8_8_8;
    v9x_r6_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    CHECK(v9x_r6_3d_build_state(&state, &stream) == V9X_STATUS_OK);
    at = find_reg(buffer, stream.count, 0x38000ul);
    CHECK(at < stream.count &&
          buffer[at + 4ul] == ((1ul << 14) | (0ul << 16) | (1ul << 19) |
                               (2ul << 22) | (3ul << 25)));
    at = find_reg(buffer, stream.count, 0x280a0ul);
    CHECK(at < stream.count &&
          buffer[at] == ((0x1aul << 2) | (1ul << 20) | (1ul << 27)));
    /* An opaque texture (XRGB8888) reads W as 1. */
    state.texture_format = V9X_R6_COLOR_8_8_8_8;
    state.texture_opaque = 1ul;
    v9x_r6_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    CHECK(v9x_r6_3d_build_state(&state, &stream) == V9X_STATUS_OK);
    at = find_reg(buffer, stream.count, 0x38000ul);
    CHECK(at < stream.count &&
          buffer[at + 4ul] == ((1ul << 14) | (0ul << 16) | (1ul << 19) |
                               (2ul << 22) | (5ul << 25)));
    state.texture_opaque = 0ul;
    /* A chain of 4 levels, trilinear: WORD3 MIP_ADDRESS level 1 / 256,
     * WORD5 LAST_LEVEL 3; sampler WORD0 MIP_FILTER(LINEAR 2) << 17, WORD1
     * MAX_LOD 3.0 in 4.6 (C0h) << 10. */
    state.texture_levels = 4ul;
    state.texture_mip_gpu = WINDOW_GPU + 0x9000ul;
    state.mip_filter = V9X_R6_MIP_LINEAR;
    v9x_r6_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    CHECK(v9x_r6_3d_build_state(&state, &stream) == V9X_STATUS_OK);
    at = find_reg(buffer, stream.count, 0x38000ul);
    CHECK(at < stream.count &&
          buffer[at + 2ul] == (WINDOW_GPU + 0x8000ul) >> 8 &&
          buffer[at + 3ul] == (WINDOW_GPU + 0x9000ul) >> 8 &&
          buffer[at + 5ul] == 3ul);
    at = find_reg(buffer, stream.count, 0x3c000ul);
    CHECK(at < stream.count &&
          buffer[at] == (state.sampler_word0 | (2ul << 17)) &&
          buffer[at + 1ul] == (0xc0ul << 10));
    /* A misaligned level 1, and a chain in xf86's order, are refused. */
    state.texture_mip_gpu = WINDOW_GPU + 0x9080ul;
    v9x_r6_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    CHECK(v9x_r6_3d_build_state(&state, &stream) ==
          V9X_STATUS_INVALID_ARGUMENT && stream.count == 0ul);
    state.texture_mip_gpu = WINDOW_GPU + 0x9000ul;
    state.d3d_order = 0ul;
    state.texture_format = V9X_R6_COLOR_8_8_8_8;
    v9x_r6_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    CHECK(v9x_r6_3d_build_state(&state, &stream) ==
          V9X_STATUS_INVALID_ARGUMENT && stream.count == 0ul);
    state.d3d_order = 1ul;
    state.texture_levels = 0ul;
    state.mip_filter = V9X_R6_MIP_NONE;
    state.texture_format = V9X_R6_COLOR_1_5_5_5;
    /* A 16-bit pitch of 64 texels is not 256 bytes: refused. */
    state.texture_pitch = 64ul;
    v9x_r6_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    CHECK(v9x_r6_3d_build_state(&state, &stream) ==
          V9X_STATUS_INVALID_ARGUMENT && stream.count == 0ul);
    /* The pitch rule itself. */
    CHECK(v9x_r6_3d_texture_pitch(V9X_R6_COLOR_8_8_8_8, 1ul) == 64ul &&
          v9x_r6_3d_texture_pitch(V9X_R6_COLOR_8_8_8_8, 65ul) == 128ul &&
          v9x_r6_3d_texture_pitch(V9X_R6_COLOR_4_4_4_4, 64ul) == 128ul &&
          v9x_r6_3d_texture_pitch(V9X_R6_COLOR_5_6_5, 129ul) == 256ul &&
          v9x_r6_3d_texture_pitch(0x13ul, 64ul) == 0ul);

    /* Blending, alpha test, kill, a write mask, linear interpolation. */
    base_state(&state);
    state.blend_enable = 1ul;
    state.blend_control = v9x_r6_3d_blend(1ul, V9X_R6_BLEND_SRC_ALPHA,
                                          V9X_R6_BLEND_ONE_MINUS_SRC_ALPHA);
    state.alpha_test_control = v9x_r6_3d_alpha_test(1ul, V9X_R6_FUNC_LESS);
    state.write_alpha = 0ul;
    state.masked_channels = V9X_R6_CHANNEL_GREEN;
    state.perspective = 0ul;
    state.pixel_centre_half = 0ul;
    v9x_r6_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    CHECK(v9x_r6_3d_build_state(&state, &stream) == V9X_STATUS_OK);
    /* CB_COLOR_CONTROL ROP3 CCh | PER_MRT_BLEND | TARGET_BLEND_ENABLE(1),
     * as r600_set_render_target for RV6xx. */
    at = find_reg(buffer, stream.count, 0x28808ul);
    CHECK(at < stream.count &&
          buffer[at] == (0x00cc0000ul | (1ul << 7) | (1ul << 8)));
    at = find_reg(buffer, stream.count, 0x28238ul);
    CHECK(at < stream.count && buffer[at] == 0x5ul);
    /* Alpha test on: LATE_Z (Z_ORDER 0). */
    at = find_reg(buffer, stream.count, 0x2880cul);
    CHECK(at < stream.count && buffer[at] == (1ul << 9));
    /* S_028644_SEL_LINEAR(1) and LINEAR_GRADIENT_ENA; PIX_CENTER off. */
    at = find_reg(buffer, stream.count, 0x28644ul);
    CHECK(at < stream.count && buffer[at] == (0x900ul | (1ul << 12)));
    at = find_reg(buffer, stream.count, 0x286ccul);
    CHECK(at < stream.count && buffer[at] == (1ul | (1ul << 29)));
    at = find_reg(buffer, stream.count, 0x28c08ul);
    CHECK(at < stream.count && buffer[at] == 0x2cul);
    state.kill = 1ul;
    state.alpha_test_control = 0ul;
    v9x_r6_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    CHECK(v9x_r6_3d_build_state(&state, &stream) == V9X_STATUS_OK);
    at = find_reg(buffer, stream.count, 0x2880cul);
    CHECK(at < stream.count && buffer[at] == ((1ul << 6) | (1ul << 9)));

    /* Refused: no interpolants, too many, another target format, a
     * scissor past the target, an overflowed stream. */
    base_state(&state);
    state.interpolants = 0ul;
    CHECK(v9x_r6_3d_build_state(&state, &stream) ==
          V9X_STATUS_INVALID_ARGUMENT);
    state.interpolants = V9X_R6_INTERPOLANTS_MAX + 1ul;
    CHECK(v9x_r6_3d_build_state(&state, &stream) ==
          V9X_STATUS_INVALID_ARGUMENT);
    base_state(&state);
    state.target_format = 0x08ul;
    CHECK(v9x_r6_3d_build_state(&state, &stream) ==
          V9X_STATUS_INVALID_ARGUMENT);
    base_state(&state);
    state.scissor_right = 72ul;
    CHECK(v9x_r6_3d_build_state(&state, &stream) ==
          V9X_STATUS_INVALID_ARGUMENT);
    base_state(&state);
    v9x_r6_pm4_begin(&stream, buffer, 40ul);
    CHECK(v9x_r6_3d_build_state(&state, &stream) ==
          V9X_STATUS_INSUFFICIENT_MEMORY);
}

static void test_draw_flush(void)
{
    struct v9x_r6_pm4 stream;

    /* r600_draw_auto with V_008958_DI_PT_TRILIST (4, xf86 DI_PT_TRILIST). */
    v9x_r6_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    v9x_r6_3d_draw(&stream, 18ul);
    CHECK(stream.count == 10ul && walks(buffer, stream.count));
    CHECK(buffer[0] == 0xc0016800ul &&
          buffer[1] == (0x8958ul - 0x8000ul) / 4ul && buffer[2] == 4ul);
    CHECK(buffer[3] == XF86_PACK3(0x2a, 1) && buffer[4] == 0ul &&
          buffer[5] == XF86_PACK3(0x2f, 1) && buffer[6] == 1ul &&
          buffer[7] == XF86_PACK3(0x2d, 2) && buffer[8] == 18ul &&
          buffer[9] == 2ul);

    /* CACHE_FLUSH_AND_INV_EVENT, WAIT_UNTIL 3D idle clean, SURFACE_SYNC of
     * CB_ACTION | CB0_DEST_BASE | TC_ACTION | VC_ACTION (bits 25, 6, 23,
     * 24), size all ones, base 0, poll 10: no DB action (bits 26, 14). */
    v9x_r6_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    v9x_r6_3d_flush(&stream);
    CHECK(stream.count == 10ul && walks(buffer, stream.count));
    CHECK(buffer[0] == XF86_PACK3(0x46, 1) && buffer[1] == 0x16ul);
    CHECK(buffer[4] == ((1ul << 15) | (1ul << 17)));
    CHECK(buffer[5] == XF86_PACK3(0x43, 4) &&
          buffer[6] == ((1ul << 25) | (1ul << 6) | (1ul << 23) |
                        (1ul << 24)) &&
          buffer[7] == 0xfffffffful && buffer[8] == 0ul && buffer[9] == 10ul);
    CHECK((buffer[6] & ((1ul << 26) | (1ul << 14))) == 0ul);
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
    test_interp_shaders();
    test_d3d_shaders();
    test_d3d_lit_ps();
    test_state_words();
    test_build_state();
    test_draw_flush();
    return failures;
}
