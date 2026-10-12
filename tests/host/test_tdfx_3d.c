#include <stdio.h>
#include <string.h>

#include "velocity9x/tdfx_3d.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

#define MIB(n) ((v9x_u32)(n) * 1048576ul)

/* IEEE single bits, worked out by hand. */
#define F_0     0x00000000ul
#define F_0_5   0x3f000000ul
#define F_1     0x3f800000ul
#define F_10    0x41200000ul
#define F_20    0x41a00000ul
#define F_100   0x42c80000ul
#define F_255   0x437f0000ul
#define F_32768 0x47000000ul

/*
 * A 640x480 RGB565 target at 16 MiB in a 32 MiB card: the desktop line on
 * the Voodoo4 is 15 MiB, so this is where DirectDraw's heap would put a
 * back buffer.
 */
static void base_state(struct v9x_tdfx_3d_state *state)
{
    memset(state, 0, sizeof(*state));
    state->vram_bytes = MIB(32);
    state->target.offset = MIB(16);
    state->target.pitch_bytes = 1280ul;
    state->width = 640ul;
    state->height = 480ul;
}

static void base_vertices(struct v9x_tdfx_3d_vertex *v)
{
    memset(v, 0, 3u * sizeof(*v));
    v[0].x = 10.0f;  v[0].y = 10.0f;
    v[1].x = 100.0f; v[1].y = 10.0f;
    v[2].x = 10.0f;  v[2].y = 100.0f;
    v[0].r = 255.0f; v[1].g = 255.0f; v[2].b = 255.0f;
    v[0].a = v[1].a = v[2].a = 255.0f;
    v[0].z = v[1].z = v[2].z = 32768.0f;
    v[0].oow = v[1].oow = v[2].oow = 1.0f;
    v[1].sow = 20.0f;
    v[2].tow = 20.0f;
}

/* The last value written to offset - what the register holds after the
 * writes - or 0xDEADBEEF when it is not written. */
static v9x_u32 value_at(const struct v9x_tdfx_3d_words *words, v9x_u32 offset)
{
    v9x_u32 index;
    v9x_u32 value = 0xdeadbeeful;

    for (index = 0u; index < words->count; ++index) {
        if (words->offsets[index] == offset) {
            value = words->values[index];
        }
    }
    return value;
}

/* How many times offset is written. */
static v9x_u32 times_written(const struct v9x_tdfx_3d_words *words,
                             v9x_u32 offset)
{
    v9x_u32 index;
    v9x_u32 count = 0u;

    for (index = 0u; index < words->count; ++index) {
        if (words->offsets[index] == offset) {
            ++count;
        }
    }
    return count;
}

static v9x_u32 written(const struct v9x_tdfx_3d_words *words, v9x_u32 offset)
{
    return value_at(words, offset) != 0xdeadbeeful;
}

/* Untextured, no depth, no blend: Gouraud colour passed straight through. */
static void test_state_plain(void)
{
    struct v9x_tdfx_3d_state state;
    struct v9x_tdfx_3d_words words;

    base_state(&state);
    CHECK(v9x_tdfx_3d_build_state(&state, &words) == V9X_STATUS_OK);

    /* renderMode 1:0 = 0, 16 bpp RGB565 (section 8.26). */
    CHECK(value_at(&words, V9X_TDFX_3D_RENDER_MODE) == 0ul);
    /* Linear: base as the byte address, stride in bytes, bit 15 clear. */
    CHECK(value_at(&words, V9X_TDFX_3D_COL_BUFFER_ADDR) == 0x01000000ul);
    CHECK(value_at(&words, V9X_TDFX_3D_COL_BUFFER_STRIDE) == 0x00000500ul);
    /* No depth: the aux buffer is left alone. */
    CHECK(!written(&words, V9X_TDFX_3D_AUX_BUFFER_ADDR));
    /* Clip: left 27:16 = 0, right 11:0 = 640; top 0, bottom 480. */
    CHECK(value_at(&words, V9X_TDFX_3D_CLIP_LEFT_RIGHT) == 0x00000280ul);
    CHECK(value_at(&words, V9X_TDFX_3D_CLIP_TOP_BOTTOM) == 0x000001e0ul);
    /* fbzMode: clip enable (0) and RGB write (9). */
    CHECK(value_at(&words, V9X_TDFX_3D_FBZ_MODE) == 0x00000201ul);
    /* fbzColorPath 0: iterated RGB and alpha as "other", multiplied by
     * 1 - 0 (select zero, reverse blend clear), nothing added. */
    CHECK(value_at(&words, V9X_TDFX_3D_FBZ_COLOR_PATH) == 0ul);
    CHECK(value_at(&words, V9X_TDFX_3D_ALPHA_MODE) == 0ul);
    CHECK(value_at(&words, V9X_TDFX_3D_FOG_MODE) == 0ul);
    /* No texture: TMU0 is not touched. */
    CHECK(!written(&words, V9X_TDFX_3D_TMU0 | V9X_TDFX_3D_TEXTURE_MODE));

    /* The order: renderMode first, the buffers, clip, then the pipeline. */
    CHECK(words.offsets[0] == V9X_TDFX_3D_RENDER_MODE);
    CHECK(words.offsets[1] == V9X_TDFX_3D_COL_BUFFER_ADDR);
}

