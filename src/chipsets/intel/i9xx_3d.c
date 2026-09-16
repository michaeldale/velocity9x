/*
 * The Gen3 3D state block for Phase 5's single triangle.
 *
 * One reviewed order, built once, in the order the audit established
 * (docs\decisions\2026-09-14-intel-gen3-3d-packet-audit.md). The sub-emitters
 * are static because exporting eleven symbols to let a test address individual
 * packets would be a worse trade than exporting v9x_i9xx_3d_state_extent and
 * letting tests index the array.
 *
 * Every packet here is in the intersection of the two drivers' invariant
 * blocks, or is one of the four target-setup packets Phase 5 needs. Nothing
 * from xf86's larger block is emitted: those packets are double-sourced but
 * their fixed values are not, and they configure state Phase 5 does not use
 * (audit section 8).
 */
#include "velocity9x/intel_gen3_3d.h"

/*
 * Packet counts, kept next to the emitters that produce them so the extent and
 * the code cannot disagree silently.
 */
#define V9X_I9XX_3D_INVARIANT_DWORDS  15ul
/* Three fewer since the depth BUF_INFO left: colour BUF_INFO, DST_BUF_VARS
 * and DRAW_RECT. */
#define V9X_I9XX_3D_TARGET_DWORDS     10ul
#define V9X_I9XX_3D_PIPELINE_DWORDS    6ul

/*
 * The nine-packet, fifteen-dword intersection. Audit section 8: this is the
 * largest set both Mesa and xf86 emit as invariant state, and the sources
 * cannot say which of it a freshly-reset engine actually requires - neither
 * driver ever sees one. Emitting the intersection is the defensible floor.
 */
static v9x_u32 v9x_i9xx_emit_invariant(v9x_u32 *stream)
{
    v9x_u32 at = 0ul;

    stream[at++] = V9X_I9XX_3DSTATE_AA |
                   V9X_I9XX_AA_LINE_ECAAR_WIDTH_EN |
                   V9X_I9XX_AA_LINE_ECAAR_WIDTH_1_0 |
                   V9X_I9XX_AA_LINE_REGION_WIDTH_EN |
                   V9X_I9XX_AA_LINE_REGION_WID_1_0;

    /* All three defaults are set to zero, exactly as both drivers do. The
     * diffuse default is unused by Phase 5 - the colour is per-vertex - but it
     * is part of the intersection and costs two dwords. */
    stream[at++] = V9X_I9XX_3DSTATE_DFLT_DIFFUSE;
    stream[at++] = 0ul;
    stream[at++] = V9X_I9XX_3DSTATE_DFLT_SPEC;
    stream[at++] = 0ul;
    stream[at++] = V9X_I9XX_3DSTATE_DFLT_Z;
    stream[at++] = 0ul;

    stream[at++] = V9X_I9XX_3DSTATE_COORD_SET_BIND |
                   V9X_I9XX_CSB_TCB_IDENTITY;

    /* The two zero dwords are required by the rect packet's own length field,
     * not by convention. Audit section 8. */
    stream[at++] = V9X_I9XX_3DSTATE_SCISSOR_RECT_0;
    stream[at++] = 0ul;
    stream[at++] = 0ul;
    stream[at++] = V9X_I9XX_3DSTATE_SCISSOR_ENABLE |
                   V9X_I9XX_DISABLE_SCISSOR_RECT;

    stream[at++] = V9X_I9XX_3DSTATE_DEPTH_SUBRECT_DISABLE;

    /* An empty enable mask plus one zero dword disables indirect state, which
     * is what makes the inline pixel shader legal. Audit section 6. */
    stream[at++] = V9X_I9XX_3DSTATE_LOAD_INDIRECT;
    stream[at++] = 0ul;

    return at;
}

/*
 * The colour target and the drawing rectangle. No depth buffer is declared;
 * see below for why one used to be.
 */
