/*
 * NVIDIA RIVA 128 (NV3) 2D engine for V9XHAL.DLL: DirectDraw solid fill
 * and screen copy.
 *
 * Registers through BAR0, which the 16-bit side had the mini-VDD map
 * (riva128_hw16.c); RAMIN, the engine's object tables, through the
 * framebuffer mapping at the top of video memory, where the hook says it
 * starts (engine.reserve_offset) and in the reversed 16-byte layout the
 * register probe measured. Everything written comes from the host-tested
 * builder, src\chipsets\nvidia\nv3_2d.c, whose bring-up, fills and copies
 * the Phase 3 probe ran on the card
 * (docs\decisions\2026-11-02-riva128-2d-engine-writes.md).
 *
 * A VBE mode set clears RAMIN and leaves PFB_CONFIG_0 describing the old
 * mode (measured, the same record), so before every operation the engine
 * checks one RAMHT word and PFB_CONFIG_0 and repeats the whole bring-up
 * when either is not what it wrote.
 *
 * 8 and 32 bpp only (v9x_nv3_depth_drawn). At 16 bpp the engine sets bit
 * 15 of every pixel it writes, and this BIOS's 16 bpp modes are 5:6:5,
 * where bit 15 is red's top bit; 16 bpp waits for the desktop to run 5:5:5
 * (decision 1 of docs\plans\nvidia-riva128.md). At 32 bpp the engine
 * rewrites the top byte, which X8R8G8B8 does not show.
 *
 * At 16 bpp the engine is present and idle, not invalid: validate and the
 * idle wait answer yes without touching a register, and fill and copy
 * decline. The core waits on validate before every CPU fill, copy and Lock
 * (v9x_render_drain), so an engine that failed validation by depth made
 * each of them answer WASSTILLDRAWING for good, and DirectDraw retries a
 * DDBLT_WAIT blit for good with the Win16 lock held: M-7V636C stopped
 * twice at V9XDDP's first back-buffer fill at 640x480x16
 * (docs\issues\2026-11-02-riva128-hang-under-v9xddp-at-32bpp.md).
 *
 * There is no recovery path: a timeout raises idle_timeouts, gives the
 * engine up for the mode and declines to the CPU.
 */
#include "ddhal_internal.h"
#include "velocity9x/nv3_2d.h"

/* Bounded busy-wait. The probe measured 0-5 status reads per small
 * operation; a full 1600x1200x32 fill is far longer, and this bound only
 * has to tell slow from stuck. */
#define V9X_NV3_WAIT_SPINS 0x00200000ul

/* BAR0 as the mini-VDD maps it (V9X_NV3_MMIO_BYTES there). */
#define V9X_NV3_MMIO_BYTES 0x01000000ul

/* The free count the FIFO holds when empty, latched after each bring-up
 * as rivafb latches FifoEmptyCount; 7Ch on M-7V636C. V9XHAL's statics are
 * shared by every process, which is what an engine's state is. */
static DWORD v9x_nv3_empty;
static DWORD v9x_nv3_pfb_expected;

static int v9x_nv3_ready(void)
{
    return v9x_hal != 0 &&
        (v9x_hal->fb.flags & V9X_DD_FB_VALID) != 0ul &&
        (v9x_hal->engine.flags & V9X_DD_ENGINE_VALID) != 0ul &&
        v9x_hal->engine.engine_type == V9X_DD_ENGINE_TYPE_NV3 &&
        v9x_hal->engine.control_linear_base != 0ul &&
        v9x_hal->engine.mapped_aperture_bytes >= V9X_NV3_MMIO_BYTES &&
        v9x_hal->engine.reserve_offset != 0ul &&
        v9x_hal->fb.linear_base != 0ul;
}

/* Whether the engine draws at the current depth; ready() first. */
static int v9x_nv3_depth_ok(void)
{
    return v9x_nv3_depth_drawn(v9x_hal->fb.bits_per_pixel / 8ul) ==
           V9X_TRUE;
}

static DWORD v9x_nv3_read32(DWORD offset)
{
    return *(volatile DWORD *)(v9x_hal->engine.control_linear_base + offset);
}

static void v9x_nv3_write32(DWORD offset, DWORD value)
{
    *(volatile DWORD *)(v9x_hal->engine.control_linear_base + offset) = value;
}