static void test_state_depth_blend(void)
{
    struct v9x_tdfx_3d_state state;
    struct v9x_tdfx_3d_words words;

    base_state(&state);
    state.depth_enable = 1ul;
    state.depth.offset = MIB(16) + 0x00097000ul;
    state.depth.pitch_bytes = 1280ul;
    state.depth_func = V9X_TDFX_CMP_LESS;
    state.depth_write = 1ul;
    state.blend_enable = 1ul;
    state.blend_src = V9X_TDFX_BLEND_SRC_ALPHA;
    state.blend_dst = V9X_TDFX_BLEND_INV_SRC_ALPHA;
    CHECK(v9x_tdfx_3d_build_state(&state, &words) == V9X_STATUS_OK);

    CHECK(value_at(&words, V9X_TDFX_3D_AUX_BUFFER_ADDR) == 0x01097000ul);
    CHECK(value_at(&words, V9X_TDFX_3D_AUX_BUFFER_STRIDE) == 0x00000500ul);
    /* fbzMode: clip 0, depth enable 4, function LESS (1) in 7:5, RGB write
     * 9, depth write 10: 0x1 | 0x10 | 0x20 | 0x200 | 0x400. */
    CHECK(value_at(&words, V9X_TDFX_3D_FBZ_MODE) == 0x00000631ul);
    /* alphaMode: blend 4; RGB source 11:8 = 1, destination 15:12 = 5;
     * alpha source 19:16 = ONE (4), destination 23:20 = ZERO, since a
     * 16-bit target has no alpha to blend into. */
    CHECK(value_at(&words, V9X_TDFX_3D_ALPHA_MODE) == 0x00045110ul);

    /* Alpha test: bit 0, function 3:1, reference 31:24. */
    base_state(&state);
    state.alpha_test = 1ul;
    state.alpha_func = V9X_TDFX_CMP_GREATER;
    state.alpha_ref = 0x80ul;
    CHECK(v9x_tdfx_3d_build_state(&state, &words) == V9X_STATUS_OK);
    CHECK(value_at(&words, V9X_TDFX_3D_ALPHA_MODE) == 0x80000009ul);

    /* Dither (fbzMode 8) only when asked; exact-pixel probes leave it off. */
    base_state(&state);
    state.dither = 1ul;
    CHECK(v9x_tdfx_3d_build_state(&state, &words) == V9X_STATUS_OK);
    CHECK(value_at(&words, V9X_TDFX_3D_FBZ_MODE) == 0x00000301ul);
}

