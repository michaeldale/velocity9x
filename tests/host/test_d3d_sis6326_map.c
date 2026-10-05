#include <stdio.h>
#include <string.h>

#include "../../src/display32/d3d/d3d_sis6326_map.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

static int surface_token;

static v9x_u32 bits(float value)
{
    union {
        float value;
        v9x_u32 bits;
    } converted;

    converted.value = value;
    return converted.bits;
}

static float from_bits(v9x_u32 value)
{
    union {
        float value;
        v9x_u32 bits;
    } converted;

    converted.bits = value;
    return converted.value;
}

/* A Direct3D draw into an 800x600 RGB565 back buffer at 1 MiB. */
static void d3d_draw(V9X_R3D_DRAW *draw)
{
    memset(draw, 0, sizeof(*draw));
    draw->target.offset = 0x00100000ul;
    draw->target.pitch = 1600ul;
    draw->target.width = 800ul;
    draw->target.height = 600ul;
    draw->target.format = V9X_R3D_FORMAT_RGB565;
    draw->depth_func = V9X_R3D_CMP_LESSEQUAL;
    draw->alpha_func = V9X_R3D_CMP_ALWAYS;
    draw->src_blend = V9X_R3D_BLEND_ONE;
    draw->dst_blend = V9X_R3D_BLEND_ZERO;
    draw->shade_mode = V9X_R3D_SHADE_GOURAUD;
    draw->texture.min_filter = V9X_R3D_FILTER_NEAREST;
    draw->texture.mag_filter = V9X_R3D_FILTER_NEAREST;
    draw->texture.op = V9X_R3D_TEXOP_MODULATE;
    draw->texture.address = V9X_R3D_ADDRESS_WRAP;
}

/* A 64x64 ARGB4444 chain of seven levels at tight pitches. */
static void chain_4444(V9X_D3D_SIS_TEXTURE *texture)
{
    v9x_u32 level;

    memset(texture, 0, sizeof(*texture));
    texture->format = V9X_SIS3D_TEXEL_ARGB4444;
    texture->has_alpha = 1;
    texture->width = 64ul;
    texture->height = 64ul;
    texture->levels = 7ul;
    texture->offset = 0x00300000ul;
    texture->pitch_bytes = 128ul;
    for (level = 0ul; level < 7ul; ++level) {
        texture->level_offsets[level] = 0x00300000ul + level * 0x2000ul;
    }
}

static v9x_u32 map(const V9X_R3D_DRAW *draw,
                   const V9X_D3D_SIS_TEXTURE *resolved,
                   struct v9x_sis3d_state *state,
                   struct v9x_sis3d_texture *texture, int *textured)
{
    return v9x_d3d_sis_map_draw(draw, resolved, 4194304ul, 0ul, state,
                                texture, textured);
}

