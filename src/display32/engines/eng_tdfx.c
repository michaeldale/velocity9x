/*
 * 3dfx VSA-100 2D engine for V9XHAL.DLL: DirectDraw solid fill and screen
 * copy.
 *
 * Registers through BAR0's 4 KiB 2D block, which the 16-bit side had the
 * mini-VDD map (vsa100_hw16.c); offset 0 of the block aliases the status
 * register. Register values come from the host-tested builder,
 * src\chipsets\tdfx\tdfx_2d.c, whose fill, forward copy and both overlap
 * directions matched byte for byte on the Voodoo4 4500 at 16 bpp
 * (docs\decisions\2026-10-11-voodoo4-2d-engine-fills-and-copies-pixel-
 * exact.md).
 *
 * Each operation: wait for the engine to be idle, write the builder's
 * dwords (the last is the command, which starts it), then read status back
 * so the writes are posted before the HAL returns.
 *
 * There is no recovery path yet. A timeout raises idle_timeouts, gives the
 * engine up for the rest of the mode and declines the operation to the CPU.
 */
#include "ddhal_internal.h"
#include "velocity9x/tdfx_2d.h"
#include "velocity9x/tdfx_vsa100.h"

/* Bounded busy-wait. The write probe measured 5-10 status reads per
 * operation on small rectangles; a full 1280x1024x16 fill is far longer,
 * and this bound only has to tell slow from stuck. At about a microsecond a
 * read it is a few seconds. */
#define V9X_TDFX_WAIT_SPINS 0x00200000ul

/* Idle is three consecutive reads with the busy bit clear
 * (docs\specifications\vsa100-registers.md, status). */
#define V9X_TDFX_IDLE_READS 3ul

/* The window the mini-VDD maps (V9X_TDFX_MMIO_BYTES there). */
#define V9X_TDFX_MMIO_BYTES 0x00001000ul

/* Fault-trace tag: "TX2D". */
#define V9X_TDFX_FAULT_TAG 0x54583244ul

static int v9x_tdfx_ready(void)
{
    return v9x_hal != 0 &&
        (v9x_hal->fb.flags & V9X_DD_FB_VALID) != 0ul &&
        (v9x_hal->engine.flags & V9X_DD_ENGINE_VALID) != 0ul &&
        v9x_hal->engine.engine_type == V9X_DD_ENGINE_TYPE_TDFX &&
        v9x_hal->engine.control_linear_base != 0ul &&
        v9x_hal->engine.mapped_aperture_bytes >= V9X_TDFX_MMIO_BYTES;
}

static DWORD v9x_tdfx_read32(DWORD offset)
{
    return *(volatile DWORD *)(v9x_hal->engine.control_linear_base + offset);
}

static void v9x_tdfx_write32(DWORD offset, DWORD value)
{
    *(volatile DWORD *)(v9x_hal->engine.control_linear_base + offset) = value;
}

/*
 * Where the surfaces DirectDraw describes start in video memory. Their
 * offsets are from the framebuffer the driver maps, which begins at the
 * desktop, and the 16-bit side places the desktop above the memory Glide is
 * given (tdfx_hw16.c); the engine addresses video memory from its first
 * byte. The mapped physical base is inside BAR1, which is 128 MiB aligned,
 * so the desktop's offset is what remains of it below that alignment.
 */
static DWORD v9x_tdfx_desktop_offset(void)
{
    return v9x_hal->fb.physical_base & (V9X_TDFX_LFB_BAR_BYTES - 1ul);
}

/* A block whose decode was turned off since the map reads all ones. */
static int v9x_tdfx_validate(void)
{
    if (!v9x_tdfx_ready()) {
        return 0;
    }
    if ((v9x_hal->engine.flags & V9X_DD_ENGINE_STATUS_VALIDATED) != 0ul) {
        return 1;
    }
    if (v9x_tdfx_read32(V9X_TDFX_2D_STATUS) == 0xfffffffful) {
        return 0;
    }
    v9x_hal->engine.flags |= V9X_DD_ENGINE_STATUS_VALIDATED;
    return 1;
}

static int v9x_tdfx_validated(void)
{
    return v9x_tdfx_ready() &&
        (v9x_hal->engine.flags & V9X_DD_ENGINE_STATUS_VALIDATED) != 0ul;
}

/* Non-zero after V9X_TDFX_IDLE_READS consecutive clear reads within the
 * given number of reads. */
static int v9x_tdfx_poll_idle(DWORD reads)
{
    DWORD clear = 0ul;

    while (reads-- != 0ul) {
        if (v9x_tdfx_status_busy(v9x_tdfx_read32(V9X_TDFX_2D_STATUS)) ==
            0ul) {
            if (++clear >= V9X_TDFX_IDLE_READS) {
                return 1;
            }
        } else {
            clear = 0ul;
        }
    }
    return 0;
}

/*
 * A real timeout gives up on the engine for the rest of the mode, as the
 * SiS engine learned to: the descriptor is invalidated, so v9x_engine32()
 * answers none, blits and fills fall to the CPU, and Lock's drain completes
 * rather than answering WASSTILLDRAWING for ever. The fault trace is written
 * on the first timeout only.
 */
