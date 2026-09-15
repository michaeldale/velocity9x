/*
 * Intel Gen3 3D command constants, for Phase 5's single triangle.
 *
 * Every value here is licensed by a section of
 * docs\decisions\2026-09-14-intel-gen3-3d-packet-audit.md, cited per constant.
 * That audit's rule is that a value may enter a builder only if two code paths
 * with different authors and purposes use it; where that could not be met the
 * audit says so explicitly and records the judgement instead. Nothing here is
 * guessed, and nothing here has been confirmed on this machine - the GMA 950
 * has executed one blitter command in its life and nothing 3D.
 *
 * Separate from intel_gma.h deliberately: that header is already three hundred
 * lines of Phase 1-4 material, and the 3D command set is a different subject
 * with a different source.
 */
#ifndef VELOCITY9X_INTEL_GEN3_3D_H
#define VELOCITY9X_INTEL_GEN3_3D_H

#include "velocity9x/backend.h"
#include "velocity9x/intel16.h"

/* ------------------------------------------------------------------ */
/* Command opcodes. Audit section 4, 5, 6, 7.                          */
/* ------------------------------------------------------------------ */

/* The 3D client's top three bits. Audit section 2 (both trees, CMD_3D). */
#define V9X_I9XX_CMD_3D                  ((v9x_u32)0x60000000ul)

/* Target description, three dwords. Audit section 4. */
#define V9X_I9XX_3DSTATE_BUF_INFO        ((v9x_u32)0x7d8e0001ul)
/* Colour back buffer id, BUF_INFO dword 1. Audit section 4. */
#define V9X_I9XX_BUF_3D_ID_COLOR_BACK    ((v9x_u32)0x03000000ul)
/* Depth buffer id, BUF_INFO dword 1. Audit section 8 (depth is declared,
 * never referenced), Mesa-sourced and recorded as such. */
#define V9X_I9XX_BUF_3D_ID_DEPTH         ((v9x_u32)0x07000000ul)
/* Both must be CLEAR for a linear target. Audit section 4. */
#define V9X_I9XX_BUF_3D_USE_FENCE        ((v9x_u32)0x00800000ul)
#define V9X_I9XX_BUF_3D_TILED_SURFACE    ((v9x_u32)0x00400000ul)
/*
 * Pitch field: the byte pitch with its low two bits discarded. The audit
 * records the encoding as ((x)/4)<<2, which is the same thing said twice, and
 * is why the pitch must be a multiple of four. Audit section 4.
 */
#define V9X_I9XX_BUF_3D_PITCH_MASK       ((v9x_u32)0x0000fffcul)

/* Destination variables, two dwords. Audit section 4. */
#define V9X_I9XX_3DSTATE_DST_BUF_VARS    ((v9x_u32)0x7d850000ul)
/* RGB565 colour buffer format, dword 1 bits 8-11. Audit section 4, two
 * independent assignments. */
#define V9X_I9XX_COLR_BUF_RGB565         ((v9x_u32)0x00000200ul)
/* 16-bit fixed depth format. Its encoded value is zero, so this is documented
 * rather than emitted. Audit section 8. */
#define V9X_I9XX_DEPTH_FRMT_16_FIXED     ((v9x_u32)0x00000000ul)
/*
 * Half-pixel sampling bias, horizontal and vertical. 0x8 in a four-bit field
 * is one half, and it is what puts sampling at pixel centres - the same
 * convention src\common\d3d_raster.c uses, which is why a software reference
 * can agree at all. Double-sourced: both trees set exactly 0x8/0x8, and Mesa
 * comments each one ".5". Audit sections 4 and 8.
 */
#define V9X_I9XX_DSTORG_HORT_BIAS_HALF   ((v9x_u32)0x00800000ul)
#define V9X_I9XX_DSTORG_VERT_BIAS_HALF   ((v9x_u32)0x00080000ul)

/* Drawing rectangle, five dwords. Audit section 4. */
#define V9X_I9XX_3DSTATE_DRAW_RECT       ((v9x_u32)0x7d800003ul)

/* Scissor. The rect packet's length field is part of its opcode, so the two
 * zero dwords after it are structural, not conventional. Audit section 8. */
