/*
 * Phase 6 scenes: several independent draws on one armed boot.
 *
 * Each scene is a complete stream - fill, full state block, fragment program,
 * MI probe, triangle run - built and submitted on its own, with its probes
 * flushed to disk before the next scene submits anything. The argument for
 * bundling, and the one rule that makes it safe, are in
 * docs\plans\intel-phase6-bundled-scenes.md: a scene may not depend on state
 * another scene left behind.
 *
 * Execution of more than one draw per boot was authorised on 2026-09-16,
 * bounded at five (docs\decisions\2026-09-15-intel-phase5-errata-gate.md).
 * V9X_I9XX_SCENE_AUTHORISED_DRAWS carries that bound, and the build refuses to
 * exceed it - the constant exists so the number is visible in the code that
 * acts on it, not so it is easy to change. Raising it needs another decision
 * recorded in that file.
 *
 * The authorisation rests on the independence rule above, not merely on the
 * count: a scene that inherited another scene's state would be outside it even
 * at two.
 *
 * The scene table is the single source of truth for what a build draws. The
 * generator, the arm tables, the validators and the capture writer all read
 * it, for the reason v9x_i9xx_phase5_parameters exists: Phase 4's equivalent
 * was four hand-maintained copies and three of them were stale.
 */
#include "velocity9x/intel_gen3_3d.h"
#include "velocity9x/intel_gma.h"

/*
 * Scene 1's colour, chosen to close the one question the two measured colours
 * left open.
 *
 * Green agreed with truncation at both values tested so far - 100 gives 25
 * either way, 135 gives 33 either way - so a backend that rounds red and blue
 * and truncates green fits every observation. Green 3 separates them:
 * round(3*63/255) = 1 against 3>>2 = 0.
 *
 * Red 46 and blue 200 are fresh values that also separate, in opposite
 * directions, so the boot re-tests three channels rather than one:
 *
 *     ch  byte  trunc  round8  floor  round  ceil
 *     R   46    5      6       5      6      6
 *     G   3     0      1       0      1      1
 *     B   200   25     25      24     24     25
 *
 * Predicted stores: round 0x3038, trunc 0x2819, floor 0x2818, round8 and
 * ceil 0x3039. Written down before the build, as the last colour was, because
 * a prediction made afterwards is a fit.
 *
 * docs\issues\2026-09-15-intel-565-conversion-outside-measured-values.md
 */
#define V9X_I9XX_SCENE1_COLOR_BGRA  ((v9x_u32)0xff2e03c8ul)

/*
 * The second measured colour, for the edge scenes. 0xfff86428 stores 0xf325.
 *
 * Both edge colours are ALREADY MEASURED, which keeps the edge rule the single
 * unknown in those scenes. Carrying an unmeasured colour into them would put
 * two unknowns in one result.
 */
#define V9X_I9XX_MEASURED_COLOR_B   ((v9x_u32)0xfff86428ul)

/*
 * The edge square, and why its diagonal has slope EXACTLY ONE.
 *
 * The hardware samples at pixel centres - DSTORG's half-pixel bias in both
 * axes, double-sourced in the packet audit - so a sample point is
 * (i + 0.5, j + 0.5). An exact-edge inclusion rule can only be observed at a
 * sample point that lies ON the edge, and whether any does is a property of
 * the geometry, not of the probes.
 *
 * The first version of this scene used (200,150)-(440,330), whose diagonal
 * satisfies 4y = 3x. Substituting a sample centre gives 4j + 2 = 3i + 1.5, so
 * 3i - 4j = 0.5: the left side is an integer and the right is not, and NOT ONE
 * sample centre in the whole square lies on that edge. The scene could not
 * have distinguished any inclusion rule, and would have produced a
 * confident-looking capture that answered nothing.
 *
 * Slope one fixes it. The edge is y = x - 50, and a centre lies on it whenever
 * j + 0.5 = i + 0.5 - 50, that is j = i - 50 - which has 241 integer solutions
 * across this square. Searched exhaustively rather than reasoned about, and
 * the host test recomputes it in integer arithmetic through its own
 * v9x_test_probe_on_edge, which is host-side because the cross product is a
 * 32-bit multiply that does not link from this segment.
 */
