/*
 * The render interface's validators (r3d_validate.h). Pure arithmetic over
 * project-owned descriptors, so every refusal is a host test.
 */
#include "r3d_validate.h"

/* Bytes per pixel of every layout the interface names. */
#define V9X_R3D_VALIDATE_BPP 2ul

/*
 * Whether the bytes a surface or level occupies - from `offset` to the last
 * pixel of its last row - lie inside `limit`, without a product or a sum
 * that could wrap. The last row needs only `width` pixels, not a whole
 * pitch, which is what lets a padded pitch end exactly at the limit.
 * `width` is bounded by the caller before this is reached, so the row
 * itself cannot overflow.
 */
static int v9x_r3d_validate_extent(v9x_u32 offset, v9x_u32 pitch,
                                   v9x_u32 width, v9x_u32 height,
                                   v9x_u32 limit)
{
    v9x_u32 row = width * V9X_R3D_VALIDATE_BPP;
    v9x_u32 span = row;

    if (height == 0ul || row > limit) {
        return 0;
    }
    if (height > 1ul) {
        if (pitch > (limit - row) / (height - 1ul)) {
            return 0;
        }
        span = pitch * (height - 1ul) + row;
    }
    return offset <= limit - span;
}

static int v9x_r3d_validate_power_of_two(v9x_u32 value)
{
    return value != 0ul && (value & (value - 1ul)) == 0ul;
}

/* Common to targets and depth buffers: shape, pitch and extent. */
static v9x_u32 v9x_r3d_validate_layout(const V9X_R3D_SURFACE *surface,
                                       v9x_u32 vram_bytes,
                                       v9x_u32 dimension_max)
{
    if (surface->width == 0ul || surface->height == 0ul ||
        surface->width > dimension_max || surface->height > dimension_max) {
        return V9X_R3D_RESULT_INVALID;
    }
    /* An odd pitch would put every other row's pixels off their 16-bit
     * alignment; a narrow one would draw into the next row. */
    if ((surface->pitch & 1ul) != 0ul ||
        surface->pitch < surface->width * V9X_R3D_VALIDATE_BPP) {
        return V9X_R3D_RESULT_INVALID;
    }
    if (!v9x_r3d_validate_extent(surface->offset, surface->pitch,
                                 surface->width, surface->height,
                                 vram_bytes)) {
        return V9X_R3D_RESULT_INVALID;
    }
    return V9X_R3D_RESULT_OK;
}

v9x_u32 v9x_r3d_validate_surface(const V9X_R3D_SURFACE *surface,
                                 v9x_u32 vram_bytes,
                                 v9x_u32 dimension_max)
{
    if (surface == 0) {
        return V9X_R3D_RESULT_INVALID;
    }
    if (surface->format != V9X_R3D_FORMAT_RGB565 &&
        surface->format != V9X_R3D_FORMAT_XRGB1555) {
        return V9X_R3D_RESULT_INVALID;
    }
    return v9x_r3d_validate_layout(surface, vram_bytes, dimension_max);
}

v9x_u32 v9x_r3d_validate_depth(const V9X_R3D_SURFACE *depth,
                               const V9X_R3D_SURFACE *target,
                               v9x_u32 vram_bytes,
                               v9x_u32 dimension_max)
{
    if (depth == 0 || target == 0) {
        return V9X_R3D_RESULT_INVALID;
    }
    /* Every fragment the target can receive has a depth cell. */
    if (depth->width < target->width || depth->height < target->height) {
        return V9X_R3D_RESULT_INVALID;
    }
    return v9x_r3d_validate_layout(depth, vram_bytes, dimension_max);
}

/* Levels in a full chain from a top of width x height: the larger edge's
 * log2, plus one. */
static v9x_u32 v9x_r3d_validate_chain_length(v9x_u32 width, v9x_u32 height)
{
    v9x_u32 edge = width > height ? width : height;
    v9x_u32 levels = 1ul;

    while (edge > 1ul) {
        edge >>= 1;
        ++levels;
    }
    return levels;
}

