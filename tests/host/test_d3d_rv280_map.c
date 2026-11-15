#include <stdio.h>
#include <string.h>

#include "../../src/display32/d3d/d3d_rv280_map.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

/* M-HNBJNV's frame buffer at GPU address 0 is the probe's; a non-zero base
 * and a desktop offset are kept here so the offset arithmetic is exercised. */
#define FB_BASE 0x10000000ul
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

/* A bound 64x32 RGB565 texture, rows of 128 bytes. */
static void base_texture(V9X_R3D_DRAW *draw, V9X_D3D_RV280_TEXTURE *resolved)
{
    draw->texture.object = &texture_object;
    memset(resolved, 0, sizeof(*resolved));
    resolved->bytes_per_pixel = 2ul;
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
                   const V9X_D3D_RV280_TEXTURE *resolved, v9x_u32 depth_bytes,
                   V9X_D3D_RV280_STATE *state)
{
    return v9x_d3d_rv280_map_draw(draw, resolved, FB_BASE, BASE, VRAM,
                                  depth_bytes, state);
}

/* The builder accepts what the map produced. */
static int builds(const V9X_D3D_RV280_STATE *state)
{
    struct v9x_rv280_3d_list list;

    if (state->textured != 0ul) {
        return v9x_rv280_build_3d_textured_target(
                   &state->target, (int)state->gouraud, &state->texture,
                   state->combine, &list) == V9X_STATUS_OK;
    }
    return v9x_rv280_build_3d_target(&state->target, (int)state->gouraud,
                                     &list) == V9X_STATUS_OK;
}

static void test_target(void)
{
    V9X_R3D_DRAW draw;
    V9X_D3D_RV280_STATE state;

    base_draw(&draw);
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_RV280_REFUSE_NONE);
    CHECK(state.target.fb_gpu_base == FB_BASE);
    CHECK(state.target.fb_bytes == BASE + VRAM);
    CHECK(state.target.offset == BASE + 0x00100000ul);
    CHECK(state.target.pitch_bytes == 1600ul);
    CHECK(state.target.bytes_per_pixel == 2ul);
    CHECK(state.target.width == 800ul);
    CHECK(state.target.height == 600ul);
    CHECK(state.target.raster == &state.raster);
    CHECK(state.gouraud == 1ul);
    CHECK(state.textured == 0ul);
    CHECK(state.raster.z_enable == 0ul);
    CHECK(state.raster.blend_enable == 0ul);
    CHECK(state.raster.alpha_test_enable == 0ul);
    CHECK(state.raster.keep_x == 0ul);
    /* The core culls before the engine sees a triangle. */
    CHECK(state.raster.cull_front == 0ul);
    CHECK(state.raster.cull_back == 0ul);
    /* Every D3DTLVERTEX field but S and T is sent untextured. */
    CHECK(state.layout == (V9X_RV280_LAYOUT_Z | V9X_RV280_LAYOUT_W |
                           V9X_RV280_LAYOUT_SPECULAR));
    CHECK(state.target.layout == state.layout);
    CHECK(builds(&state));

    draw.target.format = V9X_R3D_FORMAT_XRGB8888;
    draw.target.pitch = 3200ul;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_RV280_REFUSE_NONE);
    CHECK(state.target.bytes_per_pixel == 4ul);
    CHECK(state.raster.keep_x == 1ul);
    CHECK(builds(&state));

    draw.target.format = V9X_R3D_FORMAT_XRGB1555;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_RV280_REFUSE_TARGET);

    base_draw(&draw);
    draw.shade_mode = V9X_R3D_SHADE_FLAT;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_RV280_REFUSE_NONE);
    CHECK(state.gouraud == 0ul);
    CHECK(builds(&state));
    draw.shade_mode = 0ul;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_RV280_REFUSE_SHADE);

    base_draw(&draw);
    draw.specular_enable = 1ul;
    draw.fog_enable = 1ul;
    draw.fog_color = 0xff123456ul;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_RV280_REFUSE_NONE);
    CHECK(state.raster.specular_enable == 1ul);
    CHECK(state.raster.fog_enable == 1ul);
    CHECK(state.raster.fog_colour == 0x00123456ul);
    CHECK(builds(&state));
}