/* RAMIN's top: the reserve's start plus its 32 KiB. */
static DWORD v9x_nv3_ramin_address(DWORD ramin_offset)
{
    return v9x_hal->fb.linear_base +
           v9x_nv3_ramin_vram_offset(v9x_hal->engine.reserve_offset +
                                     V9X_NV3_RAMIN_BYTES,
                                     ramin_offset);
}

static DWORD v9x_nv3_free(void)
{
    return v9x_nv3_read32(V9X_NV3_USER_FREE) & V9X_NV3_FREE_MASK;
}

static int v9x_nv3_idle_now(void)
{
    return v9x_nv3_free() >= v9x_nv3_empty &&
           (v9x_nv3_read32(V9X_NV3_PGRAPH_STATUS) &
            V9X_NV3_STATUS_BUSY) == 0ul;
}

/* PGRAPH idle, bounded; the FIFO's count is not yet known here. */
static int v9x_nv3_wait_pgraph(void)
{
    DWORD spins = V9X_NV3_WAIT_SPINS;

    while (spins-- != 0ul) {
        if ((v9x_nv3_read32(V9X_NV3_PGRAPH_STATUS) &
             V9X_NV3_STATUS_BUSY) == 0ul) {
            return 1;
        }
    }
    return 0;
}

/*
 * rivafb's LoadStateExt for the current mode, in its order, then the
 * bindings, the latched free count and riva_setup_accel's defaults. The
 * DirectDraw heap ends below RAMIN (riva128_hw16.c's reserve), so nothing
 * else lives where this writes.
 */
static int v9x_nv3_bring_up(void)
{
    struct v9x_nv3_setup setup;
    DWORD bytes_per_pixel;
    DWORD index;

    bytes_per_pixel = v9x_hal->fb.bits_per_pixel / 8ul;
    if (v9x_nv3_build_setup(bytes_per_pixel, v9x_hal->fb.width,
                            v9x_hal->fb.height, v9x_hal->fb.pitch,
                            &setup) != V9X_STATUS_OK) {
        return 0;
    }
    v9x_wc_drain();
    for (index = 0ul; index < setup.before_count; ++index) {
        v9x_nv3_write32(setup.before[index].offset,
                        setup.before[index].value);
    }
    for (index = 0ul; index < setup.ramin_count; ++index) {
        *(volatile DWORD *)v9x_nv3_ramin_address(setup.ramin[index].offset) =
            setup.ramin[index].value;
    }
    v9x_wc_drain();
    for (index = 0ul; index < setup.after_count; ++index) {
        v9x_nv3_write32(setup.after[index].offset, setup.after[index].value);
    }
    for (index = 0ul; index < setup.bind_count; ++index) {
        v9x_nv3_write32(setup.bind[index].offset, setup.bind[index].value);
    }
    if (!v9x_nv3_wait_pgraph()) {
        return 0;
    }
    v9x_nv3_empty = v9x_nv3_free();
    for (index = 0ul; index < setup.defaults_count; ++index) {
        v9x_nv3_write32(setup.defaults[index].offset,
                        setup.defaults[index].value);
    }
    v9x_nv3_pfb_expected =
        v9x_nv3_pfb_config(bytes_per_pixel, v9x_hal->fb.width);
    return 1;
}

/* What the last bring-up wrote, still there: RAMIN's mark and PFB. */
static int v9x_nv3_state_holds(void)
{
    return *(volatile DWORD *)v9x_nv3_ramin_address(
               V9X_NV3_RAMIN_MARK_OFFSET) == V9X_NV3_RAMIN_MARK_VALUE &&
           v9x_nv3_read32(V9X_NV3_PFB_CONFIG_0) == v9x_nv3_pfb_expected;
}

static int v9x_nv3_validate(void)
{
    if (!v9x_nv3_ready()) {
        return 0;
    }
    /* Nothing is issued at this depth, so there is nothing to bring up and
     * nothing for a CPU access to wait on. */
    if (!v9x_nv3_depth_ok()) {
        return 1;
    }
    if ((v9x_hal->engine.flags & V9X_DD_ENGINE_STATUS_VALIDATED) != 0ul &&
        v9x_nv3_state_holds()) {
        return 1;
    }
    v9x_hal->engine.flags &= ~V9X_DD_ENGINE_STATUS_VALIDATED;
    if (!v9x_nv3_bring_up()) {
        if (v9x_hal->engine.idle_timeouts++ == 0ul) {
            v9x_trace_flush_fault(0x4e334255ul, V9X_NV3_PGRAPH_STATUS);
        }
        v9x_hal->engine.flags &= ~V9X_DD_ENGINE_VALID;
        return 0;
    }
    ++v9x_hal->engine.reset_count;
    v9x_hal->engine.flags |= V9X_DD_ENGINE_STATUS_VALIDATED;
    return 1;
}

