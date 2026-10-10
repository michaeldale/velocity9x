/*
 * Matrox MGA-2164W triangle setup; see mga_setup.h.
 *
 * Rows run from ceil(y_top - 1/2) to ceil(y_bottom - 1/2), exclusive. On
 * row r, centre r + 1/2, the first pixel drawn and the first not drawn are
 *
 *     X = ceil(x_edge(r + 1/2) - 1/2)
 *
 * for the left and right edge, the engine's [left, right) span. For an edge
 * a -> b spanning dyF by dxF sixteenths (S = 16 a pixel) and Yc = S*r0 + S/2
 *
 *     N = (a.x - S/2)*dyF + dxF*(Yc - a.y),  Q = S*dyF,  X0 = ceil(N / Q),
 *
 * as rage2_setup.c derives it. N grows by S*dxF a row, so after k rows the
 * edge has moved floor((R + k*S*|dxF|) / Q) columns, R being the first
 * row's remainder (N + Q - 1 - X0*Q rightward, X0*Q - N leftward), which is
 * floor((R' + k*|dxF|) / dyF) with R' = floor(R / S).
 *
 * The engine's walk (mga_3d.c, measured) steps after each row while its
 * error is negative, adding the major term dyF each time, then adds the
 * minor term -|dxF|. Starting from AR1 = dyF - R' - |dxF| - 1 it lands on
 * floor((R' + k*|dxF|) / dyF) after k rows: before the k-th row's steps the
 * error is (t + 1)*dyF - R' - k*|dxF| - 1 with t the steps so far, which
 * is negative exactly while t is short of that floor. The specification's
 * own terms are R' = dyF - 1 rightward and R' = 0 leftward, so the setup
 * hands the difference over as the edge's error_bias.
 */
#include "velocity9x/mga_setup.h"

#define V9X_MGA_SETUP_HALF (V9X_MGA_SETUP_SUBPIXEL / 2L)

/* Fixed-point scales of the engine's fields (mga_3d.h): colour 9.15, depth
 * 17.15, s and t with 1 << 20 across the texture, q 16.16. */
#define V9X_MGA_SETUP_LEVEL_ONE    32768.0
#define V9X_MGA_SETUP_ST_ONE       1048576.0
#define V9X_MGA_SETUP_Q_ONE        65536.0
/*
 * The engine truncates a level (mga_3d.c, measured), and a 9.15 colour
 * past 255.99 wraps to 0. Half a level is added to every start, so the
 * truncation rounds to nearest and an exact 255 is 255.5, short of the
 * wrap. The colour fields are signed 24 bits, +-256 levels; a start or a
 * step outside them refuses the triangle - only a sliver whose colour
 * changes by over 255 levels a pixel, or one whose first pixel lies far
 * off it, gets there.
 */
#define V9X_MGA_SETUP_ROUNDING     0.5
#define V9X_MGA_SETUP_LEVEL_LIMIT  256.0
/* A 17.15 depth in 32 bits; the engine clamps the stored value at the
 * top (depth record), so a start past it is held there. */
#define V9X_MGA_SETUP_DEPTH_LOW    -65536.0
#define V9X_MGA_SETUP_DEPTH_HIGH   65535.99
/* The eighth of a texel the engine adds to s and t before its perspective
 * divide (mga_3d.h), taken off their starts. */
#define V9X_MGA_SETUP_BIAS_PARTS \
    ((double)(1ul << V9X_MGA3D_TEX_PERSPECTIVE_BIAS_BITS))
/* Perspective's common scale of s, t and q: q's 16.16 field holds 2^14
 * with a factor of two to spare, and s and t are kept to a quarter of
 * their signed 32 bits at 1 << 20 a texture (512 textures). */
#define V9X_MGA_SETUP_Q_SCALE_MAX  16384.0
#define V9X_MGA_SETUP_ST_HEADROOM  512.0
/* The scale's fall, each time a sliver's steps overflow at it. */
#define V9X_MGA_SETUP_Q_SCALE_STEP 16.0

/* The double's two words, for the exponent test and the magic rounding,
 * as rage2_setup.c has them (local: the shared helper lives in another
 * vendor's object). */
union v9x_mga_setup_double_bits {
    double value;
    v9x_u32 word[2];
};

