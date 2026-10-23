#include <stdio.h>
#include <string.h>

#include "../../src/display32/d3d/d3d_r128_map.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

/* The Rage 128's desktop starts at video memory 0 under tier-0; a base
 * is kept here so the offset arithmetic is exercised. */
#define BASE 0x00010000ul
#define VRAM 0x00ff0000ul

static int surface_object;
static int depth_object;
static int texture_object;

/* An 800x600 RGB565 back buffer 1 MiB into the mapping, Gouraud. */
static void base_draw(V9X_R3D_DRAW *draw)
{
    memset(draw, 0, sizeof(*draw));
    draw->target.offset = 0x00100000ul;
    draw->target.pitch = 1600ul;
    draw->target.width = 800ul;
    draw->target.height = 600ul;
    draw->target.format = V9X_R3D_FORMAT_RGB565;
    draw->target.object = &surface_object;
    draw->shade_mode = V9X_R3D_SHADE_GOURAUD;
    draw->texture.min_filter = V9X_R3D_FILTER_NEAREST;
    draw->texture.mag_filter = V9X_R3D_FILTER_NEAREST;
    draw->texture.address = V9X_R3D_ADDRESS_WRAP;
    draw->texture.op = V9X_R3D_TEXOP_MODULATE;
}

/* A bound 64x32 RGB565 texture with three levels. */
static void base_texture(V9X_R3D_DRAW *draw, V9X_D3D_R128_TEXTURE *resolved)
{
    draw->texture.object = &texture_object;
    memset(resolved, 0, sizeof(*resolved));
    resolved->datatype = V9X_R128_TEX_RGB565;
    resolved->has_alpha = 0;
    resolved->width = 64ul;
    resolved->height = 32ul;
    resolved->bytes_per_texel = 2ul;
    resolved->pitch_bytes = 128ul;
    resolved->levels = 3ul;
    resolved->offsets[0] = 0x00200000ul;
    resolved->offsets[1] = 0x00201000ul;
    resolved->offsets[2] = 0x00201400ul;
}

static v9x_u32 map(const V9X_R3D_DRAW *draw,
                   const V9X_D3D_R128_TEXTURE *resolved, v9x_u32 depth_bytes,
                   struct v9x_r128_3d_state *state)
{
    return v9x_d3d_r128_map_draw(draw, resolved, BASE, VRAM, depth_bytes,
                                 state);
}

static void test_untextured(void)
{
    V9X_R3D_DRAW draw;
    struct v9x_r128_3d_state state;
    struct v9x_r128_3d_stream stream;

    base_draw(&draw);
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_R128_REFUSE_NONE);
    CHECK(state.target_offset == BASE + 0x00100000ul);
    CHECK(state.target_pitch_bytes == 1600ul);
    CHECK(state.target_bytes_per_pixel == 2ul);
    CHECK(state.vram_bytes == BASE + VRAM);
    CHECK(state.depth_enable == 0ul && state.texture.enable == 0ul);
    CHECK(state.blend_enable == 0ul && state.alpha_test_enable == 0ul);
    CHECK(state.flat_shade == 0ul);
    CHECK(state.src_blend == V9X_R128_BLEND_ONE &&
          state.dst_blend == V9X_R128_BLEND_ZERO);
    CHECK(v9x_r128_3d_build_state(&state, &stream) == V9X_STATUS_OK);

    draw.target.format = V9X_R3D_FORMAT_XRGB8888;
    draw.target.pitch = 3200ul;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_R128_REFUSE_NONE);
    CHECK(state.target_bytes_per_pixel == 4ul);
    draw.target.format = V9X_R3D_FORMAT_XRGB1555;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_R128_REFUSE_TARGET);

    base_draw(&draw);
    draw.shade_mode = V9X_R3D_SHADE_FLAT;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_R128_REFUSE_NONE);
    CHECK(state.flat_shade == 1ul);
    draw.shade_mode = 3ul;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_R128_REFUSE_SHADE);

    /* Fog and specular are the chip's own, from the vertex. */
    base_draw(&draw);
    draw.fog_enable = 1ul;
    draw.fog_color = 0xff336699ul;
    draw.specular_enable = 1ul;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_R128_REFUSE_NONE);
    CHECK(state.fog_enable == 1ul && state.fog_color == 0x00336699ul);
    CHECK(state.specular_enable == 1ul);

    /* The render interface's explicit draws are not built yet. */
    base_draw(&draw);
    draw.explicit_state = 1ul;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_R128_REFUSE_EXPLICIT);
}