v9x_u32 v9x_r3d_validate_levels(const V9X_R3D_ABI_LEVEL *levels,
                                v9x_u32 level_count,
                                v9x_u32 size_max)
{
    v9x_u32 width;
    v9x_u32 height;
    v9x_u32 level;

    if (levels == 0 || level_count == 0ul ||
        level_count > V9X_R3D_ABI_LEVELS_MAX) {
        return V9X_R3D_RESULT_INVALID;
    }
    width = levels[0].width;
    height = levels[0].height;
    if (!v9x_r3d_validate_power_of_two(width) ||
        !v9x_r3d_validate_power_of_two(height) ||
        width > size_max || height > size_max) {
        return V9X_R3D_RESULT_INVALID;
    }
    if (level_count > v9x_r3d_validate_chain_length(width, height)) {
        return V9X_R3D_RESULT_INVALID;
    }
    for (level = 0ul; level < level_count; ++level) {
        const V9X_R3D_ABI_LEVEL *current = &levels[level];

        if (current->pixels == 0 || current->width != width ||
            current->height != height ||
            current->pitch < width * V9X_R3D_VALIDATE_BPP) {
            return V9X_R3D_RESULT_INVALID;
        }
        /* The storage the ICD declares for this level holds all of it: the
         * sampler reads anywhere inside, and this is the only bound it has
         * on memory that is not DirectDraw's. */
        if (!v9x_r3d_validate_extent(0ul, current->pitch, width, height,
                                     current->bytes)) {
            return V9X_R3D_RESULT_INVALID;
        }
        width = width > 1ul ? width / 2ul : 1ul;
        height = height > 1ul ? height / 2ul : 1ul;
    }
    return V9X_R3D_RESULT_OK;
}

v9x_u32 v9x_r3d_validate_texture(const V9X_R3D_ABI_TEXTURE *texture,
                                 v9x_u32 size_max)
{
    v9x_u32 result;

    if (texture == 0) {
        return V9X_R3D_RESULT_INVALID;
    }
    if (texture->storage == V9X_R3D_ABI_TEXTURE_NONE) {
        return V9X_R3D_RESULT_OK;
    }
    if (texture->storage != V9X_R3D_ABI_TEXTURE_CPU &&
        texture->storage != V9X_R3D_ABI_TEXTURE_HW) {
        return V9X_R3D_RESULT_INVALID;
    }
    /* Texture layouts only; XRGB1555 is a target's. */
    if (texture->format != V9X_R3D_ABI_FORMAT_RGB565 &&
        texture->format != V9X_R3D_ABI_FORMAT_ARGB1555 &&
        texture->format != V9X_R3D_ABI_FORMAT_ARGB4444) {
        return V9X_R3D_RESULT_INVALID;
    }
    if ((texture->min_filter != V9X_R3D_ABI_FILTER_NEAREST &&
         texture->min_filter != V9X_R3D_ABI_FILTER_LINEAR) ||
        (texture->mag_filter != V9X_R3D_ABI_FILTER_NEAREST &&
         texture->mag_filter != V9X_R3D_ABI_FILTER_LINEAR)) {
        return V9X_R3D_RESULT_INVALID;
    }
    if (texture->mip < V9X_R3D_ABI_MIP_NONE ||
        texture->mip > V9X_R3D_ABI_MIP_LINEAR) {
        return V9X_R3D_RESULT_INVALID;
    }
    if (texture->address != V9X_R3D_ABI_ADDRESS_WRAP &&
        texture->address != V9X_R3D_ABI_ADDRESS_CLAMP) {
        return V9X_R3D_RESULT_INVALID;
    }
    if (texture->color_op < V9X_R3D_ABI_COLOROP_REPLACE ||
        texture->color_op > V9X_R3D_ABI_COLOROP_BLEND ||
        texture->alpha_op > V9X_R3D_ABI_ALPHAOP_MODULATE ||
        texture->env_color > 0x00fffffful) {
        return V9X_R3D_RESULT_INVALID;
    }

    if (texture->storage == V9X_R3D_ABI_TEXTURE_HW) {
        /* The chain is the surface's attachments, resolved at the boundary;
         * a CPU chain alongside it would be two answers to one question. */
        if (texture->surface.surface == 0 || texture->levels != 0 ||
            texture->level_count != 0ul) {
            return V9X_R3D_RESULT_INVALID;
        }
        return V9X_R3D_RESULT_OK;
    }

    result = v9x_r3d_validate_levels(texture->levels, texture->level_count,
                                     size_max);
    if (result != V9X_R3D_RESULT_OK) {
        return result;
    }
    /* A level filter needs every level to 1x1: OpenGL 1.1 calls anything
     * less an incomplete texture (3.8.9), and the sampler would otherwise
     * clamp to the last level it was given and draw the wrong blur. */
    if (texture->mip != V9X_R3D_ABI_MIP_NONE &&
        texture->level_count !=
            v9x_r3d_validate_chain_length(texture->levels[0].width,
                                          texture->levels[0].height)) {
        return V9X_R3D_RESULT_INVALID;
    }
    return V9X_R3D_RESULT_OK;
}

