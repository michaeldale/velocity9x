/*
 * An independent CPU rasteriser for the Rage II triangle setup to be checked
 * against: per pixel, an edge-function test on the pixel centre with the
 * top-left tie rule. It shares no arithmetic with rage2_setup.c, which
 * reaches the same coverage through trapezoids and Bresenham terms.
 *
 * Coordinates are fixed point, V9X_R2_SUBPIXEL per pixel; the centre of
 * pixel (x, y) is (x*S + S/2, y*S + S/2). Compiled into the host tests and
 * included by tools\diag\ati_rage2_scene_win32.c, so the engine's output on
 * the card is judged by the same function as the host model.
 */
#include "velocity9x/ati_rage2.h"
#include "rage2_reference.h"

/* Twice the signed area of (a, b, p); positive for p to the right of
 * a->b in screen space (y down) when the triangle is clockwise. */
static v9x_s32 v9x_r2_ref_edge(const struct v9x_r2_vertex *a,
                               const struct v9x_r2_vertex *b,
                               v9x_s32 px, v9x_s32 py)
{
    return (b->x - a->x) * (py - a->y) - (b->y - a->y) * (px - a->x);
}

/*
 * Whether a point exactly on edge a->b is inside: on a left edge (the
 * interior lies to larger x) or a top edge (horizontal, interior below).
 * `third` is the triangle's other vertex.
 */
static int v9x_r2_ref_owns_ties(const struct v9x_r2_vertex *a,
                                const struct v9x_r2_vertex *b,
                                const struct v9x_r2_vertex *third)
{
    v9x_s32 side;

    if (a->y == b->y) {
        return third->y > a->y;
    }
    /* The third vertex's side of the edge, as an x comparison at its y:
     * sign((third.x - edge_x(third.y))) without division. */
    side = (third->x - a->x) * (b->y - a->y) -
           (b->x - a->x) * (third->y - a->y);
    if (b->y < a->y) {
        side = -side;
    }
    return side > 0;
}

int v9x_r2_ref_covers(const struct v9x_r2_vertex *v, v9x_s32 x, v9x_s32 y)
{
    v9x_s32 px = x * V9X_R2_SUBPIXEL + V9X_R2_SUBPIXEL / 2;
    v9x_s32 py = y * V9X_R2_SUBPIXEL + V9X_R2_SUBPIXEL / 2;
    v9x_s32 area = v9x_r2_ref_edge(&v[0], &v[1], v[2].x, v[2].y);
    v9x_s32 w[3];
    int index;

    if (area == 0) {
        return 0;
    }
    w[0] = v9x_r2_ref_edge(&v[1], &v[2], px, py);
    w[1] = v9x_r2_ref_edge(&v[2], &v[0], px, py);
    w[2] = v9x_r2_ref_edge(&v[0], &v[1], px, py);
    for (index = 0; index < 3; ++index) {
        const struct v9x_r2_vertex *a = &v[(index + 1) % 3];
        const struct v9x_r2_vertex *b = &v[(index + 2) % 3];
        v9x_s32 value = area > 0 ? w[index] : -w[index];

        if (value < 0) {
            return 0;
        }
        if (value == 0 && !v9x_r2_ref_owns_ties(a, b, &v[index])) {
            return 0;
        }
    }
    return 1;
}

/*
 * The engine's edge walk, measured on A8U4I5
 * (docs\decisions\2026-10-02-rage-iic-trapezoid-edge-model.md): per row an
 * edge steps in its direction while its error is >= 0, adding DEC, then
 * adds INC; DEC 0 never steps. Not the reference's arithmetic, which is
 * the edge functions above: this is the path the textured scenes walk
 * S/T along.
 */
static void v9x_r2_ref_walk_edge(v9x_u32 start, v9x_s32 err, v9x_s32 inc,
                                 v9x_s32 dec, int rightward, v9x_u32 rows,
                                 v9x_s32 *x_out)
{
    v9x_s32 x = (v9x_s32)start;
    v9x_s32 e = err;
    v9x_u32 row;
    unsigned int guard;

    for (row = 0ul; row < rows; ++row) {
        guard = 0u;
        while (dec != 0l && e >= 0l && guard < 4096u) {
            x += rightward ? 1l : -1l;
            e += dec;
            ++guard;
        }
        x_out[row] = x;
        e += inc;
    }
}

void v9x_r2_ref_walk(const struct v9x_r2_flat_trap *trap, v9x_s32 *lead,
                     v9x_s32 *trail)
{
    v9x_r2_ref_walk_edge(trap->x, trap->lead_err, trap->lead_inc,
                         trap->lead_dec,
                         (trap->dst_cntl & V9X_M64_DST_X_DIR) != 0ul,
                         trap->length, lead);
    v9x_r2_ref_walk_edge(trap->trail_x, trap->trail_err, trap->trail_inc,
                         trap->trail_dec,
                         (trap->dst_cntl & V9X_R2_TRAIL_X_DIR) != 0ul,
                         trap->length, trail);
}

#define V9X_R2_REF_ROWS 2048u

int v9x_r2_ref_walk_st(const struct v9x_r2_flat_trap *trap,
                       const struct v9x_r2_st *st, v9x_r2_ref_texel_fn fn,
                       void *context)
{
    static v9x_s32 lead[V9X_R2_REF_ROWS];
    static v9x_s32 trail[V9X_R2_REF_ROWS];
    struct v9x_r2_st_walk walk;
    struct v9x_r2_st_walk span;
    int forward = (trap->dst_cntl & V9X_M64_DST_X_DIR) != 0ul;
    v9x_s32 at = (v9x_s32)trap->x;
    v9x_u32 row;

    if (trap->length > V9X_R2_REF_ROWS) {
        return 0;
    }
    v9x_r2_ref_walk(trap, lead, trail);
    v9x_r2_st_begin(st, &walk);
    for (row = 0ul; row < trap->length; ++row) {
        v9x_s32 x;

        /* The leading edge only ever moves in DST_X_DIR's direction. */
        while (at != lead[row]) {
            v9x_r2_st_edge_step(st, &walk);
            at += forward ? 1l : -1l;
        }
        span = walk;
        for (x = lead[row]; x < trail[row]; ++x) {
            fn(context, x, (v9x_s32)(trap->y + row), span.value[0],
               span.value[1]);
            v9x_r2_st_pixel_step(st, &span, forward);
        }
        v9x_r2_st_next_row(st, &walk);
    }
    return 1;
}