static void test_depth_and_tests(void)
{
    V9X_R3D_DRAW draw;
    struct v9x_r128_3d_state state;
    static const v9x_u32 codes[8] = {
        V9X_R128_CMP_NEVER, V9X_R128_CMP_LESS, V9X_R128_CMP_EQUAL,
        V9X_R128_CMP_LESSEQUAL, V9X_R128_CMP_GREATER, V9X_R128_CMP_NOTEQUAL,
        V9X_R128_CMP_GREATEREQUAL, V9X_R128_CMP_ALWAYS
    };
    v9x_u32 func;

    base_draw(&draw);
    draw.depth_enable = 1ul;
    draw.depth_write = 1ul;
    draw.depth.object = &depth_object;
    draw.depth.offset = 0x00400000ul;
    draw.depth.pitch = 1600ul;
    for (func = V9X_R3D_CMP_NEVER; func <= V9X_R3D_CMP_ALWAYS; ++func) {
        draw.depth_func = func;
        CHECK(map(&draw, 0, 2ul, &state) == V9X_D3D_R128_REFUSE_NONE);
        CHECK(state.depth_func == codes[func - 1ul]);
    }
    CHECK(state.depth_enable == 1ul && state.depth_write == 1ul);
    CHECK(state.depth_offset == BASE + 0x00400000ul);
    CHECK(state.depth_pitch_bytes == 1600ul);
    CHECK(state.depth_bytes_per_pixel == 2ul);
    draw.depth_func = 9ul;
    CHECK(map(&draw, 0, 2ul, &state) == V9X_D3D_R128_REFUSE_DEPTH_FUNC);
    draw.depth_func = V9X_R3D_CMP_LESS;
    CHECK(map(&draw, 0, 4ul, &state) == V9X_D3D_R128_REFUSE_DEPTH_SURFACE);

    /* Depth on but nothing bound: drawn without Z. */
    draw.depth.object = 0;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_R128_REFUSE_NONE);
    CHECK(state.depth_enable == 0ul);

    base_draw(&draw);
    draw.alpha_test_enable = 1ul;
    draw.alpha_func = V9X_R3D_CMP_GREATER;
    draw.alpha_ref = 0x180ul;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_R128_REFUSE_NONE);
    CHECK(state.alpha_test_enable == 1ul &&
          state.alpha_func == V9X_R128_CMP_GREATER &&
          state.alpha_ref == 0x80ul);
    draw.alpha_func = 0ul;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_R128_REFUSE_ALPHA_FUNC);
}

