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
 * Scene 2's two triangles share the diagonal of a rectangle.
 *
 * Both colours are ALREADY MEASURED - 0xff1587f9 stores 0x1c3e and
 * 0xfff86428 stores 0xf325. That is deliberate: this scene asks about the
 * edge rule, and carrying an unmeasured colour into it would put two unknowns
 * in one result. A pixel on the shared edge is then unambiguous - it is one
 * colour, the other, or the fill, and each names a different rule.
 *
 * docs\issues\2026-09-15-intel-edge-fill-rule-unmeasured.md
 */
#define V9X_I9XX_SCENE2_LEFT        200ul
#define V9X_I9XX_SCENE2_TOP         150ul
#define V9X_I9XX_SCENE2_RIGHT       440ul
#define V9X_I9XX_SCENE2_BOTTOM      330ul

/*
 * Ids are ASSIGNED, not derived from the table index.
 *
 * A scene's id is how its probe set is attributed in the capture and in every
 * decision record that cites one. Deriving it from position would silently
 * renumber past evidence the first time a scene is inserted or reordered.
 */
#define V9X_I9XX_SCENE_ID_PHASE5    ((v9x_u32)0ul)
#define V9X_I9XX_SCENE_ID_COLOR     ((v9x_u32)1ul)
#define V9X_I9XX_SCENE_ID_EDGE      ((v9x_u32)2ul)

/*
 * The MI probe, immediately before each scene's 3D work. Worth its two dwords
 * for the reason Phase 5 records: it separates "the ring is dead" from "the
 * packets are wrong", which are different faults with different responses.
 */
#define V9X_I9XX_SCENE_PROBE_DWORDS ((v9x_u32)2ul)
/* The GPU-side fill: XY_COLOR_BLT plus its MI_FLUSH. */
#define V9X_I9XX_SCENE_FILL_DWORDS  ((v9x_u32)7ul)

#define V9X_I9XX_SCENE_COUNT        ((v9x_u32)3ul)

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

static void v9x_i9xx_scene_clear(struct v9x_i9xx_scene *out)
{
    v9x_u32 index;

    out->id = 0ul;
    out->fill_dword = 0ul;
    out->triangle_count = 0ul;
    for (index = 0ul; index < V9X_I9XX_SCENE_MAX_TRIANGLES; ++index) {
        v9x_i9xx_scene_triangle(&out->triangles[index],
                                0ul, 0ul, 0ul, 0ul, 0ul, 0ul, 0ul);
    }
}

v9x_u32 v9x_i9xx_scene_count(void)
{
    return V9X_I9XX_SCENE_COUNT;
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
        out->fill_dword = V9X_I9XX_FILL_DWORD;
        out->triangle_count = 1ul;
        v9x_i9xx_scene_triangle(&out->triangles[0],
                                (v9x_u32)V9X_I9XX_TRI_X0,
                                (v9x_u32)V9X_I9XX_TRI_Y0,
                                (v9x_u32)V9X_I9XX_TRI_X1,
                                (v9x_u32)V9X_I9XX_TRI_Y1,
                                (v9x_u32)V9X_I9XX_TRI_X2,
                                (v9x_u32)V9X_I9XX_TRI_Y2,
                                V9X_I9XX_TRI_COLOR_BGRA);
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
        out->fill_dword = V9X_I9XX_FILL_DWORD;
        out->triangle_count = 1ul;
        v9x_i9xx_scene_triangle(&out->triangles[0],
                                (v9x_u32)V9X_I9XX_TRI_X0,
                                (v9x_u32)V9X_I9XX_TRI_Y0,
                                (v9x_u32)V9X_I9XX_TRI_X1,
                                (v9x_u32)V9X_I9XX_TRI_Y1,
                                (v9x_u32)V9X_I9XX_TRI_X2,
                                (v9x_u32)V9X_I9XX_TRI_Y2,
                                V9X_I9XX_SCENE1_COLOR_BGRA);
        return V9X_STATUS_OK;
    }

    /*
     * Scene 2: two triangles meeting along the rectangle's diagonal, from
     * top-left to bottom-right. The shared edge is the subject; the two outer
     * corners are there so each triangle has an unambiguous interior to probe.
     */
    out->id = V9X_I9XX_SCENE_ID_EDGE;
    out->fill_dword = V9X_I9XX_FILL_DWORD;
    out->triangle_count = 2ul;
    v9x_i9xx_scene_triangle(&out->triangles[0],
                            V9X_I9XX_SCENE2_LEFT, V9X_I9XX_SCENE2_TOP,
                            V9X_I9XX_SCENE2_RIGHT, V9X_I9XX_SCENE2_TOP,
                            V9X_I9XX_SCENE2_RIGHT, V9X_I9XX_SCENE2_BOTTOM,
                            V9X_I9XX_TRI_COLOR_BGRA);
    v9x_i9xx_scene_triangle(&out->triangles[1],
                            V9X_I9XX_SCENE2_LEFT, V9X_I9XX_SCENE2_TOP,
                            V9X_I9XX_SCENE2_RIGHT, V9X_I9XX_SCENE2_BOTTOM,
                            V9X_I9XX_SCENE2_LEFT, V9X_I9XX_SCENE2_BOTTOM,
                            0xfff86428ul);
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
