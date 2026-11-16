#include <stdio.h>
#include <string.h>

#include "velocity9x/si_gpu_init.h"
#include "velocity9x/si_3d.h"
#include "velocity9x/si_clear_state.h"
#include "velocity9x/vi_shader.h"
#include "velocity9x/cik_shader.h"
#include "velocity9x/si_shader.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

#define SI_OPS_MAX 512u

static struct v9x_vi_reg_seq ops[SI_OPS_MAX];

/* SICP.VXD's SCInitWindows, byte offsets [start, limit): the probe refuses
 * a table with any entry outside them, so every entry must be inside. */
static const v9x_u32 si_windows[][2] = {
    { 0x02f48ul, 0x02f4cul }, { 0x08000ul, 0x08020ul },
    { 0x08024ul, 0x0c000ul }, { 0x0c300ul, 0x0c32cul },
    { 0x0c334ul, 0x0c500ul }, { 0x0d0b8ul, 0x0d0bcul },
    { 0x0d8b8ul, 0x0d8bcul }, { 0x28000ul, 0x29000ul }
};

static int in_windows(v9x_u32 offset)
{
    v9x_u32 index;

    for (index = 0ul; index < sizeof(si_windows) / sizeof(si_windows[0]);
         ++index) {
        if (offset >= si_windows[index][0] && offset < si_windows[index][1]) {
            return 1;
        }
    }
    return 0;
}

/* The last entry for offset, or 0. */
static const struct v9x_vi_reg_seq *find_last(v9x_u32 count, v9x_u32 offset)
{
    const struct v9x_vi_reg_seq *found = 0;
    v9x_u32 index;

    for (index = 0ul; index < count; ++index) {
        if (ops[index].offset == offset) {
            found = &ops[index];
        }
    }
    return found;
}

static void test_gpu_init(void)
{
    const struct v9x_vi_reg_seq *entry;
    v9x_u32 count;
    v9x_u32 index;

    count = v9x_si_gpu_init_ops(ops, SI_OPS_MAX);
    /* 16 + 6 + 60 golden, 5 address configs, 32 tile modes, 2 for
     * setup_rb, 3 for setup_spi, 12 constants, 8 CB perfcounter selects. */
    CHECK(count == 82ul + 5ul + 32ul + 2ul + 3ul + 12ul + 8ul);
    CHECK(v9x_si_gpu_init_ops(ops, 10ul) == 0ul);
    CHECK(v9x_si_gpu_init_ops(0, SI_OPS_MAX) == 0ul);
    count = v9x_si_gpu_init_ops(ops, SI_OPS_MAX);

    /* The golden list starts with CB_HW_CONTROL's read-modify-write. */
    CHECK(ops[0].offset == 0x09a10ul && ops[0].clear == 0x00010000ul &&
          ops[0].set == 0x00018208ul);
    /* golden_rlc writes 02010002h; constants_init's 4 KiB row wins. */
    entry = find_last(count, 0x098f8ul);
    CHECK(entry != 0 && entry->set == 0x22010002ul &&
          entry->clear == 0xfffffffful);
    entry = find_last(count, 0x02f48ul);
    CHECK(entry != 0 && entry->set == 0x22010002ul);
    entry = find_last(count, 0x0d0b8ul);
    CHECK(entry != 0 && entry->set == 0x22010002ul);
    /* Tile mode 0: depth micro tiling, 2D thin, P4_8x16, 64 B split, 16
     * banks, bank height 4, macro aspect 4 (gfx_v6_0.c's CHIP_OLAND). */
    entry = find_last(count, 0x09910ul);
    CHECK(entry != 0 && entry->set == 0x003a0112ul);
    CHECK((entry->set & 3ul) == 2ul && ((entry->set >> 2) & 0xful) == 4ul &&
          ((entry->set >> 6) & 0x1ful) == 4ul);
    /* Tile mode 5 takes the row's split: 4 KiB is 6 at 13:11. */
    entry = find_last(count, 0x09910ul + 5ul * 4ul);
    CHECK(entry != 0 && ((entry->set >> 11) & 7ul) == 6ul);
    /* Modes 26-31 are cleared, as Linux's memset leaves them. */
    entry = find_last(count, 0x09910ul + 31ul * 4ul);
    CHECK(entry != 0 && entry->set == 0ul);
    /* Raster config: golden's 82h, then setup_rb's RB_YSEL alone. */
    entry = find_last(count, 0x28350ul);
    CHECK(entry != 0 && entry->set == 0x00000080ul &&
          entry->clear == 0xfffffffful);
    /* setup_spi clears CU 0's bit and nothing else. */
    entry = find_last(count, 0x090e8ul);
    CHECK(entry != 0 && entry->clear == 1ul && entry->set == 0ul);
    /* GRBM_GFX_INDEX ends broadcast. */
    entry = find_last(count, 0x0802cul);
    CHECK(entry != 0 && entry->set == 0xe0000000ul);
    entry = find_last(count, 0x08bccul);
    CHECK(entry != 0 && entry->set ==
          (0x20ul | (0x40ul << 6) | (0x30ul << 15) | (0x130ul << 23)));

    for (index = 0ul; index < count; ++index) {
        CHECK((ops[index].offset & 3ul) == 0ul);
        CHECK(in_windows(ops[index].offset));
        /* Never a reset, never the RLC's microcode port. */
        CHECK(ops[index].offset != 0x08020ul);
        CHECK(ops[index].offset != 0x0c32cul && ops[index].offset != 0x0c330ul);
    }
}

