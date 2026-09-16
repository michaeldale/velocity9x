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
/* The depth buffer's height bounds a depth run's geometry; it lives with the
 * sandbox layout that places the buffer. */
#include "velocity9x/intel_gma.h"

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

/*
 * Phase 5's triangle, which is now scene 0's geometry as well.
 *
 * Reimplemented as a call into the general builder rather than kept as a
 * second emitter. Two places deriving the same packet layout independently is
 * exactly how the published offsets came to be seven dwords short, and the
 * host test asserts this produces the same dwords it always did.
 */
v9x_status v9x_i9xx_build_vertex_run(
    v9x_u32 width, v9x_u32 height,
    v9x_u32 *stream, v9x_u32 capacity, v9x_u32 *written)
{
    struct v9x_i9xx_triangle triangle;
    v9x_u32 vertex;

    /*
     * EVERY field the emitter reads, including the ones this triangle does
     * not use. per_vertex selects between two colour sources on one bit, and
     * a local struct left partly uninitialised would pick between them on
     * whatever the stack held - which is the Phase 5 triangle, the one stream
     * whose CRC every armed boot is checked against.
     */
    triangle.per_vertex = V9X_FALSE;
    for (vertex = 0ul; vertex < V9X_I9XX_VERTEX_COUNT; ++vertex) {
        triangle.x[vertex] = v9x_i9xx_triangle_x[vertex];
        triangle.y[vertex] = v9x_i9xx_triangle_y[vertex];
        triangle.vertex_color[vertex] = 0ul;
    }
    triangle.color = V9X_I9XX_TRI_COLOR_BGRA;
    triangle.color_measured = V9X_TRUE;

    return v9x_i9xx_build_triangle_run(&triangle, 1ul, width, height,
                                       stream, capacity, written);
}

/*
 * Dwords per triangle: three vertices of five dwords. A compile-time constant,
 * so this one folds and emits no helper call.
 */
#define V9X_I9XX_TRIANGLE_DWORDS \
    (V9X_I9XX_VERTEX_COUNT * V9X_I9XX_VERTEX_DWORDS)

v9x_u32 v9x_i9xx_triangle_run_dwords(v9x_u32 count)
{
    if (count == 0ul || count > V9X_I9XX_SCENE_MAX_TRIANGLES) {
        return 0ul;
    }
    /*
     * Multiplied in 16 bits deliberately - see the header for the link-time
     * reason. count is at most V9X_I9XX_SCENE_MAX_TRIANGLES here, so the
     * product is at most that times fifteen and cannot overflow.
     */
    return 1ul + (v9x_u32)((v9x_u16)count * (v9x_u16)V9X_I9XX_TRIANGLE_DWORDS);
}

/*
 * The general form: any number of triangles, each with its own colour.
 *
 * Phase 6 needs two triangles sharing an edge in different colours, which the
 * fixed single-triangle builder above cannot express. That builder is now a
 * call into this one rather than a second copy of the packet layout - the
 * packet offsets were wrong once already because two places derived the same
 * numbers independently, and one primitive emitter is the fix for that class.
 */
/*
 * One emission path for both runs.
 *
 * `z_bits` null is the flat Z=0 run Phase 5 has always emitted. Non-null
 * applies z_bits[i] to all three vertices of triangle i and bounds Y by the
 * depth buffer's height. A second copy of this loop is what the packet-offset
 * and primitive-offset defects were both made of.
 */
static v9x_status v9x_i9xx_build_run_common(
    const struct v9x_i9xx_triangle *triangles, v9x_u32 count,
    const v9x_u32 *z_bits, v9x_u32 width, v9x_u32 height,
    v9x_u32 *stream, v9x_u32 capacity, v9x_u32 *written);

v9x_status v9x_i9xx_build_triangle_run(
    const struct v9x_i9xx_triangle *triangles, v9x_u32 count,
    v9x_u32 width, v9x_u32 height,
    v9x_u32 *stream, v9x_u32 capacity, v9x_u32 *written)
{
    return v9x_i9xx_build_run_common(triangles, count, 0, width, height,
                                     stream, capacity, written);
}

