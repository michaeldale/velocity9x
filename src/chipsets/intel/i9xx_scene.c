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
 * The shared-edge geometry and its second colour lived here until
 * 2026-09-16, when three scenes measured the fill rule for a slope-1
 * diagonal and were retired.
 *
 * Its hard-won property - that a sample centre must lie exactly ON the
 * edge, or no inclusion rule can be observed at all - is recorded in
 * docs\issues\2026-09-15-intel-edge-fill-rule-unmeasured.md,
 * which is where a scene for another slope should start rather than here.
 */

/*
 * The DEPTH scenes' geometry and depths.
 *
 * Three triangles of one shape, offset: A and B overlap each other, and C
 * lies BEHIND both and reaches past them on the right. Every vertex is above
 * y = 256 because the depth buffer is 256 rows tall - shorter than the render
 * target, which is what the reserve had room for.
 *
 * The depths are bit patterns because the driver's integer converter cannot
 * express a fraction, and they are 0.75, 0.25 and 0.90 - chosen so the ONLY
 * property the experiment depends on is their ORDER. The audit records that
 * neither reference tree states how a post-transform Z maps to a 16-bit fixed
 * depth buffer, so an absolute expectation would be a guess; an ordering
 * survives any monotonic mapping.
 *
 * C is the whole experiment. Without a draw behind the others, "the depth
 * test works" is indistinguishable from "the later draw overwrote the
 * earlier", which is what an unconditional draw does anyway. And C reaches
 * past A and B on the right so that it must appear SOMEWHERE - a C that was
 * silently dropped would otherwise look exactly like a C correctly rejected.
 */
#define V9X_I9XX_DEPTH_TOP          ((v9x_u32)20ul)
#define V9X_I9XX_DEPTH_BOTTOM       ((v9x_u32)200ul)
/* 0.75f, 0.25f, 0.90f. */
#define V9X_I9XX_DEPTH_Z_MID        ((v9x_u32)0x3f400000ul)
#define V9X_I9XX_DEPTH_Z_NEAR       ((v9x_u32)0x3e800000ul)
#define V9X_I9XX_DEPTH_Z_FAR        ((v9x_u32)0x3f666666ul)

/*
 * Ids are ASSIGNED, not derived from the table index.
 *
 * A scene's id is how its probe set is attributed in the capture and in every
 * decision record that cites one. Deriving it from position would silently
 * renumber past evidence the first time a scene is inserted or reordered.
 */
#define V9X_I9XX_SCENE_ID_PHASE5    ((v9x_u32)0ul)
#define V9X_I9XX_SCENE_ID_COLOR     ((v9x_u32)1ul)
/*
 * 2, 3 and 4 were the shared-edge scenes, answered and retired. Not
 * reused: a capture citing scene 2 means the edge experiment, and a new
 * scene wearing that number would make the two indistinguishable in every
 * record that mentions it.
 */
