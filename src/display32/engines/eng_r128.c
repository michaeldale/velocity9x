/*
 * ATI Rage 128 2D engine for V9XHAL.DLL: DirectDraw solid fill and screen
 * copy.
 *
 * Registers through the 16 KiB register BAR, which the 16-bit side had the
 * mini-VDD map (rage128_hw16.c). Register values come from the host-tested
 * builder, src\chipsets\ati\rage128\r128_2d.c, whose init list, fills and
 * copies in every overlap direction matched byte for byte at 8, 16 and 32
 * bpp on a Rage Fury Pro (docs\decisions\2026-10-22-rage128-2d-write-
 * probe.md).
 *
 * The engine takes direct register writes only while the CCE is in NONPM4,
 * which is how POST leaves it and how nothing in this driver changes it
 * (docs\decisions\2026-10-22-rage128-register-probe.md). Validation checks
 * that, checks the register file's configuration copy names ATI, then
 * writes the builder's init list once per mode: POST leaves the default
 * scissor at the visible size, which would clip every surface below it.
 *
 * Idle is r128_accel.c's R128WaitForIdle: 64 free FIFO slots and
 * GUI_ACTIVE clear, then the pixel cache flushed. The flush is needed only
 * once the engine has drawn, so it is skipped until then.
 *
 * There is no recovery path yet. A timeout raises idle_timeouts, gives the
 * engine up for the rest of the mode and declines the operation to the CPU.
 */
#include "ddhal_internal.h"
#include "velocity9x/r128_2d.h"

/* Bounded busy-wait. The write probe measured 5-7 status reads per small
 * operation; this bound only has to tell slow from stuck. */
#define V9X_R128_WAIT_SPINS 0x00200000ul

/* CANBLT's non-blocking poll: a few reads, so a flush that is about to
 * finish still lets the next blit go. */
#define V9X_R128_QUICK_READS 16ul

/* The window the mini-VDD maps (V9X_R128_MMIO_BYTES there). */
#define V9X_R128_MMIO_BYTES 0x00004000ul

/* The register file's read-only copy of configuration space; its low word
 * is the vendor. */
#define V9X_R128_PCI_COPY 0x0f00ul
#define V9X_R128_VENDOR_ATI 0x1002ul

/* BAR0 holds two 32 MiB views of video memory (the second for big-endian
 * hosts); the engine addresses video memory from its first byte. */
#define V9X_R128_APERTURE_BYTES 0x02000000ul

/* Fault-trace tag: "R12D". */
#define V9X_R128_FAULT_TAG 0x52313244ul

/* Set when the engine has drawn since the pixel cache was last flushed.
 * One engine, so one flag: V9XHAL's statics are shared by every process,
 * which is what this needs. */
static int v9x_r128_dirty;

static int v9x_r128_ready(void)
{
    return v9x_hal != 0 &&
        (v9x_hal->fb.flags & V9X_DD_FB_VALID) != 0ul &&
        (v9x_hal->engine.flags & V9X_DD_ENGINE_VALID) != 0ul &&
        v9x_hal->engine.engine_type == V9X_DD_ENGINE_TYPE_ATI_RAGE128 &&
        v9x_hal->engine.control_linear_base != 0ul &&
        v9x_hal->engine.mapped_aperture_bytes >= V9X_R128_MMIO_BYTES;
}

static DWORD v9x_r128_read32(DWORD offset)
{
    return *(volatile DWORD *)(v9x_hal->engine.control_linear_base + offset);
}

static void v9x_r128_write32(DWORD offset, DWORD value)
{
    *(volatile DWORD *)(v9x_hal->engine.control_linear_base + offset) = value;
}

/* Where the surfaces DirectDraw describes start in video memory: their
 * offsets are from the mapped framebuffer, which begins at the desktop. */
static DWORD v9x_r128_desktop_offset(void)
{
    return v9x_hal->fb.physical_base & (V9X_R128_APERTURE_BYTES - 1ul);
}

/* Non-zero once GUI_STAT reads idle within the given number of reads. */
static int v9x_r128_poll_idle(DWORD reads)
{
    while (reads-- != 0ul) {
        if (v9x_r128_gui_idle(v9x_r128_read32(V9X_R128_GUI_STAT)) != 0ul) {
            return 1;
        }
    }
    return 0;
}

/* Flush the pixel cache and wait, within the given reads, for it to say so
 * (r128_accel.c R128EngineFlush). */
static int v9x_r128_flush(DWORD reads)
{
    v9x_r128_write32(V9X_R128_PC_NGUI_CTLSTAT,
                     v9x_r128_read32(V9X_R128_PC_NGUI_CTLSTAT) |
                     V9X_R128_PC_FLUSH_ALL);
    while (reads-- != 0ul) {
        if ((v9x_r128_read32(V9X_R128_PC_NGUI_CTLSTAT) &
             V9X_R128_PC_BUSY) == 0ul) {
            return 1;
        }
    }
    return 0;
}

