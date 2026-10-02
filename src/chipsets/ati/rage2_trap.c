#include "velocity9x/ati_rage2.h"

/* The datapath values are the Mach64 fill's, which are measured on both
 * ATI parts: 565 destination, FRGD_MIX = source, DP_FRGD_CLR as source. */
#define V9X_R2_DP_PIX_WIDTH_565     0x00040004ul
#define V9X_R2_DP_MIX_FRGD_SRC      0x00070003ul
#define V9X_R2_DP_SRC_FRGD_CLR      0x00000100ul

static int v9x_r2_target_valid(const struct v9x_r2_target *target)
{
    v9x_u32 pitch_pixels;

    if (target == 0 || target->vram_bytes == 0ul ||
        (target->offset & 7ul) != 0ul || target->pitch_bytes == 0ul ||
        (target->pitch_bytes & 15ul) != 0ul || target->width == 0ul ||
        target->height == 0ul) {
        return 0;
    }
    pitch_pixels = target->pitch_bytes >> 1;
    if ((pitch_pixels >> 3) > 1023ul || target->width > pitch_pixels ||
        target->width - 1ul > V9X_R2_X_MAX) {
        return 0;
    }
    if (target->scissor_left > target->scissor_right ||
        target->scissor_top > target->scissor_bottom ||
        target->scissor_right >= target->width ||
        target->scissor_bottom >= target->height) {
        return 0;
    }
    return 1;
}

/* Whole target inside VRAM, without overflow. */
static v9x_status v9x_r2_target_fits(const struct v9x_r2_target *target)
{
    v9x_u32 rows = target->height;

    if (rows > (0xfffffffful - target->offset) / target->pitch_bytes) {
        return V9X_STATUS_INTEGER_OVERFLOW;
    }
    if (target->offset + rows * target->pitch_bytes > target->vram_bytes) {
        return V9X_STATUS_INSUFFICIENT_MEMORY;
    }
    return V9X_STATUS_OK;
}

static int v9x_r2_term_valid(v9x_s32 value)
{
    return value >= V9X_R2_BRES_MIN && value <= V9X_R2_BRES_MAX;
}

static v9x_u32 v9x_r2_term(v9x_s32 value)
{
    return (v9x_u32)value & V9X_R2_BRES_MASK;
}

v9x_status v9x_r2_build_flat_state(const struct v9x_r2_target *target,
                                   v9x_u32 color,
                                   v9x_u32 *offsets, v9x_u32 *values,
                                   v9x_u32 capacity, v9x_u32 *written)
{
    v9x_status status;

    if (written != 0) {
        *written = 0ul;
    }
    if (offsets == 0 || values == 0 || written == 0 ||
        capacity < V9X_R2_FLAT_STATE_DWORDS || !v9x_r2_target_valid(target)) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    status = v9x_r2_target_fits(target);
    if (status != V9X_STATUS_OK) {
        return status;
    }

    /* SCALE_3D_CNTL first: with SCALE_3D_FCN zero the span takes the 2D
     * datapath's source, and the accumulators are left alone (RRG p.6-7:
     * they may only be written with FCN non-zero). */
    offsets[0] = V9X_M64_SCALE_3D_CNTL;  values[0] = 0ul;
    offsets[1] = V9X_M64_Z_CNTL;         values[1] = 0ul;
    offsets[2] = V9X_M64_DP_WRITE_MASK;  values[2] = 0xfffffffful;
    offsets[3] = V9X_M64_DP_PIX_WIDTH;   values[3] = V9X_R2_DP_PIX_WIDTH_565;
    offsets[4] = V9X_M64_DP_MIX;         values[4] = V9X_R2_DP_MIX_FRGD_SRC;
    offsets[5] = V9X_M64_DP_SRC;         values[5] = V9X_R2_DP_SRC_FRGD_CLR;
    offsets[6] = V9X_M64_DP_FRGD_CLR;    values[6] = color;
    offsets[7] = V9X_M64_CLR_CMP_CNTL;   values[7] = 0ul;
    offsets[8] = V9X_M64_DST_OFF_PITCH;
    values[8] = (((target->pitch_bytes >> 1) >> 3) << 22) |
                (target->offset >> 3);
    offsets[9] = V9X_M64_SC_LEFT_RIGHT;
    values[9] = (target->scissor_right << 16) | target->scissor_left;
    offsets[10] = V9X_M64_SC_TOP_BOTTOM;
    values[10] = (target->scissor_bottom << 16) | target->scissor_top;
    *written = V9X_R2_FLAT_STATE_DWORDS;
    return V9X_STATUS_OK;
}