static v9x_u32 stream_dwords[2048];

/* gfx_v6_0_cp_gfx_start's second half: PREAMBLE begin, the seven runs,
 * PREAMBLE end, CLEAR_STATE, the VGT pair. */
static void test_start(void)
{
    struct v9x_vi_pm4 stream;
    v9x_u32 n;
    v9x_u32 total = 0ul;
    v9x_u32 run;

    for (run = 0ul; run < V9X_SI_CLEAR_STATE_RUNS; ++run) {
        total += v9x_si_clear_state_runs[run].count;
    }
    CHECK(total == V9X_SI_CLEAR_STATE_DWORDS);
    CHECK(v9x_si_clear_state_runs[0].first == 0xa000ul &&
          v9x_si_clear_state_runs[0].count == 212ul);
    CHECK(v9x_si_clear_state_runs[6].first == 0xa2a5ul &&
          v9x_si_clear_state_runs[6].count == 233ul);

    v9x_vi_pm4_init(&stream, stream_dwords, 2048ul);
    v9x_si_3d_start(&stream);
    n = stream.count;
    CHECK(stream.overflow == 0ul);
    CHECK(n == V9X_SI_START_DWORDS);
    CHECK(stream_dwords[0] == V9X_VI_PACKET3(V9X_VI_P3_PREAMBLE_CNTL, 0ul));
    CHECK(stream_dwords[1] == 0x20000000ul);
    CHECK(stream_dwords[2] == V9X_VI_PACKET3(V9X_VI_P3_SET_CONTEXT_REG, 212ul));
    CHECK(stream_dwords[3] == 0ul);
    CHECK(stream_dwords[n - 8ul] ==
          V9X_VI_PACKET3(V9X_VI_P3_PREAMBLE_CNTL, 0ul));
    CHECK(stream_dwords[n - 7ul] == 0x30000000ul);
    CHECK(stream_dwords[n - 6ul] == V9X_VI_PACKET3(V9X_VI_P3_CLEAR_STATE, 0ul));
    CHECK(stream_dwords[n - 3ul] == 0x316ul);
    CHECK(stream_dwords[n - 2ul] == 0x0000000eul &&
          stream_dwords[n - 1ul] == 0x00000010ul);
}

/* The rectangle in GFX6 mode: no SET_UCONFIG_REG anywhere, the primitive
 * type through SET_CONFIG_REG at 256h, each stage's SH run at PGM_LO, and
 * SI's fence event. */