static void test_state_texture(void)
{
    struct v9x_tdfx_3d_state state;
    struct v9x_tdfx_3d_words words;

    base_state(&state);
    state.texture_enable = 1ul;
    state.texture.offset = MIB(17);
    state.texture.format = V9X_TDFX_TEX_RGB565;
    state.texture.log2_size = 6ul;
    state.texture.combine = V9X_TDFX_COMBINE_REPLACE;
    CHECK(v9x_tdfx_3d_build_state(&state, &words) == V9X_STATUS_OK);

    /* Replace: RGB select TMU (1:0 = 1), alpha select TMU (3:2 = 1),
     * texture enable 27. */
    CHECK(value_at(&words, V9X_TDFX_3D_FBZ_COLOR_PATH) == 0x08000005ul);
    /* textureMode on TMU0: perspective 0; format 10 in 11:8; colour
     * combine replace (zero other 12, add local 18); alpha combine replace
     * (zero other 21, add local 27). */
    CHECK(value_at(&words, V9X_TDFX_3D_TMU0 | V9X_TDFX_3D_TEXTURE_MODE) ==
          0x08241a01ul);
    /* tLOD: 64 texels is LOD 2; LOD min 5:0 and max 11:6 in 4.2 fixed point,
     * so 8 and 8. */
    CHECK(value_at(&words, V9X_TDFX_3D_TMU0 | V9X_TDFX_3D_TLOD) ==
          0x00000208ul);
    /* texBaseAddr: where a full 256-texel level 0 would start, so the data
     * less levels 0 and 1, (65536 + 16384) x 2 = 28000h bytes:
     * 01100000h - 28000h = 010D8000h. */
    CHECK(value_at(&words, V9X_TDFX_3D_TMU0 | V9X_TDFX_3D_TEX_BASE_ADDR) ==
          0x010d8000ul);
    /* Written twice, a different base first: rewriting texBaseAddr is how
     * a CPU download reaches the texture cache (section 20.3 p.234). */
    CHECK(times_written(&words,
                        V9X_TDFX_3D_TMU0 | V9X_TDFX_3D_TEX_BASE_ADDR) == 2u);
    {
        v9x_u32 index;
        v9x_u32 first = 0u;

        for (index = 0u; index < words.count; ++index) {
            if (words.offsets[index] ==
                (V9X_TDFX_3D_TMU0 | V9X_TDFX_3D_TEX_BASE_ADDR)) {
                first = words.values[index];
                break;
            }
        }
        CHECK(first != 0x010d8000ul);
    }

    /* Modulate the colour, take alpha from the texel: Direct3D's MODULATE
     * on a texture with alpha. RGB select TMU, multiply by local, reverse;
     * alpha select TMU and passed on (alpha multiply select 0, reverse
     * clear). */
    state.texture.combine = V9X_TDFX_COMBINE_MODULATE_TEXALPHA;
    CHECK(v9x_tdfx_3d_build_state(&state, &words) == V9X_STATUS_OK);
    CHECK(value_at(&words, V9X_TDFX_3D_FBZ_COLOR_PATH) == 0x08002405ul);

    /* Modulate, bilinear, clamped: RGB select TMU, multiply by local (12:10
     * = 1) with reverse blend 13; alpha select TMU, alpha multiply by local
     * alpha (21:19 = 1) with reverse 22; texture enable. */
    state.texture.combine = V9X_TDFX_COMBINE_MODULATE;
    state.texture.min_bilinear = 1ul;
    state.texture.mag_bilinear = 1ul;
    state.texture.clamp = 1ul;
    CHECK(v9x_tdfx_3d_build_state(&state, &words) == V9X_STATUS_OK);
    CHECK(value_at(&words, V9X_TDFX_3D_FBZ_COLOR_PATH) == 0x08482405ul);
    /* Min and mag bilinear 1, 2; clamp S, T 6, 7. */
    CHECK(value_at(&words, V9X_TDFX_3D_TMU0 | V9X_TDFX_3D_TEXTURE_MODE) ==
          0x08241ac7ul);
    /* Each filter bit on its own. */
    state.texture.min_bilinear = 0ul;
    CHECK(v9x_tdfx_3d_build_state(&state, &words) == V9X_STATUS_OK);
    CHECK(value_at(&words, V9X_TDFX_3D_TMU0 | V9X_TDFX_3D_TEXTURE_MODE) ==
          0x08241ac5ul);
    state.texture.min_bilinear = 1ul;
    state.texture.mag_bilinear = 0ul;
    CHECK(v9x_tdfx_3d_build_state(&state, &words) == V9X_STATUS_OK);
    CHECK(value_at(&words, V9X_TDFX_3D_TMU0 | V9X_TDFX_3D_TEXTURE_MODE) ==
          0x08241ac3ul);

    /* A level-0 256 texture's base is its own offset. */
    state.texture.log2_size = 8ul;
    CHECK(v9x_tdfx_3d_build_state(&state, &words) == V9X_STATUS_OK);
    CHECK(value_at(&words, V9X_TDFX_3D_TMU0 | V9X_TDFX_3D_TEX_BASE_ADDR) ==
          0x01100000ul);
    CHECK(value_at(&words, V9X_TDFX_3D_TMU0 | V9X_TDFX_3D_TLOD) == 0ul);
}