static void test_untextured_depth_blend_alpha(void)
{
    V9X_R3D_DRAW draw;
    struct v9x_sis3d_state state;
    struct v9x_sis3d_texture texture;
    struct v9x_sis3d_writes writes;
    int textured = 1;

    d3d_draw(&draw);
    CHECK(map(&draw, 0, &state, &texture, &textured) ==
          V9X_D3D_SIS_REFUSE_NONE);
    CHECK(textured == 0);
    /* Texturing stays on: an untextured batch before a textured one stalls
     * the engine (SIS3D /phase4b, A8U4I5 boots 219-227), a Cpix one does
     * not. Colour and alpha from the vertex, a 1x1 RGB565 dummy at the
     * target's offset, perspective off so W is ignored. */
    CHECK(state.enable == (V9X_SIS3D_ENABLE_PRIM_SETUP |
                           V9X_SIS3D_ENABLE_TEXTURE |
                           V9X_SIS3D_ENABLE_TEXTURE_CACHE |
                           V9X_SIS3D_ENABLE_LARGE_CACHE |
                           V9X_SIS3D_ENABLE_BIT15));
    CHECK(texture.colour_mode == V9X_SIS3D_TBLEND_CPIX);
    CHECK(texture.alpha_mode == V9X_SIS3D_TBLEND_APIX);
    CHECK(texture.format == V9X_SIS3D_TEXEL_RGB565);
    CHECK(texture.log2_width == 0ul && texture.log2_height == 0ul);
    CHECK(texture.levels == 0ul);
    CHECK(texture.offset == 0x00100000ul);
    CHECK(texture.pitch_bytes == 4ul);
    CHECK(texture.filter == V9X_SIS3D_MIN_NEAREST);
    CHECK(v9x_sis3d_build_texture(&texture, &writes) == V9X_STATUS_OK);
    CHECK(state.target.offset == 0x00100000ul);
    CHECK(state.target.pitch_bytes == 1600ul);
    CHECK(state.blend_source == V9X_SIS3D_BLEND_ONE);
    CHECK(state.blend_destination == V9X_SIS3D_BLEND_ZERO);
    CHECK(state.alpha_compare == V9X_SIS3D_CMP_ALWAYS);
    CHECK(v9x_sis3d_build_state(&state, &writes) == V9X_STATUS_OK);

    /* Depth needs the state, a bound surface and a pitch; D3D's compare
     * numbers are the engine's plus one. */
    draw.depth_enable = 1ul;
    draw.depth_write = 1ul;
    CHECK(map(&draw, 0, &state, &texture, &textured) ==
          V9X_D3D_SIS_REFUSE_NONE);
    CHECK((state.enable & V9X_SIS3D_ENABLE_Z_TEST) == 0ul);
    draw.depth.object = &surface_token;
    draw.depth.offset = 0x00200000ul;
    draw.depth.pitch = 1600ul;
    draw.depth.width = 800ul;
    draw.depth.height = 600ul;
    CHECK(map(&draw, 0, &state, &texture, &textured) ==
          V9X_D3D_SIS_REFUSE_NONE);
    CHECK((state.enable & (V9X_SIS3D_ENABLE_Z_TEST |
                           V9X_SIS3D_ENABLE_Z_WRITE)) ==
          (V9X_SIS3D_ENABLE_Z_TEST | V9X_SIS3D_ENABLE_Z_WRITE));
    CHECK(state.z_compare == V9X_SIS3D_CMP_LEQUAL);
    CHECK(state.z_offset == 0x00200000ul);
    CHECK(state.z_pitch_bytes == 1600ul);
    CHECK(v9x_sis3d_build_state(&state, &writes) == V9X_STATUS_OK);
    draw.depth_write = 0ul;
    CHECK(map(&draw, 0, &state, &texture, &textured) ==
          V9X_D3D_SIS_REFUSE_NONE);
    CHECK((state.enable & V9X_SIS3D_ENABLE_Z_WRITE) == 0ul);
    draw.depth_func = 9ul;
    CHECK(map(&draw, 0, &state, &texture, &textured) ==
          V9X_D3D_SIS_REFUSE_DEPTH_FUNC);

    d3d_draw(&draw);
    draw.blend_enable = 1ul;
    draw.src_blend = V9X_R3D_BLEND_SRCALPHA;
    draw.dst_blend = V9X_R3D_BLEND_INVSRCALPHA;
    CHECK(map(&draw, 0, &state, &texture, &textured) ==
          V9X_D3D_SIS_REFUSE_NONE);
    CHECK((state.enable & V9X_SIS3D_ENABLE_BLEND) != 0ul);
    CHECK(state.blend_source == V9X_SIS3D_BLEND_SRC_ALPHA);
    CHECK(state.blend_destination == V9X_SIS3D_BLEND_INV_SRC_ALPHA);
    draw.src_blend = V9X_R3D_BLEND_ONE;
    draw.dst_blend = V9X_R3D_BLEND_ONE;
    CHECK(map(&draw, 0, &state, &texture, &textured) ==
          V9X_D3D_SIS_REFUSE_NONE);
    CHECK(state.blend_source == V9X_SIS3D_BLEND_ONE &&
          state.blend_destination == V9X_SIS3D_BLEND_ONE);
    /* Phase 2 measured ZERO, ONE, SRCALPHA and INVSRCALPHA; SIS3D /phase7
     * (A8U4I5 boot 293) the colour factors on the side the datasheet gives
     * them: DESTCOLOR and INVDESTCOLOR as source, SRCCOLOR and INVSRCCOLOR
     * as destination. Half-Life's 2x modulate is DESTCOLOR/SRCCOLOR. */
    draw.src_blend = V9X_R3D_BLEND_DESTCOLOR;
    draw.dst_blend = V9X_R3D_BLEND_SRCCOLOR;
    CHECK(map(&draw, 0, &state, &texture, &textured) ==
          V9X_D3D_SIS_REFUSE_NONE);
    CHECK(state.blend_source == V9X_SIS3D_BLEND_DST_COLOR);
    CHECK(state.blend_destination == V9X_SIS3D_BLEND_SRC_COLOR);
    draw.src_blend = V9X_R3D_BLEND_INVDESTCOLOR;
    draw.dst_blend = V9X_R3D_BLEND_INVSRCCOLOR;
    CHECK(map(&draw, 0, &state, &texture, &textured) ==
          V9X_D3D_SIS_REFUSE_NONE);
    CHECK(state.blend_source == V9X_SIS3D_BLEND_INV_DST_COLOR);
    CHECK(state.blend_destination == V9X_SIS3D_BLEND_INV_SRC_COLOR);
    /* The other side's colour factor has no engine code, nor do the
     * destination-alpha and saturate factors without an alpha buffer. */
    draw.src_blend = V9X_R3D_BLEND_SRCCOLOR;
    draw.dst_blend = V9X_R3D_BLEND_ZERO;
    CHECK(map(&draw, 0, &state, &texture, &textured) ==
          V9X_D3D_SIS_REFUSE_BLEND);
    draw.src_blend = V9X_R3D_BLEND_INVSRCCOLOR;
    CHECK(map(&draw, 0, &state, &texture, &textured) ==
          V9X_D3D_SIS_REFUSE_BLEND);
    draw.src_blend = V9X_R3D_BLEND_SRCALPHASAT;
    CHECK(map(&draw, 0, &state, &texture, &textured) ==
          V9X_D3D_SIS_REFUSE_BLEND);
    draw.src_blend = V9X_R3D_BLEND_ONE;
    draw.dst_blend = V9X_R3D_BLEND_DESTCOLOR;
    CHECK(map(&draw, 0, &state, &texture, &textured) ==
          V9X_D3D_SIS_REFUSE_BLEND);
    draw.dst_blend = V9X_R3D_BLEND_INVDESTCOLOR;
    CHECK(map(&draw, 0, &state, &texture, &textured) ==
          V9X_D3D_SIS_REFUSE_BLEND);
    draw.dst_blend = V9X_R3D_BLEND_DESTALPHA;
    CHECK(map(&draw, 0, &state, &texture, &textured) ==
          V9X_D3D_SIS_REFUSE_BLEND);
    draw.dst_blend = V9X_R3D_BLEND_ZERO;
    draw.src_blend = V9X_R3D_BLEND_SRCALPHA;
    CHECK(map(&draw, 0, &state, &texture, &textured) ==
          V9X_D3D_SIS_REFUSE_NONE);
    /* Blend off ignores the factors. */
    draw.blend_enable = 0ul;
    draw.src_blend = V9X_R3D_BLEND_SRCCOLOR;
    CHECK(map(&draw, 0, &state, &texture, &textured) ==
          V9X_D3D_SIS_REFUSE_NONE);
    CHECK(state.blend_source == V9X_SIS3D_BLEND_ONE &&
          state.blend_destination == V9X_SIS3D_BLEND_ZERO);

    d3d_draw(&draw);
    draw.alpha_test_enable = 1ul;
    draw.alpha_func = V9X_R3D_CMP_GREATER;
    draw.alpha_ref = 0x180ul;
    CHECK(map(&draw, 0, &state, &texture, &textured) ==
          V9X_D3D_SIS_REFUSE_NONE);
    CHECK((state.enable & V9X_SIS3D_ENABLE_ALPHA_TEST) != 0ul);
    CHECK(state.alpha_compare == V9X_SIS3D_CMP_GREATER);
    CHECK(state.alpha_reference == 0x80ul);
    draw.alpha_func = 0ul;
    CHECK(map(&draw, 0, &state, &texture, &textured) ==
          V9X_D3D_SIS_REFUSE_ALPHA_FUNC);
}

