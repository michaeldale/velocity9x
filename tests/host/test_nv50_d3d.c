#include <stdio.h>

#include "velocity9x/nv50_d3d.h"
#include "velocity9x/nv50_shader.h"
#include "velocity9x/gt218_reserve.h"

/* The 3D scratch and push area fit the reserve after the channel's
 * region, without overlap. */
typedef char v9x_assert_scratch_after_region[
    V9X_NV50_REGION_BYTES <= V9X_GT218_RESERVE_3D_SCRATCH ? 1 : -1];
typedef char v9x_assert_push_after_scratch[
    V9X_GT218_RESERVE_3D_SCRATCH + V9X_NV50_3D_SCRATCH_BYTES <=
        V9X_GT218_RESERVE_3D_PUSH ? 1 : -1];
typedef char v9x_assert_push_in_reserve[
    V9X_GT218_RESERVE_3D_PUSH + V9X_GT218_RESERVE_3D_PUSH_BYTES <=
        V9X_GT218_RESERVE_BYTES ? 1 : -1];

/*
 * The GT218's Direct3D programs, state and vertices
 * (src\chipsets\nvidia\gt218\nv50_d3d.c): the programs rebuilt here from
 * the encoder's own calls, the state's methods against the values
 * include\velocity9x\nv50_d3d.h names, and a vertex's words.
 */

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

#define R(n) V9X_NV50_SH_R(n)
#define V(off) V9X_NV50_SH_V(off)
/* The fragment program's constants: buffer 2 (FP CB_DEF) bound to its
 * slot 0 by the setup's SET_PROGRAM_CB 31h | 2 << 12, so c0[]. The first
 * /3d:d3d run read c2[], an unbound slot, and the fog draw trapped in
 * the MP. */
#define C2(off) V9X_NV50_SH_C(0, off)
#define LONG V9X_NV50_SH_LONG
#define SHORT V9X_NV50_SH_SHORT

static v9x_u32 float_bits(float value)
{
    union {
        float f;
        v9x_u32 u;
    } converted;

    converted.f = value;
    return converted.u;
}

/* The first header for method on the 3D subchannel at or after from, or
 * 0xffffffff. */
static v9x_u32 find_from(const struct v9x_nv50_push *push, v9x_u32 method,
                         v9x_u32 from, v9x_u32 *data_at)
{
    v9x_u32 at = 0ul;

    while (at < push->count) {
        v9x_u32 header = push->words[at];
        v9x_u32 count = (header >> 18) & 0x7fful;

        if (at >= from && (header & 0x1ffcul) == method &&
            ((header >> 13) & 7ul) == V9X_NV50_SUBC_3D) {
            *data_at = at + 1ul;
            return header;
        }
        at += 1ul + count;
    }
    return 0xfffffffful;
}

static v9x_u32 find(const struct v9x_nv50_push *push, v9x_u32 method,
                    v9x_u32 *data_at)
{
    return find_from(push, method, 0ul, data_at);
}

static void test_vp(void)
{
    v9x_u32 words[V9X_NV50_D3D_VP_WORDS_MAX];
    v9x_u32 expect[V9X_NV50_D3D_VP_WORDS_MAX];
    struct v9x_nv50_sh sh;
    v9x_u32 count;
    v9x_u32 index;

    /* Fourteen long moves, input to result, the last ending the
     * program. */
    v9x_nv50_sh_init(&sh, expect, V9X_NV50_D3D_VP_WORDS_MAX);
    for (index = 0ul; index < 14ul; ++index) {
        v9x_nv50_sh_mov(&sh, LONG, V9X_NV50_SH_O(index * 4ul),
                        V9X_NV50_SH_A(index * 4ul));
    }
    v9x_nv50_sh_exit(&sh);
    CHECK(sh.invalid == 0ul && sh.overflow == 0ul && sh.count == 28ul);

    count = v9x_nv50_d3d_vp(words, V9X_NV50_D3D_VP_WORDS_MAX);
    CHECK(count == 28ul);
    for (index = 0ul; index < 28ul && count == 28ul; ++index) {
        CHECK(words[index] == expect[index]);
    }
    CHECK(v9x_nv50_d3d_vp(words, 27ul) == 0ul);
}