#define V9X_I9XX_EDGE_LEFT          200ul
#define V9X_I9XX_EDGE_TOP           150ul
#define V9X_I9XX_EDGE_RIGHT         440ul
#define V9X_I9XX_EDGE_BOTTOM        390ul

/*
 * Ids are ASSIGNED, not derived from the table index.
 *
 * A scene's id is how its probe set is attributed in the capture and in every
 * decision record that cites one. Deriving it from position would silently
 * renumber past evidence the first time a scene is inserted or reordered.
 */
#define V9X_I9XX_SCENE_ID_PHASE5    ((v9x_u32)0ul)
#define V9X_I9XX_SCENE_ID_COLOR     ((v9x_u32)1ul)
#define V9X_I9XX_SCENE_ID_EDGE_UP   ((v9x_u32)2ul)
#define V9X_I9XX_SCENE_ID_EDGE_LOW  ((v9x_u32)3ul)
#define V9X_I9XX_SCENE_ID_EDGE_BOTH ((v9x_u32)4ul)

/*
 * The MI probe, immediately before each scene's 3D work. Worth its two dwords
 * for the reason Phase 5 records: it separates "the ring is dead" from "the
 * packets are wrong", which are different faults with different responses.
 */
#define V9X_I9XX_SCENE_PROBE_DWORDS ((v9x_u32)2ul)
/* The GPU-side fill: XY_COLOR_BLT plus its MI_FLUSH. */
#define V9X_I9XX_SCENE_FILL_DWORDS  ((v9x_u32)7ul)

/*
 * The bound the 2026-09-16 amendment set, and the number of scenes this build
 * defines. They are separate constants deliberately: the first is a decision
 * and the second is a build, and a build that quietly grew past its
 * authorisation should fail here rather than on the machine.
 */
#define V9X_I9XX_SCENE_AUTHORISED_DRAWS ((v9x_u32)5ul)
#define V9X_I9XX_SCENE_COUNT        ((v9x_u32)5ul)

/*
 * The three shared-edge probe pixels, and the two flanking columns.
 *
 * Each edge pixel (i, i - 50) has its centre exactly on the diagonal. The
 * flanks are the same row, two columns either side: at a given y the upper
 * triangle covers x from the diagonal to the right edge, so +2 is inside it
 * and -2 is inside the lower one. Two columns rather than one, so a
 * half-open-interval difference at the boundary cannot land on a flank probe
 * and be confused with the edge measurement.
 */
#define V9X_I9XX_EDGE_PROBE_A_X     250u
#define V9X_I9XX_EDGE_PROBE_B_X     320u
#define V9X_I9XX_EDGE_PROBE_C_X     390u
#define V9X_I9XX_EDGE_PROBE_DY      50u
#define V9X_I9XX_EDGE_FLANK         2u

static void v9x_i9xx_scene_triangle(
    struct v9x_i9xx_triangle *out,
    v9x_u32 x0, v9x_u32 y0, v9x_u32 x1, v9x_u32 y1,
    v9x_u32 x2, v9x_u32 y2, v9x_u32 color)
{
    out->x[0] = x0;
    out->y[0] = y0;
    out->x[1] = x1;
    out->y[1] = y1;
    out->x[2] = x2;
    out->y[2] = y2;
    out->color = color;
}

static void v9x_i9xx_scene_probe(
    struct v9x_i9xx_scene *scene, const char *name,
    v9x_u16 x, v9x_u16 y, v9x_u16 expect)
{
    struct v9x_i9xx_probe *probe;

    if (scene->probe_count >= V9X_I9XX_SCENE_MAX_PROBES) {
        return;
    }
    probe = &scene->probes[scene->probe_count];
    probe->name = name;
    probe->x = x;
    probe->y = y;
    probe->expect = expect;
    ++scene->probe_count;
}

static void v9x_i9xx_scene_clear(struct v9x_i9xx_scene *out)
{
    v9x_u32 index;

    out->id = 0ul;
    out->fill_dword = 0ul;
    out->triangle_count = 0ul;
    out->probe_count = 0ul;
    for (index = 0ul; index < V9X_I9XX_SCENE_MAX_TRIANGLES; ++index) {
        v9x_i9xx_scene_triangle(&out->triangles[index],
                                0ul, 0ul, 0ul, 0ul, 0ul, 0ul, 0ul);
    }
    for (index = 0ul; index < V9X_I9XX_SCENE_MAX_PROBES; ++index) {
        out->probes[index].name = "";
        out->probes[index].x = 0u;
        out->probes[index].y = 0u;
        out->probes[index].expect = V9X_I9XX_PROBE_FILL;
    }
}

