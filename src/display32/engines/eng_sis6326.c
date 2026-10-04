/*
 * SiS 6326 2D engine for V9XHAL.DLL: DirectDraw solid fill and screen copy.
 *
 * Registers through the 64 KiB BAR1 window the 16-bit side had the mini-VDD
 * map, after turning its decode on (sis6326_hw16.c). Register values come
 * from the host-tested builder, src\chipsets\sis\sis6326_engine.c, whose
 * fill, forward copy and both overlap directions matched byte for byte on
 * the card at 8 and 16 bpp
 * (docs\decisions\2026-10-05-sis6326-2d-engine-writes.md).
 *
 * Each operation: wait for the engine to be idle, write the builder's
 * dwords, write the 16-bit command word that starts it, then read the
 * status dword back as xf86-video-sis does after every command. The Turbo
 * Queue is off, so the hardware queue is the only queue and 82ABh D6 means
 * the engine is busy or that queue holds work.
 *
 * There is no recovery path: the datasheet documents no engine reset. A
 * timeout raises idle_timeouts and the operation is declined to the CPU.
 */
#include "ddhal_internal.h"
#include "velocity9x/sis6326_engine.h"

/* Bounded busy-wait. The write probe measured 2-8 status reads per
 * operation on small rectangles; a full 1600x1200x16 fill is far longer,
 * and this bound only has to tell slow from stuck. */
#define V9X_SIS_WAIT_SPINS 0x00200000ul

/* The window the mini-VDD maps (V9X_SIS_MMIO_BYTES there). */
#define V9X_SIS_MMIO_BYTES 0x00010000ul

static int v9x_sis_ready(void)
{
    return v9x_hal != 0 &&
        (v9x_hal->fb.flags & V9X_DD_FB_VALID) != 0ul &&
        (v9x_hal->engine.flags & V9X_DD_ENGINE_VALID) != 0ul &&
        v9x_hal->engine.engine_type == V9X_DD_ENGINE_TYPE_SIS_6326 &&
        v9x_hal->engine.control_linear_base != 0ul &&
        v9x_hal->engine.mapped_aperture_bytes >= V9X_SIS_MMIO_BYTES;
}

static DWORD v9x_sis_read32(DWORD offset)
{
    return *(volatile DWORD *)(v9x_hal->engine.control_linear_base + offset);
}

static void v9x_sis_write32(DWORD offset, DWORD value)
{
    *(volatile DWORD *)(v9x_hal->engine.control_linear_base + offset) = value;
}

static void v9x_sis_write16(DWORD offset, unsigned short value)
{
    *(volatile unsigned short *)(v9x_hal->engine.control_linear_base +
                                 offset) = value;
}

/* A window whose decode was turned off since the map - a mode set the
 * 16-bit side has not re-enabled after - reads all ones. */
static int v9x_sis_validate(void)
{
    if (!v9x_sis_ready()) {
        return 0;
    }
    if ((v9x_hal->engine.flags & V9X_DD_ENGINE_STATUS_VALIDATED) != 0ul) {
        return 1;
    }
    if (v9x_sis_read32(V9X_SIS_2D_CMD_STATUS) == 0xfffffffful) {
        return 0;
    }
    v9x_hal->engine.flags |= V9X_DD_ENGINE_STATUS_VALIDATED;
    return 1;
}

static int v9x_sis_validated(void)
{
    return v9x_sis_ready() &&
        (v9x_hal->engine.flags & V9X_DD_ENGINE_STATUS_VALIDATED) != 0ul;
}

static int v9x_sis_wait_idle(int wait)
{
    DWORD spins;

    if (!v9x_sis_validate()) {
        return 0;
    }
    if (!(wait && v9x_fault_injected())) {
        if (v9x_sis_status_busy(v9x_sis_read32(V9X_SIS_2D_CMD_STATUS)) ==
            0ul) {
            return 1;
        }
        if (!wait) {
            return 0;
        }
        spins = V9X_SIS_WAIT_SPINS;
        while (spins-- != 0ul) {
            if (v9x_sis_status_busy(
                    v9x_sis_read32(V9X_SIS_2D_CMD_STATUS)) == 0ul) {
                return 1;
            }
        }
    }
    ++v9x_hal->engine.idle_timeouts;
    v9x_trace_flush_fault(0x53324944ul, V9X_SIS_2D_CMD_STATUS);
    return 0;
}