static void test_gfx6_rect(void)
{
    struct v9x_vi_pm4 stream;
    struct v9x_vi_solid_rect rect;
    v9x_u32 at = 0ul;
    v9x_u32 n;
    int uconfig = 0;
    int config_prim = 0;
    int sh_ps = 0;
    int sh_vs = 0;
    int sh_rsrc3 = 0;
    int eop_ok = 0;

    memset(&rect, 0, sizeof(rect));
    rect.target_low = 0x0f809000ul;
    rect.target_high = 0xf4ul;
    rect.pitch_pixels = 64ul;
    rect.height = 16ul;
    rect.format = V9X_VI_CB_FORMAT_8_8_8_8;
    rect.tile_mode_index = V9X_VI_TILE_LINEAR_ALIGNED;
    rect.vs_low = 0x0f804000ul;
    rect.ps_low = 0x0f804100ul;
    rect.shader_high = 0xf4ul;
    rect.left = 8ul;
    rect.top = 4ul;
    rect.right = 40ul;
    rect.bottom = 12ul;
    rect.color[0] = 0x3f800000ul;
    rect.fence_low = 0x0f808100ul;
    rect.fence_high = 0xf4ul;
    rect.fence_value = 0x12345678ul;

    v9x_vi_pm4_init(&stream, stream_dwords, 2048ul);
    CHECK(stream.gfx6 == 0ul);
    stream.gfx6 = 1ul;
    CHECK(v9x_vi_3d_solid_rect(&stream, &rect) == V9X_STATUS_OK);
    CHECK(stream.overflow == 0ul);
    n = stream.count;
    while (at < n) {
        v9x_u32 header = stream_dwords[at];
        v9x_u32 op = (header >> 8) & 0xfful;

        if (op == V9X_VI_P3_SET_UCONFIG_REG) {
            uconfig = 1;
        }
        if (op == V9X_VI_P3_SET_CONFIG_REG &&
            stream_dwords[at + 1ul] == 0x256ul) {
            config_prim = 1;
        }
        if (op == V9X_VI_P3_SET_SH_REG) {
            if (stream_dwords[at + 1ul] == 0x08ul) {
                sh_ps = 1;
            }
            if (stream_dwords[at + 1ul] == 0x48ul) {
                sh_vs = 1;
            }
            if (stream_dwords[at + 1ul] == 0x07ul ||
                stream_dwords[at + 1ul] == 0x46ul) {
                sh_rsrc3 = 1;
            }
        }
        if (op == V9X_VI_P3_EVENT_WRITE_EOP &&
            stream_dwords[at + 1ul] == 0x00000514ul &&
            stream_dwords[at + 2ul] == 0x0f808100ul &&
            stream_dwords[at + 4ul] == 0x12345678ul) {
            eop_ok = 1;
        }
        at += ((header >> 16) & 0x3ffful) + 2ul;
    }
    CHECK(at == n);
    CHECK(!uconfig);
    CHECK(config_prim);
    CHECK(sh_ps && sh_vs && !sh_rsrc3);
    CHECK(eop_ok);
}

/* The solid shaders translate for GFX6; the TL vertex shader, which loads
 * through FLAT, is refused at its first FLAT instruction. */
static void test_gfx6_shaders(void)
{
    v9x_u32 words[128];
    v9x_u32 failed = 0xfffffffful;
    v9x_u32 count;

    count = v9x_vi_shader_solid_vs(words);
    CHECK(v9x_si_shader_from_vi(words, count, &failed) == V9X_STATUS_OK);
    count = v9x_vi_shader_solid_ps(words);
    CHECK(v9x_si_shader_from_vi(words, count, &failed) == V9X_STATUS_OK);
    count = v9x_vi_shader_tl_vs(words);
    CHECK(v9x_si_shader_from_vi(words, count, &failed) ==
          V9X_STATUS_UNSUPPORTED);
    CHECK(failed < count && (words[failed] & 0xfc000000ul) == 0xdc000000ul);
    /* The same program still translates for CI. */
    count = v9x_vi_shader_tl_vs(words);
    CHECK(v9x_cik_shader_from_vi(words, count, &failed) == V9X_STATUS_OK);
}

/* SI's TL VS: the shift, two MUBUF loads in place of the FLAT fetch, then
 * the translated tail. LLVM's SI assembler test reads
 * "buffer_load_dwordx4 v[1:4], v0, s[4:7], 0 offen" as E0381000h,
 * 80010100h; these differ only in VADDR, VDATA and SRSRC. */