/* The prologue every variant starts with: w from the position's
 * interpolated 1 / w, then diffuse perspective-correct into $r0-$r3. */
static void expect_prologue(struct v9x_nv50_sh *sh)
{
    v9x_u32 index;

    v9x_nv50_sh_interp(sh, R(4), V(0x00ul), V9X_NV50_SH_NONE);
    v9x_nv50_sh_rcp(sh, SHORT, R(4), R(4));
    for (index = 0ul; index < 4ul; ++index) {
        v9x_nv50_sh_interp(sh, R(index), V(0x04ul + index * 4ul), R(4));
    }
}

static void check_program(v9x_u32 variant, const struct v9x_nv50_sh *expect,
                          v9x_u32 registers)
{
    v9x_u32 words[V9X_NV50_D3D_FP_WORDS_MAX];
    v9x_u32 count;
    v9x_u32 used = 0ul;
    v9x_u32 index;

    CHECK(expect->invalid == 0ul && expect->overflow == 0ul);
    count = v9x_nv50_d3d_fp(variant, words, V9X_NV50_D3D_FP_WORDS_MAX, &used);
    CHECK(count == expect->count);
    CHECK(used == registers);
    for (index = 0ul; index < count && count == expect->count; ++index) {
        CHECK(words[index] == expect->words[index]);
    }
}