#define V9X_MGA_SETUP_DOUBLE_EXPONENT 0x7ff00000ul
/* 1.5 * 2^52: added to a double below 2^31 in magnitude, the integer
 * nearest it lands in the low mantissa word. */
#define V9X_MGA_SETUP_ROUND_MAGIC 6755399441055744.0
/* The rounding's range: a signed 32-bit result. */
#define V9X_MGA_SETUP_ROUND_HIGH  2147483647.0
#define V9X_MGA_SETUP_ROUND_LOW   -2147483648.0

/* floor(n / d) for d > 0, whatever the compiler does with negative
 * division. */
static v9x_s32 v9x_mga_setup_floor_div(v9x_s32 n, v9x_s32 d)
{
    v9x_s32 q;

    if (n >= 0L) {
        return n / d;
    }
    q = -((-n) / d);
    if (q * d != n) {
        --q;
    }
    return q;
}

static v9x_s32 v9x_mga_setup_ceil_div(v9x_s32 n, v9x_s32 d)
{
    return -v9x_mga_setup_floor_div(-n, d);
}

/* The first pixel row whose centre is at or below y. */
static v9x_s32 v9x_mga_setup_row_at(v9x_s32 y)
{
    return v9x_mga_setup_ceil_div(y - V9X_MGA_SETUP_HALF,
                                  V9X_MGA_SETUP_SUBPIXEL);
}

/* Whether `value` is finite, from its exponent: comparisons cannot be
 * trusted to find a NaN under Open Watcom. */
static int v9x_mga_setup_finite(double value)
{
    union v9x_mga_setup_double_bits pun;

    pun.value = value;
    return (pun.word[1] & V9X_MGA_SETUP_DOUBLE_EXPONENT) !=
        V9X_MGA_SETUP_DOUBLE_EXPONENT;
}

/* The integer nearest `value`, if it is finite and a signed 32-bit
 * number. */
static int v9x_mga_setup_round(double value, v9x_s32 *out)
{
    union v9x_mga_setup_double_bits pun;

    if (!v9x_mga_setup_finite(value) ||
        value > V9X_MGA_SETUP_ROUND_HIGH ||
        value < V9X_MGA_SETUP_ROUND_LOW) {
        return 0;
    }
    pun.value = value + V9X_MGA_SETUP_ROUND_MAGIC;
    *out = (v9x_s32)pun.word[0];
    return 1;
}

/*
 * Edge a -> b (a.y < b.y) from row r0, whose centre lies in [a.y, b.y):
 * the column on that row, the slope and the bias that makes the walk
 * exact (file comment). Every product stays inside 31 bits for vertices in
 * [0, 2047] pixels: (a.x - S/2)*dyF and dxF*(Yc - a.y) are each below
 * 32752^2, and their sum is the edge's x at Yc times dyF.
 */
static void v9x_mga_setup_edge(const struct v9x_mga_setup_vertex *a,
                               const struct v9x_mga_setup_vertex *b,
                               v9x_s32 r0, struct v9x_mga3d_edge *edge)
{
    v9x_s32 dy = b->y - a->y;
    v9x_s32 dx = b->x - a->x;
    v9x_s32 q = V9X_MGA_SETUP_SUBPIXEL * dy;
    v9x_s32 yc = V9X_MGA_SETUP_SUBPIXEL * r0 + V9X_MGA_SETUP_HALF;
    v9x_s32 n = (a->x - V9X_MGA_SETUP_HALF) * dy + dx * (yc - a->y);
    v9x_s32 x0 = v9x_mga_setup_ceil_div(n, q);
    v9x_s32 remainder;

    edge->x = x0;
    edge->dx = dx;
    edge->dy = dy;
    if (dx >= 0L) {
        remainder = (n + q - 1L - x0 * q) / V9X_MGA_SETUP_SUBPIXEL;
        edge->error_bias = dy - remainder - 1L;
    } else {
        remainder = (x0 * q - n) / V9X_MGA_SETUP_SUBPIXEL;
        edge->error_bias = -remainder;
    }
}

/* One attribute's plane: its value at vertex 0 and its change per pixel
 * across and down. */
struct v9x_mga_setup_plane {
    double origin;
    double ddx;
    double ddy;
};