#define V9X_I9XX_3DSTATE_SCISSOR_ENABLE  ((v9x_u32)0x7c800000ul)
#define V9X_I9XX_DISABLE_SCISSOR_RECT    ((v9x_u32)0x00000002ul)
#define V9X_I9XX_3DSTATE_SCISSOR_RECT_0  ((v9x_u32)0x7d810001ul)

/* Indirect state, disabled with an empty enable mask and one zero dword. This
 * is what makes an inline pixel shader legal and was the plan's largest
 * structural unknown. Audit section 6. */
#define V9X_I9XX_3DSTATE_LOAD_INDIRECT   ((v9x_u32)0x7d070000ul)

/* Invariant-state packets both drivers emit. Audit section 8; the set is the
 * intersection of the two drivers' invariant blocks, which is the largest set
 * double-sourced as invariant state. */
#define V9X_I9XX_3DSTATE_AA              ((v9x_u32)0x66000000ul)
#define V9X_I9XX_AA_LINE_ECAAR_WIDTH_EN  ((v9x_u32)0x00010000ul)
#define V9X_I9XX_AA_LINE_ECAAR_WIDTH_1_0 ((v9x_u32)0x00004000ul)
#define V9X_I9XX_AA_LINE_REGION_WIDTH_EN ((v9x_u32)0x00000100ul)
#define V9X_I9XX_AA_LINE_REGION_WID_1_0  ((v9x_u32)0x00000040ul)
#define V9X_I9XX_3DSTATE_DFLT_DIFFUSE    ((v9x_u32)0x7d990000ul)
#define V9X_I9XX_3DSTATE_DFLT_SPEC       ((v9x_u32)0x7d9a0000ul)
#define V9X_I9XX_3DSTATE_DFLT_Z          ((v9x_u32)0x7d980000ul)
#define V9X_I9XX_3DSTATE_COORD_SET_BIND  ((v9x_u32)0x76000000ul)
/* Identity texture-coordinate bindings, CSB_TCB(0,0)..(7,7). Audit section 8. */
#define V9X_I9XX_CSB_TCB_IDENTITY        ((v9x_u32)0x00fac688ul)
#define V9X_I9XX_3DSTATE_DEPTH_SUBRECT_DISABLE ((v9x_u32)0x7c880002ul)

/* State-immediate loads. The trailing length field is (S dwords - 1), which
 * five independent use sites agree on. Audit sections 5 and 7. */
#define V9X_I9XX_3DSTATE_LOAD_STATE_IMM1 ((v9x_u32)0x7d040000ul)
#define V9X_I9XX_I1_LOAD_S2              ((v9x_u32)0x00000040ul)
#define V9X_I9XX_I1_LOAD_S3              ((v9x_u32)0x00000080ul)
#define V9X_I9XX_I1_LOAD_S4              ((v9x_u32)0x00000100ul)
#define V9X_I9XX_I1_LOAD_S5              ((v9x_u32)0x00000200ul)
#define V9X_I9XX_I1_LOAD_S6              ((v9x_u32)0x00000400ul)

/*
 * S2: texture coordinate formats. Every unit set to NOT_PRESENT, which is the
 * all-ones encoding, because Phase 5 is untextured. Audit section 8.
 */
#define V9X_I9XX_S2_ALL_TEXCOORD_ABSENT  ((v9x_u32)0xfffffffful)

/*
 * S4: vertex format, culling and shading.
 *
 * XYZW plus per-vertex colour. Audit section 7 records why this is Mesa's
 * format rather than xf86's XY, and section 8 records plainly that the
 * two-use-site rule cannot be met for this field at all - the trees genuinely
 * disagree - so it is a recorded judgement, overturnable by evidence.
 */
#define V9X_I9XX_S4_VFMT_XYZW            ((v9x_u32)0x00000080ul)
#define V9X_I9XX_S4_VFMT_COLOR           ((v9x_u32)0x00000400ul)
#define V9X_I9XX_S4_CULLMODE_NONE        ((v9x_u32)0x00002000ul)
#define V9X_I9XX_S4_LINE_WIDTH_ONE       ((v9x_u32)0x00100000ul)
#define V9X_I9XX_S4_POINT_WIDTH_ONE      ((v9x_u32)0x00800000ul)

