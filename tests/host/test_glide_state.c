/*
 * Tests for the Glide state mapping (src\glide\glide_state.c): depth,
 * blending, alpha test, the combine as texture ops and vertex sources,
 * the chroma-key approximation, fog, the clip window as scissor, and
 * texture addressing. The settings are NFS II SE's from the census
 * (docs\decisions\2026-10-08-nfs2se-glide-census.md) where it used them.
 * Render-interface numbers are Direct3D's (r3d_abi.h).
 */
#include <stdio.h>
#include "../../src/glide/glide_state.h"
#include "../../src/glide/glide_vertex.h"

static unsigned int glide_state_failures;

#define SCHECK(condition) do { \
    if (!(condition)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, \
               #condition); \
        ++glide_state_failures; \
    } \
} while (0)

/* Direct3D's D3DCMPFUNC and D3DBLEND, as the interface takes them. */
#define D3D_CMP_LESSEQUAL   4ul
#define D3D_CMP_GREATER     5ul
#define D3D_CMP_ALWAYS      8ul
#define D3D_BLEND_ZERO      1ul
#define D3D_BLEND_ONE       2ul
#define D3D_BLEND_SRCALPHA  5ul
#define D3D_BLEND_INVSRCALPHA 6ul
#define D3D_BLEND_DESTCOLOR 9ul
#define D3D_BLEND_SRCCOLOR  3ul

static void combine(V9X_GLIDE_COMBINE *c, v9x_u32 function, v9x_u32 factor,
                    v9x_u32 local, v9x_u32 other)
{
    c->function = function;
    c->factor = factor;
    c->local = local;
    c->other = other;
    c->invert = 0ul;
}

static void test_defaults(void)
{
    V9X_GLIDE_STATE s;
    V9X_GLIDE_DRAW_SETUP d;

    v9x_glide_state_init(&s, 640ul, 480ul, V9X_GLIDE_ORIGIN_UPPER_LEFT);
    v9x_glide_state_map(&s, &d);
    SCHECK(d.recognized);
    SCHECK(!d.textured);
    SCHECK(d.color_source == V9X_GLIDE_SOURCE_ITERATED);
    SCHECK(d.alpha_source == V9X_GLIDE_SOURCE_ITERATED);
    SCHECK(!d.state.depth_enable && !d.state.blend_enable);
    SCHECK(!d.state.alpha_test_enable && !d.state.fog_enable);
    SCHECK(d.state.write_mask == V9X_R3D_ABI_WRITE_RGB);
    SCHECK(d.state.scissor_left == 0ul && d.state.scissor_top == 0ul &&
           d.state.scissor_right == 640ul && d.state.scissor_bottom == 480ul);
}

