#include <stdio.h>
#include <string.h>

#include "velocity9x/ati_mach64_engine.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

/* D3D / V9X_R3D_* numbers, written out so the test does not share the
 * implementation's constants. */
#define T_CMP_NEVER 1ul
#define T_CMP_ALWAYS 8ul
#define T_BLEND_ZERO 1ul
#define T_BLEND_ONE 2ul
#define T_BLEND_SRCCOLOR 3ul
#define T_BLEND_INVSRCCOLOR 4ul
#define T_BLEND_SRCALPHA 5ul
#define T_BLEND_INVSRCALPHA 6ul
#define T_BLEND_DESTALPHA 7ul
#define T_BLEND_INVDESTALPHA 8ul
#define T_BLEND_DESTCOLOR 9ul
#define T_BLEND_INVDESTCOLOR 10ul
#define T_BLEND_SRCALPHASAT 11ul
#define T_FILTER_NEAREST 1ul
#define T_FILTER_LINEAR 2ul
#define T_FILTER_MIPNEAREST 3ul
#define T_FILTER_MIPLINEAR 4ul
#define T_FILTER_LINEARMIPNEAREST 5ul
#define T_FILTER_LINEARMIPLINEAR 6ul
#define T_ADDRESS_WRAP 1ul
#define T_ADDRESS_MIRROR 2ul
#define T_ADDRESS_CLAMP 3ul
#define T_ADDRESS_BORDER 4ul
#define T_TEXOP_DECAL 1ul
#define T_TEXOP_MODULATE 2ul
#define T_TEXOP_DECALALPHA 3ul
#define T_TEXOP_MODULATEALPHA 4ul
#define T_TEXOP_DECALMASK 5ul
#define T_TEXOP_MODULATEMASK 6ul
#define T_TEXOP_COPY 7ul
#define T_TEXOP_ADD 8ul

/* An untextured flat RGB565 draw with a full-target scissor. */
static void base(struct v9x_m64_draw_request *request)
{
    memset(request, 0, sizeof(*request));
    request->target_format = 1ul;
    request->target_width = 640ul;
    request->target_height = 480ul;
    request->scissor_right = 640ul;
    request->scissor_bottom = 480ul;
    request->write_mask = 7ul;
    request->shade_mode = 1ul;
}

/* The measured item 4 texture: 8x8, one level, nearest, clamp, replace. */
static void textured(struct v9x_m64_draw_request *request, v9x_u32 format)
{
    base(request);
    request->textured = 1ul;
    request->texture_format = format;
    request->texture_width = 8ul;
    request->texture_height = 8ul;
    request->texture_levels = 1ul;
    request->texture_min_filter = T_FILTER_NEAREST;
    request->texture_mag_filter = T_FILTER_NEAREST;
    request->texture_address = T_ADDRESS_CLAMP;
    request->texture_op = T_TEXOP_DECAL;
}

static v9x_u32 check(const struct v9x_m64_draw_request *request)
{
    struct v9x_m64_draw_decision decision;
    return v9x_m64_check_draw(request, &decision);
}

static void test_arguments_and_target(void)
{
    struct v9x_m64_draw_request request;
    struct v9x_m64_draw_decision decision;

    base(&request);
    decision.light_fcn = 0xfffffffful;
    decision.texture_alpha = 0xfffffffful;
    CHECK(v9x_m64_check_draw(&request, &decision) == V9X_M64_REFUSE_NONE);
    CHECK(decision.light_fcn == 0ul && decision.texture_alpha == 0ul);
    CHECK(v9x_m64_check_draw(0, &decision) == V9X_M64_REFUSE_ARGUMENT);
    CHECK(v9x_m64_check_draw(&request, 0) == V9X_M64_REFUSE_ARGUMENT);

    request.target_format = 2ul;
    CHECK(check(&request) == V9X_M64_REFUSE_TARGET_FORMAT);
    request.target_format = 0ul;
    CHECK(check(&request) == V9X_M64_REFUSE_TARGET_FORMAT);
    base(&request);
    request.target_width = 0ul;
    CHECK(check(&request) == V9X_M64_REFUSE_TARGET_FORMAT);

    /* The first failing rule is the one reported. */
    base(&request);
    request.target_format = 2ul;
    request.blend_enable = 1ul;
    request.src_blend = T_BLEND_DESTALPHA;
    CHECK(check(&request) == V9X_M64_REFUSE_TARGET_FORMAT);
}

