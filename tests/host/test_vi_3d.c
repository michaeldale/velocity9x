#include <stdio.h>
#include <string.h>

#include "velocity9x/vi_3d.h"
#include "velocity9x/vi_shader.h"
#include "velocity9x/vi_clear_state.h"
#include "velocity9x/vi_gpu_init.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

/* The encoders against LLVM's GFX8 assembler tests, the little-endian
 * bytes read as a dword (build\external\carrizo\llvm-mc\). */
static void test_encodings(void)
{
    v9x_u32 out[2];

    /* gfx8_asm_vop1.s: v_mov_b32 v5, s1 [01 02 0a 7e]; v5, 0 [80 02 0a 7e]. */
    CHECK(v9x_vi_vop1(V9X_VI_VOP1_MOV_B32, 5ul, 1ul) == 0x7e0a0201ul);
    CHECK(v9x_vi_vop1(V9X_VI_VOP1_MOV_B32, 5ul, V9X_VI_INLINE_INT(0)) ==
          0x7e0a0280ul);
    /* gfx8_asm_vop2.s: v_cndmask_b32 v5, v1, v2, vcc [01 05 0a 00]. */
    CHECK(v9x_vi_vop2(V9X_VI_VOP2_CNDMASK_B32, 5ul, V9X_VI_VGPR(1), 2ul) ==
          0x000a0501ul);
    /* gfx8_asm_vopc.s: v_cmp_eq_u32 vcc, s1, v2 [01 04 94 7d];
     * vcc, -1, v2 [c1 04 94 7d]. */
    CHECK(v9x_vi_vopc(V9X_VI_VOPC_CMP_EQ_U32, 1ul, 2ul) == 0x7d940401ul);
    CHECK(v9x_vi_vopc(V9X_VI_VOPC_CMP_EQ_U32, 0xc1ul, 2ul) == 0x7d9404c1ul);
    /* exp.s: exp mrt0 v4, v3, v2, v1 done [0f 08 00 c4 04 03 02 01]. */
    CHECK(v9x_vi_exp(V9X_VI_EXP_MRT0, 1, 0, 4ul, 3ul, 2ul, 1ul, out) == 2ul);
    CHECK(out[0] == 0xc400080ful);
    CHECK(out[1] == 0x01020304ul);
    /* gfx8_asm_exp.s: exp pos0 v0 x4 [cf 00 00 c4], param0 [0f 02 00 c4],
     * mrt0 vm [0f 10 00 c4]. */
    (void)v9x_vi_exp(V9X_VI_EXP_POS0, 0, 0, 0ul, 0ul, 0ul, 0ul, out);
    CHECK(out[0] == 0xc40000cful);
    CHECK(out[1] == 0ul);
    (void)v9x_vi_exp(V9X_VI_EXP_PARAM0, 0, 0, 0ul, 0ul, 0ul, 0ul, out);
    CHECK(out[0] == 0xc400020ful);
    (void)v9x_vi_exp(V9X_VI_EXP_MRT0, 0, 1, 0ul, 0ul, 0ul, 0ul, out);
    CHECK(out[0] == 0xc400100ful);
}

/* The two shaders word for word, as assembled by hand from those rules. */
static void test_shaders(void)
{
    static const v9x_u32 vs[V9X_VI_SOLID_VS_DWORDS] = {
        0x7d940081ul,   /* v_cmp_eq_u32 vcc, 1, v0 */
        0x7e040202ul,   /* v_mov_b32 v2, s2 (x2) */
        0x00020400ul,   /* v_cndmask_b32 v1, s0 (x1), v2, vcc */
        0x7d940082ul,   /* v_cmp_eq_u32 vcc, 2, v0 */
        0x7e060203ul,   /* v_mov_b32 v3, s3 (y2) */
        0x00060601ul,   /* v_cndmask_b32 v3, s1 (y1), v3, vcc */
        0x7e080280ul,   /* v_mov_b32 v4, 0 */
        0x7e0a02f2ul,   /* v_mov_b32 v5, 1.0 */
        0xc400020ful,   /* exp param0 v4, v4, v4, v4 */
        0x04040404ul,
        0xc40008cful,   /* exp pos0 v1, v3, v4, v5 done */
        0x05040301ul,
        0xbf810000ul    /* s_endpgm */
    };
    static const v9x_u32 ps[V9X_VI_SOLID_PS_DWORDS] = {
        0x7e000200ul,   /* v_mov_b32 v0, s0 */
        0x7e020201ul,   /* v_mov_b32 v1, s1 */
        0x7e040202ul,   /* v_mov_b32 v2, s2 */
        0x7e060203ul,   /* v_mov_b32 v3, s3 */
        0xc400180ful,   /* exp mrt0 v0, v1, v2, v3 done vm */
        0x03020100ul,
        0xbf810000ul    /* s_endpgm */
    };
    v9x_u32 out[16];

    CHECK(v9x_vi_shader_solid_vs(out) == V9X_VI_SOLID_VS_DWORDS);
    CHECK(memcmp(out, vs, sizeof(vs)) == 0);
    CHECK(v9x_vi_shader_solid_ps(out) == V9X_VI_SOLID_PS_DWORDS);
    CHECK(memcmp(out, ps, sizeof(ps)) == 0);
}

