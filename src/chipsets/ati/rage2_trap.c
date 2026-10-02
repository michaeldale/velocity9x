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

    /* Both edges start inside the target, and the rows the leading edge
     * can cover stay inside it whether the length counts scanlines or
     * steps: a step never advances Y by more than one. */
    if (trap->x >= target->width || trap->trail_x >= target->width ||
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
