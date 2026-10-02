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

/* ---- Gouraud ------------------------------------------------------------ */

/*
 * round(num * 4096 / den) for den > 0, |num / den| < 2^19, as a
 * bit-serial long division: the remainder stays below den < 2^31, so
 * doubling it never overflows 32 bits. Integer only, like the rest of the
 * setup: the HAL links no runtime, Open Watcom lowers a float-to-int cast
 * to __CHP, and the host tests also build with MSVC
 * (src\display32\d3d\d3d_raster.h).
 */
static v9x_s32 v9x_r2_ratio12(v9x_s32 num, v9x_u32 den)
{
    v9x_u32 a = num < 0l ? (v9x_u32)(-num) : (v9x_u32)num;
    v9x_u32 whole = a / den;
    v9x_u32 rest = a % den;
    v9x_u32 frac = 0ul;
    unsigned int bit;
    v9x_s32 result;

    /* Thirteen fraction bits: twelve kept, one to round on. */
    for (bit = 0u; bit < 13u; ++bit) {
        rest <<= 1;
        frac <<= 1;
        if (rest >= den) {
            rest -= den;
            frac |= 1ul;
        }
    }
    result = (v9x_s32)((whole << 12) + ((frac + 1ul) >> 1));
    return num < 0l ? -result : result;
}

v9x_u32 v9x_r2_channel_out(v9x_s32 accumulator)
{
    v9x_u32 integer = ((v9x_u32)accumulator >> 16) & 0x1fful;

    if (integer < 256ul) {
        return integer;
    }
    return integer < 384ul ? 255ul : 0ul;
}

v9x_status v9x_r2_setup_shade(const struct v9x_r2_vertex *vertices,
                              const v9x_u32 *colors,
                              const struct v9x_r2_flat_trap *trap,
                              struct v9x_r2_shade *shade)
{
    v9x_s32 x1;
    v9x_s32 y1;
    v9x_s32 x2;
    v9x_s32 y2;
    v9x_s32 det;
    v9x_u32 det_abs;
    v9x_s32 anchor_dx;
    v9x_s32 anchor_dy;
    v9x_u32 channel;

    if (vertices == 0 || colors == 0 || trap == 0 || shade == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    /* Everything relative to vertex 0, in sixteenths of a pixel. With
     * coordinates under 2048 pixels every product below is under 2^30
     * and the determinant's two terms under 2^31 apart. */
    x1 = vertices[1].x - vertices[0].x;
    y1 = vertices[1].y - vertices[0].y;
    x2 = vertices[2].x - vertices[0].x;
    y2 = vertices[2].y - vertices[0].y;
    det = x1 * y2 - x2 * y1;
    if (det == 0l) {
        return V9X_STATUS_UNSUPPORTED;
    }
    det_abs = det < 0l ? (v9x_u32)(-det) : (v9x_u32)det;
    /* The DST_Y_X pixel's centre, from vertex 0. */
    anchor_dx = (v9x_s32)trap->x * V9X_R2_SUBPIXEL + V9X_R2_HALF -
                vertices[0].x;
    anchor_dy = (v9x_s32)trap->y * V9X_R2_SUBPIXEL + V9X_R2_HALF -
                vertices[0].y;

    for (channel = 0ul; channel < 3ul; ++channel) {
        v9x_u32 shift = 16ul - 8ul * channel;   /* red, green, blue */
        v9x_s32 c0 = (v9x_s32)((colors[0] >> shift) & 0xfful);
        v9x_s32 d1 = (v9x_s32)((colors[1] >> shift) & 0xfful) - c0;
        v9x_s32 d2 = (v9x_s32)((colors[2] >> shift) & 0xfful) - c0;
        /* Per pixel: (d1*y2 - d2*y1) * 16 / det, likewise for y. Each
         * numerator is under 255 * 2^15 * 2 * 16 < 2^28. */
        v9x_s32 num_x = (d1 * y2 - d2 * y1) * V9X_R2_SUBPIXEL;
        v9x_s32 num_y = (d2 * x1 - d1 * x2) * V9X_R2_SUBPIXEL;
        v9x_s32 gx;
        v9x_s32 gy;
        v9x_u32 start;

        if (det < 0l) {
            num_x = -num_x;
            num_y = -num_y;
        }
        /* The increments are S.8.12: below 256 levels a pixel. */
        if ((v9x_u32)(num_x < 0l ? -num_x : num_x) / det_abs >= 256ul ||
            (v9x_u32)(num_y < 0l ? -num_y : num_y) / det_abs >= 256ul) {
            return V9X_STATUS_UNSUPPORTED;
        }
        gx = v9x_r2_ratio12(num_x, det_abs);   /* levels/pixel, x4096 */
        gy = v9x_r2_ratio12(num_y, det_abs);

        /*
         * START = c0 + gx*dx + gy*dy + 1/2, in 16.16, where dx and dy are
         * the anchor's offset in sixteenths: gx*16 is the 16.16 gradient
         * and the sixteenths cancel it. The colour field wraps at 2^25
         * (512 levels), so only the low 32 bits of each product matter,
         * and unsigned multiplication gives exactly those.
         */
        start = ((v9x_u32)c0 << 16) + 0x00008000ul +
                (v9x_u32)gx * (v9x_u32)anchor_dx +
                (v9x_u32)gy * (v9x_u32)anchor_dy;
        shade->start[channel] = (v9x_s32)start;
        /* X_INC is per step in DST_X_DIR's direction (RRG p.6-11), and
         * the span runs left to right whichever way the leading edge
         * leans. With DST_X_DIR clear the register holds the gradient
         * negated: measured on A8U4I5, boot 136, where the unnegated
         * form drew every left-leaning triangle's spans mirrored. */
        shade->x_inc[channel] =
            (trap->dst_cntl & V9X_M64_DST_X_DIR) != 0ul ? gx * 16l
                                                        : -gx * 16l;
        shade->y_inc[channel] = gy * 16l;
    }
    return V9X_STATUS_OK;
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