static void test_refusals(void)
{
    V9X_R3D_DRAW draw;
    V9X_D3D_RV280_STATE state;
    int other;

    /* The render interface's explicit draws go to its CPU fallback until
     * scissor and write mask are built here. */
    base_draw(&draw);
    draw.explicit_state = 1ul;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_RV280_REFUSE_EXPLICIT);
    base_draw(&draw);
    draw.texture.level_count = 1ul;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_RV280_REFUSE_EXPLICIT);
    base_draw(&draw);
    draw.texture1.object = &other;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_RV280_REFUSE_EXPLICIT);
}

static void test_depth(void)
{
    V9X_R3D_DRAW draw;
    V9X_D3D_RV280_STATE state;
    static const v9x_u32 expect[8] = { 0ul, 1ul, 3ul, 2ul, 5ul, 6ul, 4ul,
                                       7ul };
    v9x_u32 func;

    base_draw(&draw);
    base_depth(&draw);
    CHECK(map(&draw, 0, 2ul, &state) == V9X_D3D_RV280_REFUSE_NONE);
    CHECK(state.raster.z_enable == 1ul);
    CHECK(state.raster.z_write == 1ul);
    CHECK(state.raster.z_func == 2ul);
    CHECK(state.raster.z_offset == BASE + 0x00300000ul);
    CHECK(state.raster.z_pitch_bytes == 1600ul);
    CHECK(builds(&state));

    /* NEVER..ALWAYS to Z_TEST's order: LEQUAL / EQUAL and GEQUAL / GREATER
     * trade places. */
    for (func = V9X_R3D_CMP_NEVER; func <= V9X_R3D_CMP_ALWAYS; ++func) {
        draw.depth_func = func;
        CHECK(map(&draw, 0, 2ul, &state) == V9X_D3D_RV280_REFUSE_NONE);
        CHECK(state.raster.z_func == expect[func - V9X_R3D_CMP_NEVER]);
    }
    draw.depth_func = 0ul;
    CHECK(map(&draw, 0, 2ul, &state) == V9X_D3D_RV280_REFUSE_DEPTH_FUNC);

    draw.depth_func = V9X_R3D_CMP_LESS;
    draw.depth_write = 0ul;
    CHECK(map(&draw, 0, 2ul, &state) == V9X_D3D_RV280_REFUSE_NONE);
    CHECK(state.raster.z_write == 0ul);

    /* Only Z16. */
    CHECK(map(&draw, 0, 4ul, &state) ==
          V9X_D3D_RV280_REFUSE_DEPTH_SURFACE);

    /* Enabled with no surface bound draws without Z. */
    draw.depth.object = 0;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_RV280_REFUSE_NONE);
    CHECK(state.raster.z_enable == 0ul);
}

