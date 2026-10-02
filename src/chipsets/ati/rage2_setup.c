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

/* ---- Texture coordinates ------------------------------------------------ */

#define V9X_R2_ST_MODULUS_MASK  0x03fffffful
/* The accumulator keeps bits 25:5, as START does. */
#define V9X_R2_ST_RESOLUTION    0xffffffe0ul

/* A register field as the engine holds it: masked to its width, then
 * sign-extended from the top bit (the mask is 2^n - 1). Unsigned, so the
 * walk's sums wrap as the hardware's do. */
static v9x_u32 v9x_r2_st_field(v9x_s32 value, v9x_u32 mask)
{
    v9x_u32 bits = (v9x_u32)value & mask;
    v9x_u32 sign = (mask >> 1) + 1ul;

    return (bits & sign) != 0ul ? bits | ~mask : bits;
}

void v9x_r2_st_begin(const struct v9x_r2_st *st, struct v9x_r2_st_walk *w)
{
    v9x_u32 axis;

    for (axis = 0ul; axis < 2ul; ++axis) {
        w->value[axis] = (v9x_u32)st->start[axis] & V9X_R2_ST_START_MASK;
        w->x_inc[axis] = v9x_r2_st_field(st->xinc_start[axis],
                                         V9X_R2_ST_INC_MASK);
        w->y_inc[axis] = v9x_r2_st_field(st->y_inc[axis],
                                         V9X_R2_ST_INC_MASK);
    }
}

void v9x_r2_st_edge_step(const struct v9x_r2_st *st,
                         struct v9x_r2_st_walk *w)
{
    v9x_u32 axis;

    for (axis = 0ul; axis < 2ul; ++axis) {
        w->value[axis] = (w->value[axis] +
                          (w->x_inc[axis] & V9X_R2_ST_RESOLUTION)) &
                         V9X_R2_ST_MODULUS_MASK;
        w->x_inc[axis] += v9x_r2_st_field(st->x_inc2[axis],
                                          V9X_R2_ST_INC2_MASK);
        w->y_inc[axis] += v9x_r2_st_field(st->xy_inc2[axis],
                                          V9X_R2_ST_INC2_MASK);
    }
}

void v9x_r2_st_next_row(const struct v9x_r2_st *st,
                        struct v9x_r2_st_walk *w)
{
    v9x_u32 axis;

    for (axis = 0ul; axis < 2ul; ++axis) {
        w->value[axis] = (w->value[axis] +
                          (w->y_inc[axis] & V9X_R2_ST_RESOLUTION)) &
                         V9X_R2_ST_MODULUS_MASK;
        w->y_inc[axis] += v9x_r2_st_field(st->y_inc2[axis],
                                          V9X_R2_ST_INC2_MASK);
        w->x_inc[axis] += v9x_r2_st_field(st->xy_inc2[axis],
                                          V9X_R2_ST_INC2_MASK);
    }
}

void v9x_r2_st_pixel_step(const struct v9x_r2_st *st,
                          struct v9x_r2_st_walk *span, int forward)
{
    v9x_u32 axis;

    for (axis = 0ul; axis < 2ul; ++axis) {
        v9x_u32 inc2 = v9x_r2_st_field(st->x_inc2[axis],
                                       V9X_R2_ST_INC2_MASK);

        if (forward) {
            span->value[axis] += span->x_inc[axis] & V9X_R2_ST_RESOLUTION;
            span->x_inc[axis] += inc2;
        } else {
            /* Back one step, then the ones' complement of the increment
             * at 32-unit resolution: -(floor) - 32. */
            span->x_inc[axis] -= inc2;
            span->value[axis] -= (span->x_inc[axis] & V9X_R2_ST_RESOLUTION) +
                                 V9X_R2_ST_AGAINST_LOSS;
        }
        span->value[axis] &= V9X_R2_ST_MODULUS_MASK;
    }
}

v9x_u32 v9x_r2_texel_index(v9x_u32 value, v9x_u32 log2_size,
                           v9x_u32 log2_wrap)
{
    if (log2_size > V9X_R2_TEX_LEVEL_MAX) {
        return 0ul;
    }
    return ((value & V9X_R2_ST_MODULUS_MASK) >>
            (V9X_R2_ST_FRACTION_BITS - log2_size)) &
           ((1ul << log2_wrap) - 1ul);
}

/* IEEE-754 double, read through its words (x86: low word first). */
union v9x_r2_double_bits {
    double value;
    v9x_u32 word[2];
};

#define V9X_R2_DOUBLE_EXPONENT  0x7ff00000ul
/* 1.5 * 2^52: added to a double below 2^31 in magnitude, the integer
 * nearest it lands in the low mantissa word. */