static void v9x_mga_setup_plane_of(const struct v9x_mga_setup_vertex *v,
                                   double a0, double a1, double a2,
                                   double inverse_det,
                                   struct v9x_mga_setup_plane *plane)
{
    double x10 = (double)(v[1].x - v[0].x) / V9X_MGA_SETUP_SUBPIXEL;
    double y10 = (double)(v[1].y - v[0].y) / V9X_MGA_SETUP_SUBPIXEL;
    double x20 = (double)(v[2].x - v[0].x) / V9X_MGA_SETUP_SUBPIXEL;
    double y20 = (double)(v[2].y - v[0].y) / V9X_MGA_SETUP_SUBPIXEL;

    plane->origin = a0;
    plane->ddx = ((a1 - a0) * y20 - (a2 - a0) * y10) * inverse_det;
    plane->ddy = ((a2 - a0) * x10 - (a1 - a0) * x20) * inverse_det;
}

/* The plane's value at pixel centre (px, py). */
static double v9x_mga_setup_at(const struct v9x_mga_setup_plane *plane,
                               const struct v9x_mga_setup_vertex *v0,
                               double px, double py)
{
    return plane->origin +
        plane->ddx * (px - (double)v0->x / V9X_MGA_SETUP_SUBPIXEL) +
        plane->ddy * (py - (double)v0->y / V9X_MGA_SETUP_SUBPIXEL);
}

/* A start and the two steps, scaled to the field. Zero when any is not a
 * finite signed 32-bit number at that scale. */
static int v9x_mga_setup_fix(const struct v9x_mga_setup_plane *plane,
                             double start, double scale, v9x_s32 *out)
{
    return v9x_mga_setup_round(start * scale, &out[0]) &&
        v9x_mga_setup_round(plane->ddx * scale, &out[1]) &&
        v9x_mga_setup_round(plane->ddy * scale, &out[2]);
}

/* A colour channel: rounded to nearest by the half level, inside the
 * signed 24-bit field (file comment above the constants). */
static int v9x_mga_setup_color(const struct v9x_mga_setup_plane *plane,
                               const struct v9x_mga_setup_vertex *v0,
                               double px, double py, v9x_s32 *out)
{
    double start = v9x_mga_setup_at(plane, v0, px, py) +
        V9X_MGA_SETUP_ROUNDING;

    if (!(start > -V9X_MGA_SETUP_LEVEL_LIMIT &&
          start < V9X_MGA_SETUP_LEVEL_LIMIT &&
          plane->ddx > -V9X_MGA_SETUP_LEVEL_LIMIT &&
          plane->ddx < V9X_MGA_SETUP_LEVEL_LIMIT &&
          plane->ddy > -V9X_MGA_SETUP_LEVEL_LIMIT &&
          plane->ddy < V9X_MGA_SETUP_LEVEL_LIMIT)) {
        return 0;
    }
    return v9x_mga_setup_fix(plane, start, V9X_MGA_SETUP_LEVEL_ONE, out);
}

/* The three planes' fields into TMR0-TMR8: per pixel s, t, q at 0, 2, 4;
 * per row at 1, 3, 5; starts at 6, 7, 8. */
static void v9x_mga_setup_put_tmr(v9x_s32 *tmr, const v9x_s32 *s,
                                  const v9x_s32 *t, const v9x_s32 *q)
{
    tmr[0] = s[1];
    tmr[1] = s[2];
    tmr[2] = t[1];
    tmr[3] = t[2];
    tmr[4] = q[1];
    tmr[5] = q[2];
    tmr[6] = s[0];
    tmr[7] = t[0];
    tmr[8] = q[0];
}

struct v9x_mga_setup_planes {
    struct v9x_mga_setup_plane red;
    struct v9x_mga_setup_plane green;
    struct v9x_mga_setup_plane blue;
    struct v9x_mga_setup_plane z;
    struct v9x_mga_setup_plane s;
    struct v9x_mga_setup_plane t;
    struct v9x_mga_setup_plane q;
};

/*
 * One trapezoid: rows [r0, r1) between edges left_a -> left_b and
 * right_a -> right_b, its planes started at its first row's first pixel.
 */