static void test_depth_and_blend(void)
{
    V9X_GLIDE_STATE s;
    V9X_GLIDE_DRAW_SETUP d;

    v9x_glide_state_init(&s, 640ul, 480ul, V9X_GLIDE_ORIGIN_UPPER_LEFT);
    s.depth_mode = V9X_GLIDE_DEPTH_WBUFFER;
    s.depth_func = 3ul;     /* LEQUAL, the census's scene */
    s.depth_mask = 1ul;
    v9x_glide_state_map(&s, &d);
    SCHECK(d.state.depth_enable && d.state.depth_write);
    SCHECK(d.state.depth_func == D3D_CMP_LESSEQUAL);
    s.depth_func = 7ul;     /* ALWAYS with writes off, the census's HUD */
    s.depth_mask = 0ul;
    v9x_glide_state_map(&s, &d);
    SCHECK(d.state.depth_enable && !d.state.depth_write);
    SCHECK(d.state.depth_func == D3D_CMP_ALWAYS);

    s.blend_src = V9X_GLIDE_BLEND_SRC_ALPHA;
    s.blend_dst = V9X_GLIDE_BLEND_ONE_MINUS_SRC_ALPHA;
    v9x_glide_state_map(&s, &d);
    SCHECK(d.state.blend_enable);
    SCHECK(d.state.src_blend == D3D_BLEND_SRCALPHA &&
           d.state.dst_blend == D3D_BLEND_INVSRCALPHA);
    s.blend_dst = V9X_GLIDE_BLEND_ONE;
    v9x_glide_state_map(&s, &d);
    SCHECK(d.state.src_blend == D3D_BLEND_SRCALPHA &&
           d.state.dst_blend == D3D_BLEND_ONE);
    /* Glide's colour factor names the other side's colour. */
    s.blend_src = V9X_GLIDE_BLEND_COLOR;
    s.blend_dst = V9X_GLIDE_BLEND_COLOR;
    v9x_glide_state_map(&s, &d);
    SCHECK(d.state.src_blend == D3D_BLEND_DESTCOLOR &&
           d.state.dst_blend == D3D_BLEND_SRCCOLOR);
    s.blend_src = V9X_GLIDE_BLEND_ONE;
    s.blend_dst = V9X_GLIDE_BLEND_ZERO;
    v9x_glide_state_map(&s, &d);
    SCHECK(!d.state.blend_enable);

    s.alpha_func = 4ul;     /* GREATER 0x10, the census */
    s.alpha_ref = 0x10ul;
    v9x_glide_state_map(&s, &d);
    SCHECK(d.state.alpha_test_enable);
    SCHECK(d.state.alpha_func == D3D_CMP_GREATER && d.state.alpha_ref == 0x10ul);
}

static void test_combine(void)
{
    V9X_GLIDE_STATE s;
    V9X_GLIDE_DRAW_SETUP d;

    v9x_glide_state_init(&s, 640ul, 480ul, V9X_GLIDE_ORIGIN_UPPER_LEFT);

    /* The census's untextured setting: LOCAL on iterated. */
    combine(&s.color, V9X_GLIDE_COMBINE_FUNCTION_LOCAL, 0ul,
            V9X_GLIDE_COMBINE_LOCAL_ITERATED, V9X_GLIDE_COMBINE_OTHER_CONSTANT);
    s.alpha = s.color;
    v9x_glide_state_map(&s, &d);
    SCHECK(d.recognized && !d.textured);
    SCHECK(d.color_source == V9X_GLIDE_SOURCE_ITERATED);

    /* The census's textured setting: texture scaled by iterated. */
    combine(&s.color, V9X_GLIDE_COMBINE_FUNCTION_SCALE_OTHER,
            V9X_GLIDE_COMBINE_FACTOR_LOCAL, V9X_GLIDE_COMBINE_LOCAL_ITERATED,
            V9X_GLIDE_COMBINE_OTHER_TEXTURE);
    s.alpha = s.color;
    v9x_glide_state_map(&s, &d);
    SCHECK(d.recognized && d.textured);
    SCHECK(d.color_op == V9X_R3D_ABI_COLOROP_MODULATE);
    SCHECK(d.alpha_op == V9X_R3D_ABI_ALPHAOP_MODULATE);
    SCHECK(d.color_source == V9X_GLIDE_SOURCE_ITERATED);

    /* Texture alone: REPLACE. */
    combine(&s.color, V9X_GLIDE_COMBINE_FUNCTION_SCALE_OTHER,
            V9X_GLIDE_COMBINE_FACTOR_ONE, V9X_GLIDE_COMBINE_LOCAL_ITERATED,
            V9X_GLIDE_COMBINE_OTHER_TEXTURE);
    s.alpha = s.color;
    v9x_glide_state_map(&s, &d);
    SCHECK(d.recognized && d.textured);
    SCHECK(d.color_op == V9X_R3D_ABI_COLOROP_REPLACE);
    SCHECK(d.alpha_op == V9X_R3D_ABI_ALPHAOP_REPLACE);

    /* Texture scaled by the constant colour: MODULATE on a constant vertex. */
    combine(&s.color, V9X_GLIDE_COMBINE_FUNCTION_SCALE_OTHER,
            V9X_GLIDE_COMBINE_FACTOR_LOCAL, V9X_GLIDE_COMBINE_LOCAL_CONSTANT,
            V9X_GLIDE_COMBINE_OTHER_TEXTURE);
    v9x_glide_state_map(&s, &d);
    SCHECK(d.recognized && d.color_op == V9X_R3D_ABI_COLOROP_MODULATE);
    SCHECK(d.color_source == V9X_GLIDE_SOURCE_CONSTANT);

    /* The constant colour alone, alpha iterated. */
    combine(&s.color, V9X_GLIDE_COMBINE_FUNCTION_LOCAL, 0ul,
            V9X_GLIDE_COMBINE_LOCAL_CONSTANT, V9X_GLIDE_COMBINE_OTHER_ITERATED);
    combine(&s.alpha, V9X_GLIDE_COMBINE_FUNCTION_LOCAL, 0ul,
            V9X_GLIDE_COMBINE_LOCAL_ITERATED, V9X_GLIDE_COMBINE_OTHER_ITERATED);
    v9x_glide_state_map(&s, &d);
    SCHECK(d.recognized && !d.textured);
    SCHECK(d.color_source == V9X_GLIDE_SOURCE_CONSTANT);
    SCHECK(d.alpha_source == V9X_GLIDE_SOURCE_ITERATED);

    /* Inverted output has no mapping: closest, and flagged. */
    s.color.invert = 1ul;
    v9x_glide_state_map(&s, &d);
    SCHECK(!d.recognized);

    /* A TMU that does not pass its texel through is not recognised. */
    combine(&s.color, V9X_GLIDE_COMBINE_FUNCTION_SCALE_OTHER,
            V9X_GLIDE_COMBINE_FACTOR_LOCAL, V9X_GLIDE_COMBINE_LOCAL_ITERATED,
            V9X_GLIDE_COMBINE_OTHER_TEXTURE);
    s.alpha = s.color;
    s.tex_rgb_function = V9X_GLIDE_COMBINE_FUNCTION_ZERO;
    v9x_glide_state_map(&s, &d);
    SCHECK(d.textured && !d.recognized);
}