static void test_scissor_mask_shade(void)
{
    struct v9x_m64_draw_request request;

    base(&request);
    request.scissor_left = 20ul;
    request.scissor_right = 21ul;
    request.scissor_top = 12ul;
    request.scissor_bottom = 13ul;
    CHECK(check(&request) == V9X_M64_REFUSE_NONE);
    request.scissor_right = 20ul;
    CHECK(check(&request) == V9X_M64_REFUSE_SCISSOR);
    base(&request);
    request.scissor_bottom = request.scissor_top;
    CHECK(check(&request) == V9X_M64_REFUSE_SCISSOR);
    base(&request);
    request.scissor_right = 641ul;
    CHECK(check(&request) == V9X_M64_REFUSE_SCISSOR);
    base(&request);
    request.scissor_bottom = 481ul;
    CHECK(check(&request) == V9X_M64_REFUSE_SCISSOR);
    base(&request);
    request.scissor_left = 100ul;
    request.scissor_right = 50ul;
    CHECK(check(&request) == V9X_M64_REFUSE_SCISSOR);

    base(&request);
    request.write_mask = 6ul;
    CHECK(check(&request) == V9X_M64_REFUSE_WRITE_MASK);
    request.write_mask = 0ul;
    CHECK(check(&request) == V9X_M64_REFUSE_WRITE_MASK);

    base(&request);
    request.shade_mode = 2ul;
    CHECK(check(&request) == V9X_M64_REFUSE_NONE);
    request.shade_mode = 0ul;
    CHECK(check(&request) == V9X_M64_REFUSE_SHADE);
    request.shade_mode = 3ul;
    CHECK(check(&request) == V9X_M64_REFUSE_SHADE);
}

static void test_depth(void)
{
    struct v9x_m64_draw_request request;
    v9x_u32 func;

    for (func = T_CMP_NEVER; func <= T_CMP_ALWAYS; ++func) {
        base(&request);
        request.depth_enable = 1ul;
        request.depth_bits = 16ul;
        request.depth_func = func;
        request.depth_write = func & 1ul;
        CHECK(check(&request) == V9X_M64_REFUSE_NONE);
    }
    request.depth_func = 0ul;
    CHECK(check(&request) == V9X_M64_REFUSE_DEPTH);
    request.depth_func = 9ul;
    CHECK(check(&request) == V9X_M64_REFUSE_DEPTH);
    request.depth_func = 2ul;
    request.depth_bits = 24ul;
    CHECK(check(&request) == V9X_M64_REFUSE_DEPTH);

    /* Depth state is irrelevant while the test is off. */
    request.depth_enable = 0ul;
    CHECK(check(&request) == V9X_M64_REFUSE_NONE);
}

static void test_blend_pairs(void)
{
    static const v9x_u32 sources[6] = {
        T_BLEND_ZERO, T_BLEND_ONE, T_BLEND_SRCALPHA, T_BLEND_INVSRCALPHA,
        T_BLEND_DESTCOLOR, T_BLEND_INVDESTCOLOR
    };
    static const v9x_u32 destinations[6] = {
        T_BLEND_ZERO, T_BLEND_ONE, T_BLEND_SRCCOLOR, T_BLEND_INVSRCCOLOR,
        T_BLEND_SRCALPHA, T_BLEND_INVSRCALPHA
    };
    static const v9x_u32 bad_sources[6] = {
        T_BLEND_SRCCOLOR, T_BLEND_INVSRCCOLOR, T_BLEND_DESTALPHA,
        T_BLEND_INVDESTALPHA, T_BLEND_SRCALPHASAT, 0ul
    };
    static const v9x_u32 bad_destinations[6] = {
        T_BLEND_DESTALPHA, T_BLEND_INVDESTALPHA, T_BLEND_DESTCOLOR,
        T_BLEND_INVDESTCOLOR, T_BLEND_SRCALPHASAT, 12ul
    };
    struct v9x_m64_draw_request request;
    unsigned int s;
    unsigned int d;
    unsigned int accepted = 0u;

    for (s = 0u; s < 6u; ++s) {
        for (d = 0u; d < 6u; ++d) {
            base(&request);
            request.blend_enable = 1ul;
            request.src_blend = sources[s];
            request.dst_blend = destinations[d];
            if (check(&request) == V9X_M64_REFUSE_NONE) {
                ++accepted;
            }
        }
    }
    CHECK(accepted == 36u);

    for (s = 0u; s < 6u; ++s) {
        base(&request);
        request.blend_enable = 1ul;
        request.src_blend = bad_sources[s];
        request.dst_blend = T_BLEND_ZERO;
        CHECK(check(&request) == V9X_M64_REFUSE_BLEND_FACTOR);
        request.src_blend = T_BLEND_ONE;
        request.dst_blend = bad_destinations[s];
        CHECK(check(&request) == V9X_M64_REFUSE_BLEND_FACTOR);
    }

    /* Factors are ignored while blending is off. */
    base(&request);
    request.src_blend = T_BLEND_DESTALPHA;
    request.dst_blend = T_BLEND_SRCALPHASAT;
    CHECK(check(&request) == V9X_M64_REFUSE_NONE);
}

