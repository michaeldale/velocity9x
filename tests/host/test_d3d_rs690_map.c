#include <stdio.h>
#include <string.h>

#include "../../src/display32/d3d/d3d_rs690_map.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

/* M-GB8KRE's frame buffer at MC 78000000; a desktop offset is kept so
 * the offset arithmetic is exercised. */
#define FB_BASE 0x78000000ul
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

/* A bound 64x32 RGB565 texture, rows padded to 128 bytes. */
static void base_texture(V9X_R3D_DRAW *draw, V9X_D3D_RS690_TEXTURE *resolved)
{
    draw->texture.object = &texture_object;
    memset(resolved, 0, sizeof(*resolved));
    resolved->format = V9X_RS690_TEXFMT_RGB565;
    resolved->has_alpha = 0;
    resolved->width = 64ul;
    resolved->height = 32ul;
    resolved->pitch_bytes = 128ul;
    resolved->offset = 0x00200000ul;
}

/* A bound 800x600 Z16 surface. */
static void base_depth(V9X_R3D_DRAW *draw)
{
    draw->depth.offset = 0x00300000ul;
    draw->depth.pitch = 1600ul;
    draw->depth.width = 800ul;
    draw->depth.height = 600ul;
    draw->depth.object = &depth_object;
    draw->depth_enable = 1ul;
    draw->depth_write = 1ul;
    draw->depth_func = V9X_R3D_CMP_LESSEQUAL;
}

static v9x_u32 map(const V9X_R3D_DRAW *draw,
                   const V9X_D3D_RS690_TEXTURE *resolved, v9x_u32 depth_bytes,
                   struct v9x_rs690_3d_state *state)
{
    return v9x_d3d_rs690_map_draw(draw, resolved, FB_BASE, BASE, VRAM,
                                  depth_bytes, state);
}

static void test_target(void)
{
    V9X_R3D_DRAW draw;
    struct v9x_rs690_3d_state state;
    struct v9x_rs690_3d_list list;

    base_draw(&draw);
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_RS690_REFUSE_NONE);
    CHECK(state.target.fb_gpu_base == FB_BASE);
    CHECK(state.target.fb_bytes == BASE + VRAM);
    CHECK(state.target.offset == BASE + 0x00100000ul);
    CHECK(state.target.pitch_bytes == 1600ul);
    CHECK(state.target.bytes_per_pixel == 2ul);
    CHECK(state.target.width == 800ul);
    CHECK(state.target.height == 600ul);
    CHECK(state.textured == 0ul);
    CHECK(state.depth_enable == 0ul);
    CHECK(state.blend_enable == 0ul);
    CHECK(state.alpha_test_enable == 0ul);
    CHECK(state.cull == V9X_RS690_CULL_NONE);
    CHECK(v9x_rs690_build_3d_state(&state, &list) == V9X_STATUS_OK);

    draw.target.format = V9X_R3D_FORMAT_XRGB8888;
    draw.target.pitch = 3200ul;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_RS690_REFUSE_NONE);
    CHECK(state.target.bytes_per_pixel == 4ul);
    /* XRGB8888: the top byte is not alpha, and stays as the clear left it. */
    CHECK(state.x_channel == 1ul);
    CHECK(v9x_rs690_build_3d_state(&state, &list) == V9X_STATUS_OK);

    draw.target.format = V9X_R3D_FORMAT_XRGB1555;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_RS690_REFUSE_TARGET);

    /* Flat and Gouraud; specular added; fog with its colour's RGB. */
    CHECK(state.flat == 0ul);
    CHECK(state.specular == 0ul);
    base_draw(&draw);
    draw.shade_mode = V9X_R3D_SHADE_FLAT;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_RS690_REFUSE_NONE);
    CHECK(state.flat == 1ul);
    draw.shade_mode = 0ul;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_RS690_REFUSE_SHADE);
    base_draw(&draw);
    draw.specular_enable = 1ul;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_RS690_REFUSE_NONE);
    CHECK(state.specular == 1ul);
    CHECK(v9x_rs690_build_3d_state(&state, &list) == V9X_STATUS_OK);
    base_draw(&draw);
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_RS690_REFUSE_NONE);
    CHECK(state.fog == 0ul);
    draw.fog_enable = 1ul;
    draw.fog_color = 0xff336699ul;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_RS690_REFUSE_NONE);
    CHECK(state.fog == 1ul);
    CHECK(state.fog_color == 0x00336699ul);
    CHECK(v9x_rs690_build_3d_state(&state, &list) == V9X_STATUS_OK);

    /* The render interface's draws go to the CPU. */
    base_draw(&draw);
    draw.explicit_state = 1ul;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_RS690_REFUSE_EXPLICIT);
    base_draw(&draw);
    draw.texture.level_count = 1ul;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_RS690_REFUSE_EXPLICIT);
    base_draw(&draw);
    draw.texcoords1 = (const float *)&surface_object;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_RS690_REFUSE_EXPLICIT);
}