v9x_status v9x_r2_build_shade_state(const struct v9x_r2_target *target,
                                    const struct v9x_r2_shade *shade,
                                    v9x_u32 *offsets, v9x_u32 *values,
                                    v9x_u32 capacity, v9x_u32 *written)
{
    static const v9x_u32 channel_base[3] = {
        V9X_R2_RED_X_INC, V9X_R2_GREEN_X_INC, V9X_R2_BLUE_X_INC
    };
    v9x_status status;
    v9x_u32 channel;
    v9x_u32 at;

    if (written != 0) {
        *written = 0ul;
    }
    /* No range check on the values: the accumulators are modular, with a
     * 9-bit integer part (measured, ATIRX /shade G7-G8), so a START outside
     * 0..255 at an anchor no pixel uses is still exact where pixels are.
     * Masking to the field is the encoding. */
    if (offsets == 0 || values == 0 || written == 0 || shade == 0 ||
        capacity < V9X_R2_SHADE_STATE_DWORDS || !v9x_r2_target_valid(target)) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    status = v9x_r2_target_fits(target);
    if (status != V9X_STATUS_OK) {
        return status;
    }

    offsets[0] = V9X_M64_SCALE_3D_CNTL;  values[0] = V9X_R2_SCALE_3D_SHADE;
    offsets[1] = V9X_M64_Z_CNTL;         values[1] = 0ul;
    offsets[2] = V9X_M64_DP_WRITE_MASK;  values[2] = 0xfffffffful;
    offsets[3] = V9X_M64_DP_PIX_WIDTH;   values[3] = V9X_R2_DP_PIX_WIDTH_565;
    offsets[4] = V9X_M64_DP_MIX;         values[4] = V9X_R2_DP_MIX_FRGD_SRC;
    offsets[5] = V9X_M64_DP_SRC;         values[5] = V9X_R2_DP_SRC_3D;
    offsets[6] = V9X_M64_CLR_CMP_CNTL;   values[6] = 0ul;
    offsets[7] = V9X_M64_DST_OFF_PITCH;
    values[7] = (((target->pitch_bytes >> 1) >> 3) << 22) |
                (target->offset >> 3);
    offsets[8] = V9X_M64_SC_LEFT_RIGHT;
    values[8] = (target->scissor_right << 16) | target->scissor_left;
    offsets[9] = V9X_M64_SC_TOP_BOTTOM;
    values[9] = (target->scissor_bottom << 16) | target->scissor_top;
    /* One spare slot keeps the layout of the flat state for the shared
     * prefix; DP_FRGD_CLR is irrelevant with the 3D source. */
    offsets[10] = V9X_M64_DP_FRGD_CLR;   values[10] = 0ul;
    at = 11ul;
    for (channel = 0ul; channel < 3ul; ++channel) {
        offsets[at] = channel_base[channel];
        values[at] = (v9x_u32)shade->x_inc[channel] & V9X_R2_COLOR_MASK;
        ++at;
        offsets[at] = channel_base[channel] + 4ul;
        values[at] = (v9x_u32)shade->y_inc[channel] & V9X_R2_COLOR_MASK;
        ++at;
        offsets[at] = channel_base[channel] + 8ul;
        values[at] = (v9x_u32)shade->start[channel] & V9X_R2_COLOR_MASK;
        ++at;
    }
    *written = at;
    return V9X_STATUS_OK;
}

