#include "d3d_mach64_map.h"
#include "d3d_state.h"

#define M64_MAP_DEPTH_BITS   16ul
#define M64_MAP_WRITE_RGB    7ul
#define M64_MAP_SPECULAR_RGB 0x00fffffful

/* The render interface's combine numbers (r3d_abi.h), which a two-unit
 * draw carries in color_op and alpha_op. */
#define M64_MAP_COLOROP_REPLACE    1ul
#define M64_MAP_COLOROP_MODULATE   2ul
#define M64_MAP_COLOROP_DECALALPHA 3ul
#define M64_MAP_ALPHAOP_FRAGMENT   0ul
#define M64_MAP_ALPHAOP_REPLACE    1ul
#define M64_MAP_ALPHAOP_MODULATE   2ul

/* The filter within a level. "LINEAR" after "MIP" is bilinear within the
 * level and "LINEAR" before it the blend between two; the Windows 98 DDK's
 * ViRGE HAL settles the reading (d3d_i9xx.c v9x_d3d_i9xx_filter). Until
 * 2026-09-29 this read it the other way, which drew LINEARMIPNEAREST
 * bilinear and refused MIPLINEAR - OpenGL's LINEAR_MIPMAP_NEAREST, Quake
 * 2's default. */
static v9x_u32 v9x_d3d_mach64_base_filter(v9x_u32 filter)
{
    switch (filter) {
    case V9X_R3D_FILTER_MIPNEAREST:
    case V9X_R3D_FILTER_LINEARMIPNEAREST:
        return V9X_R3D_FILTER_NEAREST;
    case V9X_R3D_FILTER_MIPLINEAR:
    case V9X_R3D_FILTER_LINEARMIPLINEAR:
        return V9X_R3D_FILTER_LINEAR;
    default:
        return filter;
    }
}

static int v9x_d3d_mach64_selects_level(v9x_u32 filter)
{
    return filter == V9X_R3D_FILTER_MIPNEAREST ||
           filter == V9X_R3D_FILTER_MIPLINEAR ||
           filter == V9X_R3D_FILTER_LINEARMIPLINEAR;
}

void v9x_d3d_mach64_map_request(const V9X_R3D_DRAW *draw,
                                const V9X_D3D_MACH64_TEXTURE *texture,
                                v9x_u32 specular_rgb,
                                struct v9x_m64_draw_request *request)
{
    static const struct v9x_m64_draw_request empty;

    *request = empty;
    if (draw == 0) {
        return;
    }

    request->target_format = draw->target.format;
    request->target_width = draw->target.width;
    request->target_height = draw->target.height;

    /* A Direct3D draw states no scissor or mask: the whole target, every
     * channel. The render interface states both (r3d.h). */
    if (draw->explicit_state != 0ul) {
        request->scissor_left = draw->scissor_left;
        request->scissor_top = draw->scissor_top;
        request->scissor_right = draw->scissor_right;
        request->scissor_bottom = draw->scissor_bottom;
        request->write_mask = draw->write_mask;
    } else {
        request->scissor_right = draw->target.width;
        request->scissor_bottom = draw->target.height;
        request->write_mask = M64_MAP_WRITE_RGB;
    }
    request->shade_mode = draw->shade_mode;

    request->depth_enable = v9x_d3d_state_depth_active(
        draw->depth_enable, draw->depth.object != 0 ? 1ul : 0ul,
        draw->depth.pitch);
    request->depth_bits = M64_MAP_DEPTH_BITS;
    request->depth_func = draw->depth_func;
    request->depth_write = draw->depth_write;

    /*
     * A CPU-resident texture (the ICD's levels) has no VRAM copy for the
     * sampler, so it arrives as an unknown format and is refused until an
     * upload path exists.
     */
    if (draw->texture.object != 0 || draw->texture.level_count != 0ul) {
        request->textured = 1ul;
        request->texture_format = V9X_D3D_MACH64_TEXTURE_UNKNOWN;
        if (texture != 0 && draw->texture.object != 0) {
            request->texture_format = texture->format;
            request->texture_width = texture->width;
            request->texture_height = texture->height;
            request->texture_levels = texture->levels;
        }
        request->texture_min_filter = draw->texture.min_filter;
        request->texture_mag_filter = draw->texture.mag_filter;
        /* A mip filter on one level is its base filter (Direct3D). */
        if (request->texture_levels <= 1ul) {
            request->texture_min_filter =
                v9x_d3d_mach64_base_filter(request->texture_min_filter);
        }
        request->texture_address = draw->texture.address;
        request->texture_wrap_u = draw->texture.wrap_u |
                                  draw->texture.wrap_either;
        request->texture_wrap_v = draw->texture.wrap_v |
                                  draw->texture.wrap_either;
        request->texture_op = draw->texture.op;
    }