static int v9x_r3d_validate_compare(v9x_u32 compare)
{
    return compare >= V9X_R3D_CMP_NEVER && compare <= V9X_R3D_CMP_ALWAYS;
}

static int v9x_r3d_validate_factor(v9x_u32 factor)
{
    return factor >= V9X_R3D_BLEND_ZERO && factor <= V9X_R3D_BLEND_SRCALPHASAT;
}

v9x_u32 v9x_r3d_validate_state(const V9X_R3D_ABI_STATE *state,
                               v9x_u32 width, v9x_u32 height)
{
    if (state == 0) {
        return V9X_R3D_RESULT_INVALID;
    }
    /* Selectors are read only under their enable, as a GL front end keeps
     * the defaults of a disabled test in place; values are always ranged. */
    if (state->depth_enable != 0ul &&
        !v9x_r3d_validate_compare(state->depth_func)) {
        return V9X_R3D_RESULT_INVALID;
    }
    if (state->blend_enable != 0ul &&
        (!v9x_r3d_validate_factor(state->src_blend) ||
         !v9x_r3d_validate_factor(state->dst_blend))) {
        return V9X_R3D_RESULT_INVALID;
    }
    if (state->alpha_test_enable != 0ul &&
        !v9x_r3d_validate_compare(state->alpha_func)) {
        return V9X_R3D_RESULT_INVALID;
    }
    if (state->alpha_ref > 255ul || state->fog_color > 0x00fffffful ||
        state->write_mask > V9X_R3D_ABI_WRITE_RGB) {
        return V9X_R3D_RESULT_INVALID;
    }
    if (state->scissor_left > state->scissor_right ||
        state->scissor_top > state->scissor_bottom ||
        state->scissor_right > width || state->scissor_bottom > height) {
        return V9X_R3D_RESULT_INVALID;
    }
    return V9X_R3D_RESULT_OK;
}

v9x_u32 v9x_r3d_validate_draw(const V9X_R3D_ABI_DRAW *draw,
                              v9x_u32 generation,
                              v9x_u32 texture_size_max)
{
    if (draw == 0) {
        return V9X_R3D_RESULT_INVALID;
    }
    /* Nothing past struct_bytes is read from a caller built against another
     * layout: every later field could be somewhere else. */
    if (draw->struct_bytes != (v9x_u32)sizeof(V9X_R3D_ABI_DRAW)) {
        return V9X_R3D_RESULT_ABI;
    }
    if (draw->generation != generation) {
        return V9X_R3D_RESULT_STALE;
    }
    if (draw->target.surface == 0 || draw->vertices == 0 ||
        draw->triangle_count == 0ul ||
        draw->triangle_count > V9X_R3D_ABI_BATCH_MAX) {
        return V9X_R3D_RESULT_INVALID;
    }
    return v9x_r3d_validate_texture(&draw->texture, texture_size_max);
}

v9x_u32 v9x_r3d_validate_clear(const V9X_R3D_ABI_CLEAR *clear,
                               v9x_u32 generation)
{
    if (clear == 0) {
        return V9X_R3D_RESULT_INVALID;
    }
    if (clear->struct_bytes != (v9x_u32)sizeof(V9X_R3D_ABI_CLEAR)) {
        return V9X_R3D_RESULT_ABI;
    }
    if (clear->generation != generation) {
        return V9X_R3D_RESULT_STALE;
    }
    if (clear->target.surface == 0 ||
        (clear->clear_depth != 0ul && clear->depth.surface == 0) ||
        (clear->rect_count != 0ul && clear->rects == 0)) {
        return V9X_R3D_RESULT_INVALID;
    }
    if (clear->color_value > 0x00fffffful || clear->depth_value > 0xfffful ||
        clear->write_mask > V9X_R3D_ABI_WRITE_RGB) {
        return V9X_R3D_RESULT_INVALID;
    }
    return V9X_R3D_RESULT_OK;
}

v9x_u32 v9x_r3d_validate_rects(const V9X_R3D_ABI_RECT *rects,
                               v9x_u32 rect_count,
                               v9x_u32 width, v9x_u32 height)
{
    v9x_u32 index;

    if (rect_count != 0ul && rects == 0) {
        return V9X_R3D_RESULT_INVALID;
    }
    for (index = 0ul; index < rect_count; ++index) {
        const V9X_R3D_ABI_RECT *rect = &rects[index];

        if (rect->left > rect->right || rect->top > rect->bottom ||
            rect->right > width || rect->bottom > height) {
            return V9X_R3D_RESULT_INVALID;
        }
    }
    return V9X_R3D_RESULT_OK;
}