static void test_depth(void)
{
    static const v9x_u32 codes[8] = {
        V9X_RS690_ZFUNC_NEVER, V9X_RS690_ZFUNC_LESS, V9X_RS690_ZFUNC_EQUAL,
        V9X_RS690_ZFUNC_LEQUAL, V9X_RS690_ZFUNC_GREATER,
        V9X_RS690_ZFUNC_NOTEQUAL, V9X_RS690_ZFUNC_GEQUAL,
        V9X_RS690_ZFUNC_ALWAYS
    };
    V9X_R3D_DRAW draw;
    struct v9x_rs690_3d_state state;
    struct v9x_rs690_3d_list list;
    v9x_u32 func;

    base_draw(&draw);
    base_depth(&draw);
    CHECK(map(&draw, 0, 2ul, &state) == V9X_D3D_RS690_REFUSE_NONE);
    CHECK(state.depth_enable == 1ul);
    CHECK(state.depth_write == 1ul);
    CHECK(state.depth_func == V9X_RS690_ZFUNC_LEQUAL);
    CHECK(state.depth_offset == BASE + 0x00300000ul);
    CHECK(state.depth_pitch_bytes == 1600ul);
    CHECK(v9x_rs690_build_3d_state(&state, &list) == V9X_STATUS_OK);

    for (func = V9X_R3D_CMP_NEVER; func <= V9X_R3D_CMP_ALWAYS; ++func) {
        draw.depth_func = func;
        CHECK(map(&draw, 0, 2ul, &state) == V9X_D3D_RS690_REFUSE_NONE);
        CHECK(state.depth_func == codes[func - V9X_R3D_CMP_NEVER]);
    }
    draw.depth_func = 0ul;
    CHECK(map(&draw, 0, 2ul, &state) == V9X_D3D_RS690_REFUSE_DEPTH_FUNC);
    draw.depth_func = 9ul;
    CHECK(map(&draw, 0, 2ul, &state) == V9X_D3D_RS690_REFUSE_DEPTH_FUNC);

    base_draw(&draw);
    base_depth(&draw);
    draw.depth_write = 0ul;
    CHECK(map(&draw, 0, 2ul, &state) == V9X_D3D_RS690_REFUSE_NONE);
    CHECK(state.depth_write == 0ul);
    CHECK(map(&draw, 0, 4ul, &state) == V9X_D3D_RS690_REFUSE_DEPTH_SURFACE);

    /* Enabled with nothing bound, or bound and disabled: no depth. */
    draw.depth.object = 0;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_RS690_REFUSE_NONE);
    CHECK(state.depth_enable == 0ul);
    base_draw(&draw);
    base_depth(&draw);
    draw.depth_enable = 0ul;
    CHECK(map(&draw, 0, 2ul, &state) == V9X_D3D_RS690_REFUSE_NONE);
    CHECK(state.depth_enable == 0ul);
}