#define V9X_R2_ROUND_MAGIC      6755399441055744.0
#define V9X_R2_ST_ONE_D         67108864.0          /* V9X_R2_ST_ONE */
/* Register ranges: S.11.16 increments (28 bits) and S.10.16 second
 * differences (27 bits), as read back, in the 2^26 unit. */
#define V9X_R2_ST_INC_LIMIT     134217727.0
#define V9X_R2_ST_INC2_LIMIT    67108863.0
#define V9X_R2_PROBE_STEPS      8ul

/* Whether `value` is finite, from its exponent: comparisons cannot be
 * trusted to find a NaN under Open Watcom (`x == x` is TRUE for one). */
static int v9x_r2_finite(double value)
{
    union v9x_r2_double_bits pun;

    pun.value = value;
    return (pun.word[1] & V9X_R2_DOUBLE_EXPONENT) != V9X_R2_DOUBLE_EXPONENT;
}

/* The integer nearest `value`, if it is finite and inside +-2^30. */
static int v9x_r2_round(double value, v9x_s32 *out)
{
    union v9x_r2_double_bits pun;

    if (!v9x_r2_finite(value) || value > 1073741824.0 ||
        value < -1073741824.0) {
        return 0;
    }
    pun.value = value + V9X_R2_ROUND_MAGIC;
    *out = (v9x_s32)pun.word[0];
    return 1;
}

/* Quadratic coefficients about the anchor: c[0] + c[1]x + c[2]y + c[3]x^2
 * + c[4]y^2 + c[5]xy, x and y in pixels. */
static void v9x_r2_add_product(double *c, double weight,
                               const double *a, const double *b)
{
    /* a and b are affine: a[0] + a[1]x + a[2]y. */
    c[0] += weight * a[0] * b[0];
    c[1] += weight * (a[0] * b[1] + a[1] * b[0]);
    c[2] += weight * (a[0] * b[2] + a[2] * b[0]);
    c[3] += weight * a[1] * b[1];
    c[4] += weight * a[2] * b[2];
    c[5] += weight * (a[1] * b[2] + a[2] * b[1]);
}

/* The quadratic for each axis, about an origin, in the engine's unit:
 * c[0] + c[1]x + c[2]y + c[3]x^2 + c[4]y^2 + c[5]xy, x and y in pixels.
 * `base` is vertex 0's value, taken out of the constant. */
struct v9x_r2_st_fit {
    double c[2][6];
    double base[2];
    double scale[2];            /* the engine's unit per tu, per tv */
    int affine;                 /* the plane, for a sliver */
    double texel;               /* one texel, 2^(26 - TEX_SIZE) */
};

/*
 * One axis's coefficients from the barycentrics: the quadratic
 * interpolant through `value` at the vertices and `middle` at the edge
 * midpoints (vertex i to i+1), or with `linear` the plane through the
 * vertex values alone.
 */
static void v9x_r2_fit_axis(double *c, double lambda[3][3],
                            const double *one, const double *value,
                            const double *middle, int linear)
{
    v9x_u32 vertex;
    v9x_u32 term;

    for (term = 0ul; term < 6ul; ++term) {
        c[term] = 0.0;
    }
    for (vertex = 0ul; vertex < 3ul; ++vertex) {
        v9x_u32 j = (vertex + 1ul) % 3ul;

        if (linear) {
            v9x_r2_add_product(c, value[vertex], lambda[vertex], one);
            continue;
        }
        v9x_r2_add_product(c, 2.0 * value[vertex], lambda[vertex],
                           lambda[vertex]);
        v9x_r2_add_product(c, -value[vertex], lambda[vertex], one);
        v9x_r2_add_product(c, 4.0 * middle[vertex], lambda[vertex],
                           lambda[j]);
    }
}

/*
 * The six-node fit: the exact perspective value at the vertices and at
 * the edge midpoints, where it is (s0 q0 + s1 q1) / (q0 + q1), joined by
 * the quadratic interpolant: lambda_i(2 lambda_i - 1) for a vertex and
 * 4 lambda_i lambda_j for a midpoint.
 */
static v9x_status v9x_r2_fit_st(const struct v9x_r2_vertex *vertices,
                                const struct v9x_r2_tex_coord *coords,
                                const struct v9x_r2_texture *texture,
                                double origin_x, double origin_y,
                                struct v9x_r2_st_fit *fit)
{
    double px[3];
    double py[3];
    double lambda[3][3];
    double one[3];
    double det;
    double q_max;
    double q[3];
    v9x_u32 log2_size;
    v9x_u32 vertex;
    v9x_u32 axis;