static void test_refusals(void)
{
    V9X_R3D_DRAW draw;
    struct v9x_sis3d_state state;
    struct v9x_sis3d_texture texture;
    int textured;

    d3d_draw(&draw);
    draw.target.format = V9X_R3D_FORMAT_XRGB1555;
    CHECK(map(&draw, 0, &state, &texture, &textured) ==
          V9X_D3D_SIS_REFUSE_TARGET);
    d3d_draw(&draw);
    draw.explicit_state = 1ul;
    CHECK(map(&draw, 0, &state, &texture, &textured) ==
          V9X_D3D_SIS_REFUSE_EXPLICIT);
    /* Vertex fog: the factor rides in each vertex's specular alpha, which
     * goes to the fog/specular register as it is; 8A20h takes the colour. */
    d3d_draw(&draw);
    draw.fog_enable = 1ul;
    draw.fog_color = 0xff0000fful;
    CHECK(map(&draw, 0, &state, &texture, &textured) ==
          V9X_D3D_SIS_REFUSE_NONE);
    CHECK((state.enable & V9X_SIS3D_ENABLE_FOG) != 0ul);
    CHECK(state.fog_color == 0x000000fful);
    /* Specular on only where some vertex carries specular colour. */
    d3d_draw(&draw);
    draw.specular_enable = 1ul;
    CHECK(map(&draw, 0, &state, &texture, &textured) ==
          V9X_D3D_SIS_REFUSE_NONE);
    CHECK((state.enable & V9X_SIS3D_ENABLE_SPECULAR) == 0ul);
    CHECK((state.enable & V9X_SIS3D_ENABLE_FOG) == 0ul);
    CHECK(v9x_d3d_sis_map_draw(&draw, 0, 4194304ul, 0x00010000ul, &state,
                               &texture, &textured) ==
          V9X_D3D_SIS_REFUSE_NONE);
    CHECK((state.enable & V9X_SIS3D_ENABLE_SPECULAR) != 0ul);
    d3d_draw(&draw);
    draw.shade_mode = 3ul;           /* Phong */
    CHECK(map(&draw, 0, &state, &texture, &textured) ==
          V9X_D3D_SIS_REFUSE_SHADE);
}