#define V9X_I9XX_SCENE_ID_TEXTURE   ((v9x_u32)5ul)
#define V9X_I9XX_SCENE_ID_MODULATE  ((v9x_u32)6ul)
#define V9X_I9XX_SCENE_ID_DEPTHTEST ((v9x_u32)7ul)
#define V9X_I9XX_SCENE_ID_DEPTHWRIT ((v9x_u32)8ul)

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
/*
 * FIVE scenes, which is also what the 2026-09-16 amendment authorises.
 *
 * The bound is reached rather than exceeded, and nothing was dropped to get
 * there: the colour and edge scenes retired when their questions were
 * answered, which is what made room. Scene 0 stays because it is the only
 * thing that says a change altered nothing, and scene 1 stays because it is
 * the only thing that says the TEXTURE path still works - it is byte for byte
 * what intel45 measured.
 *
 * A sixth would need a further risk decision. There is no sixth.
 */
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
    v9x_u32 x2, v9x_u32 y2, v9x_u32 color, v9x_u16 measured)
{
    out->color_measured = measured;
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
    out->kind = V9X_I9XX_SCENE_PLAIN;
    for (index = 0ul; index < V9X_I9XX_SCENE_MAX_TRIANGLES; ++index) {
        v9x_i9xx_scene_triangle(&out->triangles[index],
                                0ul, 0ul, 0ul, 0ul, 0ul, 0ul, 0ul, V9X_FALSE);
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
 * Where the triangle's texture coordinates put each probe.
 *
 * The coordinates run 0..1 across the triangle's own bounding box, so a
 * probe's quadrant follows from where it sits in that box rather than
 * from where it sits in the triangle. These four land one per quadrant,
 * and the host test recomputes that from the coordinates rather than
 * trusting the list.
 */
/*
 * The MODULATED probes: the same six pixels as the texture scene, so the two
 * scenes are comparable pixel by pixel and the multiply is the only
 * difference between them.
 */
static void v9x_i9xx_scene_modulate_probes(struct v9x_i9xx_scene *scene)
{
    v9x_i9xx_scene_probe(scene, "ModQ0", 250u, 160u,
                         V9X_I9XX_PROBE_MODQUAD0);
    v9x_i9xx_scene_probe(scene, "ModQ1", 400u, 160u,
                         V9X_I9XX_PROBE_MODQUAD1);
    v9x_i9xx_scene_probe(scene, "ModQ2", 290u, 330u,
                         V9X_I9XX_PROBE_MODQUAD2);
    v9x_i9xx_scene_probe(scene, "ModQ3", 350u, 330u,
                         V9X_I9XX_PROBE_MODQUAD3);
    /* Outside the triangle: the fill, unmodulated, because nothing drew. */
    v9x_i9xx_scene_probe(scene, "ModOutside", 40u, 400u,
                         V9X_I9XX_PROBE_FILL);
    v9x_i9xx_scene_probe(scene, "ModCorner", 0u, 0u,
                         V9X_I9XX_PROBE_FILL);
}

static void v9x_i9xx_scene_texture_probes(struct v9x_i9xx_scene *scene)
{
    v9x_i9xx_scene_probe(scene, "TexQ0", 250u, 160u,
                         V9X_I9XX_PROBE_QUADRANT0);
    v9x_i9xx_scene_probe(scene, "TexQ1", 400u, 160u,
                         V9X_I9XX_PROBE_QUADRANT1);
    v9x_i9xx_scene_probe(scene, "TexQ2", 290u, 330u,
                         V9X_I9XX_PROBE_QUADRANT2);
    v9x_i9xx_scene_probe(scene, "TexQ3", 350u, 330u,
                         V9X_I9XX_PROBE_QUADRANT3);
    /*
     * And the fill, outside the triangle entirely. A scene that sampled
     * everywhere - or that drew nothing at all - would otherwise look the
     * same at four interior probes as one that worked.
     */
    v9x_i9xx_scene_probe(scene, "TexOutside", 40u, 400u,
                         V9X_I9XX_PROBE_FILL);
    v9x_i9xx_scene_probe(scene, "TexCorner", 0u, 0u,
                         V9X_I9XX_PROBE_FILL);
}

/*
 * What a kind implies. Derived HERE and nowhere else.
 *
 * Five call sites read these - the extent, the primitive offset, the stream
 * builder, the driver and the decoder - and each one asking "is this kind
 * textured?" for itself is how two places come to disagree about one scene.
 */
v9x_u16 v9x_i9xx_scene_kind_textured(v9x_u32 kind)
{
    if (kind == V9X_I9XX_SCENE_TEXTURED ||
        kind == V9X_I9XX_SCENE_MODULATED) {
        return V9X_TRUE;
    }
    return V9X_FALSE;
}

v9x_u16 v9x_i9xx_scene_kind_depth(v9x_u32 kind)
{
    if (kind == V9X_I9XX_SCENE_DEPTH_TEST ||
        kind == V9X_I9XX_SCENE_DEPTH_WRITE) {
        return V9X_TRUE;
    }
    return V9X_FALSE;
}

v9x_u16 v9x_i9xx_scene_kind_depth_writes(v9x_u32 kind)
{
    if (kind == V9X_I9XX_SCENE_DEPTH_WRITE) {
        return V9X_TRUE;
    }
    return V9X_FALSE;
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

/*
 * Forward declarations for the scenes that live below the table function.
 *
 * The table reads top to bottom in scene order, which is how it is reviewed;
 * the helpers sit with the extent arithmetic they belong to.
 */
static v9x_status v9x_i9xx_scene_at_rest(v9x_u32 index,
                                         struct v9x_i9xx_scene *out);
static void v9x_i9xx_scene_depth_triangles(struct v9x_i9xx_scene *scene);
static void v9x_i9xx_scene_depth_probes(struct v9x_i9xx_scene *scene,
                                        v9x_u16 writes);

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
                                V9X_I9XX_TRI_COLOR_BGRA,
                                V9X_TRUE);
        v9x_i9xx_scene_phase5_probes(out);
        return V9X_STATUS_OK;
    }

    /*
     * Scene 1: the TEXTURE.
     *
     * Same triangle as scene 0, so the geometry contributes nothing new
     * and anything that differs is the texture path.
     *
     * The per-vertex colour is white and unread; see
     * V9X_I9XX_TEX_VERTEX_COLOR_BGRA for why that particular unread value. It
     * is marked UNMEASURED because it is: nothing has stored white on this
     * part. No probe expects it, so the capture never compares against it -
     * but a record claiming a measurement nobody took is how a prediction
     * becomes evidence.
     */
    if (index != 1ul) {
        /* Split because five scenes in one function ran past what Watcom will
         * generate for a 16-bit near call frame, and because the texture and
         * depth halves have nothing to say to each other. */
        return v9x_i9xx_scene_at_rest(index, out);
    }

    out->id = V9X_I9XX_SCENE_ID_TEXTURE;
    out->kind = V9X_I9XX_SCENE_TEXTURED;
    out->triangle_count = 1ul;
    v9x_i9xx_scene_triangle(&out->triangles[0],
                            (v9x_u32)V9X_I9XX_TRI_X0,
                            (v9x_u32)V9X_I9XX_TRI_Y0,
                            (v9x_u32)V9X_I9XX_TRI_X1,
                            (v9x_u32)V9X_I9XX_TRI_Y1,
                            (v9x_u32)V9X_I9XX_TRI_X2,
                            (v9x_u32)V9X_I9XX_TRI_Y2,
                            V9X_I9XX_TEX_VERTEX_COLOR_BGRA, V9X_FALSE);
    v9x_i9xx_scene_texture_probes(out);
    return V9X_STATUS_OK;
}