/*
 * S5: no stencil, no dither, no logic op, and no write-disable. Every relevant
 * enable is a set bit and the write-disable bits at 28-31 disable when SET, so
 * zero is the correct value and this name says so deliberately.
 */
#define V9X_I9XX_S5_PHASE5               ((v9x_u32)0x00000000ul)

/*
 * S6: colour writes ENABLED, and nothing else.
 *
 * This was zero until 2026-09-15, on the reasoning that every relevant enable
 * in S5 and S6 is a set bit so zero disables everything unwanted. That is true
 * of alpha test, depth test, depth write and blend - and it is also true of
 * S6_COLOR_WRITE_ENABLE, which is the one enable Phase 5 needs. Zeroing S6
 * therefore turned off the only thing that makes a rasterised pixel reach the
 * render target.
 *
 * Measured consequence, builds 4628b66 and 9064942: the GPU accepted the
 * _3DPRIMITIVE, reported no error, and wrote nothing. All fourteen probes read
 * the fill colour. The fill landed because it is an XY_COLOR_BLT and S6 does
 * not gate the blitter.
 *
 * Mesa is unambiguous. i915_state_immediate.c, upload_S6:
 *
 *     unsigned LIS6 = 0;
 *     if (i915->framebuffer.cbufs[0].texture)
 *        LIS6 |= S6_COLOR_WRITE_ENABLE;
 *
 * It is set whenever a colour buffer exists, before any blend or depth
 * consideration.
 */
#define V9X_I9XX_S6_COLOR_WRITE_ENABLE   ((v9x_u32)0x00000004ul)
#define V9X_I9XX_S6_PHASE5               V9X_I9XX_S6_COLOR_WRITE_ENABLE
/* Named so a reader can see what is being left clear. Audit section 8. */
#define V9X_I9XX_S6_DEPTH_TEST_ENABLE    ((v9x_u32)0x00080000ul)
#define V9X_I9XX_S6_DEPTH_WRITE_ENABLE   ((v9x_u32)0x00000008ul)

/* ------------------------------------------------------------------ */
/* Fragment program. Audit section 7.                                  */
/* ------------------------------------------------------------------ */

#define V9X_I9XX_3DSTATE_PIXEL_SHADER    ((v9x_u32)0x7d050000ul)
/* Declaration and MOV opcodes, and the register-type encodings. Every
 * instruction is exactly three dwords, agreed by two independently written
 * emitters. Audit section 7. */
#define V9X_I9XX_FS_D0_DCL               ((v9x_u32)0x19000000ul)
#define V9X_I9XX_FS_A0_MOV               ((v9x_u32)0x02000000ul)
#define V9X_I9XX_FS_REG_TYPE_T           ((v9x_u32)1ul)
#define V9X_I9XX_FS_REG_TYPE_OC          ((v9x_u32)4ul)
#define V9X_I9XX_FS_T_DIFFUSE            ((v9x_u32)8ul)
#define V9X_I9XX_FS_TYPE_SHIFT           19u
#define V9X_I9XX_FS_NR_SHIFT             14u
#define V9X_I9XX_FS_CHANNEL_ALL          ((v9x_u32)0x00003c00ul)
#define V9X_I9XX_FS_A0_SRC0_TYPE_SHIFT   7u
#define V9X_I9XX_FS_A0_SRC0_NR_SHIFT     2u
/*
 * A1 source swizzle .xyzw: channel selectors 0,1,2,3 at shifts 28,24,20,16.
 * Audit section 7. The composed value is spelled out because a builder that
 * assembles it from four shifts is harder to review than the constant it
 * always produces.
 */
#define V9X_I9XX_FS_A1_SWIZZLE_XYZW      ((v9x_u32)0x01230000ul)

/* ------------------------------------------------------------------ */
/* Primitive dispatch. Audit section 7.                                */
/* ------------------------------------------------------------------ */

/* Inline form: bit 23 clear. The length field is (vertex dwords - 1), counting
 * dwords and excluding the command dword - two independently derived formulas
 * agree. Audit section 7. */
#define V9X_I9XX_3DPRIMITIVE_INLINE      ((v9x_u32)0x7f000000ul)
#define V9X_I9XX_PRIM3D_TRILIST          ((v9x_u32)0x00000000ul)

/* ------------------------------------------------------------------ */
/* Phase 5's own parameters.                                           */
/* ------------------------------------------------------------------ */