static void test_texture_shape_and_sampling(void)
{
    struct v9x_m64_draw_request request;
    v9x_u32 format;
    v9x_u32 edge;

    for (format = 0ul; format < 3ul; ++format) {
        textured(&request, format);
        CHECK(check(&request) == V9X_M64_REFUSE_NONE);
    }
    textured(&request, 3ul);
    CHECK(check(&request) == V9X_M64_REFUSE_TEXTURE_FORMAT);

    /* Square powers of two from 8 to 256, the sizes the HAL probe samples. */
    textured(&request, 0ul);
    for (edge = 8ul; edge <= 256ul; edge <<= 1) {
        request.texture_width = edge;
        request.texture_height = edge;
        CHECK(check(&request) == V9X_M64_REFUSE_NONE);
    }
    request.texture_width = 512ul;
    request.texture_height = 512ul;
    CHECK(check(&request) == V9X_M64_REFUSE_TEXTURE_SHAPE);
    request.texture_width = 4ul;
    request.texture_height = 4ul;
    CHECK(check(&request) == V9X_M64_REFUSE_TEXTURE_SHAPE);
    request.texture_width = 24ul;
    request.texture_height = 24ul;
    CHECK(check(&request) == V9X_M64_REFUSE_TEXTURE_SHAPE);
    request.texture_width = 16ul;
    request.texture_height = 8ul;
    CHECK(check(&request) == V9X_M64_REFUSE_TEXTURE_SHAPE);

    /* A chain: up to one level per halving, with a filter that selects a
     * level. Blending between levels is unmeasured and refuses. */
    textured(&request, 0ul);
    request.texture_levels = 4ul;
    request.texture_min_filter = T_FILTER_MIPNEAREST;
    CHECK(check(&request) == V9X_M64_REFUSE_NONE);
    request.texture_min_filter = T_FILTER_LINEARMIPNEAREST;
    CHECK(check(&request) == V9X_M64_REFUSE_NONE);
    request.texture_min_filter = T_FILTER_NEAREST;
    CHECK(check(&request) == V9X_M64_REFUSE_NONE);
    /* Trilinear on a chain; MIPLINEAR, nearest within the levels, has no
     * engine function and refuses. */
    request.texture_min_filter = T_FILTER_MIPLINEAR;
    CHECK(check(&request) == V9X_M64_REFUSE_TEXTURE_FILTER);
    request.texture_min_filter = T_FILTER_LINEARMIPLINEAR;
    CHECK(check(&request) == V9X_M64_REFUSE_NONE);
    request.texture_min_filter = T_FILTER_MIPNEAREST;
    request.texture_levels = 5ul;
    CHECK(check(&request) == V9X_M64_REFUSE_TEXTURE_MIP);
    request.texture_levels = 0ul;
    CHECK(check(&request) == V9X_M64_REFUSE_TEXTURE_MIP);

    textured(&request, 0ul);
    request.texture_min_filter = T_FILTER_LINEAR;
    request.texture_mag_filter = T_FILTER_LINEAR;
    CHECK(check(&request) == V9X_M64_REFUSE_NONE);
    request.texture_min_filter = T_FILTER_MIPNEAREST;
    CHECK(check(&request) == V9X_M64_REFUSE_TEXTURE_FILTER);
    request.texture_min_filter = T_FILTER_LINEAR;
    request.texture_mag_filter = T_FILTER_LINEARMIPLINEAR;
    CHECK(check(&request) == V9X_M64_REFUSE_TEXTURE_FILTER);

    textured(&request, 0ul);
    request.texture_address = T_ADDRESS_WRAP;
    CHECK(check(&request) == V9X_M64_REFUSE_NONE);
    request.texture_address = T_ADDRESS_MIRROR;
    CHECK(check(&request) == V9X_M64_REFUSE_TEXTURE_ADDRESS);
    request.texture_address = T_ADDRESS_BORDER;
    CHECK(check(&request) == V9X_M64_REFUSE_TEXTURE_ADDRESS);
    request.texture_address = T_ADDRESS_WRAP;
    request.texture_wrap_u = 1ul;
    CHECK(check(&request) == V9X_M64_REFUSE_TEXTURE_ADDRESS);
    request.texture_wrap_u = 0ul;
    request.texture_wrap_v = 1ul;
    CHECK(check(&request) == V9X_M64_REFUSE_TEXTURE_ADDRESS);

    /* Texture fields are not read for an untextured draw. */
    base(&request);
    request.texture_format = 9ul;
    request.texture_op = T_TEXOP_ADD;
    CHECK(check(&request) == V9X_M64_REFUSE_NONE);
}