static v9x_u32 v9x_i9xx_emit_target(v9x_u32 *stream, v9x_u32 target_offset,
                                     v9x_u32 target_pitch,
                                     v9x_u32 width, v9x_u32 height)
{
    v9x_u32 at = 0ul;

    /* Tiled and fence bits deliberately absent: the target is linear. */
    stream[at++] = V9X_I9XX_3DSTATE_BUF_INFO;
    stream[at++] = V9X_I9XX_BUF_3D_ID_COLOR_BACK |
                   (target_pitch & V9X_I9XX_BUF_3D_PITCH_MASK);
    stream[at++] = target_offset;

    /*
     * NO DEPTH BUF_INFO. Removed 2026-09-15.
     *
     * This emitted one, declaring a depth buffer at graphics address ZERO with
     * a dummy pitch, on the reading that Mesa declares depth for a GL context
     * without a depth attachment. It does not: Mesa emits a depth BUF_INFO
     * only when a depth buffer exists, and the packet audit already recorded
     * this binding as "declared, never referenced" - which should have
     * prompted removing it rather than keeping it (audit section 8).
     *
     * Address zero is inside the aperture and is not ours. Nothing read
     * through it, because the S6 depth enables are clear, and two armed boots
     * drew correctly with it present. NEITHER OF THOSE IS A JUSTIFICATION: a
     * draw succeeding with depth testing disabled says only that nothing
     * followed the pointer.
     *
     * Removed before Phase 6 rather than after, because the first step that
     * enables depth testing either uses this binding or replaces it, and a
     * wrong replacement would then be measured against a baseline that already
     * contained a bad one.
     *
     * docs\issues\2026-09-15-intel-depth-buf-info-at-address-zero.md
     */

    stream[at++] = V9X_I9XX_3DSTATE_DST_BUF_VARS;
    stream[at++] = V9X_I9XX_COLR_BUF_RGB565 |
                   V9X_I9XX_DEPTH_FRMT_16_FIXED |
                   V9X_I9XX_DSTORG_HORT_BIAS_HALF |
                   V9X_I9XX_DSTORG_VERT_BIAS_HALF;

    /* Inclusive: both bounds are one less than the extent. Audit section 4. */
    stream[at++] = V9X_I9XX_3DSTATE_DRAW_RECT;
    stream[at++] = 0ul;
    stream[at++] = 0ul;                                  /* ymin, xmin */
    stream[at++] = ((height - 1ul) << 16) | (width - 1ul);
    stream[at++] = 0ul;                                  /* yorig, xorig */

    return at;
}

/*
 * S2 through S6 in one load, which is the shape Mesa's steady state uses. The
 * trailing length field is (S dwords - 1); five independent use sites agree.
 */
static v9x_u32 v9x_i9xx_emit_pipeline(v9x_u32 *stream, v9x_u32 s2)
{
    v9x_u32 at = 0ul;

    stream[at++] = V9X_I9XX_3DSTATE_LOAD_STATE_IMM1 |
                   V9X_I9XX_I1_LOAD_S2 | V9X_I9XX_I1_LOAD_S3 |
                   V9X_I9XX_I1_LOAD_S4 | V9X_I9XX_I1_LOAD_S5 |
                   V9X_I9XX_I1_LOAD_S6 | 4ul;
    /*
     * S2, eight nibbles - one per coordinate unit. Untextured is all-ones,
     * every unit absent; textured clears unit 0's nibble to TEXCOORDFMT_2D.
     *
     * It is a PARAMETER rather than two copies of this emitter because S2 is
     * the only pipeline dword a texture changes. S4 carries no
     * texture-coordinate field at all, which the 2026-09-16 audit established
     * and which was the largest risk the plan had named.
     */
    stream[at++] = s2;
    /* S3: no texture coordinate wrapping. */
    stream[at++] = 0ul;
    /*
     * S4 must agree exactly with the vertex dwords i9xx_vertex.c emits: four
     * floats of position then one packed colour dword. A disagreement here is
     * the single most likely silent hang in the whole phase.
     */
    stream[at++] = V9X_I9XX_S4_POINT_WIDTH_ONE |
                   V9X_I9XX_S4_LINE_WIDTH_ONE |
                   V9X_I9XX_S4_CULLMODE_NONE |
                   V9X_I9XX_S4_VFMT_XYZW |
                   V9X_I9XX_S4_VFMT_COLOR;
    stream[at++] = V9X_I9XX_S5_PHASE5;
    stream[at++] = V9X_I9XX_S6_PHASE5;

    return at;
}

v9x_u32 v9x_i9xx_3d_state_extent(void)
{
    return V9X_I9XX_3D_INVARIANT_DWORDS + V9X_I9XX_3D_TARGET_DWORDS +
           V9X_I9XX_3D_PIPELINE_DWORDS;
}

/*
 * The textured block is the untextured one plus MAP_STATE and SAMPLER_STATE,
 * each five dwords for a single unit. Both are emitted DIRECTLY into the
 * stream; neither goes through LOAD_INDIRECT, which stays disabled exactly as
 * Phase 5 emits it. Audit section 2.
 */
