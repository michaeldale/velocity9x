/*
 * Intel Gen7.5 (Haswell) blitter for V9XHAL.DLL: DirectDraw solid fill and
 * screen copy on the BCS ring. Phase B2 of docs\plans\intel-haswell-
 * direct3d.md.
 *
 * The ring and its status page were opened by the mini-VDD at the reserve
 * the 16-bit side placed (engine.reserve_offset), under a forcewake it holds
 * for the engine's lifetime, so nothing here touches power management.
 * Commands come from the host-tested builders in src\chipsets\intel\
 * hsw_3d.c; each submission ends with MI_STORE_DATA_IMM of a sequence
 * number into status dword 0, the store B1 measured landing.
 *
 * No state lives in this DLL. It is mapped into every DirectDraw process,
 * so the tail is read from the hardware and the submitted sequence is kept
 * in status dword 1, where every process sees the same value; DirectDraw
 * serialises HAL calls. Each operation waits for the previous one first,
 * as the 3dfx engine does, which also means HEAD equals TAIL whenever a new
 * submission is written.
 *
 * A timeout gives the engine up for the rest of the mode and declines to
 * the CPU. There is no reset (plan: "no reset").
 */
#include "ddhal_internal.h"
#include "velocity9x/intel_hsw.h"
#include "velocity9x/intel_hsw_3d.h"

/* Status reads at about a microsecond; long enough for a full-screen 32 bpp
 * fill, short enough to tell stuck from slow. */
#define V9X_HSW_WAIT_SPINS 0x00200000ul

/* The largest submission: an 8-dword copy plus the 4-dword store. */
#define V9X_HSW_SUBMIT_MAX 16ul
#define V9X_HSW_STORE_DWORDS 4ul

/* Fault-trace tag: "HSWB". */
#define V9X_HSW_FAULT_TAG 0x48535742ul

/* The framebuffer is at aperture offset 0 (VBE's LFB is GMADR); graphics
 * addresses are aperture offsets, so a surface's is its offset in the
 * mapped framebuffer plus the framebuffer's place in a 256 MB aperture. */
#define V9X_HSW_APERTURE_MASK 0x0ffffffful

/* Blitter coordinates are signed 16-bit, pitches 16-bit (Vol 2b). */
#define V9X_HSW_COORD_MAX 0x7ffful
#define V9X_HSW_PITCH_MAX 0xfffful

static int v9x_hsw_ready(void)
{
    return v9x_hal != 0 &&
        (v9x_hal->fb.flags & V9X_DD_FB_VALID) != 0ul &&
        (v9x_hal->engine.flags & V9X_DD_ENGINE_VALID) != 0ul &&
        v9x_hal->engine.engine_type == V9X_DD_ENGINE_TYPE_INTEL_GEN75 &&
        v9x_hal->engine.control_linear_base != 0ul &&
        v9x_hal->engine.ring_linear_base != 0ul &&
        v9x_hal->engine.ring_bytes == V9X_INTEL_HSW_RING_BYTES;
}

static DWORD v9x_hsw_read32(DWORD offset)
{
    return *(volatile DWORD *)(v9x_hal->engine.control_linear_base + offset);
}

static void v9x_hsw_write32(DWORD offset, DWORD value)
{
    *(volatile DWORD *)(v9x_hal->engine.control_linear_base + offset) = value;
}

static volatile DWORD *v9x_hsw_ring(void)
{
    return (volatile DWORD *)v9x_hal->engine.ring_linear_base;
}

static volatile DWORD *v9x_hsw_status(void)
{
    return (volatile DWORD *)(v9x_hal->engine.ring_linear_base +
                              v9x_hal->engine.ring_bytes);
}

/* The ring must still be enabled at the reserve; a mode set or anything
 * else that turned it off since open makes the engine unusable. */
static int v9x_hsw_validate(void)
{
    if (!v9x_hsw_ready()) {
        return 0;
    }
    if ((v9x_hal->engine.flags & V9X_DD_ENGINE_STATUS_VALIDATED) != 0ul) {
        return 1;
    }
    if (v9x_hal->engine.reserve_offset == 0ul ||
        (v9x_hsw_read32(V9X_INTEL_HSW_BCS_CTL) & V9X_INTEL_HSW_CTL_ENABLE) ==
            0ul ||
        v9x_hsw_read32(V9X_INTEL_HSW_BCS_START) !=
            v9x_hal->engine.reserve_offset) {
        return 0;
    }
    v9x_hal->engine.flags |= V9X_DD_ENGINE_STATUS_VALIDATED;
    return 1;
}