static void test_fp(void)
{
    v9x_u32 expect_words[V9X_NV50_D3D_FP_WORDS_MAX];
    struct v9x_nv50_sh sh;
    v9x_u32 variant;
    v9x_u32 index;
    v9x_u32 words[V9X_NV50_D3D_FP_WORDS_MAX];
    v9x_u32 used;

    /* Variants numbered op-major, then specular, then fog; each in its
     * own 100h of the FP area. */
    CHECK(v9x_nv50_d3d_fp_variant(V9X_NV50_D3D_OP_NONE, 0ul, 0ul) == 0ul);
    CHECK(v9x_nv50_d3d_fp_variant(V9X_NV50_D3D_OP_NONE, 0ul, 1ul) == 1ul);
    CHECK(v9x_nv50_d3d_fp_variant(V9X_NV50_D3D_OP_NONE, 1ul, 0ul) == 2ul);
    CHECK(v9x_nv50_d3d_fp_variant(V9X_NV50_D3D_OP_MODULATE, 1ul, 1ul) ==
          11ul);
    CHECK(v9x_nv50_d3d_fp_start(0ul) == 0x200ul);
    CHECK(v9x_nv50_d3d_fp_start(11ul) == 0xd00ul);
    CHECK(v9x_nv50_d3d_fp_start(11ul) + V9X_NV50_D3D_FP_STRIDE <=
          V9X_NV50_3D_SCRATCH_TIC - V9X_NV50_3D_SCRATCH_FP);

    /* Untextured, no specular, no fog: the prologue, then a long move to
     * end on. */
    v9x_nv50_sh_init(&sh, expect_words, V9X_NV50_D3D_FP_WORDS_MAX);
    expect_prologue(&sh);
    v9x_nv50_sh_mov(&sh, LONG, R(3), R(3));
    v9x_nv50_sh_exit(&sh);
    check_program(0ul, &sh, 5ul);

    /* REPLACE: the texel's four channels. */
    v9x_nv50_sh_init(&sh, expect_words, V9X_NV50_D3D_FP_WORDS_MAX);
    expect_prologue(&sh);
    v9x_nv50_sh_interp(&sh, R(8), V(0x24ul), R(4));
    v9x_nv50_sh_interp(&sh, R(9), V(0x28ul), R(4));
    v9x_nv50_sh_tex(&sh, R(8), 0ul, 0ul, 0xful, 0ul);
    for (index = 0ul; index < 4ul; ++index) {
        v9x_nv50_sh_mov(&sh, SHORT, R(index), R(8ul + index));
    }
    v9x_nv50_sh_mov(&sh, LONG, R(3), R(3));
    v9x_nv50_sh_exit(&sh);
    check_program(v9x_nv50_d3d_fp_variant(V9X_NV50_D3D_OP_REPLACE, 0ul, 0ul),
                  &sh, 12ul);

    /* MODULATE with specular and fog: texel times diffuse, specular's
     * colour added, then (colour - fog) * f + fog with f the specular
     * alpha and the fog colour and its negation constants. The last fmad
     * is long and ends the program. */
    v9x_nv50_sh_init(&sh, expect_words, V9X_NV50_D3D_FP_WORDS_MAX);
    expect_prologue(&sh);
    v9x_nv50_sh_interp(&sh, R(8), V(0x24ul), R(4));
    v9x_nv50_sh_interp(&sh, R(9), V(0x28ul), R(4));
    v9x_nv50_sh_tex(&sh, R(8), 0ul, 0ul, 0xful, 0ul);
    for (index = 0ul; index < 4ul; ++index) {
        v9x_nv50_sh_fmul(&sh, SHORT, R(index), R(index), R(8ul + index));
    }
    for (index = 0ul; index < 3ul; ++index) {
        v9x_nv50_sh_interp(&sh, R(12ul + index), V(0x14ul + index * 4ul),
                           R(4));
        v9x_nv50_sh_fadd(&sh, SHORT, R(index), R(index), R(12ul + index));
    }
    v9x_nv50_sh_interp(&sh, R(15), V(0x20ul), R(4));
    for (index = 0ul; index < 3ul; ++index) {
        v9x_nv50_sh_fadd(&sh, LONG, R(5), R(index),
                         C2(0x10ul + index * 4ul));
        v9x_nv50_sh_fmad(&sh, LONG, R(index), R(5), R(15),
                         C2(index * 4ul));
    }
    v9x_nv50_sh_exit(&sh);
    check_program(v9x_nv50_d3d_fp_variant(V9X_NV50_D3D_OP_MODULATE, 1ul, 1ul),
                  &sh, 16ul);

    /* Every variant fits its slot. */
    for (variant = 0ul; variant < V9X_NV50_D3D_FP_VARIANTS; ++variant) {
        v9x_u32 count = v9x_nv50_d3d_fp(variant, words,
                                        V9X_NV50_D3D_FP_WORDS_MAX, &used);

        CHECK(count != 0ul && count * 4ul <= V9X_NV50_D3D_FP_STRIDE);
    }
    CHECK(v9x_nv50_d3d_fp(V9X_NV50_D3D_FP_VARIANTS, words,
                          V9X_NV50_D3D_FP_WORDS_MAX, &used) == 0ul);
}