struct op_case {
    v9x_u32 op;
    v9x_u32 format;
    v9x_u32 reason;
    v9x_u32 light;
    v9x_u32 alpha;
};

static void test_texture_ops(void)
{
    static const struct op_case cases[] = {
        { T_TEXOP_DECAL, 0ul, V9X_M64_REFUSE_NONE,
          V9X_M64_TEX_LIGHT_FCN_REPLACE, 0ul },
        { T_TEXOP_COPY, 1ul, V9X_M64_REFUSE_NONE,
          V9X_M64_TEX_LIGHT_FCN_REPLACE, V9X_M64_TEX_MAP_AEN },
        { T_TEXOP_MODULATE, 0ul, V9X_M64_REFUSE_NONE,
          V9X_M64_TEX_LIGHT_FCN_MODULATE, 0ul },
        { T_TEXOP_MODULATE, 2ul, V9X_M64_REFUSE_NONE,
          V9X_M64_TEX_LIGHT_FCN_MODULATE, V9X_M64_TEX_MAP_AEN },
        /* RGB565 ALPHA_DECAL weights by vertex alpha: emit REPLACE. */
        { T_TEXOP_DECALALPHA, 0ul, V9X_M64_REFUSE_NONE,
          V9X_M64_TEX_LIGHT_FCN_REPLACE, 0ul },
        { T_TEXOP_DECALALPHA, 1ul, V9X_M64_REFUSE_NONE,
          V9X_M64_TEX_LIGHT_FCN_ALPHA_DECAL, V9X_M64_TEX_MAP_AEN },
        { T_TEXOP_DECALALPHA, 2ul, V9X_M64_REFUSE_NONE,
          V9X_M64_TEX_LIGHT_FCN_ALPHA_DECAL, V9X_M64_TEX_MAP_AEN },
        /* No mode produces A = At*Af. */
        { T_TEXOP_MODULATEALPHA, 2ul, V9X_M64_REFUSE_TEXTURE_OP, 0ul, 0ul },
        { T_TEXOP_DECALMASK, 1ul, V9X_M64_REFUSE_TEXTURE_OP, 0ul, 0ul },
        { T_TEXOP_MODULATEMASK, 1ul, V9X_M64_REFUSE_TEXTURE_OP, 0ul, 0ul },
        { T_TEXOP_ADD, 0ul, V9X_M64_REFUSE_TEXTURE_OP, 0ul, 0ul },
        { 0ul, 0ul, V9X_M64_REFUSE_TEXTURE_OP, 0ul, 0ul }
    };
    struct v9x_m64_draw_request request;
    struct v9x_m64_draw_decision decision;
    unsigned int index;

    for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        textured(&request, cases[index].format);
        request.texture_op = cases[index].op;
        CHECK(v9x_m64_check_draw(&request, &decision) ==
              cases[index].reason);
        CHECK(decision.light_fcn == cases[index].light);
        CHECK(decision.texture_alpha == cases[index].alpha);
    }

    /* A later refusal clears the texture decision already made. */
    textured(&request, 1ul);
    request.texture_op = T_TEXOP_DECALALPHA;
    request.blend_enable = 1ul;
    request.src_blend = T_BLEND_SRCALPHASAT;
    request.dst_blend = T_BLEND_ZERO;
    CHECK(v9x_m64_check_draw(&request, &decision) ==
          V9X_M64_REFUSE_BLEND_FACTOR);
    CHECK(decision.light_fcn == 0ul && decision.texture_alpha == 0ul);
}

