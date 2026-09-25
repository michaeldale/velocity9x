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
/* The depth buffer's geometry lives with the sandbox layout that places it,
 * not with the packets that describe it. */
#include "velocity9x/intel_gma.h"

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
static v9x_u32 v9x_i9xx_emit_pipeline(v9x_u32 *stream, v9x_u32 s2,
                                      v9x_u32 s3, v9x_u32 s6)
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
    /*
     * S3: zero for every scene; for a runtime draw, the wrap-shortest bits of
     * coordinate set 0 when the application set WRAPU or WRAPV. Only
     * v9x_i9xx_build_runtime_state produces a non-zero value, and only those
     * two bits.
     */
    stream[at++] = s3;
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
    /*
     * S6, a PARAMETER for the same reason S2 is. Colour writes alone for an
     * un-Z'd scene; plus the depth test enable and the LESS function when
     * there is a depth buffer; plus the write enable only when the scene is
     * the one that writes depth.
     *
     * Three distinct dwords, and the decoder requires the one the scene
     * claims. A stream that enabled depth writes in a scene which merely
     * tests would modify the depth buffer unasked, and the next scene's clear
     * would hide the evidence.
     */
    stream[at++] = s6;

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
/*
 * A bound depth buffer: where it is, how wide, whether the draw writes to
 * it, and WHICH COMPARISON it uses. The last was a constant in the emitter
 * until 2026-09-20 - COMPAREFUNC_LESS for every draw - and the runtime path
 * skipped depth entirely for anything else.
 */
struct v9x_i9xx_depth_binding {
    v9x_u32 offset;
    v9x_u32 pitch;
    /* Non-zero adds S6_DEPTH_WRITE_ENABLE. Separate from the binding's
     * existence because a depth TEST without writes is a distinct scene and
     * the two must not be reachable by the same argument. */
    v9x_u32 writes;
    v9x_u32 compare;
};

static v9x_status v9x_i9xx_build_state_common(
    v9x_u32 target_offset, v9x_u32 target_pitch,
    v9x_u32 width, v9x_u32 height,
    const struct v9x_i9xx_texture *texture,
    const struct v9x_i9xx_depth_binding *depth, v9x_u32 kind,
    v9x_u32 blend_src, v9x_u32 blend_dst,
    v9x_u32 s3,
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
                                       width, height, texture, 0,
                                       V9X_I9XX_SCENE_TEXTURED, 0ul, 0ul,
                                       0ul, stream, capacity, written);
}

/*
 * The DEPTH state block: the untextured one plus a real depth BUF_INFO.
 *
 * The depth binding Phase 5 used to emit named address zero and was inert
 * because the S6 enables were clear. It was removed on 2026-09-16 precisely so
 * this step would start from a stream with no depth binding at all, rather
 * than replacing a bad one and reading the result against a baseline that
 * already contained it.
 *
 * docs\decisions\2026-09-16-intel-gen3-modulate-and-depth-audit.md sections
 * 3 and 4. The BUF_INFO encoding is MESA-SOURCED ONLY - xf86 has no depth
 * buffer anywhere and cannot corroborate it - which is why the scene that
 * exercises this is built to make a wrong binding visible rather than silent.
 */
v9x_u32 v9x_i9xx_depth_state_extent(void)
{
    /* One more BUF_INFO: the command, the identity dword and the address. */
    return v9x_i9xx_3d_state_extent() + 3ul;
}