static int v9x_r128_validated(void)
{
    return v9x_r128_ready() &&
        (v9x_hal->engine.flags & V9X_DD_ENGINE_STATUS_VALIDATED) != 0ul;
}

/*
 * Once per mode: the window must be this card's, the CCE must leave the
 * engine to direct writes, and the init list goes in on an idle engine.
 * Any refusal leaves the engine unclaimed and DirectDraw on the CPU.
 */
static int v9x_r128_validate(void)
{
    struct v9x_r128_blt init;
    DWORD index;

    if (!v9x_r128_ready()) {
        return 0;
    }
    if ((v9x_hal->engine.flags & V9X_DD_ENGINE_STATUS_VALIDATED) != 0ul) {
        return 1;
    }
    /* Direct3D may have started the CCE in a mode the 2D engine has not
     * yet been used in - Glide's own, say. Refusing here would make
     * v9x_render_drain answer ABANDONED without ever stopping it, and the
     * mode set that follows a session ran against a free-running
     * microengine (A8U4I5 hung in V9XGLIDP, boot 532). */
    if (!v9x_r128_cce_drain(1)) {
        return 0;
    }
    if ((v9x_r128_read32(V9X_R128_PCI_COPY) & 0xfffful) !=
        V9X_R128_VENDOR_ATI) {
        return 0;
    }
    if (v9x_r128_pm4_direct(v9x_r128_read32(V9X_R128_PM4_BUFFER_CNTL)) ==
        0ul) {
        return 0;
    }
    if (v9x_r128_build_init(v9x_hal->fb.bits_per_pixel >> 3,
                            v9x_hal->fb.pitch,
                            v9x_r128_read32(V9X_R128_DP_DATATYPE),
                            &init) != V9X_STATUS_OK) {
        return 0;
    }
    if (!v9x_r128_poll_idle(V9X_R128_WAIT_SPINS)) {
        return 0;
    }
    for (index = 0ul; index < init.count; ++index) {
        v9x_r128_write32(init.offsets[index], init.values[index]);
    }
    if (!v9x_r128_poll_idle(V9X_R128_WAIT_SPINS)) {
        return 0;
    }
    v9x_r128_dirty = 0;
    v9x_hal->engine.flags |= V9X_DD_ENGINE_STATUS_VALIDATED;
    return 1;
}

/*
 * A real timeout gives up on the engine for the rest of the mode, as the
 * SiS and VSA-100 engines do: the descriptor is invalidated, so
 * v9x_engine32() answers none, blits and fills fall to the CPU, and Lock's
 * drain completes rather than answering WASSTILLDRAWING for ever. The fault
 * trace is written on the first timeout only.
 */
static int v9x_r128_wait_idle(int wait)
{
    DWORD reads;

    /* Direct3D's batches first: Flip, Lock and the blit status all come
     * here, and the CCE is left running between batches (r128_cce.c). */
    if (!v9x_r128_cce_drain(wait)) {
        return 0;
    }

    if (!v9x_r128_validate()) {
        return 0;
    }
    if (wait && v9x_fault_injected()) {
        ++v9x_hal->engine.idle_timeouts;
        v9x_trace_flush_fault(V9X_R128_FAULT_TAG, V9X_R128_GUI_STAT);
        return 0;
    }
    reads = wait ? V9X_R128_WAIT_SPINS : V9X_R128_QUICK_READS;
    if (v9x_r128_poll_idle(reads) &&
        (!v9x_r128_dirty || v9x_r128_flush(reads))) {
        v9x_r128_dirty = 0;
        return 1;
    }
    if (!wait) {
        return 0;
    }
    if (v9x_hal->engine.idle_timeouts++ == 0ul) {
        v9x_trace_flush_fault(V9X_R128_FAULT_TAG, V9X_R128_GUI_STAT);
    }
    v9x_hal->engine.flags &= ~V9X_DD_ENGINE_VALID;
    return 0;
}

static void v9x_r128_emit(const struct v9x_r128_blt *blt)
{
    DWORD index;

    /* The source or destination may hold CPU writes still in the
     * write-combining buffers: GDI's, or a surface just unlocked. */
    v9x_wc_drain();
    for (index = 0ul; index < blt->count; ++index) {
        v9x_r128_write32(blt->offsets[index], blt->values[index]);
    }
    (void)v9x_r128_read32(V9X_R128_GUI_STAT);
    v9x_r128_dirty = 1;
    v9x_present_note_submission();
}