/* Every type-3 packet's count must land the next header exactly at the
 * stream's end. Returns the number of packets, 0 on a mismatch. */
static v9x_u32 walk(const v9x_u32 *dwords, v9x_u32 count)
{
    v9x_u32 at = 0ul;
    v9x_u32 packets = 0ul;

    while (at < count) {
        if ((dwords[at] >> 30) != 3ul) {
            return 0ul;
        }
        if (dwords[at] == V9X_VI_RING_NOP) {
            at += 1ul;
        } else {
            at += ((dwords[at] >> 16) & 0x3ffful) + 2ul;
        }
        ++packets;
    }
    return at == count ? packets : 0ul;
}

static v9x_u32 buffer[2048];

static void test_clear_state(void)
{
    v9x_u32 index;
    v9x_u32 total = 0ul;

    for (index = 0ul; index < V9X_VI_CLEAR_STATE_RUNS; ++index) {
        total += v9x_vi_clear_state_runs[index].count;
    }
    CHECK(total == V9X_VI_CLEAR_STATE_DWORDS);
    CHECK(v9x_vi_clear_state_runs[0].first == 0xa000ul);
    CHECK(v9x_vi_clear_state_runs[0].count == 212ul);
    CHECK(v9x_vi_clear_state_runs[6].first == 0xa2a5ul);
    CHECK(v9x_vi_clear_state_runs[6].count == 233ul);
}

static void test_start(void)
{
    struct v9x_vi_pm4 stream;

    v9x_vi_pm4_init(&stream, buffer, 2048ul);
    v9x_vi_3d_start(&stream);
    CHECK(stream.overflow == 0ul);
    CHECK(stream.count == V9X_VI_START_DWORDS);
    CHECK(walk(buffer, stream.count) != 0ul);
    CHECK(buffer[0] == 0xc0004a00ul);
    CHECK(buffer[1] == 0x20000000ul);
    CHECK(buffer[2] == 0xc0012800ul);
    /* The first context run: SET_CONTEXT_REG of 212 at offset 0. */
    CHECK(buffer[5] == V9X_VI_PACKET3(V9X_VI_P3_SET_CONTEXT_REG, 212ul));
    CHECK(buffer[6] == 0ul);
    /* The end: CLEAR_STATE, then SET_BASE CE partitions 8000h. */
    CHECK(buffer[stream.count - 6ul] == 0xc0001200ul);
    CHECK(buffer[stream.count - 4ul] == 0xc0021100ul);
    CHECK(buffer[stream.count - 3ul] == 3ul);
    CHECK(buffer[stream.count - 1ul] == 0x8000ul);
}

static void base_rect(struct v9x_vi_solid_rect *rect)
{
    memset(rect, 0, sizeof(*rect));
    rect->target_low = 0x0f809000ul;
    rect->target_high = 0xf4ul;
    rect->pitch_pixels = 64ul;
    rect->height = 16ul;
    rect->format = V9X_VI_CB_FORMAT_8_8_8_8;
    rect->tile_mode_index = V9X_VI_TILE_LINEAR_ALIGNED;
    rect->vs_low = 0x0f804000ul;
    rect->ps_low = 0x0f804100ul;
    rect->shader_high = 0xf4ul;
    rect->left = 8ul;
    rect->top = 4ul;
    rect->right = 40ul;
    rect->bottom = 12ul;
    rect->color[0] = 0x3f800000ul;
    rect->fence_low = 0x0f808100ul;
    rect->fence_high = 0xf4ul;
    rect->fence_value = 0x12345678ul;
}

/* Finds the first SET_CONTEXT_REG whose offset is reg's; returns the index
 * of its first value, 0 if none. */
