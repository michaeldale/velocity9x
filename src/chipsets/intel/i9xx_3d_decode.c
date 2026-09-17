/*
 * The Phase 5 stream decoder: an allowlist, not a parser.
 *
 * It exists to refuse, and it returns BOTH a numbered reason AND the dword
 * index it rejected, because Phase 4's record names ambiguous refusals as the
 * reason that phase cost eight boots. A reason without an index cannot
 * distinguish which of two BUF_INFO packets was wrong.
 *
 * v9x_i9xx_decode_phase4_stream is deliberately untouched. Two phases, two
 * decoders, two allowlists: sharing one would mean a Phase 4 stream could
 * satisfy a Phase 5 check or the reverse, and the phases exist precisely to be
 * distinguishable.
 */
#include "velocity9x/intel_gen3_3d.h"
#include "velocity9x/intel_gma.h"

#define V9X_I9XX_REJECT(reason_code, index_value) \
    do { \
        if (rejected_index != 0) { *rejected_index = (index_value); } \
        return (reason_code); \
    } while (0)

/*
 * Commands that must never appear in an UNTEXTURED stream. Texture state is
 * forbidden outright rather than merely unused: a triangle that somehow
 * carried a texture packet would be sampling memory we never validated.
 *
 * These opcodes were WRONG until 2026-09-16 - 0x7d1d0000 and 0x7d180000, which
 * are not commands. The major opcode 0x1d had been written into the sub-opcode
 * position, apparently by pattern-matching LOAD_STATE_IMMEDIATE_1's 0x7d04.
 * The real values are 0x7d000000 and 0x7d010000, established by the texture
 * audit from two independent emitters.
 *
 * WHAT THAT DID, stated precisely because the first version of this comment
 * overstated it: a real texture packet was still REJECTED, by the unknown-
 * opcode fallback at the end of the loop, as BAD_OPCODE. There was no
 * acceptance hole. What was wrong was the classification - a capture would
 * have said "unknown opcode at index n" where the truth is "a texture packet
 * in a stream that forbids them", which are different findings and send a
 * reader to different places.
 *
 * The comparison was also unmasked, and that half could not have matched even
 * with the right opcode: both packets carry a length in their low bits, three
 * per unit, so a real MAP_STATE is 0x7d000003. The convention it needed is two
 * branches below, where LOAD_STATE_IMMEDIATE_1 masks with 0xffff0000 for
 * exactly this reason.
 *
 * They now come from the header, where the texture builders also take them, so
 * the decoder and the thing it decodes cannot disagree.
 */

/*
 * Is this dword 0.0f, 0.5f or 1.0f?
 *
 * The three texture coordinates this build emits, and nothing else. Written as
 * a predicate over bit patterns because the transport carries bit patterns and
 * the driver's float helper takes integers - it has no way to say "a half".
 */
static v9x_u16 v9x_i9xx_normalized_half(v9x_u32 bits)
{
    if (bits == 0x00000000ul || bits == 0x3f000000ul ||
        bits == 0x3f800000ul) {
        return V9X_TRUE;
    }
    return V9X_FALSE;
}

/*
 * Is this the vertex colour the kind emits?
 *
 * The plain and depth kinds draw flat measured colours - three of them in a
 * depth scene, one per triangle - and the textured kinds carry a colour the
 * fragment program either ignores or multiplies by. Each is pinned, because a
 * vertex colour nobody chose is a picture nobody can read.
 */
/*
 * How many triangles a kind draws.
 *
 * The decoder cannot ask the scene table - it validates a stream, not a scene -
 * so the count is per kind and stated here. A stream whose vertex count does
 * not match is refused rather than read as far as it goes.
 */
/*
 * Where quadrant n starts, relative to the texture's base.
 *
 * The same arithmetic the paint builder uses: (n & 1) half-widths across and
 * (n >> 1) half-heights down. Restated here rather than shared, because the
 * builder's copy lives in I9XXCODE and this is the independent opinion on it -
 * sharing one function would make the check agree with itself.
 */
static v9x_u32 v9x_i9xx_decode_quadrant_offset(v9x_u32 quadrant)
{
    v9x_u32 offset = 0ul;

    if ((quadrant & 1ul) != 0ul) {
        offset += (v9x_u32)((v9x_u16)V9X_I9XX_TEXTURE_BLOCK * (v9x_u16)2u);
    }
    if ((quadrant & 2ul) != 0ul) {
        offset += (v9x_u32)((v9x_u16)V9X_I9XX_TEXTURE_BLOCK *
                            (v9x_u16)V9X_I9XX_TEXTURE_PITCH);
    }
    return offset;
}

static v9x_u32 v9x_i9xx_decode_triangles(v9x_u32 kind)
{
    if (v9x_i9xx_scene_kind_depth(kind) != V9X_FALSE ||
        kind == V9X_I9XX_SCENE_ALPHA_TEST) {
        return 3ul;
    }
    if (kind == V9X_I9XX_SCENE_BLEND) {
        return 2ul;
    }
    return 1ul;
}

static v9x_u16 v9x_i9xx_decode_vertex_color(v9x_u32 kind, v9x_u32 color)
{
    if (kind == V9X_I9XX_SCENE_TEXTURED) {
        return (color == V9X_I9XX_TEX_VERTEX_COLOR_BGRA)
                   ? V9X_TRUE : V9X_FALSE;
    }
    if (kind == V9X_I9XX_SCENE_MODULATED) {
        return (color == V9X_I9XX_TEX_MODULATE_COLOR_BGRA)
                   ? V9X_TRUE : V9X_FALSE;
    }
    if (v9x_i9xx_scene_kind_depth(kind) != V9X_FALSE) {
        if (color == V9X_I9XX_TRI_COLOR_BGRA ||
            color == V9X_I9XX_TRI_COLOR_B ||
            color == V9X_I9XX_TRI_COLOR_C) {
            return V9X_TRUE;
        }
        return V9X_FALSE;
    }
    if (kind == V9X_I9XX_SCENE_GOURAUD) {
        /*
         * Three colours on one triangle, so the allowlist is the set rather
         * than one value. Position is NOT pinned: which vertex carries which
         * primary is the question the scene asks, and a decoder that required
         * an order would be asserting the answer.
         */
        if (color == V9X_I9XX_GOURAUD_COLOR_A ||
            color == V9X_I9XX_GOURAUD_COLOR_B ||
            color == V9X_I9XX_GOURAUD_COLOR_C) {
            return V9X_TRUE;
        }
        return V9X_FALSE;
    }
    if (kind == V9X_I9XX_SCENE_ALPHA_TEST) {
        /* The same three colours with their top byte varied. Pinned including
         * the alpha, because the alpha IS the experiment: a stream carrying a
         * different one would test a reference nobody chose. */
        if (color == V9X_I9XX_ALPHA_COLOR_PASS ||
            color == V9X_I9XX_ALPHA_COLOR_FAIL ||
            color == V9X_I9XX_ALPHA_COLOR_PASS2) {
            return V9X_TRUE;
        }
        return V9X_FALSE;
    }
    if (kind == V9X_I9XX_SCENE_BLEND) {
        if (color == V9X_I9XX_BLEND_COLOR_UNDER ||
            color == V9X_I9XX_BLEND_COLOR_OVER) {
            return V9X_TRUE;
        }
        return V9X_FALSE;
    }
    return (color == V9X_I9XX_TRI_COLOR_BGRA) ? V9X_TRUE : V9X_FALSE;
}