static void test_texture(void)
{
    v9x_u32 tic[V9X_NV50_3D_TIC_WORDS];
    v9x_u32 tsc[V9X_NV50_3D_TSC_WORDS];

    /* B8G8R8A8: EXA's solid texel word (UNORM, B G R A to X Y Z W). */
    CHECK(v9x_nv50_d3d_texture(V9X_NV50_D3D_TEX_B8G8R8A8, 0x01230000ul,
                               256ul, 64ul, 32ul, V9X_NV50_D3D_WRAP_REPEAT,
                               0ul, 0ul, tic, tsc));
    CHECK(tic[0] == 0x2a712488ul);
    CHECK(tic[1] == 0x01230000ul);
    CHECK(tic[2] == 0xd005d000ul);
    CHECK(tic[3] == 256ul);
    CHECK(tic[4] == 64ul);
    CHECK(tic[5] == 0x00010020ul);
    CHECK(tic[6] == 0ul && tic[7] == 0ul);
    CHECK(tsc[0] == 0x00024000ul);
    CHECK(tsc[1] == 0x00000051ul);
    CHECK(tsc[2] == 0ul && tsc[7] == 0ul);

    /* B5G6R5: alpha the constant one (Mesa's F3, ONE_FLOAT 7). Bilinear
     * both ways, clamped. */
    CHECK(v9x_nv50_d3d_texture(V9X_NV50_D3D_TEX_B5G6R5, 0x4000ul, 64ul, 16ul,
                               16ul, V9X_NV50_D3D_WRAP_CLAMP, 1ul, 1ul, tic,
                               tsc));
    CHECK(tic[0] == 0x3a712495ul);
    CHECK(tsc[0] == (0x00024000ul | 2ul | (2ul << 3) | (2ul << 6)));
    CHECK(tsc[1] == 0x00000062ul);

    CHECK(v9x_nv50_d3d_texture(V9X_NV50_D3D_TEX_B5G5R5A1, 0x4000ul, 64ul,
                               16ul, 16ul, V9X_NV50_D3D_WRAP_MIRROR, 0ul, 0ul,
                               tic, tsc));
    CHECK(tic[0] == 0x2a712494ul);
    CHECK(tsc[0] == (0x00024000ul | 1ul | (1ul << 3) | (1ul << 6)));
    CHECK(v9x_nv50_d3d_texture(V9X_NV50_D3D_TEX_B4G4R4A4, 0x4000ul, 64ul,
                               16ul, 16ul, 0ul, 0ul, 0ul, tic, tsc));
    CHECK(tic[0] == 0x2a712492ul);
    CHECK(!v9x_nv50_d3d_texture(0ul, 0x4000ul, 64ul, 16ul, 16ul, 0ul, 0ul,
                                0ul, tic, tsc));
}

static void base_state(struct v9x_nv50_d3d_state *state)
{
    unsigned char *bytes = (unsigned char *)state;
    v9x_u32 index;

    for (index = 0ul; index < sizeof(*state); ++index) {
        bytes[index] = 0u;
    }
    state->target.address = 0x00100000ul;
    state->target.pitch = 2560ul;
    state->target.width = 640ul;
    state->target.height = 480ul;
    state->target_format = V9X_NV50_3D_RT_BGRX8;
    state->scissor_right = 640ul;
    state->scissor_bottom = 480ul;
    state->color_mask = V9X_NV50_D3D_MASK_RGB;
    state->fp_variant = 0ul;
    state->fp_registers = 5ul;
}

