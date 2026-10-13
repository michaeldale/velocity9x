#include <stdio.h>
#include <string.h>

#include "../../src/display32/d3d/d3d_tdfx_map.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

/* The desktop's offset on the Voodoo4 and the memory above it. */
#define BASE 0x00f00000ul
#define VRAM 0x010ff000ul

static int surface_object;
static int texture_object;

/* A 640x480 RGB565 back buffer 1 MiB into the mapping, Gouraud, nothing
 * else. */
static void base_draw(V9X_R3D_DRAW *draw)
{
    memset(draw, 0, sizeof(*draw));
    draw->target.offset = 0x00100000ul;
    draw->target.pitch = 1280ul;
    draw->target.width = 640ul;
    draw->target.height = 480ul;
    draw->target.format = V9X_R3D_FORMAT_RGB565;
    draw->target.object = &surface_object;
    draw->shade_mode = V9X_R3D_SHADE_GOURAUD;
    draw->texture.min_filter = V9X_R3D_FILTER_NEAREST;
    draw->texture.mag_filter = V9X_R3D_FILTER_NEAREST;
    draw->texture.address = V9X_R3D_ADDRESS_WRAP;
    draw->texture.op = V9X_R3D_TEXOP_MODULATE;
}

/* A bound 64x64 RGB565 texture at 2 MiB into the mapping. */
static void base_texture(V9X_R3D_DRAW *draw, V9X_D3D_TDFX_TEXTURE *resolved)
{
    draw->texture.object = &texture_object;
    memset(resolved, 0, sizeof(*resolved));
    resolved->format = V9X_TDFX_TEX_RGB565;
    resolved->has_alpha = 0;
    resolved->width = 64ul;
    resolved->height = 64ul;
    resolved->offset = 0x00200000ul;
    resolved->pitch_bytes = 128ul;
}

static v9x_u32 map(const V9X_R3D_DRAW *draw,
                   const V9X_D3D_TDFX_TEXTURE *resolved,
                   struct v9x_tdfx_3d_state *state)
{
    return v9x_d3d_tdfx_map_draw(draw, resolved, BASE, VRAM, 0ul, state);
}

static void test_untextured(void)
{
    V9X_R3D_DRAW draw;
    struct v9x_tdfx_3d_state state;

    base_draw(&draw);
    CHECK(map(&draw, 0, &state) == V9X_D3D_TDFX_REFUSE_NONE);
    /* Offsets move by the desktop's; the memory bound is from byte 0. */
    CHECK(state.target.offset == BASE + 0x00100000ul);
    CHECK(state.target.pitch_bytes == 1280ul);
    CHECK(state.width == 640ul && state.height == 480ul);
    CHECK(state.vram_bytes == BASE + VRAM);
    CHECK(state.depth_enable == 0ul);
    CHECK(state.texture_enable == 0ul);
    CHECK(state.blend_enable == 0ul && state.alpha_test == 0ul);
    CHECK(state.dither == 0ul);

    draw.target.format = V9X_R3D_FORMAT_XRGB1555;
    CHECK(map(&draw, 0, &state) == V9X_D3D_TDFX_REFUSE_TARGET);
    base_draw(&draw);
    draw.shade_mode = 3ul;
    CHECK(map(&draw, 0, &state) == V9X_D3D_TDFX_REFUSE_SHADE);
    base_draw(&draw);
    draw.shade_mode = V9X_R3D_SHADE_FLAT;
    CHECK(map(&draw, 0, &state) == V9X_D3D_TDFX_REFUSE_NONE);
}