/*
 * Carmageddon II's menu text (netbook, 2026-10-11): colour LOCAL on the
 * vertex, alpha SCALE_OTHER by TEXTURE_ALPHA with other CONSTANT - the glyph
 * mask is the texture's alpha. Read as untextured, every glyph drew as a
 * solid quad. The alpha unit's TEXTURE_ALPHA factor is the texture scaled
 * by `other`; the colour beside it is approximated by MODULATE and flagged.
 */
static void test_texture_alpha_factor(void)
{
    V9X_GLIDE_STATE s;
    V9X_GLIDE_DRAW_SETUP d;

    v9x_glide_state_init(&s, 640ul, 480ul, V9X_GLIDE_ORIGIN_UPPER_LEFT);
    combine(&s.color, V9X_GLIDE_COMBINE_FUNCTION_LOCAL,
            V9X_GLIDE_COMBINE_FACTOR_ZERO, V9X_GLIDE_COMBINE_LOCAL_ITERATED,
            V9X_GLIDE_COMBINE_OTHER_ITERATED);
    combine(&s.alpha, V9X_GLIDE_COMBINE_FUNCTION_SCALE_OTHER,
            V9X_GLIDE_COMBINE_FACTOR_TEXTURE_ALPHA,
            V9X_GLIDE_COMBINE_LOCAL_CONSTANT, V9X_GLIDE_COMBINE_OTHER_CONSTANT);

    /*
     * Nothing reads the alpha - no blend on it, no alpha test - so the draw
     * is not textured. Carmageddon II leaves this alpha combine set for
     * opaque geometry with no texture source at all; taken as textured,
     * 9.7 million such draws were skipped and the screen went black
     * (netbook, 2026-10-11). On a Voodoo that alpha goes nowhere.
     */
    v9x_glide_state_map(&s, &d);
    SCHECK(!d.textured);
    SCHECK(d.color_source == V9X_GLIDE_SOURCE_ITERATED);

    /* The text blends on it: now the texture's alpha is the glyph. */
    s.blend_src = V9X_GLIDE_BLEND_SRC_ALPHA;
    s.blend_dst = V9X_GLIDE_BLEND_ONE_MINUS_SRC_ALPHA;
    v9x_glide_state_map(&s, &d);
    SCHECK(d.textured);
    SCHECK(d.alpha_op == V9X_R3D_ABI_ALPHAOP_MODULATE);
    SCHECK(d.alpha_source == V9X_GLIDE_SOURCE_CONSTANT);
    SCHECK(d.color_source == V9X_GLIDE_SOURCE_ITERATED);
    SCHECK(d.color_op == V9X_R3D_ABI_COLOROP_MODULATE);
    SCHECK(!d.recognized);

    /* Scaled by the vertex alpha instead: the texture by iterated. */
    s.alpha.other = V9X_GLIDE_COMBINE_OTHER_ITERATED;
    v9x_glide_state_map(&s, &d);
    SCHECK(d.textured && d.alpha_op == V9X_R3D_ABI_ALPHAOP_MODULATE);
    SCHECK(d.alpha_source == V9X_GLIDE_SOURCE_ITERATED);

    /* An alpha test reads it as well. */
    s.blend_src = V9X_GLIDE_BLEND_ONE;
    s.blend_dst = V9X_GLIDE_BLEND_ZERO;
    s.alpha_func = 4ul;     /* GREATER */
    v9x_glide_state_map(&s, &d);
    SCHECK(d.textured);
    s.alpha_func = V9X_GLIDE_CMP_ALWAYS;

    /* On the colour unit the factor is a texel's alpha scaling a colour,
     * which no op here says: still unknown, and still untextured unless
     * the unit names the texture. */
    s.color = s.alpha;
    s.alpha.function = V9X_GLIDE_COMBINE_FUNCTION_LOCAL;
    s.alpha.local = V9X_GLIDE_COMBINE_LOCAL_ITERATED;
    v9x_glide_state_map(&s, &d);
    SCHECK(!d.recognized);
}