static void test_state(void)
{
    static v9x_u32 words[V9X_NV50_D3D_STATE_WORDS_MAX];
    struct v9x_nv50_d3d_state state;
    struct v9x_nv50_push push;
    v9x_u32 at = 0ul;
    v9x_u32 header;

    /* Plain: linear target, no depth, no blend, no alpha test. */
    base_state(&state);
    v9x_nv50_push_init(&push, words, V9X_NV50_D3D_STATE_WORDS_MAX);
    v9x_nv50_push_d3d_state(&push, &state);
    CHECK(push.overflow == 0ul);

    header = find(&push, V9X_8597_RT_ADDRESS_HIGH0, &at);
    CHECK(header != 0xfffffffful && words[at + 1ul] == 0x00100000ul &&
          words[at + 2ul] == V9X_NV50_3D_RT_BGRX8);
    header = find(&push, V9X_8597_RT_HORIZ0, &at);
    CHECK(header != 0xfffffffful &&
          words[at] == (V9X_8597_RT_HORIZ_LINEAR | 2560ul) &&
          words[at + 1ul] == 480ul);
    header = find(&push, V9X_8597_SCREEN_SCISSOR_HORIZ, &at);
    CHECK(header != 0xfffffffful && words[at] == (640ul << 16) &&
          words[at + 1ul] == (480ul << 16));
    header = find(&push, V9X_8597_SCISSOR_HORIZ0, &at);
    CHECK(header != 0xfffffffful && words[at] == (640ul << 16) &&
          words[at + 1ul] == (480ul << 16));
    header = find(&push, 0x1a00ul, &at);
    CHECK(header != 0xfffffffful && words[at] == V9X_NV50_D3D_MASK_RGB);

    header = find(&push, V9X_8597_ZETA_ENABLE, &at);
    CHECK(header != 0xfffffffful && words[at] == 0ul);
    header = find(&push, V9X_8597_DEPTH_TEST_ENABLE, &at);
    CHECK(header != 0xfffffffful && words[at] == 0ul);
    header = find(&push, V9X_8597_DEPTH_WRITE_ENABLE, &at);
    CHECK(header != 0xfffffffful && words[at] == 0ul);
    header = find(&push, V9X_8597_BLEND_ENABLE0, &at);
    CHECK(header != 0xfffffffful);
    /* The last BLEND_ENABLE(0) wins: the target builder writes one too. */
    while (header != 0xfffffffful) {
        v9x_u32 value = words[at];

        header = find_from(&push, V9X_8597_BLEND_ENABLE0, at, &at);
        if (header == 0xfffffffful) {
            CHECK(value == 0ul);
        }
    }
    header = find(&push, 0x12ecul, &at);
    CHECK(header != 0xfffffffful && words[at] == 0ul);
    header = find(&push, 0x1918ul, &at);
    CHECK(header != 0xfffffffful && words[at] == 0ul);

    /* The programs and their linkage. */
    header = find(&push, V9X_8597_VP_START_ID, &at);
    CHECK(header != 0xfffffffful && words[at] == V9X_NV50_D3D_VP_START);
    header = find(&push, V9X_8597_FP_START_ID, &at);
    CHECK(header != 0xfffffffful && words[at] == 0x200ul);
    header = find(&push, V9X_8597_VP_ATTR_EN0, &at);
    CHECK(header != 0xfffffffful && words[at] == 0x0ffful &&
          words[at + 1ul] == 0x3ul);
    header = find(&push, V9X_8597_VP_REG_ALLOC_RESULT, &at);
    CHECK(header != 0xfffffffful && words[at] == 14ul);
    header = find(&push, V9X_8597_VP_RESULT_MAP_SIZE, &at);
    CHECK(header != 0xfffffffful && words[at] == 14ul);
    header = find(&push, V9X_8597_VP_RESULT_MAP0, &at);
    CHECK(header != 0xfffffffful && ((header >> 18) & 0x7fful) == 4ul &&
          words[at] == 0x03020100ul && words[at + 1ul] == 0x07060504ul &&
          words[at + 2ul] == 0x0b0a0908ul && words[at + 3ul] == 0x40400d0cul);
    header = find(&push, V9X_8597_FP_INTERPOLANT_CTRL, &at);
    CHECK(header != 0xfffffffful && words[at] == 0x080a040aul &&
          words[at + 1ul] == 5ul);
    /* Untextured, no fog: no table binding, no constants. */
    CHECK(find(&push, V9X_8597_TIC_FLUSH, &at) == 0xfffffffful);
    CHECK(find(&push, V9X_8597_CB_ADDR, &at) == 0xfffffffful);

    /* Blend, alpha test, depth, texture and fog. */
    base_state(&state);
    state.blend = 1ul;
    state.blend_src = V9X_NV50_D3D_BLEND_SRC_ALPHA;
    state.blend_dst = V9X_NV50_D3D_BLEND_ONE_MINUS_SRC_ALPHA;
    state.alpha_test = 1ul;
    state.alpha_func = 0x0204ul;
    state.alpha_ref = float_bits(0.5f);
    state.has_zeta = 1ul;
    state.zeta.address = 0x40200000ul;
    state.zeta.width = 640ul;
    state.zeta.height = 480ul;
    state.zeta.tile_mode = 0x10ul;
    state.zeta.tiled = 1ul;
    state.zeta_format = V9X_NV50_3D_ZETA_Z16;
    state.depth_test = 1ul;
    state.depth_write = 0ul;
    state.depth_func = 0x0203ul;
    state.textured = 1ul;
    state.fog_color = 0x00ff8000ul;
    state.fp_variant = 11ul;
    state.fp_registers = 16ul;
    v9x_nv50_push_init(&push, words, V9X_NV50_D3D_STATE_WORDS_MAX);
    v9x_nv50_push_d3d_state(&push, &state);
    CHECK(push.overflow == 0ul);

    header = find(&push, 0x1340ul, &at);
    CHECK(header != 0xfffffffful && ((header >> 18) & 0x7fful) == 5ul &&
          words[at] == 0x8006ul && words[at + 1ul] == 0x4302ul &&
          words[at + 2ul] == 0x4303ul && words[at + 3ul] == 0x8006ul &&
          words[at + 4ul] == 0x4302ul);
    header = find(&push, 0x1358ul, &at);
    CHECK(header != 0xfffffffful && words[at] == 0x4303ul);
    header = find(&push, 0x12ecul, &at);
    CHECK(header != 0xfffffffful && words[at] == 1ul);
    header = find(&push, 0x1310ul, &at);
    CHECK(header != 0xfffffffful && words[at] == float_bits(0.5f) &&
          words[at + 1ul] == 0x0204ul);

    header = find(&push, V9X_8597_ZETA_ADDRESS_HIGH, &at);
    CHECK(header != 0xfffffffful && words[at + 1ul] == 0x40200000ul &&
          words[at + 2ul] == V9X_NV50_3D_ZETA_Z16 &&
          words[at + 3ul] == 0x10ul);
    /* The depth state as asked, after push_3d_zeta's defaults. */
    {
        v9x_u32 test_at = 0ul;
        v9x_u32 write_at = 0ul;
        v9x_u32 func_at = 0ul;
        v9x_u32 value_test = 0xfffffffful;
        v9x_u32 value_write = 0xfffffffful;
        v9x_u32 value_func = 0xfffffffful;

        while (find_from(&push, V9X_8597_DEPTH_TEST_ENABLE, test_at,
                         &test_at) != 0xfffffffful) {
            value_test = words[test_at];
        }
        while (find_from(&push, V9X_8597_DEPTH_WRITE_ENABLE, write_at,
                         &write_at) != 0xfffffffful) {
            value_write = words[write_at];
        }
        while (find_from(&push, V9X_8597_DEPTH_TEST_FUNC, func_at,
                         &func_at) != 0xfffffffful) {
            value_func = words[func_at];
        }
        CHECK(value_test == 1ul);
        CHECK(value_write == 0ul);
        CHECK(value_func == 0x0203ul);
    }

    header = find(&push, V9X_8597_FP_START_ID, &at);
    CHECK(header != 0xfffffffful && words[at] == 0xd00ul);
    header = find(&push, V9X_8597_TIC_FLUSH, &at);
    CHECK(header != 0xfffffffful);
    header = find(&push, V9X_8597_BIND_TIC2, &at);
    CHECK(header != 0xfffffffful && words[at] == V9X_8597_BIND_TIC_FIRST);

    /* The fog colour and its negation into buffer 2's first eight
     * words, non-incrementing at CB_DATA. */
    header = find(&push, V9X_8597_CB_ADDR, &at);
    CHECK(header != 0xfffffffful && words[at] == V9X_8597_CB_FP);
    header = find(&push, V9X_8597_CB_DATA0, &at);
    CHECK(header != 0xfffffffful &&
          (header & V9X_NV50_PUSH_NONINC) != 0ul &&
          ((header >> 18) & 0x7fful) == V9X_NV50_D3D_FP_CONSTANTS);
    if (header != 0xfffffffful) {
        CHECK(words[at] == float_bits(1.0f));
        CHECK(words[at + 1ul] == float_bits(128.0f / 255.0f));
        CHECK(words[at + 2ul] == 0ul);
        CHECK(words[at + 4ul] == float_bits(-1.0f));
        CHECK(words[at + 5ul] == float_bits(-128.0f / 255.0f));
        /* Blue 0 negated: either zero, both add nothing. */
        CHECK(words[at + 6ul] == 0x80000000ul || words[at + 6ul] == 0ul);
    }
}