static v9x_status v9x_i9xx_scene_at_rest(v9x_u32 index, struct v9x_i9xx_scene *out)
{
    /*
     * Scene 2: the same texture, MODULATED by the vertex colour.
     *
     * Same triangle and same probes as scene 1, so the only difference between
     * the two streams is the fragment program and the vertex colour - and the
     * only difference between their pictures is the multiply. Scene 1 having
     * already reproduced intel45 byte for byte is what makes that isolation
     * real rather than asserted.
     */
    if (index == 2ul) {
        out->id = V9X_I9XX_SCENE_ID_MODULATE;
        out->kind = V9X_I9XX_SCENE_MODULATED;
        out->triangle_count = 1ul;
        v9x_i9xx_scene_triangle(&out->triangles[0],
                                (v9x_u32)V9X_I9XX_TRI_X0,
                                (v9x_u32)V9X_I9XX_TRI_Y0,
                                (v9x_u32)V9X_I9XX_TRI_X1,
                                (v9x_u32)V9X_I9XX_TRI_Y1,
                                (v9x_u32)V9X_I9XX_TRI_X2,
                                (v9x_u32)V9X_I9XX_TRI_Y2,
                                V9X_I9XX_TEX_MODULATE_COLOR_BGRA, V9X_FALSE);
        v9x_i9xx_scene_modulate_probes(out);
        return V9X_STATUS_OK;
    }

    /*
     * Scene 3: a depth buffer bound and TESTED, with writes off.
     *
     * The buffer is cleared far, and nothing writes it, so every triangle
     * passes and paint order decides - the picture is what the same three
     * triangles would draw with no depth at all. That is the point: it says a
     * real depth binding is in the stream and the hardware read through it
     * without incident, without yet depending on the test discriminating.
     *
     * The binding's encoding is Mesa-sourced only, so this is the scene where
     * a wrong one shows up cheaply.
     */
    if (index == 3ul) {
        out->id = V9X_I9XX_SCENE_ID_DEPTHTEST;
        out->kind = V9X_I9XX_SCENE_DEPTH_TEST;
        v9x_i9xx_scene_depth_triangles(out);
        v9x_i9xx_scene_depth_probes(out, V9X_FALSE);
        return V9X_STATUS_OK;
    }

    /*
     * Scene 4: the same three triangles, with depth WRITES on.
     *
     * A and B record their depths, so C - furthest, drawn last - is rejected
     * everywhere they drew and appears only past them. Two probes change their
     * answer between this scene and the last, and nothing else about the two
     * streams differs except S6's write-enable bit.
     */
    out->id = V9X_I9XX_SCENE_ID_DEPTHWRIT;
    out->kind = V9X_I9XX_SCENE_DEPTH_WRITE;
    v9x_i9xx_scene_depth_triangles(out);
    v9x_i9xx_scene_depth_probes(out, V9X_TRUE);
    return V9X_STATUS_OK;
}