/*
 * Phase 5's fourteen probes, unchanged and in their original order.
 *
 * Scene 0's comparison against C:\temp\intel42 is pixel for pixel at these
 * coordinates, so reordering or renaming them breaks the only regression
 * evidence this build has.
 */
static void v9x_i9xx_scene_phase5_probes(struct v9x_i9xx_scene *scene)
{
    v9x_i9xx_scene_probe(scene, "Centroid", 320u, 213u,
                         V9X_I9XX_PROBE_TRIANGLE0);
    v9x_i9xx_scene_probe(scene, "NearV0", 175u, 128u,
                         V9X_I9XX_PROBE_TRIANGLE0);
    v9x_i9xx_scene_probe(scene, "NearV1", 465u, 128u,
                         V9X_I9XX_PROBE_TRIANGLE0);
    v9x_i9xx_scene_probe(scene, "NearV2", 320u, 385u,
                         V9X_I9XX_PROBE_TRIANGLE0);
    v9x_i9xx_scene_probe(scene, "MidTop", 320u, 130u,
                         V9X_I9XX_PROBE_TRIANGLE0);
    v9x_i9xx_scene_probe(scene, "MidLeft", 250u, 250u,
                         V9X_I9XX_PROBE_TRIANGLE0);
    v9x_i9xx_scene_probe(scene, "MidRight", 390u, 250u,
                         V9X_I9XX_PROBE_TRIANGLE0);
    v9x_i9xx_scene_probe(scene, "Corner00", 0u, 0u, V9X_I9XX_PROBE_FILL);
    v9x_i9xx_scene_probe(scene, "CornerX0", 639u, 0u, V9X_I9XX_PROBE_FILL);
    v9x_i9xx_scene_probe(scene, "Corner0Y", 0u, 479u, V9X_I9XX_PROBE_FILL);
    v9x_i9xx_scene_probe(scene, "CornerXY", 639u, 479u, V9X_I9XX_PROBE_FILL);
    v9x_i9xx_scene_probe(scene, "OutsideTop", 320u, 40u,
                         V9X_I9XX_PROBE_FILL);
    v9x_i9xx_scene_probe(scene, "OutsideLeft", 40u, 400u,
                         V9X_I9XX_PROBE_FILL);
    v9x_i9xx_scene_probe(scene, "OutsideRight", 600u, 400u,
                         V9X_I9XX_PROBE_FILL);
}

/*
 * The edge probes, IDENTICAL COORDINATES in all three edge scenes.
 *
 * That is what makes coverage comparable pixel by pixel. Two opaque triangles
 * in one scene cannot reveal double coverage - the second simply overwrites
 * the first, and the result is indistinguishable from coverage by the second
 * alone - so the question is answered by drawing each triangle ON ITS OWN and
 * comparing:
 *
 *     upper alone   lower alone   conclusion
 *     covered       covered       DOUBLE coverage
 *     covered       fill          cleanly the upper triangle's
 *     fill          covered       cleanly the lower triangle's
 *     fill          fill          a GAP - neither claims the pixel
 *
 * The combined scene then shows what the hardware produces when both arrive
 * under one primitive, which is the case a real mesh presents. It is read
 * against the two single-triangle scenes, never on its own.
 *
 * The edge pixels themselves are always MEASURE. What they hold is the thing
 * being measured, and an expectation there would be a guess written down as
 * evidence.
 */