static void test_alpha_test(void)
{
    struct v9x_m64_draw_request request;
    v9x_u32 func;

    for (func = T_CMP_NEVER; func <= T_CMP_ALWAYS; ++func) {
        textured(&request, 1ul);
        request.alpha_test_enable = 1ul;
        request.alpha_func = func;
        request.alpha_ref = 127ul;
        CHECK(check(&request) == V9X_M64_REFUSE_NONE);
    }
    textured(&request, 2ul);
    request.alpha_test_enable = 1ul;
    request.alpha_func = T_CMP_ALWAYS;
    request.alpha_ref = 255ul;
    CHECK(check(&request) == V9X_M64_REFUSE_NONE);
    request.alpha_ref = 256ul;
    CHECK(check(&request) == V9X_M64_REFUSE_ALPHA_TEST);
    request.alpha_ref = 0ul;
    request.alpha_func = 0ul;
    CHECK(check(&request) == V9X_M64_REFUSE_ALPHA_TEST);
    request.alpha_func = 9ul;
    CHECK(check(&request) == V9X_M64_REFUSE_ALPHA_TEST);

    /* Vertex-alpha testing was never measured. */
    base(&request);
    request.alpha_test_enable = 1ul;
    request.alpha_func = T_CMP_ALWAYS;
    CHECK(check(&request) == V9X_M64_REFUSE_ALPHA_TEST);
    textured(&request, 0ul);
    request.alpha_test_enable = 1ul;
    request.alpha_func = T_CMP_ALWAYS;
    CHECK(check(&request) == V9X_M64_REFUSE_ALPHA_TEST);
}

static void test_fog_and_unmeasured_knobs(void)
{
    struct v9x_m64_draw_request request;

    base(&request);
    request.fog_enable = 1ul;
    CHECK(check(&request) == V9X_M64_REFUSE_NONE);
    request.shade_mode = 2ul;
    request.depth_enable = 1ul;
    request.depth_bits = 16ul;
    request.depth_func = 4ul;
    CHECK(check(&request) == V9X_M64_REFUSE_NONE);

    /* Even the ONE/ZERO pair counts: the blend unit is the fog unit. */
    base(&request);
    request.fog_enable = 1ul;
    request.blend_enable = 1ul;
    request.src_blend = T_BLEND_ONE;
    request.dst_blend = T_BLEND_ZERO;
    CHECK(check(&request) == V9X_M64_REFUSE_FOG_WITH_BLEND);

    textured(&request, 0ul);
    request.fog_enable = 1ul;
    CHECK(check(&request) == V9X_M64_REFUSE_FOG_WITH_TEXTURE);

    base(&request);
    request.specular_enable = 1ul;
    CHECK(check(&request) == V9X_M64_REFUSE_SPECULAR);
    base(&request);
    request.color_key_enable = 1ul;
    CHECK(check(&request) == V9X_M64_REFUSE_COLOR_KEY);
    base(&request);
    request.alpha_force = 1ul;
    CHECK(check(&request) == V9X_M64_REFUSE_ALPHA_FORCE);
}

/* The measured combinations: every Phase 4 scene's state is accepted. */
static void test_measured_scenes_accept(void)
{
    struct v9x_m64_draw_request request;
    struct v9x_m64_draw_decision decision;

    /* Item 10: ARGB4444 modulate blended over the target. */
    textured(&request, 2ul);
    request.texture_op = T_TEXOP_MODULATE;
    request.shade_mode = 2ul;
    request.blend_enable = 1ul;
    request.src_blend = T_BLEND_SRCALPHA;
    request.dst_blend = T_BLEND_INVSRCALPHA;
    CHECK(v9x_m64_check_draw(&request, &decision) == V9X_M64_REFUSE_NONE);
    CHECK(decision.light_fcn == V9X_M64_TEX_LIGHT_FCN_MODULATE);
    CHECK(decision.texture_alpha == V9X_M64_TEX_MAP_AEN);

    /* Items 6 and 8: wrap, bilinear, ARGB1555 alpha test. */
    textured(&request, 1ul);
    request.texture_address = T_ADDRESS_WRAP;
    request.texture_min_filter = T_FILTER_LINEAR;
    request.texture_mag_filter = T_FILTER_LINEAR;
    request.alpha_test_enable = 1ul;
    request.alpha_func = 5ul;
    request.alpha_ref = 127ul;
    CHECK(check(&request) == V9X_M64_REFUSE_NONE);

    /* Items 3 and 11: Z16 write with a scissor. */
    base(&request);
    request.depth_enable = 1ul;
    request.depth_bits = 16ul;
    request.depth_func = 2ul;
    request.depth_write = 1ul;
    request.scissor_left = 14ul;
    request.scissor_right = 30ul;
    request.scissor_top = 9ul;
    request.scissor_bottom = 17ul;
    CHECK(check(&request) == V9X_M64_REFUSE_NONE);
}

unsigned int v9x_run_mach64_policy_tests(void)
{
    test_arguments_and_target();
    test_scissor_mask_shade();
    test_depth();
    test_blend_pairs();
    test_texture_shape_and_sampling();
    test_texture_ops();
    test_alpha_test();
    test_fog_and_unmeasured_knobs();
    test_measured_scenes_accept();
    return failures;
}