v9x_status v9x_i9xx_build_depth_state(
    v9x_u32 target_offset, v9x_u32 target_pitch,
    v9x_u32 width, v9x_u32 height,
    v9x_u32 depth_offset, v9x_u32 depth_pitch, v9x_u32 writes,
    v9x_u32 *stream, v9x_u32 capacity, v9x_u32 *written)
{
    struct v9x_i9xx_depth_binding depth;

    if (depth_offset == 0ul || depth_pitch != V9X_I9XX_DEPTH_PITCH) {
        /*
         * Address zero is refused here as well as in the decoder. It is inside
         * the aperture and is not ours, and it is the exact value the removed
         * binding carried - so a regression that reinstated it would otherwise
         * build cleanly.
         */
        if (written != 0) { *written = 0ul; }
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    depth.offset = depth_offset;
    depth.pitch = depth_pitch;
    depth.writes = writes;
    /* A generated scene is pinned to what was audited, which is LESS. */
    depth.compare = V9X_I9XX_COMPAREFUNC_LESS;
    return v9x_i9xx_build_state_common(target_offset, target_pitch,
                                       width, height, 0, &depth,
                                       (writes != 0ul)
                                           ? V9X_I9XX_SCENE_DEPTH_WRITE
                                           : V9X_I9XX_SCENE_DEPTH_TEST,
                                       0ul, 0ul, 0ul,
                                       stream, capacity, written);
}

v9x_status v9x_i9xx_build_3d_state(
    v9x_u32 target_offset, v9x_u32 target_pitch,
    v9x_u32 width, v9x_u32 height,
    v9x_u32 *stream, v9x_u32 capacity, v9x_u32 *written)
{
    return v9x_i9xx_build_state_common(target_offset, target_pitch,
                                       width, height, 0, 0,
                                       V9X_I9XX_SCENE_PLAIN, 0ul, 0ul,
                                       0ul, stream, capacity, written);
}

/*
 * The RUNTIME block, which is the only one that may carry a texture AND a
 * depth buffer at once.
 *
 * Every scene carries one thing: that is what makes a scene readable, and
 * scene 1 modulating a texture while scene 2 tests depth is how the two were
 * measured apart. An application asks for whatever combination it likes, so
 * this is the one caller that needs the sum - and the emitter already
 * supported it, because S6 and the two optional packets were written as
 * independent conditions rather than as a choice between kinds. Only the
 * extent arithmetic had to learn about it.
 */
v9x_u16 v9x_i9xx_blend_factor_known(v9x_u32 factor)
{
    if (factor == V9X_I9XX_BLENDFACT_ZERO ||
        factor == V9X_I9XX_BLENDFACT_ONE ||
        factor == V9X_I9XX_BLENDFACT_SRC_COLR ||
        factor == V9X_I9XX_BLENDFACT_INV_SRC_COLR ||
        factor == V9X_I9XX_BLENDFACT_SRC_ALPHA ||
        factor == V9X_I9XX_BLENDFACT_INV_SRC_ALPHA ||
        factor == V9X_I9XX_BLENDFACT_DST_COLR ||
        factor == V9X_I9XX_BLENDFACT_INV_DST_COLR) {
        return V9X_TRUE;
    }
    return V9X_FALSE;
}

v9x_u32 v9x_i9xx_runtime_state_extent(v9x_u32 textured, v9x_u32 depthed,
                                      v9x_u32 blend)
{
    v9x_u32 extent = v9x_i9xx_3d_state_extent();

    if (blend != 0ul) {
        /* The IAB disable, one dword, as the blend scene carries it. */
        extent += 1ul;
    }

    if (textured != 0ul) {
        extent += v9x_i9xx_map_state_extent(1ul) +
                  v9x_i9xx_sampler_state_extent(1ul);
    }
    if (depthed != 0ul) {
        /* One BUF_INFO: the command, the identity dword and the address. */
        extent += 3ul;
    }
    return extent;
}

v9x_status v9x_i9xx_build_runtime_state(
    v9x_u32 target_offset, v9x_u32 target_pitch,
    v9x_u32 width, v9x_u32 height,
    const struct v9x_i9xx_texture *texture,
    v9x_u32 depth_offset, v9x_u32 depth_pitch, v9x_u32 depth_writes,
    v9x_u32 depth_compare,
    v9x_u32 blend_src, v9x_u32 blend_dst,
    v9x_u32 cylinder,
    v9x_u32 *stream, v9x_u32 capacity, v9x_u32 *written)
{
    struct v9x_i9xx_depth_binding depth;
    v9x_u32 s3 = 0ul;

    if (written != 0) { *written = 0ul; }
    /*
     * The cylinder request, translated here so the S3 dword can only ever
     * carry the two wrap-shortest bits for set 0. Refused without a texture
     * rather than emitted anyway: an untextured stream has no coordinate set
     * to wrap, and the decoder would refuse the stream later with less to
     * say about why.
     */
    if ((cylinder & ~(V9X_I9XX_CYLINDER_U | V9X_I9XX_CYLINDER_V)) != 0ul ||
        (cylinder != 0ul && texture == 0)) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if ((cylinder & V9X_I9XX_CYLINDER_U) != 0ul) {
        s3 |= V9X_I9XX_S3_WRAP_SHORTEST_TCX0;
    }
    if ((cylinder & V9X_I9XX_CYLINDER_V) != 0ul) {
        s3 |= V9X_I9XX_S3_WRAP_SHORTEST_TCY0;
    }
    /* Both codes or neither, and each one of the four. ONE/ZERO with the
     * enable is a legal request that draws opaque; the caller passes zeros
     * for it rather than asking for an enable that does nothing
     * measurable. */
    if ((blend_src != 0ul) != (blend_dst != 0ul) ||
        (blend_src != 0ul &&
         (v9x_i9xx_blend_factor_known(blend_src) == V9X_FALSE ||
          v9x_i9xx_blend_factor_known(blend_dst) == V9X_FALSE))) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (depth_offset != 0ul) {
        /*
         * The application's Z surface, so its pitch is whatever DirectDraw
         * allocated rather than this driver's own constant. Checked on the
         * same terms as the render target's - a multiple of four that fits
         * the field - because BUF_INFO encodes both the same way and discards
         * the low two bits of either. A depth pitch that does not survive the
         * encoding puts every row but the first at the wrong address, which
         * is a depth buffer that appears to work and rejects the wrong
         * fragments.
         */
        if ((depth_pitch & 3ul) != 0ul || depth_pitch == 0ul ||
            depth_pitch > V9X_I9XX_BUF_3D_PITCH_MASK) {
            return V9X_STATUS_INVALID_ARGUMENT;
        }
        depth.offset = depth_offset;
        depth.pitch = depth_pitch;
        depth.writes = depth_writes;
        depth.compare = depth_compare & 7ul;
    }
    return v9x_i9xx_build_state_common(
        target_offset, target_pitch, width, height, texture,
        depth_offset != 0ul ? &depth : 0,
        V9X_I9XX_SCENE_RUNTIME, blend_src, blend_dst, s3,
        stream, capacity, written);
}

/*
 * The ALPHA state block: the untextured one with a different S6, and for a
 * blend scene one extra dword.
 *
 * Both kinds are untextured and un-Z'd on purpose. The alpha comes from the
 * vertex colour, which the untextured program already moves to the output, so
 * neither needs a texture format this driver has never emitted, and neither
 * puts the texture path inside a measurement that is about alpha.
 *
 * docs\decisions\2026-09-16-intel-gen3-alpha-test-and-blend-audit.md.
 */
v9x_u32 v9x_i9xx_alpha_state_extent(v9x_u32 kind)
{
    if (kind == V9X_I9XX_SCENE_BLEND) {
        /* One more: the IAB disable, which is a single dword. */
        return v9x_i9xx_3d_state_extent() + 1ul;
    }
    return v9x_i9xx_3d_state_extent();
}

v9x_status v9x_i9xx_build_alpha_state(
    v9x_u32 target_offset, v9x_u32 target_pitch,
    v9x_u32 width, v9x_u32 height, v9x_u32 kind,
    v9x_u32 *stream, v9x_u32 capacity, v9x_u32 *written)
{
    if (kind != V9X_I9XX_SCENE_ALPHA_TEST && kind != V9X_I9XX_SCENE_BLEND) {
        if (written != 0) { *written = 0ul; }
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    return v9x_i9xx_build_state_common(target_offset, target_pitch,
                                       width, height, 0, 0, kind, 0ul, 0ul,
                                       0ul, stream, capacity, written);
}

static v9x_status v9x_i9xx_build_state_common(
    v9x_u32 target_offset, v9x_u32 target_pitch,
    v9x_u32 width, v9x_u32 height,
    const struct v9x_i9xx_texture *texture,
    const struct v9x_i9xx_depth_binding *depth, v9x_u32 kind,
    v9x_u32 blend_src, v9x_u32 blend_dst,
    v9x_u32 s3,
    v9x_u32 *stream, v9x_u32 capacity, v9x_u32 *written)
{
    /* Blending is the enable's presence; the codes were checked by the
     * runtime builder, the only caller that passes any. */
    v9x_u32 blend = (blend_src != 0ul || blend_dst != 0ul) ? 1ul : 0ul;

    v9x_u32 at = 0ul;
    v9x_u32 produced = 0ul;
    v9x_u32 needed;

    if (kind == V9X_I9XX_SCENE_RUNTIME) {
        /* Tested FIRST, because it is the only kind that may carry both and
         * either branch below would have sized it for one of them. A block
         * sized for one and emitting two overruns the caller's buffer. */
        needed = v9x_i9xx_runtime_state_extent(texture != 0 ? 1ul : 0ul,
                                               depth != 0 ? 1ul : 0ul,
                                               blend);
    } else if (texture != 0) {
        needed = v9x_i9xx_textured_state_extent();
    } else if (depth != 0) {
        needed = v9x_i9xx_depth_state_extent();
    } else if (kind == V9X_I9XX_SCENE_ALPHA_TEST ||
               kind == V9X_I9XX_SCENE_BLEND) {
        needed = v9x_i9xx_alpha_state_extent(kind);
    } else {
        needed = v9x_i9xx_3d_state_extent();
    }

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
     * The depth BUF_INFO, beside the colour one and built the same way. Mesa
     * builds both through a single code path - identity, then pitch, then
     * tiling bits only if tiled - and I915_TILE_NONE contributes nothing, so
     * a linear depth buffer is the absence of those bits rather than a
     * special case.
     */
    if (depth != 0) {
        stream[at++] = V9X_I9XX_3DSTATE_BUF_INFO;
        stream[at++] = V9X_I9XX_BUF_3D_ID_DEPTH |
                       (depth->pitch & V9X_I9XX_BUF_3D_PITCH_MASK);
        stream[at++] = depth->offset;
    }
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
        if (v9x_i9xx_build_sampler_state(texture, 1ul, stream + at,
                                         capacity - at, &produced) !=
                V9X_STATUS_OK) {
            return V9X_STATUS_INVALID_ARGUMENT;
        }
        at += produced;
    }

    /*
     * The IAB disable, before the state-immediate that enables blending.
     *
     * Emitted for the BLEND kind alone rather than added to the invariant
     * block: that block is shared by every scene, and putting it there would
     * change scene 0's stream and with it the regression that says nothing
     * changed. IAB matters only when the colour blend enable is set, so
     * confining it to the scene that sets that bit costs nothing.
     */
    if (kind == V9X_I9XX_SCENE_BLEND || blend != 0ul) {
        stream[at++] = V9X_I9XX_IAB_DISABLE_DWORD;
    }

    {
        v9x_u32 s6 = V9X_I9XX_S6_PHASE5;

        if (kind == V9X_I9XX_SCENE_ALPHA_TEST) {
            /* GREATER, so a fragment at or below the reference is rejected.
             * The reference is a whole byte, which is the precision the
             * comparison happens at whatever the target's format is. */
            s6 |= V9X_I9XX_S6_ALPHA_TEST_ENABLE |
                  (V9X_I9XX_COMPAREFUNC_GREATER <<
                       V9X_I9XX_S6_ALPHA_FUNC_SHIFT) |
                  (V9X_I9XX_ALPHA_REF << V9X_I9XX_S6_ALPHA_REF_SHIFT);
        }
        if (kind == V9X_I9XX_SCENE_BLEND) {
            /* Source alpha over one minus source alpha, added. Both factors
             * are functions of the FRAGMENT, so neither depends on a
             * destination alpha that a 565 target does not have and that
             * neither reference tree accounts for. */
            s6 |= V9X_I9XX_S6_BLEND_ENABLE |
                  (V9X_I9XX_BLENDFUNC_ADD <<
                       V9X_I9XX_S6_BLEND_FUNC_SHIFT) |
                  (V9X_I9XX_BLENDFACT_SRC_ALPHA <<
                       V9X_I9XX_S6_SRC_FACTOR_SHIFT) |
                  (V9X_I9XX_BLENDFACT_INV_SRC_ALPHA <<
                       V9X_I9XX_S6_DST_FACTOR_SHIFT);
        } else if (blend != 0ul) {
            /* The application's pair, from the four codes the audit sources.
             * Each is a function of the fragment or a constant; the
             * destination-alpha codes are not among them. */
            s6 |= V9X_I9XX_S6_BLEND_ENABLE |
                  (V9X_I9XX_BLENDFUNC_ADD <<
                       V9X_I9XX_S6_BLEND_FUNC_SHIFT) |
                  (blend_src << V9X_I9XX_S6_SRC_FACTOR_SHIFT) |
                  (blend_dst << V9X_I9XX_S6_DST_FACTOR_SHIFT);
        }
        if (depth != 0) {
            /*
             * The caller's comparison, not a constant. This emitted
             * COMPAREFUNC_LESS for every draw and the runtime path skipped
             * the depth test entirely for anything else, which intel98
             * measured as eighty-five per cent of a 3DMark99 run.
             *
             * Only the runtime builder takes this; the generated scenes
             * pass LESS and their artefacts are unchanged, which is what
             * keeps the Phase 4 and Phase 5 CRCs where they were.
             */
            s6 |= V9X_I9XX_S6_DEPTH_TEST_ENABLE |
                  ((depth->compare & 7ul) <<
                       V9X_I9XX_S6_DEPTH_FUNC_SHIFT);
            if (depth->writes != 0ul) {
                s6 |= V9X_I9XX_S6_DEPTH_WRITE_ENABLE;
            }
        }
        at += v9x_i9xx_emit_pipeline(stream + at,
                                     (texture != 0)
                                         ? V9X_I9XX_S2_TEXTURED_UNIT0
                                         : V9X_I9XX_S2_ALL_TEXCOORD_ABSENT,
                                     s3, s6);
    }

    if (at != needed) {
        /* The emitters and the extent disagree, which is a programming error
         * rather than a bad argument; refuse rather than submit a stream whose
         * length nobody can predict. */
        return V9X_STATUS_INVALID_STATE;
    }
    *written = at;
    return V9X_STATUS_OK;
}