static int v9x_hsw_validated(void)
{
    return v9x_hsw_ready() &&
        (v9x_hal->engine.flags & V9X_DD_ENGINE_STATUS_VALIDATED) != 0ul;
}

/*
 * Idle: every sequence submitted to either ring has been stored back.
 *
 * The render ring's pair is in the same status page (dwords 8 and 9, which
 * d3d_hsw.c keeps), so this reads nothing new. Including it here is what
 * makes every caller of this test - Flip, Lock, Blt, GetBltStatus,
 * v9x_render_drain - wait for draws as well as blits. The two rings are not
 * ordered against each other, so a blit must not start while the render
 * engine may still read or write its surfaces, and the CPU must not touch a
 * surface it is drawing. Phase 2 of docs\plans\intel-haswell-pipelined-
 * submission.md: d3d_hsw.c still waits for each draw, so today the render
 * pair always agrees here and this changes nothing observable; it is what
 * lets later draws be left running. A ring that was never opened leaves
 * both dwords zero, which agree.
 */
static int v9x_hsw_render_idle_now(void)
{
    volatile DWORD *status = v9x_hsw_status();

    return status[V9X_INTEL_HSW_STATUS_RCS_DONE] ==
           status[V9X_INTEL_HSW_STATUS_RCS_SENT];
}

static int v9x_hsw_idle_now(void)
{
    volatile DWORD *status = v9x_hsw_status();

    return status[0] == status[1] && v9x_hsw_render_idle_now();
}

/* The head a stuck wait is traced with: the render ring's when it is the
 * one still busy, so a capture names the engine that did not finish. */
static DWORD v9x_hsw_busy_head(void)
{
    if (!v9x_hsw_render_idle_now()) {
        return v9x_hsw_read32(V9X_INTEL_HSW_RCS_HEAD);
    }
    return v9x_hsw_read32(V9X_INTEL_HSW_BCS_HEAD);
}

static int v9x_hsw_wait_idle(int wait)
{
    DWORD spins;

    if (!v9x_hsw_validate()) {
        return 0;
    }
    if (!(wait && v9x_fault_injected())) {
        if (v9x_hsw_idle_now()) {
            return 1;
        }
        if (!wait) {
            return 0;
        }
        for (spins = 0ul; spins < V9X_HSW_WAIT_SPINS; ++spins) {
            if (v9x_hsw_idle_now()) {
                return 1;
            }
        }
        if (v9x_hal->engine.idle_timeouts++ == 0ul) {
            v9x_trace_flush_fault(V9X_HSW_FAULT_TAG, v9x_hsw_busy_head());
        }
        v9x_hal->engine.flags &= ~V9X_DD_ENGINE_VALID;
        return 0;
    }
    ++v9x_hal->engine.idle_timeouts;
    v9x_trace_flush_fault(V9X_HSW_FAULT_TAG, v9x_hsw_busy_head());
    return 0;
}

/*
 * Append count dwords (even) and the sequence store, then move TAIL. Called
 * idle, so HEAD equals TAIL: if the rest of the ring is too short, it is
 * filled with MI_NOOP (0) and the commands start at 0, and the blitter
 * walks the no-ops and wraps to them.
 */
static void v9x_hsw_submit(const v9x_u32 *dw, DWORD count)
{
    volatile DWORD *ring = v9x_hsw_ring();
    volatile DWORD *status = v9x_hsw_status();
    v9x_u32 store[V9X_HSW_STORE_DWORDS];
    struct v9x_hsw_batch batch;
    DWORD tail;
    DWORD total;
    DWORD index;
    DWORD sequence;

    /*
     * The sequence is stored by MI_FLUSH_DW's post-sync write, not by
     * MI_STORE_DATA_IMM: only the flush waits for the blit's writes to
     * reach memory before the store lands. With the plain store the status
     * pair agreed while a 32 bpp copy was still writing, so the render
     * engine drew under its tail and GDI read the frame before (the HD 4600,
     * 2026-10-21, docs\issues\2026-10-21-haswell-gl-present-at-32-bpp-
     * half-width.md). The status page's first dword is qword aligned.
     */
    sequence = status[1] + 1ul;
    v9x_intel_hsw_batch_init(&batch, store, V9X_HSW_STORE_DWORDS);
    v9x_intel_hsw_mi_flush_dw_store(&batch,
                                    v9x_hal->engine.reserve_offset +
                                        V9X_INTEL_HSW_RING_BYTES,
                                    sequence);

    total = (count + V9X_HSW_STORE_DWORDS) * 4ul;
    tail = v9x_hsw_read32(V9X_INTEL_HSW_BCS_TAIL) & V9X_INTEL_HSW_TAIL_MASK;
    if (tail + total > v9x_hal->engine.ring_bytes) {
        for (index = tail / 4ul; index < v9x_hal->engine.ring_bytes / 4ul;
             ++index) {
            ring[index] = 0ul;
        }
        tail = 0ul;
    }
    for (index = 0ul; index < count; ++index) {
        ring[tail / 4ul + index] = dw[index];
    }
    for (index = 0ul; index < V9X_HSW_STORE_DWORDS; ++index) {
        ring[tail / 4ul + count + index] = store[index];
    }
    status[1] = sequence;
    tail += total;
    if (tail >= v9x_hal->engine.ring_bytes) {
        tail = 0ul;
    }
    /* The ring words and the surfaces out of any write-combining buffers
     * before TAIL tells the blitter to read them (d3d_i9xx.c, same rule). */
    v9x_wc_drain();
    v9x_hsw_write32(V9X_INTEL_HSW_BCS_TAIL, tail);
    (void)v9x_hsw_read32(V9X_INTEL_HSW_BCS_TAIL);
    v9x_present_note_submission();
}

