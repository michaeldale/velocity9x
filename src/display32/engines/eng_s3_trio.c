/*
 * S3 Trio32/64 drawing engine for V9XHAL.DLL.
 *
 * The Trio's enhanced 8514/A-compatible engine is driven entirely by 16-bit
 * port I/O, with no MMIO window and no status latch to validate: it is either
 * present or it is not. It also has no per-surface base or stride, so it walks
 * display memory as one surface at the display pitch and declines anything
 * that does not sit on a display-pitch scan line. Those declines are expected
 * and are served by the CPU fallbacks.
 *
 * There is no recovery path here. A forced timeout raises idle_timeouts and
 * leaves reset_count flat, which is measured rather than assumed - see
 * docs/decisions/2026-08-16-engine-fault-injection.md.
 */
#include "ddhal_internal.h"

static int v9x_trio_engine_ready(void)
{
    return v9x_hal != 0 &&
           (v9x_hal->fb.flags & V9X_DD_FB_VALID) != 0ul &&
           (v9x_hal->engine.flags &
            (V9X_DD_ENGINE_VALID | V9X_DD_ENGINE_S3_TRIO64)) ==
            (V9X_DD_ENGINE_VALID | V9X_DD_ENGINE_S3_TRIO64);
}

static int v9x_trio_wait_idle(int wait)
{
    DWORD spins;

    if (!v9x_trio_engine_ready()) {
        return 0;
    }
    if (!(wait && v9x_fault_injected())) {
        if ((v9x_inpw(V9X_TRIO_CMD_STATUS) & V9X_TRIO_STATUS_BUSY) == 0u) {
            return 1;
        }
        if (!wait) {
            return 0;
        }
        spins = V9X_TRIO_IDLE_SPIN_LIMIT;
        while (spins-- != 0ul) {
            if ((v9x_inpw(V9X_TRIO_CMD_STATUS) & V9X_TRIO_STATUS_BUSY) == 0u) {
                return 1;
            }
        }
    }
    ++v9x_hal->engine.idle_timeouts;
    v9x_trace_flush_fault(0x54394944ul, V9X_TRIO_CMD_STATUS);
    return 0;
}

/*
 * Screen-to-screen BitBLT on the Trio32/64 enhanced engine.
 *
 * Unlike the ViRGE, this engine has no per-surface base or stride: it walks
 * display memory as one surface at the display pitch from a common bank
 * base, so a surface's position has to be folded into its y coordinate and
 * both rectangles must sit on display-pitch scan lines. Anything else is
 * declined and served by the CPU copy.
 */
static int v9x_trio_copy(V9X_DDHAL_BLTDATA *data, DWORD source_offset,
                         DWORD destination_offset, DWORD bytes_per_pixel,
                         int wait)
{
    DWORD pitch = v9x_hal->fb.pitch;
    DWORD width = (DWORD)(data->rSrc[2] - data->rSrc[0]);
    DWORD height = (DWORD)(data->rSrc[3] - data->rSrc[1]);
    DWORD source_x = (DWORD)data->rSrc[0];
    DWORD source_y;
    DWORD destination_x = (DWORD)data->rDest[0];
    DWORD destination_y;
    unsigned short command;

    (void)bytes_per_pixel;
    if (pitch == 0ul ||
        (DWORD)data->lpDDSrcSurface->lpGbl->lPitch != pitch ||
        (DWORD)data->lpDDDestSurface->lpGbl->lPitch != pitch ||
        (source_offset % pitch) != 0ul ||
        (destination_offset % pitch) != 0ul ||
        width == 0ul || height == 0ul) {
        return V9X_BLT_DECLINED;
    }
    source_y = source_offset / pitch + (DWORD)data->rSrc[1];
    destination_y = destination_offset / pitch + (DWORD)data->rDest[1];
    if (source_y >= 2048ul || destination_y >= 2048ul ||
        height > 2048ul - source_y || height > 2048ul - destination_y) {
        return V9X_BLT_DECLINED;
    }

    command = (unsigned short)(V9X_TRIO_CMD_BITBLT | V9X_TRIO_CMD_INC_X |
                               V9X_TRIO_CMD_INC_Y);
    /* Only a copy within one surface can overlap. */
    if (source_offset == destination_offset) {
        if (destination_y > source_y) {
            command = (unsigned short)(command & ~V9X_TRIO_CMD_INC_Y);
            source_y += height - 1ul;
            destination_y += height - 1ul;
        } else if (destination_y == source_y &&
                   destination_x > source_x) {
            command = (unsigned short)(command & ~V9X_TRIO_CMD_INC_X);
            source_x += width - 1ul;
            destination_x += width - 1ul;
        }
    }

    if (!v9x_trio_wait_idle(wait)) {
        return V9X_BLT_BUSY;
    }
    v9x_outpw(V9X_TRIO_FRGD_MIX, V9X_TRIO_FRGD_MIX_COPY);
    v9x_outpw(V9X_TRIO_MULTIFUNC_CNTL, V9X_TRIO_PIXEL_CNTL_FRGD_MIX);
    v9x_outpw(V9X_TRIO_CUR_X, (unsigned short)source_x);
    v9x_outpw(V9X_TRIO_CUR_Y, (unsigned short)source_y);
    v9x_outpw(V9X_TRIO_DESTX_DIASTP, (unsigned short)destination_x);
    v9x_outpw(V9X_TRIO_DESTY_AXSTP, (unsigned short)destination_y);
    v9x_outpw(V9X_TRIO_MAJ_AXIS_PCNT, (unsigned short)(width - 1ul));
    v9x_outpw(V9X_TRIO_MULTIFUNC_CNTL, (unsigned short)(height - 1ul));
    v9x_outpw(V9X_TRIO_CMD_STATUS, command);
    return V9X_BLT_DONE;
}