static void test_depth(void)
{
    V9X_R3D_DRAW draw;
    struct v9x_tdfx_3d_state state;

    base_draw(&draw);
    draw.depth_enable = 1ul;
    draw.depth_write = 1ul;
    draw.depth_func = V9X_R3D_CMP_LESS;
    draw.depth.object = &surface_object;
    draw.depth.offset = 0x00300000ul;
    draw.depth.pitch = 1280ul;
    CHECK(map(&draw, 0, &state) == V9X_D3D_TDFX_REFUSE_NONE);
    CHECK(state.depth_enable == 1ul);
    CHECK(state.depth.offset == BASE + 0x00300000ul);
    CHECK(state.depth.pitch_bytes == 1280ul);
    /* Direct3D's codes are the chip's plus one. */
    CHECK(state.depth_func == V9X_TDFX_CMP_LESS);
    CHECK(state.depth_write == 1ul);

    draw.depth_func = V9X_R3D_CMP_ALWAYS;
    CHECK(map(&draw, 0, &state) == V9X_D3D_TDFX_REFUSE_NONE);
    CHECK(state.depth_func == V9X_TDFX_CMP_ALWAYS);
    draw.depth_func = 0ul;
    CHECK(map(&draw, 0, &state) == V9X_D3D_TDFX_REFUSE_DEPTH_FUNC);
    draw.depth_func = 9ul;
    CHECK(map(&draw, 0, &state) == V9X_D3D_TDFX_REFUSE_DEPTH_FUNC);

    /* Enabled but unbound, or no pitch: no depth, not a refusal. */
    draw.depth_func = V9X_R3D_CMP_LESS;
    draw.depth.object = 0;
    CHECK(map(&draw, 0, &state) == V9X_D3D_TDFX_REFUSE_NONE);
    CHECK(state.depth_enable == 0ul);
    draw.depth.object = &surface_object;
    draw.depth.pitch = 0ul;
    CHECK(map(&draw, 0, &state) == V9X_D3D_TDFX_REFUSE_NONE);
    CHECK(state.depth_enable == 0ul);
}

static void test_blend_and_alpha(void)
{
    V9X_R3D_DRAW draw;
    struct v9x_tdfx_3d_state state;

    base_draw(&draw);
    draw.blend_enable = 1ul;
    draw.src_blend = V9X_R3D_BLEND_SRCALPHA;
    draw.dst_blend = V9X_R3D_BLEND_INVSRCALPHA;
    CHECK(map(&draw, 0, &state) == V9X_D3D_TDFX_REFUSE_NONE);
    CHECK(state.blend_enable == 1ul);
    CHECK(state.blend_src == V9X_TDFX_BLEND_SRC_ALPHA);
    CHECK(state.blend_dst == V9X_TDFX_BLEND_INV_SRC_ALPHA);

    /* ONE, ZERO; the colour factors on the side the chip has them (code 2
     * is the other surface's colour, 6 its inverse); saturate as source. */
    draw.src_blend = V9X_R3D_BLEND_ONE;
    draw.dst_blend = V9X_R3D_BLEND_ZERO;
    CHECK(map(&draw, 0, &state) == V9X_D3D_TDFX_REFUSE_NONE);
    CHECK(state.blend_src == 4ul && state.blend_dst == 0ul);
    draw.src_blend = V9X_R3D_BLEND_DESTCOLOR;
    draw.dst_blend = V9X_R3D_BLEND_INVSRCCOLOR;
    CHECK(map(&draw, 0, &state) == V9X_D3D_TDFX_REFUSE_NONE);
    CHECK(state.blend_src == 2ul && state.blend_dst == 6ul);
    draw.src_blend = V9X_R3D_BLEND_INVDESTCOLOR;
    draw.dst_blend = V9X_R3D_BLEND_SRCCOLOR;
    CHECK(map(&draw, 0, &state) == V9X_D3D_TDFX_REFUSE_NONE);
    CHECK(state.blend_src == 6ul && state.blend_dst == 2ul);
    draw.src_blend = V9X_R3D_BLEND_SRCALPHASAT;
    CHECK(map(&draw, 0, &state) == V9X_D3D_TDFX_REFUSE_NONE);
    CHECK(state.blend_src == 0xful);

    /* A 16-bit target has no alpha: destination alpha is refused, and a
     * colour factor on the side the chip lacks it. */
    draw.src_blend = V9X_R3D_BLEND_DESTALPHA;
    CHECK(map(&draw, 0, &state) == V9X_D3D_TDFX_REFUSE_BLEND);
    draw.src_blend = V9X_R3D_BLEND_ONE;
    draw.dst_blend = V9X_R3D_BLEND_INVDESTALPHA;
    CHECK(map(&draw, 0, &state) == V9X_D3D_TDFX_REFUSE_BLEND);
    draw.src_blend = V9X_R3D_BLEND_SRCCOLOR;
    draw.dst_blend = V9X_R3D_BLEND_ZERO;
    CHECK(map(&draw, 0, &state) == V9X_D3D_TDFX_REFUSE_BLEND);
    draw.src_blend = V9X_R3D_BLEND_ONE;
    draw.dst_blend = V9X_R3D_BLEND_DESTCOLOR;
    CHECK(map(&draw, 0, &state) == V9X_D3D_TDFX_REFUSE_BLEND);
    /* Blending off: the factors are not read. */
    draw.blend_enable = 0ul;
    CHECK(map(&draw, 0, &state) == V9X_D3D_TDFX_REFUSE_NONE);
    CHECK(state.blend_enable == 0ul);

    base_draw(&draw);
    draw.alpha_test_enable = 1ul;
    draw.alpha_func = V9X_R3D_CMP_GREATER;
    draw.alpha_ref = 0x180ul;
    CHECK(map(&draw, 0, &state) == V9X_D3D_TDFX_REFUSE_NONE);
    CHECK(state.alpha_test == 1ul);
    CHECK(state.alpha_func == V9X_TDFX_CMP_GREATER);
    CHECK(state.alpha_ref == 0x80ul);
    draw.alpha_func = 0ul;
    CHECK(map(&draw, 0, &state) == V9X_D3D_TDFX_REFUSE_ALPHA_FUNC);
}

