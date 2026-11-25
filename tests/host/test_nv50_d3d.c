#include <stdio.h>

#include "velocity9x/nv50_d3d.h"
#include "velocity9x/nv50_shader.h"
#include "velocity9x/gt218_reserve.h"
#include "nv50_sh_sim.h"

/* The 3D scratch and push area fit the reserve after the channel's
 * region, without overlap. */
typedef char v9x_assert_scratch_after_region[
    V9X_NV50_REGION_BYTES <= V9X_GT218_RESERVE_3D_SCRATCH ? 1 : -1];
typedef char v9x_assert_push_after_scratch[
    V9X_GT218_RESERVE_3D_SCRATCH + V9X_NV50_3D_SCRATCH_BYTES <=
        V9X_GT218_RESERVE_3D_PUSH ? 1 : -1];
/* The two-unit program slots: where nv50_d3d.h's offsets from the code
 * base put them, past the scratch and before the push area. */
typedef char v9x_assert_fp2_where_said[
    V9X_GT218_RESERVE_3D_SCRATCH + V9X_NV50_3D_SCRATCH_FP +
        V9X_NV50_D3D_FP2_FIRST == V9X_GT218_RESERVE_3D_FP2 ? 1 : -1];
typedef char v9x_assert_fp2_fits[
    V9X_NV50_D3D_FP2_SLOTS * V9X_NV50_D3D_FP2_STRIDE <=
            V9X_GT218_RESERVE_3D_FP2_BYTES &&
        V9X_NV50_D3D_FP2_WORDS_MAX * 4ul <= V9X_NV50_D3D_FP2_STRIDE &&
        V9X_GT218_RESERVE_3D_SCRATCH + V9X_NV50_3D_SCRATCH_BYTES <=
            V9X_GT218_RESERVE_3D_FP2 &&
        V9X_GT218_RESERVE_3D_FP2 + V9X_GT218_RESERVE_3D_FP2_BYTES <=
            V9X_GT218_RESERVE_3D_PUSH ? 1 : -1];
/* The pixel shader slots likewise, after the two-unit ones. */
typedef char v9x_assert_ps_where_said[
    V9X_GT218_RESERVE_3D_SCRATCH + V9X_NV50_3D_SCRATCH_FP +
        V9X_NV50_D3D_PS_FIRST == V9X_GT218_RESERVE_3D_PS ? 1 : -1];