static void v9x_sis_emit(const struct v9x_sis_blt *blt)
{
    DWORD index;

    for (index = 0ul; index < blt->count; ++index) {
        v9x_sis_write32(blt->offsets[index], blt->values[index]);
    }
    v9x_sis_write16(V9X_SIS_2D_COMMAND, blt->command);
    (void)v9x_sis_read32(V9X_SIS_2D_CMD_STATUS);
    v9x_present_note_submission();
}

static int v9x_sis_fill(V9X_DDHAL_BLTDATA *data, DWORD offset,
                        DWORD bytes_per_pixel, int wait)
{
    struct v9x_sis_fill fill;
    struct v9x_sis_blt blt;

    if (!v9x_sis_validate() || data == 0 || data->lpDDDestSurface == 0 ||
        data->lpDDDestSurface->lpGbl == 0 ||
        data->lpDDDestSurface->lpGbl->lPitch <= 0l ||
        data->rDest[2] <= data->rDest[0] ||
        data->rDest[3] <= data->rDest[1] ||
        data->rDest[0] < 0l || data->rDest[1] < 0l) {
        return V9X_BLT_DECLINED;
    }
    fill.vram_bytes = v9x_hal->fb.vram_bytes;
    fill.target_offset = offset;
    fill.pitch_bytes = (DWORD)data->lpDDDestSurface->lpGbl->lPitch;
    fill.bytes_per_pixel = bytes_per_pixel;
    fill.left = (DWORD)data->rDest[0];
    fill.top = (DWORD)data->rDest[1];
    fill.width = (DWORD)(data->rDest[2] - data->rDest[0]);
    fill.height = (DWORD)(data->rDest[3] - data->rDest[1]);
    fill.color = data->bltFX.dwFillColor;
    if (v9x_sis_build_fill(&fill, &blt) != V9X_STATUS_OK) {
        return V9X_BLT_DECLINED;
    }
    if (!v9x_sis_wait_idle(wait)) {
        return V9X_BLT_BUSY;
    }
    v9x_sis_emit(&blt);
    return V9X_BLT_DONE;
}

static int v9x_sis_copy(V9X_DDHAL_BLTDATA *data, DWORD source_offset,
                        DWORD destination_offset, DWORD bytes_per_pixel,
                        int wait)
{
    struct v9x_sis_copy copy;
    struct v9x_sis_blt blt;

    if (!v9x_sis_validate() || data == 0 ||
        data->lpDDSrcSurface == 0 || data->lpDDSrcSurface->lpGbl == 0 ||
        data->lpDDDestSurface == 0 || data->lpDDDestSurface->lpGbl == 0 ||
        data->lpDDSrcSurface->lpGbl->lPitch <= 0l ||
        data->lpDDDestSurface->lpGbl->lPitch <= 0l ||
        data->rSrc[2] <= data->rSrc[0] || data->rSrc[3] <= data->rSrc[1] ||
        data->rSrc[0] < 0l || data->rSrc[1] < 0l ||
        data->rDest[0] < 0l || data->rDest[1] < 0l) {
        return V9X_BLT_DECLINED;
    }
    copy.vram_bytes = v9x_hal->fb.vram_bytes;
    copy.source_offset = source_offset;
    copy.source_pitch_bytes = (DWORD)data->lpDDSrcSurface->lpGbl->lPitch;
    copy.destination_offset = destination_offset;
    copy.destination_pitch_bytes =
        (DWORD)data->lpDDDestSurface->lpGbl->lPitch;
    copy.bytes_per_pixel = bytes_per_pixel;
    copy.source_left = (DWORD)data->rSrc[0];
    copy.source_top = (DWORD)data->rSrc[1];
    copy.destination_left = (DWORD)data->rDest[0];
    copy.destination_top = (DWORD)data->rDest[1];
    copy.width = (DWORD)(data->rSrc[2] - data->rSrc[0]);
    copy.height = (DWORD)(data->rSrc[3] - data->rSrc[1]);
    if (v9x_sis_build_copy(&copy, &blt) != V9X_STATUS_OK) {
        return V9X_BLT_DECLINED;
    }
    if (!v9x_sis_wait_idle(wait)) {
        return V9X_BLT_BUSY;
    }
    v9x_sis_emit(&blt);
    return V9X_BLT_DONE;
}

/* CANBLT is the non-blocking idle poll, as on the Trio. */
static int v9x_sis_can_blt(void)
{
    return v9x_sis_wait_idle(0);
}

const V9X_ENGINE32_OPS v9x_engine32_sis6326 = {
    v9x_sis_ready,
    v9x_sis_validate,
    v9x_sis_validated,
    v9x_sis_can_blt,
    v9x_sis_wait_idle,
    v9x_sis_fill,
    v9x_sis_copy
};