static void test_fog_specular_explicit(void)
{
    V9X_R3D_DRAW draw;
    struct v9x_tdfx_3d_state state;

    base_draw(&draw);
    draw.fog_enable = 1ul;
    CHECK(map(&draw, 0, &state) == V9X_D3D_TDFX_REFUSE_FOG);

    /* SPECULARENABLE defaults on: refused only when a vertex adds colour. */
    base_draw(&draw);
    draw.specular_enable = 1ul;
    CHECK(map(&draw, 0, &state) == V9X_D3D_TDFX_REFUSE_NONE);
    CHECK(v9x_d3d_tdfx_map_draw(&draw, 0, BASE, VRAM, 0x00102030ul,
                                &state) == V9X_D3D_TDFX_REFUSE_SPECULAR);

    /* A render-interface draw is honoured exactly or refused: all of RGB
     * written, no CPU texture levels; its scissor goes to the clip
     * registers, and an empty one is refused (the chip wants left below
     * right), which the core's fallback then draws as nothing. */
    base_draw(&draw);
    draw.explicit_state = 1ul;
    draw.write_mask = 7ul;
    draw.scissor_right = 640ul;
    draw.scissor_bottom = 480ul;
    CHECK(map(&draw, 0, &state) == V9X_D3D_TDFX_REFUSE_NONE);
    CHECK(state.scissor == 1ul && state.scissor_left == 0ul &&
          state.scissor_right == 640ul && state.scissor_top == 0ul &&
          state.scissor_bottom == 480ul);
    draw.write_mask = 3ul;
    CHECK(map(&draw, 0, &state) == V9X_D3D_TDFX_REFUSE_EXPLICIT);
    draw.write_mask = 7ul;
    draw.scissor_left = 1ul;
    draw.scissor_bottom = 479ul;
    CHECK(map(&draw, 0, &state) == V9X_D3D_TDFX_REFUSE_NONE);
    CHECK(state.scissor_left == 1ul && state.scissor_bottom == 479ul);
    draw.scissor_left = 640ul;
    CHECK(map(&draw, 0, &state) == V9X_D3D_TDFX_REFUSE_EXPLICIT);
    draw.scissor_left = 0ul;
    draw.scissor_bottom = 481ul;
    CHECK(map(&draw, 0, &state) == V9X_D3D_TDFX_REFUSE_EXPLICIT);
    draw.scissor_bottom = 480ul;
    /* A Direct3D draw carries no scissor: the whole target. */
    draw.explicit_state = 0ul;
    draw.scissor_left = 5ul;
    CHECK(map(&draw, 0, &state) == V9X_D3D_TDFX_REFUSE_NONE);
    CHECK(state.scissor == 0ul);
    draw.scissor_left = 0ul;
    draw.explicit_state = 1ul;
    draw.texture.level_count = 1ul;
    draw.texture.levels = (const V9X_R3D_LEVEL *)&surface_object;
    CHECK(map(&draw, 0, &state) == V9X_D3D_TDFX_REFUSE_EXPLICIT);
}