static void test_gfx6_tl_vs(void)
{
    v9x_u32 words[V9X_SI_TL_VS_MAX];
    v9x_u32 vi[64];
    v9x_u32 count = 0ul;
    v9x_u32 vi_count;
    v9x_u32 index;
    int flat = 0;

    CHECK(v9x_si_shader_tl_vs(words, &count) == V9X_STATUS_OK);
    CHECK(count == V9X_SI_TL_VS_DWORDS);
    CHECK(words[1] == 0xe0381000ul && words[2] == 0x80000301ul);
    CHECK(words[3] == 0xe0381010ul && words[4] == 0x80000901ul);
    /* The shift is the GCN 3 program's first word translated: VOP2
     * v_lshlrev_b32 is 12h on VI, 1Ah on SI. */
    vi_count = v9x_vi_shader_tl_vs(vi);
    CHECK(vi_count == V9X_VI_TL_VS_DWORDS);
    CHECK((words[0] & 0x01fffffful) == (vi[0] & 0x01fffffful) &&
          ((words[0] >> 25) & 0x3ful) == 0x1aul);
    /* The tail is the GCN 3 program's from its s_waitcnt, same length. */
    CHECK(count - 5ul == vi_count - 10ul);
    CHECK(words[5] == vi[10]);
    for (index = 0ul; index < count; ++index) {
        if ((words[index] & 0xfc000000ul) == 0xdc000000ul) {
            flat = 1;
        }
    }
    CHECK(!flat);
    CHECK(v9x_si_shader_tl_vs(0, &count) == V9X_STATUS_INVALID_ARGUMENT);
}

/* The TL draw in GFX6 mode hands the VS a four-dword V#. */
static void test_gfx6_tl_draw(void)
{
    struct v9x_vi_pm4 stream;
    struct v9x_vi_tl_draw draw;
    v9x_u32 at = 0ul;
    int resource = 0;

    memset(&draw, 0, sizeof(draw));
    draw.target_low = 0x0f809000ul;
    draw.target_high = 0xf4ul;
    draw.pitch_pixels = 64ul;
    draw.height = 16ul;
    draw.format = V9X_VI_CB_FORMAT_8_8_8_8;
    draw.tile_mode_index = V9X_VI_TILE_LINEAR_ALIGNED;
    draw.vs_low = 0x0f804000ul;
    draw.ps_low = 0x0f804100ul;
    draw.shader_high = 0xf4ul;
    draw.vertex_low = 0x0f80b000ul;
    draw.vertex_high = 0xf4ul;
    draw.vertex_count = 6ul;
    draw.fence_low = 0x0f808100ul;
    draw.fence_high = 0xf4ul;
    draw.fence_value = 1ul;

    /* Depth on GFX6: DB_DEPTH_INFO (A00Fh) ADDR5_SWIZZLE_MASK alone, then
     * DB_Z_INFO Z16 with Oland's 1D depth tile index 4; a linear Z buffer
     * is refused. */
    draw.depth_low = 0x0f80d000ul;
    draw.depth_high = 0xf4ul;
    draw.depth_func = V9X_VI_ZFUNC_LESS;
    draw.depth_write = 1ul;
    draw.depth_linear = 1ul;
    v9x_vi_pm4_init(&stream, stream_dwords, 2048ul);
    stream.gfx6 = 1ul;
    CHECK(v9x_vi_3d_tl_draw(&stream, &draw) == V9X_STATUS_INVALID_ARGUMENT);
    draw.depth_linear = 0ul;
    v9x_vi_pm4_init(&stream, stream_dwords, 2048ul);
    stream.gfx6 = 1ul;
    CHECK(v9x_vi_3d_tl_draw(&stream, &draw) == V9X_STATUS_OK);
    {
        int depth_ok = 0;

        at = 0ul;
        while (at < stream.count) {
            v9x_u32 header = stream_dwords[at];

            if (((header >> 8) & 0xfful) == V9X_VI_P3_SET_CONTEXT_REG &&
                stream_dwords[at + 1ul] == 0x00ful) {
                depth_ok = stream_dwords[at + 2ul] == 1ul &&
                           stream_dwords[at + 3ul] == 0x00400001ul;
            }
            at += ((header >> 16) & 0x3ffful) + 2ul;
        }
        CHECK(depth_ok);
    }
    at = 0ul;
    while (at < stream.count) {
        v9x_u32 header = stream_dwords[at];

        if (((header >> 8) & 0xfful) == V9X_VI_P3_SET_SH_REG &&
            ((header >> 16) & 0x3ffful) == 4ul &&
            stream_dwords[at + 1ul] == 0x4cul) {
            resource = stream_dwords[at + 2ul] == 0x0f80b000ul &&
                       stream_dwords[at + 3ul] == 0xf4ul &&
                       stream_dwords[at + 4ul] == 6ul * 32ul &&
                       stream_dwords[at + 5ul] == 0x00027facul;
        }
        at += ((header >> 16) & 0x3ffful) + 2ul;
    }
    CHECK(resource);
}

/* The wide-vertex VS: seven IDXEN loads, then the GCN 3 tail rewritten.
 * IDXEN is MUBUF bit 13 beside OFFEN's 12 (BUFInstructions.td's
 * MUBUF_Real_gfx6_gfx7; the TL VS's "offen" load is E0381000h). */
