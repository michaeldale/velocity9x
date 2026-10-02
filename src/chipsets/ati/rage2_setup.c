/*
 * Rage II triangle setup: one screen-space triangle into at most two
 * trapezoids, split at the middle vertex.
 *
 * The coverage contract is centre sampling with the top-left rule: a pixel
 * is drawn when its centre is inside, and a centre exactly on an edge
 * belongs to a left or top edge only. In trapezoid terms, on row r with
 * centre r + 1/2:
 *
 *     leading X  = ceil(x_left(r + 1/2) - 1/2)    first pixel drawn
 *     trailing X = ceil(x_right(r + 1/2) - 1/2)   first pixel not drawn
 *
 * which is the engine's [leading, trailing) span with the fill to the right
 * (measured, docs\decisions\2026-10-02-rage-iic-trapezoid-edge-model.md).
 * Rows run from ceil(y_top - 1/2) to ceil(y_bottom - 1/2), exclusive.
 *
 * The engine walks each edge per row: step while the error is >= 0, adding
 * DEC; then add INC. So after k rows an edge has moved
 * floor((ERR + k*INC) / -DEC) + 1 pixels when that is positive. An edge's
 * exact X on row r0 + k is ceil(N(r0 + k) / Q), whose change from row r0
 * is floor((R + k*S*dxF) / (S*dyF)) for a remainder 0 <= R < Q. Dividing
 * out the common S gives DEC = -dyF, INC = dxF and
 * ERR = floor(R / S) - dyF, so the walk reproduces the exact X on every
 * row with terms inside the 18-bit fields. The host test checks this
 * against an independent per-pixel rasteriser (tests\host\test_rage2_setup.c).
 */
#include "velocity9x/ati_rage2.h"

#define V9X_R2_HALF (V9X_R2_SUBPIXEL / 2l)

/* floor(n / d) for d > 0, defined for negative n regardless of how the
 * compiler rounds signed division (C89 leaves it implementation-defined). */
static v9x_s32 v9x_r2_floor_div(v9x_s32 n, v9x_s32 d)
{
    v9x_s32 q;

    if (n >= 0l) {
        return n / d;
    }
    q = -((-n) / d);
    if (q * d != n) {
        --q;
    }
    return q;
}

static v9x_s32 v9x_r2_ceil_div(v9x_s32 n, v9x_s32 d)
{
    return -v9x_r2_floor_div(-n, d);
}

/* The first pixel row whose centre is at or below `y`. */
static v9x_s32 v9x_r2_row_at(v9x_s32 y)
{
    return v9x_r2_ceil_div(y - V9X_R2_HALF, V9X_R2_SUBPIXEL);
}

struct v9x_r2_edge_terms {
    v9x_u32 x;          /* X on the trapezoid's first row */
    v9x_s32 err;
    v9x_s32 inc;
    v9x_s32 dec;
    int rightward;
};

/*
 * Edge a -> b (a.y < b.y) evaluated from row r0, whose centre must lie in
 * [a.y, b.y). With dyF = b.y - a.y and Yc = S*r0 + S/2:
 *   N = (a.x - S/2)*dyF + (b.x - a.x)*(Yc - a.y),  Q = S*dyF,
 *   X = ceil(N / Q).
 * Each product is below 2047*16 * 2047*16 < 2^31 by the coordinate bound,
 * and the sum is bounded by the larger term because 0 <= Yc - a.y < dyF.
 */
static void v9x_r2_edge(const struct v9x_r2_vertex *a,
                        const struct v9x_r2_vertex *b,
                        v9x_s32 r0, struct v9x_r2_edge_terms *edge)
{
    v9x_s32 dyF = b->y - a->y;
    v9x_s32 dxF = b->x - a->x;
    v9x_s32 q = V9X_R2_SUBPIXEL * dyF;
    v9x_s32 yc = V9X_R2_SUBPIXEL * r0 + V9X_R2_HALF;
    v9x_s32 n = (a->x - V9X_R2_HALF) * dyF + dxF * (yc - a->y);
    v9x_s32 x0 = v9x_r2_ceil_div(n, q);
    v9x_s32 remainder;

    if (dxF >= 0l) {
        /* ceil(N/Q) = floor((N + Q - 1)/Q): R = N + Q - 1 - X*Q. */
        remainder = n + q - 1l - x0 * q;
        edge->rightward = 1;
        edge->inc = dxF;
    } else {
        /* Moving left: the steps are X(r0) - X(r0 + k), with
         * R = X*Q - N. */
        remainder = x0 * q - n;
        edge->rightward = 0;
        edge->inc = -dxF;
    }
    edge->x = (v9x_u32)x0;
    edge->dec = -dyF;
    edge->err = remainder / V9X_R2_SUBPIXEL - dyF;
}

