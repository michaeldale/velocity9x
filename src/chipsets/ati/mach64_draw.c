#include "velocity9x/ati_mach64_engine.h"

/*
 * The Mobility-M draw stream: one accepted draw's state, and one triangle's
 * setup packet.
 *
 * The state is not new arithmetic. It is the proven flat/Gouraud or texture
 * state builder's stream, with the depth builder's two Z words copied in and
 * the SCALE_3D_CNTL, ALPHA_TST_CNTL and DP_FOG_CLR slots set from the
 * encodings the Phase 4 scenes measured. Building on those builders is what
 * keeps every single-feature draw identical to a stream the Gateway ran;
 * tests\host\test_mach64_draw.c holds it to that.
 */

/* Every field SCALE_3D_CNTL carries for blending, fog and the texture
 * stage; cleared before the draw's own values go in. */
#define M64_SCALE_DRAW_FIELDS \
    (V9X_M64_ALPHA_BLEND_SOURCE_MASK | V9X_M64_ALPHA_BLEND_DEST_MASK | \
     V9X_M64_ALPHA_BLEND_ENABLE | V9X_M64_ALPHA_FOG_EN_FOG | \
     V9X_M64_ALPHA_BLEND_SATURATE | V9X_M64_TEX_LIGHT_FCN_MASK | \
     V9X_M64_TEX_MAP_AEN)

/* State slots shared by the flat and texture builders. */
#define M64_SLOT_Z_OFF_PITCH  7ul
#define M64_SLOT_Z_CNTL       8ul
#define M64_SLOT_ALPHA_TST    9ul
#define M64_SLOT_SCALE_3D     10ul
#define M64_SLOT_FOG_CLR      11ul

#define M64_FOG_COLOR_RGB     0x00fffffful
#define M64_SPECULAR_ALPHA    0xff000000ul
#define M64_FLOAT_ONE_BITS    0x3f800000ul
/* 14.2 coordinates: the pixel cross product is the fixed one over 16. */
#define M64_FIXED_AREA_SCALE  16.0f

static v9x_u32 v9x_m64_draw_float_bits(float value)
{
    union {
        float value;
        v9x_u32 bits;
    } converted;

    converted.value = value;
    return converted.bits;
}

static int v9x_m64_draw_finite(float value)
{
    return (v9x_m64_draw_float_bits(value) & 0x7f800000ul) != 0x7f800000ul;
}

/* The depth builder validates the Z surface against the target; only its
 * two Z words are taken. */
static v9x_status v9x_m64_draw_depth_words(
                              const struct v9x_m64_draw_state *state,
                              v9x_u32 *z_off_pitch, v9x_u32 *z_cntl)
{
    struct v9x_m64_depth_state depth;
    v9x_u32 offsets[V9X_M64_FLAT_STATE_DWORDS];
    v9x_u32 values[V9X_M64_FLAT_STATE_DWORDS];
    v9x_u32 written;
    v9x_status status;

    depth.color = state->color;
    depth.depth_offset = state->depth_offset;
    depth.depth_pitch_bytes = state->depth_pitch_bytes;
    depth.depth_width = state->color.target_width;
    depth.depth_height = state->color.target_height;
    depth.compare = state->depth_compare;
    depth.write_enable = state->depth_write;
    status = v9x_m64_build_depth_state(&depth, offsets, values,
                                       V9X_M64_FLAT_STATE_DWORDS, &written);
    if (status != V9X_STATUS_OK) {
        return status;
    }
    *z_off_pitch = values[M64_SLOT_Z_OFF_PITCH];
    *z_cntl = values[M64_SLOT_Z_CNTL];
    return V9X_STATUS_OK;
}

static v9x_status v9x_m64_draw_base(const struct v9x_m64_draw_state *state,
                                    v9x_u32 *offsets, v9x_u32 *values,
                                    v9x_u32 capacity, v9x_u32 *written)
{
    struct v9x_m64_texture_state texture;
    v9x_u32 level;

    /*
     * Gouraud setup always: a flat Direct3D draw arrives with three equal
     * colours, so the one measured interpolating SETUP_CNTL serves both, and
     * the provoking-vertex question never reaches the hardware.
     */
    if (state->textured == 0ul) {
        return v9x_m64_build_gouraud_state(&state->color, offsets, values,
                                           capacity, written);
    }
    texture.color = state->color;
    texture.texture_offset = state->texture_offset;
    texture.texture_pitch_bytes = state->texture_pitch_bytes;
    texture.texture_width = state->texture_width;
    texture.texture_height = state->texture_height;
    texture.wrap_s = state->wrap_s;
    texture.wrap_t = state->wrap_t;
    texture.bilinear_min = state->bilinear_min;
    texture.bilinear_mag = state->bilinear_mag;
    texture.texture_format = state->texture_format;
    texture.level_count = state->level_count;
    for (level = 0ul; level < V9X_M64_TEXTURE_LEVELS_MAX; ++level) {
        texture.level_offsets[level] = state->level_offsets[level];
    }
    return v9x_m64_build_texture_state(&texture, offsets, values, capacity,
                                       written);
}