static void test_texture(void)
{
    V9X_R3D_DRAW draw;
    V9X_D3D_TDFX_TEXTURE resolved;
    struct v9x_tdfx_3d_state state;

    base_draw(&draw);
    base_texture(&draw, &resolved);
    CHECK(map(&draw, &resolved, &state) == V9X_D3D_TDFX_REFUSE_NONE);
    CHECK(state.texture_enable == 1ul);
    CHECK(state.texture.offset == BASE + 0x00200000ul);
    CHECK(state.texture.format == V9X_TDFX_TEX_RGB565);
    CHECK(state.texture.log2_size == 6ul);
    CHECK(state.texture.aspect == 0ul);
    CHECK(state.texture.min_bilinear == 0ul &&
          state.texture.mag_bilinear == 0ul);
    CHECK(state.texture.clamp == 0ul);
    /* MODULATE on a texture without alpha multiplies both; its texels'
     * alpha is one, so the vertex alpha comes through. */
    CHECK(state.texture.combine == V9X_TDFX_COMBINE_MODULATE);

    /* With alpha, Direct3D's MODULATE takes the texel's alpha. */
    resolved.format = V9X_TDFX_TEX_ARGB4444;
    resolved.has_alpha = 1;
    CHECK(map(&draw, &resolved, &state) == V9X_D3D_TDFX_REFUSE_NONE);
    CHECK(state.texture.combine == V9X_TDFX_COMBINE_MODULATE_TEXALPHA);
    draw.texture.op = V9X_R3D_TEXOP_MODULATEALPHA;
    CHECK(map(&draw, &resolved, &state) == V9X_D3D_TDFX_REFUSE_NONE);
    CHECK(state.texture.combine == V9X_TDFX_COMBINE_MODULATE);
    draw.texture.op = V9X_R3D_TEXOP_DECAL;
    CHECK(map(&draw, &resolved, &state) == V9X_D3D_TDFX_REFUSE_NONE);
    CHECK(state.texture.combine == V9X_TDFX_COMBINE_REPLACE);
    draw.texture.op = V9X_R3D_TEXOP_COPY;
    CHECK(map(&draw, &resolved, &state) == V9X_D3D_TDFX_REFUSE_NONE);
    CHECK(state.texture.combine == V9X_TDFX_COMBINE_REPLACE);
    draw.texture.op = V9X_R3D_TEXOP_DECALALPHA;
    CHECK(map(&draw, &resolved, &state) == V9X_D3D_TDFX_REFUSE_TEXTURE_OP);
    draw.texture.op = V9X_R3D_TEXOP_MODULATEMASK;
    CHECK(map(&draw, &resolved, &state) == V9X_D3D_TDFX_REFUSE_TEXTURE_OP);
    draw.texture.op = V9X_R3D_TEXOP_MODULATE;

    /* Filters: one level, so a mip filter folds to its within-level half. */
    draw.texture.min_filter = V9X_R3D_FILTER_LINEAR;
    CHECK(map(&draw, &resolved, &state) == V9X_D3D_TDFX_REFUSE_NONE);
    CHECK(state.texture.min_bilinear == 1ul &&
          state.texture.mag_bilinear == 0ul);
    draw.texture.min_filter = V9X_R3D_FILTER_MIPNEAREST;
    draw.texture.mag_filter = V9X_R3D_FILTER_LINEARMIPLINEAR;
    CHECK(map(&draw, &resolved, &state) == V9X_D3D_TDFX_REFUSE_NONE);
    CHECK(state.texture.min_bilinear == 0ul &&
          state.texture.mag_bilinear == 1ul);
    draw.texture.min_filter = V9X_R3D_FILTER_MIPLINEAR;
    CHECK(map(&draw, &resolved, &state) == V9X_D3D_TDFX_REFUSE_NONE);
    CHECK(state.texture.min_bilinear == 1ul);
    draw.texture.min_filter = V9X_R3D_FILTER_LINEARMIPNEAREST;
    CHECK(map(&draw, &resolved, &state) == V9X_D3D_TDFX_REFUSE_NONE);
    CHECK(state.texture.min_bilinear == 0ul);
    draw.texture.min_filter = 0ul;
    CHECK(map(&draw, &resolved, &state) ==
          V9X_D3D_TDFX_REFUSE_TEXTURE_FILTER);
    draw.texture.min_filter = V9X_R3D_FILTER_NEAREST;
    draw.texture.mag_filter = 7ul;
    CHECK(map(&draw, &resolved, &state) ==
          V9X_D3D_TDFX_REFUSE_TEXTURE_FILTER);
    draw.texture.mag_filter = V9X_R3D_FILTER_NEAREST;

    /* A chain of seven packed levels, 64 down to 1: a mip minification
     * filter samples all of them, any other level 0 alone. A within-level
     * blend between levels is not built, so the LINEARMIP filters take
     * their nearest level. */
    resolved.levels = 7ul;
    CHECK(map(&draw, &resolved, &state) == V9X_D3D_TDFX_REFUSE_NONE);
    CHECK(state.texture.levels == 1ul);
    draw.texture.min_filter = V9X_R3D_FILTER_MIPNEAREST;
    CHECK(map(&draw, &resolved, &state) == V9X_D3D_TDFX_REFUSE_NONE);
    CHECK(state.texture.levels == 7ul);
    CHECK(state.texture.min_bilinear == 0ul);
    draw.texture.min_filter = V9X_R3D_FILTER_MIPLINEAR;
    CHECK(map(&draw, &resolved, &state) == V9X_D3D_TDFX_REFUSE_NONE);
    CHECK(state.texture.levels == 7ul);
    CHECK(state.texture.min_bilinear == 1ul);
    draw.texture.min_filter = V9X_R3D_FILTER_LINEARMIPLINEAR;
    CHECK(map(&draw, &resolved, &state) == V9X_D3D_TDFX_REFUSE_NONE);
    CHECK(state.texture.levels == 7ul);
    draw.texture.min_filter = V9X_R3D_FILTER_LINEAR;
    CHECK(map(&draw, &resolved, &state) == V9X_D3D_TDFX_REFUSE_NONE);
    CHECK(state.texture.levels == 1ul);
    /* A mag filter does not choose levels. */
    draw.texture.min_filter = V9X_R3D_FILTER_NEAREST;
    draw.texture.mag_filter = V9X_R3D_FILTER_MIPLINEAR;
    CHECK(map(&draw, &resolved, &state) == V9X_D3D_TDFX_REFUSE_NONE);
    CHECK(state.texture.levels == 1ul);
    draw.texture.mag_filter = V9X_R3D_FILTER_NEAREST;
    /* No chain (0, as the engine leaves an unwalked texture) is one
     * level; more levels than the edge halves is a shape the chip cannot
     * sample. */
    draw.texture.min_filter = V9X_R3D_FILTER_MIPNEAREST;
    resolved.levels = 0ul;
    CHECK(map(&draw, &resolved, &state) == V9X_D3D_TDFX_REFUSE_NONE);
    CHECK(state.texture.levels == 1ul);
    resolved.levels = 8ul;
    CHECK(map(&draw, &resolved, &state) == V9X_D3D_TDFX_REFUSE_TEXTURE_SHAPE);
    resolved.levels = 0ul;
    draw.texture.min_filter = V9X_R3D_FILTER_NEAREST;

    /* Address: wrap and clamp; mirror, border and the cylindrical wraps
     * are not built. */
    draw.texture.address = V9X_R3D_ADDRESS_CLAMP;
    CHECK(map(&draw, &resolved, &state) == V9X_D3D_TDFX_REFUSE_NONE);
    CHECK(state.texture.clamp == 1ul);
    draw.texture.address = V9X_R3D_ADDRESS_MIRROR;
    CHECK(map(&draw, &resolved, &state) ==
          V9X_D3D_TDFX_REFUSE_TEXTURE_ADDRESS);
    draw.texture.address = V9X_R3D_ADDRESS_BORDER;
    CHECK(map(&draw, &resolved, &state) ==
          V9X_D3D_TDFX_REFUSE_TEXTURE_ADDRESS);
    draw.texture.address = V9X_R3D_ADDRESS_WRAP;
    draw.texture.wrap_u = 1ul;
    CHECK(map(&draw, &resolved, &state) ==
          V9X_D3D_TDFX_REFUSE_TEXTURE_ADDRESS);
    draw.texture.wrap_u = 0ul;
    /* wrap_either is the core's folded flag, set whatever the application
     * asked: not read. */
    draw.texture.wrap_either = 1ul;
    CHECK(map(&draw, &resolved, &state) == V9X_D3D_TDFX_REFUSE_NONE);

    /* Shape: powers of two up to 256 a side and 8:1, at the tight pitch.
     * The long side is log2_size; aspect and S wider say the rest. */
    resolved.height = 32ul;
    CHECK(map(&draw, &resolved, &state) == V9X_D3D_TDFX_REFUSE_NONE);
    CHECK(state.texture.log2_size == 6ul && state.texture.aspect == 1ul &&
          state.texture.s_wider == 1ul);
    resolved.width = 16ul;
    resolved.height = 128ul;
    resolved.pitch_bytes = 32ul;
    CHECK(map(&draw, &resolved, &state) == V9X_D3D_TDFX_REFUSE_NONE);
    CHECK(state.texture.log2_size == 7ul && state.texture.aspect == 3ul &&
          state.texture.s_wider == 0ul);
    /* A chain of a 16x128 runs to the long side's last texel: 8 levels. */
    draw.texture.min_filter = V9X_R3D_FILTER_MIPNEAREST;
    resolved.levels = 8ul;
    CHECK(map(&draw, &resolved, &state) == V9X_D3D_TDFX_REFUSE_NONE);
    CHECK(state.texture.levels == 8ul);
    resolved.levels = 9ul;
    CHECK(map(&draw, &resolved, &state) == V9X_D3D_TDFX_REFUSE_TEXTURE_SHAPE);
    resolved.levels = 0ul;
    draw.texture.min_filter = V9X_R3D_FILTER_NEAREST;
    /* 16:1 is past the chip's 8:1. */
    resolved.width = 8ul;
    resolved.pitch_bytes = 16ul;
    CHECK(map(&draw, &resolved, &state) == V9X_D3D_TDFX_REFUSE_TEXTURE_SHAPE);
    resolved.width = 64ul;
    resolved.height = 64ul;
    resolved.pitch_bytes = 256ul;
    CHECK(map(&draw, &resolved, &state) == V9X_D3D_TDFX_REFUSE_TEXTURE_SHAPE);
    resolved.width = resolved.height = 512ul;
    resolved.pitch_bytes = 1024ul;
    CHECK(map(&draw, &resolved, &state) == V9X_D3D_TDFX_REFUSE_TEXTURE_SHAPE);
    resolved.width = resolved.height = 48ul;
    resolved.pitch_bytes = 96ul;
    CHECK(map(&draw, &resolved, &state) == V9X_D3D_TDFX_REFUSE_TEXTURE_SHAPE);
    resolved.width = resolved.height = 1ul;
    resolved.pitch_bytes = 2ul;
    CHECK(map(&draw, &resolved, &state) == V9X_D3D_TDFX_REFUSE_NONE);
    CHECK(state.texture.log2_size == 0ul);

    resolved.format = V9X_D3D_TDFX_TEXTURE_UNKNOWN;
    CHECK(map(&draw, &resolved, &state) ==
          V9X_D3D_TDFX_REFUSE_TEXTURE_FORMAT);
    CHECK(map(&draw, 0, &state) == V9X_D3D_TDFX_REFUSE_TEXTURE_FORMAT);

    base_texture(&draw, &resolved);
    draw.color_key_enable = 1ul;
    CHECK(map(&draw, &resolved, &state) == V9X_D3D_TDFX_REFUSE_COLOR_KEY);
}