/*
 * Five dwords per vertex: x, y, z, w as IEEE-754 floats, then one packed
 * colour dword. Audit section 7 - the colour is NOT four floats.
 */
#define V9X_I9XX_VERTEX_DWORDS           ((v9x_u32)5ul)
#define V9X_I9XX_VERTEX_COUNT            ((v9x_u32)3ul)

/*
 * The triangle, in screen pixels. Deliberately asymmetric in both axes so a
 * transposed X/Y is visible in the artefact rather than plausible.
 */
#define V9X_I9XX_TRI_X0                  160
#define V9X_I9XX_TRI_Y0                  120
#define V9X_I9XX_TRI_X1                  480
#define V9X_I9XX_TRI_Y1                  120
#define V9X_I9XX_TRI_X2                  320
#define V9X_I9XX_TRI_Y2                  400

/*
 * The triangle's colour, packed BGRA: B=0x28, G=0x64, R=0xf8, A=0xff.
 *
 * Two constraints, both satisfied. No byte repeats, so a channel swap is
 * visible in the artefact. And it is exactly representable in RGB565 - red and
 * blue have their low three bits clear and green its low two - because
 * dithering is on by default on this hardware and cannot be cleanly disabled
 * (audit section 8). A colour the hardware must dither would disagree with the
 * software reference across the triangle's interior, not merely at the edges
 * the plan licenses.
 */
/*
 * Changed 2026-09-15, from 0xfff86428, to measure the hardware's 8-bit to
 * 5/6-bit conversion. R=21, G=135, B=249: no byte repeats, and between them the
 * three channels separate round(v*max/255) from trunc, round8, floor and ceil.
 * The prediction table and what the result licenses are in
 * plans\intel-phase5-colour-conversion-experiment.md.
 *
 * It drops the previous colour's 565-exactness, which was chosen because the
 * audit recorded dithering as on by default and not cleanly disableable. Two
 * observations sit against that premise: S5 leaves S5_COLOR_DITHER_ENABLE
 * clear, and on 6c81c52 all seven interior probes read an identical F325.
 *
 * The second is weak and should not be leaned on. An ordered dither has a
 * period, so seven scattered points can all land on cells carrying the same
 * value - agreement does not exclude dithering. Nor would disagreement
 * uniquely establish it: interpolation or two genuinely different regions
 * would look the same. The probes are worth reading and cannot settle it.
 */
#define V9X_I9XX_TRI_COLOR_BGRA          ((v9x_u32)0xff1587f9ul)
/*
 * MEASURED on the 945GSE, 2026-09-15, build 83f24ec: all seven interior probes
 * read 0x1c3e. round(21*31/255)=3, round(135*63/255)=33, round(249*31/255)=30.
 *
 * This colour was chosen to separate the candidates, and it did. trunc would
 * have given 0x143f, floor 0x143e, round8 and ceil 0x1c5f; none was observed.
 *
 * It does NOT establish the rule for every channel. Green agreed with
 * truncation at both tested values, so a backend that rounds red and blue and
 * truncates green fits everything measured. See the decision record.
 *
 * Seven identical reads also make dithering unlikely here, but do not exclude
 * it: an ordered dither has a period and scattered points can share a cell.
 * What this licenses is a claim about the conversion, not about the dither.
 */
#define V9X_I9XX_TRI_COLOR_RGB565        ((v9x_u32)0x00001c3eul)

/* The background the target is filled with before the draw. Distinct from the
 * triangle colour in every byte, and likewise 565-exact. */
#define V9X_I9XX_FILL_RGB565             ((v9x_u32)0x00000842ul)
/*
 * The same word duplicated into a dword, because the GPU fills the target
 * rather than the CPU.
 *
 * At 16 bpp a 640x480 target is exactly a 320x480 THIRTY-TWO-BIT surface at
 * the same 1280-byte pitch, so Phase 4's proven XY_COLOR_BLT fills it with no
 * new packet type and no unproven depth encoding. The span works out to
 * V9X_I9XX_TARGET_BYTES to the byte, so the builder's existing bounds check is
 * the bound.
 *
 * The CPU no longer writes the target at all - it only reads it back. That is
 * a condition of the errata gate opening, not an implementation preference:
 * 600 KiB of CPU writes immediately before the GPU reads adjacent memory was
 * the closest thing in this design to erratum 12's own description of its
 * trigger (docs\decisions6-09-15-intel-phase5-errata-gate.md).
 */
