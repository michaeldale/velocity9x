#include "velocity9x/ati_rage2_draw.h"
#include "velocity9x/ati_mach64_regs.h"

/*
 * Rage IIC draw acceptance and packets. Every accepted state has a passing
 * scene on A8U4I5 (docs\decisions\2026-10-02-rage-iic-texture-addressing.md
 * and -filtering-formats-blending-fog.md, Phase 3's -triangles-and-
 * gouraud.md); everything else is refused before a FIFO slot is reserved.
 * Units that share hardware are not combined: fog is the blend unit with
 * DP_FRGD_CLR as the destination.
 */

/* Numbering shared with Direct3D and V9X_R3D_* (mach64_policy.c). */
#define R2_TARGET_RGB565        1ul
#define R2_WRITE_MASK_RGB       7ul
#define R2_SHADE_FLAT           1ul
#define R2_SHADE_GOURAUD        2ul
#define R2_CMP_NEVER            1ul
#define R2_CMP_GREATER          5ul
#define R2_CMP_NOTEQUAL         6ul
#define R2_CMP_GREATEREQUAL     7ul
#define R2_CMP_ALWAYS           8ul
#define R2_ALPHA_REF_MAX        255ul
#define R2_DEPTH_BITS           16ul

#define R2_BLEND_ZERO           1ul
#define R2_BLEND_ONE            2ul
#define R2_BLEND_SRCCOLOR       3ul
#define R2_BLEND_INVSRCCOLOR    4ul
#define R2_BLEND_SRCALPHA       5ul
#define R2_BLEND_INVSRCALPHA    6ul
#define R2_BLEND_DESTCOLOR      9ul
#define R2_BLEND_INVDESTCOLOR   10ul

#define R2_FILTER_NEAREST       1ul
#define R2_FILTER_LINEAR        2ul
#define R2_FILTER_MIPNEAREST    3ul
#define R2_FILTER_MIPLINEAR     4ul
#define R2_FILTER_LINEARMIPNEAREST 5ul
#define R2_FILTER_LINEARMIPLINEAR  6ul

#define R2_ADDRESS_WRAP         1ul
#define R2_ADDRESS_CLAMP        3ul

#define R2_TEXOP_DECAL          1ul
#define R2_TEXOP_MODULATE       2ul
#define R2_TEXOP_DECALALPHA     3ul
#define R2_TEXOP_MODULATEALPHA  4ul
#define R2_TEXOP_COPY           7ul

/* The chip's ALPHA_BLND_SRC/DST codes (RRG p.6-5): 0 zero, 1 one, 2 the
 * other side's colour, 3 its inverse, 4 source alpha, 5 its inverse. */
#define R2_FACTOR_NONE          0xfffffffful
#define R2_FACTOR_ALPHA         4ul
#define R2_FACTOR_INV_ALPHA     5ul

/* Z_CNTL (2026-10-02-rage-iic-triangles-and-gouraud.md): Z_EN, Z_TEST
 * 6:4 numbered never, <, <=, ==, >=, >, !=, always, Z_MASK write. */
#define R2_Z_SHIFT_FRACTION     4096.0
#define R2_Z_FIELD_MASK         0x1ffffffful
#define R2_Z_GRADIENT_LIMIT     65536.0

/* ---- Policy ------------------------------------------------------------- */

static int r2_pow2_in_range(v9x_u32 edge)
{
    return (edge & (edge - 1ul)) == 0ul && edge >= V9X_R2_DRAW_TEXTURE_MIN &&
           edge <= V9X_R2_DRAW_TEXTURE_MAX;
}

/* Only level 0 is sampled (MIP_MAP_DISABLE; mip-mapping was not
 * measured), so a mip filter is its filter within the level. */
static v9x_u32 r2_base_filter(v9x_u32 filter)
{
    switch (filter) {
    case R2_FILTER_MIPNEAREST:
    case R2_FILTER_LINEARMIPNEAREST:
        return R2_FILTER_NEAREST;
    case R2_FILTER_MIPLINEAR:
    case R2_FILTER_LINEARMIPLINEAR:
        return R2_FILTER_LINEAR;
    default:
        return filter;
    }
}

static v9x_u32 r2_source_factor(v9x_u32 factor)
{
    switch (factor) {
    case R2_BLEND_ZERO:         return 0ul;
    case R2_BLEND_ONE:          return 1ul;
    case R2_BLEND_DESTCOLOR:    return 2ul;
    case R2_BLEND_INVDESTCOLOR: return 3ul;
    case R2_BLEND_SRCALPHA:     return R2_FACTOR_ALPHA;
    case R2_BLEND_INVSRCALPHA:  return R2_FACTOR_INV_ALPHA;
    default:                    return R2_FACTOR_NONE;
    }
}

