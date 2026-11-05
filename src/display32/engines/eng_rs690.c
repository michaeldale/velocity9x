/*
 * ATI Radeon X1200 IGP (RS690) 2D engine for V9XHAL.DLL: DirectDraw solid
 * fill and screen copy.
 *
 * Registers through the 64 KiB register BAR, which the 16-bit side had the
 * mini-VDD map (rs690_hw16.c). Register values come from the host-tested
 * builder, src\chipsets\ati\rs690\rs690_2d.c, whose init list, fills and
 * copies in every overlap direction matched byte for byte at 8, 16 and 32
 * bpp on M-GB8KRE (docs\decisions\2026-11-04-rs690-2d-write-probe.md).
 * eng_r128.c is the model; the register file is the Radeon one.
 *
 * The engine takes direct register writes while the CP's queues are off,
 * which is how VBE leaves them and how nothing in this driver changes them
 * (docs\decisions\2026-11-04-rs690-register-probe.md). Validation checks
 * that, checks CONFIG_MEMSIZE answers through the window (the RS690 keeps
 * no configuration copy to check instead), reads the frame buffer's GPU
 * address from HDP_FB_LOCATION, then writes the builder's init list once
 * per mode.
 *
 * Idle is RADEONWaitForIdleMMIO's: 64 free FIFO entries and RBBM_STATUS
 * bit 31 clear, then the 2D destination cache flushed (the R300 branch of
 * RADEONEngineFlush). The flush is needed only once the engine has drawn,
 * so it is skipped until then.
 *
 * There is no recovery path yet. A timeout raises idle_timeouts, gives the
 * engine up for the rest of the mode and declines the operation to the CPU.
 */
#include "ddhal_internal.h"
#include "velocity9x/rs690_2d.h"

/* Bounded busy-wait. The write probe measured 5-11 status reads per small
 * operation; this bound only has to tell slow from stuck. */
#define V9X_RS690_WAIT_SPINS 0x00200000ul

/* CANBLT's non-blocking poll: a few reads, so a flush that is about to
 * finish still lets the next blit go. */
#define V9X_RS690_QUICK_READS 16ul

/* The window the mini-VDD maps (V9X_RS690_MMIO_BYTES there). */
#define V9X_RS690_MMIO_BYTES 0x00010000ul

/* CONFIG_MEMSIZE, in bytes, as the mini-VDD checks it before mapping. */
#define V9X_RS690_MEMSIZE_MAX 0x20000000ul

/* CONFIG_APER_SIZE on M-GB8KRE: the frame buffer BAR0 shows the CPU. The
 * desktop's offset into the frame buffer is its physical address within
 * that window; VBE puts it at 0. */
#define V9X_RS690_APERTURE_BYTES 0x04000000ul

/* Fault-trace tag: "RS2D". */
#define V9X_RS690_FAULT_TAG 0x52533244ul

/* Set when the engine has drawn since the destination cache was last
 * flushed, and the frame buffer's GPU address read at validation. One
 * engine, so one of each: V9XHAL's statics are shared by every process,
 * which is what this needs. */
static int v9x_rs690_dirty;
static DWORD v9x_rs690_fb_base;

static int v9x_rs690_ready(void)
{
    return v9x_hal != 0 &&
        (v9x_hal->fb.flags & V9X_DD_FB_VALID) != 0ul &&
        (v9x_hal->engine.flags & V9X_DD_ENGINE_VALID) != 0ul &&
        v9x_hal->engine.engine_type == V9X_DD_ENGINE_TYPE_ATI_RS690 &&
        v9x_hal->engine.control_linear_base != 0ul &&
        v9x_hal->engine.mapped_aperture_bytes >= V9X_RS690_MMIO_BYTES;
}

static DWORD v9x_rs690_read32(DWORD offset)
{
    return *(volatile DWORD *)(v9x_hal->engine.control_linear_base + offset);
}

static void v9x_rs690_write32(DWORD offset, DWORD value)
{
    *(volatile DWORD *)(v9x_hal->engine.control_linear_base + offset) = value;
}

/* Where the surfaces DirectDraw describes start in the frame buffer: their
 * offsets are from the mapped frame buffer, which begins at the desktop. */
static DWORD v9x_rs690_desktop_offset(void)
{
    return v9x_hal->fb.physical_base & (V9X_RS690_APERTURE_BYTES - 1ul);
}

/* Non-zero once RBBM_STATUS reads idle within the given number of reads. */
static int v9x_rs690_poll_idle(DWORD reads)
{
    while (reads-- != 0ul) {
        if (v9x_rs690_gui_idle(v9x_rs690_read32(V9X_RS690_RBBM_STATUS)) !=
            0ul) {
            return 1;
        }
    }
    return 0;
}

/* Flush and free the 2D destination cache and wait, within the given
 * reads, for it to say so. */