static void test_gfx6_wide_vs(void)
{
    v9x_u32 words[64];
    v9x_u32 count = 0ul;
    v9x_u32 index;
    int flat = 0;
    int exports = 0;

    CHECK(v9x_si_shader_wide_vs(words, &count) == V9X_STATUS_OK);
    CHECK(count == V9X_SI_WIDE_VS_DWORDS);
    CHECK(words[0] == 0xe0382000ul && words[1] == 0x80000100ul);
    CHECK(words[2] == 0xe0382010ul && words[3] == 0x80000500ul);
    CHECK(words[12] == 0xe0382060ul && words[13] == 0x80001900ul);
    /* SI's s_waitcnt, then six parameter exports and POS0 with done, in
     * SI's EXP encoding (F8000000h). */
    CHECK(words[14] == v9x_vi_s_waitcnt(V9X_VI_WAITCNT_ALL));
    for (index = 15ul; index + 1ul < count; index += 2ul) {
        if ((words[index] & 0xfc000000ul) == 0xf8000000ul) {
            ++exports;
        }
    }
    CHECK(exports == 7);
    CHECK(((words[15] >> 4) & 0x3ful) == 32ul &&
          words[16] == 0x08070605ul);                 /* PARAM0, v5-v8 */
    CHECK(((words[27] >> 4) & 0x3ful) == 12ul &&
          (words[27] & 0x800ul) != 0ul &&
          words[28] == 0x04030201ul);                 /* POS0 done */
    CHECK(words[29] == V9X_VI_S_ENDPGM);
    for (index = 0ul; index < count; ++index) {
        if ((words[index] & 0xfc000000ul) == 0xdc000000ul) {
            flat = 1;
        }
    }
    CHECK(!flat);
    CHECK(v9x_si_shader_wide_vs(0, &count) == V9X_STATUS_INVALID_ARGUMENT);
}

/* The TL2 VS: the TL VS with a third load (s4-s7, IDXEN on v0) after its
 * two and parameter 3's export ahead of POS0's. */
static void test_gfx6_tl2_vs(void)
{
    v9x_u32 tl[V9X_SI_TL_VS_MAX];
    v9x_u32 words[V9X_SI_TL_VS_MAX];
    v9x_u32 tl_count = 0ul;
    v9x_u32 count = 0ul;
    v9x_u32 index;

    CHECK(v9x_si_shader_tl_vs(tl, &tl_count) == V9X_STATUS_OK);
    CHECK(v9x_si_shader_tl2_vs(words, &count) == V9X_STATUS_OK);
    CHECK(count == V9X_SI_TL2_VS_DWORDS && count <= 64ul);
    for (index = 0ul; index < 5ul; ++index) {
        CHECK(words[index] == tl[index]);
    }
    CHECK(words[5] == 0xe0382000ul && words[6] == 0x80011100ul);
    for (index = 5ul; index < tl_count - 3ul; ++index) {
        CHECK(words[index + 2ul] == tl[index]);
    }
    /* PARAM3 (target 35) of v17-v20, then POS0 done and s_endpgm. */
    CHECK((words[count - 5ul] & 0xfc000000ul) == 0xf8000000ul &&
          ((words[count - 5ul] >> 4) & 0x3ful) == 35ul &&
          words[count - 4ul] == 0x14131211ul);
    CHECK(words[count - 3ul] == tl[tl_count - 3ul] &&
          words[count - 1ul] == tl[tl_count - 1ul]);
}

/* A pixel shader 1.x draw on GFX6: the strided V#, six parameters, the
 * table as two PS user SGPRs, the PS's SGPRs in RSRC1, and COMP_SWAP. */