static v9x_u32 r2_destination_factor(v9x_u32 factor)
{
    switch (factor) {
    case R2_BLEND_ZERO:         return 0ul;
    case R2_BLEND_ONE:          return 1ul;
    case R2_BLEND_SRCCOLOR:     return 2ul;
    case R2_BLEND_INVSRCCOLOR:  return 3ul;
    case R2_BLEND_SRCALPHA:     return R2_FACTOR_ALPHA;
    case R2_BLEND_INVSRCALPHA:  return R2_FACTOR_INV_ALPHA;
    default:                    return R2_FACTOR_NONE;
    }
}

/* The Mobility-M's texture format numbers to DP_PIX_WIDTH[31:28]. */
static v9x_u32 r2_texture_format(v9x_u32 m64_format)
{
    switch (m64_format) {
    case V9X_M64_TEXTURE_FORMAT_RGB565:   return V9X_R2_TEX_FORMAT_565;
    case V9X_M64_TEXTURE_FORMAT_ARGB1555: return V9X_R2_TEX_FORMAT_1555;
    case V9X_M64_TEXTURE_FORMAT_ARGB4444: return V9X_R2_TEX_FORMAT_4444;
    default:                              return 0ul;
    }
}

/*
 * The alpha test the chip has is TEX_AMASK_AEN: a texel whose alpha LSB
 * is 0 is not drawn (F5, F6). On ARGB1555, whose alpha is 0 or 255, that
 * is exactly "alpha > ref" for ref < 255, "alpha >= ref" for 1..255 and
 * "alpha != 0". Nothing else is.
 */
static int r2_alpha_test_is_mask(const struct v9x_m64_draw_request *request)
{
    if (request->alpha_ref > R2_ALPHA_REF_MAX) {
        return 0;
    }
    switch (request->alpha_func) {
    case R2_CMP_GREATER:
        return request->alpha_ref < R2_ALPHA_REF_MAX;
    case R2_CMP_GREATEREQUAL:
        return request->alpha_ref >= 1ul;
    case R2_CMP_NOTEQUAL:
        return request->alpha_ref == 0ul;
    default:
        return 0;
    }
}

v9x_u32 v9x_r2_check_draw(const struct v9x_m64_draw_request *request,
                          v9x_u32 coords_in_unit,
                          struct v9x_r2_draw_decision *decision)
{
    static const struct v9x_r2_draw_decision empty;
    v9x_u32 scale;
    int blending;
    int texel_alpha = 0;

    if (decision != 0) {
        *decision = empty;
    }
    if (request == 0 || decision == 0) {
        return V9X_M64_REFUSE_ARGUMENT;
    }

    /* RGB565 is the only target the scenes drew. */
    if (request->target_format != R2_TARGET_RGB565 ||
        request->target_width == 0ul || request->target_height == 0ul ||
        request->target_width > 2048ul || request->target_height > 2048ul) {
        return V9X_M64_REFUSE_TARGET_FORMAT;
    }
    if (request->scissor_left >= request->scissor_right ||
        request->scissor_top >= request->scissor_bottom ||
        request->scissor_right > request->target_width ||
        request->scissor_bottom > request->target_height) {
        return V9X_M64_REFUSE_SCISSOR;
    }
    if (request->write_mask != R2_WRITE_MASK_RGB) {
        return V9X_M64_REFUSE_WRITE_MASK;
    }
    if (request->shade_mode != R2_SHADE_FLAT &&
        request->shade_mode != R2_SHADE_GOURAUD) {
        return V9X_M64_REFUSE_SHADE;
    }
    if (request->depth_enable != 0ul &&
        (request->depth_bits != R2_DEPTH_BITS || request->depth_func == 0ul ||
         request->depth_func > R2_CMP_ALWAYS)) {
        return V9X_M64_REFUSE_DEPTH;
    }
    if (request->specular_enable != 0ul) {
        return V9X_M64_REFUSE_SPECULAR;
    }
    if (request->color_key_enable != 0ul) {
        return V9X_M64_REFUSE_COLOR_KEY;
    }
    if (request->alpha_force != 0ul) {
        return V9X_M64_REFUSE_ALPHA_FORCE;
    }

    scale = request->textured != 0ul
        ? V9X_R2_SCALE_3D_TEXTURE | V9X_R2_TEX_CACHE_DIS |
              V9X_R2_MIP_MAP_DISABLE
        : V9X_R2_SCALE_3D_SHADE;