static void v9x_r2_emit(const struct v9x_r2_vertex *left_a,
                        const struct v9x_r2_vertex *left_b,
                        const struct v9x_r2_vertex *right_a,
                        const struct v9x_r2_vertex *right_b,
                        v9x_s32 r0, v9x_s32 r1,
                        struct v9x_r2_flat_trap *trap)
{
    struct v9x_r2_edge_terms left;
    struct v9x_r2_edge_terms right;

    v9x_r2_edge(left_a, left_b, r0, &left);
    v9x_r2_edge(right_a, right_b, r0, &right);
    trap->x = left.x;
    trap->y = (v9x_u32)r0;
    trap->length = (v9x_u32)(r1 - r0);
    trap->trail_x = right.x;
    trap->lead_err = left.err;
    trap->lead_inc = left.inc;
    trap->lead_dec = left.dec;
    trap->trail_err = right.err;
    trap->trail_inc = right.inc;
    trap->trail_dec = right.dec;
    trap->dst_cntl = V9X_M64_DST_Y_DIR | V9X_R2_TRAP_FILL_DIR |
                     (left.rightward ? V9X_M64_DST_X_DIR : 0ul) |
                     (right.rightward ? V9X_R2_TRAIL_X_DIR : 0ul);
}

v9x_status v9x_r2_setup_triangle(const struct v9x_r2_target *target,
                                 const struct v9x_r2_vertex *vertices,
                                 struct v9x_r2_flat_trap *traps,
                                 v9x_u32 *count)
{
    const struct v9x_r2_vertex *v[3];
    const struct v9x_r2_vertex *swap;
    v9x_s32 max_x;
    v9x_s32 max_y;
    v9x_s32 cross;
    v9x_s32 r_top;
    v9x_s32 r_mid;
    v9x_s32 r_bottom;
    v9x_u32 index;
    v9x_u32 emitted = 0ul;

    if (count != 0) {
        *count = 0ul;
    }
    if (target == 0 || vertices == 0 || traps == 0 || count == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    max_x = (v9x_s32)target->width * V9X_R2_SUBPIXEL;
    max_y = (v9x_s32)target->height * V9X_R2_SUBPIXEL;
    if (target->width == 0ul || target->height == 0ul ||
        max_x > V9X_R2_SETUP_COORD_MAX + V9X_R2_SUBPIXEL ||
        max_y > V9X_R2_SETUP_COORD_MAX + V9X_R2_SUBPIXEL) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    for (index = 0ul; index < 3ul; ++index) {
        if (vertices[index].x < 0l || vertices[index].x > max_x ||
            vertices[index].y < 0l || vertices[index].y > max_y) {
            return V9X_STATUS_INVALID_ARGUMENT;
        }
        v[index] = &vertices[index];
    }

    /* Sort by y: v[0] top, v[2] bottom. */
    if (v[1]->y < v[0]->y) { swap = v[0]; v[0] = v[1]; v[1] = swap; }
    if (v[2]->y < v[1]->y) { swap = v[1]; v[1] = v[2]; v[2] = swap; }
    if (v[1]->y < v[0]->y) { swap = v[0]; v[0] = v[1]; v[1] = swap; }

    /* Which side of the long edge v0->v2 the middle vertex is on:
     * positive when it is to the left. Zero is no area, no pixels. */
    cross = (v[2]->x - v[0]->x) * (v[1]->y - v[0]->y) -
            (v[1]->x - v[0]->x) * (v[2]->y - v[0]->y);
    if (cross == 0l) {
        return V9X_STATUS_OK;
    }

    r_top = v9x_r2_row_at(v[0]->y);
    r_mid = v9x_r2_row_at(v[1]->y);
    r_bottom = v9x_r2_row_at(v[2]->y);

    if (r_mid > r_top) {
        if (cross > 0l) {
            v9x_r2_emit(v[0], v[1], v[0], v[2], r_top, r_mid,
                        &traps[emitted]);
        } else {
            v9x_r2_emit(v[0], v[2], v[0], v[1], r_top, r_mid,
                        &traps[emitted]);
        }
        ++emitted;
    }
    if (r_bottom > r_mid) {
        if (cross > 0l) {
            v9x_r2_emit(v[1], v[2], v[0], v[2], r_mid, r_bottom,
                        &traps[emitted]);
        } else {
            v9x_r2_emit(v[0], v[2], v[1], v[2], r_mid, r_bottom,
                        &traps[emitted]);
        }
        ++emitted;
    }
    *count = emitted;
    return V9X_STATUS_OK;
}