typedef char v9x_assert_ps_fits[
    V9X_NV50_D3D_PS_SLOTS * V9X_NV50_D3D_PS_STRIDE <=
            V9X_GT218_RESERVE_3D_PS_BYTES &&
        V9X_NV50_D3D_PS_WORDS_MAX * 4ul <= V9X_NV50_D3D_PS_STRIDE &&
        V9X_GT218_RESERVE_3D_FP2 + V9X_GT218_RESERVE_3D_FP2_BYTES <=
            V9X_GT218_RESERVE_3D_PS &&
        V9X_GT218_RESERVE_3D_PS + V9X_GT218_RESERVE_3D_PS_BYTES <=
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

    /* Sixteen long moves, input to result (unit 1's coordinates the last
     * two), the last ending the program. */
    v9x_nv50_sh_init(&sh, expect, V9X_NV50_D3D_VP_WORDS_MAX);
    for (index = 0ul; index < 16ul; ++index) {
        v9x_nv50_sh_mov(&sh, LONG, V9X_NV50_SH_O(index * 4ul),
                        V9X_NV50_SH_A(index * 4ul));
    }
    v9x_nv50_sh_exit(&sh);
    CHECK(sh.invalid == 0ul && sh.overflow == 0ul && sh.count == 32ul);

    count = v9x_nv50_d3d_vp(words, V9X_NV50_D3D_VP_WORDS_MAX);
    CHECK(count == 32ul);
    for (index = 0ul; index < 32ul && count == 32ul; ++index) {
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
     * colour added and saturated (long: the short form has no SAT), then
     * (colour - fog) * f + fog with f the specular alpha and the fog
     * colour and its negation constants. Unsaturated, a sum over 1 came
     * out too bright under fog (V9XD32 T19 FfSpecFog on the GT218). The
     * last fmad is long and ends the program. */
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
        v9x_nv50_sh_fadd(&sh, LONG, V9X_NV50_SH_SAT(R(index)), R(index),
                         R(12ul + index));
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

    /* MODULATE keeping the texel's alpha (DX5 MODULATE on a texture with
     * alpha, docs\issues\2026-11-17-oland-nature-tree-leaves-missing-
     * most-frames.md): B, G and R multiplied, alpha the texel's. Its four
     * variants sit at 4000h of the code space, past the scratch and
     * before the two-unit slots (gt218_reserve.h). */
    v9x_nv50_sh_init(&sh, expect_words, V9X_NV50_D3D_FP_WORDS_MAX);
    expect_prologue(&sh);
    v9x_nv50_sh_interp(&sh, R(8), V(0x24ul), R(4));
    v9x_nv50_sh_interp(&sh, R(9), V(0x28ul), R(4));
    v9x_nv50_sh_tex(&sh, R(8), 0ul, 0ul, 0xful, 0ul);
    for (index = 0ul; index < 3ul; ++index) {
        v9x_nv50_sh_fmul(&sh, SHORT, R(index), R(index), R(8ul + index));
    }
    v9x_nv50_sh_mov(&sh, SHORT, R(3), R(11));
    v9x_nv50_sh_mov(&sh, LONG, R(3), R(3));
    v9x_nv50_sh_exit(&sh);
    CHECK(v9x_nv50_d3d_fp_variant(V9X_NV50_D3D_OP_MODULATE_RGB, 0ul, 0ul) ==
          12ul);
    check_program(12ul, &sh, 12ul);
    CHECK(v9x_nv50_d3d_fp_start(12ul) == 0x4000ul);
    CHECK(v9x_nv50_d3d_fp_start(15ul) + V9X_NV50_D3D_FP_STRIDE <=
          V9X_NV50_D3D_FP2_FIRST);
    CHECK(V9X_NV50_3D_SCRATCH_FP + v9x_nv50_d3d_fp_start(12ul) >=
          V9X_NV50_3D_SCRATCH_BYTES);
    CHECK(V9X_GT218_RESERVE_3D_SCRATCH + V9X_NV50_3D_SCRATCH_FP +
              v9x_nv50_d3d_fp_start(15ul) + V9X_NV50_D3D_FP_STRIDE <=
          V9X_GT218_RESERVE_3D_FP2);

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

    /* B8G8R8X8: the 8888 sizes with alpha the constant one, Mesa's F3
     * B8G8R8X8_UNORM row (V9XD32 T22X). */
    CHECK(v9x_nv50_d3d_texture(V9X_NV50_D3D_TEX_B8G8R8X8, 0x4000ul, 64ul,
                               16ul, 16ul, 0ul, 0ul, 0ul, tic, tsc));
    CHECK(tic[0] == 0x3a712488ul);
    CHECK(!v9x_nv50_d3d_texture(0ul, 0x4000ul, 64ul, 16ul, 16ul, 0ul, 0ul,
                                0ul, tic, tsc));

    /* The signed formats (step 6), Mesa's nv50_formats.c rows: Q8W8V8U8
     * as R8G8B8A8_SNORM (A8B8G8R8 sizes, SNORM 1 in all four types, R G B
     * A to X Y Z W: U V W Q in memory order); V8U8 as R8G8_SNORM (G8R8
     * sizes, R G to X Y, zero and one float to Z and W). */
    CHECK(v9x_nv50_d3d_texture(V9X_NV50_D3D_TEX_Q8W8V8U8, 0x4000ul, 64ul,
                               8ul, 8ul, 0ul, 0ul, 0ul, tic, tsc));
    CHECK(tic[0] == (0x08ul | 0x9240ul | (2ul << 18) | (3ul << 21) |
                     (4ul << 24) | (5ul << 27)));
    CHECK(tic[2] == 0xd005d000ul && tic[3] == 64ul);
    CHECK(v9x_nv50_d3d_texture(V9X_NV50_D3D_TEX_V8U8, 0x4000ul, 64ul, 8ul,
                               8ul, 0ul, 0ul, 0ul, tic, tsc));
    CHECK(tic[0] == (0x18ul | 0x9240ul | (2ul << 18) | (3ul << 21) |
                     (0ul << 24) | (7ul << 27)));
}

/* Mipmaps (docs\plans\nvidia-gt218-mipmaps.md): Mesa's nv50_miptree.c
 * tiled layout with nv50_tex_choose_tile_dims, and the 2D block-linear TIC
 * with a mip filter and its LOD clamp. */
static void test_mip(void)
{
    v9x_u32 tic[V9X_NV50_3D_TIC_WORDS];
    v9x_u32 tsc[V9X_NV50_3D_TSC_WORDS];
    v9x_u32 offsets[V9X_NV50_D3D_MIP_LEVELS_MAX];
    v9x_u32 pitches[V9X_NV50_D3D_MIP_LEVELS_MAX];
    v9x_u32 rows[V9X_NV50_D3D_MIP_LEVELS_MAX];
    v9x_u32 modes[V9X_NV50_D3D_MIP_LEVELS_MAX];
    v9x_u32 total = 0ul;

    /* V9XD32 T21's 32x32 RGB565 chain of six: each level's pitch on the
     * 64-byte GOB; its block the smallest of 4 to 64 rows covering twice
     * its height (32 rows -> 64 -> mode 30h, 32-row blocks; 16 -> 20h;
     * 8 -> 10h; 4, 2 and 1 -> 0, 4-row blocks); levels back to back. */
    CHECK(v9x_nv50_d3d_mip_layout(32ul, 32ul, 6ul, 2ul, offsets, pitches,
                                  rows, modes, &total));
    CHECK(modes[0] == 0x30ul && modes[1] == 0x20ul && modes[2] == 0x10ul &&
          modes[3] == 0ul && modes[4] == 0ul && modes[5] == 0ul);
    CHECK(pitches[0] == 64ul && pitches[5] == 64ul);
    CHECK(rows[0] == 32ul && rows[1] == 16ul && rows[2] == 8ul &&
          rows[3] == 4ul && rows[4] == 4ul && rows[5] == 4ul);
    CHECK(offsets[0] == 0ul && offsets[1] == 2048ul &&
          offsets[2] == 3072ul && offsets[3] == 3584ul &&
          offsets[4] == 3840ul && offsets[5] == 4096ul);
    CHECK(total == 4352ul);
    /* 256x128 ARGB8888, two levels: 1024-byte rows; 128 rows -> 256 ->
     * mode 40h, 64-row blocks; 64 rows -> 128 -> 40h as well. */
    CHECK(v9x_nv50_d3d_mip_layout(256ul, 128ul, 2ul, 4ul, offsets, pitches,
                                  rows, modes, &total));
    CHECK(modes[0] == 0x40ul && modes[1] == 0x40ul);
    CHECK(pitches[0] == 1024ul && pitches[1] == 512ul);
    CHECK(rows[0] == 128ul && rows[1] == 64ul);
    CHECK(offsets[1] == 131072ul && total == 131072ul + 32768ul);
    CHECK(!v9x_nv50_d3d_mip_layout(0ul, 8ul, 1ul, 4ul, offsets, pitches,
                                   rows, modes, &total));
    CHECK(!v9x_nv50_d3d_mip_layout(8ul, 8ul,
                                   V9X_NV50_D3D_MIP_LEVELS_MAX + 1ul, 4ul,
                                   offsets, pitches, rows, modes, &total));

    /* The pitch TIC turned into the chain: TWO_D (4000h) block-linear,
     * level 0's tile mode, last level 5 in word 5; MIPNEAREST (80h) and
     * the maximum LOD 5.0 in 4.8 fixed point at word 2 bit 12. */
    CHECK(v9x_nv50_d3d_texture(V9X_NV50_D3D_TEX_B5G6R5, 0x4000ul, 64ul,
                               32ul, 32ul, V9X_NV50_D3D_WRAP_REPEAT, 0ul,
                               0ul, tic, tsc));
    v9x_nv50_d3d_tic_mip(0x20456000ul, 0x30ul, 5ul, 0ul, tic, tsc);
    CHECK(tic[0] == 0x3a712495ul);
    CHECK(tic[1] == 0x20456000ul);
    CHECK(tic[2] == (0xd0001000ul | 0x00004000ul | 0x00c00000ul));
    CHECK(tic[3] == 0x00300000ul);
    CHECK(tic[4] == (0x80000000ul | 32ul));
    CHECK(tic[5] == (0x50000000ul | 0x00010000ul | 32ul));
    /* Word 7 on the GT218's class (past NV50_3D_CLASS in nv50_tex.c): the
     * view's last level << 4 and first level 0. Left 0, as the NV50 class
     * has it, the sampler read level 0 at every size (V9XD32 T21,
     * W9QI0DAD boot 148). */
    CHECK(tic[6] == 0x03000000ul && tic[7] == (5ul << 4));
    CHECK(tsc[0] == 0x00024000ul);
    CHECK(tsc[1] == (0x01ul | 0x10ul | 0x80ul));
    CHECK(tsc[2] == (0x500ul << 12));
    /* Linear between levels: C0h. */
    v9x_nv50_d3d_tic_mip(0x20456000ul, 0x30ul, 5ul, 1ul, tic, tsc);
    CHECK((tsc[1] & 0xc0ul) == 0xc0ul);
}

/* Cube maps (step 6 of docs\plans\nvidia-gt218-ps11.md): the tiled copy's
 * layout and the TIC that samples it, as Mesa's nv50_miptree.c and
 * nv50_tex.c lay out and describe a block-linear cube of one level. */
static void test_cube(void)
{
    v9x_u32 tic[V9X_NV50_3D_TIC_WORDS];
    v9x_u32 tsc[V9X_NV50_3D_TSC_WORDS];
    v9x_u32 pitch = 0ul;
    v9x_u32 rows = 0ul;
    v9x_u32 layer = 0ul;

    /* 64 texels of 4 bytes: 256-byte rows, 64 rows (a whole number of
     * 8-row blocks), 16 KiB a face, already on the 512-byte tile. */
    CHECK(v9x_nv50_d3d_cube_layout(64ul, 4ul, &pitch, &rows, &layer));
    CHECK(pitch == 256ul && rows == 64ul && layer == 16384ul);
    /* 4 texels of 2 bytes: one 64-byte GOB row, 8 rows, 512 bytes. */
    CHECK(v9x_nv50_d3d_cube_layout(4ul, 2ul, &pitch, &rows, &layer));
    CHECK(pitch == 64ul && rows == 8ul && layer == 512ul);
    /* 20 texels of 4 bytes: 80 bytes to 128, 20 rows to 24: 3072 bytes,
     * six tiles. */
    CHECK(v9x_nv50_d3d_cube_layout(20ul, 4ul, &pitch, &rows, &layer));
    CHECK(pitch == 128ul && rows == 24ul && layer == 3072ul);
    CHECK(!v9x_nv50_d3d_cube_layout(0ul, 4ul, &pitch, &rows, &layer));
    CHECK(!v9x_nv50_d3d_cube_layout(16ul, 3ul, &pitch, &rows, &layer));

    /* The pitch TIC turned into a block-linear cube: word 0 kept; word 1
     * the copy's address; word 2 Mesa's 10001000h base with border and
     * normalized coordinates as the pitch TIC has them, CUBEMAP (C000h)
     * and two GOBs a block (tile mode 10h, (10h & F0h) << 18); words 3-6
     * Mesa's block-linear values, depth one cube, one level. */
    CHECK(v9x_nv50_d3d_texture(V9X_NV50_D3D_TEX_B8G8R8A8, 0x4000ul, 256ul,
                               64ul, 64ul, V9X_NV50_D3D_WRAP_REPEAT, 1ul,
                               1ul, tic, tsc));
    v9x_nv50_d3d_tic_cube(0x20123400ul, 0x10ul, tic, tsc);
    CHECK(tic[0] == 0x2a712488ul);
    CHECK(tic[1] == 0x20123400ul);
    CHECK(tic[2] == (0xd0001000ul | 0x0000c000ul | 0x00400000ul));
    CHECK(tic[3] == 0x00300000ul);
    CHECK(tic[4] == (0x80000000ul | 64ul));
    CHECK(tic[5] == (0x00010000ul | 64ul));
    CHECK(tic[6] == 0x03000000ul && tic[7] == 0ul);
    /* A cube's coordinates are clamped whatever the stage's mode. */
    CHECK(tsc[0] == (0x00024000ul | 2ul | (2ul << 3) | (2ul << 6)));
    CHECK(tsc[1] == 0x00000062ul);
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
          words[at + 1ul] == 0x33ul);
    header = find(&push, V9X_8597_VP_REG_ALLOC_RESULT, &at);
    CHECK(header != 0xfffffffful && words[at] == 16ul);
    header = find(&push, V9X_8597_VP_RESULT_MAP_SIZE, &at);
    CHECK(header != 0xfffffffful && words[at] == 16ul);
    header = find(&push, V9X_8597_VP_RESULT_MAP0, &at);
    CHECK(header != 0xfffffffful && ((header >> 18) & 0x7fful) == 4ul &&
          words[at] == 0x03020100ul && words[at + 1ul] == 0x07060504ul &&
          words[at + 2ul] == 0x0b0a0908ul && words[at + 3ul] == 0x0f0e0d0cul);
    header = find(&push, V9X_8597_FP_INTERPOLANT_CTRL, &at);
    CHECK(header != 0xfffffffful && words[at] == 0x080c040cul &&
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

/*
 * Two units (step 5 of docs\plans\nvidia-gt218-ps11.md): every
 * combination v9x_nv50_d3d_combine_key numbers, built by
 * v9x_nv50_d3d_fp2 and run in the Tesla model (nv50_sh_sim.c) at a few
 * fragments, held to the combine arithmetic in C: unit 0 on the vertex
 * colour, unit 1 on its result, ADD saturated, then specular added and
 * fog as (c - fog) f + fog with f the specular alpha.
 */
#define FP2_WEIGHT 0.5f

static float fp2_attribute(v9x_u32 fragment, v9x_u32 a, v9x_u32 c)
{
    return 0.08f + 0.11f * (float)a + 0.07f * (float)c +
           0.05f * (float)fragment - 0.04f * (float)((a + c) % 3ul);
}

static void fp2_texel(v9x_u32 unit, v9x_u32 argc, v9x_u32 cube,
                      const float *coords, float rgba[4])
{
    v9x_u32 k;

    (void)argc;
    (void)cube;
    for (k = 0ul; k < 4ul; ++k) {
        rgba[k] = 0.15f + 0.17f * (float)k + 0.2f * (float)unit +
                  0.3f * coords[0] - 0.1f * coords[1];
        if (rgba[k] > 1.0f) {
            rgba[k] -= 0.7f;
        }
    }
}

static void fp2_unit(v9x_u32 color, v9x_u32 alpha, const float *t, float *c)
{
    v9x_u32 k;

    for (k = 0ul; k < 3ul; ++k) {
        switch (color) {
        case V9X_NV50_D3D_COLOR_REPLACE:  c[k] = t[k]; break;
        case V9X_NV50_D3D_COLOR_MODULATE: c[k] = c[k] * t[k]; break;
        case V9X_NV50_D3D_COLOR_DECAL:    c[k] += t[3] * (t[k] - c[k]); break;
        default:
            c[k] += t[k];
            if (c[k] > 1.0f) {
                c[k] = 1.0f;
            }
            break;
        }
    }
    if (alpha == V9X_NV50_D3D_ALPHA_REPLACE) {
        c[3] = t[3];
    } else if (alpha == V9X_NV50_D3D_ALPHA_MODULATE) {
        c[3] *= t[3];
    }
}

static void test_fp2(void)
{
    static v9x_u32 words[V9X_NV50_D3D_FP2_WORDS_MAX];
    static struct v9x_nv50_sim sim;
    static const float fog[3] = { 0.25f, 0.5f, 0.75f };
    struct v9x_nv50_d3d_combine c;
    v9x_u32 seen[V9X_NV50_D3D_COLOR_OPS * V9X_NV50_D3D_ALPHA_OPS *
                 V9X_NV50_D3D_COLOR_OPS * V9X_NV50_D3D_ALPHA_OPS * 4ul];
    v9x_u32 total = sizeof(seen) / sizeof(seen[0]);
    v9x_u32 key;
    v9x_u32 count;
    v9x_u32 registers;
    v9x_u32 largest = 0ul;
    v9x_u32 bad = 0ul;
    v9x_u32 fragment;
    v9x_u32 k;

    for (key = 0ul; key < total; ++key) {
        seen[key] = 0ul;
    }
    for (key = 0ul; key < total; ++key) {
        v9x_u32 rest = key;

        c.fog = rest % 2ul;
        rest /= 2ul;
        c.specular = rest % 2ul;
        rest /= 2ul;
        c.alpha[1] = rest % V9X_NV50_D3D_ALPHA_OPS;
        rest /= V9X_NV50_D3D_ALPHA_OPS;
        c.color[1] = rest % V9X_NV50_D3D_COLOR_OPS;
        rest /= V9X_NV50_D3D_COLOR_OPS;
        c.alpha[0] = rest % V9X_NV50_D3D_ALPHA_OPS;
        c.color[0] = rest / V9X_NV50_D3D_ALPHA_OPS;
        CHECK(v9x_nv50_d3d_combine_key(&c) == key);
        if (v9x_nv50_d3d_combine_key(&c) < total) {
            ++seen[v9x_nv50_d3d_combine_key(&c)];
        }
        count = v9x_nv50_d3d_fp2(&c, words, V9X_NV50_D3D_FP2_WORDS_MAX,
                                 &registers);
        CHECK(count != 0ul && registers <= 20ul);
        if (count == 0ul) {
            continue;
        }
        if (count > largest) {
            largest = count;
        }
        for (fragment = 0ul; fragment < 3ul; ++fragment) {
            float colour[4];
            float t[4];
            float uv[2];
            v9x_u32 a;

            memset(&sim, 0xcc, sizeof(sim));
            sim.killed = 0;
            sim.error = 0;
            sim.slot = 0ul;
            memset(sim.flags, 0, sizeof(sim.flags));
            for (k = 0ul; k < 3ul; ++k) {
                sim.c[k] = fog[k];
                sim.c[4ul + k] = -fog[k];
            }
            sim.v[0] = FP2_WEIGHT;
            /* v0 at 1-4, v1 5-8, unit 0's u v 9-10, unit 1's 11-12. */
            for (a = 0ul; a < 3ul; ++a) {
                for (k = 0ul; k < 4ul; ++k) {
                    if (a == 2ul && k >= 2ul) {
                        break;
                    }
                    sim.v[1ul + a * 4ul + k] =
                        fp2_attribute(fragment, a, k) * FP2_WEIGHT;
                }
            }
            sim.v[11] = fp2_attribute(fragment, 3ul, 0ul) * FP2_WEIGHT;
            sim.v[12] = fp2_attribute(fragment, 3ul, 1ul) * FP2_WEIGHT;
            if (!v9x_nv50_sim_run(&sim, words, count, fp2_texel)) {
                printf("FAIL fp2 key %lu: the model stopped at word %lu "
                       "(%s)\n", (unsigned long)key,
                       (unsigned long)sim.error_at, sim.error);
                ++failures;
                return;
            }
            for (k = 0ul; k < 4ul; ++k) {
                colour[k] = fp2_attribute(fragment, 0ul, k);
            }
            uv[0] = fp2_attribute(fragment, 2ul, 0ul);
            uv[1] = fp2_attribute(fragment, 2ul, 1ul);
            fp2_texel(0ul, 2ul, 0ul, uv, t);
            fp2_unit(c.color[0], c.alpha[0], t, colour);
            uv[0] = fp2_attribute(fragment, 3ul, 0ul);
            uv[1] = fp2_attribute(fragment, 3ul, 1ul);
            fp2_texel(1ul, 2ul, 0ul, uv, t);
            fp2_unit(c.color[1], c.alpha[1], t, colour);
            if (c.specular != 0ul) {
                for (k = 0ul; k < 3ul; ++k) {
                    colour[k] += fp2_attribute(fragment, 1ul, k);
                    colour[k] = colour[k] > 1.0f ? 1.0f : colour[k];
                }
            }
            if (c.fog != 0ul) {
                float f = fp2_attribute(fragment, 1ul, 3ul);

                for (k = 0ul; k < 3ul; ++k) {
                    colour[k] = (colour[k] - fog[k]) * f + fog[k];
                }
            }
            for (k = 0ul; k < 4ul; ++k) {
                float got = v9x_nv50_sim_reg(&sim, k);
                float diff = got - colour[k];

                if (!(diff < 1.0e-5f && diff > -1.0e-5f)) {
                    if (bad == 0ul) {
                        printf("FAIL fp2 key %lu fragment %lu channel %lu: "
                               "program %f, expected %f\n",
                               (unsigned long)key, (unsigned long)fragment,
                               (unsigned long)k, (double)got,
                               (double)colour[k]);
                    }
                    ++bad;
                }
            }
        }
    }
    CHECK(bad == 0ul);
    for (key = 0ul; key < total; ++key) {
        CHECK(seen[key] == 1ul);
    }
    CHECK(largest <= V9X_NV50_D3D_FP2_WORDS_MAX);
    printf("INFO nv50_d3d fp2: %lu combinations held to their arithmetic, "
           "the largest %lu words\n", (unsigned long)total,
           (unsigned long)largest);

    /* Ops past the set have no program. */
    c.color[0] = V9X_NV50_D3D_COLOR_OPS;
    c.alpha[0] = 0ul;
    CHECK(v9x_nv50_d3d_combine_key(&c) == V9X_NV50_D3D_COMBINE_NONE);
    CHECK(v9x_nv50_d3d_fp2(&c, words, V9X_NV50_D3D_FP2_WORDS_MAX,
                           &registers) == 0ul);
    CHECK(v9x_nv50_d3d_fp2_start(3ul) == 0x7600ul);
}

/*
 * Pixel shader draws (step 4 of nvidia-gt218-ps11.md): /3d:ps11's
 * interface from the state, the shader vertex program, the constant block
 * and the wide vertex.
 */
static void test_shader(void)
{
    static v9x_u32 words[V9X_NV50_D3D_STATE_WORDS_MAX + 80ul];
    v9x_u32 vp[V9X_NV50_D3D_SHADER_VP_WORDS_MAX];
    v9x_u32 expect[V9X_NV50_D3D_SHADER_VP_WORDS_MAX];
    v9x_u32 block[4];
    float wide[28];
    struct v9x_nv50_d3d_state state;
    struct v9x_nv50_push push;
    struct v9x_nv50_sh sh;
    v9x_u32 at = 0ul;
    v9x_u32 header;
    v9x_u32 index;
    v9x_u32 binds;

    /* The vertex program: 28 moves, attribute to result. */
    v9x_nv50_sh_init(&sh, expect, V9X_NV50_D3D_SHADER_VP_WORDS_MAX);
    for (index = 0ul; index < 28ul; ++index) {
        v9x_nv50_sh_mov(&sh, LONG, V9X_NV50_SH_O(index * 4ul),
                        V9X_NV50_SH_A(index * 4ul));
    }
    v9x_nv50_sh_exit(&sh);
    CHECK(v9x_nv50_d3d_shader_vp(vp, V9X_NV50_D3D_SHADER_VP_WORDS_MAX) ==
          sh.count);
    for (index = 0ul; index < sh.count; ++index) {
        CHECK(vp[index] == expect[index]);
    }
    CHECK(v9x_nv50_d3d_ps_start(2ul) == 0x11000ul);

    base_state(&state);
    state.shader = 1ul;
    state.kills = 1ul;
    state.fp_start = v9x_nv50_d3d_ps_start(1ul);
    state.fp_registers = 64ul;
    state.code_flush = 1ul;
    v9x_nv50_push_init(&push, words, V9X_NV50_D3D_STATE_WORDS_MAX);
    v9x_nv50_push_d3d_state(&push, &state);
    CHECK(push.overflow == 0ul);
    header = find(&push, V9X_8597_VP_START_ID, &at);
    CHECK(header != 0xfffffffful && words[at] == 0x600ul);
    CHECK(find(&push, 0x1440ul, &at) != 0xfffffffful);   /* CODE_CB_FLUSH */
    header = find(&push, V9X_8597_FP_START_ID, &at);
    CHECK(header != 0xfffffffful && words[at] == 0x10000ul);
    header = find(&push, 0x19a8ul, &at);                  /* FP_CONTROL */
    CHECK(header != 0xfffffffful && words[at] == 0x00100000ul);
    header = find(&push, V9X_8597_VP_ATTR_EN0, &at);
    CHECK(header != 0xfffffffful && words[at] == 0x0ffffffful &&
          words[at + 1ul] == 0ul);
    header = find(&push, V9X_8597_VP_RESULT_MAP_SIZE, &at);
    CHECK(header != 0xfffffffful && words[at] == 28ul);
    header = find(&push, V9X_8597_VP_RESULT_MAP0, &at);
    CHECK(header != 0xfffffffful && ((header >> 18) & 0x7fful) == 7ul &&
          words[at] == 0x03020100ul && words[at + 6ul] == 0x1b1a1918ul);
    header = find(&push, V9X_8597_FP_INTERPOLANT_CTRL, &at);
    CHECK(header != 0xfffffffful && words[at] == 0x08180418ul &&
          words[at + 1ul] == 64ul);
    /* All four units bound: TIC N for unit N. */
    binds = 0ul;
    at = 0ul;
    while (find_from(&push, V9X_8597_BIND_TIC2, at, &at) != 0xfffffffful) {
        CHECK(words[at] == ((binds << 9) | (binds << 1) | 1ul));
        ++binds;
        ++at;
    }
    CHECK(binds == 4ul);

    /* A fixed-function draw writes FP_CONTROL 0, after a shader killed. */
    base_state(&state);
    v9x_nv50_push_init(&push, words, V9X_NV50_D3D_STATE_WORDS_MAX);
    v9x_nv50_push_d3d_state(&push, &state);
    header = find(&push, 0x19a8ul, &at);
    CHECK(header != 0xfffffffful && words[at] == 0ul);

    /* The constant block: CB_ADDR entry 0 of buffer 2, then the words
     * non-incrementing. */
    for (index = 0ul; index < 4ul; ++index) {
        block[index] = 0x3f800000ul + index;
    }
    v9x_nv50_push_init(&push, words, 16ul);
    v9x_nv50_push_d3d_constants(&push, block, 4ul);
    CHECK(push.count == 7ul && words[1] == V9X_8597_CB_FP &&
          (words[2] & V9X_NV50_PUSH_NONINC) != 0ul && words[6] == block[3]);

    /* A wide vertex: attributes 1-6, then the position with the centre
     * added and rhw as the weight (1 for an rhw that is not positive). */
    for (index = 0ul; index < 28ul; ++index) {
        wide[index] = 0.25f * (float)index;
    }
    wide[3] = -1.0f;
    v9x_nv50_push_init(&push, words, 64ul);
    v9x_nv50_push_d3d_wide(&push, wide, 0.5f);
    CHECK(push.count == V9X_NV50_D3D_WIDE_WORDS);
    CHECK(words[0] == V9X_NV50_PUSH_HEADER(V9X_NV50_SUBC_3D, 0x0510ul, 4ul));
    CHECK(words[1] == 0x3f800000ul);                      /* wide[4] 1.0 */
    CHECK(words[25] == V9X_NV50_PUSH_HEADER(V9X_NV50_SUBC_3D, 0x0560ul,
                                            4ul));
    CHECK(words[30] == V9X_NV50_PUSH_HEADER(V9X_NV50_SUBC_3D, 0x0500ul,
                                            4ul));
    CHECK(words[31] == 0x3f000000ul);                     /* 0 + 0.5 */
    CHECK(words[32] == 0x3f400000ul);                     /* 0.25 + 0.5 */
    CHECK(words[34] == 0x3f800000ul);                     /* weight 1 */
}

unsigned int v9x_run_nv50_d3d_tests(void)
{
    failures = 0u;
    test_vp();
    test_fp();
    test_fp2();
    test_texture();
    test_cube();
    test_mip();
    test_state();
    test_vertex();
    test_shader();
    return failures;
}