    request->blend_enable = draw->blend_enable;
    request->src_blend = draw->src_blend;
    request->dst_blend = draw->dst_blend;
    request->alpha_test_enable = draw->alpha_test_enable;
    request->alpha_func = draw->alpha_func;
    request->alpha_ref = draw->alpha_ref;
    request->fog_enable = draw->fog_enable;
    request->specular_enable =
        draw->specular_enable != 0ul && specular_rgb != 0ul ? 1ul : 0ul;
    request->color_key_enable = draw->color_key_enable;
    request->alpha_force = draw->alpha_force;
}

void v9x_d3d_mach64_map_state(const V9X_R3D_DRAW *draw,
                              const struct v9x_m64_draw_request *request,
                              const V9X_D3D_MACH64_TEXTURE *texture,
                              v9x_u32 vram_bytes,
                              struct v9x_m64_draw_state *state)
{
    static const struct v9x_m64_draw_state empty;

    *state = empty;
    if (draw == 0 || request == 0) {
        return;
    }

    state->color.vram_bytes = vram_bytes;
    state->color.target_offset = draw->target.offset;
    state->color.target_pitch_bytes = draw->target.pitch;
    state->color.target_width = draw->target.width;
    state->color.target_height = draw->target.height;
    state->color.scissor_left = request->scissor_left;
    state->color.scissor_top = request->scissor_top;
    state->color.scissor_right = request->scissor_right;
    state->color.scissor_bottom = request->scissor_bottom;

    if (request->depth_enable != 0ul) {
        state->depth_enable = 1ul;
        state->depth_offset = draw->depth.offset;
        state->depth_pitch_bytes = draw->depth.pitch;
        state->depth_compare = request->depth_func;
        state->depth_write = request->depth_write != 0ul ? 1ul : 0ul;
    }

    if (request->textured != 0ul && texture != 0) {
        state->textured = 1ul;
        state->texture_offset = texture->offset;
        state->texture_pitch_bytes = texture->pitch_bytes;
        state->texture_width = texture->width;
        state->texture_height = texture->height;
        state->texture_format = texture->format;
        state->wrap_s = request->texture_address == V9X_R3D_ADDRESS_WRAP
            ? 1ul : 0ul;
        state->wrap_t = state->wrap_s;
        state->bilinear_min =
            v9x_d3d_mach64_base_filter(request->texture_min_filter) ==
                V9X_R3D_FILTER_LINEAR ? 1ul : 0ul;
        state->bilinear_mag =
            request->texture_mag_filter == V9X_R3D_FILTER_LINEAR ? 1ul : 0ul;
        /* A chain drawn with a filter that selects no level samples level
         * 0 alone, with MIP_MAP_DISABLE. */
        state->level_count = 1ul;
        if (request->texture_levels > 1ul &&
            v9x_d3d_mach64_selects_level(request->texture_min_filter)) {
            v9x_u32 level;

            state->level_count = request->texture_levels;
            for (level = 0ul; level < state->level_count &&
                              level < V9X_M64_TEXTURE_LEVELS_MAX; ++level) {
                state->level_offsets[level] = texture->level_offsets[level];
            }
            /* Bilinear within two levels and blended between: trilinear. */
            if (request->texture_min_filter ==
                    V9X_R3D_FILTER_LINEARMIPLINEAR) {
                state->bilinear_min = 2ul;
            }
        }
    }

    if (request->blend_enable != 0ul) {
        state->blend_enable = 1ul;
        state->src_blend = request->src_blend;
        state->dst_blend = request->dst_blend;
    }
    if (request->alpha_test_enable != 0ul) {
        state->alpha_test_enable = 1ul;
        state->alpha_compare = request->alpha_func;
        state->alpha_reference = request->alpha_ref;
    }
    if (request->fog_enable != 0ul) {
        state->fog_enable = 1ul;
        state->fog_color = draw->fog_color;
    }
    state->specular_enable = request->specular_enable != 0ul ? 1ul : 0ul;
}

/*
 * Unit 0's combine as the Direct3D op the policy reads. A two-unit draw
 * carries only color_op and alpha_op (d3d_core.c v9x_r3d_describe_texture,
 * which leaves `op` zero there), so they are read as v9x_r3d_texture_op
 * reads a one-unit draw's: MODULATE takes the texel's alpha when the
 * texture has one and the fragment's when it does not. Zero is no op the
 * policy accepts.
 */
static v9x_u32 v9x_d3d_mach64_unit0_op(const V9X_R3D_DRAW *draw,
                                       v9x_u32 has_alpha)
{
    if (draw->texture.color_op == M64_MAP_COLOROP_REPLACE &&
        draw->texture.alpha_op == M64_MAP_ALPHAOP_REPLACE) {
        return V9X_R3D_TEXOP_DECAL;
    }
    if (draw->texture.color_op == M64_MAP_COLOROP_MODULATE) {
        if (draw->texture.alpha_op == M64_MAP_ALPHAOP_MODULATE) {
            return V9X_R3D_TEXOP_MODULATEALPHA;
        }
        if (draw->texture.alpha_op == (has_alpha != 0ul
                                           ? M64_MAP_ALPHAOP_REPLACE
                                           : M64_MAP_ALPHAOP_FRAGMENT)) {
            return V9X_R3D_TEXOP_MODULATE;
        }
        return 0ul;
    }
    if (draw->texture.color_op == M64_MAP_COLOROP_DECALALPHA &&
        draw->texture.alpha_op == M64_MAP_ALPHAOP_FRAGMENT) {
        return V9X_R3D_TEXOP_DECALALPHA;
    }
    return 0ul;
}

