/*
 * Back-face culling. See r3d_cull.h for why it is decided here rather than
 * by the hardware.
 *
 * Nothing in this file converts a float to an integer, so it needs none of
 * the inline fistp that d3d_zfixed.c carries: a multiply, a subtract and two
 * comparisons are all x87 instructions, and the HAL's nodefaultlibs link has
 * no runtime helper to miss.
 */
#include "r3d_cull.h"

/* IEEE-754 single precision: the eight exponent bits. All ones is NaN or
 * infinity. */
#define V9X_R3D_CULL_EXPONENT_MASK 0x7f800000ul

int v9x_r3d_cull_triangle(unsigned long mode,
                          float ax, float ay,
                          float bx, float by,
                          float cx, float cy)
{
    float area;

    if (mode != V9X_R3D_CULL_CW && mode != V9X_R3D_CULL_CCW) {
        return 0;
    }

    /*
     * Twice the signed area. With y growing downward, positive means the
     * vertices run clockwise on the monitor: (10,10) -> (50,10) -> (10,50)
     * goes right along the top and then down the left, and gives +1600.
     */
    area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);

    /*
     * A non-finite area is drawn, and it is detected from the BITS.
     *
     * A NaN or infinite vertex is the clipper's to refuse, with its count,
     * rather than this function's to lose without one. No float comparison
     * can be trusted to say so under Open Watcom: tests\host\test_r3d_cull.c
     * caught it answering `area > 0.0f` TRUE for a NaN area, and `nan != nan`
     * FALSE - its x87 compares do not honour the unordered result. An
     * all-ones exponent is NaN or infinity whatever the compiler does with
     * a comparison, and storing through the union also rounds the area to
     * float, so the sign tested below is the stored value's.
     */
    {
        union {
            float value;
            unsigned long bits;
        } stored;

        stored.value = area;
        if ((stored.bits & V9X_R3D_CULL_EXPONENT_MASK) ==
                V9X_R3D_CULL_EXPONENT_MASK) {
            return 0;
        }
    }
    /* Strict both ways: zero area covers no pixel, so it is drawn. */
    if (mode == V9X_R3D_CULL_CW) {
        return area > 0.0f ? 1 : 0;
    }
    return area < 0.0f ? 1 : 0;
}

unsigned long v9x_r3d_cull_honoured(unsigned long mode,
                                    int claims_cw, int claims_ccw)
{
    if (mode == V9X_R3D_CULL_CW && claims_cw != 0) {
        return V9X_R3D_CULL_CW;
    }
    if (mode == V9X_R3D_CULL_CCW && claims_ccw != 0) {
        return V9X_R3D_CULL_CCW;
    }
    return V9X_R3D_CULL_NONE;
}
