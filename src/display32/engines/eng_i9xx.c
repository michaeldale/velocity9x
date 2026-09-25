/*
 * Intel Gen3 blitter for V9XHAL.DLL: DirectDraw fills and copies through the
 * ring. docs\plans\intel-gen3-directdraw-blits.md.
 *
 * Until this table existed every Blt on the 945GSE was completed by
 * blt_cpu.c through the uncached aperture. Half-Life at 640x480x16 presents
 * each frame as a Blt from the back buffer, and V9XSNA7.INI (2026-09-25)
 * timed those copies at about 53 million cycles each - some 32 ms at the
 * netbook's 1.63 GHz, including the render drain ahead of each one.
 *
 * Everything expensive is somebody else's: the ring and its wrap plan, the
 * breadcrumb that says a batch has drawn, the drain Flip and Lock already
 * call. This file turns a DDHAL blit into a packet and hands it to
 * v9x_d3d_i9xx_submit_blt, which seals it, decodes it and submits it.
 *
 * There is no reset here and none is invented (plan decision 9): a wedged
 * Gen3 ring is a full-GPU-reset event, and the drain's abandon path already
 * stops the channel asking for completions that will not come.
 */
#include "ddhal_internal.h"
#include "velocity9x/intel_gma.h"

/*
 * The ring the blit goes through, and the windows it is reached by. A boot
 * without a ring has no ring_linear_base, so this fails and every blit takes
 * the CPU path - which is the gate, and the only one (plan decision 8).
 */
static int v9x_i9xx_engine_ready(void)
{
    return v9x_hal != 0 &&
           (v9x_hal->fb.flags & V9X_DD_FB_VALID) != 0ul &&
           (v9x_hal->engine.flags & V9X_DD_ENGINE_VALID) != 0ul &&
           v9x_hal->engine.engine_type == V9X_DD_ENGINE_TYPE_INTEL_GEN3 &&
           v9x_hal->engine.control_linear_base != 0ul &&
           v9x_hal->engine.ring_linear_base != 0ul &&
           v9x_hal->engine.ring_bytes != 0ul;
}

/*
 * Idle is the breadcrumb: nothing outstanding, or the outstanding sequence
 * seen in the status page. The ring's head reaching its tail says only that
 * the parser has consumed the commands, not that the pixels are written.
 */
static int v9x_i9xx_engine_wait_idle(int wait)
{
    return v9x_d3d_i9xx_render_drain(wait);
}

static int v9x_i9xx_engine_can_blt(void)
{
    return v9x_d3d_i9xx_render_drain(0);
}

/*
 * Submit a built packet. The submit waits, bounded, for the head and then
 * for the breadcrumb, whatever `wait` says - the same as every draw - so
 * a blit that returns DONE has been parsed, and one whose breadcrumb timed
 * out is left outstanding for the next drain.
 *
 * Any refusal is DECLINED, never BUSY: the submit refuses a ring that is
 * full rather than waiting for room, and the ring is empty between
 * submissions because each one waits for its head. The core then drains and
 * completes the blit on the CPU with correct pixels, as on the S3 engines.
 */
static int v9x_i9xx_engine_submit(DWORD *stream, DWORD written,
                                  DWORD bytes_per_pixel)
{
    if (!v9x_d3d_i9xx_submit_blt(stream, written, V9X_I9XX_BLT_STREAM_DWORDS,
                                 bytes_per_pixel)) {
        return V9X_BLT_DECLINED;
    }
    return V9X_BLT_DONE;
}

static int v9x_i9xx_engine_fill(V9X_DDHAL_BLTDATA *data, DWORD offset,
                                DWORD bytes_per_pixel, int wait)
{
    struct v9x_i9xx_blt blt;
    DWORD stream[V9X_I9XX_BLT_STREAM_DWORDS];
    DWORD written = 0ul;

    (void)wait;
    if (!v9x_i9xx_engine_ready()) {
        return V9X_BLT_DECLINED;
    }

    /* A depth fill arrives here too, with the depth buffer's two bytes and
     * dwFillDepth, which shares dwFillColor's DWORD. */
    blt.bytes_per_pixel = bytes_per_pixel;
    blt.vram_bytes = v9x_hal->fb.vram_bytes;
    blt.destination_base = offset;
    blt.destination_pitch = (DWORD)data->lpDDDestSurface->lpGbl->lPitch;
    blt.left = (DWORD)data->rDest[0];
    blt.top = (DWORD)data->rDest[1];
    blt.right = (DWORD)data->rDest[2];
    blt.bottom = (DWORD)data->rDest[3];
    blt.source_base = 0ul;
    blt.source_pitch = 0ul;
    blt.source_left = 0ul;
    blt.source_top = 0ul;
    blt.color = data->bltFX.dwFillColor;
    if (v9x_i9xx_build_fill_blt(&blt, stream, V9X_I9XX_BLT_STREAM_DWORDS,
                                &written) != V9X_STATUS_OK) {
        return V9X_BLT_DECLINED;
    }
    return v9x_i9xx_engine_submit(stream, written, bytes_per_pixel);
}

/*
 * Screen-to-screen copy. Overlapping rectangles are refused by the builder
 * and served by the CPU copy (plan decision 7): XY_SRC_COPY_BLT has no scan
 * direction, and nothing says what it does when source and destination
 * share pixels.
 */
static int v9x_i9xx_engine_copy(V9X_DDHAL_BLTDATA *data, DWORD source_offset,
                                DWORD destination_offset,
                                DWORD bytes_per_pixel, int wait)
{
    struct v9x_i9xx_blt blt;
    DWORD stream[V9X_I9XX_BLT_STREAM_DWORDS];
    DWORD written = 0ul;

    (void)wait;
    if (!v9x_i9xx_engine_ready()) {
        return V9X_BLT_DECLINED;
    }

    blt.bytes_per_pixel = bytes_per_pixel;
    blt.vram_bytes = v9x_hal->fb.vram_bytes;
    blt.destination_base = destination_offset;
    blt.destination_pitch = (DWORD)data->lpDDDestSurface->lpGbl->lPitch;
    blt.left = (DWORD)data->rDest[0];
    blt.top = (DWORD)data->rDest[1];
    blt.right = (DWORD)data->rDest[2];
    blt.bottom = (DWORD)data->rDest[3];
    blt.source_base = source_offset;
    blt.source_pitch = (DWORD)data->lpDDSrcSurface->lpGbl->lPitch;
    blt.source_left = (DWORD)data->rSrc[0];
    blt.source_top = (DWORD)data->rSrc[1];
    blt.color = 0ul;
    if (v9x_i9xx_build_copy_blt(&blt, stream, V9X_I9XX_BLT_STREAM_DWORDS,
                                &written) != V9X_STATUS_OK) {
        return V9X_BLT_DECLINED;
    }
    return v9x_i9xx_engine_submit(stream, written, bytes_per_pixel);
}

/*
 * Ready, validate_status and status_validated collapse onto one test, as on
 * the Trio64: there is no status register to latch, only a ring that is
 * either published or not.
 */
const V9X_ENGINE32_OPS v9x_engine32_i9xx = {
    v9x_i9xx_engine_ready,
    v9x_i9xx_engine_ready,
    v9x_i9xx_engine_ready,
    v9x_i9xx_engine_can_blt,
    v9x_i9xx_engine_wait_idle,
    v9x_i9xx_engine_fill,
    v9x_i9xx_engine_copy
};
