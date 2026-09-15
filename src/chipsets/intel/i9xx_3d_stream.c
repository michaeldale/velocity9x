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
    out->stream_dwords = V9X_I9XX_P5_FILL_DWORDS +
                         v9x_i9xx_3d_state_extent() +
                         v9x_i9xx_fragment_program_extent() +
                         V9X_I9XX_P5_PROBE_DWORDS +
                         v9x_i9xx_vertex_run_extent();
    /*
     * The target offset is not a compile-time constant: it comes from
     * v9x_i9xx_sandbox_calculate, which derives it from the VBE-reported size.
     * Left zero here so a caller that forgets to fill it in produces a stream
     * the decoder refuses rather than one that writes to aperture offset zero,
     * which is the top of the published DirectDraw heap.
     */
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

    if (v9x_i9xx_build_vertex_run(
            V9X_I9XX_TARGET_WIDTH, V9X_I9XX_TARGET_HEIGHT,
            stream + at, capacity - at, &produced) != V9X_STATUS_OK) {
        return V9X_STATUS_INSUFFICIENT_MEMORY;
    }
    at += produced;

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