#define V9X_I9XX_FILL_DWORD              ((v9x_u32)0x08420842ul)
#define V9X_I9XX_FILL_BLT_WIDTH          ((v9x_u16)320u)
#define V9X_I9XX_FILL_BLT_HEIGHT         ((v9x_u16)480u)

/* ------------------------------------------------------------------ */
/* Decoder refusal reasons. Every one is a distinct number, and the      */
/* decoder also returns the dword index it rejected.                    */
/* ------------------------------------------------------------------ */

#define V9X_I9XX_P5_OK                    0u
#define V9X_I9XX_P5_TRUNCATED             1u
#define V9X_I9XX_P5_BAD_OPCODE            2u
#define V9X_I9XX_P5_BAD_LENGTH            3u
#define V9X_I9XX_P5_TARGET_RANGE          4u
#define V9X_I9XX_P5_PITCH                 5u
#define V9X_I9XX_P5_FORMAT                6u
#define V9X_I9XX_P5_DRAW_RECT             7u
#define V9X_I9XX_P5_SCISSOR_ENABLED       8u
#define V9X_I9XX_P5_INDIRECT_FORBIDDEN    9u
#define V9X_I9XX_P5_TEXTURE_FORBIDDEN    10u
#define V9X_I9XX_P5_DEPTH_FORBIDDEN      11u
#define V9X_I9XX_P5_TILED_FORBIDDEN      12u
#define V9X_I9XX_P5_VERTEX_FORMAT        13u
#define V9X_I9XX_P5_VERTEX_COUNT         14u
#define V9X_I9XX_P5_VERTEX_RANGE         15u
#define V9X_I9XX_P5_SHADER               16u
#define V9X_I9XX_P5_MISSING_PACKET       17u

/* Float transport refusal reasons, from i9xx_float.c. */
#define V9X_I9XX_FLOAT_OK                 0u
#define V9X_I9XX_FLOAT_NEGATIVE           1u
#define V9X_I9XX_FLOAT_TOO_LARGE          2u
#define V9X_I9XX_FLOAT_NOT_FINITE         3u
#define V9X_I9XX_FLOAT_DENORMAL           4u
#define V9X_I9XX_FLOAT_FRACTIONAL         5u
#define V9X_I9XX_FLOAT_NEGATIVE_ZERO      6u

/* ------------------------------------------------------------------ */
/* Leaf unit entry points.                                              */
/* ------------------------------------------------------------------ */

/* src\chipsets\intel\i9xx_float.c */
v9x_u16 v9x_i9xx_float_from_int(v9x_u32 value, v9x_u32 *bits);
v9x_u16 v9x_i9xx_float_to_int(v9x_u32 bits, v9x_u32 *value);

/*
 * src\chipsets\intel\i9xx_3d_stream.c - the 8-bit-to-565 conversion this
 * chip's colour backend was MEASURED to use, 2026-09-15.
 *
 * Intel-specific on purpose. The shared software rasteriser truncates, which
 * is a legitimate and common choice, and nothing here says it is wrong; the
 * two simply disagree by up to one level per channel. Changing the shared
 * rasteriser to match one chip would be a change to every chip's output made
 * on one chip's evidence, so the expectation lives on the Intel side and the
 * disagreement is reported rather than hidden.
 *
 * round(v * max / 255), evaluated as (v * max + 127) / 255 so it is integer
 * throughout. Clamping is structural rather than applied: v <= 255 gives a
 * result <= max for every max here, so no channel can overflow its field.
 *
 * docs\decisions6-09-15-intel-565-conversion-rounds.md carries the two
 * colours that separate this from truncation on all three channels.
 */
v9x_u16 v9x_i9xx_rgb565_round(v9x_u32 red, v9x_u32 green, v9x_u32 blue);

/* src\chipsets\intel\i9xx_3d.c */
/* Dwords before the 3D state block: the GPU fill plus its MI_FLUSH. */
v9x_u32 v9x_i9xx_phase5_fill_extent(void);
v9x_u32 v9x_i9xx_3d_state_extent(void);
v9x_status v9x_i9xx_build_3d_state(
    v9x_u32 target_offset, v9x_u32 target_pitch,
    v9x_u32 width, v9x_u32 height,
    v9x_u32 *stream, v9x_u32 capacity, v9x_u32 *written);