static v9x_status v9x_mga_setup_emit(
    const struct v9x_mga3d_trap *base, const struct v9x_mga_setup_vertex *v,
    const struct v9x_mga_setup_planes *planes,
    const struct v9x_mga_setup_vertex *left_a,
    const struct v9x_mga_setup_vertex *left_b,
    const struct v9x_mga_setup_vertex *right_a,
    const struct v9x_mga_setup_vertex *right_b,
    v9x_s32 r0, v9x_s32 r1, struct v9x_mga3d_trap *trap)
{
    double px;
    double py;
    v9x_s32 s[3];
    v9x_s32 t[3];
    v9x_s32 q[3];

    *trap = *base;
    trap->top = (v9x_u32)r0;
    trap->length = (v9x_u32)(r1 - r0);
    v9x_mga_setup_edge(left_a, left_b, r0, &trap->left);
    v9x_mga_setup_edge(right_a, right_b, r0, &trap->right);
    px = (double)trap->left.x + 0.5;
    py = (double)r0 + 0.5;

    if (base->shade == V9X_MGA3D_SHADE_GOURAUD &&
        (!v9x_mga_setup_color(&planes->red, v, px, py, trap->red) ||
         !v9x_mga_setup_color(&planes->green, v, px, py, trap->green) ||
         !v9x_mga_setup_color(&planes->blue, v, px, py, trap->blue))) {
        return V9X_STATUS_UNSUPPORTED;
    }
    if (base->depth == V9X_MGA3D_DEPTH_16) {
        double z = v9x_mga_setup_at(&planes->z, v, px, py) +
            V9X_MGA_SETUP_ROUNDING;

        if (z < V9X_MGA_SETUP_DEPTH_LOW) {
            z = V9X_MGA_SETUP_DEPTH_LOW;
        }
        if (z > V9X_MGA_SETUP_DEPTH_HIGH) {
            z = V9X_MGA_SETUP_DEPTH_HIGH;
        }
        if (!v9x_mga_setup_fix(&planes->z, z, V9X_MGA_SETUP_LEVEL_ONE,
                               trap->z)) {
            return V9X_STATUS_UNSUPPORTED;
        }
    }
    if (base->texture.enabled != 0ul) {
        /* s and t already carry q where perspective is on, and run past the
         * texture by design where it wraps: no clamp, only the field. The
         * engine's perspective constant comes off their starts, so it
         * draws floor(s / q). */
        double s_bias = 0.0;
        double t_bias = 0.0;

        if (base->texture.perspective != 0ul) {
            s_bias = 1.0 / (V9X_MGA_SETUP_BIAS_PARTS *
                            (double)(1ul << base->texture.log2_width));
            t_bias = 1.0 / (V9X_MGA_SETUP_BIAS_PARTS *
                            (double)(1ul << base->texture.log2_height));
        }
        if (!v9x_mga_setup_fix(&planes->s,
                               v9x_mga_setup_at(&planes->s, v, px, py) -
                                   s_bias,
                               V9X_MGA_SETUP_ST_ONE, s) ||
            !v9x_mga_setup_fix(&planes->t,
                               v9x_mga_setup_at(&planes->t, v, px, py) -
                                   t_bias,
                               V9X_MGA_SETUP_ST_ONE, t) ||
            !v9x_mga_setup_fix(&planes->q,
                               v9x_mga_setup_at(&planes->q, v, px, py),
                               V9X_MGA_SETUP_Q_ONE, q)) {
            return V9X_STATUS_UNSUPPORTED;
        }
        v9x_mga_setup_put_tmr(trap->texture.tmr, s, t, q);
    }
    return V9X_STATUS_OK;
}

/*
 * s, t and q times the largest power of two that keeps s and t a quarter
 * of their fields: the engine divides s by q, so only the ratio is drawn,
 * and the bits gained are q's. At q's own scale, 1.0 the largest, a
 * receding wall's q step per pixel has few significant bits, and the
 * rounding builds up across the span into a texel that moves with the
 * view - Half-Life's walls swam on A8U4I5 (boot 390). `cap` bounds the
 * scale, which the caller lowers for a sliver whose steps then overflow.
 */