static v9x_u32 find_context(const v9x_u32 *dwords, v9x_u32 count, v9x_u32 reg)
{
    v9x_u32 at = 0ul;

    while (at < count) {
        if ((dwords[at] & 0xff00ul) ==
                (V9X_VI_P3_SET_CONTEXT_REG << 8) &&
            dwords[at + 1ul] == reg - V9X_VI_CONTEXT_REG_START) {
            return at + 2ul;
        }
        at += dwords[at] == V9X_VI_RING_NOP ? 1ul :
              ((dwords[at] >> 16) & 0x3ffful) + 2ul;
    }
    return 0ul;
}

static void test_solid_rect(void)
{
    struct v9x_vi_pm4 stream;
    struct v9x_vi_solid_rect rect;
    v9x_u32 at;

    base_rect(&rect);
    v9x_vi_pm4_init(&stream, buffer, 2048ul);
    CHECK(v9x_vi_3d_solid_rect(&stream, &rect) == V9X_STATUS_OK);
    CHECK(stream.overflow == 0ul);
    CHECK(walk(buffer, stream.count) != 0ul);

    /* CB_COLOR0_BASE..ATTRIB: the 40-bit address >> 8, pitch and slice in
     * 8-pixel and 64-pixel tiles less one, INFO's format, the tile index. */
    at = find_context(buffer, stream.count, 0xa318ul);
    CHECK(at != 0ul);
    CHECK(buffer[at] == 0xf40f8090ul);
    CHECK(buffer[at + 1ul] == 7ul);
    CHECK(buffer[at + 2ul] == 15ul);
    CHECK(buffer[at + 4ul] == (V9X_VI_CB_FORMAT_8_8_8_8 << 2));
    CHECK(buffer[at + 5ul] == V9X_VI_TILE_LINEAR_ALIGNED);

    /* The VS user SGPRs: 8, 4, 40 and 12 as float bits. */
    at = 0ul;
    while (at < stream.count &&
           !((buffer[at] & 0xff00ul) == (V9X_VI_P3_SET_SH_REG << 8) &&
             buffer[at + 1ul] == 0x4cul)) {
        at += buffer[at] == V9X_VI_RING_NOP ? 1ul :
              ((buffer[at] >> 16) & 0x3ffful) + 2ul;
    }
    CHECK(at < stream.count);
    CHECK(buffer[at + 2ul] == 0x41000000ul);
    CHECK(buffer[at + 3ul] == 0x40800000ul);
    CHECK(buffer[at + 4ul] == 0x42200000ul);
    CHECK(buffer[at + 5ul] == 0x41400000ul);

    /* The tail: DRAW_INDEX_AUTO of 3 auto-indexed vertices, then the
     * end-of-pipe fence (Linux's ring_emit_fence_gfx with a 32-bit value). */
    CHECK(buffer[stream.count - 9ul] ==
          V9X_VI_PACKET3(V9X_VI_P3_DRAW_INDEX_AUTO, 1ul));
    CHECK(buffer[stream.count - 8ul] == 3ul);
    CHECK(buffer[stream.count - 7ul] == 2ul);
    CHECK(buffer[stream.count - 6ul] ==
          V9X_VI_PACKET3(V9X_VI_P3_EVENT_WRITE_EOP, 4ul));
    /* EOP_TCL1_ACTION_EN | EOP_TC_ACTION_EN | EOP_TC_WB_ACTION_EN |
     * EVENT_TYPE(CACHE_FLUSH_AND_INV_TS_EVENT) | EVENT_INDEX(5). */
    CHECK(buffer[stream.count - 5ul] == 0x00038514ul);
    CHECK(buffer[stream.count - 4ul] == 0x0f808100ul);
    CHECK(buffer[stream.count - 3ul] == (0xf4ul | (1ul << 29)));
    CHECK(buffer[stream.count - 2ul] == 0x12345678ul);
    CHECK(buffer[stream.count - 1ul] == 0ul);

    /* Refusals: a rectangle past the target, an odd pitch, an unaligned
     * shader. */
    base_rect(&rect);
    rect.right = 65ul;
    CHECK(v9x_vi_3d_solid_rect(&stream, &rect) == V9X_STATUS_INVALID_ARGUMENT);
    base_rect(&rect);
    rect.pitch_pixels = 60ul;
    CHECK(v9x_vi_3d_solid_rect(&stream, &rect) == V9X_STATUS_INVALID_ARGUMENT);
    base_rect(&rect);
    rect.ps_low = 0x0f804104ul;
    CHECK(v9x_vi_3d_solid_rect(&stream, &rect) == V9X_STATUS_INVALID_ARGUMENT);
}