static void test_texture_mapping(void)
{
    V9X_R3D_DRAW draw;
    V9X_D3D_SIS_TEXTURE resolved;
    struct v9x_sis3d_state state;
    struct v9x_sis3d_texture texture;
    struct v9x_sis3d_writes writes;
    int textured = 0;

    d3d_draw(&draw);
    draw.texture.object = &surface_token;
    draw.texture.min_filter = V9X_R3D_FILTER_LINEARMIPLINEAR;
    draw.texture.mag_filter = V9X_R3D_FILTER_LINEAR;
    chain_4444(&resolved);
    CHECK(map(&draw, &resolved, &state, &texture, &textured) ==
          V9X_D3D_SIS_REFUSE_NONE);
    CHECK(textured == 1);
    CHECK(state.enable == (V9X_SIS3D_ENABLE_PRIM_SETUP |
                           V9X_SIS3D_ENABLE_TEXTURE |
                           V9X_SIS3D_ENABLE_TEXTURE_CACHE |
                           V9X_SIS3D_ENABLE_LARGE_CACHE |
                           V9X_SIS3D_ENABLE_BIT15 |
                           V9X_SIS3D_ENABLE_PERSPECTIVE));
    CHECK(texture.format == V9X_SIS3D_TEXEL_ARGB4444);
    CHECK(texture.log2_width == 6ul && texture.log2_height == 6ul);
    CHECK(texture.levels == 6ul);
    CHECK(texture.offset == 0x00300000ul);
    CHECK(texture.pitch_bytes == 128ul);
    CHECK(texture.level_offsets[0] == 0x00302000ul);
    CHECK(texture.level_offsets[5] == 0x0030c000ul);
    CHECK(texture.filter == (V9X_SIS3D_MAG_LINEAR | 5ul));
    CHECK(texture.mapping == (V9X_SIS3D_TEXTURE_WRAP_U |
                              V9X_SIS3D_TEXTURE_WRAP_V));
    CHECK(texture.colour_mode == 2ul);
    CHECK(texture.alpha_mode == V9X_SIS3D_TBLEND_ATEX);
    CHECK(texture.clear_cache == 0);
    CHECK(v9x_sis3d_build_texture(&texture, &writes) == V9X_STATUS_OK);

    /* MIPNEAREST 2, MIPLINEAR (bilinear in the nearest level) 3,
     * LINEARMIPNEAREST (nearest texel, levels blended) 4: run I measured
     * blending between levels under 4 and 5, none under 2 and 3. */
    draw.texture.min_filter = V9X_R3D_FILTER_MIPNEAREST;
    CHECK(map(&draw, &resolved, &state, &texture, &textured) ==
          V9X_D3D_SIS_REFUSE_NONE);
    CHECK((texture.filter & 7ul) == 2ul);
    draw.texture.min_filter = V9X_R3D_FILTER_MIPLINEAR;
    CHECK(map(&draw, &resolved, &state, &texture, &textured) ==
          V9X_D3D_SIS_REFUSE_NONE);
    CHECK((texture.filter & 7ul) == 3ul);
    draw.texture.min_filter = V9X_R3D_FILTER_LINEARMIPNEAREST;
    CHECK(map(&draw, &resolved, &state, &texture, &textured) ==
          V9X_D3D_SIS_REFUSE_NONE);
    CHECK((texture.filter & 7ul) == 4ul);
    /* A single level takes the mip filter's within-level half. */
    resolved.levels = 1ul;
    CHECK(map(&draw, &resolved, &state, &texture, &textured) ==
          V9X_D3D_SIS_REFUSE_NONE);
    CHECK(texture.filter == V9X_SIS3D_MAG_LINEAR);
    CHECK(texture.levels == 0ul);
    draw.texture.min_filter = V9X_R3D_FILTER_MIPLINEAR;
    CHECK(map(&draw, &resolved, &state, &texture, &textured) ==
          V9X_D3D_SIS_REFUSE_NONE);
    CHECK((texture.filter & 7ul) == V9X_SIS3D_MIN_LINEAR);
    /* A mip filter as the magnification filter takes its within-level half,
     * as d3d_i9xx.c folds it: Final Reality's full run had every one of its
     * 17,192 refusals on the filter (A8U4I5 boot 290). */
    draw.texture.mag_filter = V9X_R3D_FILTER_MIPNEAREST;
    CHECK(map(&draw, &resolved, &state, &texture, &textured) ==
          V9X_D3D_SIS_REFUSE_NONE);
    CHECK((texture.filter & V9X_SIS3D_MAG_LINEAR) == 0ul);
    draw.texture.mag_filter = V9X_R3D_FILTER_MIPLINEAR;
    CHECK(map(&draw, &resolved, &state, &texture, &textured) ==
          V9X_D3D_SIS_REFUSE_NONE);
    CHECK((texture.filter & V9X_SIS3D_MAG_LINEAR) != 0ul);
    draw.texture.mag_filter = V9X_R3D_FILTER_LINEARMIPNEAREST;
    CHECK(map(&draw, &resolved, &state, &texture, &textured) ==
          V9X_D3D_SIS_REFUSE_NONE);
    CHECK((texture.filter & V9X_SIS3D_MAG_LINEAR) == 0ul);
    draw.texture.mag_filter = V9X_R3D_FILTER_LINEARMIPLINEAR;
    CHECK(map(&draw, &resolved, &state, &texture, &textured) ==
          V9X_D3D_SIS_REFUSE_NONE);
    CHECK((texture.filter & V9X_SIS3D_MAG_LINEAR) != 0ul);
    draw.texture.mag_filter = 0ul;
    CHECK(map(&draw, &resolved, &state, &texture, &textured) ==
          V9X_D3D_SIS_REFUSE_TEXTURE_FILTER);

    /* The texture-blend modes measured on 2026-10-05; alpha per D3D. */
    d3d_draw(&draw);
    draw.texture.object = &surface_token;
    chain_4444(&resolved);
    resolved.levels = 1ul;
    draw.texture.op = V9X_R3D_TEXOP_DECAL;
    CHECK(map(&draw, &resolved, &state, &texture, &textured) ==
          V9X_D3D_SIS_REFUSE_NONE);
    CHECK(texture.colour_mode == 0ul &&
          texture.alpha_mode == V9X_SIS3D_TBLEND_ATEX);
    draw.texture.op = V9X_R3D_TEXOP_COPY;
    CHECK(map(&draw, &resolved, &state, &texture, &textured) ==
          V9X_D3D_SIS_REFUSE_NONE);
    CHECK(texture.colour_mode == 0ul);
    draw.texture.op = V9X_R3D_TEXOP_DECALALPHA;
    CHECK(map(&draw, &resolved, &state, &texture, &textured) ==
          V9X_D3D_SIS_REFUSE_NONE);
    CHECK(texture.colour_mode == 4ul &&
          texture.alpha_mode == V9X_SIS3D_TBLEND_APIX);
    draw.texture.op = V9X_R3D_TEXOP_MODULATEALPHA;
    CHECK(map(&draw, &resolved, &state, &texture, &textured) ==
          V9X_D3D_SIS_REFUSE_NONE);
    CHECK(texture.colour_mode == 2ul &&
          texture.alpha_mode == V9X_SIS3D_TBLEND_APIX_ATEX);
    draw.texture.op = V9X_R3D_TEXOP_DECALMASK;
    CHECK(map(&draw, &resolved, &state, &texture, &textured) ==
          V9X_D3D_SIS_REFUSE_NONE);
    CHECK(texture.colour_mode == 8ul && texture.blend_mask_bit == 7ul &&
          texture.alpha_mode == V9X_SIS3D_TBLEND_APIX);
    draw.texture.op = V9X_R3D_TEXOP_MODULATEMASK;
    CHECK(map(&draw, &resolved, &state, &texture, &textured) ==
          V9X_D3D_SIS_REFUSE_NONE);
    CHECK(texture.colour_mode == 12ul && texture.blend_mask_bit == 7ul);
    draw.texture.op = V9X_R3D_TEXOP_ADD;
    CHECK(map(&draw, &resolved, &state, &texture, &textured) ==
          V9X_D3D_SIS_REFUSE_TEXTURE_OP);
    /* MODULATE without texel alpha takes the vertex's. */
    draw.texture.op = V9X_R3D_TEXOP_MODULATE;
    resolved.format = V9X_SIS3D_TEXEL_RGB565;
    resolved.has_alpha = 0;
    CHECK(map(&draw, &resolved, &state, &texture, &textured) ==
          V9X_D3D_SIS_REFUSE_NONE);
    CHECK(texture.colour_mode == 2ul &&
          texture.alpha_mode == V9X_SIS3D_TBLEND_APIX);

    /* Addressing: mirror and clamp per axis pair; border and the
     * cylindrical WRAPU/WRAPV render states refuse. */
    draw.texture.address = V9X_R3D_ADDRESS_MIRROR;
    CHECK(map(&draw, &resolved, &state, &texture, &textured) ==
          V9X_D3D_SIS_REFUSE_NONE);
    CHECK(texture.mapping == (V9X_SIS3D_TEXTURE_MIRROR_U |
                              V9X_SIS3D_TEXTURE_MIRROR_V));
    draw.texture.address = V9X_R3D_ADDRESS_CLAMP;
    CHECK(map(&draw, &resolved, &state, &texture, &textured) ==
          V9X_D3D_SIS_REFUSE_NONE);
    CHECK(texture.mapping == (V9X_SIS3D_TEXTURE_CLAMP_U |
                              V9X_SIS3D_TEXTURE_CLAMP_V));
    draw.texture.address = V9X_R3D_ADDRESS_BORDER;
    CHECK(map(&draw, &resolved, &state, &texture, &textured) ==
          V9X_D3D_SIS_REFUSE_TEXTURE_ADDRESS);
    draw.texture.address = V9X_R3D_ADDRESS_WRAP;
    draw.texture.wrap_u = 1ul;
    CHECK(map(&draw, &resolved, &state, &texture, &textured) ==
          V9X_D3D_SIS_REFUSE_TEXTURE_ADDRESS);
    draw.texture.wrap_u = 0ul;
    /* wrap_either is the ViRGE's folded flag, which the core starts at 1
     * whatever the application set. Only the per-axis fields mean
     * WRAPU/WRAPV. */
    draw.texture.wrap_either = 1ul;
    CHECK(map(&draw, &resolved, &state, &texture, &textured) ==
          V9X_D3D_SIS_REFUSE_NONE);
    draw.texture.wrap_either = 0ul;

    /* What the sampler cannot take. */
    resolved.format = V9X_D3D_SIS_TEXTURE_UNKNOWN;
    CHECK(map(&draw, &resolved, &state, &texture, &textured) ==
          V9X_D3D_SIS_REFUSE_TEXTURE_FORMAT);
    CHECK(map(&draw, 0, &state, &texture, &textured) ==
          V9X_D3D_SIS_REFUSE_TEXTURE_FORMAT);
    resolved.format = V9X_SIS3D_TEXEL_RGB565;
    resolved.width = 48ul;
    CHECK(map(&draw, &resolved, &state, &texture, &textured) ==
          V9X_D3D_SIS_REFUSE_TEXTURE_SHAPE);
    resolved.width = 1024ul;
    CHECK(map(&draw, &resolved, &state, &texture, &textured) ==
          V9X_D3D_SIS_REFUSE_TEXTURE_SHAPE);
    d3d_draw(&draw);
    draw.texture.object = &surface_token;
    draw.color_key_enable = 1ul;
    chain_4444(&resolved);
    CHECK(map(&draw, &resolved, &state, &texture, &textured) ==
          V9X_D3D_SIS_REFUSE_COLOR_KEY);
}