v9x_status v9x_m64_build_draw_state(
                              const struct v9x_m64_draw_state *state,
                              const struct v9x_m64_draw_decision *decision,
                              v9x_u32 *offsets, v9x_u32 *values,
                              v9x_u32 capacity, v9x_u32 *written)
{
    v9x_u32 scale;
    v9x_u32 word;
    v9x_u32 z_off_pitch;
    v9x_u32 z_cntl;
    v9x_status status;

    if (written != 0) {
        *written = 0ul;
    }
    if (state == 0 || decision == 0 || offsets == 0 || values == 0 ||
        written == 0 || capacity < V9X_M64_DRAW_STATE_DWORDS) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    /* Fog is the blend unit mixing with DP_FOG_CLR and was measured only
     * untextured; the policy refuses both, so reaching here is a bug. */
    if (state->fog_enable != 0ul &&
        (state->blend_enable != 0ul || state->textured != 0ul)) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    status = v9x_m64_draw_base(state, offsets, values, capacity, written);
    if (status != V9X_STATUS_OK) {
        return status;
    }

    if (state->depth_enable != 0ul) {
        status = v9x_m64_draw_depth_words(state, &z_off_pitch, &z_cntl);
        if (status != V9X_STATUS_OK) {
            *written = 0ul;
            return status;
        }
        values[M64_SLOT_Z_OFF_PITCH] = z_off_pitch;
        values[M64_SLOT_Z_CNTL] = z_cntl;
    }

    /* Texel alpha is the only measured alpha-test source (items 7, 8). */
    if (state->alpha_test_enable != 0ul) {
        status = v9x_m64_build_alpha_control(state->alpha_compare,
                                             state->alpha_reference, 0ul,
                                             &word);
        if (status != V9X_STATUS_OK) {
            *written = 0ul;
            return status;
        }
        values[M64_SLOT_ALPHA_TST] = word;
    }

    scale = values[M64_SLOT_SCALE_3D] & ~M64_SCALE_DRAW_FIELDS;
    if (state->textured != 0ul) {
        scale |= decision->light_fcn | decision->texture_alpha;
    }
    if (state->fog_enable != 0ul) {
        /* Item 12's word: ALPHA_FOG_EN=2 with SRCALPHA/INVSRCALPHA. */
        scale |= V9X_M64_ALPHA_FOG_EN_FOG |
                 V9X_M64_ALPHA_BLEND_SOURCE_SRC_ALPHA |
                 V9X_M64_ALPHA_BLEND_DEST_INV_SRC_ALPHA;
        values[M64_SLOT_FOG_CLR] = state->fog_color & M64_FOG_COLOR_RGB;
    } else if (state->blend_enable != 0ul) {
        status = v9x_m64_build_blend_control(state->src_blend,
                                             state->dst_blend, &word);
        if (status != V9X_STATUS_OK) {
            *written = 0ul;
            return status;
        }
        scale |= word;
    } else {
        scale |= V9X_M64_ALPHA_BLEND_SOURCE_ONE |
                 V9X_M64_ALPHA_BLEND_DEST_ZERO;
    }
    values[M64_SLOT_SCALE_3D] = scale;
    return V9X_STATUS_OK;
}

