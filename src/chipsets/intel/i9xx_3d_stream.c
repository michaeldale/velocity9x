/*
 * Phase 5 stream assembly, and the single source of truth for its parameters.
 *
 * The whole stream is one contiguous run in the reserve: state packets, the
 * fragment program, a two-dword MI probe, then the inline primitive with its
 * vertices. There is no vertex buffer and no second allocation, because an
 * inline primitive needs neither
 * (docs\decisions\2026-09-14-intel-gen3-3d-packet-audit.md section 7).
 *
 * v9x_i9xx_phase5_parameters exists so the generator, the mini-VDD's arm
 * table, the validators and the capture writer all read one set of numbers.
 * Phase 4's equivalent was four hand-maintained copies and the layout move at
 * step 2 found three of them stale.
 */
#include "velocity9x/intel_gen3_3d.h"
#include "velocity9x/intel_gma.h"

/*
 * The probe sits immediately before the 3D stream and is worth its two dwords:
 * it is what separates "the ring is dead" from "the packets are wrong". If the
 * probe drains and the 3D stream does not, the ring is fine and the packets
 * are the problem - which is the reproduce-once-then-kill case.
 */
#define V9X_I9XX_P5_PROBE_DWORDS  2ul
/*
 * The GPU fills its own render target: an XY_COLOR_BLT of six dwords and
 * one MI_FLUSH, at the head of the stream.
 *
 * Phase 4 executed exactly this packet on this machine on 2026-09-14, so
 * it is the one GPU operation this hardware is known to perform correctly
 * under this driver. The alternative - the CPU bulk-filling 600 KiB
 * through GMADR immediately before the GPU read adjacent memory - is the
 * closest thing in this design to erratum 12's own description of its
 * trigger, and the errata gate opened on condition that it not be done
 * (docs\decisions\2026-09-15-intel-phase5-errata-gate.md).
 *
 * The MI_FLUSH after it has every bit clear, which is what FLUSHES the
 * render cache rather than inhibiting it - bit 2 is an inhibit, and this
 * is the sign-inverted field the packet audit flagged.
 */
#define V9X_I9XX_P5_FILL_DWORDS   7ul

void v9x_i9xx_phase5_parameters(struct v9x_i9xx_phase5_parameters *out)
{
    if (out == 0) {
        return;
    }
    out->target_offset = 0ul;
    out->target_pitch = V9X_I9XX_TARGET_PITCH;
    out->target_bytes = V9X_I9XX_TARGET_BYTES;
    out->width = V9X_I9XX_TARGET_WIDTH;
    out->height = V9X_I9XX_TARGET_HEIGHT;
    out->fill_word = V9X_I9XX_FILL_RGB565;
    out->triangle_color = V9X_I9XX_TRI_COLOR_BGRA;
    {
        /*
         * The two qword pads the builder inserts, counted the same way it
         * applies them. A published length that did not include them would
         * describe a shorter stream than the one staged, and the arm gate
         * compares lengths.
         */
        v9x_u32 prefix = v9x_i9xx_phase5_primitive_offset();

        out->stream_dwords = prefix + v9x_i9xx_vertex_run_extent();
        out->stream_dwords += (out->stream_dwords & 1ul);
    }
    /*
     * The target offset is not a compile-time constant: it comes from
     * v9x_i9xx_sandbox_calculate, which derives it from the VBE-reported size.
     * Left zero here so a caller that forgets to fill it in produces a stream
     * the decoder refuses rather than one that writes to aperture offset zero,
     * which is the top of the published DirectDraw heap.
     */
}

/*
 * Dwords the stream carries BEFORE the 3D state block: the GPU-side fill and
 * the MI_FLUSH that separates it from the draw.
 *
 * Derived by asking the same builder the stream uses, not by a constant typed
 * here. A hand-derived figure is exactly what went wrong: when the fill moved
 * to the GPU the stream gained this prefix, and intel_3d16.c's packet offsets
 * - which carried a comment claiming they could not disagree with the stream -
 * were not updated. Every published offset, and the vertex base derived from
 * it, was seven dwords short. The stream itself was correct throughout; only
 * the capture's account of it was wrong, which is the harder kind of wrong to
 * notice.
 */
v9x_u32 v9x_i9xx_phase5_fill_extent(void)
{
    struct v9x_i9xx_sandbox_layout layout;
    v9x_u32 scratch[16];
    v9x_u32 produced = 0ul;

    if (v9x_i9xx_sandbox_calculate(0x007b0000ul, 0x7f800000ul, &layout) !=
            V9X_STATUS_OK) {
        return 0ul;
    }
    if (v9x_i9xx_build_color_blt(
            layout.target_offset,
            V9X_I9XX_FILL_BLT_WIDTH, V9X_I9XX_FILL_BLT_HEIGHT,
            (v9x_u16)layout.target_pitch, V9X_I9XX_FILL_DWORD,
            layout.target_offset, layout.target_bytes,
            scratch, (v9x_u32)(sizeof(scratch) / sizeof(scratch[0])),
            &produced) != V9X_STATUS_OK) {
        return 0ul;
    }
    /* Plus the MI_FLUSH the builder writes immediately after it. */
    return produced + 1ul;
}

