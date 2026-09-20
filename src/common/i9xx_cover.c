/*
 * The presented surface's read plan. See include\velocity9x\i9xx_cover.h
 * for why this is a module of its own.
 */
#include "velocity9x/i9xx_cover.h"
#include "velocity9x/i9xx_wm.h"

/*
 * PIPE_SRCSZ: width less one in bits 27:16, height less one in 11:0. The
 * netbook reads 0x03FF023F for its 1024x576 panel, which is 1023 and 575.
 */
#define V9X_I9XX_COVER_SRC_SHIFT   16u
#define V9X_I9XX_COVER_SRC_MASK    ((v9x_u32)0x0ffful)

v9x_u16 v9x_i9xx_cover_plan(v9x_u32 pipesrc, v9x_u32 dspcntr,
                            v9x_u32 stride, v9x_u32 byte_offset,
                            v9x_u32 vram_bytes, v9x_u32 step,
                            struct v9x_i9xx_cover_plan *plan)
{
    v9x_u32 width;
    v9x_u32 height;
    v9x_u32 cpp;

    if (plan == 0 || step == 0ul) {
        return V9X_FALSE;
    }

    width = ((pipesrc >> V9X_I9XX_COVER_SRC_SHIFT) &
             V9X_I9XX_COVER_SRC_MASK) + 1ul;
    height = (pipesrc & V9X_I9XX_COVER_SRC_MASK) + 1ul;

    /*
     * The pixel format, from the plane's own control register. Eight bytes
     * a pixel is the half-float surface: the field names it and this reads
     * whole pixels as integers, so it is declined rather than misread.
     */
    cpp = v9x_i9xx_wm_cpp_from_dspcntr(dspcntr);
    if (cpp != 1ul && cpp != 2ul && cpp != 4ul) {
        return V9X_FALSE;
    }

    if (stride == 0ul || vram_bytes == 0ul || byte_offset >= vram_bytes) {
        return V9X_FALSE;
    }

    /*
     * A row must fit the pitch, tested by DIVIDING rather than multiplying
     * width by cpp: the product of two attacker-shaped values is exactly
     * the overflow this check exists to survive.
     */
    if (width > stride / cpp) {
        return V9X_FALSE;
    }

    /*
     * And the whole surface must fit what is left of video memory, tested
     * the same way. vram_bytes - byte_offset cannot underflow here because
     * byte_offset was bounded above.
     */
    if (height > (vram_bytes - byte_offset) / stride) {
        return V9X_FALSE;
    }

    plan->width = width;
    plan->height = height;
    plan->pitch = stride;
    plan->bytes_per_pixel = cpp;
    plan->format = (dspcntr >> 26) & 0x0ful;
    plan->step = step;
    /* Ceiling division: the first sample of a row is always taken, so a
     * width of one at a step of eight is one column and not none. */
    plan->columns = (width + step - 1ul) / step;
    plan->rows = (height + step - 1ul) / step;

    return V9X_TRUE;
}

void v9x_i9xx_cover_request(struct v9x_i9xx_cover_state *state)
{
    if (state == 0) {
        return;
    }
    /*
     * Scheduling only. Nothing is cleared here, because the caller may be
     * the desktop-restoration flip on the way out of a benchmark, and the
     * records and image it is about to discard are the evidence that run
     * just produced.
     */
    state->rearm = 1ul;
    state->image_wanted = 1ul;
}

v9x_u16 v9x_i9xx_cover_plan_same(const struct v9x_i9xx_cover_plan *first,
                                 const struct v9x_i9xx_cover_plan *second)
{
    if (first == 0 || second == 0) {
        return V9X_FALSE;
    }
    if (first->width != second->width ||
        first->height != second->height ||
        first->pitch != second->pitch ||
        first->bytes_per_pixel != second->bytes_per_pixel ||
        first->format != second->format ||
        first->step != second->step) {
        return V9X_FALSE;
    }

    return V9X_TRUE;
}

v9x_u16 v9x_i9xx_cover_commit_image(struct v9x_i9xx_cover_state *state,
                                    v9x_u32 status)
{
    if (state == 0) {
        return V9X_FALSE;
    }
    if (status != 1ul) {           /* V9X_D3D_IMAGE_WRITTEN */
        return V9X_FALSE;
    }
    state->image_wanted = 0ul;

    return V9X_TRUE;
}

v9x_u16 v9x_i9xx_cover_begin(struct v9x_i9xx_cover_state *state,
                             v9x_u32 slots)
{
    if (state == 0) {
        return V9X_FALSE;
    }
    /*
     * The pending request is applied HERE, at the sample, and not where it
     * was made. That is what stops an arming path that runs later in the
     * same flip from erasing the capture that flip just took.
     */
    if (state->rearm != 0ul) {
        state->rearm = 0ul;
        state->records = 0ul;
        state->attempts = 0ul;
    }
    if (state->records >= slots) {
        return V9X_FALSE;
    }
    ++state->records;

    return V9X_TRUE;
}