static int v9x_trio_fill(V9X_DDHAL_BLTDATA *data, DWORD offset,
                         DWORD bytes_per_pixel, int wait)
{
    DWORD pitch = v9x_hal->fb.pitch;
    DWORD y;
    DWORD width;
    DWORD height;

    (void)bytes_per_pixel;
    /* The engine addresses display memory as one surface at the display
     * pitch, so only display-pitch surfaces starting on a scan line can be
     * expressed as an (x, y) rectangle. */
    if ((DWORD)data->lpDDDestSurface->lpGbl->lPitch != pitch ||
        pitch == 0ul || (offset % pitch) != 0ul) {
        return V9X_BLT_DECLINED;
    }
    y = offset / pitch + (DWORD)data->rDest[1];
    width = (DWORD)(data->rDest[2] - data->rDest[0]);
    height = (DWORD)(data->rDest[3] - data->rDest[1]);
    if (y >= 2048ul || height > 2048ul - y) {
        return V9X_BLT_DECLINED;
    }
    if (!v9x_trio_wait_idle(wait)) {
        return V9X_BLT_BUSY;
    }

    /* S3 Trio32/64 databook section 13.3.3: solid rectangle fill. */
    v9x_outpw(V9X_TRIO_FRGD_MIX, V9X_TRIO_FRGD_MIX_NEW);
    v9x_outpw(V9X_TRIO_FRGD_COLOR,
              (unsigned short)data->bltFX.dwFillColor);
    v9x_outpw(V9X_TRIO_MULTIFUNC_CNTL, V9X_TRIO_PIXEL_CNTL_FRGD_MIX);
    v9x_outpw(V9X_TRIO_CUR_X, (unsigned short)data->rDest[0]);
    v9x_outpw(V9X_TRIO_CUR_Y, (unsigned short)y);
    v9x_outpw(V9X_TRIO_MAJ_AXIS_PCNT, (unsigned short)(width - 1ul));
    v9x_outpw(V9X_TRIO_MULTIFUNC_CNTL, (unsigned short)(height - 1ul));
    v9x_outpw(V9X_TRIO_CMD_STATUS, V9X_TRIO_CMD_RECT_SOLID);
    return V9X_BLT_DONE;
}

/*
 * The Trio64 has no status to validate, so CANBLT is answered by the same
 * non-blocking idle poll that answers ISBLTDONE. Passing wait == 0 is also
 * what keeps a poll from spending a fault injection on an answer it was
 * always entitled to give.
 */
static int v9x_trio_can_blt(void)
{
    return v9x_trio_wait_idle(0);
}

const V9X_ENGINE32_OPS v9x_engine32_trio = {
    v9x_trio_engine_ready,
    v9x_trio_engine_ready,
    v9x_trio_engine_ready,
    v9x_trio_can_blt,
    v9x_trio_wait_idle,
    v9x_trio_fill,
    v9x_trio_copy
};

