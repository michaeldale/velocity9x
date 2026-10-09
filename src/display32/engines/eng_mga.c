/*
 * Matrox Millennium drawing engine (MGA-2064W, MGA-2164W) for V9XHAL.DLL:
 * DirectDraw solid fill and screen copy.
 *
 * Registers through the 16 KiB control aperture the 16-bit side had the
 * mini-VDD map (millennium_hw16.c). Register values come from the
 * host-tested builder, src\chipsets\matrox\mga_engine.c.
 *
 * The engine's per-mode state - pixel width, plane mask, clip window - is
 * write-only and is written here once per descriptor, when the window is
 * first validated after a mode set; nothing reads it back, because nothing
 * can (MGA-1064SG specification Table 3-4: drawing registers are WO and
 * their reads are not decoded).
 *
 * Each operation waits for the engine to be idle, which also means its
 * 32-entry FIFO is empty (STATUS dwgengsts, p.4-74), so a builder's at most
 * ten writes always fit. A blocking wait that succeeds invalidates the
 * chip's CPU read cache, which the engine's own writes do not flush
 * (section 5.1.6): the CPU reads the framebuffer next.
 *
 * There is no recovery path. A timeout raises idle_timeouts, gives up on
 * the engine for the mode and declines to the CPU, as on the SiS.
 */
#include "ddhal_internal.h"
#include "velocity9x/mga_engine.h"

/* Bounded busy-wait; it only has to tell slow from stuck. A 1600x1200x32
 * fill is the longest single operation this engine is given. */
#define V9X_MGA_WAIT_SPINS 0x00200000ul

/* The window the mini-VDD maps (V9X_MGA_MMIO_BYTES there). */
#define V9X_MGA_MMIO_BYTES 0x00004000ul

/* Any VGA register write invalidates the read cache (section 5.1.6); the
 * CRTC index written back with its own value changes nothing else. */
#define V9X_MGA_CRTC_INDEX 0x03d4u

/* Fault-trace tag: "MGA2". */
#define V9X_MGA_FAULT_TAG 0x4d474132ul

static int v9x_mga_ready(void)
{
    return v9x_hal != 0 &&
        (v9x_hal->fb.flags & V9X_DD_FB_VALID) != 0ul &&
        (v9x_hal->engine.flags & V9X_DD_ENGINE_VALID) != 0ul &&
        v9x_hal->engine.engine_type == V9X_DD_ENGINE_TYPE_MGA &&
        v9x_hal->engine.control_linear_base != 0ul &&
        v9x_hal->engine.mapped_aperture_bytes >= V9X_MGA_MMIO_BYTES;
}

static DWORD v9x_mga_read32(DWORD offset)
{
    return *(volatile DWORD *)(v9x_hal->engine.control_linear_base + offset);
}

static void v9x_mga_write32(DWORD offset, DWORD value)
{
    *(volatile DWORD *)(v9x_hal->engine.control_linear_base + offset) = value;
}

static void v9x_mga_emit(const struct v9x_mga_writes *writes)
{
    DWORD index;

    for (index = 0ul; index < writes->count; ++index) {
        v9x_mga_write32(writes->offsets[index], writes->values[index]);
    }
}

/*
 * The first validation after a descriptor is built: an aperture that reads
 * all ones is not decoding, and is refused. Then the per-mode state, while
 * the engine is idle - the mode set left it so, and nothing else drives it.
 * A depth the engine cannot draw (24 bpp) leaves the engine unvalidated, so
 * every operation declines to the CPU.
 *
 * The setup writes are drained before validation is claimed. The first
 * caller is often the non-blocking CANBLT poll, and STATUS reads busy while
 * the FIFO holds anything: without the drain that poll answered
 * WASSTILLDRAWING with nothing drawing, and V9XDDP's fill cell never ran
 * (Win98SE-Millennium, 2026-10-09).
 */
static int v9x_mga_validate(void)
{
    struct v9x_mga_writes setup;
    DWORD spins;

    if (!v9x_mga_ready()) {
        return 0;
    }
    if ((v9x_hal->engine.flags & V9X_DD_ENGINE_STATUS_VALIDATED) != 0ul) {
        return 1;
    }
    if (v9x_mga_read32(V9X_MGA_STATUS) == 0xfffffffful) {
        return 0;
    }
    if (v9x_mga_build_setup(v9x_hal->fb.bits_per_pixel / 8ul, &setup) !=
        V9X_STATUS_OK) {
        return 0;
    }
    v9x_mga_emit(&setup);
    spins = V9X_MGA_WAIT_SPINS;
    while (v9x_mga_status_busy(v9x_mga_read32(V9X_MGA_STATUS)) != 0ul) {
        if (spins-- == 0ul) {
            return 0;
        }
    }
    v9x_hal->engine.flags |= V9X_DD_ENGINE_STATUS_VALIDATED;
    return 1;
}

static int v9x_mga_validated(void)
{
    return v9x_mga_ready() &&
        (v9x_hal->engine.flags & V9X_DD_ENGINE_STATUS_VALIDATED) != 0ul;
}

static void v9x_mga_flush_read_cache(void)
{
    v9x_outp(V9X_MGA_CRTC_INDEX, v9x_inp(V9X_MGA_CRTC_INDEX));
}

/*
 * A real timeout gives up on the engine for the rest of the mode: the
 * descriptor is invalidated, so v9x_engine32() answers none and Lock's
 * drain completes, for the reason eng_sis6326.c gives. The fault trace is
 * written on the first timeout only.
 */