static void test_blend(void)
{
    V9X_R3D_DRAW draw;
    struct v9x_r128_3d_state state;

    base_draw(&draw);
    draw.blend_enable = 1ul;
    draw.src_blend = V9X_R3D_BLEND_SRCALPHA;
    draw.dst_blend = V9X_R3D_BLEND_INVSRCALPHA;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_R128_REFUSE_NONE);
    CHECK(state.blend_enable == 1ul &&
          state.src_blend == V9X_R128_BLEND_SRCALPHA &&
          state.dst_blend == V9X_R128_BLEND_INVSRCALPHA);

    draw.src_blend = V9X_R3D_BLEND_DESTCOLOR;
    draw.dst_blend = V9X_R3D_BLEND_SRCCOLOR;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_R128_REFUSE_NONE);
    CHECK(state.src_blend == V9X_R128_BLEND_DSTCOLOR &&
          state.dst_blend == V9X_R128_BLEND_SRCCOLOR);

    draw.src_blend = V9X_R3D_BLEND_SRCALPHASAT;
    draw.dst_blend = V9X_R3D_BLEND_ONE;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_R128_REFUSE_NONE);
    CHECK(state.src_blend == V9X_R128_BLEND_SAT);
    draw.dst_blend = V9X_R3D_BLEND_SRCALPHASAT;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_R128_REFUSE_BLEND);

    /* No destination alpha in either target. */
    draw.src_blend = V9X_R3D_BLEND_DESTALPHA;
    draw.dst_blend = V9X_R3D_BLEND_ZERO;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_R128_REFUSE_BLEND);
}