    if (request->textured != 0ul) {
        v9x_u32 mag = request->texture_mag_filter;
        v9x_u32 min = r2_base_filter(request->texture_min_filter);
        v9x_u32 format = r2_texture_format(request->texture_format);
        int has_alpha = format == V9X_R2_TEX_FORMAT_1555 ||
                        format == V9X_R2_TEX_FORMAT_4444;

        if (format == 0ul) {
            return V9X_M64_REFUSE_TEXTURE_FORMAT;
        }
        if (!r2_pow2_in_range(request->texture_width) ||
            !r2_pow2_in_range(request->texture_height)) {
            return V9X_M64_REFUSE_TEXTURE_SHAPE;
        }
        /*
         * Magnifying, BILINEAR_TEX_EN alone decides; minifying,
         * TEX_BLEND_FCN alone (B3, B8-B10, L1-L10). Nearest magnification
         * with bilinear minification draws nothing when it magnifies (B4),
         * so that pair is refused.
         */
        if ((mag != R2_FILTER_NEAREST && mag != R2_FILTER_LINEAR) ||
            (min != R2_FILTER_NEAREST && min != R2_FILTER_LINEAR) ||
            (mag == R2_FILTER_NEAREST && min == R2_FILTER_LINEAR)) {
            return V9X_M64_REFUSE_TEXTURE_FILTER;
        }
        if (mag == R2_FILTER_LINEAR) {
            scale |= V9X_R2_BILINEAR_TEX_EN;
        }
        if (min == R2_FILTER_LINEAR) {
            scale |= V9X_R2_TEX_BLEND_2X2;
        }
        /* Wrap is all the GT2C has (T4, T14, T15). */
        if (request->texture_wrap_u != 0ul || request->texture_wrap_v != 0ul) {
            return V9X_M64_REFUSE_TEXTURE_ADDRESS;
        }
        if (request->texture_address == R2_ADDRESS_CLAMP &&
            coords_in_unit != 0ul) {
            decision->clamp_in_unit = 1ul;
        } else if (request->texture_address != R2_ADDRESS_WRAP) {
            return V9X_M64_REFUSE_TEXTURE_ADDRESS;
        }

        /*
         * Direct3D's texture blends onto TEX_LIGHT_FCN (M1-M4). With
         * TEX_MAP_AEN the blend's source alpha is the texel's (A1), without
         * it the interpolator's (A5), which is Direct3D's choice for DECAL,
         * MODULATE and COPY. MODULATEALPHA multiplies the two, which the
         * chip cannot: it is accepted where one of them is 1.
         */
        switch (request->texture_op) {
        case R2_TEXOP_DECAL:
        case R2_TEXOP_COPY:
            texel_alpha = has_alpha;
            break;
        case R2_TEXOP_MODULATE:
            scale |= V9X_R2_TEX_LIGHT_MODULATE;
            texel_alpha = has_alpha;
            break;
        case R2_TEXOP_MODULATEALPHA:
            if (has_alpha && request->vertex_alpha_opaque == 0ul) {
                return V9X_M64_REFUSE_TEXTURE_OP;
            }
            scale |= V9X_R2_TEX_LIGHT_MODULATE;
            texel_alpha = has_alpha;
            break;
        case R2_TEXOP_DECALALPHA:
            /* Alpha decal needs TEX_MAP_AEN, which would also make the
             * blend read the texel's alpha where Direct3D wants the
             * vertex's: only without blending. */
            if (has_alpha) {
                if (request->blend_enable != 0ul) {
                    return V9X_M64_REFUSE_TEXTURE_OP;
                }
                scale |= V9X_R2_TEX_LIGHT_DECAL;
                texel_alpha = 1;
            }
            break;
        default:
            return V9X_M64_REFUSE_TEXTURE_OP;
        }
        decision->texture_format = format;
    }

    if (request->alpha_test_enable != 0ul &&
        request->alpha_func != R2_CMP_ALWAYS) {
        if (request->textured == 0ul ||
            decision->texture_format != V9X_R2_TEX_FORMAT_1555 ||
            !texel_alpha || !r2_alpha_test_is_mask(request)) {
            return V9X_M64_REFUSE_ALPHA_TEST;
        }
        scale |= V9X_R2_TEX_AMASK_AEN;
    }
    if (texel_alpha) {
        scale |= V9X_R2_TEX_MAP_AEN;
    }

    /* ONE / ZERO is no blend at all. */
    blending = request->blend_enable != 0ul &&
               !(request->src_blend == R2_BLEND_ONE &&
                 request->dst_blend == R2_BLEND_ZERO);
    if (blending) {
        v9x_u32 src = r2_source_factor(request->src_blend);
        v9x_u32 dst = r2_destination_factor(request->dst_blend);

        if (src == R2_FACTOR_NONE || dst == R2_FACTOR_NONE) {
            return V9X_M64_REFUSE_BLEND_FACTOR;
        }
        if (request->fog_enable != 0ul) {
            return V9X_M64_REFUSE_FOG_WITH_BLEND;
        }
        scale |= V9X_R2_ALPHA_FOG_BLEND |
                 (src << V9X_R2_BLEND_SRC_SHIFT) |
                 (dst << V9X_R2_BLEND_DST_SHIFT);
    } else if (request->fog_enable != 0ul) {
        /* Fog is the blend unit with the fog colour as the destination;
         * it needs As and 1-As, or it draws black (G1-G5). */
        scale |= V9X_R2_ALPHA_FOG_FOG |
                 (R2_FACTOR_ALPHA << V9X_R2_BLEND_SRC_SHIFT) |
                 (R2_FACTOR_INV_ALPHA << V9X_R2_BLEND_DST_SHIFT);
        decision->alpha_from_fog = 1ul;
    }