/*
 * The three triangles the depth scenes draw, in submission order.
 *
 * One shape at three horizontal offsets. A and B overlap; C sits behind both
 * and extends past them to the right.
 */
static void v9x_i9xx_scene_depth_triangles(struct v9x_i9xx_scene *scene)
{
    scene->triangle_count = 3ul;
    /* A: drawn first, middle depth. */
    v9x_i9xx_scene_triangle(&scene->triangles[0],
                            60ul, V9X_I9XX_DEPTH_TOP,
                            260ul, V9X_I9XX_DEPTH_TOP,
                            160ul, V9X_I9XX_DEPTH_BOTTOM,
                            V9X_I9XX_TRI_COLOR_BGRA, V9X_TRUE);
    /* B: nearest, and it overlaps A. */
    v9x_i9xx_scene_triangle(&scene->triangles[1],
                            120ul, V9X_I9XX_DEPTH_TOP,
                            320ul, V9X_I9XX_DEPTH_TOP,
                            220ul, V9X_I9XX_DEPTH_BOTTOM,
                            V9X_I9XX_TRI_COLOR_B, V9X_TRUE);
    /* C: furthest, drawn LAST, overlapping both and reaching past them. */
    v9x_i9xx_scene_triangle(&scene->triangles[2],
                            200ul, V9X_I9XX_DEPTH_TOP,
                            460ul, V9X_I9XX_DEPTH_TOP,
                            330ul, V9X_I9XX_DEPTH_BOTTOM,
                            V9X_I9XX_TRI_COLOR_C, V9X_TRUE);
}

/*
 * The depth probes. Coordinates are shared by both depth scenes so the two
 * can be compared pixel by pixel; only the expectations differ, and that
 * difference is the measurement.
 *
 * `writes` selects which: with depth writes off, nothing the triangles draw
 * changes the buffer, so all three pass the test against a far-cleared buffer
 * and paint order decides - C last, C wins everywhere it covers. With writes
 * on, A and B record their depths and C is rejected wherever they drew.
 *
 * That contrast IS the experiment. The same three triangles at the same three
 * depths produce two different pictures, and only a working depth test
 * produces the second.
 */
