/*
 * ATI Radeon 9250 / 9200SE (RV280) 2D engine for V9XHAL.DLL: DirectDraw
 * solid fill and screen copy.
 *
 * The RS690's engine module (eng_rs690.c), with the RV280's own flush,
 * frame-buffer location and R200 3D init (for d3d_rv280.c). Registers
 * through the 64 KiB
 * register BAR, which the 16-bit side had the mini-VDD map (rv280_hw16.c).
 * Register values come from the RS690's host-tested builder,
 * src\chipsets\ati\rs690\rs690_2d.c: the legacy Radeon 2D register file,
 * whose init list, fills and copies in every overlap direction matched byte
 * for byte at 8, 16 and 32 bpp on M-HNBJNV's Radeon 9250
 * (docs\decisions\2026-11-15-rv280-2d-write-probe.md).
 *
 * The engine takes direct register writes while the CP's queues are off,
 * which is how VBE leaves them (docs\decisions\2026-11-15-rv280-register-
 * probe.md). Validation checks that, checks CONFIG_MEMSIZE answers through
 * the window, reads the frame buffer's GPU address from MC_FB_LOCATION and
 * the BAR0 window's size from CONFIG_APER_SIZE, then writes the builder's
 * init list once per mode.
 *
 * Idle is RADEONWaitForIdleMMIO's: 64 free FIFO entries and RBBM_STATUS bit
 * 31 clear. Then the destination cache is flushed through
 * RB3D_DSTCACHE_CTLSTAT, the pre-R300 branch of RADEONEngineFlush
 * (xf86-video-ati 6.14.6 radeon_accel.c:195-210); the RS690's DSTCACHE_
 * CTLSTAT at 1714h is the R300 one. The flush is needed only once the engine
 * has drawn, so it is skipped until then.
 *
 * There is no recovery path yet. A timeout raises idle_timeouts, gives the
 * engine up for the rest of the mode and declines the operation to the CPU.
 */
#include "ddhal_internal.h"
#include "velocity9x/rs690_2d.h"
#include "velocity9x/rv280_3d.h"
#include "velocity9x/rv280_crtc.h"

/* Bounded busy-wait, as the RS690's: it only has to tell slow from stuck. */
#define V9X_RV280_WAIT_SPINS 0x00200000ul

/* CANBLT's non-blocking poll. */
#define V9X_RV280_QUICK_READS 16ul

/* The window the mini-VDD maps (V9X_RS690_MMIO_BYTES there). */
#define V9X_RV280_MMIO_BYTES 0x00010000ul

/* CONFIG_MEMSIZE, in bytes, as the mini-VDD checks it before mapping. */
#define V9X_RV280_MEMSIZE_MAX 0x20000000ul

/* radeon_reg.h: RADEON_CONFIG_APER_SIZE, the BAR0 window in bytes (128 MiB
 * on M-HNBJNV); RADEON_MC_FB_LOCATION, 15:0 the frame buffer's GPU start
 * >> 16, decoded as the RS690's HDP_FB_LOCATION is; RADEON_RB3D_DSTCACHE_
 * CTLSTAT with RB3D_DC_FLUSH_ALL and RB3D_DC_BUSY. */
#define V9X_RV280_CONFIG_APER_SIZE   0x0108ul
#define V9X_RV280_MC_FB_LOCATION     0x0148ul
#define V9X_RV280_RB3D_DSTCACHE      0x325cul
#define V9X_RV280_RB3D_DC_FLUSH_ALL  0x0000000ful
#define V9X_RV280_RB3D_DC_BUSY       0x80000000ul

/* Fault-trace tag: "R22D". */
#define V9X_RV280_FAULT_TAG 0x52323244ul

/* Set when the engine has drawn since the destination cache was last
 * flushed; the frame buffer's GPU address and BAR0 window size read at
 * validation. One engine, so one of each: V9XHAL's statics are shared by
 * every process, which is what this needs. */