/* The engine configuration list: Linux's tables in order, then gpu_init's
 * own writes, with the values Linux computes for Carrizo. */
static struct v9x_vi_reg_seq ops[V9X_VI_GPU_INIT_OPS_MAX];

static v9x_u32 find_op(v9x_u32 count, v9x_u32 offset, v9x_u32 from)
{
    v9x_u32 index;

    for (index = from; index < count; ++index) {
        if (ops[index].offset == offset) {
            return index;
        }
    }
    return count;
}

static void test_gpu_init(void)
{
    v9x_u32 count;
    v9x_u32 at;
    v9x_u32 vmid;

    count = v9x_vi_gpu_init_ops(ops, V9X_VI_GPU_INIT_OPS_MAX);
    /* 75 + 12 + 8 table entries, GRBM_CNTL, two address configs, 28 tile
     * and 15 macrotile modes, 16 VMIDs of select and four SH_MEM writes,
     * the select back, the broadcast and the FIFO sizes. */
    CHECK(count == 224ul);
    CHECK(v9x_vi_gpu_init_ops(ops, 100ul) == 0ul);

    /* The first entry is cz_mgcg_cgcg_init's RLC_CGTT_MGCG_OVERRIDE. */
    CHECK(ops[0].clear == 0xfffffffful && ops[0].set == 0xfffffffful);
    /* Golden settings' RLC_CGCG_CGLS_CTRL is a read-modify-write. */
    at = find_op(count, 0x3b124ul, 75ul);
    CHECK(at < count && ops[at].clear == 3ul && ops[at].set == 0x3cul);

    at = find_op(count, V9X_VI_GRBM_CNTL, 0ul);
    CHECK(at < count && ops[at].clear == 0xfful && ops[at].set == 0xfful);
    at = find_op(count, V9X_VI_HDP_ADDR_CONFIG, 0ul);
    CHECK(at < count && ops[at].set == 0x22010001ul);

    /* Tile modes: 0 and 8 as Linux's macros give them, 7 and 12 skipped;
     * macrotile 7 skipped. */
    at = find_op(count, V9X_VI_GB_TILE_MODE0, 0ul);
    CHECK(at < count && ops[at].set == 0x00800010ul);
    at = find_op(count, V9X_VI_GB_TILE_MODE0 + 8ul * 4ul, 0ul);
    CHECK(at < count && ops[at].set == 0x00000004ul);
    CHECK(find_op(count, V9X_VI_GB_TILE_MODE0 + 7ul * 4ul, 0ul) == count);
    CHECK(find_op(count, V9X_VI_GB_TILE_MODE0 + 12ul * 4ul, 0ul) == count);
    CHECK(find_op(count, V9X_VI_GB_MACROTILE_MODE0 + 7ul * 4ul, 0ul) ==
          count);

    /* SH_MEM_CONFIG: UC with unaligned access for VMID 0, NC for the
     * others, each behind its SRBM_GFX_CNTL select. */
    at = 0ul;
    for (vmid = 0ul; vmid < 16ul; ++vmid) {
        at = find_op(count, V9X_VI_SRBM_GFX_CNTL, at);
        CHECK(at < count && ops[at].set == (vmid << 4));
        at = find_op(count, V9X_VI_SH_MEM_CONFIG, at);
        CHECK(at < count && ops[at].set == (vmid == 0ul ? 0x378ul : 0x138ul));
    }
    at = find_op(count, V9X_VI_SRBM_GFX_CNTL, at);
    CHECK(at < count && ops[at].set == 0ul);

    /* The end: broadcast, then Carrizo's FIFO sizes 20h, 100h, 30h, 130h. */
    CHECK(ops[count - 2ul].offset == V9X_VI_GRBM_GFX_INDEX);
    CHECK(ops[count - 2ul].set == 0xe0000000ul);
    CHECK(ops[count - 1ul].offset == V9X_VI_PA_SC_FIFO_SIZE);
    CHECK(ops[count - 1ul].set == 0x98184020ul);
}

unsigned int v9x_run_vi_3d_tests(void)
{
    failures = 0u;
    test_gpu_init();
    test_encodings();
    test_shaders();
    test_clear_state();
    test_start();
    test_solid_rect();
    return failures;
}