    decision->scale_3d_cntl = scale;
    return V9X_M64_REFUSE_NONE;
}

/* ---- State -------------------------------------------------------------- */

static v9x_u32 r2_off_pitch(v9x_u32 pitch_bytes, v9x_u32 offset)
{
    return (((pitch_bytes >> 1) >> 3) << 22) | (offset >> 3);
}

v9x_status v9x_r2_build_draw_state(const struct v9x_r2_draw_state *state,
                                   const struct v9x_r2_draw_decision *decision,
                                   v9x_u32 *offsets, v9x_u32 *values,
                                   v9x_u32 capacity, v9x_u32 *written)
{
    const struct v9x_r2_target *target;
    v9x_u32 at = 0ul;
    v9x_u32 pix_width = V9X_R2_DP_PIX_WIDTH_565;

    if (written != 0) {
        *written = 0ul;
    }
    if (state == 0 || decision == 0 || offsets == 0 || values == 0 ||
        written == 0 || capacity < V9X_R2_DRAW_STATE_DWORDS) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    target = &state->target;
    if (target->offset > target->vram_bytes ||
        target->height > (target->vram_bytes - target->offset) /
                             (target->pitch_bytes != 0ul
                                  ? target->pitch_bytes : 1ul) ||
        target->pitch_bytes == 0ul || (target->pitch_bytes & 15ul) != 0ul ||
        (target->offset & 7ul) != 0ul ||
        target->scissor_right >= target->width ||
        target->scissor_bottom >= target->height) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (state->depth_enable != 0ul &&
        ((state->depth_offset & 7ul) != 0ul ||
         state->depth_pitch_bytes < target->width * 2ul ||
         (state->depth_pitch_bytes & 15ul) != 0ul ||
         ((state->depth_pitch_bytes >> 1) >> 3) > 1023ul)) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (state->depth_enable != 0ul &&
        (state->depth_offset > target->vram_bytes ||
         target->height > (target->vram_bytes - state->depth_offset) /
                              state->depth_pitch_bytes)) {
        return V9X_STATUS_INSUFFICIENT_MEMORY;
    }
    if (state->textured != 0ul) {
        const struct v9x_r2_texture *texture = &state->texture;
        v9x_u32 map_bytes;

        if (texture->log2_width > V9X_R2_TEX_LEVEL_MAX ||
            texture->log2_height > V9X_R2_TEX_LEVEL_MAX ||
            texture->log2_pitch != texture->log2_width ||
            (texture->offset & 7ul) != 0ul) {
            return V9X_STATUS_INVALID_ARGUMENT;
        }
        map_bytes = (1ul << texture->log2_pitch) *
                    (1ul << texture->log2_height) * 2ul;
        if (texture->offset > target->vram_bytes ||
            map_bytes > target->vram_bytes - texture->offset) {
            return V9X_STATUS_INSUFFICIENT_MEMORY;
        }
        pix_width |= decision->texture_format << V9X_R2_TEX_FORMAT_SHIFT;
    }

    /* SCALE_3D_CNTL first: the accumulators may only be written with a
     * function selected (RRG p.6-7). */
    offsets[at] = V9X_M64_SCALE_3D_CNTL;  values[at++] = decision->scale_3d_cntl;
    offsets[at] = V9X_M64_DP_WRITE_MASK;  values[at++] = 0xfffffffful;
    offsets[at] = V9X_M64_DP_PIX_WIDTH;   values[at++] = pix_width;
    offsets[at] = V9X_M64_DP_MIX;         values[at++] = V9X_R2_DP_MIX_FRGD_SRC;
    offsets[at] = V9X_M64_DP_SRC;         values[at++] = V9X_R2_DP_SRC_3D;
    offsets[at] = V9X_M64_CLR_CMP_CNTL;   values[at++] = 0ul;
    offsets[at] = V9X_M64_DST_OFF_PITCH;
    values[at++] = r2_off_pitch(target->pitch_bytes, target->offset);
    offsets[at] = V9X_M64_SC_LEFT_RIGHT;
    values[at++] = (target->scissor_right << 16) | target->scissor_left;
    offsets[at] = V9X_M64_SC_TOP_BOTTOM;
    values[at++] = (target->scissor_bottom << 16) | target->scissor_top;
    if (state->depth_enable != 0ul) {
        offsets[at] = V9X_M64_Z_OFF_PITCH;
        values[at++] = r2_off_pitch(state->depth_pitch_bytes,
                                    state->depth_offset);
        offsets[at] = V9X_M64_Z_CNTL;
        values[at++] = V9X_R2_Z_EN |
                       ((state->depth_func - 1ul) << V9X_R2_Z_TEST_SHIFT) |
                       (state->depth_write != 0ul ? V9X_R2_Z_WRITE : 0ul);
    } else {
        offsets[at] = V9X_M64_Z_CNTL;
        values[at++] = 0ul;
    }
    if (state->textured != 0ul) {
        const struct v9x_r2_texture *texture = &state->texture;
        v9x_u32 level = texture->log2_width > texture->log2_height
            ? texture->log2_width : texture->log2_height;

        offsets[at] = V9X_R2_TEX_SIZE_PITCH;
        values[at++] = texture->log2_pitch | (level << 4) |
                       (texture->log2_height << 8);
        offsets[at] = V9X_R2_TEX_0_OFF + level * 4ul;
        values[at++] = texture->offset;
    }
    /* The fog colour is DP_FRGD_CLR read as ARGB8888 (G7). Irrelevant
     * with the 3D source otherwise. */
    offsets[at] = V9X_M64_DP_FRGD_CLR;
    values[at++] = state->fog_color & 0x00fffffful;
    *written = at;
    return V9X_STATUS_OK;
}

