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