static void test_gfx6_wide_draw(void)
{
    static v9x_u32 stream_dwords[2048];
    struct v9x_vi_pm4 stream;
    struct v9x_vi_tl_draw draw;
    v9x_u32 at = 0ul;
    int resource = 0;
    int table = 0;
    int rsrc = 0;
    int swap = 0;
    int inputs = 0;

    memset(&draw, 0, sizeof(draw));
    draw.target_low = 0x0f80b000ul;
    draw.target_high = 0xf4ul;
    draw.pitch_pixels = 64ul;
    draw.height = 48ul;
    draw.format = V9X_VI_CB_FORMAT_8_8_8_8;
    draw.tile_mode_index = V9X_VI_TILE_LINEAR_ALIGNED;
    draw.vs_low = 0x0f804000ul;
    draw.ps_low = 0x0f804100ul;
    draw.shader_high = 0xf4ul;
    draw.vertex_low = 0x0f808400ul;
    draw.vertex_high = 0xf4ul;
    draw.vertex_count = 6ul;
    draw.fence_low = 0x0f808100ul;
    draw.fence_high = 0xf4ul;
    draw.fence_value = 1ul;
    draw.params = V9X_SI_WIDE_PARAMS;
    draw.vertex_stride = V9X_SI_WIDE_VERTEX_BYTES;
    draw.vs_vgprs = V9X_SI_WIDE_VS_VGPRS;
    draw.ps_vgprs = 72ul;
    draw.ps_sgprs = 96ul;
    draw.ps_table_low = 0x0f808200ul;
    draw.ps_table_high = 0xf4ul;
    draw.target_swap = V9X_VI_CB_SWAP_ALT;
    draw.ps_kill = 1ul;

    /* A strided V# is GFX6's, and a table cannot go with a texture. */
    v9x_vi_pm4_init(&stream, stream_dwords, 2048ul);
    CHECK(v9x_vi_3d_tl_draw(&stream, &draw) == V9X_STATUS_INVALID_ARGUMENT);
    draw.texture_low = 0x0f80e000ul;
    v9x_vi_pm4_init(&stream, stream_dwords, 2048ul);
    stream.gfx6 = 1ul;
    CHECK(v9x_vi_3d_tl_draw(&stream, &draw) == V9X_STATUS_INVALID_ARGUMENT);
    draw.texture_low = 0ul;
    v9x_vi_pm4_init(&stream, stream_dwords, 2048ul);
    stream.gfx6 = 1ul;
    CHECK(v9x_vi_3d_tl_draw(&stream, &draw) == V9X_STATUS_OK);

    while (at < stream.count) {
        v9x_u32 header = stream_dwords[at];
        v9x_u32 op = (header >> 8) & 0xfful;
        v9x_u32 n = (header >> 16) & 0x3ffful;
        v9x_u32 reg = stream_dwords[at + 1ul];

        if (op == V9X_VI_P3_SET_SH_REG && reg == 0x4cul && n == 4ul) {
            resource = stream_dwords[at + 2ul] == 0x0f808400ul &&
                       stream_dwords[at + 3ul] == (0xf4ul | (112ul << 16)) &&
                       stream_dwords[at + 4ul] == 6ul;
        }
        if (op == V9X_VI_P3_SET_SH_REG && reg == 0x0cul && n == 2ul) {
            table = stream_dwords[at + 2ul] == 0x0f808200ul &&
                    stream_dwords[at + 3ul] == 0xf4ul;
        }
        /* SPI_SHADER_PGM_LO_PS (2C08h): VGPRS 17 (72), SGPRS 12 (98
         * with VCC), FLOAT_MODE C0h; two user SGPRs. */
        if (op == V9X_VI_P3_SET_SH_REG && reg == 0x08ul && n == 4ul) {
            rsrc = stream_dwords[at + 4ul] == 0x000c0311ul &&
                   stream_dwords[at + 5ul] == 4ul;
        }
        /* CB_COLOR0_INFO: COLOR_8_8_8_8, SWAP_ALT at 12:11. */
        if (op == V9X_VI_P3_SET_CONTEXT_REG && reg == 0x31cul && n == 1ul &&
            stream_dwords[at + 2ul] == 0x00000828ul) {
            swap = 1;
        }
        /* SPI_PS_IN_CONTROL: six interpolants. */
        if (op == V9X_VI_P3_SET_CONTEXT_REG && reg == 0x1b6ul && n == 1ul) {
            inputs = stream_dwords[at + 2ul] == 6ul;
        }
        at += n + 2ul;
    }
    CHECK(resource);
    CHECK(table);
    CHECK(rsrc);
    CHECK(swap);
    CHECK(inputs);
}

unsigned int v9x_run_si_gpu_tests(void)
{
    failures = 0u;
    test_gpu_init();
    test_start();
    test_gfx6_rect();
    test_gfx6_shaders();
    test_gfx6_tl_vs();
    test_gfx6_tl_draw();
    test_gfx6_wide_vs();
    test_gfx6_tl2_vs();
    test_gfx6_wide_draw();
    return failures;
}