static void v9x_i9xx_scene_depth_probes(struct v9x_i9xx_scene *scene,
                                        v9x_u16 writes)
{
    /*
     * Every coordinate below is a claim about which triangles cover a pixel,
     * and the host test recomputes all of them from the geometry rather than
     * trusting this list. It has already caught one: an "A and C but not B"
     * probe was specified here and cannot exist. B is A shifted right by 60
     * and C starts further right again, so wherever A and C overlap, B covers
     * it too - the pair is never alone. Three triangles in a row admit fewer
     * distinct regions than they appear to.
     *
     * At y = 60 the spans are A 82..237, B 142..297, C 229..430.
     */
    /* A alone. */
    v9x_i9xx_scene_probe(scene, "DepA", 100u, 60u,
                         V9X_I9XX_PROBE_TRIANGLE0);
    /*
     * A and B. B is nearer AND drawn later, so it reads B under either rule -
     * the control. A scene that got the answer right here and nowhere else is
     * visibly not testing depth, it is painting in order.
     */
    v9x_i9xx_scene_probe(scene, "DepAB", 200u, 60u,
                         V9X_I9XX_PROBE_TRIANGLE1);
    /*
     * All three, in the eight-pixel band where A has not yet ended and C has
     * already begun. C is furthest and drawn last: with writes it is rejected
     * and B stands, without them it wins.
     */
    v9x_i9xx_scene_probe(scene, "DepABC", 233u, 60u,
                         (v9x_u16)(writes != V9X_FALSE
                                       ? V9X_I9XX_PROBE_TRIANGLE1
                                       : V9X_I9XX_PROBE_TRIANGLE2));
    /* B and C, past A's right edge. The same contrast on a different pair, so
     * one wrong edge cannot account for both. */
    v9x_i9xx_scene_probe(scene, "DepBC", 260u, 60u,
                         (v9x_u16)(writes != V9X_FALSE
                                       ? V9X_I9XX_PROBE_TRIANGLE1
                                       : V9X_I9XX_PROBE_TRIANGLE2));
    /* C alone, past A and B entirely. C must appear here in BOTH scenes: it
     * is what distinguishes "C was rejected" from "C never ran". */
    v9x_i9xx_scene_probe(scene, "DepC", 400u, 60u,
                         V9X_I9XX_PROBE_TRIANGLE2);
    /* And the fill, below every apex. */
    v9x_i9xx_scene_probe(scene, "DepOut", 560u, 220u,
                         V9X_I9XX_PROBE_FILL);
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
    {
        v9x_u32 prefix;
        v9x_u32 total;

        if (v9x_i9xx_scene_kind_textured(scene->kind) != V9X_FALSE) {
            /* The paint comes first, then the same shape with the textured
             * state block and whichever program this kind runs. */
            prefix = v9x_i9xx_texture_paint_extent() +
                     V9X_I9XX_SCENE_FILL_DWORDS +
                     v9x_i9xx_textured_state_extent() +
                     ((scene->kind == V9X_I9XX_SCENE_MODULATED)
                          ? v9x_i9xx_modulate_program_extent()
                          : v9x_i9xx_sampling_program_extent()) +
                     V9X_I9XX_SCENE_PROBE_DWORDS;
        } else if (v9x_i9xx_scene_kind_depth(scene->kind) != V9X_FALSE) {
            /* The depth clear before the fill, for the same reason the texture
             * paint comes before it: both end in a flush, and both must be in
             * memory before anything reads them. */
            prefix = v9x_i9xx_depth_clear_extent() +
                     V9X_I9XX_SCENE_FILL_DWORDS +
                     v9x_i9xx_depth_state_extent() +
                     v9x_i9xx_fragment_program_extent() +
                     V9X_I9XX_SCENE_PROBE_DWORDS;
        } else {
            prefix = V9X_I9XX_SCENE_FILL_DWORDS +
                     v9x_i9xx_3d_state_extent() +
                     v9x_i9xx_fragment_program_extent() +
                     V9X_I9XX_SCENE_PROBE_DWORDS;
        }

        /* Both pads, computed the same way the builder applies them. The
         * primitive boundary and the draw boundary must each be qword
         * aligned; see the builder for the measurement. */
        prefix += (prefix & 1ul);
        total = prefix +
                ((v9x_i9xx_scene_kind_textured(scene->kind) != V9X_FALSE)
                     ? v9x_i9xx_textured_run_dwords(scene->triangle_count)
                     : v9x_i9xx_triangle_run_dwords(scene->triangle_count));
        total += (total & 1ul);
        return total;
    }
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
    run = (v9x_i9xx_scene_kind_textured(scene->kind) != V9X_FALSE)
              ? v9x_i9xx_textured_run_dwords(scene->triangle_count)
              : v9x_i9xx_triangle_run_dwords(scene->triangle_count);
    if (run == 0ul || run >= extent) {
        return 0ul;
    }
    /*
     * Subtracting the run from the extent no longer gives the boundary: the
     * extent may carry a TRAILING pad after the run. Take the run and any
     * trailing pad off together.
     */
    {
        v9x_u32 boundary = extent - run;

        boundary -= (boundary & 1ul);
        return boundary;
    }
}