static void test_chroma_and_fog(void)
{
    V9X_GLIDE_STATE s;
    V9X_GLIDE_DRAW_SETUP d;

    v9x_glide_state_init(&s, 640ul, 480ul, V9X_GLIDE_ORIGIN_UPPER_LEFT);
    combine(&s.color, V9X_GLIDE_COMBINE_FUNCTION_SCALE_OTHER,
            V9X_GLIDE_COMBINE_FACTOR_LOCAL, V9X_GLIDE_COMBINE_LOCAL_ITERATED,
            V9X_GLIDE_COMBINE_OTHER_TEXTURE);
    s.alpha = s.color;
    s.chroma_mode = V9X_GLIDE_CHROMAKEY_ENABLE;
    s.chroma_value = 0x0000FF00ul;
    v9x_glide_state_map(&s, &d);
    /* Keyed texels get alpha 0; an alpha test the game left off discards
     * them. The alpha op was the texture's already: not promoted. */
    SCHECK(d.key_texture && !d.key_alpha);
    SCHECK(d.state.alpha_test_enable && d.state.alpha_func == D3D_CMP_GREATER &&
           d.state.alpha_ref == 0ul);
    /* The game's own test already discards alpha 0: it is kept. */
    s.alpha_func = 4ul;
    s.alpha_ref = 0x10ul;
    v9x_glide_state_map(&s, &d);
    SCHECK(d.key_texture && d.state.alpha_ref == 0x10ul);
    /* Untextured: nothing to key. */
    combine(&s.color, V9X_GLIDE_COMBINE_FUNCTION_LOCAL, 0ul,
            V9X_GLIDE_COMBINE_LOCAL_ITERATED, V9X_GLIDE_COMBINE_OTHER_CONSTANT);
    s.alpha = s.color;
    v9x_glide_state_map(&s, &d);
    SCHECK(!d.key_texture);

    s.fog_mode = V9X_GLIDE_FOG_TABLE;
    s.fog_color = 0xFF000204ul;     /* the census's fog colour */
    v9x_glide_state_map(&s, &d);
    SCHECK(d.state.fog_enable && d.state.fog_color == 0x00000204ul);
    s.fog_mode = V9X_GLIDE_FOG_DISABLE;
    v9x_glide_state_map(&s, &d);
    SCHECK(!d.state.fog_enable);
}

