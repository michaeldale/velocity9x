/*
 * The inline vertex run: one _3DPRIMITIVE dword, then three vertices.
 *
 * S0 and S1 are deliberately absent. They are the vertex buffer's address and
 * stride, and they are meaningless when the vertices follow the command dword
 * in the ring - the hardware takes the vertex layout from S2 and S4 instead
 * (docs\decisions\2026-09-14-intel-gen3-3d-packet-audit.md section 7, closed
 * against Mesa's structural evidence and xf86's inline emitter).
 *
 * Five dwords per vertex: x, y, z, w as IEEE-754 floats, then ONE PACKED
 * COLOUR DWORD - not four floats. That is the mistake that would still draw a
 * plausible triangle, which is why it is in capitals here and asserted in the
 * host test.
 */
#include "velocity9x/intel_gen3_3d.h"

#define V9X_I9XX_VERTEX_RUN_DWORDS \
    (1ul + V9X_I9XX_VERTEX_COUNT * V9X_I9XX_VERTEX_DWORDS)

/* The triangle, as integers, before float conversion. Deliberately asymmetric
 * in both axes so a transposed X/Y is visible rather than plausible. */
static const v9x_u32 v9x_i9xx_triangle_x[V9X_I9XX_VERTEX_COUNT] = {
    (v9x_u32)V9X_I9XX_TRI_X0,
    (v9x_u32)V9X_I9XX_TRI_X1,
    (v9x_u32)V9X_I9XX_TRI_X2
};
static const v9x_u32 v9x_i9xx_triangle_y[V9X_I9XX_VERTEX_COUNT] = {
    (v9x_u32)V9X_I9XX_TRI_Y0,
    (v9x_u32)V9X_I9XX_TRI_Y1,
    (v9x_u32)V9X_I9XX_TRI_Y2
};

v9x_u32 v9x_i9xx_vertex_run_extent(void)
{
    return V9X_I9XX_VERTEX_RUN_DWORDS;
}

v9x_status v9x_i9xx_build_vertex_run(
    v9x_u32 width, v9x_u32 height,
    v9x_u32 *stream, v9x_u32 capacity, v9x_u32 *written)
{
    v9x_u32 at = 0ul;
    v9x_u32 vertex;
    v9x_u32 zero_bits = 0ul;
    v9x_u32 one_bits = 0ul;

    if (written != 0) { *written = 0ul; }
    if (stream == 0 || written == 0 ||
        capacity < V9X_I9XX_VERTEX_RUN_DWORDS ||
        width == 0ul || height == 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    /*
     * Z and W are the same at every vertex: zero depth, and W = 1.0 so the
     * perspective divide is the identity and screen coordinates pass through
     * unchanged. Built through the same converter as the coordinates, so a
     * fault in it cannot be masked by hand-written constants.
     */
    if (v9x_i9xx_float_from_int(0ul, &zero_bits) != V9X_I9XX_FLOAT_OK ||
        v9x_i9xx_float_from_int(1ul, &one_bits) != V9X_I9XX_FLOAT_OK) {
        return V9X_STATUS_INVALID_STATE;
    }

    /* Length field: (vertex dwords - 1), counting dwords and excluding the
     * command dword. Two independently derived formulas agree. */
    stream[at++] = V9X_I9XX_3DPRIMITIVE_INLINE |
                   V9X_I9XX_PRIM3D_TRILIST |
                   ((V9X_I9XX_VERTEX_COUNT * V9X_I9XX_VERTEX_DWORDS) - 1ul);

    for (vertex = 0ul; vertex < V9X_I9XX_VERTEX_COUNT; ++vertex) {
        v9x_u32 x_bits = 0ul;
        v9x_u32 y_bits = 0ul;

        /*
         * Refuse any coordinate outside the drawing rectangle. The rectangle
         * is inclusive, so the last addressable pixel is width - 1. A vertex
         * outside it would be clipped by hardware rather than refused, and the
         * software reference would then disagree for a reason nobody could see
         * in the capture.
         */
        if (v9x_i9xx_triangle_x[vertex] >= width ||
            v9x_i9xx_triangle_y[vertex] >= height) {
            return V9X_STATUS_INVALID_ARGUMENT;
        }
        if (v9x_i9xx_float_from_int(v9x_i9xx_triangle_x[vertex], &x_bits) !=
                V9X_I9XX_FLOAT_OK ||
            v9x_i9xx_float_from_int(v9x_i9xx_triangle_y[vertex], &y_bits) !=
                V9X_I9XX_FLOAT_OK) {
            return V9X_STATUS_INVALID_ARGUMENT;
        }

        stream[at++] = x_bits;
        stream[at++] = y_bits;
        stream[at++] = zero_bits;
        stream[at++] = one_bits;
        /*
         * All three vertices carry the same colour, which makes flat versus
         * smooth shading moot and keeps the provoking-vertex rules off the
         * critical path entirely - both were single-sourced. A flat-shaded
         * triangle is what the plan asks for; a uniformly coloured one
         * satisfies that under either shading mode and cannot disagree with
         * the software reference about which vertex supplied the colour.
         */
        stream[at++] = V9X_I9XX_TRI_COLOR_BGRA;
    }

    *written = at;
    return V9X_STATUS_OK;
}