v9x_status v9x_i9xx_build_depth_run(
    const struct v9x_i9xx_triangle *triangles, v9x_u32 count,
    const v9x_u32 *z_bits, v9x_u32 width, v9x_u32 height,
    v9x_u32 *stream, v9x_u32 capacity, v9x_u32 *written)
{
    if (z_bits == 0) {
        /* A depth run with no depths is a mistake, not a flat one. The caller
         * that wanted flat should have asked for a triangle run. */
        if (written != 0) { *written = 0ul; }
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    return v9x_i9xx_build_run_common(triangles, count, z_bits, width, height,
                                     stream, capacity, written);
}

static v9x_status v9x_i9xx_build_run_common(
    const struct v9x_i9xx_triangle *triangles, v9x_u32 count,
    const v9x_u32 *z_bits, v9x_u32 width, v9x_u32 height,
    v9x_u32 *stream, v9x_u32 capacity, v9x_u32 *written)
{
    v9x_u32 at = 0ul;
    v9x_u32 index;
    v9x_u32 vertex;
    v9x_u32 run_dwords;
    v9x_u32 zero_bits = 0ul;
    v9x_u32 one_bits = 0ul;

    if (written != 0) { *written = 0ul; }
    if (stream == 0 || written == 0 || triangles == 0 ||
        count == 0ul || count > V9X_I9XX_SCENE_MAX_TRIANGLES ||
        width == 0ul || height == 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    run_dwords = v9x_i9xx_triangle_run_dwords(count);
    if (run_dwords == 0ul || capacity < run_dwords) {
        return V9X_STATUS_INSUFFICIENT_MEMORY;
    }

    /* Same converter for Z and W as for the coordinates, so a fault in it
     * cannot be masked by hand-written constants. */
    if (v9x_i9xx_float_from_int(0ul, &zero_bits) != V9X_I9XX_FLOAT_OK ||
        v9x_i9xx_float_from_int(1ul, &one_bits) != V9X_I9XX_FLOAT_OK) {
        return V9X_STATUS_INVALID_STATE;
    }

    /*
     * ONE primitive command for the whole run, not one per triangle. A
     * triangle list takes 3n vertices under a single header, and the length
     * field counts every vertex dword in the run less one.
     */
    /*
     * The length counts every vertex dword in the run less one, and the run
     * length already includes the command dword - hence the two.
     */
    stream[at++] = V9X_I9XX_3DPRIMITIVE_INLINE |
                   V9X_I9XX_PRIM3D_TRILIST |
                   (run_dwords - 2ul);

    for (index = 0ul; index < count; ++index) {
        for (vertex = 0ul; vertex < V9X_I9XX_VERTEX_COUNT; ++vertex) {
            v9x_u32 x_bits = 0ul;
            v9x_u32 y_bits = 0ul;

            /*
             * Refuse anything outside the drawing rectangle rather than let
             * the hardware clip it. The rectangle is inclusive, so the last
             * addressable pixel is width - 1; a clipped vertex would make the
             * software reference disagree for a reason invisible in the
             * capture.
             */
            if (triangles[index].x[vertex] >= width ||
                triangles[index].y[vertex] >= height) {
                return V9X_STATUS_INVALID_ARGUMENT;
            }
            /*
             * A depth scene is bounded by the DEPTH buffer as well, which is
             * shorter than the render target - 256 rows against 480, because
             * that is what the reserve has left. A pixel below it would have
             * the hardware address depth memory past the allocation, and the
             * first thing it would reach is the guard page.
             *
             * Refused here rather than caught by the guard afterwards: a
             * guard says something went wrong, this says what.
             */
            if (z_bits != 0 &&
                triangles[index].y[vertex] >= V9X_I9XX_DEPTH_HEIGHT) {
                return V9X_STATUS_INVALID_ARGUMENT;
            }
            if (v9x_i9xx_float_from_int(triangles[index].x[vertex],
                                        &x_bits) != V9X_I9XX_FLOAT_OK ||
                v9x_i9xx_float_from_int(triangles[index].y[vertex],
                                        &y_bits) != V9X_I9XX_FLOAT_OK) {
                return V9X_STATUS_INVALID_ARGUMENT;
            }

            stream[at++] = x_bits;
            stream[at++] = y_bits;
            /*
             * Z: flat per triangle, which is all an occlusion test needs and
             * which keeps depth interpolation out of a measurement that is
             * about the test. The bit pattern is the scene's, because the
             * converter takes integers and these are fractions.
             */
            stream[at++] = (z_bits != 0) ? z_bits[index] : zero_bits;
            stream[at++] = one_bits;
            /*
             * One colour per TRIANGLE, repeated at its three vertices. Flat
             * versus smooth shading is then moot and the provoking-vertex
             * rules stay off the critical path - both were single-sourced.
             * Two triangles may still differ from each other, which is what
             * makes a shared edge readable.
             */
            /* The vertex's own colour when the scene carries three, and
             * the triangle's one colour otherwise. A scene that sets
             * per_vertex without filling the array would emit zeros, which is
             * black and visible, rather than silently repeating one value. */
            stream[at++] = triangles[index].per_vertex != V9X_FALSE
                ? triangles[index].vertex_color[vertex]
                : triangles[index].color;
        }
    }

    *written = at;
    return V9X_STATUS_OK;
}

/*
 * The TEXTURED vertex run: position, colour, then one 2D coordinate set.
 *
 * Seven dwords per vertex rather than five. The order is not a choice - Mesa
 * emits attributes in a fixed sequence and that sequence IS the layout:
 * position, point size, primary colour, secondary colour, fog, then texture
 * coordinates. Audit section 7.
 *
 * S4 does not change. It carries no texture-coordinate field at all - its bits
 * are point width, specular fog, colour, depth offset, position format and a
 * fog parameter - and S2 alone declares that a coordinate set exists. That was
 * the single largest risk in the plan and the audit removed it.
 *
 * The coordinates are NORMALIZED, which is what SS3_NORMALIZED_COORDS selects,
 * so they are independent of the texture's size. A vertex at u=1 samples the
 * right-hand edge whatever the texture turns out to be.
 */
v9x_status v9x_i9xx_build_textured_run(
    const struct v9x_i9xx_triangle *triangles, v9x_u32 count,
    const v9x_u32 *u_bits, const v9x_u32 *v_bits,
    v9x_u32 width, v9x_u32 height,
    v9x_u32 *stream, v9x_u32 capacity, v9x_u32 *written)
{
    v9x_u32 at = 0ul;
    v9x_u32 index;
    v9x_u32 vertex;
    v9x_u32 vertices;
    v9x_u32 zero_bits = 0ul;
    v9x_u32 one_bits = 0ul;

    if (written != 0) { *written = 0ul; }
    if (stream == 0 || written == 0 || triangles == 0 ||
        u_bits == 0 || v_bits == 0 ||
        count == 0ul || count > V9X_I9XX_SCENE_MAX_TRIANGLES ||
        width == 0ul || height == 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    vertices = (v9x_u32)((v9x_u16)count * (v9x_u16)V9X_I9XX_VERTEX_COUNT);
    if (capacity < v9x_i9xx_textured_run_dwords(count)) {
        return V9X_STATUS_INSUFFICIENT_MEMORY;
    }

    if (v9x_i9xx_float_from_int(0ul, &zero_bits) != V9X_I9XX_FLOAT_OK ||
        v9x_i9xx_float_from_int(1ul, &one_bits) != V9X_I9XX_FLOAT_OK) {
        return V9X_STATUS_INVALID_STATE;
    }

    /* One primitive command for the whole run; the length counts every vertex
     * dword less one, and a textured vertex is seven. */
    stream[at++] = V9X_I9XX_3DPRIMITIVE_INLINE |
                   V9X_I9XX_PRIM3D_TRILIST |
                   ((v9x_u32)((v9x_u16)vertices *
                              (v9x_u16)V9X_I9XX_TEXTURED_VERTEX_DWORDS) - 1ul);

    for (index = 0ul; index < count; ++index) {
        for (vertex = 0ul; vertex < V9X_I9XX_VERTEX_COUNT; ++vertex) {
            v9x_u32 x_bits = 0ul;
            v9x_u32 y_bits = 0ul;
            v9x_u32 slot = (v9x_u32)((v9x_u16)index *
                                     (v9x_u16)V9X_I9XX_VERTEX_COUNT) + vertex;

            if (triangles[index].x[vertex] >= width ||
                triangles[index].y[vertex] >= height) {
                return V9X_STATUS_INVALID_ARGUMENT;
            }
            if (v9x_i9xx_float_from_int(triangles[index].x[vertex],
                                        &x_bits) != V9X_I9XX_FLOAT_OK ||
                v9x_i9xx_float_from_int(triangles[index].y[vertex],
                                        &y_bits) != V9X_I9XX_FLOAT_OK) {
                return V9X_STATUS_INVALID_ARGUMENT;
            }

            stream[at++] = x_bits;
            stream[at++] = y_bits;
            stream[at++] = zero_bits;
            stream[at++] = one_bits;
            /* The vertex's own colour when the scene carries three, and
             * the triangle's one colour otherwise. A scene that sets
             * per_vertex without filling the array would emit zeros, which is
             * black and visible, rather than silently repeating one value. */
            stream[at++] = triangles[index].per_vertex != V9X_FALSE
                ? triangles[index].vertex_color[vertex]
                : triangles[index].color;
            /*
             * The coordinates last, AFTER the colour. Taken as float bits
             * rather than built here: the only values this scene needs are 0
             * and 1, and the converter this file uses takes integers - it
             * cannot express a coordinate between them. Passing bits keeps
             * that limit visible at the call site instead of hiding it behind
             * a converter that silently rounds.
             */
            stream[at++] = u_bits[slot];
            stream[at++] = v_bits[slot];
        }
    }

    *written = at;
    return V9X_STATUS_OK;
}

/*
 * Dwords a textured run occupies. Same link-time constraint as the untextured
 * one: the multiply is done in 16 bits because count is bounded first, and a
 * 32-bit multiply in I9XXCODE calls __U4M in a segment a near call cannot
 * reach.
 */
v9x_u32 v9x_i9xx_textured_run_dwords(v9x_u32 count)
{
    if (count == 0ul || count > V9X_I9XX_SCENE_MAX_TRIANGLES) {
        return 0ul;
    }
    return 1ul + (v9x_u32)((v9x_u16)count *
                           (v9x_u16)(V9X_I9XX_VERTEX_COUNT *
                                     V9X_I9XX_TEXTURED_VERTEX_DWORDS));
}

/*
 * Dwords a textured runtime run occupies: the command plus seven per vertex.
 *
 * The bound on `triangles` comes first so the multiply stays in sixteen bits.
 * A 32-bit multiply here calls __U4M in the default code segment, which a
 * near call from I9XXCODE cannot reach - E2052, and a link error rather than
 * a run-time one only because the linker checks segments.
 */
/*
 * Dwords an UNTEXTURED application batch occupies.
 *
 * Separate from v9x_i9xx_triangle_run_dwords, which a scene uses and which is
 * bounded at three: a scene and a batch are different things and sharing the
 * bound silently limited every application draw to three triangles.
 */
v9x_u32 v9x_i9xx_runtime_run_dwords(v9x_u32 triangles)
{
    if (triangles == 0ul || triangles > V9X_I9XX_RUNTIME_MAX_TRIANGLES) {
        return 0ul;
    }
    return 1ul + (v9x_u32)((v9x_u16)triangles *
                           (v9x_u16)V9X_I9XX_TRIANGLE_DWORDS);
}

v9x_u32 v9x_i9xx_runtime_textured_run_dwords(v9x_u32 triangles)
{
    if (triangles == 0ul || triangles > V9X_I9XX_RUNTIME_MAX_TRIANGLES) {
        return 0ul;
    }
    return 1ul + (v9x_u32)((v9x_u16)triangles *
                           (v9x_u16)(V9X_I9XX_VERTEX_COUNT *
                                     V9X_I9XX_TEXTURED_VERTEX_DWORDS));
}

/*
 * One emission path for application geometry, textured or not.
 *
 * `uv` null is the five-dword vertex the runtime path has always emitted;
 * non-null appends the coordinate pair and makes it seven. The alternative
 * was a second copy of the coordinate, depth and rhw checks, and those checks
 * are the memory-safety argument for the whole runtime path - two copies is
 * two places for them to drift, which is the defect class this project keeps
 * finding.
 */
static v9x_status v9x_i9xx_build_runtime_run_common(
    const v9x_u32 *xyzw, const v9x_u32 *colors, const v9x_u32 *uv,
    v9x_u32 triangles, v9x_u32 width, v9x_u32 height,
    v9x_u32 *stream, v9x_u32 capacity, v9x_u32 *written)
{
    v9x_u32 at = 0ul;
    v9x_u32 vertices;
    v9x_u32 vertex;
    v9x_u32 run_dwords;
    v9x_u32 width_bits = 0ul;
    v9x_u32 height_bits = 0ul;
    v9x_u32 one_bits = 0ul;

    if (written != 0) { *written = 0ul; }
    if (stream == 0 || written == 0 || xyzw == 0 || colors == 0 ||
        triangles == 0ul || width == 0ul || height == 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    run_dwords = uv != 0
        ? v9x_i9xx_runtime_textured_run_dwords(triangles)
        : v9x_i9xx_runtime_run_dwords(triangles);
    if (run_dwords == 0ul || capacity < run_dwords) {
        return V9X_STATUS_INSUFFICIENT_MEMORY;
    }

    /*
     * The bounds as FLOATS, converted once through the same helper the scene
     * builders use. Comparing a coordinate's bit pattern against a bound
     * derived any other way would be two opinions about the same edge.
     *
     * The bound is the extent rather than the last pixel: a vertex exactly at
     * the right edge is legal geometry - a triangle covering the whole surface
     * has one - and the rasteriser's own sample rule decides whether the last
     * column is covered.
     */
    if (v9x_i9xx_float_from_int(width, &width_bits) != V9X_I9XX_FLOAT_OK ||
        v9x_i9xx_float_from_int(height, &height_bits) != V9X_I9XX_FLOAT_OK ||
        v9x_i9xx_float_from_int(1ul, &one_bits) != V9X_I9XX_FLOAT_OK) {
        return V9X_STATUS_INVALID_STATE;
    }

    vertices = (v9x_u32)((v9x_u16)V9X_I9XX_VERTEX_COUNT *
                         (v9x_u16)triangles);

    /* The length counts vertex dwords less one, and run_dwords counts them
     * plus the command - so this is the same expression for a five-dword
     * vertex and a seven-dword one, which is why the layout is not named
     * here. */
    stream[at++] = V9X_I9XX_3DPRIMITIVE_INLINE |
                   V9X_I9XX_PRIM3D_TRILIST |
                   (run_dwords - 2ul);

    for (vertex = 0ul; vertex < vertices; ++vertex) {
        v9x_u32 base = vertex * 4ul;

        /*
         * Every coordinate inside the rectangle it will be rasterised in.
         *
         * The core clips before an engine is called, so a vertex outside it
         * means the core and this engine disagree about the target - and the
         * consequence is not a wrong picture, it is a write outside the
         * surface. On this part that is a write to a page that may not be
         * ours, which is the failure everything else here is arranged to
         * prevent.
         */
        if (v9x_i9xx_float_in_range(xyzw[base], width_bits) == V9X_FALSE ||
            v9x_i9xx_float_in_range(xyzw[base + 1ul], height_bits) ==
                V9X_FALSE) {
            return V9X_STATUS_INVALID_ARGUMENT;
        }
        /* Z in [0, 1]: the depth range the hardware is given, and the range a
         * post-transform vertex is defined over. */
        if (v9x_i9xx_float_in_range(xyzw[base + 2ul], one_bits) ==
                V9X_FALSE) {
            return V9X_STATUS_INVALID_ARGUMENT;
        }
        /*
         * RHW, and it is NOT required to be one.
         *
         * It was, on the reasoning that these are post-transform vertices so
         * W must already have been divided through. That confuses W with its
         * reciprocal: the fourth float of a D3DTLVERTEX is rhw, which the
         * hardware uses to interpolate across the triangle rather than to
         * divide the coordinates again, and which Direct3D defines as varying
         * with projection. Requiring 1.0f refused ordinary projected geometry
         * before it reached the stream, and every scene this project has
         * measured uses 1.0f, so nothing here disagreed with it.
         *
         * X and Y stay bounded and Z stays in [0, 1]. Those say where the
         * rasteriser may write; this says how it shades between the corners,
         * and fixing a value for it fixes what may be drawn.
         */
        if (v9x_i9xx_float_positive_finite(xyzw[base + 3ul]) == V9X_FALSE) {
            return V9X_STATUS_INVALID_ARGUMENT;
        }

        stream[at++] = xyzw[base];
        stream[at++] = xyzw[base + 1ul];
        stream[at++] = xyzw[base + 2ul];
        stream[at++] = xyzw[base + 3ul];
        /* The colour is NOT checked. Every 32-bit value is a legal colour,
         * and a decoder or builder asserting one would be asserting what the
         * application may draw. */
        stream[at++] = colors[vertex];
        /*
         * The coordinates last, after the colour, which is Mesa's fixed
         * attribute order and therefore the layout - position, point size,
         * primary colour, secondary colour, fog, then texture coordinates.
         *
         * Refused only for the values that name no place on a texture. A
         * coordinate outside [0, 1] is ORDINARY: the sampler normalizes, so
         * two is the far edge of the second tile and a negative one is the
         * tile to the left, and which of those an application sees is the
         * wrap mode's business rather than this builder's.
         */
        if (uv != 0) {
            v9x_u32 pair = vertex * 2ul;

            if (v9x_i9xx_float_finite(uv[pair]) == V9X_FALSE ||
                v9x_i9xx_float_finite(uv[pair + 1ul]) == V9X_FALSE) {
                return V9X_STATUS_INVALID_ARGUMENT;
            }
            stream[at++] = uv[pair];
            stream[at++] = uv[pair + 1ul];
        }
    }

    *written = at;
    return V9X_STATUS_OK;
}

v9x_status v9x_i9xx_build_runtime_run(
    const v9x_u32 *xyzw, const v9x_u32 *colors, v9x_u32 triangles,
    v9x_u32 width, v9x_u32 height,
    v9x_u32 *stream, v9x_u32 capacity, v9x_u32 *written)
{
    return v9x_i9xx_build_runtime_run_common(xyzw, colors, 0, triangles,
                                             width, height, stream,
                                             capacity, written);
}

v9x_status v9x_i9xx_build_textured_runtime_run(
    const v9x_u32 *xyzw, const v9x_u32 *colors, const v9x_u32 *uv,
    v9x_u32 triangles, v9x_u32 width, v9x_u32 height,
    v9x_u32 *stream, v9x_u32 capacity, v9x_u32 *written)
{
    if (uv == 0) {
        /* A textured run with no coordinates is a mistake, not an untextured
         * run: the vertex format the state block declares would disagree with
         * the dwords emitted, which the pipeline emitter calls the single most
         * likely silent hang in the phase. */
        if (written != 0) { *written = 0ul; }
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    return v9x_i9xx_build_runtime_run_common(xyzw, colors, uv, triangles,
                                             width, height, stream,
                                             capacity, written);
}