static void test_vertex(void)
{
    v9x_u32 words[V9X_NV50_D3D_VERTEX_WORDS_MAX * 2ul];
    struct v9x_nv50_push push;
    v9x_u32 at = 0ul;
    v9x_u32 header;

    /* Untextured, no specular: diffuse, then the position, which emits
     * the vertex. The fourth word is rhw itself: the engine weights the
     * interpolation by it (/3d:d3d rectangle D, M-BXMFA8). */
    v9x_nv50_push_init(&push, words, V9X_NV50_D3D_VERTEX_WORDS_MAX);
    v9x_nv50_push_d3d_vertex(&push, 10.0f, 20.0f, 0.25f, 0.5f, 0x80ff0033ul,
                             0ul, 0.0f, 0.0f, 0.5f, 0ul, 0ul);
    CHECK(push.overflow == 0ul && push.count == 10ul);
    CHECK(words[0] == V9X_NV50_PUSH_HEADER(V9X_NV50_SUBC_3D, 0x510ul, 4ul));
    CHECK(words[1] == float_bits(1.0f));
    CHECK(words[2] == 0ul);
    CHECK(words[3] == float_bits(51.0f / 255.0f));
    CHECK(words[4] == float_bits(128.0f / 255.0f));
    CHECK(words[5] == V9X_NV50_PUSH_HEADER(V9X_NV50_SUBC_3D, 0x500ul, 4ul));
    CHECK(words[6] == float_bits(10.5f));
    CHECK(words[7] == float_bits(20.5f));
    CHECK(words[8] == float_bits(0.25f));
    CHECK(words[9] == float_bits(0.5f));

    /* Textured with specular: coordinates (attribute 8), specular (2),
     * diffuse, position. */
    v9x_nv50_push_init(&push, words, V9X_NV50_D3D_VERTEX_WORDS_MAX);
    v9x_nv50_push_d3d_vertex(&push, 0.0f, 0.0f, 0.0f, 1.0f, 0ul,
                             0xff000000ul, 0.25f, 0.75f, 0.0f, 1ul, 1ul);
    CHECK(push.overflow == 0ul &&
          push.count == V9X_NV50_D3D_VERTEX_WORDS_MAX);
    CHECK(words[0] == V9X_NV50_PUSH_HEADER(V9X_NV50_SUBC_3D, 0x3c0ul, 2ul));
    CHECK(words[1] == float_bits(0.25f) && words[2] == float_bits(0.75f));
    CHECK(words[3] == V9X_NV50_PUSH_HEADER(V9X_NV50_SUBC_3D, 0x520ul, 4ul));
    CHECK(words[7] == float_bits(1.0f));
    header = find(&push, 0x500ul, &at);
    CHECK(header != 0xfffffffful && at == 14ul);

    /* A non-positive rhw is taken as 1. */
    v9x_nv50_push_init(&push, words, V9X_NV50_D3D_VERTEX_WORDS_MAX);
    v9x_nv50_push_d3d_vertex(&push, 4.0f, 8.0f, 0.5f, 0.0f, 0ul, 0ul, 0.0f,
                             0.0f, 0.0f, 0ul, 0ul);
    CHECK(words[6] == float_bits(4.0f) && words[9] == float_bits(1.0f));

    v9x_nv50_push_init(&push, words, V9X_NV50_D3D_VERTEX_WORDS_MAX);
    v9x_nv50_push_d3d_begin(&push);
    v9x_nv50_push_d3d_end(&push);
    CHECK(words[0] ==
          V9X_NV50_PUSH_HEADER(V9X_NV50_SUBC_3D, V9X_8597_VERTEX_BEGIN_GL,
                               1ul) &&
          words[1] == V9X_8597_PRIMITIVE_TRIANGLES);
    CHECK(words[2] ==
          V9X_NV50_PUSH_HEADER(V9X_NV50_SUBC_3D, V9X_8597_VERTEX_END_GL,
                               1ul) &&
          words[3] == 0ul);
}

unsigned int v9x_run_nv50_d3d_tests(void)
{
    failures = 0u;
    test_vp();
    test_fp();
    test_texture();
    test_state();
    test_vertex();
    return failures;
}