static int v9x_rv280_dirty;
/* Set when a 2D operation went in since the engine was last seen idle:
 * d3d_rv280.c's draws wait for it first (v9x_rv280_3d_start). */
static int v9x_rv280_2d_pending;
static DWORD v9x_rv280_fb_base;
static DWORD v9x_rv280_aperture;

static int v9x_rv280_ready(void)
{
    return v9x_hal != 0 &&
        (v9x_hal->fb.flags & V9X_DD_FB_VALID) != 0ul &&
        (v9x_hal->engine.flags & V9X_DD_ENGINE_VALID) != 0ul &&
        v9x_hal->engine.engine_type == V9X_DD_ENGINE_TYPE_ATI_RV280 &&
        v9x_hal->engine.control_linear_base != 0ul &&
        v9x_hal->engine.mapped_aperture_bytes >= V9X_RV280_MMIO_BYTES;
}

static DWORD v9x_rv280_read32(DWORD offset)
{
    return *(volatile DWORD *)(v9x_hal->engine.control_linear_base + offset);
}

static void v9x_rv280_write32(DWORD offset, DWORD value)
{
    *(volatile DWORD *)(v9x_hal->engine.control_linear_base + offset) = value;
}

/* Where the surfaces DirectDraw describes start in the frame buffer: their
 * offsets are from the mapped frame buffer, which begins at the desktop,
 * whose physical address lies inside the BAR0 window. VBE puts it at 0. */
static DWORD v9x_rv280_desktop_offset(void)
{
    return v9x_hal->fb.physical_base & (v9x_rv280_aperture - 1ul);
}

/* Non-zero once RBBM_STATUS reads idle within the given number of reads. */
static int v9x_rv280_poll_idle(DWORD reads)
{
    while (reads-- != 0ul) {
        if (v9x_rs690_gui_idle(v9x_rv280_read32(V9X_RS690_RBBM_STATUS)) !=
            0ul) {
            return 1;
        }
    }
    return 0;
}

/* Flush the destination cache and wait, within the given reads, for it to
 * say so. */
static int v9x_rv280_flush(DWORD reads)
{
    v9x_rv280_write32(V9X_RV280_RB3D_DSTCACHE,
                      v9x_rv280_read32(V9X_RV280_RB3D_DSTCACHE) |
                      V9X_RV280_RB3D_DC_FLUSH_ALL);
    while (reads-- != 0ul) {
        if ((v9x_rv280_read32(V9X_RV280_RB3D_DSTCACHE) &
             V9X_RV280_RB3D_DC_BUSY) == 0ul) {
            return 1;
        }
    }
    return 0;
}

static int v9x_rv280_validated(void)
{
    return v9x_rv280_ready() &&
        (v9x_hal->engine.flags & V9X_DD_ENGINE_STATUS_VALIDATED) != 0ul;
}

/*
 * Once per mode: the window must answer as this chip's, the CP must leave
 * the engine to direct writes, the BAR0 window must have a usable size, and
 * the init list goes in on an idle engine. A frame buffer GPU address of 0
 * is valid here (M-HNBJNV reads MC_FB_LOCATION 1FFF0000h), unlike on the
 * RS690. Any refusal leaves the engine unclaimed and DirectDraw on the CPU.
 */