v9x_status v9x_r2_build_texture_state(const struct v9x_r2_target *target,
                                      const struct v9x_r2_texture *texture,
                                      const struct v9x_r2_st *st,
                                      v9x_u32 *offsets, v9x_u32 *values,
                                      v9x_u32 capacity, v9x_u32 *written)
{
    v9x_u32 level;
    v9x_u32 at;
    v9x_u32 axis;
    v9x_u32 map_bytes;
    v9x_status status;

    if (written != 0) {
        *written = 0ul;
    }
    if (offsets == 0 || values == 0 || written == 0 || texture == 0 ||
        st == 0 || capacity < V9X_R2_TEXTURE_STATE_DWORDS ||
        !v9x_r2_target_valid(target)) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (texture->log2_width > V9X_R2_TEX_LEVEL_MAX ||
        texture->log2_height > V9X_R2_TEX_LEVEL_MAX ||
        texture->log2_pitch > V9X_R2_TEX_LEVEL_MAX ||
        texture->log2_pitch != texture->log2_width ||
        (texture->offset & 7ul) != 0ul ||
        (texture->format != V9X_R2_TEX_FORMAT_565 &&
         texture->format != V9X_R2_TEX_FORMAT_1555 &&
         texture->format != V9X_R2_TEX_FORMAT_4444) ||
        (texture->scale_3d_extra & ~V9X_R2_TEX_EXTRA_MASK) != 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    /* The map must lie inside VRAM: 2^pitch * 2^height texels of 2 bytes. */
    map_bytes = (1ul << texture->log2_pitch) *
                (1ul << texture->log2_height) * 2ul;
    if (texture->offset > target->vram_bytes ||
        map_bytes > target->vram_bytes - texture->offset) {
        return V9X_STATUS_INSUFFICIENT_MEMORY;
    }
    status = v9x_r2_target_fits(target);
    if (status != V9X_STATUS_OK) {
        return status;
    }

    level = texture->log2_width > texture->log2_height
        ? texture->log2_width : texture->log2_height;
    offsets[0] = V9X_M64_SCALE_3D_CNTL;
    values[0] = V9X_R2_SCALE_3D_TEXTURE | V9X_R2_TEX_CACHE_DIS |
                V9X_R2_MIP_MAP_DISABLE | texture->scale_3d_extra;
    offsets[1] = V9X_M64_Z_CNTL;         values[1] = 0ul;
    offsets[2] = V9X_M64_DP_WRITE_MASK;  values[2] = 0xfffffffful;
    offsets[3] = V9X_M64_DP_PIX_WIDTH;
    values[3] = V9X_R2_DP_PIX_WIDTH_565 |
                (texture->format << V9X_R2_TEX_FORMAT_SHIFT);
    offsets[4] = V9X_M64_DP_MIX;         values[4] = V9X_R2_DP_MIX_FRGD_SRC;
    offsets[5] = V9X_M64_DP_SRC;         values[5] = V9X_R2_DP_SRC_3D;
    offsets[6] = V9X_M64_CLR_CMP_CNTL;   values[6] = 0ul;
    offsets[7] = V9X_M64_DST_OFF_PITCH;
    values[7] = (((target->pitch_bytes >> 1) >> 3) << 22) |
                (target->offset >> 3);
    offsets[8] = V9X_M64_SC_LEFT_RIGHT;
    values[8] = (target->scissor_right << 16) | target->scissor_left;
    offsets[9] = V9X_M64_SC_TOP_BOTTOM;
    values[9] = (target->scissor_bottom << 16) | target->scissor_top;
    offsets[10] = V9X_R2_TEX_SIZE_PITCH;
    values[10] = texture->log2_pitch | (level << 4) |
                 (texture->log2_height << 8);
    offsets[11] = V9X_R2_TEX_0_OFF + level * 4ul;
    values[11] = texture->offset;
    at = 12ul;
    for (axis = 0ul; axis < 2ul; ++axis) {
        v9x_u32 base = axis == 0ul ? V9X_R2_S_X_INC2 : V9X_R2_T_X_INC2;

        offsets[at] = base;
        values[at++] = (v9x_u32)st->x_inc2[axis] & V9X_R2_ST_INC2_MASK;
        offsets[at] = base + 4ul;
        values[at++] = (v9x_u32)st->y_inc2[axis] & V9X_R2_ST_INC2_MASK;
        offsets[at] = base + 8ul;
        values[at++] = (v9x_u32)st->xy_inc2[axis] & V9X_R2_ST_INC2_MASK;
        offsets[at] = base + 12ul;
        values[at++] = (v9x_u32)st->xinc_start[axis] & V9X_R2_ST_INC_MASK;
        offsets[at] = base + 16ul;
        values[at++] = (v9x_u32)st->y_inc[axis] & V9X_R2_ST_INC_MASK;
        offsets[at] = base + 20ul;
        values[at++] = (v9x_u32)st->start[axis] & V9X_R2_ST_START_MASK;
    }
    /* One spare: DP_FRGD_CLR, irrelevant with the 3D source, keeps the
     * count even for the 8-entry chunks. */
    offsets[at] = V9X_M64_DP_FRGD_CLR;
    values[at++] = 0ul;
    *written = at;
    return V9X_STATUS_OK;
}

/* 16.16 to the S.16.12 field: an arithmetic shift right by four, written
 * out because C89 leaves a signed right shift implementation-defined. */
static v9x_u32 v9x_r2_z_field(v9x_s32 value)
{
    v9x_u32 bits = (v9x_u32)value >> 4;

    if (value < 0l) {
        bits |= 0xf0000000ul;
    }
    return bits & V9X_R2_Z_MASK;
}

/* Z_CNTL bits that read back on A8U4I5: 0, 1, 2, 6:4, 8. */
#define V9X_R2_Z_CNTL_IMPLEMENTED   0x00000177ul

v9x_status v9x_r2_build_z_state(const struct v9x_r2_target *target,
                                const struct v9x_r2_depth *depth,
                                v9x_u32 *offsets, v9x_u32 *values,
                                v9x_u32 capacity, v9x_u32 *written)
{
    struct v9x_r2_target z_surface;
    v9x_status status;

    if (written != 0) {
        *written = 0ul;
    }
    if (offsets == 0 || values == 0 || written == 0 || depth == 0 ||
        capacity < V9X_R2_Z_STATE_DWORDS || !v9x_r2_target_valid(target) ||
        (depth->z_cntl & ~V9X_R2_Z_CNTL_IMPLEMENTED) != 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    /* The Z surface is the target's shape at its own offset. */
    z_surface = *target;
    z_surface.offset = depth->offset;
    if (!v9x_r2_target_valid(&z_surface)) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    status = v9x_r2_target_fits(&z_surface);
    if (status != V9X_STATUS_OK) {
        return status;
    }

    offsets[0] = V9X_M64_Z_OFF_PITCH;
    values[0] = (((target->pitch_bytes >> 1) >> 3) << 22) |
                (depth->offset >> 3);
    offsets[1] = V9X_M64_Z_CNTL;   values[1] = depth->z_cntl;
    offsets[2] = V9X_R2_Z_X_INC;   values[2] = v9x_r2_z_field(depth->x_inc);
    offsets[3] = V9X_R2_Z_Y_INC;   values[3] = v9x_r2_z_field(depth->y_inc);
    offsets[4] = V9X_R2_Z_START;   values[4] = v9x_r2_z_field(depth->start);
    *written = V9X_R2_Z_STATE_DWORDS;
    return V9X_STATUS_OK;
}

v9x_status v9x_r2_build_trap(const struct v9x_r2_target *target,
                             const struct v9x_r2_flat_trap *trap,
                             v9x_u32 *offsets, v9x_u32 *values,
                             v9x_u32 capacity, v9x_u32 *written)
{
    if (written != 0) {
        *written = 0ul;
    }
    if (offsets == 0 || values == 0 || written == 0 || trap == 0 ||
        capacity < V9X_R2_TRAP_DWORDS || !v9x_r2_target_valid(target)) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    /* Both edges start inside the target or on its right border: a span
     * is [leading, trailing), so X equal to the width draws nothing past
     * it. The length counts rows (measured), so the rows stay inside. */
    if (trap->x > target->width || trap->trail_x > target->width ||
        trap->trail_x > V9X_R2_X_MAX ||
        trap->y >= target->height || trap->length == 0ul ||
        trap->length > V9X_R2_TRAP_LENGTH_MAX ||
        trap->length > target->height - trap->y) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (!v9x_r2_term_valid(trap->lead_err) ||
        !v9x_r2_term_valid(trap->lead_inc) ||
        !v9x_r2_term_valid(trap->lead_dec) ||
        !v9x_r2_term_valid(trap->trail_err) ||
        !v9x_r2_term_valid(trap->trail_inc) ||
        !v9x_r2_term_valid(trap->trail_dec)) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if ((trap->dst_cntl & ~V9X_R2_TRAP_DST_CNTL_MASK) != 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    offsets[0] = V9X_M64_DST_CNTL;       values[0] = trap->dst_cntl;
    offsets[1] = V9X_M64_DST_Y_X;
    values[1] = (trap->x << 16) | trap->y;
    offsets[2] = V9X_M64_DST_BRES_ERR;   values[2] = v9x_r2_term(trap->lead_err);
    offsets[3] = V9X_M64_DST_BRES_INC;   values[3] = v9x_r2_term(trap->lead_inc);
    offsets[4] = V9X_M64_DST_BRES_DEC;   values[4] = v9x_r2_term(trap->lead_dec);
    offsets[5] = V9X_R2_TRAIL_BRES_ERR;  values[5] = v9x_r2_term(trap->trail_err);
    offsets[6] = V9X_R2_TRAIL_BRES_INC;  values[6] = v9x_r2_term(trap->trail_inc);
    offsets[7] = V9X_R2_TRAIL_BRES_DEC;  values[7] = v9x_r2_term(trap->trail_dec);
    /* Last, and the only write that starts anything (RRG p.4-46). */
    offsets[8] = V9X_R2_DST_BRES_LNTH;
    values[8] = V9X_R2_LNTH_LOAD_TRAIL | V9X_R2_LNTH_DRAW_TRAP |
                (trap->trail_x << V9X_R2_LNTH_TRAIL_X_SHIFT) |
                (trap->length & V9X_R2_LNTH_LENGTH_MASK);
    *written = V9X_R2_TRAP_DWORDS;
    return V9X_STATUS_OK;
}