    if (vertices == 0 || coords == 0 || texture == 0 ||
        texture->log2_width > V9X_R2_TEX_LEVEL_MAX ||
        texture->log2_height > V9X_R2_TEX_LEVEL_MAX) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    q_max = 0.0;
    for (vertex = 0ul; vertex < 3ul; ++vertex) {
        if (!v9x_r2_finite(coords[vertex].q) || coords[vertex].q <= 0.0 ||
            !v9x_r2_finite(coords[vertex].tu) ||
            !v9x_r2_finite(coords[vertex].tv)) {
            return V9X_STATUS_UNSUPPORTED;
        }
        if (coords[vertex].q > q_max) {
            q_max = coords[vertex].q;
        }
    }

    /* 1.0 = 2^26 spans the larger dimension on both axes, so the shorter
     * axis's coordinate shrinks by the aspect ratio. */
    log2_size = texture->log2_width > texture->log2_height
        ? texture->log2_width : texture->log2_height;
    fit->scale[0] = V9X_R2_ST_ONE_D /
        (double)(v9x_s32)(1ul << (log2_size - texture->log2_width));
    fit->scale[1] = V9X_R2_ST_ONE_D /
        (double)(v9x_s32)(1ul << (log2_size - texture->log2_height));
    fit->texel = V9X_R2_ST_ONE_D / (double)(v9x_s32)(1ul << log2_size);

    /* Pixels about the origin; the barycentrics as affine functions. */
    for (vertex = 0ul; vertex < 3ul; ++vertex) {
        px[vertex] = (double)vertices[vertex].x /
                     (double)V9X_R2_SUBPIXEL - origin_x;
        py[vertex] = (double)vertices[vertex].y /
                     (double)V9X_R2_SUBPIXEL - origin_y;
        q[vertex] = coords[vertex].q / q_max;
    }
    det = (px[1] - px[0]) * (py[2] - py[0]) -
          (px[2] - px[0]) * (py[1] - py[0]);
    if (det == 0.0) {
        return V9X_STATUS_UNSUPPORTED;
    }
    for (vertex = 0ul; vertex < 3ul; ++vertex) {
        v9x_u32 i = (vertex + 1ul) % 3ul;
        v9x_u32 j = (vertex + 2ul) % 3ul;

        lambda[vertex][0] = (px[i] * py[j] - px[j] * py[i]) / det;
        lambda[vertex][1] = (py[i] - py[j]) / det;
        lambda[vertex][2] = (px[j] - px[i]) / det;
    }
    one[0] = 1.0;
    one[1] = 0.0;
    one[2] = 0.0;

