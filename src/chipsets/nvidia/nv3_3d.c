/*
 * NVIDIA RIVA 128 (NV3) 3D: the D3D object's RAMIN, surface, state and
 * vertex words. Pure policy, integer only; see velocity9x\nv3_3d.h for the
 * measurements behind each value.
 */
#include "velocity9x/nv3_3d.h"

/* Texels are 16 bits in every format the object takes. */
#define V9X_NV3_3D_TEXEL_BYTES      2ul
#define V9X_NV3_3D_PIXEL_BYTES      2ul
#define V9X_NV3_3D_SLOT_MASK        0x0000000ful
#define V9X_NV3_3D_TRIPLE_SHIFT     12u
#define V9X_NV3_3D_FOG_MAX          0xfful
#define V9X_NV3_3D_ALPHA_MASK       0xff000000ul

static void v9x_nv3_3d_put(struct v9x_nv3_write *list, v9x_u32 *count,
                           v9x_u32 offset, v9x_u32 value)
{
    list[*count].offset = offset;
    list[*count].value = value;
    ++*count;
}

static v9x_u32 v9x_nv3_3d_method(v9x_u32 subchannel, v9x_u32 method)
{
    return V9X_NV3_USER + subchannel * V9X_NV3_SUBCHANNEL_STEP + method;
}

static int v9x_nv3_3d_compare_valid(v9x_u32 compare)
{
    return compare >= V9X_NV3_CMP_NEVER && compare <= V9X_NV3_CMP_ALWAYS;
}

v9x_status v9x_nv3_3d_build_object(v9x_u32 vram_bytes,
                                   struct v9x_nv3_3d_object *object)
{
    if (object == 0 || vram_bytes == 0ul ||
        (vram_bytes & (V9X_NV3_D3D_DMA_PAGE - 1ul)) != 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    /* Past 8 MiB the page table would run beyond RAMIN's 32 KiB. */
    if (vram_bytes / V9X_NV3_D3D_DMA_PAGE > V9X_NV3_D3D_DMA_ENTRIES_MAX) {
        return V9X_STATUS_UNSUPPORTED;
    }

    object->ramin_count = 0ul;
    v9x_nv3_3d_put(object->ramin, &object->ramin_count, V9X_NV3_D3D_RAMHT,
                   V9X_NV3_D3D_HANDLE);
    v9x_nv3_3d_put(object->ramin, &object->ramin_count,
                   V9X_NV3_D3D_RAMHT + 4ul, V9X_NV3_D3D_CONTEXT);
    v9x_nv3_3d_put(object->ramin, &object->ramin_count, V9X_NV3_D3D_INSTANCE,
                   V9X_NV3_D3D_OPTIONS);
    v9x_nv3_3d_put(object->ramin, &object->ramin_count,
                   V9X_NV3_D3D_INSTANCE + 4ul, V9X_NV3_D3D_DMA_INSTANCE);
    v9x_nv3_3d_put(object->ramin, &object->ramin_count, V9X_NV3_D3D_DMA,
                   V9X_NV3_D3D_DMA_FLAGS);
    v9x_nv3_3d_put(object->ramin, &object->ramin_count,
                   V9X_NV3_D3D_DMA + 4ul, vram_bytes - 1ul);
    object->entry_count = vram_bytes / V9X_NV3_D3D_DMA_PAGE;
    object->bind.offset = v9x_nv3_3d_method(V9X_NV3_SUB_D3D, 0ul);
    object->bind.value = V9X_NV3_D3D_HANDLE;
    return V9X_STATUS_OK;
}

/* Entry n maps the object's page n to video memory page n. */
struct v9x_nv3_write v9x_nv3_3d_dma_entry(v9x_u32 page)
{
    struct v9x_nv3_write entry;