static void v9x_mga_setup_scale_perspective(double *s, double *t, double *q,
                                            double cap)
{
    double largest = 0.0;
    double scale = 1.0;
    unsigned int index;

    for (index = 0u; index < 3u; ++index) {
        double a = s[index] < 0.0 ? -s[index] : s[index];
        double b = t[index] < 0.0 ? -t[index] : t[index];

        if (a > largest) {
            largest = a;
        }
        if (b > largest) {
            largest = b;
        }
    }
    while (scale < cap &&
           largest * scale * 2.0 <= V9X_MGA_SETUP_ST_HEADROOM) {
        scale *= 2.0;
    }
    for (index = 0u; index < 3u; ++index) {
        s[index] *= scale;
        t[index] *= scale;
        q[index] *= scale;
    }
}

/*
 * The per-vertex s, t and q the planes run through. With perspective, q is
 * 1/w scaled so the largest is 1, and s, t are (u, v) times it, all three
 * then scaled up together; without, q is 1 and s, t are u, v.
 */
static void v9x_mga_setup_texture_values(
    const struct v9x_mga3d_texture *texture,
    const struct v9x_mga_setup_vertex *v, double cap, double *s, double *t,
    double *q)
{
    double q_max = v[0].q;
    unsigned int index;

    if (v[1].q > q_max) {
        q_max = v[1].q;
    }
    if (v[2].q > q_max) {
        q_max = v[2].q;
    }
    for (index = 0u; index < 3u; ++index) {
        if (texture->perspective != 0ul) {
            q[index] = v[index].q / q_max;
            s[index] = v[index].u * q[index];
            t[index] = v[index].v * q[index];
        } else {
            q[index] = 1.0;
            s[index] = v[index].u;
            t[index] = v[index].v;
        }
    }
    if (texture->perspective != 0ul) {
        v9x_mga_setup_scale_perspective(s, t, q, cap);
    }
}

static v9x_status v9x_mga_setup_emit_both(
    const struct v9x_mga3d_trap *base, const struct v9x_mga_setup_vertex *v,
    const struct v9x_mga_setup_planes *planes, int long_on_left,
    v9x_s32 r0, v9x_s32 r1, v9x_s32 r2, struct v9x_mga3d_trap *traps,
    v9x_u32 *count);