/* src\chipsets\intel\i9xx_fragprog.c - no arguments, because the program is a
 * constant, and it is a constant because Phase 6 forbids a shader compiler. */
v9x_u32 v9x_i9xx_fragment_program_extent(void);
v9x_status v9x_i9xx_build_fragment_program(
    v9x_u32 *stream, v9x_u32 capacity, v9x_u32 *written);

/* src\chipsets\intel\i9xx_vertex.c */
v9x_u32 v9x_i9xx_vertex_run_extent(void);
v9x_status v9x_i9xx_build_vertex_run(
    v9x_u32 width, v9x_u32 height,
    v9x_u32 *stream, v9x_u32 capacity, v9x_u32 *written);

/* ------------------------------------------------------------------ */
/* Phase 6 scenes. src\chipsets\intel\i9xx_scene.c                     */
/* ------------------------------------------------------------------ */

/*
 * A scene is one complete, independent draw: a fill, a full state block, the
 * fragment program, an MI probe and a run of triangles. Several scenes run on
 * one armed boot, one after another, each with its own probes written to disk
 * before the next submits anything.
 *
 * The rule that makes that safe is INDEPENDENCE, not brevity: a scene may not
 * depend on state another scene left behind. Every scene re-emits complete
 * state, which is what the Phase 5 constraint "complete state re-emitted every
 * draw" bought and what makes a failure attributable to one scene.
 * docs\plans\intel-phase6-bundled-scenes.md has the argument in full.
 *
 * Scenes REUSE the single render target rather than each owning one. That is
 * forced arithmetic, not a preference: the reserve is 0x100000 bytes, the
 * ring, status page, scratch page, target and upper guard already consume
 * 0xA9000, and a second 640x480x16 target needs 0x96000 against the 0x57000
 * that remains. The earlier plan asserted a per-scene allocator without doing
 * this subtraction.
 *
 * Reuse costs nothing that matters. A scene's pixels are destroyed by the next
 * scene's fill, but the probes are the evidence and they are already on disk -
 * the same flush-after-every-step property that makes a hang localisable.
 */
#define V9X_I9XX_SCENE_MAX_TRIANGLES     ((v9x_u32)2ul)
#define V9X_I9XX_SCENE_MAX_PROBES        ((v9x_u32)14ul)

/*
 * What a probe expects to find, or that it expects nothing.
 *
 * MEASURE is not "unknown because nobody worked it out". It marks a pixel
 * whose value IS the measurement - the shared-edge samples - and writing an
 * expectation there would be a guess recorded as evidence. The validator
 * reports those and fails on the others.
 */
#define V9X_I9XX_PROBE_FILL              ((v9x_u16)0u)
#define V9X_I9XX_PROBE_TRIANGLE0         ((v9x_u16)1u)
#define V9X_I9XX_PROBE_TRIANGLE1         ((v9x_u16)2u)
#define V9X_I9XX_PROBE_MEASURE           ((v9x_u16)0xffffu)

struct v9x_i9xx_probe {
    /* Published as the capture key, so it survives into every record that
     * cites the result. Static storage; never freed. */
    const char *name;
    v9x_u16 x;
    v9x_u16 y;
    v9x_u16 expect;
};

struct v9x_i9xx_triangle {
    /* Whole pixels, inclusive of the drawing rectangle's last addressable
     * pixel. The builder refuses anything outside it rather than letting the
     * hardware clip, because a clipped vertex is invisible in the capture. */
    v9x_u32 x[3];
    v9x_u32 y[3];
    /* BGRA, one colour for all three vertices - see the vertex builder for
     * why that keeps provoking-vertex rules off the critical path. */
    v9x_u32 color;
};