static void v9x_i9xx_scene_edge_probes(
    struct v9x_i9xx_scene *scene, v9x_u16 right_of, v9x_u16 left_of)
{
    v9x_i9xx_scene_probe(scene, "EdgeA",
                         V9X_I9XX_EDGE_PROBE_A_X,
                         V9X_I9XX_EDGE_PROBE_A_X - V9X_I9XX_EDGE_PROBE_DY,
                         V9X_I9XX_PROBE_MEASURE);
    v9x_i9xx_scene_probe(scene, "EdgeB",
                         V9X_I9XX_EDGE_PROBE_B_X,
                         V9X_I9XX_EDGE_PROBE_B_X - V9X_I9XX_EDGE_PROBE_DY,
                         V9X_I9XX_PROBE_MEASURE);
    v9x_i9xx_scene_probe(scene, "EdgeC",
                         V9X_I9XX_EDGE_PROBE_C_X,
                         V9X_I9XX_EDGE_PROBE_C_X - V9X_I9XX_EDGE_PROBE_DY,
                         V9X_I9XX_PROBE_MEASURE);
    v9x_i9xx_scene_probe(scene, "FlankRight",
                         V9X_I9XX_EDGE_PROBE_B_X + V9X_I9XX_EDGE_FLANK,
                         V9X_I9XX_EDGE_PROBE_B_X - V9X_I9XX_EDGE_PROBE_DY,
                         right_of);
    v9x_i9xx_scene_probe(scene, "FlankLeft",
                         V9X_I9XX_EDGE_PROBE_B_X - V9X_I9XX_EDGE_FLANK,
                         V9X_I9XX_EDGE_PROBE_B_X - V9X_I9XX_EDGE_PROBE_DY,
                         left_of);
    /* Well inside each half, where no edge rule can reach. */
    v9x_i9xx_scene_probe(scene, "UpperBody", 420u, 200u, right_of);
    v9x_i9xx_scene_probe(scene, "LowerBody", 210u, 380u, left_of);
    /* Outside the square entirely, in every edge scene. */
    v9x_i9xx_scene_probe(scene, "OutsideEdge", 40u, 400u,
                         V9X_I9XX_PROBE_FILL);
}

v9x_u32 v9x_i9xx_scene_count(void)
{
    /*
     * Refuses rather than clamps. A build defining more scenes than the errata
     * decision authorises is a mistake about what was agreed, and returning a
     * silently truncated set would execute four fifths of it and look like it
     * worked.
     */
    if (V9X_I9XX_SCENE_COUNT > V9X_I9XX_SCENE_AUTHORISED_DRAWS) {
        return 0ul;
    }
    return V9X_I9XX_SCENE_COUNT;
}

v9x_u32 v9x_i9xx_scene_authorised_draws(void)
{
    return V9X_I9XX_SCENE_AUTHORISED_DRAWS;
}