static int v9x_rs690_flush(DWORD reads)
{
    v9x_rs690_write32(V9X_RS690_DSTCACHE_CTLSTAT,
                      v9x_rs690_read32(V9X_RS690_DSTCACHE_CTLSTAT) |
                      V9X_RS690_DC_FLUSH_ALL);
    while (reads-- != 0ul) {
        if ((v9x_rs690_read32(V9X_RS690_DSTCACHE_CTLSTAT) &
             V9X_RS690_DC_BUSY) == 0ul) {
            return 1;
        }
    }
    return 0;
}

static int v9x_rs690_validated(void)
{
    return v9x_rs690_ready() &&
        (v9x_hal->engine.flags & V9X_DD_ENGINE_STATUS_VALIDATED) != 0ul;
}

/*
 * Once per mode: the window must answer as this chip's, the CP must leave
 * the engine to direct writes, the frame buffer must have a GPU address,
 * and the init list goes in on an idle engine. Any refusal leaves the
 * engine unclaimed and DirectDraw on the CPU.
 */
static int v9x_rs690_validate(void)
{
    struct v9x_rs690_blt init;
    DWORD memsize;
    DWORD index;

    if (!v9x_rs690_ready()) {
        return 0;
    }
    if ((v9x_hal->engine.flags & V9X_DD_ENGINE_STATUS_VALIDATED) != 0ul) {
        return 1;
    }
    memsize = v9x_rs690_read32(V9X_RS690_CONFIG_MEMSIZE);
    if (memsize == 0ul || memsize > V9X_RS690_MEMSIZE_MAX) {
        return 0;
    }
    if (v9x_rs690_cp_off(v9x_rs690_read32(V9X_RS690_CP_CSQ_CNTL)) == 0ul) {
        return 0;
    }
    v9x_rs690_fb_base =
        v9x_rs690_fb_gpu_base(v9x_rs690_read32(V9X_RS690_HDP_FB_LOCATION));
    if (v9x_rs690_fb_base == 0ul) {
        return 0;
    }
    if (v9x_rs690_build_init(v9x_rs690_read32(V9X_RS690_DP_DATATYPE),
                             &init) != V9X_STATUS_OK) {
        return 0;
    }
    if (!v9x_rs690_poll_idle(V9X_RS690_WAIT_SPINS)) {
        return 0;
    }
    for (index = 0ul; index < init.count; ++index) {
        v9x_rs690_write32(init.offsets[index], init.values[index]);
    }
    if (!v9x_rs690_poll_idle(V9X_RS690_WAIT_SPINS)) {
        return 0;
    }
    v9x_rs690_dirty = 0;
    v9x_hal->engine.flags |= V9X_DD_ENGINE_STATUS_VALIDATED;
    return 1;
}

/*
 * A real timeout gives up on the engine for the rest of the mode, as the
 * Rage 128's does: the descriptor is invalidated, so v9x_engine32() answers
 * none, blits and fills fall to the CPU, and Lock's drain completes rather
 * than answering WASSTILLDRAWING for ever. The fault trace is written on
 * the first timeout only.
 */
static int v9x_rs690_wait_idle(int wait)
{
    DWORD reads;

    if (!v9x_rs690_validate()) {
        return 0;
    }
    if (wait && v9x_fault_injected()) {
        ++v9x_hal->engine.idle_timeouts;
        v9x_trace_flush_fault(V9X_RS690_FAULT_TAG, V9X_RS690_RBBM_STATUS);
        return 0;
    }
    reads = wait ? V9X_RS690_WAIT_SPINS : V9X_RS690_QUICK_READS;
    if (v9x_rs690_poll_idle(reads) &&
        (!v9x_rs690_dirty || v9x_rs690_flush(reads))) {
        v9x_rs690_dirty = 0;
        return 1;
    }
    if (!wait) {
        return 0;
    }
    if (v9x_hal->engine.idle_timeouts++ == 0ul) {
        v9x_trace_flush_fault(V9X_RS690_FAULT_TAG, V9X_RS690_RBBM_STATUS);
    }
    v9x_hal->engine.flags &= ~V9X_DD_ENGINE_VALID;
    return 0;
}

static void v9x_rs690_emit(const struct v9x_rs690_blt *blt)
{
    DWORD index;

    /* The source or destination may hold CPU writes still in the
     * write-combining buffers: GDI's, or a surface just unlocked. */
    v9x_wc_drain();
    for (index = 0ul; index < blt->count; ++index) {
        v9x_rs690_write32(blt->offsets[index], blt->values[index]);
    }
    (void)v9x_rs690_read32(V9X_RS690_RBBM_STATUS);
    v9x_rs690_dirty = 1;
    v9x_present_note_submission();
}