v9x_status v9x_m64_build_setup(const struct v9x_m64_setup_vertex *vertex,
                               v9x_u32 textured, v9x_u32 fog,
                               v9x_u32 *offsets, v9x_u32 *values,
                               v9x_u32 capacity, v9x_u32 *written)
{
    static const v9x_u32 registers[3][6] = {
        { V9X_M64_VERTEX_1_S, V9X_M64_VERTEX_1_T,
          V9X_M64_VERTEX_1_W, V9X_M64_VERTEX_1_Z,
          V9X_M64_VERTEX_1_ARGB, V9X_M64_VERTEX_1_X_Y },
        { V9X_M64_VERTEX_2_S, V9X_M64_VERTEX_2_T,
          V9X_M64_VERTEX_2_W, V9X_M64_VERTEX_2_Z,
          V9X_M64_VERTEX_2_ARGB, V9X_M64_VERTEX_2_X_Y },
        { V9X_M64_VERTEX_3_S, V9X_M64_VERTEX_3_T,
          V9X_M64_VERTEX_3_W, V9X_M64_VERTEX_3_Z,
          V9X_M64_VERTEX_3_ARGB, V9X_M64_VERTEX_3_X_Y }
    };
    static const v9x_u32 specular_registers[3] = {
        V9X_M64_VERTEX_1_SPEC_ARGB, V9X_M64_VERTEX_2_SPEC_ARGB,
        V9X_M64_VERTEX_3_SPEC_ARGB
    };
    v9x_u32 needed;
    v9x_u32 at = 0ul;
    v9x_u32 index;
    v9x_s32 dx1;
    v9x_s32 dy1;
    v9x_s32 dx2;
    v9x_s32 dy2;
    v9x_s32 cross;
    const struct v9x_m64_setup_vertex *v;

    if (written != 0) {
        *written = 0ul;
    }
    needed = V9X_M64_FLAT_TRIANGLE_DWORDS +
             (fog != 0ul ? V9X_M64_SPECULAR_DWORDS : 0ul);
    if (vertex == 0 || offsets == 0 || values == 0 || written == 0 ||
        capacity < needed) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    for (index = 0ul; index < 3ul; ++index) {
        v = &vertex[index];
        if (v->x_fixed > V9X_M64_SETUP_COORD_MAX_FIXED ||
            v->y_fixed > V9X_M64_SETUP_COORD_MAX_FIXED ||
            v->z16 > 0xfffful) {
            return V9X_STATUS_INVALID_ARGUMENT;
        }
        if (textured != 0ul &&
            (!v9x_m64_draw_finite(v->rhw) || !(v->rhw > 0.0f) ||
             !v9x_m64_draw_finite(v->s) || !v9x_m64_draw_finite(v->t))) {
            return V9X_STATUS_INVALID_ARGUMENT;
        }
    }

    /* Bounded by the coordinate limit: |cross| < 2 * 16384^2. */
    dx1 = (v9x_s32)vertex[1].x_fixed - (v9x_s32)vertex[0].x_fixed;
    dy1 = (v9x_s32)vertex[1].y_fixed - (v9x_s32)vertex[0].y_fixed;
    dx2 = (v9x_s32)vertex[2].x_fixed - (v9x_s32)vertex[0].x_fixed;
    dy2 = (v9x_s32)vertex[2].y_fixed - (v9x_s32)vertex[0].y_fixed;
    cross = dx1 * dy2 - dy1 * dx2;
    if (cross == 0l) {
        return V9X_STATUS_UNSUPPORTED;
    }

    /* Item 12's order: the three specular words, then the vertices. */
    if (fog != 0ul) {
        for (index = 0ul; index < 3ul; ++index) {
            offsets[at] = specular_registers[index];
            values[at] = vertex[index].specular & M64_SPECULAR_ALPHA;
            ++at;
        }
    }

    /*
     * S and T are the plain coordinates and W is D3D's rhw. TEX_CNTL leaves
     * bit 19 clear, TEX_ST_MULT_W (xf86-video-mach64 atiregs.h): the engine
     * multiplies S and T by W itself before it interpolates. That is the
     * mode Phase 4 item 5 measured with plain S and T and unequal W. Mesa's
     * driver premultiplied instead, but under TEX_ST_DIRECT. This builder
     * premultiplied under ST_MULT_W until 2026-09-29, which squared W: on
     * the Gateway a uniform rhw k scaled every coordinate by k and shifted
     * the mip level by log2 k, and 3DMark's tunnel drew smeared.
     */
    for (index = 0ul; index < 3ul; ++index) {
        v = &vertex[index];
        offsets[at] = registers[index][0];
        values[at++] = textured != 0ul
            ? v9x_m64_draw_float_bits(v->s) : 0ul;
        offsets[at] = registers[index][1];
        values[at++] = textured != 0ul
            ? v9x_m64_draw_float_bits(v->t) : 0ul;
        offsets[at] = registers[index][2];
        values[at++] = textured != 0ul
            ? v9x_m64_draw_float_bits(v->rhw) : M64_FLOAT_ONE_BITS;
        /* Z16 in [31:16], the placement item 3 measured. */
        offsets[at] = registers[index][3];
        values[at++] = v->z16 << 16;
        offsets[at] = registers[index][4];
        values[at++] = v->argb;
        offsets[at] = registers[index][5];
        values[at++] = (v->x_fixed << 16) | v->y_fixed;
    }
    offsets[at] = V9X_M64_ONE_OVER_AREA;
    values[at++] = v9x_m64_draw_float_bits(M64_FIXED_AREA_SCALE /
                                           (float)cross);
    *written = at;
    return V9X_STATUS_OK;
}