v9x_u16 v9x_i9xx_decode_phase5_stream(
    const v9x_u32 *stream, v9x_u32 dword_count,
    const struct v9x_i9xx_decode_limits *limits,
    v9x_u32 *rejected_index)
{
    v9x_u32 index = 0ul;
    v9x_u32 target_end;
    v9x_u32 target_offset;
    v9x_u32 target_bytes;
    v9x_u32 texture_offset;
    v9x_u32 texture_bytes;
    v9x_u32 depth_offset;
    v9x_u32 depth_bytes;
    v9x_u16 textured;
    v9x_u16 depthed;
    v9x_u16 saw_map_state = V9X_FALSE;
    v9x_u16 saw_sampler_state = V9X_FALSE;
    v9x_u16 saw_depth_buf_info = V9X_FALSE;
    v9x_u16 saw_texture_paint = V9X_FALSE;
    v9x_u16 saw_depth_clear = V9X_FALSE;
    v9x_u16 saw_iab_disable = V9X_FALSE;
    /* One bit per texture quadrant painted, so the four can be required to be
     * four DIFFERENT ones rather than four of anything. */
    v9x_u32 painted = 0ul;
    v9x_u32 width_bits = 0ul;
    v9x_u32 height_bits = 0ul;
    v9x_u16 saw_buf_info_color = V9X_FALSE;
    v9x_u16 saw_dst_buf_vars = V9X_FALSE;
    v9x_u16 saw_draw_rect = V9X_FALSE;
    v9x_u16 saw_scissor_disable = V9X_FALSE;
    v9x_u16 saw_indirect_disable = V9X_FALSE;
    v9x_u16 saw_shader = V9X_FALSE;
    v9x_u16 saw_primitive = V9X_FALSE;
    /* Without the fill the target holds whatever it held, and "the triangle
     * drew" becomes indistinguishable from "that memory already looked like
     * this". Its absence is a refusal, not a warning. */
    v9x_u16 saw_fill = V9X_FALSE;

    if (rejected_index != 0) { *rejected_index = 0ul; }
    if (stream == 0 || limits == 0) {
        V9X_I9XX_REJECT(V9X_I9XX_P5_TRUNCATED, 0ul);
    }
    target_offset = limits->target_offset;
    target_bytes = limits->target_bytes;
    texture_offset = limits->texture_offset;
    texture_bytes = limits->texture_bytes;
    depth_offset = limits->depth_offset;
    depth_bytes = limits->depth_bytes;
    /*
     * What the KIND requires, and what the ranges permit, checked against each
     * other before anything else. A modulated stream with no texture range, or
     * a plain stream carrying a depth range, is a caller that has got its own
     * arguments wrong - and the decoder refusing is cheaper than validating a
     * stream against limits nobody meant.
     */
    /*
     * WHICH SURFACES THIS STREAM USES.
     *
     * A scene is answered by its kind, and the consistency check below then
     * pins the caller's declaration to it - a textured scene handed no texture
     * range is a caller and a stream that disagree.
     *
     * A runtime stream has no such kind. The application binds a texture or a
     * depth buffer per draw, so the declaration IS the answer and there is
     * nothing to cross-check it against here. The check that matters for those
     * surfaces is the footprint arithmetic in the HAL, which refuses before
     * this decoder ever runs.
     */
    if (limits->kind == V9X_I9XX_SCENE_RUNTIME) {
        textured = (texture_bytes != 0ul) ? V9X_TRUE : V9X_FALSE;
        depthed = (depth_bytes != 0ul) ? V9X_TRUE : V9X_FALSE;
    } else {
        textured = v9x_i9xx_scene_kind_textured(limits->kind);
        depthed = v9x_i9xx_scene_kind_depth(limits->kind);
    }
    if (limits->kind != V9X_I9XX_SCENE_RUNTIME &&
        ((textured != V9X_FALSE) != (texture_bytes != 0ul) ||
         (depthed != V9X_FALSE) != (depth_bytes != 0ul))) {
        V9X_I9XX_REJECT(V9X_I9XX_P5_TRUNCATED, 0ul);
    }
    if (dword_count == 0ul || target_bytes == 0ul ||
        target_offset > 0xfffffffful - target_bytes) {
        V9X_I9XX_REJECT(V9X_I9XX_P5_TRUNCATED, 0ul);
    }
    /*
     * The target's shape has to be describable before anything is checked
     * against it. A zero width or height would make the DRAW_RECT comparison
     * underflow to 0xffff and accept a rectangle nobody asked for.
     */
    if (limits->target_width == 0ul || limits->target_height == 0ul ||
        limits->target_pitch == 0ul ||
        (limits->target_pitch & 3ul) != 0ul ||
        limits->target_pitch > V9X_I9XX_BUF_3D_PITCH_MASK) {
        V9X_I9XX_REJECT(V9X_I9XX_P5_TRUNCATED, 0ul);
    }
    if (textured != V9X_FALSE &&
        texture_offset > 0xfffffffful - texture_bytes) {
        V9X_I9XX_REJECT(V9X_I9XX_P5_TRUNCATED, 0ul);
    }
    if (depthed != V9X_FALSE &&
        (depth_offset == 0ul ||
         depth_offset > 0xfffffffful - depth_bytes)) {
        /* Address zero included: it is inside the aperture, it is not ours,
         * and it is exactly the value the removed Phase 5 binding carried. */
        V9X_I9XX_REJECT(V9X_I9XX_P5_TRUNCATED, 0ul);
    }
    /*
     * The rectangle as FLOATS, for the runtime coordinate checks. Converted
     * through the same helper the builder uses, so the decoder's edge and the
     * builder's edge are the same number rather than two derivations of it.
     */
    if (v9x_i9xx_float_from_int(limits->target_width, &width_bits) !=
            V9X_I9XX_FLOAT_OK ||
        v9x_i9xx_float_from_int(limits->target_height, &height_bits) !=
            V9X_I9XX_FLOAT_OK) {
        V9X_I9XX_REJECT(V9X_I9XX_P5_TRUNCATED, 0ul);
    }
    target_end = target_offset + target_bytes;

    while (index < dword_count) {
        v9x_u32 command = stream[index];

        if (command == V9X_I9XX_3DSTATE_BUF_INFO) {
            v9x_u32 identity;
            v9x_u32 address;
            if (dword_count - index < 3ul) {
                V9X_I9XX_REJECT(V9X_I9XX_P5_TRUNCATED, index);
            }
            identity = stream[index + 1ul];
            address = stream[index + 2ul];
            if ((identity & (V9X_I9XX_BUF_3D_USE_FENCE |
                             V9X_I9XX_BUF_3D_TILED_SURFACE)) != 0ul) {
                V9X_I9XX_REJECT(V9X_I9XX_P5_TILED_FORBIDDEN, index + 1ul);
            }
            if ((identity & V9X_I9XX_BUF_3D_ID_DEPTH) ==
                    V9X_I9XX_BUF_3D_ID_DEPTH) {
                /*
                 * A REAL depth buffer, from 2026-09-16.
                 *
                 * The issue that closed the address-zero binding said in
                 * advance what this would become: "a check that its address
                 * is inside the reserve, not a relaxation back to accepting
                 * zero". Address zero is still refused - the preamble rejects
                 * a zero depth range outright, and an undepthed stream
                 * refuses the packet entirely.
                 *
                 * The encoding is MESA-SOURCED ONLY. xf86 has no depth buffer
                 * anywhere, so no second site can exist, and this check is
                 * the only thing standing between a mis-derived binding and a
                 * GPU reading a page that is not ours.
                 */
                if (depthed == V9X_FALSE) {
                    V9X_I9XX_REJECT(V9X_I9XX_P5_DEPTH_FORBIDDEN, index + 1ul);
                }
                /*
                 * The pitch: this build's constant for a scene, the
                 * application's surface for a runtime stream. Equality either
                 * way - the question is whether the stream binds the buffer
                 * the engine meant, and BUF_INFO discards the pitch's low two
                 * bits, so a pitch that did not survive the encoding would
                 * place every depth row but the first at the wrong address.
                 */
                if ((identity & V9X_I9XX_BUF_3D_PITCH_MASK) !=
                        ((limits->kind == V9X_I9XX_SCENE_RUNTIME
                              ? limits->depth_pitch
                              : V9X_I9XX_DEPTH_PITCH) &
                         V9X_I9XX_BUF_3D_PITCH_MASK)) {
                    V9X_I9XX_REJECT(V9X_I9XX_P5_PITCH, index + 1ul);
                }
                if (address != depth_offset) {
                    V9X_I9XX_REJECT(V9X_I9XX_P5_TARGET_RANGE, index + 2ul);
                }
                /*
                 * And the range must hold the buffer the pitch implies.
                 *
                 * A SCENE only: both operands are constants and the product is
                 * folded. A runtime stream's are variables, and a 32-bit
                 * multiply here calls __U4D's sibling __U4M in the default
                 * CODE segment, which a near call from I9XXCODE cannot reach.
                 * The runtime footprint is computed in v9x_d3d_i9xx.c, in
                 * 32-bit code, and refused there before this runs.
                 */
                if (limits->kind != V9X_I9XX_SCENE_RUNTIME &&
                    depth_bytes < (v9x_u32)(V9X_I9XX_DEPTH_HEIGHT *
                                            V9X_I9XX_DEPTH_PITCH)) {
                    V9X_I9XX_REJECT(V9X_I9XX_P5_TARGET_RANGE, index + 2ul);
                }
                saw_depth_buf_info = V9X_TRUE;
                index += 3ul;
                continue;
            }
            if ((identity & V9X_I9XX_BUF_3D_ID_COLOR_BACK) !=
                    V9X_I9XX_BUF_3D_ID_COLOR_BACK) {
                V9X_I9XX_REJECT(V9X_I9XX_P5_BAD_OPCODE, index + 1ul);
            }
            if ((identity & V9X_I9XX_BUF_3D_PITCH_MASK) !=
                    (limits->target_pitch & V9X_I9XX_BUF_3D_PITCH_MASK)) {
                V9X_I9XX_REJECT(V9X_I9XX_P5_PITCH, index + 1ul);
            }
            /* The colour target must be exactly the reserve's target page, and
             * its last row must still be inside it. */
            if (address != target_offset ||
                address > target_end - target_bytes) {
                V9X_I9XX_REJECT(V9X_I9XX_P5_TARGET_RANGE, index + 2ul);
            }
            saw_buf_info_color = V9X_TRUE;
            index += 3ul;

        } else if (command == V9X_I9XX_3DSTATE_DST_BUF_VARS) {
            v9x_u32 format;
            if (dword_count - index < 2ul) {
                V9X_I9XX_REJECT(V9X_I9XX_P5_TRUNCATED, index);
            }
            format = stream[index + 1ul];
            if (format != (V9X_I9XX_COLR_BUF_RGB565 |
                           V9X_I9XX_DEPTH_FRMT_16_FIXED |
                           V9X_I9XX_DSTORG_HORT_BIAS_HALF |
                           V9X_I9XX_DSTORG_VERT_BIAS_HALF)) {
                V9X_I9XX_REJECT(V9X_I9XX_P5_FORMAT, index + 1ul);
            }
            saw_dst_buf_vars = V9X_TRUE;
            index += 2ul;

        } else if (command == V9X_I9XX_3DSTATE_DRAW_RECT) {
            if (dword_count - index < 5ul) {
                V9X_I9XX_REJECT(V9X_I9XX_P5_TRUNCATED, index);
            }
            if (stream[index + 1ul] != 0ul || stream[index + 2ul] != 0ul ||
                stream[index + 4ul] != 0ul) {
                V9X_I9XX_REJECT(V9X_I9XX_P5_DRAW_RECT, index + 1ul);
            }
            /* Inclusive bounds: exactly one less than the extent. */
            if (stream[index + 3ul] !=
                    (((limits->target_height - 1ul) << 16) |
                     (limits->target_width - 1ul))) {
                V9X_I9XX_REJECT(V9X_I9XX_P5_DRAW_RECT, index + 3ul);
            }
            saw_draw_rect = V9X_TRUE;
            index += 5ul;

        } else if (command == (V9X_I9XX_3DSTATE_SCISSOR_ENABLE |
                               V9X_I9XX_DISABLE_SCISSOR_RECT)) {
            saw_scissor_disable = V9X_TRUE;
            index += 1ul;

        } else if ((command & 0xffff0000ul) ==
                       V9X_I9XX_3DSTATE_SCISSOR_ENABLE) {
            /* Any other scissor-enable encoding, including the one that turns
             * it on, is refused: a scissor rectangle we did not set would clip
             * the triangle for a reason invisible in the capture. */
            V9X_I9XX_REJECT(V9X_I9XX_P5_SCISSOR_ENABLED, index);

        } else if (command == V9X_I9XX_3DSTATE_SCISSOR_RECT_0) {
            if (dword_count - index < 3ul) {
                V9X_I9XX_REJECT(V9X_I9XX_P5_TRUNCATED, index);
            }
            if (stream[index + 1ul] != 0ul || stream[index + 2ul] != 0ul) {
                V9X_I9XX_REJECT(V9X_I9XX_P5_BAD_LENGTH, index + 1ul);
            }
            index += 3ul;

        } else if (command == V9X_I9XX_3DSTATE_LOAD_INDIRECT) {
            if (dword_count - index < 2ul) {
                V9X_I9XX_REJECT(V9X_I9XX_P5_TRUNCATED, index);
            }
            /* Only the disable form, with an empty enable mask, is allowed. */
            if (stream[index + 1ul] != 0ul) {
                V9X_I9XX_REJECT(V9X_I9XX_P5_INDIRECT_FORBIDDEN, index + 1ul);
            }
            saw_indirect_disable = V9X_TRUE;
            index += 2ul;

        } else if ((command & 0xffff0000ul) == V9X_I9XX_3DSTATE_MAP_STATE ||
                   (command & 0xffff0000ul) ==
                       V9X_I9XX_3DSTATE_SAMPLER_STATE) {
            /*
             * MASKED, like every other length-bearing packet in this file.
             *
             * This compared the whole dword against the bare opcode, and both
             * packets carry a length in their low bits - three per unit - so
             * a real MAP_STATE is 0x7d000003 and never equalled 0x7d000000.
             * Both halves were wrong - the opcodes and the missing mask -
             * but a real texture packet still fell through to the
             * unknown-opcode fallback and was rejected as BAD_OPCODE. This
             * corrects the REASON, not an acceptance hole.
             *
             * The convention it needed is two branches below, where
             * LOAD_STATE_IMMEDIATE_1 masks with 0xffff0000 for exactly this
             * reason.
             */
            if (textured == V9X_FALSE) {
                V9X_I9XX_REJECT(V9X_I9XX_P5_TEXTURE_FORBIDDEN, index);
            }
            {
                /* The length is the payload less one, and the payload is the
                 * enable mask plus three dwords per unit. One unit. */
                v9x_u32 payload = (command & 0x0000fffful) + 1ul;

                if (dword_count - index < payload + 1ul) {
                    V9X_I9XX_REJECT(V9X_I9XX_P5_TRUNCATED, index);
                }
                if (payload != 4ul) {
                    /* Exactly one unit. More would describe texture state
                     * this build never set and cannot account for. */
                    V9X_I9XX_REJECT(V9X_I9XX_P5_BAD_LENGTH, index);
                }
                if (stream[index + 1ul] != 1ul) {
                    /* Unit 0 alone enabled. */
                    V9X_I9XX_REJECT(V9X_I9XX_P5_BAD_LENGTH, index + 1ul);
                }
                if ((command & 0xffff0000ul) ==
                        V9X_I9XX_3DSTATE_MAP_STATE) {
                    /*
                     * The map ADDRESS, checked against the reserve rather
                     * than trusted. This is the dword that makes the GPU read
                     * memory of the driver's choosing, and a wrong one reads a
                     * page that is not ours - which on this part is the access
                     * that hangs it.
                     */
                    if (stream[index + 2ul] != texture_offset) {
                        V9X_I9XX_REJECT(V9X_I9XX_P5_TARGET_RANGE, index + 2ul);
                    }
                    /*
                     * And the FOOTPRINT, which the address alone does not
                     * bound. This was missing until 2026-09-16: MS4 carries
                     * the pitch, and a pitch four times the real one leaves
                     * the address correct and every other check satisfied
                     * while the sampler reads 32 rows of 8192 bytes out of a
                     * 2048-byte allocation - 126 KiB past the end, into pages
                     * that are not ours. MS3's height does the same by the
                     * same route.
                     *
                     * Required to be EXACTLY this build's geometry rather
                     * than merely to fit. The paint builder refuses any other
                     * shape, so any other shape here is a map describing a
                     * texture that was never painted, whether or not it
                     * happens to land inside the allocation.
                     *
                     * This restates the encoding the builder produces, and is
                     * worth being clear about what that does and does not
                     * catch: a stream that is not the one this build emits,
                     * yes; a shared misreading of the databook by both, no.
                     * The same is true of the BUF_INFO pitch check above, and
                     * for the same reason - an allowlist can only refuse what
                     * it was told to expect.
                     */
                    {
                        /*
                         * The shape, from this build's constants for a scene
                         * and from the application's surface for a runtime
                         * stream. Equality either way: the question is whether
                         * the stream describes the map the engine meant, and
                         * "fits inside the allocation" is a different and
                         * weaker question that the HAL has already answered.
                         */
                        v9x_u32 map_width = V9X_I9XX_TEXTURE_WIDTH;
                        v9x_u32 map_height = V9X_I9XX_TEXTURE_HEIGHT;
                        v9x_u32 map_pitch = V9X_I9XX_TEXTURE_PITCH;
                        v9x_u32 map_format = V9X_I9XX_MAPSURF_16BIT_RGB565;

                        if (limits->kind == V9X_I9XX_SCENE_RUNTIME) {
                            map_width = limits->texture_width;
                            map_height = limits->texture_height;
                            map_pitch = limits->texture_pitch;
                            if (map_width == 0ul || map_height == 0ul ||
                                map_pitch == 0ul) {
                                V9X_I9XX_REJECT(V9X_I9XX_P5_TEXTURE_STATE,
                                                index + 3ul);
                            }
                            /*
                             * The format the engine read off the surface,
                             * one of the three the builder emits. A scene
                             * never gets here: its format is the constant
                             * above, so a scene stream carrying an alpha
                             * format is rejected as not the audited one.
                             */
                            if (limits->texture_format != 0ul) {
                                map_format = limits->texture_format;
                            }
                            if (v9x_i9xx_map_format_known(map_format) ==
                                    V9X_FALSE) {
                                V9X_I9XX_REJECT(V9X_I9XX_P5_TEXTURE_STATE,
                                                index + 3ul);
                            }
                        }
                        if (stream[index + 3ul] !=
                                (map_format |
                                 ((map_height - 1ul) <<
                                  V9X_I9XX_MS3_HEIGHT_SHIFT) |
                                 ((map_width - 1ul) <<
                                  V9X_I9XX_MS3_WIDTH_SHIFT))) {
                            V9X_I9XX_REJECT(V9X_I9XX_P5_TEXTURE_STATE,
                                            index + 3ul);
                        }
                        if (stream[index + 4ul] !=
                                (((map_pitch >> 2) - 1ul) <<
                                 V9X_I9XX_MS4_PITCH_SHIFT)) {
                            V9X_I9XX_REJECT(V9X_I9XX_P5_TEXTURE_STATE,
                                            index + 4ul);
                        }
                        /*
                         * And the declared range must hold that map.
                         *
                         * Done only for a SCENE, where both operands are
                         * constants and the product is folded. A runtime
                         * stream's operands are variables, and a 32-bit
                         * multiply here calls __U4M in the default CODE
                         * segment, which a near call from I9XXCODE cannot
                         * reach - E2052 at link time. The runtime footprint is
                         * computed instead in v9x_d3d_i9xx.c, in 32-bit code
                         * where the multiply is free and overflow-checked, and
                         * refused there before this decoder runs.
                         */
                        if (limits->kind != V9X_I9XX_SCENE_RUNTIME &&
                            texture_bytes <
                                (v9x_u32)(V9X_I9XX_TEXTURE_HEIGHT *
                                          V9X_I9XX_TEXTURE_PITCH)) {
                            V9X_I9XX_REJECT(V9X_I9XX_P5_TARGET_RANGE,
                                            index + 2ul);
                        }
                    }
                    saw_map_state = V9X_TRUE;
                } else {
                    /*
                     * SS2, SS3 and SS4, each required to be the value the
                     * audit licensed.
                     *
                     * SS3 carries the MAP INDEX, and sampler n and map n are
                     * not implicitly paired - both reference emitters write
                     * the index. A sampler pointed at map 1, which this stream
                     * never declares, reads whatever map 1 held from an
                     * earlier client: a well-formed packet, one unit enabled,
                     * MAP_STATE's own address correct, and a fetch from
                     * somewhere nobody chose.
                     *
                     * SS2 is the filter and SS4 the border colour. Neither can
                     * read outside the map, but a sampler this build never
                     * configured is state from somewhere else, and a texel
                     * fetched under a filter nobody set would answer the
                     * addressing question wrongly and look like an answer.
                     */
                    {
                        /*
                         * The audited words for a scene; for a runtime
                         * stream, the words the engine declared - wrap
                         * and bilinear are each one field, and the decoder
                         * checks the engine emitted what it said rather
                         * than a third thing. A scene's limits leave both
                         * zero, so a generated stream is pinned to clamp
                         * and nearest as before.
                         */
                        v9x_u32 ss2 = V9X_I9XX_SS2_NEAREST_NO_MIP;
                        v9x_u32 mode = V9X_I9XX_TEXCOORDMODE_CLAMP_EDGE;

                        if (limits->kind == V9X_I9XX_SCENE_RUNTIME) {
                            ss2 = v9x_i9xx_sampler_filter_word(
                                limits->texture_min_linear,
                                limits->texture_mag_linear);
                            if (limits->texture_wrap != 0ul) {
                                mode = V9X_I9XX_TEXCOORDMODE_WRAP;
                            }
                        }
                        if (stream[index + 2ul] != ss2) {
                            V9X_I9XX_REJECT(V9X_I9XX_P5_TEXTURE_STATE,
                                            index + 2ul);
                        }
                        if (stream[index + 3ul] !=
                                (V9X_I9XX_SS3_NORMALIZED_COORDS |
                                 (mode << V9X_I9XX_SS3_TCX_SHIFT) |
                                 (mode << V9X_I9XX_SS3_TCY_SHIFT) |
                                 (mode << V9X_I9XX_SS3_TCZ_SHIFT) |
                                 (0ul << V9X_I9XX_SS3_MAP_INDEX_SHIFT))) {
                            V9X_I9XX_REJECT(V9X_I9XX_P5_TEXTURE_STATE,
                                            index + 3ul);
                        }
                    }
                    if (stream[index + 4ul] != V9X_I9XX_SS4_BORDER_COLOR) {
                        V9X_I9XX_REJECT(V9X_I9XX_P5_TEXTURE_STATE, index + 4ul);
                    }
                    saw_sampler_state = V9X_TRUE;
                }
                index += payload + 1ul;
            }

        } else if ((command & 0xffff0000ul) ==
                       V9X_I9XX_3DSTATE_LOAD_STATE_IMM1) {
            v9x_u32 payload = (command & 0x0000000ful) + 1ul;
            v9x_u32 s4;
            if (dword_count - index < payload + 1ul) {
                V9X_I9XX_REJECT(V9X_I9XX_P5_TRUNCATED, index);
            }
            /* Phase 5 loads exactly S2..S6 in one packet; any other selection
             * is a stream this decoder was not written for. */
            if (command != (V9X_I9XX_3DSTATE_LOAD_STATE_IMM1 |
                            V9X_I9XX_I1_LOAD_S2 | V9X_I9XX_I1_LOAD_S3 |
                            V9X_I9XX_I1_LOAD_S4 | V9X_I9XX_I1_LOAD_S5 |
                            V9X_I9XX_I1_LOAD_S6 | 4ul)) {
                V9X_I9XX_REJECT(V9X_I9XX_P5_BAD_LENGTH, index);
            }
            /*
             * S2 says which coordinate sets the vertex carries, and it must
             * match the mode exactly in BOTH directions.
             *
             * An untextured stream declaring a coordinate set would make the
             * hardware read a seventh and eighth dword from a five-dword
             * vertex; a textured one declaring none would leave the sampler
             * reading coordinate zero at every fragment, which draws a single
             * flat texel over the whole triangle and looks like a plausible
             * picture.
             */
            if (stream[index + 1ul] !=
                    ((textured != V9X_FALSE)
                         ? V9X_I9XX_S2_TEXTURED_UNIT0
                         : V9X_I9XX_S2_ALL_TEXCOORD_ABSENT)) {
                V9X_I9XX_REJECT(V9X_I9XX_P5_TEXTURE_FORBIDDEN, index + 1ul);
            }
            s4 = stream[index + 3ul];
            if (s4 != (V9X_I9XX_S4_POINT_WIDTH_ONE |
                       V9X_I9XX_S4_LINE_WIDTH_ONE |
                       V9X_I9XX_S4_CULLMODE_NONE |
                       V9X_I9XX_S4_VFMT_XYZW |
                       V9X_I9XX_S4_VFMT_COLOR)) {
                V9X_I9XX_REJECT(V9X_I9XX_P5_VERTEX_FORMAT, index + 3ul);
            }
            /*
             * S6, pinned to the kind rather than merely screened for
             * forbidden bits.
             *
             * An undepthed stream must have both enables clear, as before. A
             * depth stream must have the test enable AND the LESS function,
             * and the write enable exactly when it is the writing kind -
             * equality, not a mask, because a stream that enabled writes in
             * the testing scene would modify the depth buffer unasked and the
             * next scene's clear would erase the evidence.
             */
            {
                v9x_u32 want_s6 = V9X_I9XX_S6_PHASE5;

                if (limits->kind == V9X_I9XX_SCENE_ALPHA_TEST) {
                    want_s6 |= V9X_I9XX_S6_ALPHA_TEST_ENABLE |
                               (V9X_I9XX_COMPAREFUNC_GREATER <<
                                    V9X_I9XX_S6_ALPHA_FUNC_SHIFT) |
                               (V9X_I9XX_ALPHA_REF <<
                                    V9X_I9XX_S6_ALPHA_REF_SHIFT);
                }
                /* A runtime stream whose engine declared a factor pair:
                 * the declared codes, each one of the four, same equality. */
                if (limits->kind == V9X_I9XX_SCENE_RUNTIME &&
                    limits->blend_src != 0ul) {
                    if (v9x_i9xx_blend_factor_known(limits->blend_src) ==
                            V9X_FALSE ||
                        v9x_i9xx_blend_factor_known(limits->blend_dst) ==
                            V9X_FALSE) {
                        V9X_I9XX_REJECT(V9X_I9XX_P5_DEPTH_FORBIDDEN,
                                        index + 5ul);
                    }
                    want_s6 |= V9X_I9XX_S6_BLEND_ENABLE |
                               (V9X_I9XX_BLENDFUNC_ADD <<
                                    V9X_I9XX_S6_BLEND_FUNC_SHIFT) |
                               (limits->blend_src <<
                                    V9X_I9XX_S6_SRC_FACTOR_SHIFT) |
                               (limits->blend_dst <<
                                    V9X_I9XX_S6_DST_FACTOR_SHIFT);
                }
                if (limits->kind == V9X_I9XX_SCENE_BLEND) {
                    want_s6 |= V9X_I9XX_S6_BLEND_ENABLE |
                               (V9X_I9XX_BLENDFUNC_ADD <<
                                    V9X_I9XX_S6_BLEND_FUNC_SHIFT) |
                               (V9X_I9XX_BLENDFACT_SRC_ALPHA <<
                                    V9X_I9XX_S6_SRC_FACTOR_SHIFT) |
                               (V9X_I9XX_BLENDFACT_INV_SRC_ALPHA <<
                                    V9X_I9XX_S6_DST_FACTOR_SHIFT);
                }
                if (depthed != V9X_FALSE) {
                    want_s6 |= V9X_I9XX_S6_DEPTH_TEST_ENABLE |
                               (V9X_I9XX_COMPAREFUNC_LESS <<
                                    V9X_I9XX_S6_DEPTH_FUNC_SHIFT);
                    /*
                     * Whether writes are enabled: the scene's kind, or for a
                     * runtime stream the caller's declaration, because an
                     * application sets ZWRITEENABLE per draw and there is no
                     * kind to read it from. Still an equality - a stream that
                     * enabled writes the engine did not intend would modify
                     * the application's depth buffer unasked.
                     */
                    if (limits->kind == V9X_I9XX_SCENE_RUNTIME
                            ? (limits->depth_writes != 0ul)
                            : (v9x_i9xx_scene_kind_depth_writes(limits->kind) !=
                                   V9X_FALSE)) {
                        want_s6 |= V9X_I9XX_S6_DEPTH_WRITE_ENABLE;
                    }
                }
                if (stream[index + 5ul] != want_s6) {
                    V9X_I9XX_REJECT(V9X_I9XX_P5_DEPTH_FORBIDDEN, index + 5ul);
                }
            }
            index += payload + 1ul;

        } else if ((command & 0xffff0000ul) ==
                       V9X_I9XX_3DSTATE_PIXEL_SHADER) {
            v9x_u32 payload = (command & 0x0000fffful) + 1ul;
            if (dword_count - index < payload + 1ul) {
                V9X_I9XX_REJECT(V9X_I9XX_P5_TRUNCATED, index);
            }
            /*
             * Each program is a constant, and only the one this mode's
             * program actually is may appear. Lengths rather than contents,
             * as before - but the two differ, so a textured stream carrying
             * the untextured program is caught here, and that program would
             * write the interpolated vertex colour and never sample at all.
             */
            {
                v9x_u32 want;

                if (limits->kind == V9X_I9XX_SCENE_RUNTIME &&
                    textured != V9X_FALSE) {
                    /* The program the engine declared, by length: zero is
                     * the original modulate, the two alpha-keeping forms are
                     * three dwords longer; an unknown declaration has extent
                     * zero and matches nothing. */
                    want = v9x_i9xx_texture_program_extent(
                               limits->texture_program) - 1ul;
                } else if (limits->kind == V9X_I9XX_SCENE_MODULATED) {
                    /*
                     * A runtime textured draw MODULATES: the texel by the
                     * interpolated vertex colour, which is what
                     * D3DTBLEND_MODULATE means and the only texture blend
                     * this engine publishes. The sampling program below
                     * writes the texel alone and is scene 1's, which measured
                     * addressing rather than blending - a runtime draw
                     * carrying it would ignore the vertex colour and look
                     * like a lighting bug.
                     */
                    want = v9x_i9xx_modulate_program_extent() - 1ul;
                } else if (textured != V9X_FALSE) {
                    want = v9x_i9xx_sampling_program_extent() - 1ul;
                } else {
                    want = v9x_i9xx_fragment_program_extent() - 1ul;
                }
                if (payload != want) {
                    V9X_I9XX_REJECT(V9X_I9XX_P5_SHADER, index);
                }
            }
            saw_shader = V9X_TRUE;
            index += payload + 1ul;

        } else if (command == (V9X_I9XX_3DSTATE_AA |
                               V9X_I9XX_AA_LINE_ECAAR_WIDTH_EN |
                               V9X_I9XX_AA_LINE_ECAAR_WIDTH_1_0 |
                               V9X_I9XX_AA_LINE_REGION_WIDTH_EN |
                               V9X_I9XX_AA_LINE_REGION_WID_1_0) ||
                   command == (V9X_I9XX_3DSTATE_COORD_SET_BIND |
                               V9X_I9XX_CSB_TCB_IDENTITY) ||
                   command == V9X_I9XX_3DSTATE_DEPTH_SUBRECT_DISABLE) {
            /* Single-dword invariant packets, accepted only in the exact form
             * i9xx_3d.c emits. Audit section 8. */
            index += 1ul;

        } else if (command == V9X_I9XX_3DSTATE_DFLT_DIFFUSE ||
                   command == V9X_I9XX_3DSTATE_DFLT_SPEC ||
                   command == V9X_I9XX_3DSTATE_DFLT_Z) {
            if (dword_count - index < 2ul) {
                V9X_I9XX_REJECT(V9X_I9XX_P5_TRUNCATED, index);
            }
            /* All three defaults are set to zero, as both drivers do. A
             * non-zero default diffuse in particular would be a second,
             * competing source of colour. */
            if (stream[index + 1ul] != 0ul) {
                V9X_I9XX_REJECT(V9X_I9XX_P5_BAD_LENGTH, index + 1ul);
            }
            index += 2ul;

        } else if (command == V9X_I9XX_XY_COLOR_BLT) {
            /*
             * The GPU filling its own render target. The CPU does not
             * write this memory at all - that is a condition of the errata
             * gate opening, because 600 KiB of CPU writes immediately
             * before the GPU reads adjacent memory is the closest thing in
             * this design to erratum 12's own description of its trigger.
             *
             * The bounds arithmetic is delegated to the Phase 4 decoder
             * rather than reimplemented: it is the same packet, and a
             * second copy of that check is a second thing to get wrong.
             * Bounded by the TARGET here, not the scratch page.
             */
            if (dword_count - index < 6ul) {
                V9X_I9XX_REJECT(V9X_I9XX_P5_TRUNCATED, index);
            }
            if (v9x_i9xx_decode_phase4_stream(stream + index, 6ul,
                                              target_offset,
                                              target_bytes) ==
                    V9X_STATUS_OK) {
                /* And it must fill with the agreed background, or "the
                 * triangle drew" stops being distinguishable from "that
                 * memory already looked like this". */
                if (stream[index + 5ul] != V9X_I9XX_FILL_DWORD) {
                    V9X_I9XX_REJECT(V9X_I9XX_P5_FORMAT, index + 5ul);
                }
                saw_fill = V9X_TRUE;
            } else if (textured != V9X_FALSE &&
                       v9x_i9xx_decode_phase4_stream(stream + index, 6ul,
                                                     texture_offset,
                                                     texture_bytes) ==
                           V9X_STATUS_OK) {
                /*
                 * A quadrant paint, bounded by the texture range so a quadrant
                 * whose address arithmetic went wrong is refused here rather
                 * than overwriting a page that is not ours.
                 *
                 * And the four of them must TILE the texture, for the same
                 * reason the depth clear must cover its buffer: a paint that
                 * covered one quadrant would leave three holding whatever the
                 * page held, and under a nearest filter that is a picture -
                 * just not one of this build's making. Each is required to be
                 * one of the four expected rectangles, and the completeness
                 * check below requires all four.
                 *
                 * No colour check: the four quadrant colours are the
                 * experiment's variable, and a decoder asserting them would be
                 * asserting the answer.
                 */
                if (stream[index + 1ul] !=
                        (V9X_I9XX_BLT_DEPTH_32 | V9X_I9XX_BLT_ROP_PATCOPY |
                         V9X_I9XX_TEXTURE_PITCH)) {
                    V9X_I9XX_REJECT(V9X_I9XX_P5_PITCH, index + 1ul);
                }
                if (stream[index + 3ul] !=
                        ((V9X_I9XX_TEXTURE_BLOCK << 16) |
                         (V9X_I9XX_TEXTURE_BLOCK / 2ul))) {
                    V9X_I9XX_REJECT(V9X_I9XX_P5_TARGET_RANGE, index + 3ul);
                }
                {
                    v9x_u32 quadrant;
                    v9x_u16 known = V9X_FALSE;

                    for (quadrant = 0ul; quadrant < 4ul; ++quadrant) {
                        if (stream[index + 4ul] ==
                                texture_offset +
                                v9x_i9xx_decode_quadrant_offset(quadrant)) {
                            /* Each exactly once: two paints of one quadrant
                             * satisfy a count and leave another unpainted. */
                            if ((painted & (1ul << quadrant)) != 0ul) {
                                V9X_I9XX_REJECT(V9X_I9XX_P5_TARGET_RANGE,
                                                index + 4ul);
                            }
                            painted |= (1ul << quadrant);
                            known = V9X_TRUE;
                        }
                    }
                    if (known == V9X_FALSE) {
                        V9X_I9XX_REJECT(V9X_I9XX_P5_TARGET_RANGE, index + 4ul);
                    }
                }
                saw_texture_paint = V9X_TRUE;
            } else if (depthed != V9X_FALSE &&
                       v9x_i9xx_decode_phase4_stream(stream + index, 6ul,
                                                     depth_offset,
                                                     depth_bytes) ==
                           V9X_STATUS_OK) {
                /*
                 * The depth CLEAR. It must clear to FAR, and it must clear the
                 * WHOLE buffer.
                 *
                 * Being inside the depth range is not enough, which is what
                 * this checked until 2026-09-16: a one-row blit is inside the
                 * range, carries the right value and sits before the draw, and
                 * leaves 255 of the 256 rows holding whatever the previous
                 * scene left. The triangles then test against memory nobody
                 * cleared - the exact condition the clear exists to remove,
                 * reached through a clear that is present and correct.
                 *
                 * So the rectangle, the pitch and the destination are each
                 * required to be the whole buffer rather than merely to fit
                 * in it. Exact rather than "at least", because a blit larger
                 * than the buffer would not fit and one at a different pitch
                 * walks a different grid over the same bytes.
                 */
                if (stream[index + 5ul] != V9X_I9XX_DEPTH_CLEAR_DWORD) {
                    V9X_I9XX_REJECT(V9X_I9XX_P5_FORMAT, index + 5ul);
                }
                if (stream[index + 1ul] !=
                        (V9X_I9XX_BLT_DEPTH_32 | V9X_I9XX_BLT_ROP_PATCOPY |
                         V9X_I9XX_DEPTH_PITCH)) {
                    V9X_I9XX_REJECT(V9X_I9XX_P5_PITCH, index + 1ul);
                }
                /* The width counts DWORDS and the buffer is 16-bit, so a full
                 * row is the pitch over four. */
                if (stream[index + 3ul] !=
                        ((V9X_I9XX_DEPTH_HEIGHT << 16) |
                         (V9X_I9XX_DEPTH_PITCH / 4ul))) {
                    V9X_I9XX_REJECT(V9X_I9XX_P5_TARGET_RANGE, index + 3ul);
                }
                if (stream[index + 4ul] != depth_offset) {
                    V9X_I9XX_REJECT(V9X_I9XX_P5_TARGET_RANGE, index + 4ul);
                }
                saw_depth_clear = V9X_TRUE;
            } else {
                V9X_I9XX_REJECT(V9X_I9XX_P5_TARGET_RANGE, index);
            }
            /*
             * The texture and the depth buffer are both painted by the GPU for
             * the same reason the target is: a CPU upload through the aperture
             * is the access the errata gate was opened on condition of
             * avoiding. So these blits are part of every textured or depth
             * stream, and the allowlist has to name them.
             */
            index += 6ul;

        } else if (command == V9X_I9XX_IAB_DISABLE_DWORD) {
            /*
             * The independent-alpha-blend DISABLE, and only that exact dword.
             *
             * Accepted in a blend stream and refused in every other, on the
             * same terms as the texture packets: a stream that carries it
             * without enabling blending is a stream doing something nobody
             * asked for, and one that enables blending without it leaves the
             * alpha channel blended by whatever the engine last had.
             *
             * The whole dword is compared rather than the opcode, because the
             * MODIFY bits are what make it a disable. A packet with only
             * IAB_MODIFY_ENABLE set would leave the factors untouched and
             * would pass an opcode check.
             */
            if (limits->kind != V9X_I9XX_SCENE_BLEND &&
                !(limits->kind == V9X_I9XX_SCENE_RUNTIME &&
                  limits->blend_src != 0ul)) {
                V9X_I9XX_REJECT(V9X_I9XX_P5_TEXTURE_STATE, index);
            }
            saw_iab_disable = V9X_TRUE;
            index += 1ul;

        } else if ((command & 0xff000000ul) == V9X_I9XX_3DSTATE_IAB) {
            /* Any OTHER form of the packet, including one that enables it. */
            V9X_I9XX_REJECT(V9X_I9XX_P5_TEXTURE_STATE, index);

        } else if (command == V9X_I9XX_MI_NOOP ||
                   command == V9X_I9XX_MI_FLUSH) {
            index += 1ul;

        } else if ((command & 0xff000000ul) ==
                       V9X_I9XX_3DPRIMITIVE_INLINE) {
            v9x_u32 payload = (command & 0x0003fffful) + 1ul;
            v9x_u32 vertex;
            v9x_u32 base;
            v9x_u32 stride;
            v9x_u32 vertices;
            if ((command & 0x00800000ul) != 0ul) {
                /* Bit 23 set is the indirect form, which would fetch vertices
                 * from a buffer this phase does not allocate. */
                V9X_I9XX_REJECT(V9X_I9XX_P5_INDIRECT_FORBIDDEN, index);
            }
            if (((command >> 18) & 0x1ful) != V9X_I9XX_PRIM3D_TRILIST) {
                V9X_I9XX_REJECT(V9X_I9XX_P5_BAD_OPCODE, index);
            }
            /*
             * The buffers this draw reads must already have been PREPARED.
             *
             * Checked here rather than at the end of the stream, and the
             * difference is not pedantic: a depth clear after the draw erases
             * the result it was meant to make meaningful, and a texture paint
             * after the draw samples whatever the page held from the last
             * boot. Both are present, complete, correctly addressed and
             * useless, and an end-of-stream presence check accepts both.
             *
             * The fill is already required this way for the same reason - it
             * is what makes "the triangle drew" distinguishable from "that
             * memory already looked like this".
             */
            /*
             * A scene must have FILLED before it draws, or "the triangle
             * drew" is indistinguishable from "that memory already looked
             * like this". A runtime stream must not: the surface belongs to
             * the application, which decides when it is cleared, and a driver
             * that filled it every draw would erase the frame it is drawing.
             */
            if (limits->kind != V9X_I9XX_SCENE_RUNTIME &&
                saw_fill == V9X_FALSE) {
                V9X_I9XX_REJECT(V9X_I9XX_P5_MISSING_PACKET, index);
            }
            /*
             * A SCENE must have painted its texture and cleared its depth
             * buffer before it draws, for the same reason it must have filled
             * the target: otherwise the result depends on what that memory
             * already held.
             *
             * A RUNTIME stream must not do either. Both surfaces belong to the
             * application - it uploads its own texels and clears its own depth
             * buffer when it decides to - and a driver that painted over a
             * texture or cleared a depth buffer on every draw would destroy
             * the frame it was asked to render.
             *
             * The depth BUF_INFO is still required in both, because that is
             * not a precondition about memory contents: it is the binding,
             * and a depth-enabled stream without it tests against whatever
             * buffer the engine last had.
             */
            if (limits->kind != V9X_I9XX_SCENE_RUNTIME) {
                if (textured != V9X_FALSE &&
                    (saw_texture_paint == V9X_FALSE || painted != 0x0ful)) {
                    V9X_I9XX_REJECT(V9X_I9XX_P5_MISSING_PACKET, index);
                }
                if (depthed != V9X_FALSE && saw_depth_clear == V9X_FALSE) {
                    V9X_I9XX_REJECT(V9X_I9XX_P5_MISSING_PACKET, index);
                }
            }
            if (depthed != V9X_FALSE && saw_depth_buf_info == V9X_FALSE) {
                V9X_I9XX_REJECT(V9X_I9XX_P5_MISSING_PACKET, index);
            }
            /* A blend draw without the IAB disable would blend the alpha
             * channel with factors nobody set. */
            if ((limits->kind == V9X_I9XX_SCENE_BLEND ||
                 (limits->kind == V9X_I9XX_SCENE_RUNTIME &&
                  limits->blend_src != 0ul)) &&
                saw_iab_disable == V9X_FALSE) {
                V9X_I9XX_REJECT(V9X_I9XX_P5_MISSING_PACKET, index);
            }
            /*
             * The stride follows the mode, and so does the payload. A textured
             * vertex is seven dwords, not five: the same three vertices make a
             * 21-dword payload rather than 15, and a stream whose S2 declares
             * a coordinate set while its vertices are five dwords long would
             * have the hardware read two dwords of the NEXT vertex as this
             * one's coordinates.
             */
            stride = (textured != V9X_FALSE) ? V9X_I9XX_TEXTURED_VERTEX_DWORDS
                                             : V9X_I9XX_VERTEX_DWORDS;
            /*
             * Vertices: three per triangle, and a depth scene draws three
             * triangles where every other kind draws one. Bounded by the
             * scene table's own maximum rather than fixed at three.
             */
            if (limits->kind == V9X_I9XX_SCENE_RUNTIME) {
                /*
                 * The count comes from the PAYLOAD, because nobody knows it in
                 * advance. Derived by division rather than trusted: a payload
                 * that is not a whole number of vertices is a stream whose
                 * last vertex is short, and the loop below would read past
                 * what was submitted.
                 */
                /*
                 * SIXTEEN-BIT arithmetic, and not as an optimisation.
                 *
                 * A 32-bit divide here is a __U4D call into the default CODE
                 * segment, which a near call out of I9XXCODE cannot reach -
                 * the linker refuses it with E2052, which is how this was
                 * found. The same constraint the triangle run and the texture
                 * quadrants already work under.
                 *
                 * The bound is what makes the narrowing safe rather than a
                 * hope: a payload that does not fit sixteen bits is refused
                 * before anything is divided, and the largest legal runtime
                 * payload is 1024 triangles of three seven-dword vertices,
                 * which is 21504.
                 */
                if (stride == 0ul || payload > 0xfffful) {
                    V9X_I9XX_REJECT(V9X_I9XX_P5_VERTEX_COUNT, index);
                }
                {
                    v9x_u16 payload16 = (v9x_u16)payload;
                    v9x_u16 stride16 = (v9x_u16)stride;
                    v9x_u16 count16;

                    if ((v9x_u16)(payload16 % stride16) != 0u) {
                        V9X_I9XX_REJECT(V9X_I9XX_P5_VERTEX_COUNT, index);
                    }
                    count16 = (v9x_u16)(payload16 / stride16);
                    if (count16 == 0u ||
                        (v9x_u16)(count16 %
                                  (v9x_u16)V9X_I9XX_VERTEX_COUNT) != 0u) {
                        /* A triangle list is three vertices at a time. */
                        V9X_I9XX_REJECT(V9X_I9XX_P5_VERTEX_COUNT, index);
                    }
                    if ((v9x_u32)(count16 /
                                  (v9x_u16)V9X_I9XX_VERTEX_COUNT) >
                            V9X_I9XX_RUNTIME_TRIANGLES_MAX) {
                        V9X_I9XX_REJECT(V9X_I9XX_P5_VERTEX_COUNT, index);
                    }
                    vertices = (v9x_u32)count16;
                }
            } else {
                vertices = (v9x_u32)((v9x_u16)V9X_I9XX_VERTEX_COUNT *
                                     (v9x_u16)v9x_i9xx_decode_triangles(
                                         limits->kind));
                if (payload !=
                        (v9x_u32)((v9x_u16)vertices * (v9x_u16)stride)) {
                    V9X_I9XX_REJECT(V9X_I9XX_P5_VERTEX_COUNT, index);
                }
            }
            if (dword_count - index < payload + 1ul) {
                V9X_I9XX_REJECT(V9X_I9XX_P5_TRUNCATED, index);
            }
            /*
             * base advances by a stride rather than being computed as
             * vertex * V9X_I9XX_VERTEX_DWORDS. The stride is five, not a power
             * of two, so the multiply would be a call to Open Watcom's __U4M
             * helper - which lives in clibc.lib's _TEXT and cannot be reached
             * by a near call from this segment. The linker refuses it (E2052),
             * which is how this was found.
             */
            base = index + 1ul;
            for (vertex = 0ul; vertex < vertices; ++vertex) {
                v9x_u32 decoded = 0ul;
                /*
                 * Decode the coordinates back through the float transport and
                 * bounds-check the integers. This is the check that makes the
                 * capture's redundant "raw bits and decoded integers" pairing
                 * meaningful: if the two disagree, this refuses.
                 */
                /*
                 * A RUNTIME coordinate is a float and need not be a whole
                 * pixel - a triangle at x = 160.25 is ordinary geometry and
                 * the integer decode below refuses it outright. So the two
                 * kinds are checked differently, and neither check is the
                 * other's approximation:
                 *
                 *  - a scene's coordinates are whole pixels, and decoding
                 *    them back through the float transport is what makes the
                 *    capture's "raw bits and decoded integers" pairing mean
                 *    something. If the two disagree the transport is wrong.
                 *  - a runtime coordinate is checked as a MAGNITUDE against
                 *    the rectangle, by the same predicate the builder uses.
                 *    There is nothing to cross-check it against, because
                 *    nobody wrote it down in two forms.
                 */
                if (limits->kind == V9X_I9XX_SCENE_RUNTIME) {
                    if (v9x_i9xx_float_in_range(stream[base],
                                                width_bits) == V9X_FALSE) {
                        V9X_I9XX_REJECT(V9X_I9XX_P5_VERTEX_RANGE, base);
                    }
                } else if (v9x_i9xx_float_to_int(stream[base], &decoded) !=
                               V9X_I9XX_FLOAT_OK ||
                           decoded >= limits->target_width) {
                    V9X_I9XX_REJECT(V9X_I9XX_P5_VERTEX_RANGE, base);
                }
                /*
                 * Y, against the DEPTH buffer's height in a depth stream and
                 * the target's otherwise.
                 *
                 * The depth buffer is 256 rows where the target is 480,
                 * because that is what the reserve had left. A vertex at
                 * y = 400 is inside the drawing rectangle and outside the
                 * depth allocation, and the hardware would address depth
                 * memory past the end of it - the first thing past the end
                 * being the guard page.
                 *
                 * The builder already refused this and the decoder did not,
                 * which is the whole point of the decoder existing: it is
                 * meant to be the second opinion, and a second opinion that
                 * only checks what the first one checks is not one.
                 */
                if (limits->kind == V9X_I9XX_SCENE_RUNTIME) {
                    if (v9x_i9xx_float_in_range(stream[base + 1ul],
                                                height_bits) == V9X_FALSE) {
                        V9X_I9XX_REJECT(V9X_I9XX_P5_VERTEX_RANGE, base + 1ul);
                    }
                } else if (v9x_i9xx_float_to_int(stream[base + 1ul],
                                                 &decoded) !=
                               V9X_I9XX_FLOAT_OK ||
                           decoded >= ((depthed != V9X_FALSE)
                                           ? V9X_I9XX_DEPTH_HEIGHT
                                           : limits->target_height)) {
                    V9X_I9XX_REJECT(V9X_I9XX_P5_VERTEX_RANGE, base + 1ul);
                }
                /*
                 * Z. Zero in an undepthed stream - there is nothing for a
                 * depth to mean - and a fraction in [0, 1] in a depth one,
                 * where it is the whole point.
                 *
                 * The fraction is checked as a bit pattern, not decoded: the
                 * driver's float helper takes integers and cannot express a
                 * depth between the planes. What is required is that the
                 * pattern is a positive finite float no greater than 1.0f,
                 * which the ordering of IEEE-754 magnitudes makes a single
                 * comparison - anything above 0x3f800000 is either greater
                 * than one, infinite or a NaN, and a negative Z has the sign
                 * bit set and exceeds it too.
                 */
                if (depthed != V9X_FALSE ||
                    limits->kind == V9X_I9XX_SCENE_RUNTIME) {
                    if (v9x_i9xx_float_in_range(stream[base + 2ul],
                                                0x3f800000ul) == V9X_FALSE) {
                        V9X_I9XX_REJECT(V9X_I9XX_P5_VERTEX_RANGE, base + 2ul);
                    }
                } else if (v9x_i9xx_float_to_int(stream[base + 2ul],
                                                 &decoded) !=
                               V9X_I9XX_FLOAT_OK || decoded != 0ul) {
                    V9X_I9XX_REJECT(V9X_I9XX_P5_VERTEX_RANGE, base + 2ul);
                }
                /*
                 * RHW. Exactly one in a generated scene, because the tables
                 * this build emits carry one and a stream that does not is
                 * not the stream that was audited. Any legal reciprocal in a
                 * runtime stream, because that value is the application's -
                 * see v9x_i9xx_float_positive_finite for what makes one legal
                 * and why demanding 1.0f here refused real geometry.
                 *
                 * The two kinds are separated deliberately rather than
                 * relaxed together: widening the scene rule would weaken the
                 * one check that pins the diagnostic streams to their CRCs.
                 */
                if (limits->kind == V9X_I9XX_SCENE_RUNTIME) {
                    if (v9x_i9xx_float_positive_finite(stream[base + 3ul]) ==
                            V9X_FALSE) {
                        V9X_I9XX_REJECT(V9X_I9XX_P5_VERTEX_RANGE, base + 3ul);
                    }
                } else if (v9x_i9xx_float_to_int(stream[base + 3ul],
                                                 &decoded) !=
                               V9X_I9XX_FLOAT_OK || decoded != 1ul) {
                    V9X_I9XX_REJECT(V9X_I9XX_P5_VERTEX_RANGE, base + 3ul);
                }
                /* Uniform colour, which is what makes shading mode moot.
                 * The textured stream's colour is unread by the sampling
                 * program but must still be the one this build emits. */
                /* A runtime vertex's colour is the application's. Every
                 * 32-bit value is a legal colour, and asserting one would be
                 * asserting what may be drawn. */
                if (limits->kind != V9X_I9XX_SCENE_RUNTIME &&
                    v9x_i9xx_decode_vertex_color(limits->kind,
                                                 stream[base + 4ul]) ==
                        V9X_FALSE) {
                    V9X_I9XX_REJECT(V9X_I9XX_P5_VERTEX_FORMAT, base + 4ul);
                }
                if (textured != V9X_FALSE) {
                    /*
                     * The coordinates, checked as bit patterns.
                     *
                     * Only 0, 1/2 and 1 are ever emitted - the triangle's
                     * corners in its own bounding box - and float_to_int
                     * cannot express a half, so this is the one place where
                     * an exact pattern is the check rather than a decode.
                     * Anything else is a coordinate nobody derived, and the
                     * clamp-to-edge sampler would turn it into a plausible
                     * texel instead of an error.
                     */
                    if (limits->kind == V9X_I9XX_SCENE_RUNTIME) {
                        /*
                         * An application's coordinates, so any finite value.
                         *
                         * The scene rule below - exactly 0, 1/2 or 1 - is
                         * right for a stream this build generated and wrong
                         * for one it did not: the sampler NORMALIZES, so two
                         * is the far edge of the second tile and a negative
                         * one is the tile to the left, and both are ordinary
                         * things for an application to ask for. What is still
                         * refused is an infinity or a NaN, which name no place
                         * on a texture and leave the interpolator walking a
                         * span nobody can predict.
                         */
                        if (v9x_i9xx_float_finite(stream[base + 5ul]) ==
                                V9X_FALSE) {
                            V9X_I9XX_REJECT(V9X_I9XX_P5_VERTEX_RANGE,
                                            base + 5ul);
                        }
                        if (v9x_i9xx_float_finite(stream[base + 6ul]) ==
                                V9X_FALSE) {
                            V9X_I9XX_REJECT(V9X_I9XX_P5_VERTEX_RANGE,
                                            base + 6ul);
                        }
                    } else {
                        if (v9x_i9xx_normalized_half(stream[base + 5ul]) ==
                                V9X_FALSE) {
                            V9X_I9XX_REJECT(V9X_I9XX_P5_VERTEX_RANGE,
                                            base + 5ul);
                        }
                        if (v9x_i9xx_normalized_half(stream[base + 6ul]) ==
                                V9X_FALSE) {
                            V9X_I9XX_REJECT(V9X_I9XX_P5_VERTEX_RANGE,
                                            base + 6ul);
                        }
                    }
                }
                base += stride;
            }
            saw_primitive = V9X_TRUE;
            index += payload + 1ul;

        } else {
            V9X_I9XX_REJECT(V9X_I9XX_P5_BAD_OPCODE, index);
        }
    }

    /*
     * Presence, not just absence of the forbidden. A stream missing its target
     * description would otherwise decode clean and draw into whatever the
     * engine last had configured.
     */
    if (saw_buf_info_color == V9X_FALSE || saw_dst_buf_vars == V9X_FALSE ||
        saw_draw_rect == V9X_FALSE || saw_scissor_disable == V9X_FALSE ||
        saw_indirect_disable == V9X_FALSE || saw_shader == V9X_FALSE ||
        saw_primitive == V9X_FALSE) {
        V9X_I9XX_REJECT(V9X_I9XX_P5_MISSING_PACKET, dword_count);
    }
    if (limits->kind != V9X_I9XX_SCENE_RUNTIME && saw_fill == V9X_FALSE) {
        V9X_I9XX_REJECT(V9X_I9XX_P5_MISSING_PACKET, dword_count);
    }
    /*
     * A textured stream must carry BOTH texture packets. S2 having declared a
     * coordinate set is not enough: without MAP_STATE the sampler reads
     * whatever map the engine last had, which is another client's memory or
     * none, and the draw would sample it without complaint.
     */
    if (textured != V9X_FALSE &&
        (saw_map_state == V9X_FALSE || saw_sampler_state == V9X_FALSE)) {
        V9X_I9XX_REJECT(V9X_I9XX_P5_MISSING_PACKET, dword_count);
    }
    /*
     * And a depth stream must BIND one. Without the BUF_INFO the S6 enables
     * point the hardware at whatever depth buffer the engine last had, which
     * is another client's memory or none - and the draw would test against it
     * without complaint.
     */
    if (depthed != V9X_FALSE && saw_depth_buf_info == V9X_FALSE) {
        V9X_I9XX_REJECT(V9X_I9XX_P5_MISSING_PACKET, dword_count);
    }
    /*
     * The CLEAR is a scene's obligation and not a runtime stream's, for the
     * reason given at the draw: the application owns the buffer and clears it
     * when it decides to, and a driver clearing it every draw would erase the
     * frame. The BINDING above is required in both, because that is not about
     * what the memory holds.
     */
    if (depthed != V9X_FALSE && limits->kind != V9X_I9XX_SCENE_RUNTIME &&
        saw_depth_clear == V9X_FALSE) {
        V9X_I9XX_REJECT(V9X_I9XX_P5_MISSING_PACKET, dword_count);
    }
    /*
     * A textured stream must also PAINT its texture. Without the blits the
     * sampler reads a page holding whatever the last boot left, which under a
     * nearest filter is a picture - just not one of this build's making.
     */
    if (textured != V9X_FALSE && limits->kind != V9X_I9XX_SCENE_RUNTIME &&
        (saw_texture_paint == V9X_FALSE || painted != 0x0ful)) {
        V9X_I9XX_REJECT(V9X_I9XX_P5_MISSING_PACKET, dword_count);
    }
    if (limits->kind == V9X_I9XX_SCENE_BLEND &&
        saw_iab_disable == V9X_FALSE) {
        V9X_I9XX_REJECT(V9X_I9XX_P5_MISSING_PACKET, dword_count);
    }
    return V9X_I9XX_P5_OK;
}