/*
 * The chroma key does not depend on the alpha combine (Glide keys before
 * it), so a keyed draw whose alpha comes from the vertex or a constant
 * must still carry the texture's key alpha to the test. Diablo II's menus
 * key with alpha from a ZERO combine or the constant colour, blending
 * ONE/ZERO, ONE/ONE or ZERO/SRC_COLOR (census): the fragment alpha alone
 * left every keyed texel drawn, and the Rage XL refused the draw outright
 * (A8U4I5, 2026-10-10).
 */
static void test_chroma_alpha_source(void)
{
    V9X_GLIDE_STATE s;
    V9X_GLIDE_DRAW_SETUP d;

    v9x_glide_state_init(&s, 800ul, 600ul, V9X_GLIDE_ORIGIN_UPPER_LEFT);
    combine(&s.color, V9X_GLIDE_COMBINE_FUNCTION_SCALE_OTHER,
            V9X_GLIDE_COMBINE_FACTOR_LOCAL, V9X_GLIDE_COMBINE_LOCAL_ITERATED,
            V9X_GLIDE_COMBINE_OTHER_TEXTURE);
    combine(&s.alpha, V9X_GLIDE_COMBINE_FUNCTION_ZERO, 0ul,
            V9X_GLIDE_COMBINE_LOCAL_CONSTANT, V9X_GLIDE_COMBINE_OTHER_CONSTANT);
    s.chroma_mode = V9X_GLIDE_CHROMAKEY_ENABLE;

    /* Nothing reads the source alpha: the key alone decides it. */
    v9x_glide_state_map(&s, &d);
    SCHECK(d.textured && d.key_texture && d.key_alpha);
    SCHECK(d.color_op == V9X_R3D_ABI_COLOROP_MODULATE);
    SCHECK(d.alpha_op == V9X_R3D_ABI_ALPHAOP_REPLACE);
    SCHECK(d.state.alpha_test_enable && d.state.alpha_func == D3D_CMP_GREATER);

    combine(&s.alpha, V9X_GLIDE_COMBINE_FUNCTION_LOCAL, 0ul,
            V9X_GLIDE_COMBINE_LOCAL_CONSTANT, V9X_GLIDE_COMBINE_OTHER_CONSTANT);
    s.blend_src = V9X_GLIDE_BLEND_ONE;
    s.blend_dst = V9X_GLIDE_BLEND_ONE;
    v9x_glide_state_map(&s, &d);
    SCHECK(d.alpha_op == V9X_R3D_ABI_ALPHAOP_REPLACE);
    s.blend_src = V9X_GLIDE_BLEND_ZERO;
    s.blend_dst = V9X_GLIDE_BLEND_COLOR;
    v9x_glide_state_map(&s, &d);
    SCHECK(d.alpha_op == V9X_R3D_ABI_ALPHAOP_REPLACE);

    /* A blend that reads the source alpha keeps the fragment's, scaled by
     * the key: an unkeyed texel's alpha is one. */
    s.blend_src = V9X_GLIDE_BLEND_SRC_ALPHA;
    s.blend_dst = V9X_GLIDE_BLEND_ONE_MINUS_SRC_ALPHA;
    v9x_glide_state_map(&s, &d);
    SCHECK(d.alpha_op == V9X_R3D_ABI_ALPHAOP_MODULATE);

    /* So does the game's own alpha test. */
    s.blend_src = V9X_GLIDE_BLEND_ONE;
    s.blend_dst = V9X_GLIDE_BLEND_ZERO;
    s.alpha_func = 4ul;
    s.alpha_ref = 0x10ul;
    v9x_glide_state_map(&s, &d);
    SCHECK(d.alpha_op == V9X_R3D_ABI_ALPHAOP_MODULATE);

    /* Without the key the fragment alpha stands, as before. */
    s.alpha_func = V9X_GLIDE_CMP_ALWAYS;
    s.chroma_mode = V9X_GLIDE_CHROMAKEY_DISABLE;
    v9x_glide_state_map(&s, &d);
    SCHECK(!d.key_texture && !d.key_alpha &&
           d.alpha_op == V9X_R3D_ABI_ALPHAOP_FRAGMENT);
}