v9x_u32 v9x_i9xx_textured_state_extent(void)
{
    return v9x_i9xx_3d_state_extent() +
           v9x_i9xx_map_state_extent(1ul) +
           v9x_i9xx_sampler_state_extent(1ul);
}

/*
 * One emission path, textured or not.
 *
 * `texture` null is the untextured block Phase 5 has always emitted. Non-null
 * adds the two texture packets and switches S2. A second copy of the invariant
 * and target emitters is what this avoids, and it is what the packet-offset
 * defect and the primitive-offset defect were both made of.
 */
static v9x_status v9x_i9xx_build_state_common(
    v9x_u32 target_offset, v9x_u32 target_pitch,
    v9x_u32 width, v9x_u32 height,
    const struct v9x_i9xx_texture *texture,
    v9x_u32 *stream, v9x_u32 capacity, v9x_u32 *written);

v9x_status v9x_i9xx_build_textured_state(
    v9x_u32 target_offset, v9x_u32 target_pitch,
    v9x_u32 width, v9x_u32 height,
    const struct v9x_i9xx_texture *texture,
    v9x_u32 *stream, v9x_u32 capacity, v9x_u32 *written)
{
    if (texture == 0) {
        /* A textured block with no texture is a mistake, not an untextured
         * block. The caller that wanted one should have said so. */
        if (written != 0) { *written = 0ul; }
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    return v9x_i9xx_build_state_common(target_offset, target_pitch,
                                       width, height, texture,
                                       stream, capacity, written);
}

v9x_status v9x_i9xx_build_3d_state(
    v9x_u32 target_offset, v9x_u32 target_pitch,
    v9x_u32 width, v9x_u32 height,
    v9x_u32 *stream, v9x_u32 capacity, v9x_u32 *written)
{
    return v9x_i9xx_build_state_common(target_offset, target_pitch,
                                       width, height, 0,
                                       stream, capacity, written);
}

static v9x_status v9x_i9xx_build_state_common(
    v9x_u32 target_offset, v9x_u32 target_pitch,
    v9x_u32 width, v9x_u32 height,
    const struct v9x_i9xx_texture *texture,
    v9x_u32 *stream, v9x_u32 capacity, v9x_u32 *written)
{
    v9x_u32 at = 0ul;
    v9x_u32 produced = 0ul;
    v9x_u32 needed = (texture != 0) ? v9x_i9xx_textured_state_extent()
                                    : v9x_i9xx_3d_state_extent();

    if (written != 0) { *written = 0ul; }
    if (stream == 0 || written == 0 || capacity < needed) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    /*
     * The pitch must be a multiple of four because BUF_INFO discards its low
     * two bits, and must fit the field. A pitch that does not survive the
     * encoding would place every row but the first at the wrong address - a
     * sheared picture rather than an error, which is why it is refused here.
     */
    if (width == 0ul || height == 0ul ||
        width > 0x10000ul || height > 0x10000ul ||
        target_pitch == 0ul || (target_pitch & 3ul) != 0ul ||
        target_pitch > V9X_I9XX_BUF_3D_PITCH_MASK ||
        (target_offset & 3ul) != 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    at += v9x_i9xx_emit_invariant(stream + at);
    at += v9x_i9xx_emit_target(stream + at, target_offset, target_pitch,
                               width, height);
    /*
     * The texture packets sit between the target and the pipeline. Order
     * follows both reference emitters, which write map and sampler state
     * before the state-immediate load that declares the coordinate format.
     */
    if (texture != 0) {
        if (v9x_i9xx_build_map_state(texture, 1ul, stream + at,
                                     capacity - at, &produced) !=
                V9X_STATUS_OK) {
            return V9X_STATUS_INVALID_ARGUMENT;
        }
        at += produced;
        if (v9x_i9xx_build_sampler_state(1ul, stream + at,
                                         capacity - at, &produced) !=
                V9X_STATUS_OK) {
            return V9X_STATUS_INVALID_ARGUMENT;
        }
        at += produced;
    }

    at += v9x_i9xx_emit_pipeline(stream + at,
                                 (texture != 0)
                                     ? V9X_I9XX_S2_TEXTURED_UNIT0
                                     : V9X_I9XX_S2_ALL_TEXCOORD_ABSENT);

    if (at != needed) {
        /* The emitters and the extent disagree, which is a programming error
         * rather than a bad argument; refuse rather than submit a stream whose
         * length nobody can predict. */
        return V9X_STATUS_INVALID_STATE;
    }
    *written = at;
    return V9X_STATUS_OK;
}