v9x_status v9x_i9xx_scene_at(v9x_u32 index, struct v9x_i9xx_scene *out)
{
    if (out == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    v9x_i9xx_scene_clear(out);
    if (index >= V9X_I9XX_SCENE_COUNT) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    out->fill_dword = V9X_I9XX_FILL_DWORD;

    /*
     * Scene 0 is the Phase 5 triangle, unchanged in every respect.
     *
     * It runs FIRST and nothing precedes it, which is what keeps its result
     * comparable byte for byte against C:\temp\intel42 - seven interior probes
     * at 0x1c3e and seven exterior at 0x0842. That comparison is the
     * regression evidence for anything else this build changes, and it is only
     * worth having while this scene stays first and stays identical.
     */
    if (index == 0ul) {
        out->id = V9X_I9XX_SCENE_ID_PHASE5;
        out->triangle_count = 1ul;
        v9x_i9xx_scene_triangle(&out->triangles[0],
                                (v9x_u32)V9X_I9XX_TRI_X0,
                                (v9x_u32)V9X_I9XX_TRI_Y0,
                                (v9x_u32)V9X_I9XX_TRI_X1,
                                (v9x_u32)V9X_I9XX_TRI_Y1,
                                (v9x_u32)V9X_I9XX_TRI_X2,
                                (v9x_u32)V9X_I9XX_TRI_Y2,
                                V9X_I9XX_TRI_COLOR_BGRA);
        v9x_i9xx_scene_phase5_probes(out);
        return V9X_STATUS_OK;
    }

    /*
     * Scene 1: the same geometry, a different colour. Identical geometry on
     * purpose - it means any difference between scenes 0 and 1 is the colour
     * and nothing else, so the conversion question is answered without the
     * rasteriser's behaviour entering the comparison.
     */
    if (index == 1ul) {
        out->id = V9X_I9XX_SCENE_ID_COLOR;
        out->triangle_count = 1ul;
        v9x_i9xx_scene_triangle(&out->triangles[0],
                                (v9x_u32)V9X_I9XX_TRI_X0,
                                (v9x_u32)V9X_I9XX_TRI_Y0,
                                (v9x_u32)V9X_I9XX_TRI_X1,
                                (v9x_u32)V9X_I9XX_TRI_Y1,
                                (v9x_u32)V9X_I9XX_TRI_X2,
                                (v9x_u32)V9X_I9XX_TRI_Y2,
                                V9X_I9XX_SCENE1_COLOR_BGRA);
        v9x_i9xx_scene_phase5_probes(out);
        return V9X_STATUS_OK;
    }

    /*
     * Scene 2: the UPPER triangle alone - the half right of the diagonal.
     * Vertices: top-left, top-right, bottom-right.
     */
    if (index == 2ul) {
        out->id = V9X_I9XX_SCENE_ID_EDGE_UP;
        out->triangle_count = 1ul;
        v9x_i9xx_scene_triangle(&out->triangles[0],
                                V9X_I9XX_EDGE_LEFT, V9X_I9XX_EDGE_TOP,
                                V9X_I9XX_EDGE_RIGHT, V9X_I9XX_EDGE_TOP,
                                V9X_I9XX_EDGE_RIGHT, V9X_I9XX_EDGE_BOTTOM,
                                V9X_I9XX_TRI_COLOR_BGRA);
        /* Right of the diagonal is this scene's triangle; left is fill,
         * because the lower triangle is not drawn here at all. */
        v9x_i9xx_scene_edge_probes(out, V9X_I9XX_PROBE_TRIANGLE0,
                                   V9X_I9XX_PROBE_FILL);
        return V9X_STATUS_OK;
    }

    /*
     * Scene 3: the LOWER triangle alone - the half left of the diagonal.
     * Vertices: top-left, bottom-right, bottom-left. Same shared edge, same
     * probe coordinates, opposite expectations.
     */
    if (index == 3ul) {
        out->id = V9X_I9XX_SCENE_ID_EDGE_LOW;
        out->triangle_count = 1ul;
        v9x_i9xx_scene_triangle(&out->triangles[0],
                                V9X_I9XX_EDGE_LEFT, V9X_I9XX_EDGE_TOP,
                                V9X_I9XX_EDGE_RIGHT, V9X_I9XX_EDGE_BOTTOM,
                                V9X_I9XX_EDGE_LEFT, V9X_I9XX_EDGE_BOTTOM,
                                V9X_I9XX_MEASURED_COLOR_B);
        v9x_i9xx_scene_edge_probes(out, V9X_I9XX_PROBE_FILL,
                                   V9X_I9XX_PROBE_TRIANGLE0);
        return V9X_STATUS_OK;
    }

    /*
     * Scene 4: both triangles under one primitive, upper first. This is the
     * case a real mesh presents, and it is read against scenes 2 and 3 rather
     * than on its own - by itself it cannot separate double coverage from
     * exclusive ownership, because the second triangle simply overwrites.
     */
    out->id = V9X_I9XX_SCENE_ID_EDGE_BOTH;
    out->triangle_count = 2ul;
    v9x_i9xx_scene_triangle(&out->triangles[0],
                            V9X_I9XX_EDGE_LEFT, V9X_I9XX_EDGE_TOP,
                            V9X_I9XX_EDGE_RIGHT, V9X_I9XX_EDGE_TOP,
                            V9X_I9XX_EDGE_RIGHT, V9X_I9XX_EDGE_BOTTOM,
                            V9X_I9XX_TRI_COLOR_BGRA);
    v9x_i9xx_scene_triangle(&out->triangles[1],
                            V9X_I9XX_EDGE_LEFT, V9X_I9XX_EDGE_TOP,
                            V9X_I9XX_EDGE_RIGHT, V9X_I9XX_EDGE_BOTTOM,
                            V9X_I9XX_EDGE_LEFT, V9X_I9XX_EDGE_BOTTOM,
                            V9X_I9XX_MEASURED_COLOR_B);
    v9x_i9xx_scene_edge_probes(out, V9X_I9XX_PROBE_TRIANGLE0,
                               V9X_I9XX_PROBE_TRIANGLE1);
    return V9X_STATUS_OK;
}

v9x_u32 v9x_i9xx_scene_extent(const struct v9x_i9xx_scene *scene)
{
    if (scene == 0 || scene->triangle_count == 0ul ||
        scene->triangle_count > V9X_I9XX_SCENE_MAX_TRIANGLES) {
        return 0ul;
    }
    /*
     * The triangle run's own helper rather than the multiply repeated here:
     * a runtime 32-bit multiply in this segment does not link, and two places
     * deriving the same length is how the published packet offsets came to be
     * seven dwords short.
     */
    return V9X_I9XX_SCENE_FILL_DWORDS +
           v9x_i9xx_3d_state_extent() +
           v9x_i9xx_fragment_program_extent() +
           V9X_I9XX_SCENE_PROBE_DWORDS +
           v9x_i9xx_triangle_run_dwords(scene->triangle_count);
}

/*
 * Dwords a scene submits BEFORE its _3DPRIMITIVE: the fill, the state block,
 * the fragment program and the MI probe.
 *
 * The executor submits in two halves and stops between them, so that a drain
 * of the first and a stall on the second says "the ring is alive and the state
 * was accepted, the PRIMITIVE is what hung". That only holds if the boundary
 * is exactly the primitive's first dword.
 *
 * Derived by SUBTRACTING the triangle run from the scene's own extent, not by
 * adding the prefix up a second time. The two would be independent
 * computations of the same number and could disagree - which is precisely what
 * happened to the executor: loader.asm carried the boundary as a literal 50,
 * correct for the 66-dword stream, and kept it when the depth BUF_INFO removal
 * moved the primitive to 47. That submitted three dwords INTO the primitive
 * and then drew through dwords nobody had staged.
 */
v9x_u32 v9x_i9xx_scene_primitive_offset(const struct v9x_i9xx_scene *scene)
{
    v9x_u32 extent;
    v9x_u32 run;

    extent = v9x_i9xx_scene_extent(scene);
    if (extent == 0ul) {
        return 0ul;
    }
    run = v9x_i9xx_triangle_run_dwords(scene->triangle_count);
    if (run == 0ul || run >= extent) {
        return 0ul;
    }
    return extent - run;
}

v9x_status v9x_i9xx_build_scene_stream(
    const struct v9x_i9xx_scene *scene,
    v9x_u32 *stream, v9x_u32 capacity, v9x_u32 *written)
{
    struct v9x_i9xx_sandbox_layout layout;
    v9x_u32 at = 0ul;
    v9x_u32 produced = 0ul;

    if (written != 0) { *written = 0ul; }
    if (stream == 0 || written == 0 || scene == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    /*
     * The layout is recomputed here rather than passed in, so the stream and
     * the reserve can never describe different memory. Same inputs and same
     * reasoning as the Phase 5 builder.
     */
    if (v9x_i9xx_sandbox_calculate(0x007b0000ul, 0x7f800000ul, &layout) !=
            V9X_STATUS_OK) {
        return V9X_STATUS_INVALID_STATE;
    }

    /*
     * The fill, first, and the GPU performs it. The CPU bulk-filling 600 KiB
     * through GMADR immediately before the GPU reads adjacent memory is the
     * closest thing in this design to erratum 12's own description of its
     * trigger, and the errata gate opened on condition that it not be done
     * (docs\decisions\2026-09-15-intel-phase5-errata-gate.md).
     *
     * Every scene fills, which is what destroys the previous scene's pixels
     * and what makes each scene independent of the last. The probes are
     * already on disk by then.
     */
    if (v9x_i9xx_build_color_blt(
            layout.target_offset,
            V9X_I9XX_FILL_BLT_WIDTH, V9X_I9XX_FILL_BLT_HEIGHT,
            (v9x_u16)layout.target_pitch, scene->fill_dword,
            layout.target_offset, layout.target_bytes,
            stream + at, capacity - at, &produced) != V9X_STATUS_OK) {
        return V9X_STATUS_INSUFFICIENT_MEMORY;
    }
    at += produced;
    if (capacity - at < 1ul) {
        return V9X_STATUS_INSUFFICIENT_MEMORY;
    }
    /* Every bit clear, which FLUSHES the render cache rather than inhibiting
     * it - bit 2 is an inhibit, the sign-inverted field the audit flagged. */
    stream[at++] = V9X_I9XX_MI_FLUSH;

    if (v9x_i9xx_build_3d_state(
            layout.target_offset, layout.target_pitch,
            V9X_I9XX_TARGET_WIDTH, V9X_I9XX_TARGET_HEIGHT,
            stream + at, capacity - at, &produced) != V9X_STATUS_OK) {
        return V9X_STATUS_INSUFFICIENT_MEMORY;
    }
    at += produced;

    if (v9x_i9xx_build_fragment_program(
            stream + at, capacity - at, &produced) != V9X_STATUS_OK) {
        return V9X_STATUS_INSUFFICIENT_MEMORY;
    }
    at += produced;

    if (capacity - at < V9X_I9XX_SCENE_PROBE_DWORDS) {
        return V9X_STATUS_INSUFFICIENT_MEMORY;
    }
    if (v9x_i9xx_build_mi_probe(stream + at, capacity - at, &produced) !=
            V9X_STATUS_OK) {
        return V9X_STATUS_INSUFFICIENT_MEMORY;
    }
    at += produced;

    if (v9x_i9xx_build_triangle_run(
            scene->triangles, scene->triangle_count,
            V9X_I9XX_TARGET_WIDTH, V9X_I9XX_TARGET_HEIGHT,
            stream + at, capacity - at, &produced) != V9X_STATUS_OK) {
        return V9X_STATUS_INSUFFICIENT_MEMORY;
    }
    at += produced;

    *written = at;
    return V9X_STATUS_OK;
}

v9x_u32 v9x_i9xx_scene_crc(v9x_u32 index)
{
    struct v9x_i9xx_scene scene;
    v9x_u32 stream[160];
    v9x_u32 written = 0ul;

    if (v9x_i9xx_scene_at(index, &scene) != V9X_STATUS_OK) {
        return 0ul;
    }
    if (v9x_i9xx_build_scene_stream(
            &scene, stream, (v9x_u32)(sizeof(stream) / sizeof(stream[0])),
            &written) != V9X_STATUS_OK) {
        return 0ul;
    }
    return v9x_i9xx_crc32_dwords(stream, written);
}

/*
 * Over every scene's dwords in execution order.
 *
 * Deliberately over the streams AS BUILT and concatenated, not over the
 * per-scene CRCs: the whole point of the arm gate is that the thing armed and
 * the thing submitted are the same bytes, and a CRC of CRCs would not hold if
 * a scene's length changed while its contents hashed the same.
 *
 * Returns zero if any scene cannot be built, which no arm path accepts.
 */
v9x_u32 v9x_i9xx_scene_combined_crc(void)
{
    struct v9x_i9xx_scene scene;
    v9x_u32 stream[512];
    v9x_u32 at = 0ul;
    v9x_u32 index;
    v9x_u32 produced = 0ul;

    for (index = 0ul; index < V9X_I9XX_SCENE_COUNT; ++index) {
        if (v9x_i9xx_scene_at(index, &scene) != V9X_STATUS_OK) {
            return 0ul;
        }
        if (v9x_i9xx_build_scene_stream(
                &scene, stream + at,
                (v9x_u32)(sizeof(stream) / sizeof(stream[0])) - at,
                &produced) != V9X_STATUS_OK) {
            return 0ul;
        }
        at += produced;
    }
    return v9x_i9xx_crc32_dwords(stream, at);
}

/*
 * Total probe reads across every scene, which is the number the aperture-read
 * hazard is actually about.
 *
 * Bulk reads hang this part: three succeeded and 153,600 hung
 * (docs\decisions\2026-09-15-bulk-aperture-reads-hang-the-945gse.md). The
 * budget is a published number rather than a hope, and the capture carries a
 * running count so a hang names the read it stopped at.
 */
v9x_u32 v9x_i9xx_scene_total_probes(void)
{
    struct v9x_i9xx_scene scene;
    v9x_u32 total = 0ul;
    v9x_u32 index;

    for (index = 0ul; index < V9X_I9XX_SCENE_COUNT; ++index) {
        if (v9x_i9xx_scene_at(index, &scene) != V9X_STATUS_OK) {
            return 0ul;
        }
        total += scene.probe_count;
    }
    return total;
}