static void test_alpha_and_blend(void)
{
    V9X_R3D_DRAW draw;
    V9X_D3D_RV280_STATE state;

    base_draw(&draw);
    draw.alpha_test_enable = 1ul;
    draw.alpha_func = V9X_R3D_CMP_GREATER;
    draw.alpha_ref = 0x180ul;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_RV280_REFUSE_NONE);
    CHECK(state.raster.alpha_test_enable == 1ul);
    CHECK(state.raster.alpha_func == 5ul);
    CHECK(state.raster.alpha_ref == 0x80ul);
    draw.alpha_func = V9X_R3D_CMP_LESSEQUAL;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_RV280_REFUSE_NONE);
    CHECK(state.raster.alpha_func == 2ul);
    CHECK(builds(&state));
    draw.alpha_func = 9ul;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_RV280_REFUSE_ALPHA_FUNC);

    base_draw(&draw);
    draw.blend_enable = 1ul;
    draw.src_blend = V9X_R3D_BLEND_SRCALPHA;
    draw.dst_blend = V9X_R3D_BLEND_INVSRCALPHA;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_RV280_REFUSE_NONE);
    CHECK(state.raster.blend_enable == 1ul);
    CHECK(state.raster.blend_src == 38ul);
    CHECK(state.raster.blend_dst == 39ul);
    CHECK(builds(&state));

    draw.src_blend = V9X_R3D_BLEND_ONE;
    draw.dst_blend = V9X_R3D_BLEND_ZERO;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_RV280_REFUSE_NONE);
    CHECK(state.raster.blend_src == 33ul);
    CHECK(state.raster.blend_dst == 32ul);
    draw.src_blend = V9X_R3D_BLEND_DESTCOLOR;
    draw.dst_blend = V9X_R3D_BLEND_INVSRCCOLOR;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_RV280_REFUSE_NONE);
    CHECK(state.raster.blend_src == 36ul);
    CHECK(state.raster.blend_dst == 35ul);
    draw.src_blend = V9X_R3D_BLEND_SRCCOLOR;
    draw.dst_blend = V9X_R3D_BLEND_INVDESTCOLOR;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_RV280_REFUSE_NONE);
    CHECK(state.raster.blend_src == 34ul);
    CHECK(state.raster.blend_dst == 37ul);

    /* No target here keeps destination alpha; SRCALPHASAT is not built. */
    draw.src_blend = V9X_R3D_BLEND_DESTALPHA;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_RV280_REFUSE_BLEND);
    draw.src_blend = V9X_R3D_BLEND_ONE;
    draw.dst_blend = V9X_R3D_BLEND_INVDESTALPHA;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_RV280_REFUSE_BLEND);
    draw.src_blend = V9X_R3D_BLEND_SRCALPHASAT;
    draw.dst_blend = V9X_R3D_BLEND_ONE;
    CHECK(map(&draw, 0, 0ul, &state) == V9X_D3D_RV280_REFUSE_BLEND);
}