static int v9x_rv280_validate(void)
{
    struct v9x_rs690_blt init;
    struct v9x_rv280_3d_list init3d;
    DWORD memsize;
    DWORD aperture;
    DWORD index;

    if (!v9x_rv280_ready()) {
        return 0;
    }
    if ((v9x_hal->engine.flags & V9X_DD_ENGINE_STATUS_VALIDATED) != 0ul) {
        return 1;
    }
    memsize = v9x_rv280_read32(V9X_RS690_CONFIG_MEMSIZE);
    if (memsize == 0ul || memsize > V9X_RV280_MEMSIZE_MAX) {
        return 0;
    }
    if (v9x_rs690_cp_off(v9x_rv280_read32(V9X_RS690_CP_CSQ_CNTL)) == 0ul) {
        return 0;
    }
    /* A power of two, as a BAR's size is; anything else is not a window
     * this offset arithmetic can use. */
    aperture = v9x_rv280_read32(V9X_RV280_CONFIG_APER_SIZE);
    if (aperture == 0ul || (aperture & (aperture - 1ul)) != 0ul) {
        return 0;
    }
    v9x_rv280_aperture = aperture;
    v9x_rv280_fb_base =
        v9x_rs690_fb_gpu_base(v9x_rv280_read32(V9X_RV280_MC_FB_LOCATION));
    if (v9x_rs690_build_init(v9x_rv280_read32(V9X_RS690_DP_DATATYPE),
                             &init) != V9X_STATUS_OK) {
        return 0;
    }
    if (!v9x_rv280_poll_idle(V9X_RV280_WAIT_SPINS)) {
        return 0;
    }
    for (index = 0ul; index < init.count; ++index) {
        v9x_rv280_write32(init.offsets[index], init.values[index]);
    }
    if (!v9x_rv280_poll_idle(V9X_RV280_WAIT_SPINS)) {
        return 0;
    }
    /* The R200 3D engine's once-per-mode setup, RADEONInit3DEngine's R200
     * branch, for d3d_rv280.c: VBE leaves SE_CNTL and RB3D_CNTL holding
     * values of its own. Twelve writes, inside the FIFO's 64 entries, on
     * the idle engine; the 2D engine was exact after it in every probe run
     * (docs\decisions\2026-11-15-rv280-3d-draw-probe.md). */
    if (v9x_rv280_build_3d_init(&init3d) != V9X_STATUS_OK) {
        return 0;
    }
    for (index = 0ul; index < init3d.count; ++index) {
        v9x_rv280_write32(init3d.offsets[index], init3d.values[index]);
    }
    if (!v9x_rv280_poll_idle(V9X_RV280_WAIT_SPINS)) {
        return 0;
    }
    v9x_rv280_dirty = 0;
    v9x_rv280_2d_pending = 0;
    v9x_hal->engine.flags |= V9X_DD_ENGINE_STATUS_VALIDATED;
    return 1;
}

/*
 * A real timeout gives up on the engine for the rest of the mode, as the
 * RS690's does: the descriptor is invalidated, so v9x_engine32() answers
 * none, blits and fills fall to the CPU, and Lock's drain completes rather
 * than answering WASSTILLDRAWING for ever. The fault trace is written on the
 * first timeout only.
 */
static int v9x_rv280_wait_idle(int wait)
{
    DWORD reads;

    if (!v9x_rv280_validate()) {
        return 0;
    }
    if (wait && v9x_fault_injected()) {
        ++v9x_hal->engine.idle_timeouts;
        v9x_trace_flush_fault(V9X_RV280_FAULT_TAG, V9X_RS690_RBBM_STATUS);
        return 0;
    }
    reads = wait ? V9X_RV280_WAIT_SPINS : V9X_RV280_QUICK_READS;
    if (v9x_rv280_poll_idle(reads) &&
        (!v9x_rv280_dirty || v9x_rv280_flush(reads))) {
        v9x_rv280_dirty = 0;
        v9x_rv280_2d_pending = 0;
        return 1;
    }
    if (!wait) {
        return 0;
    }
    if (v9x_hal->engine.idle_timeouts++ == 0ul) {
        v9x_trace_flush_fault(V9X_RV280_FAULT_TAG, V9X_RS690_RBBM_STATUS);
    }
    v9x_hal->engine.flags &= ~V9X_DD_ENGINE_VALID;
    return 0;
}

static void v9x_rv280_emit(const struct v9x_rs690_blt *blt)
{
    DWORD index;

    /* The source or destination may hold CPU writes still in the
     * write-combining buffers: GDI's, or a surface just unlocked. */
    v9x_wc_drain();
    for (index = 0ul; index < blt->count; ++index) {
        v9x_rv280_write32(blt->offsets[index], blt->values[index]);
    }
    (void)v9x_rv280_read32(V9X_RS690_RBBM_STATUS);
    v9x_rv280_dirty = 1;
    v9x_rv280_2d_pending = 1;
    v9x_present_note_submission();
}