static void test_state_refusals(void)
{
    struct v9x_tdfx_3d_state state;
    struct v9x_tdfx_3d_words words;

    /* Base 25:4: a target not 16-byte aligned cannot be expressed. */
    base_state(&state);
    state.target.offset += 8ul;
    CHECK(v9x_tdfx_3d_build_state(&state, &words) ==
          V9X_STATUS_INVALID_ARGUMENT);

    /* A linear stride is a multiple of 16 bytes, at most 13:0. */
    base_state(&state);
    state.target.pitch_bytes = 1288ul;
    CHECK(v9x_tdfx_3d_build_state(&state, &words) == V9X_STATUS_UNSUPPORTED);
    base_state(&state);
    state.target.pitch_bytes = 0x4000ul;
    state.width = 2048ul;
    CHECK(v9x_tdfx_3d_build_state(&state, &words) == V9X_STATUS_UNSUPPORTED);

    /* A pitch too narrow for the width. */
    base_state(&state);
    state.width = 641ul;
    CHECK(v9x_tdfx_3d_build_state(&state, &words) ==
          V9X_STATUS_INVALID_ARGUMENT);

    /* The target past the end of memory. */
    base_state(&state);
    state.target.offset = MIB(32) - 1280ul * 100ul;
    CHECK(v9x_tdfx_3d_build_state(&state, &words) ==
          V9X_STATUS_INVALID_ARGUMENT);

    /* Clip is 12 bits a side. */
    base_state(&state);
    state.width = 4096ul;
    state.target.pitch_bytes = 0x2000ul;
    state.height = 4ul;
    CHECK(v9x_tdfx_3d_build_state(&state, &words) == V9X_STATUS_UNSUPPORTED);

    /* Depth on the target's own page parity is accepted: the probe drew it
     * exactly on the 4500 (D36, A8U4I5 boot 428). */
    base_state(&state);
    state.depth_enable = 1ul;
    state.depth.offset = MIB(16) + 0x00096000ul;
    state.depth.pitch_bytes = 1280ul;
    CHECK(v9x_tdfx_3d_build_state(&state, &words) == V9X_STATUS_OK);

    /* Textures: a size above 256, a format outside the three, a misaligned
     * or out-of-memory level. */
    base_state(&state);
    state.texture_enable = 1ul;
    state.texture.offset = MIB(17);
    state.texture.format = V9X_TDFX_TEX_RGB565;
    state.texture.log2_size = 9ul;
    CHECK(v9x_tdfx_3d_build_state(&state, &words) == V9X_STATUS_UNSUPPORTED);
    state.texture.log2_size = 6ul;
    state.texture.format = 5ul;
    CHECK(v9x_tdfx_3d_build_state(&state, &words) == V9X_STATUS_UNSUPPORTED);
    state.texture.format = V9X_TDFX_TEX_ARGB4444;
    state.texture.offset = MIB(17) + 4ul;
    CHECK(v9x_tdfx_3d_build_state(&state, &words) ==
          V9X_STATUS_INVALID_ARGUMENT);
    /* 64 x 64 at two bytes a texel is 8 KiB: it fits exactly at the top,
     * and not 16 bytes higher. */
    state.texture.offset = MIB(32) - 64ul * 64ul * 2ul;
    CHECK(v9x_tdfx_3d_build_state(&state, &words) == V9X_STATUS_OK);
    state.texture.offset = MIB(32) - 64ul * 64ul * 2ul + 16ul;
    CHECK(v9x_tdfx_3d_build_state(&state, &words) ==
          V9X_STATUS_INVALID_ARGUMENT);

    CHECK(v9x_tdfx_3d_build_state(0, &words) == V9X_STATUS_INVALID_ARGUMENT);
}