static int v9x_nv3_validated(void)
{
    return v9x_nv3_ready() &&
        (!v9x_nv3_depth_ok() ||
         (v9x_hal->engine.flags & V9X_DD_ENGINE_STATUS_VALIDATED) != 0ul);
}

/*
 * rivafb's nv3Busy: busy while the free count is below the latched empty
 * count or PGRAPH STATUS bit 0 is set. A real timeout gives the engine up
 * for the rest of the mode, as eng_sis6326.c explains.
 */
static int v9x_nv3_wait_idle(int wait)
{
    DWORD spins;

    if (!v9x_nv3_validate()) {
        return 0;
    }
    if (!v9x_nv3_depth_ok()) {
        return 1;
    }
    if (!(wait && v9x_fault_injected())) {
        if (v9x_nv3_idle_now()) {
            return 1;
        }
        if (!wait) {
            return 0;
        }
        spins = V9X_NV3_WAIT_SPINS;
        while (spins-- != 0ul) {
            if (v9x_nv3_idle_now()) {
                return 1;
            }
        }
        if (v9x_hal->engine.idle_timeouts++ == 0ul) {
            v9x_trace_flush_fault(0x4e334944ul, V9X_NV3_PGRAPH_STATUS);
        }
        v9x_hal->engine.flags &= ~V9X_DD_ENGINE_VALID;
        return 0;
    }
    ++v9x_hal->engine.idle_timeouts;
    v9x_trace_flush_fault(0x4e334944ul, V9X_NV3_PGRAPH_STATUS);
    return 0;
}

/* The engine is idle and the FIFO empty here (every caller waited), so the
 * surface registers can be written directly and the methods all fit. */
static void v9x_nv3_emit(const struct v9x_nv3_op *op)
{
    DWORD index;

    /* The source or destination may hold CPU writes still in the
     * write-combining buffers: GDI's, or a surface just unlocked. */
    v9x_wc_drain();
    for (index = 0ul; index < op->surface_count; ++index) {
        v9x_nv3_write32(op->surface[index].offset, op->surface[index].value);
    }
    for (index = 0ul; index < op->method_count; ++index) {
        v9x_nv3_write32(op->methods[index].offset, op->methods[index].value);
    }
    v9x_present_note_submission();
}

static int v9x_nv3_fill(V9X_DDHAL_BLTDATA *data, DWORD offset,
                        DWORD bytes_per_pixel, int wait)
{
    struct v9x_nv3_fill fill;
    struct v9x_nv3_op op;

    if (!v9x_nv3_validate() || !v9x_nv3_depth_ok() ||
        data == 0 || data->lpDDDestSurface == 0 ||
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
    if (v9x_nv3_build_fill(&fill, &op) != V9X_STATUS_OK) {
        return V9X_BLT_DECLINED;
    }
    if (!v9x_nv3_wait_idle(wait)) {
        return V9X_BLT_BUSY;
    }
    v9x_nv3_emit(&op);
    return V9X_BLT_DONE;
}

static int v9x_nv3_copy(V9X_DDHAL_BLTDATA *data, DWORD source_offset,
                        DWORD destination_offset, DWORD bytes_per_pixel,
                        int wait)
{
    struct v9x_nv3_copy copy;
    struct v9x_nv3_op op;

    if (!v9x_nv3_validate() || !v9x_nv3_depth_ok() || data == 0 ||
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
    if (v9x_nv3_build_copy(&copy, &op) != V9X_STATUS_OK) {
        return V9X_BLT_DECLINED;
    }
    if (!v9x_nv3_wait_idle(wait)) {
        return V9X_BLT_BUSY;
    }
    v9x_nv3_emit(&op);
    return V9X_BLT_DONE;
}

/* CANBLT is the non-blocking idle poll, as on the Trio. */
static int v9x_nv3_can_blt(void)
{
    return v9x_nv3_wait_idle(0);
}

const V9X_ENGINE32_OPS v9x_engine32_nv3 = {
    v9x_nv3_ready,
    v9x_nv3_validate,
    v9x_nv3_validated,
    v9x_nv3_can_blt,
    v9x_nv3_wait_idle,
    v9x_nv3_fill,
    v9x_nv3_copy
};