static int v9x_mga_wait_idle(int wait)
{
    DWORD spins;

    if (!v9x_mga_validate()) {
        return 0;
    }
    if (!(wait && v9x_fault_injected())) {
        if (v9x_mga_status_busy(v9x_mga_read32(V9X_MGA_STATUS)) == 0ul) {
            if (wait) {
                v9x_mga_flush_read_cache();
            }
            return 1;
        }
        if (!wait) {
            return 0;
        }
        spins = V9X_MGA_WAIT_SPINS;
        while (spins-- != 0ul) {
            if (v9x_mga_status_busy(v9x_mga_read32(V9X_MGA_STATUS)) == 0ul) {
                v9x_mga_flush_read_cache();
                return 1;
            }
        }
        if (v9x_hal->engine.idle_timeouts++ == 0ul) {
            v9x_trace_flush_fault(V9X_MGA_FAULT_TAG, V9X_MGA_STATUS);
        }
        v9x_hal->engine.flags &= ~V9X_DD_ENGINE_VALID;
        return 0;
    }
    ++v9x_hal->engine.idle_timeouts;
    v9x_trace_flush_fault(V9X_MGA_FAULT_TAG, V9X_MGA_STATUS);
    return 0;
}

static int v9x_mga_fill(V9X_DDHAL_BLTDATA *data, DWORD offset,
                        DWORD bytes_per_pixel, int wait)
{
    struct v9x_mga_fill fill;
    struct v9x_mga_writes writes;

    if (!v9x_mga_validate() || data == 0 || data->lpDDDestSurface == 0 ||
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
    if (v9x_mga_build_fill(&fill, &writes) != V9X_STATUS_OK) {
        return V9X_BLT_DECLINED;
    }
    if (!v9x_mga_wait_idle(wait)) {
        return V9X_BLT_BUSY;
    }
    v9x_mga_emit(&writes);
    v9x_present_note_submission();
    return V9X_BLT_DONE;
}

static int v9x_mga_copy(V9X_DDHAL_BLTDATA *data, DWORD source_offset,
                        DWORD destination_offset, DWORD bytes_per_pixel,
                        int wait)
{
    struct v9x_mga_copy copy;
    struct v9x_mga_writes writes;

    if (!v9x_mga_validate() || data == 0 ||
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
    if (v9x_mga_build_copy(&copy, &writes) != V9X_STATUS_OK) {
        return V9X_BLT_DECLINED;
    }
    if (!v9x_mga_wait_idle(wait)) {
        return V9X_BLT_BUSY;
    }
    v9x_mga_emit(&writes);
    v9x_present_note_submission();
    return V9X_BLT_DONE;
}

/* CANBLT is the non-blocking idle poll, as on the Trio. */
static int v9x_mga_can_blt(void)
{
    return v9x_mga_wait_idle(0);
}

/*
 * The display start, for page flips: CRTC0D (bits 7:0), CRTC0C (15:8) and
 * CRTCEXT0<3:0> (19:16), in units the mode's own CRTC13 decides - see
 * v9x_mga_display_start, host-tested, for why the documents cannot.
 *
 * The VGA fallback cannot serve: its high bits go to S3's CR69. CRTCEXT0
 * is written last, because the change takes effect "at the beginning of
 * the next horizontal retrace following the write to CRTCEXT0" (1064SG
 * 5.6.5, p.5-66). That is applied at once, not latched at the retrace, so
 * the core writes it inside the vertical blank (v9x_scanout_writes_in_blank)
 * and completes the flip when that blank ends.
 */
#define V9X_MGA_CRTCEXT_INDEX 0x03deu
#define V9X_MGA_CRTCEXT_DATA  0x03dfu
#define V9X_MGA_CRTCEXT0      0x00u
#define V9X_MGA_CRTC_OFFSET   0x13u

int v9x_mga_scanout_active(void)
{
    if (v9x_hal == 0 ||
        (v9x_hal->engine.flags & V9X_DD_ENGINE_VALID) == 0ul ||
        v9x_hal->engine.engine_type != V9X_DD_ENGINE_TYPE_MGA) {
        return 0;
    }
    return (v9x_hal->engine.engine_caps & V9X_DD_ENGINE_CAP_FLIP) != 0ul;
}

int v9x_mga_set_display_start(DWORD byte_offset)
{
    v9x_u32 start;
    unsigned char ext0;

    if ((v9x_hal->fb.flags & V9X_DD_FB_VALID) == 0ul) {
        return 0;
    }
    v9x_outp(V9X_MGA_CRTCEXT_INDEX, V9X_MGA_CRTCEXT0);
    ext0 = v9x_inp(V9X_MGA_CRTCEXT_DATA);
    if (v9x_mga_display_start(byte_offset, v9x_hal->fb.pitch,
                              v9x_read_crtc(V9X_MGA_CRTC_OFFSET), ext0,
                              v9x_hal->fb.vram_bytes, &start) !=
            V9X_STATUS_OK) {
        return 0;
    }
    v9x_write_crtc(0x0du, (unsigned char)(start & 0xfful));
    v9x_write_crtc(0x0cu, (unsigned char)((start >> 8) & 0xfful));
    v9x_outp(V9X_MGA_CRTCEXT_INDEX, V9X_MGA_CRTCEXT0);
    v9x_outp(V9X_MGA_CRTCEXT_DATA,
             (unsigned char)v9x_mga_crtcext0_with_start(ext0, start));
    return 1;
}

const V9X_ENGINE32_OPS v9x_engine32_mga = {
    v9x_mga_ready,
    v9x_mga_validate,
    v9x_mga_validated,
    v9x_mga_can_blt,
    v9x_mga_wait_idle,
    v9x_mga_fill,
    v9x_mga_copy
};