static void test_alpha_and_blend(void)
{
    V9X_R3D_DRAW draw;
    struct v9x_rs690_3d_state state;
    struct v9x_rs690_3d_list list;

    base_draw(&draw);
    draw.alpha_test_enable = 1ul;
    draw.alpha_func = V9X_R3D_CMP_GREATER;
    draw.alpha_ref = 0x80ul;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_RS690_REFUSE_NONE);
    CHECK(state.alpha_test_enable == 1ul);
    CHECK(state.alpha_func == V9X_RS690_AFUNC_GREATER);
    CHECK(state.alpha_ref == 0x80ul);
    draw.alpha_func = V9X_R3D_CMP_NEVER;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_RS690_REFUSE_NONE);
    CHECK(state.alpha_func == V9X_RS690_AFUNC_NEVER);
    draw.alpha_func = V9X_R3D_CMP_ALWAYS;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_RS690_REFUSE_NONE);
    CHECK(state.alpha_func == V9X_RS690_AFUNC_ALWAYS);
    draw.alpha_ref = 0x1ccul;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_RS690_REFUSE_NONE);
    CHECK(state.alpha_ref == 0xccul);
    draw.alpha_func = 9ul;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_RS690_REFUSE_ALPHA_FUNC);

    base_draw(&draw);
    draw.blend_enable = 1ul;
    draw.src_blend = V9X_R3D_BLEND_SRCALPHA;
    draw.dst_blend = V9X_R3D_BLEND_INVSRCALPHA;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_RS690_REFUSE_NONE);
    CHECK(state.blend_enable == 1ul);
    CHECK(state.blend_src == V9X_RS690_BLEND_SRC_ALPHA);
    CHECK(state.blend_dst == V9X_RS690_BLEND_INV_SRC_ALPHA);
    CHECK(v9x_rs690_build_3d_state(&state, &list) == V9X_STATUS_OK);

    draw.src_blend = V9X_R3D_BLEND_ONE;
    draw.dst_blend = V9X_R3D_BLEND_ZERO;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_RS690_REFUSE_NONE);
    CHECK(state.blend_src == V9X_RS690_BLEND_ONE);
    CHECK(state.blend_dst == V9X_RS690_BLEND_ZERO);
    draw.src_blend = V9X_R3D_BLEND_DESTCOLOR;
    draw.dst_blend = V9X_R3D_BLEND_SRCCOLOR;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_RS690_REFUSE_NONE);
    CHECK(state.blend_src == V9X_RS690_BLEND_DST_COLOR);
    CHECK(state.blend_dst == V9X_RS690_BLEND_SRC_COLOR);
    draw.src_blend = V9X_R3D_BLEND_INVDESTCOLOR;
    draw.dst_blend = V9X_R3D_BLEND_INVSRCCOLOR;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_RS690_REFUSE_NONE);
    CHECK(state.blend_src == V9X_RS690_BLEND_INV_DST_COLOR);
    CHECK(state.blend_dst == V9X_RS690_BLEND_INV_SRC_COLOR);
    draw.src_blend = V9X_R3D_BLEND_SRCALPHASAT;
    draw.dst_blend = V9X_R3D_BLEND_ONE;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_RS690_REFUSE_NONE);
    CHECK(state.blend_src == V9X_RS690_BLEND_SRC_ALPHA_SAT);

    /* No destination alpha on these targets; SRCALPHASAT only as source;
     * nothing unknown. */
    draw.src_blend = V9X_R3D_BLEND_DESTALPHA;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_RS690_REFUSE_BLEND);
    draw.src_blend = V9X_R3D_BLEND_ONE;
    draw.dst_blend = V9X_R3D_BLEND_INVDESTALPHA;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_RS690_REFUSE_BLEND);
    draw.dst_blend = V9X_R3D_BLEND_SRCALPHASAT;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_RS690_REFUSE_BLEND);
    draw.dst_blend = 12ul;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_RS690_REFUSE_BLEND);
    draw.dst_blend = 0ul;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_RS690_REFUSE_BLEND);
}