/*
 * The dword the Phase 5 _3DPRIMITIVE starts at, pads included.
 *
 * The ONE place this is computed. It was four: the builder, the capture's
 * OffsetVertices, the capture's vertex-bit reader, and the emitter that feeds
 * the mini-VDD's arm table. Every one of them summed the same prefix
 * independently, and when the qword pad appeared they disagreed - the arm
 * table said 48, the capture said 47, and the vertex reader published the
 * primitive header as a coordinate.
 *
 * That is the same defect three times over in this file's history: the
 * published packet offsets were seven dwords short, the executor's submission
 * boundary survived a stream shrinking under it, and now this. The answer each
 * time was one function.
 */
v9x_u32 v9x_i9xx_phase5_primitive_offset(void)
{
    v9x_u32 prefix = V9X_I9XX_P5_FILL_DWORDS +
                     v9x_i9xx_3d_state_extent() +
                     v9x_i9xx_fragment_program_extent() +
                     V9X_I9XX_P5_PROBE_DWORDS;

    /* Padded so the executor's submission lands on a qword boundary; see the
     * builder for the measurement that made that necessary. */
    return prefix + (prefix & 1ul);
}

v9x_status v9x_i9xx_build_phase5_stream(
    v9x_u32 *stream, v9x_u32 capacity, v9x_u32 *written)
{
    struct v9x_i9xx_sandbox_layout layout;
    v9x_u32 at = 0ul;
    v9x_u32 produced = 0ul;

    if (written != 0) { *written = 0ul; }
    if (stream == 0 || written == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    /*
     * The layout is recomputed here rather than passed in, so the stream and
     * the reserve can never describe different memory. The inputs are the
     * measured netbook values; a machine that reports anything else fails the
     * driver's own preflight long before this.
     */
    if (v9x_i9xx_sandbox_calculate(0x007b0000ul, 0x7f800000ul, &layout) !=
            V9X_STATUS_OK) {
        return V9X_STATUS_INVALID_STATE;
    }

    /*
     * The fill, first. Bounds are the target itself, so the builder's
     * existing extent check is what keeps the BLT inside it - and at
     * 320x480x32 the span is exactly V9X_I9XX_TARGET_BYTES, so that check
     * is tight rather than generous.
     */
    if (v9x_i9xx_build_color_blt(
            layout.target_offset,
            V9X_I9XX_FILL_BLT_WIDTH, V9X_I9XX_FILL_BLT_HEIGHT,
            (v9x_u16)layout.target_pitch, V9X_I9XX_FILL_DWORD,
            layout.target_offset, layout.target_bytes,
            stream + at, capacity - at, &produced) != V9X_STATUS_OK) {
        return V9X_STATUS_INSUFFICIENT_MEMORY;
    }
    at += produced;
    if (capacity - at < 1ul) {
        return V9X_STATUS_INSUFFICIENT_MEMORY;
    }
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

    if (capacity - at < V9X_I9XX_P5_PROBE_DWORDS) {
        return V9X_STATUS_INSUFFICIENT_MEMORY;
    }
    if (v9x_i9xx_build_mi_probe(stream + at, capacity - at, &produced) !=
            V9X_STATUS_OK) {
        return V9X_STATUS_INSUFFICIENT_MEMORY;
    }
    at += produced;

    /*
     * Pad so the PRIMITIVE begins on a qword boundary. RING_TAIL holds a
     * qword-aligned offset and bit 2 is not writable - measured 2026-09-16,
     * 0x10BC written and 0x10B8 read back.
     *
     * This path was aligned by accident at 50 and 66 dwords and broke when the
     * depth BUF_INFO removal made them 47 and 63. It had drawn correctly twice
     * and would not have again.
     *
     * docs\decisions6-09-16-intel-ring-tail-requires-qword-alignment.md
     */
    while (at < v9x_i9xx_phase5_primitive_offset()) {
        if (capacity - at < 1ul) {
            return V9X_STATUS_INSUFFICIENT_MEMORY;
        }
        stream[at++] = V9X_I9XX_MI_NOOP;
    }

    if (v9x_i9xx_build_vertex_run(
            V9X_I9XX_TARGET_WIDTH, V9X_I9XX_TARGET_HEIGHT,
            stream + at, capacity - at, &produced) != V9X_STATUS_OK) {
        return V9X_STATUS_INSUFFICIENT_MEMORY;
    }
    at += produced;

    /* And the draw boundary. */
    if ((at & 1ul) != 0ul) {
        if (capacity - at < 1ul) {
            return V9X_STATUS_INSUFFICIENT_MEMORY;
        }
        stream[at++] = V9X_I9XX_MI_NOOP;
    }

    *written = at;
    return V9X_STATUS_OK;
}

/*
 * The CRC the mini-VDD's arm gate compares against.
 *
 * Deliberately over the stream as built, not over a separate description of
 * it: the whole point is that the thing armed and the thing submitted are the
 * same bytes. Returns zero if the stream cannot be built, which no arm path
 * will ever accept as a valid CRC.
 */
v9x_u32 v9x_i9xx_phase5_execution_crc(void)
{
    v9x_u32 stream[160];
    v9x_u32 written = 0ul;

    if (v9x_i9xx_build_phase5_stream(
            stream, (v9x_u32)(sizeof(stream) / sizeof(stream[0])),
            &written) != V9X_STATUS_OK) {
        return 0ul;
    }
    return v9x_i9xx_crc32_dwords(stream, written);
}

/*
 * One channel of the measured conversion: round(value * max / 255).
 *
 * Written as a multiply and a shift, with NO division and no 32-bit
 * arithmetic, and that is a link-time constraint rather than an optimisation.
 * This file is compiled into I9XXCODE, the driver's second CODE segment. A
 * 32-bit divide or multiply on 16-bit Watcom is a call to a runtime helper
 * (__U4D, __U4M) that lives in the default CODE segment, and a near call
 * cannot reach it:
 *
 *     Error! E2052: ... relocation at 0003:25b5 not in the same segment
 *
 * which is what the first version of this function produced.
 *
 * The constants are exact for every one of the 256 possible inputs, not
 * approximations that happen to agree near the endpoints - searched
 * exhaustively against (value * max + 127) / 255, and the host test pins the
 * level boundary where round and truncate part company.
 *
 *     5-bit: (value * 249 + 1024) >> 11
 *     6-bit: (value * 253 +  512) >> 10
 *
 * The largest intermediate is 255 * 249 + 1024 = 64519, so 16-bit unsigned
 * arithmetic holds every step and nothing widens.
 */
#define V9X_I9XX_R5_MUL   ((v9x_u16)249u)
#define V9X_I9XX_R5_ADD   ((v9x_u16)1024u)
#define V9X_I9XX_R5_SHIFT 11
#define V9X_I9XX_R6_MUL   ((v9x_u16)253u)
#define V9X_I9XX_R6_ADD   ((v9x_u16)512u)
#define V9X_I9XX_R6_SHIFT 10

static v9x_u16 v9x_i9xx_round8_to(v9x_u32 value, v9x_u16 multiplier,
                                  v9x_u16 addend, int shift)
{
    v9x_u16 byte;

    byte = (v9x_u16)(value & 0xfful);

    /*
     * No clamp, and none is needed: byte cannot exceed 255, so the result
     * cannot exceed the channel maximum. A clamp here would imply an input
     * this cannot receive.
     */
    return (v9x_u16)((v9x_u16)(byte * multiplier + addend) >> shift);
}

/*
 * The 8-bit-to-RGB565 conversion MEASURED on the 945GSE colour backend.
 *
 * Two triangle colours establish it as far as it is established. 0xfff86428
 * stored 0xf325 where truncation predicted 0xfb25; 0xff1587f9 stored 0x1c3e
 * where truncation predicted 0x143f. Every one of the six tested channel
 * values agrees with round, and trunc, round8, floor and ceil are each
 * excluded as UNIFORM rules.
 *
 * The limit, because it is easy to miss: green agreed with truncation at both
 * tested values - 100 gives 25 either way, 135 gives 33 either way. A backend
 * that rounds red and blue but truncates green fits the data equally well.
 * That is untested, not excluded. docs\decisions6-09-15-intel-565-
 * conversion-rounds.md says what one more green value would settle.
 *
 * What this function is used for stands either way: it reproduces what the
 * hardware was observed to store for the colours in use, which is what the
 * capture validator needs.
 *
 * Intel-only by intent. The shared rasteriser truncates, and that is not a
 * defect there; see the header for why the expectation lives here instead.
 */
v9x_u16 v9x_i9xx_rgb565_round(v9x_u32 red, v9x_u32 green, v9x_u32 blue)
{
    v9x_u16 packed;

    packed = (v9x_u16)(
        (v9x_u16)(v9x_i9xx_round8_to(red, V9X_I9XX_R5_MUL, V9X_I9XX_R5_ADD,
                                     V9X_I9XX_R5_SHIFT) << 11) |
        (v9x_u16)(v9x_i9xx_round8_to(green, V9X_I9XX_R6_MUL, V9X_I9XX_R6_ADD,
                                     V9X_I9XX_R6_SHIFT) << 5) |
        v9x_i9xx_round8_to(blue, V9X_I9XX_R5_MUL, V9X_I9XX_R5_ADD,
                           V9X_I9XX_R5_SHIFT));

    return packed;
}