/* ---- Vertices and splitting -------------------------------------------- */

v9x_s32 v9x_r2_snap(v9x_s32 sixteenths)
{
    if (sixteenths < 0l) {
        return -v9x_r2_snap(-sixteenths);
    }
    return ((sixteenths + V9X_R2_DRAW_SNAP / 2l) / V9X_R2_DRAW_SNAP) *
           V9X_R2_DRAW_SNAP;
}

static v9x_u32 r2_mid_channel(v9x_u32 a, v9x_u32 b, v9x_u32 shift)
{
    return ((((a >> shift) & 0xfful) + ((b >> shift) & 0xfful) + 1ul) / 2ul)
           << shift;
}

/* The midpoint of an edge: screen-linear position, depth, colour, fog and
 * 1/w; perspective-correct texture coordinates. Exact in position because
 * the endpoints are on the snap grid and at most two splits are made. */
static void r2_midpoint(const struct v9x_r2_draw_vertex *a,
                        const struct v9x_r2_draw_vertex *b,
                        struct v9x_r2_draw_vertex *m)
{
    double weight = a->q + b->q;

    m->x = (a->x + b->x) / 2l;
    m->y = (a->y + b->y) / 2l;
    m->z = (a->z + b->z + 1ul) / 2ul;
    m->argb = r2_mid_channel(a->argb, b->argb, 24) |
              r2_mid_channel(a->argb, b->argb, 16) |
              r2_mid_channel(a->argb, b->argb, 8) |
              r2_mid_channel(a->argb, b->argb, 0);
    m->fog = (a->fog + b->fog + 1ul) / 2ul;
    m->q = weight / 2.0;
    m->tu = (a->tu * a->q + b->tu * b->q) / weight;
    m->tv = (a->tv * a->q + b->tv * b->q) / weight;
}

static int r2_needs_split(const struct v9x_r2_draw_state *state,
                          const struct v9x_r2_draw_vertex *v)
{
    struct v9x_r2_vertex positions[3];
    struct v9x_r2_tex_coord coords[3];
    double texels;
    v9x_u32 k;

    if (state->textured == 0ul ||
        (v[0].q == v[1].q && v[1].q == v[2].q)) {
        return 0;
    }
    for (k = 0ul; k < 3ul; ++k) {
        positions[k].x = v[k].x;
        positions[k].y = v[k].y;
        coords[k].tu = v[k].tu;
        coords[k].tv = v[k].tv;
        coords[k].q = v[k].q;
    }
    if (v9x_r2_texture_error(positions, coords, &state->texture, &texels) !=
            V9X_STATUS_OK) {
        return 0;
    }
    return texels * 1000.0 > (double)(v9x_s32)V9X_R2_DRAW_SPLIT_MILLI;
}