struct v9x_i9xx_scene {
    /* Stable across builds and published in the capture. A scene's number is
     * how a probe set is attributed, so reordering the table must not silently
     * renumber the evidence: ids are assigned, not derived from the index. */
    v9x_u32 id;
    /* The BLT fill pattern: two RGB565 pixels in one dword. */
    v9x_u32 fill_dword;
    v9x_u32 triangle_count;
    struct v9x_i9xx_triangle triangles[V9X_I9XX_SCENE_MAX_TRIANGLES];
    /*
     * The pixels this scene reads back, carried WITH the scene rather than
     * held in a parallel table. A probe set that can drift from the geometry
     * it describes is how a capture comes to report fourteen confident values
     * about the wrong triangle.
     */
    v9x_u32 probe_count;
    struct v9x_i9xx_probe probes[V9X_I9XX_SCENE_MAX_PROBES];
};

/*
 * Scenes this build executes, or ZERO if it defines more than the errata
 * decision authorises. Zero is a refusal, not an empty set: every caller
 * treats it as a build fault.
 */
v9x_u32 v9x_i9xx_scene_count(void);
/* The bound the 2026-09-16 amendment set. Five. */
v9x_u32 v9x_i9xx_scene_authorised_draws(void);
v9x_status v9x_i9xx_scene_at(v9x_u32 index, struct v9x_i9xx_scene *out);
v9x_u32 v9x_i9xx_scene_extent(const struct v9x_i9xx_scene *scene);
v9x_status v9x_i9xx_build_scene_stream(
    const struct v9x_i9xx_scene *scene,
    v9x_u32 *stream, v9x_u32 capacity, v9x_u32 *written);
/* Zero if the scene cannot be built, which no arm path accepts as a CRC. */
v9x_u32 v9x_i9xx_scene_crc(v9x_u32 index);
/* Over every scene's dwords in execution order, which is what the arm gate
 * compares - the same construction as the Phase 4/5 chain. */
v9x_u32 v9x_i9xx_scene_combined_crc(void);
/* Every scene's probes added up: the aperture-read budget for a whole boot. */
v9x_u32 v9x_i9xx_scene_total_probes(void);


/* src\chipsets\intel\i9xx_vertex.c */
/*
 * Dwords a run of `count` triangles occupies: the primitive command plus
 * count * 3 vertices of 5 dwords each. Zero if count is out of range.
 *
 * A function rather than an expression at each site, and it exists for a
 * LINK-TIME reason. count is a runtime value, so count * 15 in v9x_u32 is a
 * 32-bit multiply, which on 16-bit Watcom is a call to __U4M in the default
 * CODE segment - and this file is compiled into I9XXCODE, from which a near
 * call cannot reach it:
 *
 *     Error! E2052: ... relocation at 0003:21b9 not in the same segment
 *
 * which is what the first version produced, three times over. The same class
 * of fault as the 32-bit divide in the 565 conversion. Here the multiply is
 * done in 16 bits, where it is a single instruction: count is bounded by
 * V9X_I9XX_SCENE_MAX_TRIANGLES before it is used, so the product cannot leave
 * a 16-bit register.
 */
v9x_u32 v9x_i9xx_triangle_run_dwords(v9x_u32 count);
v9x_status v9x_i9xx_build_triangle_run(
    const struct v9x_i9xx_triangle *triangles, v9x_u32 count,
    v9x_u32 width, v9x_u32 height,
    v9x_u32 *stream, v9x_u32 capacity, v9x_u32 *written);

/* src\chipsets\intel\i9xx_3d_stream.c */
struct v9x_i9xx_phase5_parameters {
    v9x_u32 target_offset;
    v9x_u32 target_pitch;
    v9x_u32 target_bytes;
    v9x_u32 width;
    v9x_u32 height;
    v9x_u32 fill_word;
    v9x_u32 triangle_color;
    v9x_u32 stream_dwords;
};
void v9x_i9xx_phase5_parameters(struct v9x_i9xx_phase5_parameters *out);
v9x_status v9x_i9xx_build_phase5_stream(
    v9x_u32 *stream, v9x_u32 capacity, v9x_u32 *written);
v9x_u32 v9x_i9xx_phase5_execution_crc(void);

/* src\chipsets\intel\i9xx_3d_decode.c */
v9x_u16 v9x_i9xx_decode_phase5_stream(
    const v9x_u32 *stream, v9x_u32 dword_count,
    v9x_u32 target_offset, v9x_u32 target_bytes,
    v9x_u32 *rejected_index);

#endif /* VELOCITY9X_INTEL_GEN3_3D_H */