    entry.offset = V9X_NV3_D3D_DMA + 8ul + page * 4ul;
    entry.value = (page * V9X_NV3_D3D_DMA_PAGE) | V9X_NV3_D3D_DMA_PRESENT_RW;
    return entry;
}

/* One 16 bpp surface of the target: aligned, wide enough for the width,
 * inside video memory, and inside the 4 MiB the surface registers are
 * trusted to reach (nv3_2d.h). */
static v9x_status v9x_nv3_3d_surface(v9x_u32 vram_bytes, v9x_u32 offset,
                                     v9x_u32 pitch_bytes, v9x_u32 width,
                                     v9x_u32 height)
{
    v9x_u32 end;

    if ((offset & (V9X_NV3_SURFACE_ALIGN - 1ul)) != 0ul ||
        pitch_bytes == 0ul ||
        (pitch_bytes & (V9X_NV3_SURFACE_ALIGN - 1ul)) != 0ul ||
        pitch_bytes < width * V9X_NV3_3D_PIXEL_BYTES) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (pitch_bytes > V9X_NV3_PITCH_MAX) {
        return V9X_STATUS_UNSUPPORTED;
    }

    /* Height is 16-bit and pitch below 8 KiB, so this cannot overflow. */
    end = (height - 1ul) * pitch_bytes + width * V9X_NV3_3D_PIXEL_BYTES;
    if (offset >= vram_bytes || end > vram_bytes - offset) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (offset + end > V9X_NV3_SURFACE_LIMIT) {
        return V9X_STATUS_UNSUPPORTED;
    }
    return V9X_STATUS_OK;
}

v9x_status v9x_nv3_3d_build_target(const struct v9x_nv3_3d_target *target,
                                   struct v9x_nv3_op *op)
{
    v9x_status status;