static void corner(V9X_R3D_VERTEX *v, float x, float y, v9x_u32 color)
{
    memset(v, 0, sizeof(*v));
    v->sx = x;
    v->sy = y;
    v->sz = 0.5f;
    v->rhw = 0.5f;
    v->color = color;
    v->tu = 0.25f;
    v->tv = 1.0f;
}

static void test_triangle(void)
{
    V9X_R3D_VERTEX t[3];
    struct v9x_tdfx_3d_vertex out[3];
    struct v9x_tdfx_3d_texture square;
    struct v9x_tdfx_3d_texture *tex = &square;

    memset(&square, 0, sizeof(square));
    square.log2_size = 6ul;

    corner(&t[0], 10.0f, 10.0f, 0xff804020ul);
    corner(&t[1], 100.0f, 10.0f, 0x80ff0000ul);
    corner(&t[2], 10.0f, 100.0f, 0x000000fful);
    CHECK(v9x_d3d_tdfx_triangle(t, V9X_R3D_SHADE_GOURAUD, tex, out));
    /* Positions pass through: the core has already put Direct3D's on the
     * render core's pixel centres, x + 0.5, which is where the chip
     * samples with subpixel correction (d3d_core.c v9x_d3d_centre;
     * V9XDDP and V9XGLP TexPhase, A8U4I5 boot 444). */
    CHECK(out[0].x == 10.0f && out[0].y == 10.0f);
    CHECK(out[1].x == 100.0f && out[2].y == 100.0f);

    /* Texture coordinates are not moved either: with subpixel correction
     * the chip starts them at the pixel centre (tdfx_3d.c). */
    t[1].sx = 106.0f;
    t[1].tu = 1.0f;
    CHECK(v9x_d3d_tdfx_triangle(t, V9X_R3D_SHADE_GOURAUD, tex, out));
    CHECK(out[0].sow == 32.0f && out[1].sow == 128.0f);
    t[1].sx = 100.0f;
    t[1].tu = 0.25f;

    CHECK(v9x_d3d_tdfx_triangle(t, V9X_R3D_SHADE_GOURAUD, tex, out));
    CHECK(out[0].r == 128.0f && out[0].g == 64.0f && out[0].b == 32.0f &&
          out[0].a == 255.0f);
    CHECK(out[1].r == 255.0f && out[1].a == 128.0f);
    CHECK(out[2].b == 255.0f && out[2].a == 0.0f);
    /* Z in depth counts; 1/W is RHW; S/W and T/W are 256ths of the width
     * times RHW: 0.25 x 256 x 0.5 = 32, 1 x 256 x 0.5 = 128. */
    CHECK(out[0].z == 0.5f * 65535.0f);
    CHECK(out[0].oow == 0.5f);
    CHECK(out[0].sow == 32.0f && out[0].tow == 128.0f);

    /* Rectangular: S and T count texels of the 256-long level of the same
     * aspect, so the short side's unit is 256 >> aspect (Glide's
     * convention; not yet measured). 64x32: T in 128ths; 32x64: S. */
    {
        struct v9x_tdfx_3d_texture wide = square;

        wide.aspect = 1ul;
        wide.s_wider = 1ul;
        CHECK(v9x_d3d_tdfx_triangle(t, V9X_R3D_SHADE_GOURAUD, &wide, out));
        CHECK(out[0].sow == 32.0f && out[0].tow == 64.0f);
        wide.s_wider = 0ul;
        CHECK(v9x_d3d_tdfx_triangle(t, V9X_R3D_SHADE_GOURAUD, &wide, out));
        CHECK(out[0].sow == 16.0f && out[0].tow == 128.0f);
    }

    /* Flat: vertex 0's colour and alpha throughout. */
    CHECK(v9x_d3d_tdfx_triangle(t, V9X_R3D_SHADE_FLAT, tex, out));
    CHECK(out[1].r == 128.0f && out[2].b == 32.0f && out[2].a == 255.0f);

    /* Z clamps to the buffer's range. */
    t[0].sz = 1.5f;
    t[1].sz = -0.25f;
    CHECK(v9x_d3d_tdfx_triangle(t, V9X_R3D_SHADE_GOURAUD, 0, out));
    CHECK(out[0].z == 65535.0f && out[1].z == 0.0f);

    /* Untextured: RHW is not read, so zero is fine; textured, it is not. */
    t[0].rhw = 0.0f;
    CHECK(v9x_d3d_tdfx_triangle(t, V9X_R3D_SHADE_GOURAUD, 0, out));
    CHECK(!v9x_d3d_tdfx_triangle(t, V9X_R3D_SHADE_GOURAUD, tex, out));
    t[0].rhw = 0.5f;

    /* Zero area and a coordinate that is not finite draw nothing. */
    t[2].sx = 55.0f;
    t[2].sy = 10.0f;
    CHECK(!v9x_d3d_tdfx_triangle(t, V9X_R3D_SHADE_GOURAUD, 0, out));
    t[2].sx = 10.0f;
    t[2].sy = 100.0f;
    {
        union {
            v9x_u32 bits;
            float value;
        } nan;

        nan.bits = 0x7fc00000ul;
        t[1].sx = nan.value;
        CHECK(!v9x_d3d_tdfx_triangle(t, V9X_R3D_SHADE_GOURAUD, 0, out));
    }
}

unsigned int v9x_run_d3d_tdfx_map_tests(void)
{
    failures = 0u;
    test_untextured();
    test_depth();
    test_blend_and_alpha();
    test_fog_specular_explicit();
    test_texture();
    test_triangle();
    return failures;
}