v9x_status v9x_r2_split_triangle(const struct v9x_r2_draw_state *state,
                                 const struct v9x_r2_draw_decision *decision,
                                 const struct v9x_r2_draw_vertex *vertices,
                                 struct v9x_r2_draw_vertex *pieces_out,
                                 v9x_u32 *pieces)
{
    /* A stack of (triangle, depth): at most 1 + 3 per split level open.
     * Static, as every large buffer in the HAL is (its frame audit fails
     * a frame over 2 KiB); the HAL is not reentrant. */
    static struct v9x_r2_draw_vertex stack[1u + 3u * V9X_R2_DRAW_SPLIT_DEPTH +
                                           1u][3];
    static unsigned int depth[1u + 3u * V9X_R2_DRAW_SPLIT_DEPTH + 1u];
    unsigned int top = 0u;
    v9x_u32 count = 0ul;

    if (pieces != 0) {
        *pieces = 0ul;
    }
    if (state == 0 || decision == 0 || vertices == 0 || pieces_out == 0 ||
        pieces == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    stack[0][0] = vertices[0];
    stack[0][1] = vertices[1];
    stack[0][2] = vertices[2];
    depth[0] = 0u;
    top = 1u;
    while (top > 0u) {
        struct v9x_r2_draw_vertex t[3];
        struct v9x_r2_draw_vertex m[3];
        unsigned int d;

        --top;
        t[0] = stack[top][0];
        t[1] = stack[top][1];
        t[2] = stack[top][2];
        d = depth[top];
        if (d >= V9X_R2_DRAW_SPLIT_DEPTH || !r2_needs_split(state, t)) {
            if (count >= V9X_R2_DRAW_SPLIT_MAX) {
                return V9X_STATUS_INTEGER_OVERFLOW;
            }
            pieces_out[count * 3ul] = t[0];
            pieces_out[count * 3ul + 1ul] = t[1];
            pieces_out[count * 3ul + 2ul] = t[2];
            ++count;
            continue;
        }
        /* Four by the edge midpoints, all wound as the parent. */
        r2_midpoint(&t[0], &t[1], &m[0]);
        r2_midpoint(&t[1], &t[2], &m[1]);
        r2_midpoint(&t[2], &t[0], &m[2]);
        stack[top][0] = t[0]; stack[top][1] = m[0]; stack[top][2] = m[2];
        depth[top++] = d + 1u;
        stack[top][0] = m[0]; stack[top][1] = t[1]; stack[top][2] = m[1];
        depth[top++] = d + 1u;
        stack[top][0] = m[2]; stack[top][1] = m[1]; stack[top][2] = t[2];
        depth[top++] = d + 1u;
        stack[top][0] = m[0]; stack[top][1] = m[1]; stack[top][2] = m[2];
        depth[top++] = d + 1u;
    }
    *pieces = count;
    return V9X_STATUS_OK;
}

/* ---- Trapezoids --------------------------------------------------------- */

/* One edge's walk over `rows` rows from (x, err): where it stands after,
 * as the engine walks it (2026-10-02-rage-iic-trapezoid-edge-model.md). */
static void r2_walk(v9x_s32 *x, v9x_s32 *err, v9x_s32 inc, v9x_s32 dec,
                    int rightward, v9x_u32 rows)
{
    v9x_u32 row;

    for (row = 0ul; row < rows; ++row) {
        unsigned int guard = 0u;

        while (dec != 0l && *err >= 0l && guard < 8192u) {
            *x += rightward ? 1l : -1l;
            *err += dec;
            ++guard;
        }
        *err += inc;
    }
}

v9x_status v9x_r2_split_trap(const struct v9x_r2_flat_trap *trap,
                             v9x_u32 rows_max,
                             struct v9x_r2_flat_trap *pieces,
                             v9x_u32 capacity, v9x_u32 *count)
{
    v9x_s32 lead_x;
    v9x_s32 lead_err;
    v9x_s32 trail_x;
    v9x_s32 trail_err;
    v9x_u32 done = 0ul;
    v9x_u32 n = 0ul;
    int lead_right;
    int trail_right;

    if (count != 0) {
        *count = 0ul;
    }
    if (trap == 0 || pieces == 0 || count == 0 || rows_max == 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    lead_x = (v9x_s32)trap->x;
    lead_err = trap->lead_err;
    trail_x = (v9x_s32)trap->trail_x;
    trail_err = trap->trail_err;
    lead_right = (trap->dst_cntl & V9X_M64_DST_X_DIR) != 0ul;
    trail_right = (trap->dst_cntl & V9X_R2_TRAIL_X_DIR) != 0ul;
    while (done < trap->length) {
        v9x_u32 rows = trap->length - done;

        if (rows > rows_max) {
            rows = rows_max;
        }
        if (n >= capacity || lead_x < 0l || trail_x < 0l) {
            return V9X_STATUS_INTEGER_OVERFLOW;
        }
        pieces[n] = *trap;
        pieces[n].x = (v9x_u32)lead_x;
        pieces[n].y = trap->y + done;
        pieces[n].length = rows;
        pieces[n].trail_x = (v9x_u32)trail_x;
        pieces[n].lead_err = lead_err;
        pieces[n].trail_err = trail_err;
        ++n;
        r2_walk(&lead_x, &lead_err, trap->lead_inc, trap->lead_dec,
                lead_right, rows);
        r2_walk(&trail_x, &trail_err, trap->trail_inc, trap->trail_dec,
                trail_right, rows);
        done += rows;
    }
    *count = n;
    return V9X_STATUS_OK;
}

/* ---- Packets ------------------------------------------------------------ */

union r2_double_bits {
    double value;
    v9x_u32 word[2];
};

/* The integer nearest `value` (|value| < 2^30), without the cast the HAL
 * has no runtime for (rage2_setup.c has the same). */
static int r2_round(double value, v9x_s32 *out)
{
    union r2_double_bits pun;

    if (!(value > -1073741824.0 && value < 1073741824.0)) {
        return 0;
    }
    pun.value = value + 6755399441055744.0;
    *out = (v9x_s32)pun.word[0];
    return 1;
}

/*
 * Z as a plane through the three vertices, in the S.16.12 fields: START at
 * the DST_Y_X pixel's centre plus a half for the engine's truncation,
 * X_INC per step in DST_X_DIR's direction, as colour (Phase 3).
 */
static int r2_depth_fields(const struct v9x_r2_draw_vertex *v,
                           const struct v9x_r2_flat_trap *trap,
                           v9x_u32 *fields)
{
    double x1 = (double)(v[1].x - v[0].x) / (double)V9X_R2_SUBPIXEL;
    double y1 = (double)(v[1].y - v[0].y) / (double)V9X_R2_SUBPIXEL;
    double x2 = (double)(v[2].x - v[0].x) / (double)V9X_R2_SUBPIXEL;
    double y2 = (double)(v[2].y - v[0].y) / (double)V9X_R2_SUBPIXEL;
    double d1 = (double)(v9x_s32)v[1].z - (double)(v9x_s32)v[0].z;
    double d2 = (double)(v9x_s32)v[2].z - (double)(v9x_s32)v[0].z;
    double det = x1 * y2 - x2 * y1;
    double gx;
    double gy;
    double ax;
    double ay;
    double start;
    v9x_s32 value;

    if (det == 0.0) {
        return 0;
    }
    gx = (d1 * y2 - d2 * y1) / det;
    gy = (d2 * x1 - d1 * x2) / det;
    if (!(gx < R2_Z_GRADIENT_LIMIT && gx > -R2_Z_GRADIENT_LIMIT &&
          gy < R2_Z_GRADIENT_LIMIT && gy > -R2_Z_GRADIENT_LIMIT)) {
        return 0;
    }
    ax = (double)(v9x_s32)trap->x + 0.5 -
         (double)v[0].x / (double)V9X_R2_SUBPIXEL;
    ay = (double)(v9x_s32)trap->y + 0.5 -
         (double)v[0].y / (double)V9X_R2_SUBPIXEL;
    start = (double)(v9x_s32)v[0].z + gx * ax + gy * ay + 0.5;
    if (start < 0.0) {
        start = 0.0;
    }
    if (start > 65535.5) {
        start = 65535.5;
    }
    if ((trap->dst_cntl & V9X_M64_DST_X_DIR) == 0ul) {
        gx = -gx;
    }
    if (!r2_round(start * R2_Z_SHIFT_FRACTION, &value)) {
        return 0;
    }
    fields[2] = (v9x_u32)value & R2_Z_FIELD_MASK;
    if (!r2_round(gx * R2_Z_SHIFT_FRACTION, &value)) {
        return 0;
    }
    fields[0] = (v9x_u32)value & R2_Z_FIELD_MASK;
    if (!r2_round(gy * R2_Z_SHIFT_FRACTION, &value)) {
        return 0;
    }
    fields[1] = (v9x_u32)value & R2_Z_FIELD_MASK;
    return 1;
}

v9x_status v9x_r2_build_piece(const struct v9x_r2_draw_state *state,
                              const struct v9x_r2_draw_decision *decision,
                              const struct v9x_r2_draw_vertex *vertices,
                              v9x_u32 *offsets, v9x_u32 *values,
                              v9x_u32 capacity, v9x_u32 *written,
                              struct v9x_r2_flat_trap *traps,
                              v9x_u32 *trap_count)
{
    static const v9x_u32 color_base[4] = {
        V9X_R2_RED_X_INC, V9X_R2_GREEN_X_INC, V9X_R2_BLUE_X_INC,
        V9X_R2_ALPHA_X_INC
    };
    struct v9x_r2_vertex positions[3];
    struct v9x_r2_tex_coord coords[3];
    struct v9x_r2_flat_trap whole[V9X_R2_SETUP_TRAPS];
    static struct v9x_r2_flat_trap pieces[V9X_R2_DRAW_TRAPS_MAX];
    v9x_u32 colors[3];
    v9x_u32 alphas[3];
    v9x_u32 whole_count = 0ul;
    v9x_u32 piece_count = 0ul;
    v9x_u32 at = 0ul;
    v9x_u32 index;
    v9x_u32 k;
    v9x_status status;

    if (written != 0) {
        *written = 0ul;
    }
    if (trap_count != 0) {
        *trap_count = 0ul;
    }
    if (state == 0 || decision == 0 || vertices == 0 || offsets == 0 ||
        values == 0 || written == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    for (k = 0ul; k < 3ul; ++k) {
        positions[k].x = vertices[k].x;
        positions[k].y = vertices[k].y;
        coords[k].tu = vertices[k].tu;
        coords[k].tv = vertices[k].tv;
        coords[k].q = vertices[k].q;
        colors[k] = vertices[k].argb & 0x00fffffful;
        /* The alpha interpolator carries the fog factor under fog, the
         * diffuse alpha otherwise; it rides in setup_shade's red slot. */
        alphas[k] = (decision->alpha_from_fog != 0ul
                         ? vertices[k].fog & 0xfful
                         : vertices[k].argb >> 24) << 16;
    }

    status = v9x_r2_setup_triangle(&state->target, positions, whole,
                                   &whole_count);
    if (status != V9X_STATUS_OK) {
        return status;
    }
    for (index = 0ul; index < whole_count; ++index) {
        v9x_u32 added = 0ul;

        status = v9x_r2_split_trap(&whole[index], V9X_R2_TRAP_LENGTH_MAX,
                                   pieces + piece_count,
                                   V9X_R2_DRAW_TRAPS_MAX - piece_count,
                                   &added);
        if (status != V9X_STATUS_OK) {
            return status;
        }
        piece_count += added;
    }
    if (capacity < piece_count * V9X_R2_DRAW_TRAP_DWORDS) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    for (index = 0ul; index < piece_count; ++index) {
        const struct v9x_r2_flat_trap *trap = &pieces[index];
        struct v9x_r2_shade shade;
        struct v9x_r2_shade alpha;
        v9x_u32 channel;
        v9x_u32 trap_written = 0ul;

        status = v9x_r2_setup_shade(positions, colors, trap, &shade);
        if (status == V9X_STATUS_OK) {
            status = v9x_r2_setup_shade(positions, alphas, trap, &alpha);
        }
        if (status != V9X_STATUS_OK) {
            return V9X_STATUS_UNSUPPORTED;
        }
        for (channel = 0ul; channel < 4ul; ++channel) {
            const struct v9x_r2_shade *source = channel < 3ul ? &shade
                                                              : &alpha;
            v9x_u32 slot = channel < 3ul ? channel : 0ul;

            offsets[at] = color_base[channel];
            values[at++] = (v9x_u32)source->x_inc[slot] & V9X_R2_COLOR_MASK;
            offsets[at] = color_base[channel] + 4ul;
            values[at++] = (v9x_u32)source->y_inc[slot] & V9X_R2_COLOR_MASK;
            offsets[at] = color_base[channel] + 8ul;
            values[at++] = (v9x_u32)source->start[slot] & V9X_R2_COLOR_MASK;
        }
        if (state->depth_enable != 0ul) {
            v9x_u32 fields[3];

            if (!r2_depth_fields(vertices, trap, fields)) {
                return V9X_STATUS_UNSUPPORTED;
            }
            offsets[at] = V9X_R2_Z_X_INC;  values[at++] = fields[0];
            offsets[at] = V9X_R2_Z_Y_INC;  values[at++] = fields[1];
            offsets[at] = V9X_R2_Z_START;  values[at++] = fields[2];
        }
        if (state->textured != 0ul) {
            struct v9x_r2_st st;
            v9x_u32 axis;

            status = v9x_r2_setup_texture(positions, coords, &state->texture,
                                          trap, &st, 0);
            if (status != V9X_STATUS_OK) {
                return V9X_STATUS_UNSUPPORTED;
            }
            for (axis = 0ul; axis < 2ul; ++axis) {
                v9x_u32 base = axis == 0ul ? V9X_R2_S_X_INC2
                                           : V9X_R2_T_X_INC2;

                offsets[at] = base;
                values[at++] = (v9x_u32)st.x_inc2[axis] & V9X_R2_ST_INC2_MASK;
                offsets[at] = base + 4ul;
                values[at++] = (v9x_u32)st.y_inc2[axis] & V9X_R2_ST_INC2_MASK;
                offsets[at] = base + 8ul;
                values[at++] = (v9x_u32)st.xy_inc2[axis] &
                               V9X_R2_ST_INC2_MASK;
                offsets[at] = base + 12ul;
                values[at++] = (v9x_u32)st.xinc_start[axis] &
                               V9X_R2_ST_INC_MASK;
                offsets[at] = base + 16ul;
                values[at++] = (v9x_u32)st.y_inc[axis] & V9X_R2_ST_INC_MASK;
                offsets[at] = base + 20ul;
                values[at++] = (v9x_u32)st.start[axis] & V9X_R2_ST_START_MASK;
            }
        }
        status = v9x_r2_build_trap(&state->target, trap, offsets + at,
                                   values + at, capacity - at, &trap_written);
        if (status != V9X_STATUS_OK) {
            return status;
        }
        at += trap_written;
        if (traps != 0) {
            traps[index] = *trap;
        }
    }
    *written = at;
    if (trap_count != 0) {
        *trap_count = piece_count;
    }
    return V9X_STATUS_OK;
}