static void test_texture(void)
{
    V9X_R3D_DRAW draw;
    V9X_D3D_R128_TEXTURE resolved;
    struct v9x_r128_3d_state state;
    struct v9x_r128_3d_stream stream;

    /* Plain filter: level 0 only, whatever is attached. */
    base_draw(&draw);
    base_texture(&draw, &resolved);
    CHECK(map(&draw, &resolved, 0ul, &state) == V9X_D3D_R128_REFUSE_NONE);
    CHECK(state.texture.enable == 1ul);
    CHECK(state.texture.datatype == V9X_R128_TEX_RGB565);
    CHECK(state.texture.width_log2 == 6ul && state.texture.height_log2 == 5ul);
    CHECK(state.texture.levels == 1ul);
    CHECK(state.texture.offsets[0] == BASE + 0x00200000ul);
    CHECK(state.texture.combine == V9X_R128_COMBINE_MODULATE_RGB);
    CHECK(state.texture.clamp_s == 0ul && state.texture.clamp_t == 0ul);
    CHECK(v9x_r128_3d_build_state(&state, &stream) == V9X_STATUS_OK);

    /* A mip filter takes the chain. */
    draw.texture.min_filter = V9X_R3D_FILTER_MIPLINEAR;
    draw.texture.mag_filter = V9X_R3D_FILTER_LINEAR;
    CHECK(map(&draw, &resolved, 0ul, &state) == V9X_D3D_R128_REFUSE_NONE);
    CHECK(state.texture.levels == 3ul);
    CHECK(state.texture.min_filter == V9X_R128_FILTER_MIPLINEAR);
    CHECK(state.texture.offsets[2] == BASE + 0x00201400ul);
    CHECK(v9x_r128_3d_build_state(&state, &stream) == V9X_STATUS_OK);

    /* ...and falls back within level 0 without one. */
    resolved.levels = 1ul;
    CHECK(map(&draw, &resolved, 0ul, &state) == V9X_D3D_R128_REFUSE_NONE);
    CHECK(state.texture.levels == 1ul &&
          state.texture.min_filter == V9X_R128_FILTER_LINEAR);
    draw.texture.min_filter = V9X_R3D_FILTER_LINEARMIPNEAREST;
    CHECK(map(&draw, &resolved, 0ul, &state) == V9X_D3D_R128_REFUSE_NONE);
    CHECK(state.texture.min_filter == V9X_R128_FILTER_NEAREST);
    CHECK(v9x_r128_3d_build_state(&state, &stream) == V9X_STATUS_OK);

    /* Combines by texture op and alpha. */
    base_draw(&draw);
    base_texture(&draw, &resolved);
    resolved.has_alpha = 1;
    resolved.datatype = V9X_R128_TEX_ARGB4444;
    CHECK(map(&draw, &resolved, 0ul, &state) == V9X_D3D_R128_REFUSE_NONE);
    CHECK(state.texture.combine == V9X_R128_COMBINE_MODULATE_TEXALPHA);
    draw.texture.op = V9X_R3D_TEXOP_MODULATEALPHA;
    CHECK(map(&draw, &resolved, 0ul, &state) == V9X_D3D_R128_REFUSE_NONE);
    CHECK(state.texture.combine == V9X_R128_COMBINE_MODULATE);
    draw.texture.op = V9X_R3D_TEXOP_DECAL;
    CHECK(map(&draw, &resolved, 0ul, &state) == V9X_D3D_R128_REFUSE_NONE);
    CHECK(state.texture.combine == V9X_R128_COMBINE_REPLACE);
    draw.texture.op = V9X_R3D_TEXOP_COPY;
    CHECK(map(&draw, &resolved, 0ul, &state) == V9X_D3D_R128_REFUSE_NONE);
    CHECK(state.texture.combine == V9X_R128_COMBINE_REPLACE);
    draw.texture.op = V9X_R3D_TEXOP_DECALALPHA;
    CHECK(map(&draw, &resolved, 0ul, &state) == V9X_D3D_R128_REFUSE_NONE);
    CHECK(state.texture.combine == V9X_R128_COMBINE_DECAL_ALPHA);
    draw.texture.op = V9X_R3D_TEXOP_ADD;
    CHECK(map(&draw, &resolved, 0ul, &state) ==
          V9X_D3D_R128_REFUSE_TEXTURE_OP);

    /* Address modes, filters, shapes, formats, colour key. */
    base_draw(&draw);
    base_texture(&draw, &resolved);
    draw.texture.address = V9X_R3D_ADDRESS_CLAMP;
    CHECK(map(&draw, &resolved, 0ul, &state) == V9X_D3D_R128_REFUSE_NONE);
    CHECK(state.texture.clamp_s == 1ul && state.texture.clamp_t == 1ul);
    draw.texture.address = V9X_R3D_ADDRESS_MIRROR;
    CHECK(map(&draw, &resolved, 0ul, &state) ==
          V9X_D3D_R128_REFUSE_TEXTURE_ADDRESS);
    draw.texture.address = V9X_R3D_ADDRESS_WRAP;
    draw.texture.wrap_u = 1ul;
    CHECK(map(&draw, &resolved, 0ul, &state) ==
          V9X_D3D_R128_REFUSE_TEXTURE_ADDRESS);
    draw.texture.wrap_u = 0ul;
    draw.texture.wrap_either = 1ul;
    CHECK(map(&draw, &resolved, 0ul, &state) == V9X_D3D_R128_REFUSE_NONE);
    draw.texture.mag_filter = V9X_R3D_FILTER_MIPLINEAR;
    CHECK(map(&draw, &resolved, 0ul, &state) ==
          V9X_D3D_R128_REFUSE_TEXTURE_FILTER);
    draw.texture.mag_filter = V9X_R3D_FILTER_NEAREST;
    resolved.pitch_bytes = 256ul;
    CHECK(map(&draw, &resolved, 0ul, &state) ==
          V9X_D3D_R128_REFUSE_TEXTURE_SHAPE);
    resolved.pitch_bytes = 96ul;
    resolved.width = 48ul;
    CHECK(map(&draw, &resolved, 0ul, &state) ==
          V9X_D3D_R128_REFUSE_TEXTURE_SHAPE);
    base_texture(&draw, &resolved);
    resolved.datatype = V9X_D3D_R128_TEXTURE_UNKNOWN;
    CHECK(map(&draw, &resolved, 0ul, &state) ==
          V9X_D3D_R128_REFUSE_TEXTURE_FORMAT);
    base_texture(&draw, &resolved);
    draw.color_key_enable = 1ul;
    CHECK(map(&draw, &resolved, 0ul, &state) ==
          V9X_D3D_R128_REFUSE_COLOR_KEY);
}

unsigned int v9x_run_d3d_r128_map_tests(void)
{
    failures = 0u;
    test_untextured();
    test_depth_and_tests();
    test_blend();
    test_texture();
    return failures;
}