static void test_triangle_plain(void)
{
    struct v9x_tdfx_3d_state state;
    struct v9x_tdfx_3d_vertex v[3];
    struct v9x_tdfx_3d_words words;
    v9x_u32 at;

    base_state(&state);
    base_vertices(v);
    words.count = 0u;
    CHECK(v9x_tdfx_3d_build_triangle(&state, v, &words) == V9X_STATUS_OK);

    /* sSetupMode: RGB (0) and alpha (1); strip, no culling. */
    CHECK(words.offsets[0] == V9X_TDFX_3D_S_SETUP_MODE);
    CHECK(words.values[0] == 0x00000003ul);

    /* Per vertex: X, Y, R, G, B, A, then the command. Six parameters and
     * a command, three times: 1 + 21 writes. */
    CHECK(words.count == 22u);
    at = 1u;
    CHECK(words.offsets[at] == V9X_TDFX_3D_S_VX && words.values[at] == F_10);
    CHECK(words.offsets[at + 1u] == V9X_TDFX_3D_S_VY &&
          words.values[at + 1u] == F_10);
    CHECK(words.offsets[at + 2u] == V9X_TDFX_3D_S_RED &&
          words.values[at + 2u] == F_255);
    CHECK(words.offsets[at + 3u] == V9X_TDFX_3D_S_GREEN &&
          words.values[at + 3u] == F_0);
    CHECK(words.offsets[at + 5u] == V9X_TDFX_3D_S_ALPHA &&
          words.values[at + 5u] == F_255);
    /* The first vertex begins, the next two draw. */
    CHECK(words.offsets[at + 6u] == V9X_TDFX_3D_S_BEGIN_TRI_CMD);
    CHECK(words.offsets[at + 13u] == V9X_TDFX_3D_S_DRAW_TRI_CMD);
    CHECK(words.offsets[at + 20u] == V9X_TDFX_3D_S_DRAW_TRI_CMD);
    CHECK(words.values[at + 7u] == F_100);
    CHECK(words.values[at + 15u] == F_100);
}

static void test_triangle_depth_texture(void)
{
    struct v9x_tdfx_3d_state state;
    struct v9x_tdfx_3d_vertex v[3];
    struct v9x_tdfx_3d_words words;
    v9x_u32 at;

    base_state(&state);
    state.depth_enable = 1ul;
    state.depth.offset = MIB(16) + 0x00097000ul;
    state.depth.pitch_bytes = 1280ul;
    state.depth_func = V9X_TDFX_CMP_LESS;
    state.texture_enable = 1ul;
    state.texture.offset = MIB(17);
    state.texture.format = V9X_TDFX_TEX_RGB565;
    state.texture.log2_size = 6ul;
    base_vertices(v);
    words.count = 0u;
    CHECK(v9x_tdfx_3d_build_triangle(&state, v, &words) == V9X_STATUS_OK);

    /* RGB, A, Z (2), Wb (3), W0 (4), S0/T0 (5). */
    CHECK(words.values[0] == 0x0000003ful);
    /* X, Y, R, G, B, A, Z, Wb, W0, S/W0, T/W0, command: 12 per vertex. */
    CHECK(words.count == 1u + 3u * 12u);
    at = 1u + 12u;
    CHECK(words.offsets[at + 6u] == V9X_TDFX_3D_S_VZ &&
          words.values[at + 6u] == F_32768);
    CHECK(words.offsets[at + 7u] == V9X_TDFX_3D_S_WB &&
          words.values[at + 7u] == F_1);
    CHECK(words.offsets[at + 8u] == V9X_TDFX_3D_S_WTMU0 &&
          words.values[at + 8u] == F_1);
    CHECK(words.offsets[at + 9u] == V9X_TDFX_3D_S_S_W0 &&
          words.values[at + 9u] == F_20);
    CHECK(words.offsets[at + 10u] == V9X_TDFX_3D_S_T_W0 &&
          words.values[at + 10u] == F_0);
    CHECK(words.offsets[at + 11u] == V9X_TDFX_3D_S_DRAW_TRI_CMD);

    /* Appended after what is already there; refused when it cannot fit. */
    words.count = V9X_TDFX_3D_WRITES - 10u;
    CHECK(v9x_tdfx_3d_build_triangle(&state, v, &words) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(words.count == V9X_TDFX_3D_WRITES - 10u);
}

static void test_float_bits(void)
{
    CHECK(v9x_tdfx_3d_float_bits(0.0f) == F_0);
    CHECK(v9x_tdfx_3d_float_bits(0.5f) == F_0_5);
    CHECK(v9x_tdfx_3d_float_bits(255.0f) == F_255);
    CHECK(v9x_tdfx_3d_float_bits(-1.0f) == 0xbf800000ul);
}

unsigned int v9x_run_tdfx_3d_tests(void)
{
    failures = 0u;
    test_float_bits();
    test_state_plain();
    test_state_depth_blend();
    test_state_texture();
    test_state_refusals();
    test_triangle_plain();
    test_triangle_depth_texture();
    return failures;
}