static void test_clip_and_texture(void)
{
    V9X_GLIDE_STATE s;
    V9X_GLIDE_DRAW_SETUP d;

    v9x_glide_state_init(&s, 640ul, 480ul, V9X_GLIDE_ORIGIN_UPPER_LEFT);
    /* A census HUD pane. */
    s.clip_min_x = 532ul; s.clip_min_y = 318ul;
    s.clip_max_x = 640ul; s.clip_max_y = 453ul;
    v9x_glide_state_map(&s, &d);
    SCHECK(d.state.scissor_left == 532ul && d.state.scissor_top == 318ul &&
           d.state.scissor_right == 640ul && d.state.scissor_bottom == 453ul);
    /* Beyond the screen: clamped. */
    s.clip_max_x = 700ul; s.clip_max_y = 600ul;
    v9x_glide_state_map(&s, &d);
    SCHECK(d.state.scissor_right == 640ul && d.state.scissor_bottom == 480ul);

    v9x_glide_state_init(&s, 640ul, 480ul, V9X_GLIDE_ORIGIN_LOWER_LEFT);
    s.clip_min_x = 10ul; s.clip_min_y = 20ul;
    s.clip_max_x = 100ul; s.clip_max_y = 200ul;
    v9x_glide_state_map(&s, &d);
    SCHECK(d.state.scissor_left == 10ul && d.state.scissor_right == 100ul);
    SCHECK(d.state.scissor_top == 280ul && d.state.scissor_bottom == 460ul);

    s.clamp_s = V9X_GLIDE_TEXTURE_CLAMP;
    s.clamp_t = V9X_GLIDE_TEXTURE_CLAMP;
    s.min_filter = V9X_GLIDE_TEXTURE_BILINEAR;
    s.mag_filter = V9X_GLIDE_TEXTURE_POINT;
    v9x_glide_state_map(&s, &d);
    SCHECK(d.address == V9X_R3D_ABI_ADDRESS_CLAMP);
    SCHECK(d.min_filter == V9X_R3D_ABI_FILTER_LINEAR);
    SCHECK(d.mag_filter == V9X_R3D_ABI_FILTER_NEAREST);
    s.clamp_s = V9X_GLIDE_TEXTURE_WRAP;
    s.clamp_t = V9X_GLIDE_TEXTURE_WRAP;
    v9x_glide_state_map(&s, &d);
    SCHECK(d.address == V9X_R3D_ABI_ADDRESS_WRAP);
}