    fit->affine = 0;
    for (axis = 0ul; axis < 2ul; ++axis) {
        double value[3];
        double middle[3];
        v9x_u32 j;

        for (vertex = 0ul; vertex < 3ul; ++vertex) {
            value[vertex] = (axis == 0ul ? coords[vertex].tu
                                         : coords[vertex].tv) *
                            fit->scale[axis];
        }
        /* The map repeats every 2^26, so only differences matter: take
         * vertex 0's value out to keep every term small. */
        fit->base[axis] = value[0];
        for (vertex = 0ul; vertex < 3ul; ++vertex) {
            value[vertex] -= fit->base[axis];
        }
        for (vertex = 0ul; vertex < 3ul; ++vertex) {
            j = (vertex + 1ul) % 3ul;
            middle[vertex] = (value[vertex] * q[vertex] + value[j] * q[j]) /
                             (q[vertex] + q[j]);
        }
        v9x_r2_fit_axis(fit->c[axis], lambda, one, value, middle, 0);
        /* The engine's second differences are 2 c_xx, 2 c_yy and c_xy,
         * whatever the anchor. */
        if (!v9x_r2_finite(fit->c[axis][3]) ||
            !v9x_r2_finite(fit->c[axis][4]) ||
            !v9x_r2_finite(fit->c[axis][5]) ||
            2.0 * fit->c[axis][3] > V9X_R2_ST_INC2_LIMIT ||
            2.0 * fit->c[axis][3] < -V9X_R2_ST_INC2_LIMIT ||
            2.0 * fit->c[axis][4] > V9X_R2_ST_INC2_LIMIT ||
            2.0 * fit->c[axis][4] < -V9X_R2_ST_INC2_LIMIT ||
            fit->c[axis][5] > V9X_R2_ST_INC2_LIMIT ||
            fit->c[axis][5] < -V9X_R2_ST_INC2_LIMIT) {
            fit->affine = 1;
        }
        /* The first differences are the gradient at the anchor, which is
         * affine in position: its extremes over the triangle are at the
         * vertices. Half the S.11.16 range leaves room for an anchor a
         * pixel outside. */
        for (vertex = 0ul; vertex < 3ul; ++vertex) {
            const double *c = fit->c[axis];
            double gx = c[1] + 2.0 * c[3] * px[vertex] + c[5] * py[vertex];
            double gy = c[2] + 2.0 * c[4] * py[vertex] + c[5] * px[vertex];

            if (!(gx < V9X_R2_ST_INC_LIMIT / 2.0 &&
                  gx > -V9X_R2_ST_INC_LIMIT / 2.0 &&
                  gy < V9X_R2_ST_INC_LIMIT / 2.0 &&
                  gy > -V9X_R2_ST_INC_LIMIT / 2.0)) {
                fit->affine = 1;
            }
        }
    }
    if (fit->affine) {
        /*
         * A sliver: across a triangle a pixel or so thick the quadratic's
         * curvature exceeds the S.10.16 second differences. Such a
         * triangle covers few pixels; it gets the tangent plane of the
         * exact S = N / Q at its centroid, N and Q being the screen-linear
         * S q and q. Not the plane through the vertices: across a sliver
         * that plane's slope is the perspective's departure from linear
         * over the thickness, far past S.11.16.
         */
        for (axis = 0ul; axis < 2ul; ++axis) {
            double n[3];
            double d[3];
            double value[3];
            double n0;
            double d0;
            v9x_u32 term;

            for (vertex = 0ul; vertex < 3ul; ++vertex) {
                value[vertex] = (axis == 0ul ? coords[vertex].tu
                                             : coords[vertex].tv) *
                                fit->scale[axis] - fit->base[axis];
            }
            for (term = 0ul; term < 3ul; ++term) {
                n[term] = 0.0;
                d[term] = 0.0;
                for (vertex = 0ul; vertex < 3ul; ++vertex) {
                    n[term] += lambda[vertex][term] * value[vertex] *
                               q[vertex];
                    d[term] += lambda[vertex][term] * q[vertex];
                }
            }
            /* N and Q at the centroid, (sum of the vertices) / 3. */
            n0 = 0.0;
            d0 = 0.0;
            for (vertex = 0ul; vertex < 3ul; ++vertex) {
                n0 += value[vertex] * q[vertex] / 3.0;
                d0 += q[vertex] / 3.0;
            }
            /* grad S = (Q grad N - N grad Q) / Q^2, then the plane through
             * the centroid's value, about the origin. */
            fit->c[axis][1] = (d0 * n[1] - n0 * d[1]) / (d0 * d0);
            fit->c[axis][2] = (d0 * n[2] - n0 * d[2]) / (d0 * d0);
            fit->c[axis][0] = n0 / d0 -
                fit->c[axis][1] * (px[0] + px[1] + px[2]) / 3.0 -
                fit->c[axis][2] * (py[0] + py[1] + py[2]) / 3.0;
            fit->c[axis][3] = 0.0;
            fit->c[axis][4] = 0.0;
            fit->c[axis][5] = 0.0;
        }
    }
    return V9X_STATUS_OK;
}

static double v9x_r2_fit_at(const double *c, double x, double y)
{
    return c[0] + c[1] * x + c[2] * y + c[3] * x * x + c[4] * y * y +
           c[5] * x * y;
}

v9x_status v9x_r2_setup_texture(const struct v9x_r2_vertex *vertices,
                                const struct v9x_r2_tex_coord *coords,
                                const struct v9x_r2_texture *texture,
                                const struct v9x_r2_flat_trap *trap,
                                struct v9x_r2_st *st, v9x_u32 *affine)
{
    struct v9x_r2_st_fit fit;
    v9x_status status;
    v9x_u32 axis;
    double sign;