/*
 * A Z buffer the Direct3D engine draws is tiled: the R100/R200 family keeps
 * every depth buffer so, with no control to turn it off, and the layout is
 * not decoded (docs\decisions\2026-11-15-rv280-z-blend-probe.md). A linear
 * rectangle of it is the wrong pixels, but a constant over all of it is the
 * same value in every pixel whatever the tiling. So a fill of the whole
 * surface is widened to the pitch and the rows d3d_rv280.c placed
 * (V9X_RV280_DEPTH_ROW_ALIGN), and a fill of less is declined, which fails
 * it (depth_tiled), as eng_r6.c does for its 1D tiles before its CP path.
 */
static int v9x_rv280_tiled_depth_fill(const V9X_DDHAL_BLTDATA *data,
                                      struct v9x_rs690_fill *fill)
{
    const V9X_DD_SURFACE_LCL *surface = data->lpDDDestSurface;

    if ((surface->ddsCaps & V9X_DDSCAPS_ZBUFFER) == 0ul ||
        !v9x_d3d_depth_tiled()) {
        return 1;
    }
    if (fill->left != 0ul || fill->top != 0ul ||
        fill->width != (DWORD)surface->lpGbl->wWidth ||
        fill->height != (DWORD)surface->lpGbl->wHeight) {
        return 0;
    }
    fill->width = fill->pitch_bytes / fill->bytes_per_pixel;
    fill->height = (fill->height + V9X_RV280_DEPTH_ROW_ALIGN - 1ul) &
                   ~(V9X_RV280_DEPTH_ROW_ALIGN - 1ul);
    return 1;
}

static int v9x_rv280_fill(V9X_DDHAL_BLTDATA *data, DWORD offset,
                          DWORD bytes_per_pixel, int wait)
{
    struct v9x_rs690_fill fill;
    struct v9x_rs690_blt blt;

    if (!v9x_rv280_validate() || data == 0 || data->lpDDDestSurface == 0 ||
        data->lpDDDestSurface->lpGbl == 0 ||
        data->lpDDDestSurface->lpGbl->lPitch <= 0l ||
        data->rDest[2] <= data->rDest[0] ||
        data->rDest[3] <= data->rDest[1] ||
        data->rDest[0] < 0l || data->rDest[1] < 0l) {
        return V9X_BLT_DECLINED;
    }
    fill.fb_gpu_base = v9x_rv280_fb_base;
    fill.fb_bytes = v9x_rv280_desktop_offset() + v9x_hal->fb.vram_bytes;
    fill.target_offset = v9x_rv280_desktop_offset() + offset;
    fill.pitch_bytes = (DWORD)data->lpDDDestSurface->lpGbl->lPitch;
    fill.bytes_per_pixel = bytes_per_pixel;
    fill.left = (DWORD)data->rDest[0];
    fill.top = (DWORD)data->rDest[1];
    fill.width = (DWORD)(data->rDest[2] - data->rDest[0]);
    fill.height = (DWORD)(data->rDest[3] - data->rDest[1]);
    fill.color = data->bltFX.dwFillColor;
    if (!v9x_rv280_tiled_depth_fill(data, &fill) ||
        v9x_rs690_build_fill(&fill, &blt) != V9X_STATUS_OK) {
        return V9X_BLT_DECLINED;
    }
    if (!v9x_rv280_wait_idle(wait)) {
        return V9X_BLT_BUSY;
    }
    v9x_rv280_emit(&blt);
    return V9X_BLT_DONE;
}