static DWORD v9x_hsw_address(DWORD offset)
{
    return (v9x_hal->fb.physical_base & V9X_HSW_APERTURE_MASK) + offset;
}

static int v9x_hsw_depth_ok(DWORD bytes_per_pixel)
{
    return bytes_per_pixel == 1ul || bytes_per_pixel == 2ul ||
           bytes_per_pixel == 4ul;
}

static int v9x_hsw_fill(V9X_DDHAL_BLTDATA *data, DWORD offset,
                        DWORD bytes_per_pixel, int wait)
{
    v9x_u32 dw[V9X_HSW_SUBMIT_MAX];
    struct v9x_hsw_batch batch;
    DWORD color;

    /* A Z buffer is Y-tiled: the blitter's linear rectangle is not its
     * pixels, so the render engine draws the fill (Phase F2b). */
    if (data != 0 && data->lpDDDestSurface != 0 &&
        (data->lpDDDestSurface->ddsCaps & V9X_DDSCAPS_ZBUFFER) != 0ul) {
        /* D16, or a 32-bit D24_UNORM_X8; the fill takes the value's low
         * 16 or 24 bits by the surface's cell (v9x_d3d_hsw_fill_z). */
        if (!v9x_hsw_validate() ||
            (bytes_per_pixel != 2ul && bytes_per_pixel != 4ul)) {
            return V9X_BLT_DECLINED;
        }
        return v9x_d3d_hsw_depth_fill(data->lpDDDestSurface, data->rDest,
                                      data->bltFX.dwFillColor, wait);
    }
    if (!v9x_hsw_validate() || data == 0 || data->lpDDDestSurface == 0 ||
        data->lpDDDestSurface->lpGbl == 0 ||
        !v9x_hsw_depth_ok(bytes_per_pixel) ||
        data->lpDDDestSurface->lpGbl->lPitch <= 0l ||
        (DWORD)data->lpDDDestSurface->lpGbl->lPitch > V9X_HSW_PITCH_MAX ||
        data->rDest[0] < 0l || data->rDest[1] < 0l ||
        data->rDest[2] <= data->rDest[0] ||
        data->rDest[3] <= data->rDest[1] ||
        (DWORD)data->rDest[2] > V9X_HSW_COORD_MAX ||
        (DWORD)data->rDest[3] > V9X_HSW_COORD_MAX) {
        return V9X_BLT_DECLINED;
    }
    color = data->bltFX.dwFillColor;
    if (bytes_per_pixel == 1ul) {
        color &= 0xfful;
    } else if (bytes_per_pixel == 2ul) {
        color &= 0xfffful;
    }
    v9x_intel_hsw_batch_init(&batch, dw, V9X_HSW_SUBMIT_MAX -
                             V9X_HSW_STORE_DWORDS);
    if (v9x_intel_hsw_xy_color_blt(&batch, (v9x_u8)bytes_per_pixel,
            (v9x_u16)data->lpDDDestSurface->lpGbl->lPitch,
            (v9x_u16)data->rDest[0], (v9x_u16)data->rDest[1],
            (v9x_u16)data->rDest[2], (v9x_u16)data->rDest[3],
            v9x_hsw_address(offset), color) != V9X_STATUS_OK) {
        return V9X_BLT_DECLINED;
    }
    if (!v9x_hsw_wait_idle(wait)) {
        return V9X_BLT_BUSY;
    }
    v9x_hsw_submit(dw, batch.used);
    return V9X_BLT_DONE;
}

