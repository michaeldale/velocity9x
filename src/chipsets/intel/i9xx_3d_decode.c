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

v9x_u16 v9x_i9xx_decode_phase5_stream(
    const v9x_u32 *stream, v9x_u32 dword_count,
    v9x_u32 target_offset, v9x_u32 target_bytes,
    v9x_u32 *rejected_index)
{
    v9x_u32 index = 0ul;
    v9x_u32 target_end;
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
    if (stream == 0 || dword_count == 0ul || target_bytes == 0ul ||
        target_offset > 0xfffffffful - target_bytes) {
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
                 * ANY depth BUF_INFO is now a refusal, address zero included.
                 *
                 * This used to accept one whose address was zero, on the
                 * reading that a declared-but-unreferenced depth buffer is
                 * what the reference path emits. It is not: Mesa emits none
                 * when there is no depth buffer, and address zero is inside
                 * the aperture and is not ours.
                 *
                 * The stream no longer contains one, so this is the decoder
                 * enforcing that rather than permitting it - which is the
                 * whole point of an allowlist. When Phase 6 adds a real depth
                 * buffer, this becomes a check that its address is inside the
                 * reserve, not a relaxation back to accepting zero.
                 *
                 * docs\issues\2026-09-15-intel-depth-buf-info-at-address-zero.md
                 */
                V9X_I9XX_REJECT(V9X_I9XX_P5_DEPTH_FORBIDDEN, index + 1ul);
            }
            if ((identity & V9X_I9XX_BUF_3D_ID_COLOR_BACK) !=
                    V9X_I9XX_BUF_3D_ID_COLOR_BACK) {
                V9X_I9XX_REJECT(V9X_I9XX_P5_BAD_OPCODE, index + 1ul);
            }
            if ((identity & V9X_I9XX_BUF_3D_PITCH_MASK) !=
                    (V9X_I9XX_TARGET_PITCH & V9X_I9XX_BUF_3D_PITCH_MASK)) {
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
                    (((V9X_I9XX_TARGET_HEIGHT - 1ul) << 16) |
                     (V9X_I9XX_TARGET_WIDTH - 1ul))) {
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
            V9X_I9XX_REJECT(V9X_I9XX_P5_TEXTURE_FORBIDDEN, index);

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
            /* S2 must declare every texture coordinate absent. */
            if (stream[index + 1ul] != V9X_I9XX_S2_ALL_TEXCOORD_ABSENT) {
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
            /* S6's depth enables must be clear: Phase 5 is un-Z'd and the
             * depth buffer it declares has no memory behind it. */
            if ((stream[index + 5ul] & (V9X_I9XX_S6_DEPTH_TEST_ENABLE |
                                        V9X_I9XX_S6_DEPTH_WRITE_ENABLE)) !=
                    0ul) {
                V9X_I9XX_REJECT(V9X_I9XX_P5_DEPTH_FORBIDDEN, index + 5ul);
            }
            index += payload + 1ul;

        } else if ((command & 0xffff0000ul) ==
                       V9X_I9XX_3DSTATE_PIXEL_SHADER) {
            v9x_u32 payload = (command & 0x0000fffful) + 1ul;
            if (dword_count - index < payload + 1ul) {
                V9X_I9XX_REJECT(V9X_I9XX_P5_TRUNCATED, index);
            }
            /* The program is a constant; only its exact length is accepted. */
            if (payload != v9x_i9xx_fragment_program_extent() - 1ul) {
                V9X_I9XX_REJECT(V9X_I9XX_P5_SHADER, index);
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
                                              target_bytes) !=
                    V9X_STATUS_OK) {
                V9X_I9XX_REJECT(V9X_I9XX_P5_TARGET_RANGE, index);
            }
            /* And it must fill with the agreed background, or "the
             * triangle drew" stops being distinguishable from "that
             * memory already looked like this". */
            if (stream[index + 5ul] != V9X_I9XX_FILL_DWORD) {
                V9X_I9XX_REJECT(V9X_I9XX_P5_FORMAT, index + 5ul);
            }
            saw_fill = V9X_TRUE;
            index += 6ul;

        } else if (command == V9X_I9XX_MI_NOOP ||
                   command == V9X_I9XX_MI_FLUSH) {
            index += 1ul;

        } else if ((command & 0xff000000ul) ==
                       V9X_I9XX_3DPRIMITIVE_INLINE) {
            v9x_u32 payload = (command & 0x0003fffful) + 1ul;
            v9x_u32 vertex;
            v9x_u32 base;
            if ((command & 0x00800000ul) != 0ul) {
                /* Bit 23 set is the indirect form, which would fetch vertices
                 * from a buffer this phase does not allocate. */
                V9X_I9XX_REJECT(V9X_I9XX_P5_INDIRECT_FORBIDDEN, index);
            }
            if (((command >> 18) & 0x1ful) != V9X_I9XX_PRIM3D_TRILIST) {
                V9X_I9XX_REJECT(V9X_I9XX_P5_BAD_OPCODE, index);
            }
            if (payload != V9X_I9XX_VERTEX_COUNT * V9X_I9XX_VERTEX_DWORDS) {
                V9X_I9XX_REJECT(V9X_I9XX_P5_VERTEX_COUNT, index);
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
            for (vertex = 0ul; vertex < V9X_I9XX_VERTEX_COUNT; ++vertex) {
                v9x_u32 decoded = 0ul;
                /*
                 * Decode the coordinates back through the float transport and
                 * bounds-check the integers. This is the check that makes the
                 * capture's redundant "raw bits and decoded integers" pairing
                 * meaningful: if the two disagree, this refuses.
                 */
                if (v9x_i9xx_float_to_int(stream[base], &decoded) !=
                        V9X_I9XX_FLOAT_OK ||
                    decoded >= V9X_I9XX_TARGET_WIDTH) {
                    V9X_I9XX_REJECT(V9X_I9XX_P5_VERTEX_RANGE, base);
                }
                if (v9x_i9xx_float_to_int(stream[base + 1ul], &decoded) !=
                        V9X_I9XX_FLOAT_OK ||
                    decoded >= V9X_I9XX_TARGET_HEIGHT) {
                    V9X_I9XX_REJECT(V9X_I9XX_P5_VERTEX_RANGE, base + 1ul);
                }
                /* Z must be zero and W exactly one. */
                if (v9x_i9xx_float_to_int(stream[base + 2ul], &decoded) !=
                        V9X_I9XX_FLOAT_OK || decoded != 0ul) {
                    V9X_I9XX_REJECT(V9X_I9XX_P5_VERTEX_RANGE, base + 2ul);
                }
                if (v9x_i9xx_float_to_int(stream[base + 3ul], &decoded) !=
                        V9X_I9XX_FLOAT_OK || decoded != 1ul) {
                    V9X_I9XX_REJECT(V9X_I9XX_P5_VERTEX_RANGE, base + 3ul);
                }
                /* Uniform colour, which is what makes shading mode moot. */
                if (stream[base + 4ul] != V9X_I9XX_TRI_COLOR_BGRA) {
                    V9X_I9XX_REJECT(V9X_I9XX_P5_VERTEX_FORMAT, base + 4ul);
                }
                base += V9X_I9XX_VERTEX_DWORDS;
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
        saw_primitive == V9X_FALSE || saw_fill == V9X_FALSE) {
        V9X_I9XX_REJECT(V9X_I9XX_P5_MISSING_PACKET, dword_count);
    }
    return V9X_I9XX_P5_OK;
}