static void test_resolution_and_color(void)
{
    v9x_u32 width = 0ul;
    v9x_u32 height = 0ul;

    /* NFS II SE opened resolution 7 (census). */
    SCHECK(v9x_glide_resolution_size(7ul, &width, &height));
    SCHECK(width == 640ul && height == 480ul);
    SCHECK(v9x_glide_resolution_size(8ul, &width, &height));
    SCHECK(width == 800ul && height == 600ul);
    SCHECK(v9x_glide_resolution_size(12ul, &width, &height));
    SCHECK(width == 1024ul && height == 768ul);
    SCHECK(v9x_glide_resolution_size(1ul, &width, &height));
    SCHECK(width == 320ul && height == 240ul);
    SCHECK(v9x_glide_resolution_size(15ul, &width, &height));
    SCHECK(width == 400ul && height == 300ul);
    SCHECK(!v9x_glide_resolution_size(0xFFul, &width, &height));

    SCHECK(v9x_glide_color_to_argb(0x80112233ul, 0ul) == 0x80112233ul);
    SCHECK(v9x_glide_color_to_argb(0x80332211ul, 1ul) == 0x80112233ul);
    SCHECK(v9x_glide_color_to_argb(0x11223380ul, 2ul) == 0x80112233ul);
    SCHECK(v9x_glide_color_to_argb(0x33221180ul, 3ul) == 0x80112233ul);

    /* The way back, for grConstantColorValue4's floats: every format. */
    SCHECK(v9x_glide_argb_to_color(0x80112233ul, 0ul) == 0x80112233ul);
    SCHECK(v9x_glide_argb_to_color(0x80112233ul, 1ul) == 0x80332211ul);
    SCHECK(v9x_glide_argb_to_color(0x80112233ul, 2ul) == 0x11223380ul);
    SCHECK(v9x_glide_argb_to_color(0x80112233ul, 3ul) == 0x33221180ul);
}

/*
 * guColorCombineFunction's presets as grColorCombine arguments. Carmageddon
 * II sets its colour combine only this way; as a stub it left the default
 * (the vertex colour), and the menu's textured tiles drew in the vertex
 * colours the game never wrote - black (netbook, 2026-10-11).
 */