static int v9x_rv280_copy(V9X_DDHAL_BLTDATA *data, DWORD source_offset,
                          DWORD destination_offset, DWORD bytes_per_pixel,
                          int wait)
{
    struct v9x_rs690_copy copy;
    struct v9x_rs690_blt blt;

    if (!v9x_rv280_validate() || data == 0 ||
        data->lpDDSrcSurface == 0 || data->lpDDSrcSurface->lpGbl == 0 ||
        data->lpDDDestSurface == 0 || data->lpDDDestSurface->lpGbl == 0 ||
        data->lpDDSrcSurface->lpGbl->lPitch <= 0l ||
        data->lpDDDestSurface->lpGbl->lPitch <= 0l ||
        data->rSrc[2] <= data->rSrc[0] || data->rSrc[3] <= data->rSrc[1] ||
        data->rSrc[0] < 0l || data->rSrc[1] < 0l ||
        data->rDest[0] < 0l || data->rDest[1] < 0l) {
        return V9X_BLT_DECLINED;
    }
    copy.fb_gpu_base = v9x_rv280_fb_base;
    copy.fb_bytes = v9x_rv280_desktop_offset() + v9x_hal->fb.vram_bytes;
    copy.source_offset = v9x_rv280_desktop_offset() + source_offset;
    copy.source_pitch_bytes = (DWORD)data->lpDDSrcSurface->lpGbl->lPitch;
    copy.destination_offset = v9x_rv280_desktop_offset() + destination_offset;
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
    if (!v9x_rv280_wait_idle(wait)) {
        return V9X_BLT_BUSY;
    }
    v9x_rv280_emit(&blt);
    return V9X_BLT_DONE;
}

/*
 * For rv280_scanout.c: the CRTC_OFFSET value, without the lock, for the
 * surface at byte_offset into the mapped frame buffer, once the engine has
 * finished everything drawn so far, so a flip never shows a frame the engine
 * is still writing. Zero when the engine is not validated, does not go idle,
 * or the offset is not one the CRTC can scan (rv280_crtc.c, host-tested).
 */
int v9x_rv280_flip_address(DWORD byte_offset, DWORD *value)
{
    v9x_u32 start;

    if (value == 0 || !v9x_rv280_wait_idle(1)) {
        return 0;
    }
    if (byte_offset > 0xfffffffful - v9x_rv280_desktop_offset() ||
        v9x_rv280_crtc_start(v9x_rv280_fb_base,
                             v9x_rv280_desktop_offset() +
                                 v9x_hal->fb.vram_bytes,
                             v9x_rv280_desktop_offset() + byte_offset,
                             v9x_hal->fb.bits_per_pixel / 8ul,
                             &start) != V9X_STATUS_OK) {
        return 0;
    }
    *value = start;
    return 1;
}

/*
 * For d3d_rv280.c, before a draw: the engine validated (its 2D and 3D init
 * lists written for this mode), idle after any 2D work so a draw never
 * lands on a fill still running, and where the frame buffer is. Marks the
 * engine as having drawn, so the next idle wait flushes the destination
 * cache. As v9x_rs690_3d_begin.
 */
int v9x_rv280_3d_start(DWORD *fb_gpu_base, DWORD *desktop_offset)
{
    if (fb_gpu_base == 0 || desktop_offset == 0 || !v9x_rv280_validate()) {
        return 0;
    }
    if (v9x_rv280_2d_pending && !v9x_rv280_wait_idle(1)) {
        return 0;
    }
    *fb_gpu_base = v9x_rv280_fb_base;
    *desktop_offset = v9x_rv280_desktop_offset();
    v9x_rv280_dirty = 1;
    return 1;
}

/* CANBLT is the non-blocking idle poll, as on the RS690. */
static int v9x_rv280_can_blt(void)
{
    return v9x_rv280_wait_idle(0);
}

const V9X_ENGINE32_OPS v9x_engine32_rv280 = {
    v9x_rv280_ready,
    v9x_rv280_validate,
    v9x_rv280_validated,
    v9x_rv280_can_blt,
    v9x_rv280_wait_idle,
    v9x_rv280_fill,
    v9x_rv280_copy
};