    if (trap == 0 || st == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    /* About the DST_Y_X pixel's centre. */
    status = v9x_r2_fit_st(vertices, coords, texture,
                           (double)(v9x_s32)trap->x + 0.5,
                           (double)(v9x_s32)trap->y + 0.5, &fit);
    if (status != V9X_STATUS_OK) {
        return status;
    }
    if (affine != 0) {
        *affine = fit.affine ? 1ul : 0ul;
    }

    /* DST_X_DIR clear counts dx leftward: x = -dx. */
    sign = (trap->dst_cntl & V9X_M64_DST_X_DIR) != 0ul ? 1.0 : -1.0;
    for (axis = 0ul; axis < 2ul; ++axis) {
        const double *c = fit.c[axis];
        double terms[6];
        v9x_s32 fixed[6];
        v9x_u32 term;

        /*
         * Onto the engine's forward differences (dx = sign * x):
         *   START + A dx + B dy + C dx(dx-1)/2 + D dy(dy-1)/2 + E dx dy
         * gives C = 2 c_xx, A = sign c_x + c_xx, D = 2 c_yy,
         * B = c_y + c_yy, E = sign c_xy. The engine floors, which is
         * Direct3D's point sample of u * width, so START has no bias.
         */
        terms[0] = c[0] + fit.base[axis];
        terms[1] = sign * c[1] + c[3];
        terms[2] = c[2] + c[4];
        terms[3] = 2.0 * c[3];
        terms[4] = 2.0 * c[4];
        terms[5] = sign * c[5];
        for (term = 1ul; term < 6ul; ++term) {
            double limit = term < 3ul ? V9X_R2_ST_INC_LIMIT
                                      : V9X_R2_ST_INC2_LIMIT;

            if (!v9x_r2_finite(terms[term]) || terms[term] > limit ||
                terms[term] < -limit ||
                !v9x_r2_round(terms[term], &fixed[term])) {
                return V9X_STATUS_UNSUPPORTED;
            }
        }
        /* START modulo the repeat. It keeps bits 25:5: half its step
         * first makes that a rounding. */
        if (!v9x_r2_round(terms[0] / V9X_R2_ST_ONE_D - 0.5, &fixed[0])) {
            return V9X_STATUS_UNSUPPORTED;
        }
        terms[0] -= (double)fixed[0] * V9X_R2_ST_ONE_D;
        terms[0] += (double)(v9x_s32)(V9X_R2_ST_AGAINST_LOSS / 2ul);
        if (!v9x_r2_round(terms[0], &fixed[0])) {
            return V9X_STATUS_UNSUPPORTED;
        }

        st->start[axis] = fixed[0];
        st->xinc_start[axis] = fixed[1];
        st->y_inc[axis] = fixed[2];
        st->x_inc2[axis] = fixed[3];
        st->y_inc2[axis] = fixed[4];
        st->xy_inc2[axis] = fixed[5];
    }
    return V9X_STATUS_OK;
}

v9x_status v9x_r2_texture_error(const struct v9x_r2_vertex *vertices,
                                const struct v9x_r2_tex_coord *coords,
                                const struct v9x_r2_texture *texture,
                                double *texels)
{
    struct v9x_r2_st_fit fit;
    double worst = 0.0;
    v9x_status status;
    v9x_u32 i;
    v9x_u32 j;

    if (texels == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    *texels = 0.0;
    status = v9x_r2_fit_st(vertices, coords, texture, 0.0, 0.0, &fit);
    if (status != V9X_STATUS_OK) {
        return status;
    }
    /* A barycentric grid in eighths, edges included. The error peaks
     * between the six exact nodes, and ten hand-placed probes read up to
     * 15% low in the host test; the grid reads within 10%. */
    for (i = 0ul; i <= V9X_R2_PROBE_STEPS; ++i) {
        for (j = 0ul; i + j <= V9X_R2_PROBE_STEPS; ++j) {
            double l[3];
            double x = 0.0;
            double y = 0.0;
            double q = 0.0;
            double sq[2];
            v9x_u32 vertex;
            v9x_u32 axis;

            if (!fit.affine && (i & 3ul) == 0ul && (j & 3ul) == 0ul) {
                continue;           /* a vertex or midpoint: exact */
            }
            l[0] = (double)(v9x_s32)i / (double)V9X_R2_PROBE_STEPS;
            l[1] = (double)(v9x_s32)j / (double)V9X_R2_PROBE_STEPS;
            l[2] = 1.0 - l[0] - l[1];
            sq[0] = 0.0;
            sq[1] = 0.0;
            for (vertex = 0ul; vertex < 3ul; ++vertex) {
                x += l[vertex] * (double)vertices[vertex].x /
                     (double)V9X_R2_SUBPIXEL;
                y += l[vertex] * (double)vertices[vertex].y /
                     (double)V9X_R2_SUBPIXEL;
                q += l[vertex] * coords[vertex].q;
                sq[0] += l[vertex] * coords[vertex].q * coords[vertex].tu;
                sq[1] += l[vertex] * coords[vertex].q * coords[vertex].tv;
            }
            for (axis = 0ul; axis < 2ul; ++axis) {
                double error = v9x_r2_fit_at(fit.c[axis], x, y) +
                               fit.base[axis] - sq[axis] / q * fit.scale[axis];

                if (error < 0.0) {
                    error = -error;
                }
                if (error > worst) {
                    worst = error;
                }
            }
        }
    }
    *texels = worst / fit.texel;
    return V9X_STATUS_OK;
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