v9x_status v9x_i9xx_build_scene_stream(
    const struct v9x_i9xx_scene *scene,
    v9x_u32 *stream, v9x_u32 capacity, v9x_u32 *written)
{
    struct v9x_i9xx_sandbox_layout layout;
    /*
     * ONE description of the texture, for the paint and the state block both.
     * Two would be two places for the geometry to drift, and a paint that
     * disagreed with the sampler would draw a plausible wrong picture instead
     * of failing.
     */
    struct v9x_i9xx_texture texture;
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
     * The TEXTURE first, painted by the GPU, before the target is even
     * filled.
     *
     * Order matters: the paint ends with an MI_FLUSH, so the texels are out of
     * the render cache before anything samples them. Painting it after the
     * state block would leave the sampler reading a texture the blits had not
     * reached.
     */
    texture.offset = layout.texture_offset;
    texture.width = V9X_I9XX_TEXTURE_WIDTH;
    texture.height = V9X_I9XX_TEXTURE_HEIGHT;
    texture.pitch = layout.texture_pitch;

    if (v9x_i9xx_scene_kind_textured(scene->kind) != V9X_FALSE) {
        if (v9x_i9xx_build_texture_paint(&texture, stream + at,
                                         capacity - at, &produced) !=
                V9X_STATUS_OK) {
            return V9X_STATUS_INSUFFICIENT_MEMORY;
        }
        at += produced;
    }

    /*
     * The depth CLEAR, likewise before anything reads depth. Far everywhere,
     * so the first primitive at any depth passes a LESS test and the scene's
     * result depends on what the triangles do rather than on what the page
     * happened to hold.
     */
    if (v9x_i9xx_scene_kind_depth(scene->kind) != V9X_FALSE) {
        if (v9x_i9xx_build_depth_clear(
                layout.depth_offset, layout.depth_pitch, layout.depth_bytes,
                stream + at, capacity - at, &produced) != V9X_STATUS_OK) {
            return V9X_STATUS_INSUFFICIENT_MEMORY;
        }
        at += produced;
    }

    /*
     * The fill, and the GPU performs it. The CPU bulk-filling 600 KiB
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

    if (v9x_i9xx_scene_kind_depth(scene->kind) != V9X_FALSE) {
        if (v9x_i9xx_build_depth_state(
                layout.target_offset, layout.target_pitch,
                V9X_I9XX_TARGET_WIDTH, V9X_I9XX_TARGET_HEIGHT,
                layout.depth_offset, layout.depth_pitch,
                (v9x_u32)((v9x_i9xx_scene_kind_depth_writes(scene->kind) !=
                           V9X_FALSE) ? 1ul : 0ul),
                stream + at, capacity - at, &produced) != V9X_STATUS_OK) {
            return V9X_STATUS_INSUFFICIENT_MEMORY;
        }
        at += produced;
        /* The untextured program: a depth scene draws flat colours, and
         * keeping the texture out of it is what makes a depth result a depth
         * result. */
        if (v9x_i9xx_build_fragment_program(
                stream + at, capacity - at, &produced) != V9X_STATUS_OK) {
            return V9X_STATUS_INSUFFICIENT_MEMORY;
        }
        at += produced;
    } else if (v9x_i9xx_scene_kind_textured(scene->kind) != V9X_FALSE) {
        if (v9x_i9xx_build_textured_state(
                layout.target_offset, layout.target_pitch,
                V9X_I9XX_TARGET_WIDTH, V9X_I9XX_TARGET_HEIGHT, &texture,
                stream + at, capacity - at, &produced) != V9X_STATUS_OK) {
            return V9X_STATUS_INSUFFICIENT_MEMORY;
        }
        at += produced;
        /*
         * The program this kind runs. SAMPLING loads the texel straight to the
         * output colour; MODULATED loads it into a temporary and multiplies by
         * the interpolated vertex colour. The untextured program moves the
         * vertex colour alone, and a textured scene running it would draw a
         * flat triangle that looked like a plausible failure.
         */
        if (((scene->kind == V9X_I9XX_SCENE_MODULATED)
                 ? v9x_i9xx_build_modulate_program(
                       stream + at, capacity - at, &produced)
                 : v9x_i9xx_build_sampling_program(
                       stream + at, capacity - at, &produced)) !=
                V9X_STATUS_OK) {
            return V9X_STATUS_INSUFFICIENT_MEMORY;
        }
        at += produced;
    } else {
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
    }

    if (capacity - at < V9X_I9XX_SCENE_PROBE_DWORDS) {
        return V9X_STATUS_INSUFFICIENT_MEMORY;
    }
    if (v9x_i9xx_build_mi_probe(stream + at, capacity - at, &produced) !=
            V9X_STATUS_OK) {
        return V9X_STATUS_INSUFFICIENT_MEMORY;
    }
    at += produced;

    /*
     * Pad so the PRIMITIVE begins on a qword boundary.
     *
     * The executor stops between the prefix and the primitive, and RING_TAIL
     * holds a qword-aligned offset: bit 2 is not writable. Measured on the
     * part 2026-09-16 - the mini-VDD wrote 0x10BC and read back 0x10B8, the
     * tail compare failed and the run was poisoned.
     *
     * An odd dword count is never a qword-aligned byte offset. The old
     * boundaries were even and therefore aligned BY ACCIDENT; removing the
     * depth BUF_INFO made them odd and broke the Phase 5 path as well as
     * this one.
     *
     * docs\decisions\2026-09-16-intel-ring-tail-requires-qword-alignment.md
     */
    if ((at & 1ul) != 0ul) {
        if (capacity - at < 1ul) {
            return V9X_STATUS_INSUFFICIENT_MEMORY;
        }
        stream[at++] = V9X_I9XX_MI_NOOP;
    }

    if (v9x_i9xx_scene_kind_depth(scene->kind) != V9X_FALSE) {
        /*
         * The three depths, in submission order. Bit patterns because the
         * converter takes integers; ordered rather than absolute because the
         * mapping from Z to the depth format is not stated by either reference
         * tree and an absolute expectation would be a guess.
         */
        static const v9x_u32 z_bits[3] = {
            V9X_I9XX_DEPTH_Z_MID,
            V9X_I9XX_DEPTH_Z_NEAR,
            V9X_I9XX_DEPTH_Z_FAR
        };

        if (v9x_i9xx_build_depth_run(
                scene->triangles, scene->triangle_count, z_bits,
                V9X_I9XX_TARGET_WIDTH, V9X_I9XX_TARGET_HEIGHT,
                stream + at, capacity - at, &produced) != V9X_STATUS_OK) {
            return V9X_STATUS_INSUFFICIENT_MEMORY;
        }
    } else if (v9x_i9xx_scene_kind_textured(scene->kind) != V9X_FALSE) {
        /*
         * Coordinates spanning the triangle's own bounding box, 0..1 in each
         * axis, so every quadrant of the texture lands somewhere inside the
         * triangle. Vertex 0 is the top-left corner of that box, vertex 1 the
         * top-right, vertex 2 the bottom-middle - which is the triangle this
         * scene draws, and the coordinates follow it rather than being chosen.
         *
         * Bit patterns, because the converter this driver uses takes integers
         * and cannot express a half. Only 0, 0.5 and 1 are needed and all
         * three are written out.
         */
        static const v9x_u32 u_bits[3] = {
            0x00000000ul, 0x3f800000ul, 0x3f000000ul
        };
        static const v9x_u32 v_bits[3] = {
            0x00000000ul, 0x00000000ul, 0x3f800000ul
        };

        if (v9x_i9xx_build_textured_run(
                scene->triangles, scene->triangle_count, u_bits, v_bits,
                V9X_I9XX_TARGET_WIDTH, V9X_I9XX_TARGET_HEIGHT,
                stream + at, capacity - at, &produced) != V9X_STATUS_OK) {
            return V9X_STATUS_INSUFFICIENT_MEMORY;
        }
    } else if (v9x_i9xx_build_triangle_run(
            scene->triangles, scene->triangle_count,
            V9X_I9XX_TARGET_WIDTH, V9X_I9XX_TARGET_HEIGHT,
            stream + at, capacity - at, &produced) != V9X_STATUS_OK) {
        return V9X_STATUS_INSUFFICIENT_MEMORY;
    }
    at += produced;

    /* And the draw boundary, for the same reason. A triangle run is
     * 1 + 15n dwords, so an even prefix plus two triangles lands odd. */
    if ((at & 1ul) != 0ul) {
        if (capacity - at < 1ul) {
            return V9X_STATUS_INSUFFICIENT_MEMORY;
        }
        stream[at++] = V9X_I9XX_MI_NOOP;
    }

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