/* Rectangles of one surface that share any pixel; the blitter's direction
 * on overlap is not measured, so those copies stay on the CPU, as Gen3's
 * builder refuses them. */
static int v9x_hsw_overlaps(V9X_DDHAL_BLTDATA *data, DWORD source_offset,
                            DWORD destination_offset)
{
    LONG width;
    LONG height;
    LONG dx;
    LONG dy;

    if (source_offset != destination_offset) {
        return 0;
    }
    width = data->rSrc[2] - data->rSrc[0];
    height = data->rSrc[3] - data->rSrc[1];
    dx = data->rDest[0] - data->rSrc[0];
    dy = data->rDest[1] - data->rSrc[1];
    return dx < width && -dx < width && dy < height && -dy < height;
}

static int v9x_hsw_copy(V9X_DDHAL_BLTDATA *data, DWORD source_offset,
                        DWORD destination_offset, DWORD bytes_per_pixel,
                        int wait)
{
    v9x_u32 dw[V9X_HSW_SUBMIT_MAX];
    struct v9x_hsw_batch batch;
    LONG width;
    LONG height;

    if (!v9x_hsw_validate() || data == 0 ||
        data->lpDDSrcSurface == 0 || data->lpDDSrcSurface->lpGbl == 0 ||
        data->lpDDDestSurface == 0 || data->lpDDDestSurface->lpGbl == 0 ||
        !v9x_hsw_depth_ok(bytes_per_pixel) ||
        data->lpDDSrcSurface->lpGbl->lPitch <= 0l ||
        data->lpDDDestSurface->lpGbl->lPitch <= 0l ||
        (DWORD)data->lpDDSrcSurface->lpGbl->lPitch > V9X_HSW_PITCH_MAX ||
        (DWORD)data->lpDDDestSurface->lpGbl->lPitch > V9X_HSW_PITCH_MAX ||
        data->rSrc[2] <= data->rSrc[0] || data->rSrc[3] <= data->rSrc[1] ||
        data->rSrc[0] < 0l || data->rSrc[1] < 0l ||
        data->rDest[0] < 0l || data->rDest[1] < 0l) {
        return V9X_BLT_DECLINED;
    }
    width = data->rSrc[2] - data->rSrc[0];
    height = data->rSrc[3] - data->rSrc[1];
    if ((DWORD)(data->rDest[0] + width) > V9X_HSW_COORD_MAX ||
        (DWORD)(data->rDest[1] + height) > V9X_HSW_COORD_MAX ||
        (DWORD)data->rSrc[2] > V9X_HSW_COORD_MAX ||
        (DWORD)data->rSrc[3] > V9X_HSW_COORD_MAX ||
        v9x_hsw_overlaps(data, source_offset, destination_offset)) {
        return V9X_BLT_DECLINED;
    }
    v9x_intel_hsw_batch_init(&batch, dw, V9X_HSW_SUBMIT_MAX -
                             V9X_HSW_STORE_DWORDS);
    if (v9x_intel_hsw_xy_src_copy_blt(&batch, (v9x_u8)bytes_per_pixel,
            (v9x_u16)data->lpDDDestSurface->lpGbl->lPitch,
            (v9x_u16)data->rDest[0], (v9x_u16)data->rDest[1],
            (v9x_u16)(data->rDest[0] + width),
            (v9x_u16)(data->rDest[1] + height),
            v9x_hsw_address(destination_offset),
            (v9x_u16)data->lpDDSrcSurface->lpGbl->lPitch,
            (v9x_u16)data->rSrc[0], (v9x_u16)data->rSrc[1],
            v9x_hsw_address(source_offset)) != V9X_STATUS_OK) {
        return V9X_BLT_DECLINED;
    }
    if (!v9x_hsw_wait_idle(wait)) {
        return V9X_BLT_BUSY;
    }
    v9x_hsw_submit(dw, batch.used);
    return V9X_BLT_DONE;
}

/* CANBLT is the non-blocking idle poll, as on the 3dfx and the SiS. */
static int v9x_hsw_can_blt(void)
{
    return v9x_hsw_wait_idle(0);
}

const V9X_ENGINE32_OPS v9x_engine32_hsw = {
    v9x_hsw_ready,
    v9x_hsw_validate,
    v9x_hsw_validated,
    v9x_hsw_can_blt,
    v9x_hsw_wait_idle,
    v9x_hsw_fill,
    v9x_hsw_copy
};