static void test_gu_color_combine(void)
{
    V9X_GLIDE_STATE s;
    V9X_GLIDE_DRAW_SETUP d;

    v9x_glide_state_init(&s, 640ul, 480ul, V9X_GLIDE_ORIGIN_UPPER_LEFT);

    /* GR_COLORCOMBINE_DECAL_TEXTURE (4): the texture alone. */
    SCHECK(v9x_glide_gu_color_combine(4ul, &s.color));
    SCHECK(s.color.function == V9X_GLIDE_COMBINE_FUNCTION_SCALE_OTHER &&
           s.color.factor == V9X_GLIDE_COMBINE_FACTOR_ONE &&
           s.color.other == V9X_GLIDE_COMBINE_OTHER_TEXTURE &&
           s.color.invert == 0ul);
    v9x_glide_state_map(&s, &d);
    SCHECK(d.textured && d.color_op == V9X_R3D_ABI_COLOROP_REPLACE);

    /* GR_COLORCOMBINE_TEXTURE_TIMES_ITRGB (6): texture by the vertex. */
    SCHECK(v9x_glide_gu_color_combine(6ul, &s.color));
    v9x_glide_state_map(&s, &d);
    SCHECK(d.textured && d.color_op == V9X_R3D_ABI_COLOROP_MODULATE);
    SCHECK(d.color_source == V9X_GLIDE_SOURCE_ITERATED);

    /* GR_COLORCOMBINE_TEXTURE_TIMES_CCRGB (5): texture by the constant. */
    SCHECK(v9x_glide_gu_color_combine(5ul, &s.color));
    v9x_glide_state_map(&s, &d);
    SCHECK(d.textured && d.color_source == V9X_GLIDE_SOURCE_CONSTANT);

    /* GR_COLORCOMBINE_CCRGB (1) and ITRGB (2): untextured. */
    SCHECK(v9x_glide_gu_color_combine(1ul, &s.color));
    s.alpha = s.color;
    v9x_glide_state_map(&s, &d);
    SCHECK(!d.textured && d.color_source == V9X_GLIDE_SOURCE_CONSTANT);
    SCHECK(v9x_glide_gu_color_combine(2ul, &s.color));
    v9x_glide_state_map(&s, &d);
    SCHECK(!d.textured && d.color_source == V9X_GLIDE_SOURCE_ITERATED);

    /* GR_COLORCOMBINE_ONE (16): zero, inverted. */
    SCHECK(v9x_glide_gu_color_combine(16ul, &s.color));
    SCHECK(s.color.function == V9X_GLIDE_COMBINE_FUNCTION_ZERO &&
           s.color.invert == 1ul);

    /*
     * DECAL_TEXTURE with a constant alpha, no blend and no alpha test: the
     * alpha goes nowhere, so the texture's own is as good as the vertex's
     * and makes the op one every engine has (REPLACE/REPLACE, DECAL).
     * Carmageddon II's menu drew this on RGB565 textures and Gen3 refused
     * REPLACE beside the fragment's alpha (netbook, 2026-10-11).
     */
    SCHECK(v9x_glide_gu_color_combine(4ul, &s.color));
    combine(&s.alpha, V9X_GLIDE_COMBINE_FUNCTION_SCALE_OTHER,
            V9X_GLIDE_COMBINE_FACTOR_ONE, V9X_GLIDE_COMBINE_LOCAL_CONSTANT,
            V9X_GLIDE_COMBINE_OTHER_CONSTANT);
    v9x_glide_state_map(&s, &d);
    SCHECK(d.textured && d.color_op == V9X_R3D_ABI_COLOROP_REPLACE);
    SCHECK(d.alpha_op == V9X_R3D_ABI_ALPHAOP_REPLACE);
    /* With a blend on the alpha, the constant is the alpha. */
    s.blend_src = V9X_GLIDE_BLEND_SRC_ALPHA;
    s.blend_dst = V9X_GLIDE_BLEND_ONE_MINUS_SRC_ALPHA;
    v9x_glide_state_map(&s, &d);
    SCHECK(d.alpha_op == V9X_R3D_ABI_ALPHAOP_FRAGMENT);
    s.blend_src = V9X_GLIDE_BLEND_ONE;
    s.blend_dst = V9X_GLIDE_BLEND_ZERO;

    /*
     * DECAL_TEXTURE beside texture-alpha times the constant, blended on it:
     * Carmageddon II's fading menu text (netbook, 2026-10-11). REPLACE
     * beside MODULATE is no single-unit texture op, and Gen3 refused it;
     * MODULATE on a white vertex is the same colour exactly.
     */
    SCHECK(v9x_glide_gu_color_combine(4ul, &s.color));
    combine(&s.alpha, V9X_GLIDE_COMBINE_FUNCTION_SCALE_OTHER,
            V9X_GLIDE_COMBINE_FACTOR_TEXTURE_ALPHA,
            V9X_GLIDE_COMBINE_LOCAL_CONSTANT, V9X_GLIDE_COMBINE_OTHER_CONSTANT);
    s.blend_src = V9X_GLIDE_BLEND_SRC_ALPHA;
    s.blend_dst = V9X_GLIDE_BLEND_ONE_MINUS_SRC_ALPHA;
    v9x_glide_state_map(&s, &d);
    SCHECK(d.textured && d.color_op == V9X_R3D_ABI_COLOROP_MODULATE);
    SCHECK(d.alpha_op == V9X_R3D_ABI_ALPHAOP_MODULATE);
    SCHECK(d.color_source == V9X_GLIDE_SOURCE_WHITE);
    SCHECK(d.alpha_source == V9X_GLIDE_SOURCE_CONSTANT);
    s.blend_src = V9X_GLIDE_BLEND_ONE;
    s.blend_dst = V9X_GLIDE_BLEND_ZERO;

    /* An unknown preset leaves the combine as it was. */
    SCHECK(v9x_glide_gu_color_combine(4ul, &s.color));
    SCHECK(!v9x_glide_gu_color_combine(17ul, &s.color));
    SCHECK(s.color.other == V9X_GLIDE_COMBINE_OTHER_TEXTURE);
}

unsigned int v9x_run_glide_state_tests(void)
{
    glide_state_failures = 0u;
    test_resolution_and_color();
    test_defaults();
    test_depth_and_blend();
    test_combine();
    test_texture_alpha_factor();
    test_gu_color_combine();
    test_chroma_and_fog();
    test_chroma_alpha_source();
    test_clip_and_texture();
    return glide_state_failures;
}
