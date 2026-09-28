#include "d3d_mach64_map.h"
#include "d3d_state.h"

#define M64_MAP_DEPTH_BITS   16ul
#define M64_MAP_WRITE_RGB    7ul
#define M64_MAP_SPECULAR_RGB 0x00fffffful

/* The filter within a level: MIPNEAREST and MIPLINEAR sample nearest,
 * LINEARMIPNEAREST and LINEARMIPLINEAR bilinear. */
static v9x_u32 v9x_d3d_mach64_base_filter(v9x_u32 filter)
{
    switch (filter) {
    case V9X_R3D_FILTER_MIPNEAREST:
    case V9X_R3D_FILTER_MIPLINEAR:
        return V9X_R3D_FILTER_NEAREST;
    case V9X_R3D_FILTER_LINEARMIPNEAREST:
    case V9X_R3D_FILTER_LINEARMIPLINEAR:
        return V9X_R3D_FILTER_LINEAR;
    default:
        return filter;
    }
}

static int v9x_d3d_mach64_selects_level(v9x_u32 filter)
{
    return filter == V9X_R3D_FILTER_MIPNEAREST ||
           filter == V9X_R3D_FILTER_LINEARMIPNEAREST;
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