static void test_texture(void)
{
    V9X_R3D_DRAW draw;
    V9X_D3D_RS690_TEXTURE resolved;
    struct v9x_rs690_3d_state state;
    struct v9x_rs690_3d_list list;

    base_draw(&draw);
    base_texture(&draw, &resolved);
    CHECK(map(&draw, &resolved, 0ul, &state) == V9X_D3D_RS690_REFUSE_NONE);
    CHECK(state.textured == 1ul);
    CHECK(state.texture.offset == BASE + 0x00200000ul);
    CHECK(state.texture.pitch_bytes == 128ul);
    CHECK(state.texture.format == V9X_RS690_TEXFMT_RGB565);
    CHECK(state.texture.width == 64ul);
    CHECK(state.texture.height == 32ul);
    CHECK(state.texture.filter == V9X_RS690_TEXFILTER_NEAREST);
    CHECK(state.texture.address == V9X_RS690_TEXADDR_WRAP);
    /* DX5 MODULATE: colours multiplied, the vertex's alpha for a texture
     * without one, the texel's for one with. */
    CHECK(state.texture.combine == V9X_RS690_TEXCOMBINE_MODULATE);
    CHECK(state.texture_alpha == V9X_RS690_TEXALPHA_DIFFUSE);
    CHECK(v9x_rs690_build_3d_state(&state, &list) == V9X_STATUS_OK);
    resolved.has_alpha = 1;
    resolved.format = V9X_RS690_TEXFMT_ARGB4444;
    CHECK(map(&draw, &resolved, 0ul, &state) == V9X_D3D_RS690_REFUSE_NONE);
    CHECK(state.texture_alpha == V9X_RS690_TEXALPHA_TEXEL);
    CHECK(state.texture.format == V9X_RS690_TEXFMT_ARGB4444);

    draw.texture.op = V9X_R3D_TEXOP_MODULATEALPHA;
    CHECK(map(&draw, &resolved, 0ul, &state) == V9X_D3D_RS690_REFUSE_NONE);
    CHECK(state.texture.combine == V9X_RS690_TEXCOMBINE_MODULATE);
    CHECK(state.texture_alpha == V9X_RS690_TEXALPHA_MODULATE);
    draw.texture.op = V9X_R3D_TEXOP_DECAL;
    CHECK(map(&draw, &resolved, 0ul, &state) == V9X_D3D_RS690_REFUSE_NONE);
    CHECK(state.texture.combine == V9X_RS690_TEXCOMBINE_REPLACE);
    CHECK(state.texture_alpha == V9X_RS690_TEXALPHA_TEXEL);
    draw.texture.op = V9X_R3D_TEXOP_COPY;
    CHECK(map(&draw, &resolved, 0ul, &state) == V9X_D3D_RS690_REFUSE_NONE);
    CHECK(state.texture.combine == V9X_RS690_TEXCOMBINE_REPLACE);
    /* DECALALPHA and ADD keep the diffuse alpha; the mask forms are
     * refused. */
    draw.texture.op = V9X_R3D_TEXOP_DECALALPHA;
    CHECK(map(&draw, &resolved, 0ul, &state) == V9X_D3D_RS690_REFUSE_NONE);
    CHECK(state.texture.combine == V9X_RS690_TEXCOMBINE_DECALALPHA);
    CHECK(state.texture_alpha == V9X_RS690_TEXALPHA_DIFFUSE);
    CHECK(v9x_rs690_build_3d_state(&state, &list) == V9X_STATUS_OK);
    draw.texture.op = V9X_R3D_TEXOP_ADD;
    CHECK(map(&draw, &resolved, 0ul, &state) == V9X_D3D_RS690_REFUSE_NONE);
    CHECK(state.texture.combine == V9X_RS690_TEXCOMBINE_ADD);
    CHECK(state.texture_alpha == V9X_RS690_TEXALPHA_DIFFUSE);
    CHECK(v9x_rs690_build_3d_state(&state, &list) == V9X_STATUS_OK);
    draw.texture.op = V9X_R3D_TEXOP_DECALMASK;
    CHECK(map(&draw, &resolved, 0ul, &state) ==
          V9X_D3D_RS690_REFUSE_TEXTURE_OP);

    /* Addressing. */
    base_draw(&draw);
    base_texture(&draw, &resolved);
    draw.texture.address = V9X_R3D_ADDRESS_CLAMP;
    CHECK(map(&draw, &resolved, 0ul, &state) == V9X_D3D_RS690_REFUSE_NONE);
    CHECK(state.texture.address == V9X_RS690_TEXADDR_CLAMP);
    draw.texture.address = V9X_R3D_ADDRESS_MIRROR;
    CHECK(map(&draw, &resolved, 0ul, &state) ==
          V9X_D3D_RS690_REFUSE_TEXTURE_ADDRESS);
    draw.texture.address = V9X_R3D_ADDRESS_WRAP;
    draw.texture.wrap_u = 1ul;
    CHECK(map(&draw, &resolved, 0ul, &state) ==
          V9X_D3D_RS690_REFUSE_TEXTURE_ADDRESS);

    /* Filters: level 0's filter, the mip forms by the filter they use
     * within a level; minification and magnification must agree. */
    base_draw(&draw);
    base_texture(&draw, &resolved);
    draw.texture.min_filter = V9X_R3D_FILTER_MIPLINEAR;
    draw.texture.mag_filter = V9X_R3D_FILTER_LINEAR;
    CHECK(map(&draw, &resolved, 0ul, &state) == V9X_D3D_RS690_REFUSE_NONE);
    CHECK(state.texture.filter == V9X_RS690_TEXFILTER_LINEAR);
    draw.texture.min_filter = V9X_R3D_FILTER_LINEARMIPLINEAR;
    CHECK(map(&draw, &resolved, 0ul, &state) == V9X_D3D_RS690_REFUSE_NONE);
    CHECK(state.texture.filter == V9X_RS690_TEXFILTER_LINEAR);
    draw.texture.min_filter = V9X_R3D_FILTER_MIPNEAREST;
    draw.texture.mag_filter = V9X_R3D_FILTER_NEAREST;
    CHECK(map(&draw, &resolved, 0ul, &state) == V9X_D3D_RS690_REFUSE_NONE);
    CHECK(state.texture.filter == V9X_RS690_TEXFILTER_NEAREST);
    draw.texture.min_filter = V9X_R3D_FILTER_LINEARMIPNEAREST;
    CHECK(map(&draw, &resolved, 0ul, &state) == V9X_D3D_RS690_REFUSE_NONE);
    CHECK(state.texture.filter == V9X_RS690_TEXFILTER_NEAREST);
    draw.texture.mag_filter = V9X_R3D_FILTER_LINEAR;
    CHECK(map(&draw, &resolved, 0ul, &state) ==
          V9X_D3D_RS690_REFUSE_TEXTURE_FILTER);
    draw.texture.min_filter = 0ul;
    CHECK(map(&draw, &resolved, 0ul, &state) ==
          V9X_D3D_RS690_REFUSE_TEXTURE_FILTER);

    /* A one-level texture under a mip filter samples its base alone. */
    base_draw(&draw);
    base_texture(&draw, &resolved);
    draw.texture.min_filter = V9X_R3D_FILTER_MIPNEAREST;
    CHECK(map(&draw, &resolved, 0ul, &state) == V9X_D3D_RS690_REFUSE_NONE);
    CHECK(state.texture.levels == 0ul);
    CHECK(state.texture.mip_filter == V9X_RS690_MIPFILTER_NONE);

    /* A placed chain: DX5's names say the filter within a level and then
     * between levels - MIPNEAREST nearest/nearest, MIPLINEAR linear/
     * nearest, LINEARMIPNEAREST nearest/linear, LINEARMIPLINEAR
     * linear/linear. The 64x32 base has seven levels. */
    resolved.levels = 7ul;
    resolved.pitch_bytes = 128ul;
    CHECK(map(&draw, &resolved, 0ul, &state) == V9X_D3D_RS690_REFUSE_NONE);
    CHECK(state.texture.levels == 7ul);
    CHECK(state.texture.mip_filter == V9X_RS690_MIPFILTER_NEAREST);
    CHECK(state.texture.filter == V9X_RS690_TEXFILTER_NEAREST);
    CHECK(v9x_rs690_build_3d_state(&state, &list) == V9X_STATUS_OK);
    draw.texture.min_filter = V9X_R3D_FILTER_LINEARMIPNEAREST;
    CHECK(map(&draw, &resolved, 0ul, &state) == V9X_D3D_RS690_REFUSE_NONE);
    CHECK(state.texture.mip_filter == V9X_RS690_MIPFILTER_LINEAR);
    CHECK(state.texture.filter == V9X_RS690_TEXFILTER_NEAREST);
    draw.texture.min_filter = V9X_R3D_FILTER_MIPLINEAR;
    draw.texture.mag_filter = V9X_R3D_FILTER_LINEAR;
    CHECK(map(&draw, &resolved, 0ul, &state) == V9X_D3D_RS690_REFUSE_NONE);
    CHECK(state.texture.mip_filter == V9X_RS690_MIPFILTER_NEAREST);
    CHECK(state.texture.filter == V9X_RS690_TEXFILTER_LINEAR);
    draw.texture.min_filter = V9X_R3D_FILTER_LINEARMIPLINEAR;
    CHECK(map(&draw, &resolved, 0ul, &state) == V9X_D3D_RS690_REFUSE_NONE);
    CHECK(state.texture.mip_filter == V9X_RS690_MIPFILTER_LINEAR);
    CHECK(state.texture.levels == 7ul);
    CHECK(v9x_rs690_build_3d_state(&state, &list) == V9X_STATUS_OK);

    /* The chain present but a plain filter: the base alone. */
    draw.texture.min_filter = V9X_R3D_FILTER_LINEAR;
    CHECK(map(&draw, &resolved, 0ul, &state) == V9X_D3D_RS690_REFUSE_NONE);
    CHECK(state.texture.levels == 0ul);
    CHECK(state.texture.mip_filter == V9X_RS690_MIPFILTER_NONE);

    /* Format, shape, colour key. */
    base_draw(&draw);
    base_texture(&draw, &resolved);
    resolved.format = V9X_D3D_RS690_TEXTURE_UNKNOWN;
    CHECK(map(&draw, &resolved, 0ul, &state) ==
          V9X_D3D_RS690_REFUSE_TEXTURE_FORMAT);
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_RS690_REFUSE_TEXTURE_FORMAT);
    base_texture(&draw, &resolved);
    resolved.width = 0ul;
    CHECK(map(&draw, &resolved, 0ul, &state) ==
          V9X_D3D_RS690_REFUSE_TEXTURE_SHAPE);
    base_texture(&draw, &resolved);
    resolved.height = 4096ul;
    CHECK(map(&draw, &resolved, 0ul, &state) ==
          V9X_D3D_RS690_REFUSE_TEXTURE_SHAPE);
    /* Colour key on an RGB565 texture with a source key: the texture
     * unit's chroma key, FORCE (kills the fragment), the key as stored
     * (16 bits). Measured, docs\decisions\2026-11-06-rs690-colour-key.md. */
    base_texture(&draw, &resolved);
    draw.color_key_enable = 1ul;
    resolved.has_key = 1;
    resolved.key = 0xffff0065ul;
    CHECK(map(&draw, &resolved, 0ul, &state) == V9X_D3D_RS690_REFUSE_NONE);
    CHECK(state.texture.chroma_mode == V9X_RS690_CHROMA_FORCE);
    CHECK(state.texture.chroma_key == 0x00000065ul);
    CHECK(v9x_rs690_build_3d_state(&state, &list) == V9X_STATUS_OK);
    {
        v9x_u32 index;
        v9x_u32 key = 0xfffffffful;

        for (index = 0ul; index < list.count; ++index) {
            if (list.offsets[index] == V9X_RS690_TX_CHROMA_KEY_0) {
                key = list.values[index];
            }
        }
        CHECK(key == 0x00000065ul);
    }

    /* Enabled, but the texture has no key: drawn unkeyed. */
    resolved.has_key = 0;
    CHECK(map(&draw, &resolved, 0ul, &state) == V9X_D3D_RS690_REFUSE_NONE);
    CHECK(state.texture.chroma_mode == V9X_RS690_CHROMA_DISABLE);

    /* A format with alpha: the unit compares the alpha bits too, which
     * Direct3D's colour key does not. Refused. */
    resolved.has_key = 1;
    resolved.has_alpha = 1;
    resolved.format = V9X_RS690_TEXFMT_ARGB8888;
    CHECK(map(&draw, &resolved, 0ul, &state) ==
          V9X_D3D_RS690_REFUSE_COLOR_KEY);

    /* A colour key with no texture bound means nothing. */
    base_draw(&draw);
    draw.color_key_enable = 1ul;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_RS690_REFUSE_NONE);
}

/* Everything at once builds. */
static void test_full(void)
{
    V9X_R3D_DRAW draw;
    V9X_D3D_RS690_TEXTURE resolved;
    struct v9x_rs690_3d_state state;
    struct v9x_rs690_3d_list list;

    base_draw(&draw);
    base_texture(&draw, &resolved);
    base_depth(&draw);
    draw.blend_enable = 1ul;
    draw.src_blend = V9X_R3D_BLEND_SRCALPHA;
    draw.dst_blend = V9X_R3D_BLEND_INVSRCALPHA;
    draw.alpha_test_enable = 1ul;
    draw.alpha_func = V9X_R3D_CMP_GREATEREQUAL;
    draw.alpha_ref = 8ul;
    CHECK(map(&draw, &resolved, 2ul, &state) == V9X_D3D_RS690_REFUSE_NONE);
    CHECK(v9x_rs690_build_3d_state(&state, &list) == V9X_STATUS_OK);
    CHECK(list.count <= V9X_RS690_3D_WRITES);
}

unsigned int v9x_run_d3d_rs690_map_tests(void)
{
    failures = 0u;
    test_target();
    test_depth();
    test_alpha_and_blend();
    test_texture();
    test_full();
    return failures;
}