static void vertex(V9X_R3D_VERTEX *v, float x, float y, float z,
                   v9x_u32 color)
{
    memset(v, 0, sizeof(*v));
    v->sx = x;
    v->sy = y;
    v->sz = z;
    v->rhw = 0.5f;
    v->color = color;
    v->specular = color ^ 0x00fffffful;
    v->tu = 0.25f;
    v->tv = 0.75f;
}

static void test_triangles(void)
{
    V9X_R3D_VERTEX tri[3];
    struct v9x_sis3d_vertex out[3];
    v9x_u32 primitive = 0ul;

    /* Middle vertex right of the long edge: direction 0. Phase 2's
     * Gouraud word, 00106602h. */
    vertex(&tri[0], 10.0f, 10.0f, 0.5f, 0xffff0000ul);
    vertex(&tri[1], 30.0f, 12.0f, 1.0f, 0xff00ff00ul);
    vertex(&tri[2], 12.0f, 30.0f, 2.0f, 0xff0000fful);
    CHECK(v9x_d3d_sis_triangle(tri, V9X_R3D_SHADE_GOURAUD, 0, out,
                               &primitive) == 1);
    CHECK(primitive == 0x00106602ul);
    /* Up and left by 1/256: the tie shift (2026-10-05). */
    CHECK(out[0].x == bits(10.0f - 0.00390625f));
    CHECK(out[1].y == bits(12.0f - 0.00390625f));
    /* Z16 wraps to zero at 1.0, so z stops below it. */
    CHECK(out[0].z == 0x3f000000ul);
    CHECK(out[1].z == 0x3f7ffffful);
    CHECK(out[2].z == 0x3f7ffffful);
    CHECK(out[1].argb == 0xff00ff00ul);
    CHECK(out[1].fog_specular == (0xff00ff00ul ^ 0x00fffffful));
    /* Untextured: W is 1.0, U and V zero. */
    CHECK(out[0].w == 0x3f800000ul);
    CHECK(out[0].u == 0ul);

    /* Textured: RHW and U/V unchanged. */
    CHECK(v9x_d3d_sis_triangle(tri, V9X_R3D_SHADE_GOURAUD, 1, out,
                               &primitive) == 1);
    CHECK(out[2].w == bits(0.5f));
    CHECK(out[2].u == bits(0.25f) && out[2].v == bits(0.75f));

    /* Flat: vertex 0's colours on all three, flat via top. */
    CHECK(v9x_d3d_sis_triangle(tri, V9X_R3D_SHADE_FLAT, 0, out,
                               &primitive) == 1);
    CHECK(primitive == 0x00046602ul);
    CHECK(out[2].argb == 0xffff0000ul);
    CHECK(out[1].fog_specular == (0xffff0000ul ^ 0x00fffffful));

    /* The mirror image: middle vertex left, direction 1. */
    vertex(&tri[0], 30.0f, 10.0f, 0.0f, 0xffffffff);
    vertex(&tri[1], 10.0f, 12.0f, 0.0f, 0xffffffff);
    vertex(&tri[2], 28.0f, 30.0f, -0.25f, 0xffffffff);
    CHECK(v9x_d3d_sis_triangle(tri, V9X_R3D_SHADE_GOURAUD, 0, out,
                               &primitive) == 1);
    CHECK((primitive & V9X_SIS3D_DIRECTION_BIT) != 0ul);
    CHECK(out[2].z == 0ul);

    /* Order by Y: vertex 2 on top. */
    vertex(&tri[0], 10.0f, 20.0f, 0.0f, 0xffffffff);
    vertex(&tri[1], 20.0f, 30.0f, 0.0f, 0xffffffff);
    vertex(&tri[2], 15.0f, 5.0f, 0.0f, 0xffffffff);
    CHECK(v9x_d3d_sis_triangle(tri, V9X_R3D_SHADE_GOURAUD, 0, out,
                               &primitive) == 1);
    CHECK(((primitive >> V9X_SIS3D_TOP_SHIFT) & 3ul) == 2ul);
    CHECK(((primitive >> V9X_SIS3D_MIDDLE_SHIFT) & 3ul) == 0ul);
    CHECK(((primitive >> V9X_SIS3D_BOTTOM_SHIFT) & 3ul) == 1ul);

    /* Nothing to draw. */
    vertex(&tri[0], 10.0f, 10.0f, 0.0f, 0xffffffff);
    vertex(&tri[1], 20.0f, 20.0f, 0.0f, 0xffffffff);
    vertex(&tri[2], 30.0f, 30.0f, 0.0f, 0xffffffff);
    CHECK(v9x_d3d_sis_triangle(tri, V9X_R3D_SHADE_GOURAUD, 0, out,
                               &primitive) == 0);
    vertex(&tri[2], 30.0f, 10.0f, 0.0f, 0xffffffff);
    tri[1].sx = from_bits(0x7f800000ul);     /* infinity */
    CHECK(v9x_d3d_sis_triangle(tri, V9X_R3D_SHADE_GOURAUD, 0, out,
                               &primitive) == 0);
    vertex(&tri[1], 20.0f, 20.0f, 0.0f, 0xffffffff);
    tri[1].rhw = 0.0f;
    CHECK(v9x_d3d_sis_triangle(tri, V9X_R3D_SHADE_GOURAUD, 0, out,
                               &primitive) == 1);
    CHECK(v9x_d3d_sis_triangle(tri, V9X_R3D_SHADE_GOURAUD, 1, out,
                               &primitive) == 0);
}

unsigned int v9x_run_d3d_sis6326_map_tests(void)
{
    test_untextured_depth_blend_alpha();
    test_refusals();
    test_texture_mapping();
    test_triangles();
    return failures;
}
