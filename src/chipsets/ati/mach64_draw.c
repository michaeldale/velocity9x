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
/* Texture builder slots the composite adds to. */
#define M64_SLOT_PIX_WIDTH     13ul
#define M64_SLOT_TEX_SIZE      15ul
#define M64_SLOT_TEX_CNTL      16ul
#define M64_SLOT_SECONDARY_OFF 17ul

#define M64_FOG_COLOR_RGB     0x00fffffful
#define M64_SPECULAR_ALPHA    0xff000000ul
#define M64_SPECULAR_RGB      0x00fffffful
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

static v9x_u32 v9x_m64_draw_log2(v9x_u32 value)
{
    v9x_u32 result = 0ul;

    while (value > 1ul) {
        value >>= 1;
        ++result;
    }
    return result;
}

/*
 * The second texture onto a textured stream (Mesa 7.10 mach64_texstate.c,
 * the only driver that drove the composite): SCALE_3D_CNTL's blend function
 * becomes TRILINEAR with the texture cache split between the two, and the
 * second texture's format, size, combine and offset go in the words the
 * first leaves free. The checks are the texture builder's, for one level.
 */
static v9x_status v9x_m64_draw_composite(
                              const struct v9x_m64_draw_state *state,
                              v9x_u32 *values)
{
    v9x_u32 pix_width;
    v9x_u32 width_log2;
    v9x_u32 height_log2;
    v9x_u32 max_log2;
    v9x_u32 bytes;
    v9x_u32 end;
    v9x_u32 color_end;
    v9x_u32 control;

    if (state->textured == 0ul || state->bilinear_min > 1ul ||
        (state->composite_offset & (V9X_M64_TEXTURE_BASE_ALIGN - 1ul)) != 0ul ||
        state->composite_width < 2ul || state->composite_width > 1024ul ||
        state->composite_height < 2ul || state->composite_height > 1024ul ||
        (state->composite_width & (state->composite_width - 1ul)) != 0ul ||
        (state->composite_height & (state->composite_height - 1ul)) != 0ul ||
        state->composite_pitch_bytes != state->composite_width * 2ul ||
        state->composite_wrap_s > 1ul || state->composite_wrap_t > 1ul ||
        state->composite_bilinear_min > 1ul ||
        state->composite_bilinear_mag > 1ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    switch (state->composite_format) {
    case V9X_M64_TEXTURE_FORMAT_RGB565:
        pix_width = V9X_M64_SCALE_3D_TEXTURE_RGB565;
        break;
    case V9X_M64_TEXTURE_FORMAT_ARGB1555:
        pix_width = V9X_M64_SCALE_3D_TEXTURE_ARGB1555;
        break;
    case V9X_M64_TEXTURE_FORMAT_ARGB4444:
        pix_width = V9X_M64_SCALE_3D_TEXTURE_ARGB4444;
        break;
    default:
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    bytes = state->composite_pitch_bytes * state->composite_height;
    if (state->composite_offset > state->color.vram_bytes ||
        bytes > state->color.vram_bytes - state->composite_offset) {
        return V9X_STATUS_INSUFFICIENT_MEMORY;
    }
    end = state->composite_offset + bytes;
    color_end = state->color.target_offset +
                (state->color.target_height - 1ul) *
                state->color.target_pitch_bytes +
                state->color.target_width * 2ul;
    if (state->composite_offset < color_end &&
        state->color.target_offset < end) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    width_log2 = v9x_m64_draw_log2(state->composite_width);
    height_log2 = v9x_m64_draw_log2(state->composite_height);
    max_log2 = width_log2 > height_log2 ? width_log2 : height_log2;

    values[M64_SLOT_SCALE_3D] =
        (values[M64_SLOT_SCALE_3D] & ~V9X_M64_TEX_BLEND_FCN_MASK) |
        V9X_M64_TEX_BLEND_FCN_TRILINEAR | V9X_M64_TEX_CACHE_SPLIT;
    /* COMPOSITE_PIX_WIDTH takes the same datatype numbers as the first
     * texture's SCALE_PIX_WIDTH [31:28]. */
    values[M64_SLOT_PIX_WIDTH] =
        (values[M64_SLOT_PIX_WIDTH] & ~V9X_M64_COMPOSITE_PIX_WIDTH_MASK) |
        ((pix_width >> V9X_M64_SCALE_PIX_WIDTH_SHIFT) <<
         V9X_M64_COMPOSITE_PIX_WIDTH_SHIFT);
    values[M64_SLOT_TEX_SIZE] |= (width_log2 << 16) | (max_log2 << 20) |
                                 (height_log2 << 24);
    control = V9X_M64_TEXTURE_COMPOSITE | V9X_M64_COMP_COMBINE_MODULATE |
              V9X_M64_SECONDARY_STW;
    if (state->composite_wrap_s == 0ul) {
        control |= V9X_M64_SEC_TEX_CLAMP_S;
    }
    if (state->composite_wrap_t == 0ul) {
        control |= V9X_M64_SEC_TEX_CLAMP_T;
    }
    if (state->composite_bilinear_min != 0ul) {
        control |= V9X_M64_COMP_BLEND_BILINEAR;
    }
    if (state->composite_bilinear_mag != 0ul) {
        control |= V9X_M64_COMP_FILTER_BILINEAR;
    }
    values[M64_SLOT_TEX_CNTL] |= control;
    values[M64_SLOT_SECONDARY_OFF] = state->composite_offset;
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

    /* Fog is the blend unit mixing with DP_FOG_CLR, so it cannot blend as
     * well; the policy refuses that, and reaching here is a bug. Over a
     * texture it mixes the textured colour, as Mesa's driver fogs. */
    if (state->fog_enable != 0ul && state->blend_enable != 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    status = v9x_m64_draw_base(state, offsets, values, capacity, written);
    if (status != V9X_STATUS_OK) {
        return status;
    }

    if (state->composite != 0ul) {
        status = v9x_m64_draw_composite(state, values);
        if (status != V9X_STATUS_OK) {
            *written = 0ul;
            return status;
        }
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
    if (state->specular_enable != 0ul) {
        values[M64_SLOT_ALPHA_TST] |= V9X_M64_SPECULAR_LIGHT_EN;
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
    static const v9x_u32 secondary_registers[3][3] = {
        { V9X_M64_VERTEX_1_SECONDARY_S, V9X_M64_VERTEX_1_SECONDARY_T,
          V9X_M64_VERTEX_1_SECONDARY_W },
        { V9X_M64_VERTEX_2_SECONDARY_S, V9X_M64_VERTEX_2_SECONDARY_T,
          V9X_M64_VERTEX_2_SECONDARY_W },
        { V9X_M64_VERTEX_3_SECONDARY_S, V9X_M64_VERTEX_3_SECONDARY_T,
          V9X_M64_VERTEX_3_SECONDARY_W }
    };
    v9x_u32 specular = fog & (V9X_M64_SETUP_FOG | V9X_M64_SETUP_SPECULAR);
    v9x_u32 second = fog & V9X_M64_SETUP_SECONDARY;
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
             (specular != 0ul ? V9X_M64_SPECULAR_DWORDS : 0ul) +
             (second != 0ul ? V9X_M64_SECONDARY_DWORDS : 0ul);
    if (vertex == 0 || offsets == 0 || values == 0 || written == 0 ||
        capacity < needed || (second != 0ul && textured == 0ul)) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    for (index = 0ul; index < 3ul; ++index) {
        v = &vertex[index];
        if (v->x_fixed > V9X_M64_SETUP_COORD_MAX_FIXED ||
            v->y_fixed > V9X_M64_SETUP_COORD_MAX_FIXED ||
            v->z16 > 0xfffful) {
            return V9X_STATUS_INVALID_ARGUMENT;
        }
    }

    /* Bounded by the coordinate limit: |cross| < 2 * 16384^2. Area comes
     * before the texture values: 3DMark 99 sends zero-area triangles
     * whose unused corners carry rhw 0 (2026-09-29), and those draw
     * nothing whatever their coordinates. */
    dx1 = (v9x_s32)vertex[1].x_fixed - (v9x_s32)vertex[0].x_fixed;
    dy1 = (v9x_s32)vertex[1].y_fixed - (v9x_s32)vertex[0].y_fixed;
    dx2 = (v9x_s32)vertex[2].x_fixed - (v9x_s32)vertex[0].x_fixed;
    dy2 = (v9x_s32)vertex[2].y_fixed - (v9x_s32)vertex[0].y_fixed;
    cross = dx1 * dy2 - dy1 * dx2;
    if (cross == 0l) {
        return V9X_STATUS_UNSUPPORTED;
    }

    /* A non-finite or non-positive W, S or T names no texel. 3DMark 99
     * also sends NaN S and T on real triangles. Such a triangle cannot be
     * drawn, so it gets a status of its own: the caller skips it rather
     * than refusing the batch, which drops every other triangle too. */
    if (textured != 0ul) {
        for (index = 0ul; index < 3ul; ++index) {
            v = &vertex[index];
            if (!v9x_m64_draw_finite(v->rhw) || !(v->rhw > 0.0f) ||
                !v9x_m64_draw_finite(v->s) || !v9x_m64_draw_finite(v->t)) {
                return V9X_STATUS_INVALID_STATE;
            }
            if (second != 0ul &&
                (!v9x_m64_draw_finite(v->s1) || !v9x_m64_draw_finite(v->t1))) {
                return V9X_STATUS_INVALID_STATE;
            }
        }
    }

    /* Item 12's order: the three specular words, then the vertices. Fog
     * reads their alpha, specular their RGB (ALPHA_TST_CNTL
     * SPECULAR_LIGHT_EN); a part that is not asked for is sent as zero. */
    if (specular != 0ul) {
        for (index = 0ul; index < 3ul; ++index) {
            offsets[at] = specular_registers[index];
            values[at] = vertex[index].specular &
                (((fog & V9X_M64_SETUP_FOG) != 0ul ? M64_SPECULAR_ALPHA : 0ul) |
                 ((fog & V9X_M64_SETUP_SPECULAR) != 0ul
                    ? M64_SPECULAR_RGB : 0ul));
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
        /* The second texture's S and T under the same W, after the
         * vertex's own words as Mesa's mach64_tris.c sends them. */
        if (second != 0ul) {
            offsets[at] = secondary_registers[index][0];
            values[at++] = v9x_m64_draw_float_bits(v->s1);
            offsets[at] = secondary_registers[index][1];
            values[at++] = v9x_m64_draw_float_bits(v->t1);
            offsets[at] = secondary_registers[index][2];
            values[at++] = v9x_m64_draw_float_bits(v->rhw);
        }
    }
    offsets[at] = V9X_M64_ONE_OVER_AREA;
    values[at++] = v9x_m64_draw_float_bits(M64_FIXED_AREA_SCALE /
                                           (float)cross);
    *written = at;
    return V9X_STATUS_OK;
}

static v9x_u32 v9x_m64_setup_slot_equal(
                              const struct v9x_m64_setup_slot *slot,
                              const struct v9x_m64_setup_slot *candidate,
                              v9x_u32 fog)
{
    v9x_u32 index;

    if (slot->known == 0ul) {
        return V9X_FALSE;
    }
    for (index = 0ul; index < 6ul; ++index) {
        if (slot->word[index] != candidate->word[index]) {
            return V9X_FALSE;
        }
    }
    if ((fog & (V9X_M64_SETUP_FOG | V9X_M64_SETUP_SPECULAR)) != 0ul &&
        slot->specular != candidate->specular) {
        return V9X_FALSE;
    }
    if ((fog & V9X_M64_SETUP_SECONDARY) != 0ul) {
        for (index = 0ul; index < 3ul; ++index) {
            if (slot->secondary[index] != candidate->secondary[index]) {
                return V9X_FALSE;
            }
        }
    }
    return V9X_TRUE;
}

v9x_status v9x_m64_build_reused_setup(
                               const struct v9x_m64_setup_vertex *vertex,
                               v9x_u32 textured, v9x_u32 fog,
                               struct v9x_m64_setup_slot *slot,
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
    static const v9x_u32 secondary_registers[3][3] = {
        { V9X_M64_VERTEX_1_SECONDARY_S, V9X_M64_VERTEX_1_SECONDARY_T,
          V9X_M64_VERTEX_1_SECONDARY_W },
        { V9X_M64_VERTEX_2_SECONDARY_S, V9X_M64_VERTEX_2_SECONDARY_T,
          V9X_M64_VERTEX_2_SECONDARY_W },
        { V9X_M64_VERTEX_3_SECONDARY_S, V9X_M64_VERTEX_3_SECONDARY_T,
          V9X_M64_VERTEX_3_SECONDARY_W }
    };
    static const v9x_u8 permutations[6][3] = {
        { 0u, 1u, 2u }, { 0u, 2u, 1u }, { 1u, 0u, 2u },
        { 1u, 2u, 0u }, { 2u, 0u, 1u }, { 2u, 1u, 0u }
    };
    v9x_u32 specular = fog & (V9X_M64_SETUP_FOG | V9X_M64_SETUP_SPECULAR);
    v9x_u32 second = fog & V9X_M64_SETUP_SECONDARY;
    /* Words a vertex takes in the full packet: its six, and three more
     * for the second texture. */
    v9x_u32 stride = 6ul + (second != 0ul ? 3ul : 0ul);
    struct v9x_m64_setup_slot candidate[3];
    v9x_u32 full_offsets[V9X_M64_SETUP_DWORDS];
    v9x_u32 full_values[V9X_M64_SETUP_DWORDS];
    v9x_u32 full_written;
    v9x_u32 base;
    v9x_u32 best = 0ul;
    v9x_u32 best_matches = 0ul;
    v9x_u32 matches;
    v9x_u32 permutation;
    v9x_u32 index;
    v9x_u32 word;
    v9x_u32 needed = 1ul;
    v9x_u32 at = 0ul;
    v9x_s32 dx1;
    v9x_s32 dy1;
    v9x_s32 dx2;
    v9x_s32 dy2;
    v9x_s32 cross;
    v9x_status status;

    if (written != 0) {
        *written = 0ul;
    }
    if (vertex == 0 || slot == 0 || offsets == 0 || values == 0 ||
        written == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    status = v9x_m64_build_setup(vertex, textured, fog, full_offsets,
                                 full_values, V9X_M64_SETUP_DWORDS,
                                 &full_written);
    if (status != V9X_STATUS_OK) {
        return status;
    }

    base = specular != 0ul ? 3ul : 0ul;
    for (index = 0ul; index < 3ul; ++index) {
        for (word = 0ul; word < 6ul; ++word) {
            candidate[index].word[word] =
                full_values[base + index * stride + word];
        }
        for (word = 0ul; word < 3ul; ++word) {
            candidate[index].secondary[word] = second != 0ul
                ? full_values[base + index * stride + 6ul + word] : 0ul;
        }
        candidate[index].specular = specular != 0ul ? full_values[index] : 0ul;
        candidate[index].known = V9X_TRUE;
    }

    for (permutation = 0ul; permutation < 6ul; ++permutation) {
        matches = 0ul;
        for (index = 0ul; index < 3ul; ++index) {
            if (v9x_m64_setup_slot_equal(&slot[index],
                    &candidate[permutations[permutation][index]], fog)) {
                ++matches;
            }
        }
        if (matches > best_matches) {
            best_matches = matches;
            best = permutation;
        }
    }

    for (index = 0ul; index < 3ul; ++index) {
        if (!v9x_m64_setup_slot_equal(&slot[index],
                &candidate[permutations[best][index]], fog)) {
            needed += stride + (specular != 0ul ? 1ul : 0ul);
        }
    }
    if (capacity < needed) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    for (index = 0ul; index < 3ul; ++index) {
        const struct v9x_m64_setup_slot *source =
            &candidate[permutations[best][index]];
        if (!v9x_m64_setup_slot_equal(&slot[index], source, fog)) {
            if (specular != 0ul) {
                offsets[at] = specular_registers[index];
                values[at++] = source->specular;
            }
            for (word = 0ul; word < 6ul; ++word) {
                offsets[at] = registers[index][word];
                values[at++] = source->word[word];
            }
            if (second != 0ul) {
                for (word = 0ul; word < 3ul; ++word) {
                    offsets[at] = secondary_registers[index][word];
                    values[at++] = source->secondary[word];
                }
            }
            slot[index] = *source;
        }
    }

    dx1 = (v9x_s32)slot[1].word[5] / 0x10000l -
          (v9x_s32)slot[0].word[5] / 0x10000l;
    dy1 = (v9x_s32)(slot[1].word[5] & 0xfffful) -
          (v9x_s32)(slot[0].word[5] & 0xfffful);
    dx2 = (v9x_s32)slot[2].word[5] / 0x10000l -
          (v9x_s32)slot[0].word[5] / 0x10000l;
    dy2 = (v9x_s32)(slot[2].word[5] & 0xfffful) -
          (v9x_s32)(slot[0].word[5] & 0xfffful);
    cross = dx1 * dy2 - dy1 * dx2;
    offsets[at] = V9X_M64_ONE_OVER_AREA;
    values[at++] = v9x_m64_draw_float_bits(M64_FIXED_AREA_SCALE /
                                           (float)cross);
    *written = at;
    return V9X_STATUS_OK;
}
