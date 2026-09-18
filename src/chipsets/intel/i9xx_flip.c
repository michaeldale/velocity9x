/*
 * The Gen3 ring flip: MI_DISPLAY_FLIP built, and checked, as pure C.
 *
 * A bare write to the plane base register tears on the 945GSE whenever this
 * driver makes it: mid-frame it splits the picture where the beam is
 * (intel65), inside the blank it splits it worse (intel66), and intel69
 * measured that the blank IS where the line register says it is - so the
 * write, not the wait, is what does not fit. Linux i915 does not write the
 * base for a Gen3 page flip. It puts MI_DISPLAY_FLIP in the ring and the
 * display engine applies the new base at the retrace itself, reporting the
 * flip pending in the interrupt status register until it has.
 *
 * i915_reg.h, cited by name because nothing here has been measured:
 *
 *   MI_INSTR(op, flags) = (op << 23) | flags
 *   MI_DISPLAY_FLIP_I915 = MI_INSTR(0x14, 1)      three dwords: cmd, pitch, base
 *   MI_DISPLAY_FLIP_PLANE(n) = (n) << 20          plane A 0, plane B 1
 *   ISR 0x020ac; I915_DISPLAY_PLANE_A_FLIP_PENDING_INTERRUPT (1 << 11),
 *                I915_DISPLAY_PLANE_B_FLIP_PENDING_INTERRUPT (1 << 10)
 *   (the audit of 2026-09-18 corrected these from 1 << 2 and 1 << 6, which
 *   are MI_WAIT_FOR_EVENT's plane-flip operand bits)
 *
 * intel_gen3_queue_flip emits MI_WAIT_FOR_EVENT on the plane's flip-pending
 * bit ahead of the flip so a second flip cannot overtake the first. This
 * driver does not: the flip state machine refuses a new Flip while one is
 * pending, by reading the same ISR bit, so the ring never waits and a
 * DirectDraw Flip never blocks inside the parser.
 *
 * The stream is four dwords, the third being MI_NOOP so the tail stays qword
 * aligned. The decoder requires exactly those four for the declared plane,
 * pitch and base, which is the allowlist rule every stream this engine
 * submits has to pass.
 */
#include "velocity9x/intel_gma.h"

v9x_u32 v9x_i9xx_flip_stream_extent(void)
{
    return V9X_I9XX_FLIP_STREAM_DWORDS;
}

/*
 * Plane 0 or 1; a pitch that is a non-zero multiple of 64 bytes, which is
 * what the plane stride register itself holds and is read from; a base that
 * is dword aligned and inside the framebuffer. Each refused rather than
 * rounded, for the reason the base write refuses an odd offset.
 */
static v9x_u16 v9x_i9xx_flip_arguments_ok(v9x_u32 plane, v9x_u32 pitch,
                                          v9x_u32 base, v9x_u32 vram_bytes)
{
    if (plane > 1ul) {
        return V9X_FALSE;
    }
    if (pitch == 0ul || (pitch & 63ul) != 0ul) {
        return V9X_FALSE;
    }
    if ((base & 3ul) != 0ul || base >= vram_bytes) {
        return V9X_FALSE;
    }
    return V9X_TRUE;
}

v9x_status v9x_i9xx_build_flip_stream(
    v9x_u32 plane, v9x_u32 pitch, v9x_u32 base, v9x_u32 vram_bytes,
    v9x_u32 *stream, v9x_u32 capacity, v9x_u32 *written)
{
    if (written != 0) { *written = 0ul; }
    if (stream == 0 || written == 0 ||
        capacity < V9X_I9XX_FLIP_STREAM_DWORDS) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (v9x_i9xx_flip_arguments_ok(plane, pitch, base, vram_bytes) ==
            V9X_FALSE) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    stream[0] = V9X_I9XX_MI_DISPLAY_FLIP_I915 |
                (plane << V9X_I9XX_MI_DISPLAY_FLIP_PLANE_SHIFT);
    stream[1] = pitch;
    stream[2] = base;
    stream[3] = V9X_I9XX_MI_NOOP;
    *written = V9X_I9XX_FLIP_STREAM_DWORDS;
    return V9X_STATUS_OK;
}

v9x_status v9x_i9xx_build_breadcrumb_stream(
    v9x_u32 byte_offset, v9x_u32 value,
    v9x_u32 *stream, v9x_u32 capacity, v9x_u32 *written)
{
    if (written != 0) { *written = 0ul; }
    if (stream == 0 || written == 0 ||
        capacity < V9X_I9XX_BREADCRUMB_STREAM_DWORDS) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    /* A dword of the page: dword aligned and inside the 4 KiB. */
    if ((byte_offset & 3ul) != 0ul || byte_offset >= 0x1000ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    stream[0] = V9X_I9XX_MI_STORE_DWORD_INDEX;
    stream[1] = byte_offset;
    stream[2] = value;
    stream[3] = V9X_I9XX_MI_NOOP;
    *written = V9X_I9XX_BREADCRUMB_STREAM_DWORDS;
    return V9X_STATUS_OK;
}

v9x_u16 v9x_i9xx_decode_flip_stream(
    const v9x_u32 *stream, v9x_u32 dword_count,
    v9x_u32 plane, v9x_u32 pitch, v9x_u32 base, v9x_u32 vram_bytes,
    v9x_u32 *rejected_index)
{
    v9x_u32 index = 0ul;

    if (rejected_index != 0) { *rejected_index = 0ul; }
    if (stream == 0 || dword_count != V9X_I9XX_FLIP_STREAM_DWORDS ||
        v9x_i9xx_flip_arguments_ok(plane, pitch, base, vram_bytes) ==
            V9X_FALSE) {
        return V9X_FALSE;
    }
    if (stream[0] != (V9X_I9XX_MI_DISPLAY_FLIP_I915 |
                      (plane << V9X_I9XX_MI_DISPLAY_FLIP_PLANE_SHIFT))) {
        index = 0ul;
    } else if (stream[1] != pitch) {
        index = 1ul;
    } else if (stream[2] != base) {
        index = 2ul;
    } else if (stream[3] != V9X_I9XX_MI_NOOP) {
        index = 3ul;
    } else {
        return V9X_TRUE;
    }
    if (rejected_index != 0) { *rejected_index = index; }
    return V9X_FALSE;
}

/* The ISR bit that says a flip is queued on this plane and not yet taken. */
v9x_u32 v9x_i9xx_flip_pending_bit(v9x_u32 plane)
{
    if (plane == 0ul) {
        return V9X_I9XX_ISR_PLANE_A_FLIP_PENDING;
    }
    if (plane == 1ul) {
        return V9X_I9XX_ISR_PLANE_B_FLIP_PENDING;
    }
    return 0ul;
}