static int v9x_r128_fill(V9X_DDHAL_BLTDATA *data, DWORD offset,
                         DWORD bytes_per_pixel, int wait)
{
    struct v9x_r128_fill fill;
    struct v9x_r128_blt blt;

    /* Direct register writes must not meet a running microengine. */
    if (!v9x_r128_cce_drain(1) ||
        !v9x_r128_validate() || data == 0 || data->lpDDDestSurface == 0 ||
        data->lpDDDestSurface->lpGbl == 0 ||
        data->lpDDDestSurface->lpGbl->lPitch <= 0l ||
        data->rDest[2] <= data->rDest[0] ||
        data->rDest[3] <= data->rDest[1] ||
        data->rDest[0] < 0l || data->rDest[1] < 0l) {
        return V9X_BLT_DECLINED;
    }
    fill.vram_bytes = v9x_r128_desktop_offset() + v9x_hal->fb.vram_bytes;
    fill.target_offset = v9x_r128_desktop_offset() + offset;
    fill.pitch_bytes = (DWORD)data->lpDDDestSurface->lpGbl->lPitch;
    fill.bytes_per_pixel = bytes_per_pixel;
    fill.left = (DWORD)data->rDest[0];
    fill.top = (DWORD)data->rDest[1];
    fill.width = (DWORD)(data->rDest[2] - data->rDest[0]);
    fill.height = (DWORD)(data->rDest[3] - data->rDest[1]);
    fill.color = data->bltFX.dwFillColor;
    if (v9x_r128_build_fill(&fill, &blt) != V9X_STATUS_OK) {
        return V9X_BLT_DECLINED;
    }
    if (!v9x_r128_wait_idle(wait)) {
        return V9X_BLT_BUSY;
    }
    v9x_r128_emit(&blt);
    return V9X_BLT_DONE;
}

static int v9x_r128_copy(V9X_DDHAL_BLTDATA *data, DWORD source_offset,
                         DWORD destination_offset, DWORD bytes_per_pixel,
                         int wait)
{
    struct v9x_r128_copy copy;
    struct v9x_r128_blt blt;

    /* Direct register writes must not meet a running microengine. */
    if (!v9x_r128_cce_drain(1) ||
        !v9x_r128_validate() || data == 0 ||
        data->lpDDSrcSurface == 0 || data->lpDDSrcSurface->lpGbl == 0 ||
        data->lpDDDestSurface == 0 || data->lpDDDestSurface->lpGbl == 0 ||
        data->lpDDSrcSurface->lpGbl->lPitch <= 0l ||
        data->lpDDDestSurface->lpGbl->lPitch <= 0l ||
        data->rSrc[2] <= data->rSrc[0] || data->rSrc[3] <= data->rSrc[1] ||
        data->rSrc[0] < 0l || data->rSrc[1] < 0l ||
        data->rDest[0] < 0l || data->rDest[1] < 0l) {
        return V9X_BLT_DECLINED;
    }
    copy.vram_bytes = v9x_r128_desktop_offset() + v9x_hal->fb.vram_bytes;
    copy.source_offset = v9x_r128_desktop_offset() + source_offset;
    copy.source_pitch_bytes = (DWORD)data->lpDDSrcSurface->lpGbl->lPitch;
    copy.destination_offset = v9x_r128_desktop_offset() + destination_offset;
    copy.destination_pitch_bytes =
        (DWORD)data->lpDDDestSurface->lpGbl->lPitch;
    copy.bytes_per_pixel = bytes_per_pixel;
    copy.source_left = (DWORD)data->rSrc[0];
    copy.source_top = (DWORD)data->rSrc[1];
    copy.destination_left = (DWORD)data->rDest[0];
    copy.destination_top = (DWORD)data->rDest[1];
    copy.width = (DWORD)(data->rSrc[2] - data->rSrc[0]);
    copy.height = (DWORD)(data->rSrc[3] - data->rSrc[1]);
    if (v9x_r128_build_copy(&copy, &blt) != V9X_STATUS_OK) {
        return V9X_BLT_DECLINED;
    }
    if (!v9x_r128_wait_idle(wait)) {
        return V9X_BLT_BUSY;
    }
    v9x_r128_emit(&blt);
    return V9X_BLT_DONE;
}

/* CANBLT is the non-blocking idle poll, as on the SiS and the VSA-100. */
static int v9x_r128_can_blt(void)
{
    return v9x_r128_wait_idle(0);
}

const V9X_ENGINE32_OPS v9x_engine32_r128 = {
    v9x_r128_ready,
    v9x_r128_validate,
    v9x_r128_validated,
    v9x_r128_can_blt,
    v9x_r128_wait_idle,
    v9x_r128_fill,
    v9x_r128_copy
};