static int v9x_tdfx_wait_idle(int wait)
{
    if (!v9x_tdfx_validate()) {
        return 0;
    }
    if (!(wait && v9x_fault_injected())) {
        if (v9x_tdfx_poll_idle(V9X_TDFX_IDLE_READS)) {
            return 1;
        }
        if (!wait) {
            return 0;
        }
        if (v9x_tdfx_poll_idle(V9X_TDFX_WAIT_SPINS)) {
            return 1;
        }
        if (v9x_hal->engine.idle_timeouts++ == 0ul) {
            v9x_trace_flush_fault(V9X_TDFX_FAULT_TAG, V9X_TDFX_2D_STATUS);
        }
        v9x_hal->engine.flags &= ~V9X_DD_ENGINE_VALID;
        return 0;
    }
    ++v9x_hal->engine.idle_timeouts;
    v9x_trace_flush_fault(V9X_TDFX_FAULT_TAG, V9X_TDFX_2D_STATUS);
    return 0;
}

static void v9x_tdfx_emit(const struct v9x_tdfx_blt *blt)
{
    DWORD index;

    for (index = 0ul; index < blt->count; ++index) {
        v9x_tdfx_write32(blt->offsets[index], blt->values[index]);
    }
    (void)v9x_tdfx_read32(V9X_TDFX_2D_STATUS);
    v9x_present_note_submission();
}

static int v9x_tdfx_fill(V9X_DDHAL_BLTDATA *data, DWORD offset,
                         DWORD bytes_per_pixel, int wait)
{
    struct v9x_tdfx_fill fill;
    struct v9x_tdfx_blt blt;

    if (!v9x_tdfx_validate() || data == 0 || data->lpDDDestSurface == 0 ||
        data->lpDDDestSurface->lpGbl == 0 ||
        data->lpDDDestSurface->lpGbl->lPitch <= 0l ||
        data->rDest[2] <= data->rDest[0] ||
        data->rDest[3] <= data->rDest[1] ||
        data->rDest[0] < 0l || data->rDest[1] < 0l) {
        return V9X_BLT_DECLINED;
    }
    fill.vram_bytes = v9x_tdfx_desktop_offset() + v9x_hal->fb.vram_bytes;
    fill.target_offset = v9x_tdfx_desktop_offset() + offset;
    fill.pitch_bytes = (DWORD)data->lpDDDestSurface->lpGbl->lPitch;
    fill.bytes_per_pixel = bytes_per_pixel;
    fill.left = (DWORD)data->rDest[0];
    fill.top = (DWORD)data->rDest[1];
    fill.width = (DWORD)(data->rDest[2] - data->rDest[0]);
    fill.height = (DWORD)(data->rDest[3] - data->rDest[1]);
    fill.color = data->bltFX.dwFillColor;
    fill.rop = V9X_TDFX_ROP_SRCCOPY;
    if (v9x_tdfx_build_fill(&fill, &blt) != V9X_STATUS_OK) {
        return V9X_BLT_DECLINED;
    }
    if (!v9x_tdfx_wait_idle(wait)) {
        return V9X_BLT_BUSY;
    }
    v9x_tdfx_emit(&blt);
    return V9X_BLT_DONE;
}

static int v9x_tdfx_copy(V9X_DDHAL_BLTDATA *data, DWORD source_offset,
                         DWORD destination_offset, DWORD bytes_per_pixel,
                         int wait)
{
    struct v9x_tdfx_copy copy;
    struct v9x_tdfx_blt blt;

    if (!v9x_tdfx_validate() || data == 0 ||
        data->lpDDSrcSurface == 0 || data->lpDDSrcSurface->lpGbl == 0 ||
        data->lpDDDestSurface == 0 || data->lpDDDestSurface->lpGbl == 0 ||
        data->lpDDSrcSurface->lpGbl->lPitch <= 0l ||
        data->lpDDDestSurface->lpGbl->lPitch <= 0l ||
        data->rSrc[2] <= data->rSrc[0] || data->rSrc[3] <= data->rSrc[1] ||
        data->rSrc[0] < 0l || data->rSrc[1] < 0l ||
        data->rDest[0] < 0l || data->rDest[1] < 0l) {
        return V9X_BLT_DECLINED;
    }
    copy.vram_bytes = v9x_tdfx_desktop_offset() + v9x_hal->fb.vram_bytes;
    copy.source_offset = v9x_tdfx_desktop_offset() + source_offset;
    copy.source_pitch_bytes = (DWORD)data->lpDDSrcSurface->lpGbl->lPitch;
    copy.destination_offset = v9x_tdfx_desktop_offset() + destination_offset;
    copy.destination_pitch_bytes =
        (DWORD)data->lpDDDestSurface->lpGbl->lPitch;
    copy.bytes_per_pixel = bytes_per_pixel;
    copy.source_left = (DWORD)data->rSrc[0];
    copy.source_top = (DWORD)data->rSrc[1];
    copy.destination_left = (DWORD)data->rDest[0];
    copy.destination_top = (DWORD)data->rDest[1];
    copy.width = (DWORD)(data->rSrc[2] - data->rSrc[0]);
    copy.height = (DWORD)(data->rSrc[3] - data->rSrc[1]);
    if (v9x_tdfx_build_copy(&copy, &blt) != V9X_STATUS_OK) {
        return V9X_BLT_DECLINED;
    }
    if (!v9x_tdfx_wait_idle(wait)) {
        return V9X_BLT_BUSY;
    }
    v9x_tdfx_emit(&blt);
    return V9X_BLT_DONE;
}

/* CANBLT is the non-blocking idle poll, as on the SiS and the Trio. */
static int v9x_tdfx_can_blt(void)
{
    return v9x_tdfx_wait_idle(0);
}

const V9X_ENGINE32_OPS v9x_engine32_tdfx = {
    v9x_tdfx_ready,
    v9x_tdfx_validate,
    v9x_tdfx_validated,
    v9x_tdfx_can_blt,
    v9x_tdfx_wait_idle,
    v9x_tdfx_fill,
    v9x_tdfx_copy
};