void v9x_d3d_mach64_map_composite(const V9X_R3D_DRAW *draw,
                                  const V9X_D3D_MACH64_TEXTURE *texture1,
                                  struct v9x_m64_draw_request *request)
{
    if (draw == 0 || request == 0 || draw->texcoords1 == 0) {
        return;
    }
    request->texture_op = v9x_d3d_mach64_unit0_op(draw,
        request->texture_format != V9X_M64_TEXTURE_FORMAT_RGB565 ? 1ul : 0ul);
    request->composite = 1ul;
    request->composite_format = V9X_D3D_MACH64_TEXTURE_UNKNOWN;
    if (texture1 != 0 && draw->texture1.object != 0) {
        request->composite_format = texture1->format;
        request->composite_width = texture1->width;
        request->composite_height = texture1->height;
    }
    request->composite_min_filter = draw->texture1.min_filter;
    request->composite_mag_filter = draw->texture1.mag_filter;
    request->composite_address = draw->texture1.address;
    request->composite_color_op = draw->texture1.color_op;
    request->composite_alpha_op = draw->texture1.alpha_op;
}

void v9x_d3d_mach64_map_composite_state(
                              const struct v9x_m64_draw_request *request,
                              const V9X_D3D_MACH64_TEXTURE *texture1,
                              struct v9x_m64_draw_state *state)
{
    if (request == 0 || texture1 == 0 || state == 0 ||
        request->composite == 0ul || state->textured == 0ul) {
        return;
    }
    state->level_count = 1ul;
    if (state->bilinear_min > 1ul) {
        state->bilinear_min = 1ul;
    }
    state->composite = 1ul;
    state->composite_offset = texture1->offset;
    state->composite_pitch_bytes = texture1->pitch_bytes;
    state->composite_width = texture1->width;
    state->composite_height = texture1->height;
    state->composite_format = texture1->format;
    state->composite_wrap_s =
        request->composite_address == V9X_R3D_ADDRESS_WRAP ? 1ul : 0ul;
    state->composite_wrap_t = state->composite_wrap_s;
    state->composite_bilinear_min =
        v9x_d3d_mach64_base_filter(request->composite_min_filter) ==
            V9X_R3D_FILTER_LINEAR ? 1ul : 0ul;
    state->composite_bilinear_mag =
        request->composite_mag_filter == V9X_R3D_FILTER_LINEAR ? 1ul : 0ul;
}

v9x_u32 v9x_d3d_mach64_specular_rgb(const V9X_R3D_VERTEX *vertices,
                                    v9x_u32 vertex_count)
{
    v9x_u32 index;

    if (vertices == 0) {
        return 0ul;
    }
    for (index = 0ul; index < vertex_count; ++index) {
        if ((vertices[index].specular & M64_MAP_SPECULAR_RGB) != 0ul) {
            return 1ul;
        }
    }
    return 0ul;
}

v9x_u32 v9x_d3d_mach64_vertices_opaque(const V9X_R3D_VERTEX *vertices,
                                       v9x_u32 vertex_count)
{
    v9x_u32 index;

    if (vertices == 0 || vertex_count == 0ul) {
        return 0ul;
    }
    for (index = 0ul; index < vertex_count; ++index) {
        if ((vertices[index].color & 0xFF000000ul) != 0xFF000000ul) {
            return 0ul;
        }
    }
    return 1ul;
}

v9x_u32 v9x_d3d_mach64_vertices_alpha_min(const V9X_R3D_VERTEX *vertices,
                                          v9x_u32 vertex_count)
{
    v9x_u32 index;
    v9x_u32 least = 0xfful;

    if (vertices == 0 || vertex_count == 0ul) {
        return 0ul;
    }
    for (index = 0ul; index < vertex_count; ++index) {
        v9x_u32 alpha = (vertices[index].color >> 24) & 0xfful;

        if (alpha < least) {
            least = alpha;
        }
    }
    return least;
}

int v9x_d3d_mach64_wrap_reference(const struct v9x_m64_setup_vertex *setup,
                                  float *s_out, float *t_out)
{
    float weight = 0.0f;
    float s = 0.0f;
    float t = 0.0f;
    v9x_u32 corner;

    if (setup == 0 || s_out == 0 || t_out == 0) {
        return 0;
    }
    for (corner = 0ul; corner < 3ul; ++corner) {
        weight += setup[corner].rhw;
        s += setup[corner].s * setup[corner].rhw;
        t += setup[corner].t * setup[corner].rhw;
    }
    if (!(weight > 0.0f)) {
        return 0;
    }
    *s_out = s / weight;
    *t_out = t / weight;
    return 1;
}