v9x_status v9x_mga_setup_triangle(const struct v9x_mga3d_trap *base,
                                  const struct v9x_mga_setup_vertex *vertices,
                                  struct v9x_mga3d_trap *traps,
                                  v9x_u32 *count)
{
    struct v9x_mga_setup_vertex v[3];
    struct v9x_mga_setup_vertex swap;
    struct v9x_mga_setup_planes planes;
    double s[3];
    double t[3];
    double q[3];
    double det;
    double inverse_det;
    double cap;
    v9x_s32 r0;
    v9x_s32 r1;
    v9x_s32 r2;
    int long_on_left;
    v9x_u32 index;
    v9x_status status;

    if (count != 0) {
        *count = 0ul;
    }
    if (base == 0 || vertices == 0 || traps == 0 || count == 0 ||
        base->depth == V9X_MGA3D_DEPTH_32) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    for (index = 0ul; index < 3ul; ++index) {
        v[index] = vertices[index];
        if (v[index].x < 0L || v[index].y < 0L ||
            v[index].x > V9X_MGA_SETUP_COORD_MAX ||
            v[index].y > V9X_MGA_SETUP_COORD_MAX) {
            return V9X_STATUS_INVALID_ARGUMENT;
        }
    }

    /* Top to bottom; the order between equal heights does not matter, an
     * edge of no height never bounds a row. */
    if (v[1].y < v[0].y) {
        swap = v[0]; v[0] = v[1]; v[1] = swap;
    }
    if (v[2].y < v[1].y) {
        swap = v[1]; v[1] = v[2]; v[2] = swap;
    }
    if (v[1].y < v[0].y) {
        swap = v[0]; v[0] = v[1]; v[1] = swap;
    }

    /* Twice the signed area in sixteenths squared: below 2^31 for the
     * coordinate bound only as a double, so the sign is taken from one. */
    det = (double)(v[1].x - v[0].x) * (double)(v[2].y - v[0].y) -
          (double)(v[2].x - v[0].x) * (double)(v[1].y - v[0].y);
    if (det == 0.0) {
        return V9X_STATUS_OK;
    }
    r0 = v9x_mga_setup_row_at(v[0].y);
    r1 = v9x_mga_setup_row_at(v[1].y);
    r2 = v9x_mga_setup_row_at(v[2].y);
    if (r0 == r2) {
        return V9X_STATUS_OK;
    }

    /* The planes, against the sorted vertices; det in pixels squared. */
    inverse_det = (double)(V9X_MGA_SETUP_SUBPIXEL * V9X_MGA_SETUP_SUBPIXEL) /
        det;
    if (base->shade == V9X_MGA3D_SHADE_GOURAUD) {
        v9x_mga_setup_plane_of(v, v[0].red, v[1].red, v[2].red, inverse_det,
                               &planes.red);
        v9x_mga_setup_plane_of(v, v[0].green, v[1].green, v[2].green,
                               inverse_det, &planes.green);
        v9x_mga_setup_plane_of(v, v[0].blue, v[1].blue, v[2].blue,
                               inverse_det, &planes.blue);
    }
    if (base->depth == V9X_MGA3D_DEPTH_16) {
        v9x_mga_setup_plane_of(v, v[0].z, v[1].z, v[2].z, inverse_det,
                               &planes.z);
    }
    /*
     * Which side the long edge v0 -> v2 is on. With y down, the middle
     * vertex lies to its left when the cross product of the long edge
     * with v0 -> v1 is positive; then the long edge is the right one.
     * Each trapezoid's edges run top to bottom: the top half between
     * v0 -> v1 and the long edge, the bottom half between v1 -> v2 and it.
     */
    long_on_left = (double)(v[2].x - v[0].x) * (double)(v[1].y - v[0].y) -
        (double)(v[1].x - v[0].x) * (double)(v[2].y - v[0].y) < 0.0;

    /* A perspective sliver whose steps overflow at the full scale is set
     * up again at a smaller one, down to q's own. */
    for (cap = V9X_MGA_SETUP_Q_SCALE_MAX; ; cap /= V9X_MGA_SETUP_Q_SCALE_STEP) {
        if (cap < 1.0) {
            cap = 1.0;
        }
        if (base->texture.enabled != 0ul) {
            v9x_mga_setup_texture_values(&base->texture, v, cap, s, t, q);
            v9x_mga_setup_plane_of(v, s[0], s[1], s[2], inverse_det,
                                   &planes.s);
            v9x_mga_setup_plane_of(v, t[0], t[1], t[2], inverse_det,
                                   &planes.t);
            v9x_mga_setup_plane_of(v, q[0], q[1], q[2], inverse_det,
                                   &planes.q);
        }
        status = v9x_mga_setup_emit_both(base, v, &planes, long_on_left,
                                         r0, r1, r2, traps, count);
        if (status == V9X_STATUS_OK || base->texture.enabled == 0ul ||
            base->texture.perspective == 0ul || cap <= 1.0) {
            return status;
        }
    }
}

/* The triangle's one or two trapezoids: rows [r0, r1) above the middle
 * vertex, [r1, r2) below. */
static v9x_status v9x_mga_setup_emit_both(
    const struct v9x_mga3d_trap *base, const struct v9x_mga_setup_vertex *v,
    const struct v9x_mga_setup_planes *planes, int long_on_left,
    v9x_s32 r0, v9x_s32 r1, v9x_s32 r2, struct v9x_mga3d_trap *traps,
    v9x_u32 *count)
{
    v9x_status status;

    *count = 0ul;
    if (r1 > r0) {
        status = long_on_left
            ? v9x_mga_setup_emit(base, v, planes, &v[0], &v[2], &v[0], &v[1],
                                 r0, r1, &traps[*count])
            : v9x_mga_setup_emit(base, v, planes, &v[0], &v[1], &v[0], &v[2],
                                 r0, r1, &traps[*count]);
        if (status != V9X_STATUS_OK) {
            *count = 0ul;
            return status;
        }
        ++*count;
    }
    if (r2 > r1) {
        status = long_on_left
            ? v9x_mga_setup_emit(base, v, planes, &v[0], &v[2], &v[1], &v[2],
                                 r1, r2, &traps[*count])
            : v9x_mga_setup_emit(base, v, planes, &v[1], &v[2], &v[0], &v[2],
                                 r1, r2, &traps[*count]);
        if (status != V9X_STATUS_OK) {
            *count = 0ul;
            return status;
        }
        ++*count;
    }
    return V9X_STATUS_OK;
}