    if (target == 0 || op == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    op->surface_count = 0ul;
    op->method_count = 0ul;
    if (target->width == 0ul || target->height == 0ul ||
        target->width > V9X_NV3_EXTENT_MAX ||
        target->height > V9X_NV3_EXTENT_MAX) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    status = v9x_nv3_3d_surface(target->vram_bytes, target->colour_offset,
                                target->colour_pitch_bytes, target->width,
                                target->height);
    if (status != V9X_STATUS_OK) {
        return status;
    }
    status = v9x_nv3_3d_surface(target->vram_bytes, target->zeta_offset,
                                target->zeta_pitch_bytes, target->width,
                                target->height);
    if (status != V9X_STATUS_OK) {
        return status;
    }

    v9x_nv3_3d_put(op->surface, &op->surface_count,
                   V9X_NV3_PGRAPH_SURF_OFFSET +
                   V9X_NV3_D3D_SURFACE_COLOUR * 4ul, target->colour_offset);
    v9x_nv3_3d_put(op->surface, &op->surface_count,
                   V9X_NV3_PGRAPH_SURF_PITCH +
                   V9X_NV3_D3D_SURFACE_COLOUR * 4ul,
                   target->colour_pitch_bytes);
    v9x_nv3_3d_put(op->surface, &op->surface_count,
                   V9X_NV3_PGRAPH_SURF_OFFSET +
                   V9X_NV3_D3D_SURFACE_ZETA * 4ul, target->zeta_offset);
    v9x_nv3_3d_put(op->surface, &op->surface_count,
                   V9X_NV3_PGRAPH_SURF_PITCH + V9X_NV3_D3D_SURFACE_ZETA * 4ul,
                   target->zeta_pitch_bytes);
    v9x_nv3_3d_put(op->methods, &op->method_count,
                   v9x_nv3_3d_method(V9X_NV3_SUB_CLIP, V9X_NV3_M_CLIP_POINT),
                   0ul);
    v9x_nv3_3d_put(op->methods, &op->method_count,
                   v9x_nv3_3d_method(V9X_NV3_SUB_CLIP, V9X_NV3_M_CLIP_SIZE),
                   target->width | (target->height << 16));
    return V9X_STATUS_OK;
}

v9x_u32 v9x_nv3_3d_add_specular(v9x_u32 colour, v9x_u32 specular)
{
    v9x_u32 result;
    v9x_u32 sum;
    v9x_u32 shift;

    result = colour & V9X_NV3_3D_ALPHA_MASK;
    for (shift = 0u; shift < 24u; shift += 8u) {
        sum = ((colour >> shift) & 0xfful) + ((specular >> shift) & 0xfful);
        result |= (sum > 0xfful ? 0xfful : sum) << shift;
    }
    return result;
}

v9x_u32 v9x_nv3_3d_chain_bytes(v9x_u32 log2_side, v9x_u32 levels)
{
    v9x_u32 bytes = 0ul;
    v9x_u32 level;

    if (levels == 0ul) {
        levels = 1ul;
    }
    for (level = 0ul; level < levels && level <= log2_side; ++level) {
        bytes += V9X_NV3_3D_TEXEL_BYTES <<
                 (2u * (unsigned int)(log2_side - level));
    }
    return bytes;
}

/* Checks the fields that have to hold a value whatever is enabled, and
 * the texture's place in video memory. */
static v9x_status v9x_nv3_3d_check_state(const struct v9x_nv3_3d_state *state)
{
    v9x_u32 bytes;

    if (state->texture_format > V9X_NV3_TEX_R5G6B5 ||
        state->texture_log2 > V9X_NV3_TEX_LOG2_MAX ||
        (state->texture_offset & (V9X_NV3_3D_TEXEL_BYTES - 1ul)) != 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (state->texture_levels > 1ul &&
        (state->texture_levels - 1ul > state->texture_log2 ||
         state->texture_log2 - (state->texture_levels - 1ul) <
             V9X_NV3_MIP_LOG2_MIN)) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    /* At most 2048 x 2048 x 2 x 4/3, under 12 MiB: no overflow. */
    bytes = v9x_nv3_3d_chain_bytes(state->texture_log2,
                                   state->texture_levels);
    if (state->texture_offset >= state->vram_bytes ||
        bytes > state->vram_bytes - state->texture_offset) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (state->wrap_u < V9X_NV3_WRAP || state->wrap_u > V9X_NV3_CLAMP ||
        state->wrap_v < V9X_NV3_WRAP || state->wrap_v > V9X_NV3_CLAMP ||
        state->filter > V9X_NV3_FILTER_LINEAR ||
        state->colour_key_texel > V9X_NV3_TEX_KEY_MASK) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (state->z_enable && !v9x_nv3_3d_compare_valid(state->z_func)) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (state->alpha_test &&
        (!v9x_nv3_3d_compare_valid(state->alpha_func) ||
         state->alpha_reference > 0xfful)) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    /* A narrower texture's copies tile the side exactly. */
    if (state->u_shift > state->texture_log2 ||
        state->v_shift > state->texture_log2) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (state->blend > V9X_NV3_BLEND_ADD_SRC_ALPHA ||
        (state->blend == V9X_NV3_BLEND_ADD_SRC_ALPHA && state->fog)) {
        return V9X_STATUS_UNSUPPORTED;
    }
    return V9X_STATUS_OK;
}

/* An IEEE single divided by 2^shift through its exponent; what would
 * leave the normal range becomes zero of the same sign, far below a
 * texel at any texture size. */
static v9x_u32 v9x_nv3_3d_scale_down(v9x_u32 bits, v9x_u32 shift)
{
    v9x_u32 exponent;

    exponent = (bits >> 23) & 0xfful;
    if (shift == 0ul || exponent == 0ul) {
        return bits;
    }
    if (exponent <= shift) {
        return bits & 0x80000000ul;
    }
    return bits - (shift << 23);
}

v9x_status v9x_nv3_3d_build_state(const struct v9x_nv3_3d_state *state,
                                  struct v9x_nv3_3d_words *words)
{
    v9x_u32 count;
    v9x_u32 colour;
    v9x_u32 format;
    v9x_u32 config;
    v9x_u32 z_func;
    v9x_u32 z_write;
    v9x_u32 colour_write;
    v9x_u32 alpha;
    v9x_u32 smallest;
    v9x_u32 filter;
    v9x_status status;

    if (state == 0 || words == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    status = v9x_nv3_3d_check_state(state);
    if (status != V9X_STATUS_OK) {
        return status;
    }

    /*
     * The chip has no blend that ignores alpha: its blend is
     * dst x (1 - beta) + src x beta. An opaque draw is the source alpha
     * blend with alpha made 1: every vertex's alpha FFh, and A1R5G5B5
     * texels read as X1R5G5B5, the same bits with alpha 1. Under the alpha
     * test A1R5G5B5 keeps its alpha, since what passes has alpha 1 anyway.
     * A4R4G4B4 has no alpha-less twin and partial alphas, so it is refused
     * and the HAL draws that state some other way.
     */
    colour = state->texture_format;
    words->colour_or = 0ul;
    if (state->blend == V9X_NV3_BLEND_OPAQUE) {
        if (colour == V9X_NV3_TEX_A4R4G4B4) {
            return V9X_STATUS_UNSUPPORTED;
        }
        if (colour == V9X_NV3_TEX_A1R5G5B5 && !state->alpha_test) {
            colour = V9X_NV3_TEX_X1R5G5B5;
        }
        words->colour_or = V9X_NV3_3D_ALPHA_MASK;
    }
    /* The add takes the texel x vertex colour whole; with the vertex
     * colour already x alpha that is Direct3D's SRCALPHA, ONE. */
    words->premultiply = 0;
    if (state->blend == V9X_NV3_BLEND_ADD_SRC_ALPHA) {
        if (colour == V9X_NV3_TEX_A1R5G5B5) {
            colour = V9X_NV3_TEX_X1R5G5B5;
        }
        words->premultiply = 1;
    }
    if (state->vertex_white) {
        words->colour_or = 0xfffffffful;
    }
    words->u_shift = state->u_shift;
    words->v_shift = state->v_shift;
    smallest = state->texture_log2;
    filter = 0ul;
    if (state->texture_levels > 1ul) {
        smallest -= state->texture_levels - 1ul;
        filter = V9X_NV3_FILTER_MIP_BIAS;
    }
    format = (colour << V9X_NV3_TEX_COLOUR_SHIFT) |
             (smallest << V9X_NV3_TEX_SIZE_MIN_SHIFT) |
             (state->texture_log2 << V9X_NV3_TEX_SIZE_MAX_SHIFT);
    if (state->colour_key) {
        format |= V9X_NV3_TEX_KEY_ENABLE | state->colour_key_texel;
    }

    /*
     * The write enables carry the tests: with Z on, colour and zeta are
     * written only where Z passes (Z_TEST), and with the alpha test too,
     * only where both pass, so an alpha-rejected texel leaves zeta alone
     * as Direct3D wants.
     */
    z_func = V9X_NV3_CMP_ALWAYS;
    z_write = V9X_NV3_WREN_NEVER;
    if (state->z_enable && state->alpha_test) {
        z_func = state->z_func;
        colour_write = V9X_NV3_WREN_ALPHA_AND_Z;
        if (state->z_write) {
            z_write = V9X_NV3_WREN_ALPHA_AND_Z;
        }
    } else if (state->z_enable) {
        z_func = state->z_func;
        colour_write = V9X_NV3_WREN_Z;
        if (state->z_write) {
            z_write = V9X_NV3_WREN_Z;
        }
    } else if (state->alpha_test) {
        colour_write = V9X_NV3_WREN_ALPHA;
    } else {
        colour_write = V9X_NV3_WREN_ALWAYS;
    }

    config = (state->filter == V9X_NV3_FILTER_LINEAR ?
              V9X_NV3_CFG_INTERP_FOH : V9X_NV3_CFG_INTERP_ZOH) |
             (state->wrap_u << V9X_NV3_CFG_WRAP_U_SHIFT) |
             (state->wrap_v << V9X_NV3_CFG_WRAP_V_SHIFT) |
             V9X_NV3_CFG_SOURCE_NORMAL | V9X_NV3_CFG_CULL_NONE |
             (z_func << V9X_NV3_CFG_ZFUNC_SHIFT) |
             (z_write << V9X_NV3_CFG_ZWRITE_SHIFT) |
             (colour_write << V9X_NV3_CFG_CWRITE_SHIFT);
    /* ADD is the saturating sum, measured over the Z case's red. MODULATE,
     * beta the destination colour and the destination's own term zeroed,
     * is src x dst by envytools' model of the blend; not drawn. */
    if (state->blend == V9X_NV3_BLEND_ADD ||
        state->blend == V9X_NV3_BLEND_ADD_SRC_ALPHA) {
        config |= V9X_NV3_CFG_ROP_ADD;
    } else if (state->blend == V9X_NV3_BLEND_MODULATE) {
        config |= V9X_NV3_CFG_BETA_DSTCOLOUR | V9X_NV3_CFG_DST_ZERO;
    }

    alpha = V9X_NV3_CMP_ALWAYS << V9X_NV3_ALPHA_FUNC_SHIFT;
    if (state->alpha_test) {
        alpha = (state->alpha_func << V9X_NV3_ALPHA_FUNC_SHIFT) |
                state->alpha_reference;
    }

    /* The filter word is 0 for one level, as in every state case, and the
     * level bias for a chain; the draw needs all five of offset, format,
     * filter, fog colour and config sent. */
    count = 0ul;
    v9x_nv3_3d_put(words->methods, &count,
                   v9x_nv3_3d_method(V9X_NV3_SUB_D3D,
                                     V9X_NV3_M_D3D_TEX_OFFSET),
                   state->texture_offset);
    v9x_nv3_3d_put(words->methods, &count,
                   v9x_nv3_3d_method(V9X_NV3_SUB_D3D,
                                     V9X_NV3_M_D3D_TEX_FORMAT),
                   format);
    v9x_nv3_3d_put(words->methods, &count,
                   v9x_nv3_3d_method(V9X_NV3_SUB_D3D, V9X_NV3_M_D3D_FILTER),
                   filter);
    v9x_nv3_3d_put(words->methods, &count,
                   v9x_nv3_3d_method(V9X_NV3_SUB_D3D,
                                     V9X_NV3_M_D3D_FOG_COLOUR),
                   state->fog ? (state->fog_colour & 0x00fffffful) : 0ul);
    v9x_nv3_3d_put(words->methods, &count,
                   v9x_nv3_3d_method(V9X_NV3_SUB_D3D, V9X_NV3_M_D3D_CONFIG),
                   config);
    v9x_nv3_3d_put(words->methods, &count,
                   v9x_nv3_3d_method(V9X_NV3_SUB_D3D, V9X_NV3_M_D3D_ALPHA),
                   alpha);
    words->fog = state->fog ? 1 : 0;
    return V9X_STATUS_OK;
}

v9x_u32 v9x_nv3_3d_specular(v9x_u32 slot, const v9x_u32 *others,
                            const struct v9x_nv3_3d_words *words,
                            v9x_u32 d3d_specular)
{
    v9x_u32 first;
    v9x_u32 second;
    v9x_u32 fog;

    slot &= V9X_NV3_3D_SLOT_MASK;
    second = slot | (slot << 4) | (slot << 8);
    first = second;
    if (others != 0) {
        first = slot | ((others[0] & V9X_NV3_3D_SLOT_MASK) << 4) |
                ((others[1] & V9X_NV3_3D_SLOT_MASK) << 8);
    }

    /* Direct3D's specular alpha is 255 for no fog; the chip's byte is the
     * fog amount, 0 for none. */
    fog = 0ul;
    if (words != 0 && words->fog) {
        fog = V9X_NV3_3D_FOG_MAX - (d3d_specular >> V9X_NV3_FOG_SHIFT);
    }
    return first | (second << V9X_NV3_3D_TRIPLE_SHIFT) |
           (fog << V9X_NV3_FOG_SHIFT);
}

/* An A8R8G8B8 colour with each of R, G and B x A / 255, rounded; A kept. */
static v9x_u32 v9x_nv3_3d_premultiply(v9x_u32 colour)
{
    v9x_u32 alpha;
    v9x_u32 result;
    v9x_u32 shift;

    alpha = colour >> 24;
    result = colour & V9X_NV3_3D_ALPHA_MASK;
    for (shift = 0u; shift < 24u; shift += 8u) {
        result |= ((((colour >> shift) & 0xfful) * alpha + 127ul) / 255ul)
                  << shift;
    }
    return result;
}

v9x_status v9x_nv3_3d_build_vertex(const struct v9x_nv3_3d_vertex *vertex,
                                   v9x_u32 slot, const v9x_u32 *others,
                                   const struct v9x_nv3_3d_words *words,
                                   struct v9x_nv3_write
                                       methods[V9X_NV3_D3D_VERTEX_METHODS])
{
    v9x_u32 values[V9X_NV3_D3D_VERTEX_METHODS];
    v9x_u32 index;

    if (vertex == 0 || words == 0 || methods == 0 || slot >= V9X_NV3_SLOTS) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    /* A triangle draws only from three different slots. */
    if (others != 0 &&
        (others[0] >= V9X_NV3_SLOTS || others[1] >= V9X_NV3_SLOTS ||
         others[0] == slot || others[1] == slot || others[0] == others[1])) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    /* The TLVERTEX order; the V coordinate last, which stores the vertex
     * and draws. */
    values[0] = v9x_nv3_3d_specular(slot, others, words, vertex->specular);
    values[1] = vertex->colour | words->colour_or;
    if (words->premultiply) {
        values[1] = v9x_nv3_3d_premultiply(values[1]);
    }
    values[2] = vertex->sx;
    values[3] = vertex->sy;
    values[4] = vertex->sz;
    values[5] = vertex->rhw;
    values[6] = v9x_nv3_3d_scale_down(vertex->tu, words->u_shift);
    values[7] = v9x_nv3_3d_scale_down(vertex->tv, words->v_shift);
    for (index = 0ul; index < V9X_NV3_D3D_VERTEX_METHODS; ++index) {
        methods[index].offset =
            v9x_nv3_3d_method(V9X_NV3_SUB_D3D,
                              V9X_NV3_M_D3D_VERTEX + index * 4ul);
        methods[index].value = values[index];
    }
    return V9X_STATUS_OK;
}

v9x_u16 v9x_nv3_3d_opaque_texel(v9x_u16 argb4444)
{
    v9x_u16 red;
    v9x_u16 green;
    v9x_u16 blue;

    red = (v9x_u16)((argb4444 >> 8) & 0x0fu);
    green = (v9x_u16)((argb4444 >> 4) & 0x0fu);
    blue = (v9x_u16)(argb4444 & 0x0fu);
    red = (v9x_u16)((red << 1) | (red >> 3));
    green = (v9x_u16)((green << 1) | (green >> 3));
    blue = (v9x_u16)((blue << 1) | (blue >> 3));
    return (v9x_u16)((red << 10) | (green << 5) | blue);
}

/* Morton order, u in the even bits and v in the odd, measured for every
 * texel of an 8x8 texture; coordinates wrap inside the side. */
v9x_u32 v9x_nv3_3d_swizzle(v9x_u32 u, v9x_u32 v, v9x_u32 log2_size)
{
    v9x_u32 bit;
    v9x_u32 offset;

    offset = 0ul;
    for (bit = 0ul; bit < log2_size; ++bit) {
        offset |= ((u >> bit) & 1ul) << (2ul * bit);
        offset |= ((v >> bit) & 1ul) << (2ul * bit + 1ul);
    }
    return offset;
}
