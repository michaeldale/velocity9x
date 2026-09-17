/*
 * Scanline and frame-counter readings, summarised.
 *
 * The question a Gen3 flip path has to answer before it exists is whether
 * this driver can SEE the retrace on this part. The ViRGE path reads the VGA
 * input-status port for that; whether that bit means anything on a 945GSE
 * driving an LVDS panel through pipe B is unmeasured. The pipe's own display
 * line register and frame counter are the native answer, and this module is
 * the pure half of reading them: the HAL feeds raw register values, this
 * keeps the range, the number of changes and the frame count at both ends,
 * and a capture reports the result.
 *
 * No register is read here. The masks and the split frame counter are from
 * Linux i915_reg.h, cited beside the offsets in intel_gma.h.
 */
#include "velocity9x/intel_gma.h"

void v9x_i9xx_scan_begin(struct v9x_i9xx_scan_summary *summary)
{
    if (summary == 0) {
        return;
    }
    summary->samples = 0ul;
    summary->line_min = 0ul;
    summary->line_max = 0ul;
    summary->line_changes = 0ul;
    summary->line_last = 0ul;
    summary->frame_first = 0ul;
    summary->frame_last = 0ul;
}

/*
 * The frame counter as one number: the high 16 bits from FRAMEHIGH above the
 * low 8 from the top byte of FRAMEPIXEL. Two reads, not atomic, so a carry
 * between them can produce one wrong value in a run; the summary keeps only
 * the first and last, and a run long enough to matter is long enough that
 * one reading is not the evidence.
 */
static v9x_u32 v9x_i9xx_scan_frame(v9x_u32 frame_high_raw,
                                   v9x_u32 frame_pixel_raw)
{
    return (((frame_high_raw & V9X_I9XX_FRAME_HIGH_MASK) << 8) |
            (frame_pixel_raw >> V9X_I9XX_FRAME_LOW_SHIFT)) &
           V9X_I9XX_FRAME_COUNT_MASK;
}

void v9x_i9xx_scan_feed(struct v9x_i9xx_scan_summary *summary,
                        v9x_u32 dsl_raw, v9x_u32 frame_high_raw,
                        v9x_u32 frame_pixel_raw)
{
    v9x_u32 line;
    v9x_u32 frame;

    if (summary == 0) {
        return;
    }
    line = dsl_raw & V9X_I9XX_DSL_LINE_MASK;
    frame = v9x_i9xx_scan_frame(frame_high_raw, frame_pixel_raw);

    if (summary->samples == 0ul) {
        summary->line_min = line;
        summary->line_max = line;
        summary->frame_first = frame;
    } else {
        if (line < summary->line_min) {
            summary->line_min = line;
        }
        if (line > summary->line_max) {
            summary->line_max = line;
        }
        if (line != summary->line_last) {
            ++summary->line_changes;
        }
    }
    summary->line_last = line;
    summary->frame_last = frame;
    ++summary->samples;
}

v9x_u32 v9x_i9xx_scan_frames(const struct v9x_i9xx_scan_summary *summary)
{
    if (summary == 0 || summary->samples == 0ul) {
        return 0ul;
    }
    return (summary->frame_last - summary->frame_first) &
           V9X_I9XX_FRAME_COUNT_MASK;
}