static void test_texture(void)
{
    V9X_R3D_DRAW draw;
    V9X_D3D_RV280_STATE state;
    V9X_D3D_RV280_TEXTURE resolved;

    base_draw(&draw);
    base_texture(&draw, &resolved);
    CHECK(map(&draw, &resolved, 0ul, &state) == V9X_D3D_RV280_REFUSE_NONE);
    CHECK(state.textured == 1ul);
    CHECK(state.layout == (V9X_RV280_LAYOUT_Z | V9X_RV280_LAYOUT_W |
                           V9X_RV280_LAYOUT_SPECULAR | V9X_RV280_LAYOUT_ST));
    /* The textured builder adds S and T to the vertex itself. */
    CHECK(state.target.layout == (V9X_RV280_LAYOUT_Z | V9X_RV280_LAYOUT_W |
                                  V9X_RV280_LAYOUT_SPECULAR));
    CHECK(state.texture.fb_gpu_base == FB_BASE);
    CHECK(state.texture.fb_bytes == BASE + VRAM);
    CHECK(state.texture.offset == BASE + 0x00200000ul);
    CHECK(state.texture.pitch_bytes == 128ul);
    CHECK(state.texture.bytes_per_pixel == 2ul);
    CHECK(state.texture.width == 64ul);
    CHECK(state.texture.height == 32ul);
    CHECK(state.texture.linear == 0ul);
    CHECK(state.texture.clamp == 0ul);
    /* MODULATE on a texture without alpha keeps the vertex's. */
    CHECK(state.combine == V9X_RV280_COMBINE_MODULATE_DIFFUSE_ALPHA);
    CHECK(builds(&state));

    /* MODULATE on one with alpha takes the texel's. */
    resolved.bytes_per_pixel = 4ul;
    resolved.has_alpha = 1;
    resolved.pitch_bytes = 256ul;
    CHECK(map(&draw, &resolved, 0ul, &state) == V9X_D3D_RV280_REFUSE_NONE);
    CHECK(state.combine == V9X_RV280_COMBINE_MODULATE_TEXEL_ALPHA);
    CHECK(state.texture.bytes_per_pixel == 4ul);
    CHECK(builds(&state));
    /* A DX6 stage selecting diffuse alpha. */
    draw.texture.alpha_diffuse = 1ul;
    CHECK(map(&draw, &resolved, 0ul, &state) == V9X_D3D_RV280_REFUSE_NONE);
    CHECK(state.combine == V9X_RV280_COMBINE_MODULATE_DIFFUSE_ALPHA);
    draw.texture.op = V9X_R3D_TEXOP_DECAL;
    CHECK(map(&draw, &resolved, 0ul, &state) == V9X_D3D_RV280_REFUSE_NONE);
    CHECK(state.combine == V9X_RV280_COMBINE_DECAL_DIFFUSE_ALPHA);
    CHECK(builds(&state));
    draw.texture.alpha_diffuse = 0ul;
    CHECK(map(&draw, &resolved, 0ul, &state) == V9X_D3D_RV280_REFUSE_NONE);
    CHECK(state.combine == V9X_RV280_COMBINE_DECAL);
    draw.texture.op = V9X_R3D_TEXOP_COPY;
    CHECK(map(&draw, &resolved, 0ul, &state) == V9X_D3D_RV280_REFUSE_NONE);
    CHECK(state.combine == V9X_RV280_COMBINE_DECAL);
    draw.texture.op = V9X_R3D_TEXOP_MODULATEALPHA;
    CHECK(map(&draw, &resolved, 0ul, &state) == V9X_D3D_RV280_REFUSE_NONE);
    CHECK(state.combine == V9X_RV280_COMBINE_MODULATE);
    CHECK(builds(&state));
    draw.texture.op = V9X_R3D_TEXOP_DECALALPHA;
    CHECK(map(&draw, &resolved, 0ul, &state) ==
          V9X_D3D_RV280_REFUSE_TEXTURE_OP);
    draw.texture.op = V9X_R3D_TEXOP_ADD;
    CHECK(map(&draw, &resolved, 0ul, &state) ==
          V9X_D3D_RV280_REFUSE_TEXTURE_OP);

    /* ARGB1555 and ARGB4444 have alpha; XRGB8888 does not, so MODULATE
     * keeps the vertex's. The format reaches the builder. */
    base_texture(&draw, &resolved);
    draw.texture.op = V9X_R3D_TEXOP_MODULATE;
    resolved.format = V9X_RV280_TEXFMT_ARGB1555;
    resolved.has_alpha = 1;
    CHECK(map(&draw, &resolved, 0ul, &state) == V9X_D3D_RV280_REFUSE_NONE);
    CHECK(state.texture.format == V9X_RV280_TEXFMT_ARGB1555);
    CHECK(state.combine == V9X_RV280_COMBINE_MODULATE_TEXEL_ALPHA);
    CHECK(builds(&state));
    resolved.format = V9X_RV280_TEXFMT_XRGB8888;
    resolved.has_alpha = 0;
    resolved.bytes_per_pixel = 4ul;
    resolved.pitch_bytes = 256ul;
    CHECK(map(&draw, &resolved, 0ul, &state) == V9X_D3D_RV280_REFUSE_NONE);
    CHECK(state.texture.format == V9X_RV280_TEXFMT_XRGB8888);
    CHECK(state.combine == V9X_RV280_COMBINE_MODULATE_DIFFUSE_ALPHA);
    CHECK(builds(&state));

    /* Filters: one both ways; a mip filter is its level filter. */
    base_draw(&draw);
    base_texture(&draw, &resolved);
    draw.texture.min_filter = V9X_R3D_FILTER_LINEAR;
    draw.texture.mag_filter = V9X_R3D_FILTER_LINEAR;
    CHECK(map(&draw, &resolved, 0ul, &state) == V9X_D3D_RV280_REFUSE_NONE);
    CHECK(state.texture.linear == 1ul);
    draw.texture.min_filter = V9X_R3D_FILTER_LINEARMIPLINEAR;
    CHECK(map(&draw, &resolved, 0ul, &state) == V9X_D3D_RV280_REFUSE_NONE);
    CHECK(state.texture.linear == 1ul);
    draw.texture.min_filter = V9X_R3D_FILTER_MIPNEAREST;
    CHECK(map(&draw, &resolved, 0ul, &state) ==
          V9X_D3D_RV280_REFUSE_TEXTURE_FILTER);
    draw.texture.mag_filter = V9X_R3D_FILTER_NEAREST;
    CHECK(map(&draw, &resolved, 0ul, &state) == V9X_D3D_RV280_REFUSE_NONE);
    CHECK(state.texture.linear == 0ul);
    draw.texture.mag_filter = 0ul;
    CHECK(map(&draw, &resolved, 0ul, &state) ==
          V9X_D3D_RV280_REFUSE_TEXTURE_FILTER);

    /* Addressing: wrap or clamp. */
    base_draw(&draw);
    base_texture(&draw, &resolved);
    draw.texture.address = V9X_R3D_ADDRESS_CLAMP;
    CHECK(map(&draw, &resolved, 0ul, &state) == V9X_D3D_RV280_REFUSE_NONE);
    CHECK(state.texture.clamp == 1ul);
    CHECK(builds(&state));
    draw.texture.address = V9X_R3D_ADDRESS_MIRROR;
    CHECK(map(&draw, &resolved, 0ul, &state) ==
          V9X_D3D_RV280_REFUSE_TEXTURE_ADDRESS);
    draw.texture.address = V9X_R3D_ADDRESS_WRAP;
    draw.texture.wrap_u = 1ul;
    CHECK(map(&draw, &resolved, 0ul, &state) ==
          V9X_D3D_RV280_REFUSE_TEXTURE_ADDRESS);

    /* Format and shape. */
    base_draw(&draw);
    base_texture(&draw, &resolved);
    CHECK(map(&draw, 0, 0ul, &state) ==
          V9X_D3D_RV280_REFUSE_TEXTURE_FORMAT);
    resolved.bytes_per_pixel = V9X_D3D_RV280_TEXTURE_UNKNOWN;
    CHECK(map(&draw, &resolved, 0ul, &state) ==
          V9X_D3D_RV280_REFUSE_TEXTURE_FORMAT);
    base_texture(&draw, &resolved);
    resolved.width = 48ul;
    CHECK(map(&draw, &resolved, 0ul, &state) ==
          V9X_D3D_RV280_REFUSE_TEXTURE_SHAPE);
    resolved.width = 4096ul;
    CHECK(map(&draw, &resolved, 0ul, &state) ==
          V9X_D3D_RV280_REFUSE_TEXTURE_SHAPE);
    resolved.width = 0ul;
    CHECK(map(&draw, &resolved, 0ul, &state) ==
          V9X_D3D_RV280_REFUSE_TEXTURE_SHAPE);
    /* An 8x8 RGB565 texture's 16-byte rows are padded to 32 bytes. */
    base_texture(&draw, &resolved);
    resolved.width = 8ul;
    resolved.height = 8ul;
    resolved.pitch_bytes = 32ul;
    CHECK(map(&draw, &resolved, 0ul, &state) == V9X_D3D_RV280_REFUSE_NONE);
    CHECK(builds(&state));

    /* A colour key on a keyed texture is not built; enabled with no key it
     * means nothing. */
    base_texture(&draw, &resolved);
    draw.color_key_enable = 1ul;
    CHECK(map(&draw, &resolved, 0ul, &state) == V9X_D3D_RV280_REFUSE_NONE);
    resolved.has_key = 1;
    CHECK(map(&draw, &resolved, 0ul, &state) ==
          V9X_D3D_RV280_REFUSE_COLOR_KEY);
}

/* Everything together, as a typical game draw, at 32 bpp. */
static void test_full(void)
{
    V9X_R3D_DRAW draw;
    V9X_D3D_RV280_STATE state;
    V9X_D3D_RV280_TEXTURE resolved;

    base_draw(&draw);
    draw.target.format = V9X_R3D_FORMAT_XRGB8888;
    draw.target.pitch = 3200ul;
    base_depth(&draw);
    base_texture(&draw, &resolved);
    draw.blend_enable = 1ul;
    draw.src_blend = V9X_R3D_BLEND_SRCALPHA;
    draw.dst_blend = V9X_R3D_BLEND_INVSRCALPHA;
    draw.alpha_test_enable = 1ul;
    draw.alpha_func = V9X_R3D_CMP_GREATER;
    draw.fog_enable = 1ul;
    draw.specular_enable = 1ul;
    CHECK(map(&draw, &resolved, 2ul, &state) == V9X_D3D_RV280_REFUSE_NONE);
    CHECK(builds(&state));
}

unsigned int v9x_run_d3d_rv280_map_tests(void)
{
    failures = 0u;
    test_target();
    test_refusals();
    test_depth();
    test_alpha_and_blend();
    test_texture();
    test_full();
    return failures;
}