static int v9x_rs690_fill(V9X_DDHAL_BLTDATA *data, DWORD offset,
                          DWORD bytes_per_pixel, int wait)
{
    struct v9x_rs690_fill fill;
    struct v9x_rs690_blt blt;

    if (!v9x_rs690_validate() || data == 0 || data->lpDDDestSurface == 0 ||
        data->lpDDDestSurface->lpGbl == 0 ||
        data->lpDDDestSurface->lpGbl->lPitch <= 0l ||
        data->rDest[2] <= data->rDest[0] ||
        data->rDest[3] <= data->rDest[1] ||
        data->rDest[0] < 0l || data->rDest[1] < 0l) {
        return V9X_BLT_DECLINED;
    }
    fill.fb_gpu_base = v9x_rs690_fb_base;
    fill.fb_bytes = v9x_rs690_desktop_offset() + v9x_hal->fb.vram_bytes;
    fill.target_offset = v9x_rs690_desktop_offset() + offset;
    fill.pitch_bytes = (DWORD)data->lpDDDestSurface->lpGbl->lPitch;
    fill.bytes_per_pixel = bytes_per_pixel;
    fill.left = (DWORD)data->rDest[0];
    fill.top = (DWORD)data->rDest[1];
    fill.width = (DWORD)(data->rDest[2] - data->rDest[0]);
    fill.height = (DWORD)(data->rDest[3] - data->rDest[1]);
    fill.color = data->bltFX.dwFillColor;
    if (v9x_rs690_build_fill(&fill, &blt) != V9X_STATUS_OK) {
        return V9X_BLT_DECLINED;
    }
    if (!v9x_rs690_wait_idle(wait)) {
        return V9X_BLT_BUSY;
    }
    v9x_rs690_emit(&blt);
    return V9X_BLT_DONE;
}

static int v9x_rs690_copy(V9X_DDHAL_BLTDATA *data, DWORD source_offset,
                          DWORD destination_offset, DWORD bytes_per_pixel,
                          int wait)
{
    struct v9x_rs690_copy copy;
    struct v9x_rs690_blt blt;

    if (!v9x_rs690_validate() || data == 0 ||
        data->lpDDSrcSurface == 0 || data->lpDDSrcSurface->lpGbl == 0 ||
        data->lpDDDestSurface == 0 || data->lpDDDestSurface->lpGbl == 0 ||
        data->lpDDSrcSurface->lpGbl->lPitch <= 0l ||
        data->lpDDDestSurface->lpGbl->lPitch <= 0l ||
        data->rSrc[2] <= data->rSrc[0] || data->rSrc[3] <= data->rSrc[1] ||
        data->rSrc[0] < 0l || data->rSrc[1] < 0l ||
        data->rDest[0] < 0l || data->rDest[1] < 0l) {
        return V9X_BLT_DECLINED;
    }
    copy.fb_gpu_base = v9x_rs690_fb_base;
    copy.fb_bytes = v9x_rs690_desktop_offset() + v9x_hal->fb.vram_bytes;
    copy.source_offset = v9x_rs690_desktop_offset() + source_offset;
    copy.source_pitch_bytes = (DWORD)data->lpDDSrcSurface->lpGbl->lPitch;
    copy.destination_offset = v9x_rs690_desktop_offset() + destination_offset;
    copy.destination_pitch_bytes =
        (DWORD)data->lpDDDestSurface->lpGbl->lPitch;
    copy.bytes_per_pixel = bytes_per_pixel;
    copy.source_left = (DWORD)data->rSrc[0];
    copy.source_top = (DWORD)data->rSrc[1];
    copy.destination_left = (DWORD)data->rDest[0];
    copy.destination_top = (DWORD)data->rDest[1];
    copy.width = (DWORD)(data->rSrc[2] - data->rSrc[0]);
    copy.height = (DWORD)(data->rSrc[3] - data->rSrc[1]);
    if (v9x_rs690_build_copy(&copy, &blt) != V9X_STATUS_OK) {
        return V9X_BLT_DECLINED;
    }
    if (!v9x_rs690_wait_idle(wait)) {
        return V9X_BLT_BUSY;
    }
    v9x_rs690_emit(&blt);
    return V9X_BLT_DONE;
}

/*
 * For rs690_scanout.c: D1GRPH's address for the surface at byte_offset
 * into the mapped frame buffer, once the engine has finished everything
 * drawn so far, so a flip never shows a frame the engine is still
 * writing. Zero when the engine is not validated, does not go idle, or
 * the offset is not one the controller can scan.
 */
int v9x_rs690_flip_address(DWORD byte_offset, DWORD *address)
{
    v9x_u32 gpu;

    if (address == 0 || !v9x_rs690_wait_idle(1)) {
        return 0;
    }
    if (byte_offset > 0xfffffffful - v9x_rs690_desktop_offset() ||
        v9x_rs690_scanout_address(v9x_rs690_fb_base,
                                  v9x_rs690_desktop_offset() +
                                      v9x_hal->fb.vram_bytes,
                                  v9x_rs690_desktop_offset() + byte_offset,
                                  &gpu) != V9X_STATUS_OK) {
        return 0;
    }
    *address = gpu;
    return 1;
}

/* CANBLT is the non-blocking idle poll, as on the Rage 128. */
static int v9x_rs690_can_blt(void)
{
    return v9x_rs690_wait_idle(0);
}

const V9X_ENGINE32_OPS v9x_engine32_rs690 = {
    v9x_rs690_ready,
    v9x_rs690_validate,
    v9x_rs690_validated,
    v9x_rs690_can_blt,
    v9x_rs690_wait_idle,
    v9x_rs690_fill,
    v9x_rs690_copy
};
