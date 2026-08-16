/*
 * S3 ViRGE/DX drawing engine for V9XHAL.DLL.
 *
 * The ViRGE is addressed through the new-MMIO window inside the mapped linear
 * aperture: a status register carrying a FIFO count and an idle bit, and a
 * command register fed by a handful of parameter registers. Everything that
 * knows those registers lives here, including the bounded waits and the CR66
 * engine reset they fall back on.
 *
 * Engine recovery is deliberately file-local. It is only ever reached from a
 * wait in this file that has already expired, and the dispatch table exports
 * no recovery entry precisely because the Trio64 has none - see
 * docs/decisions/2026-08-16-engine32-vtable.md.
 */
#include "ddhal_internal.h"

static int v9x_engine_ready(void)
{
    return v9x_hal != 0 &&
           (v9x_hal->fb.flags & V9X_DD_FB_VALID) != 0ul &&
           (v9x_hal->engine.flags &
            (V9X_DD_ENGINE_VALID | V9X_DD_ENGINE_S3_VIRGE_DX)) ==
            (V9X_DD_ENGINE_VALID | V9X_DD_ENGINE_S3_VIRGE_DX) &&
           v9x_hal->engine.control_linear_base != 0ul &&
           v9x_hal->engine.mapped_aperture_bytes >
               V9X_VIRGE_RECT_DEST_XY + sizeof(DWORD);
}

int v9x_engine_status_validated(void)
{
    return v9x_engine_ready() &&
           (v9x_hal->engine.flags &
            V9X_DD_ENGINE_STATUS_VALIDATED) != 0ul;
}

static DWORD v9x_mmio_read(DWORD offset)
{
    volatile DWORD *reg = (volatile DWORD *)
        (v9x_hal->engine.control_linear_base + offset);

    return *reg;
}

void v9x_mmio_write(DWORD offset, DWORD value)
{
    volatile DWORD *reg = (volatile DWORD *)
        (v9x_hal->engine.control_linear_base + offset);

    *reg = value;
}

static DWORD v9x_engine_status(void)
{
    return v9x_mmio_read(V9X_VIRGE_ENGINE_STATUS);
}

static DWORD v9x_fifo_free(DWORD status)
{
    return (status & V9X_VIRGE_STATUS_FIFO_MASK) >>
           V9X_VIRGE_STATUS_FIFO_SHIFT;
}

/*
 * Confirm once that the engine status register reads sensibly before any
 * MMIO command is issued to it.
 *
 * This used to be latched only by GetBltStatus(DDGBS_CANBLT) and by the
 * Direct3D draw callbacks, which meant a DirectDraw application that blits
 * without polling first could never reach the engine. That was invisible
 * while the driver did not advertise DDCAPS_BLT; once it does, an unreached
 * blit is reported to the application as DDERR_UNSUPPORTED rather than being
 * emulated, so the check has to be able to run on the blit path itself.
 *
 * The validated bit is cleared on every Enable and ReEnable, so this runs
 * again after each mode change. Sampling the status register once was enough
 * for the polling callers but not here: the engine is not necessarily idle
 * in the instant after a mode set, and a single unlucky sample sent the
 * first fill of a mode down the CPU fallback. A short bounded re-sample
 * makes acceleration deterministic without invoking engine recovery, which
 * would be far too aggressive for a liveness check.
 */
#define V9X_ENGINE_VALIDATE_SPINS 64ul

int v9x_engine_validate_status(void)
{
    DWORD status;
    DWORD spins = V9X_ENGINE_VALIDATE_SPINS;

    if (!v9x_engine_ready()) {
        return 0;
    }
    if ((v9x_hal->engine.flags & V9X_DD_ENGINE_STATUS_VALIDATED) != 0ul) {
        return 1;
    }
    while (spins-- != 0ul) {
        status = v9x_engine_status();
        if (v9x_fifo_free(status) >= 8ul &&
            (status & V9X_VIRGE_STATUS_IDLE) != 0ul) {
            v9x_hal->engine.flags |= V9X_DD_ENGINE_STATUS_VALIDATED;
            return 1;
        }
    }
    return 0;
}

/* CR66 bit 1 is the ViRGE/DX graphics-engine reset used by the Windows 98
 * S3 sample. It is touched only after a bounded wait has expired. */
static void v9x_engine_recover(void)
{
    unsigned char cr66;

    if (!v9x_engine_ready()) {
        return;
    }
    cr66 = v9x_read_crtc(0x66u);
    v9x_write_crtc(0x66u, (unsigned char)(cr66 | 0x02u));
    v9x_write_crtc(0x66u, cr66);
    ++v9x_hal->engine.reset_count;
}

int v9x_wait_fifo(DWORD entries, int wait)
{
    DWORD spins;

    if (!v9x_engine_ready() || entries == 0ul || entries > 31ul) {
        return 0;
    }
    if (!(wait && v9x_fault_injected())) {
        if (v9x_fifo_free(v9x_engine_status()) >= entries) {
            return 1;
        }
        if (!wait) {
            return 0;
        }
        spins = V9X_VIRGE_FIFO_SPIN_LIMIT;
        while (spins-- != 0ul) {
            if (v9x_fifo_free(v9x_engine_status()) >= entries) {
                return 1;
            }
        }
    }
    ++v9x_hal->engine.fifo_timeouts;
    v9x_trace_flush_fault(0x56394646ul, V9X_VIRGE_ENGINE_STATUS);
    v9x_engine_recover();
    return 0;
}

int v9x_wait_idle(int wait)
{
    DWORD spins;

    if (!v9x_engine_ready()) {
        return 0;
    }
    if (!(wait && v9x_fault_injected())) {
        if ((v9x_engine_status() & V9X_VIRGE_STATUS_IDLE) != 0ul) {
            return 1;
        }
        if (!wait) {
            return 0;
        }
        spins = V9X_VIRGE_IDLE_SPIN_LIMIT;
        while (spins-- != 0ul) {
            if ((v9x_engine_status() & V9X_VIRGE_STATUS_IDLE) != 0ul) {
                return 1;
            }
        }
    }
    ++v9x_hal->engine.idle_timeouts;
    v9x_trace_flush_fault(0x56394944ul, V9X_VIRGE_ENGINE_STATUS);
    v9x_engine_recover();
    return 0;
}

/*
 * Screen-to-screen BitBLT on the ViRGE 2D engine.
 *
 * This is what makes a video-memory source copy affordable. The CPU fallback
 * has to read every byte back out of the aperture, which measured about
 * 300 ms for a 640x480x16 frame - 3 FPS under Ironfield's BltFast
 * presentation path. The engine moves the same rectangle without the data
 * crossing the bus at all.
 *
 * Overlap is handled by direction rather than by row order: the engine walks
 * from whichever corner keeps a same-surface copy correct, which is why the
 * scan direction is derived from the rectangles rather than fixed.
 */
static int v9x_virge_copy(V9X_DDHAL_BLTDATA *data, DWORD source_offset,
                          DWORD destination_offset, DWORD bytes_per_pixel,
                          int wait)
{
    DWORD source_pitch = (DWORD)data->lpDDSrcSurface->lpGbl->lPitch;
    DWORD destination_pitch = (DWORD)data->lpDDDestSurface->lpGbl->lPitch;
    DWORD width = (DWORD)(data->rSrc[2] - data->rSrc[0]);
    DWORD height = (DWORD)(data->rSrc[3] - data->rSrc[1]);
    DWORD source_x = (DWORD)data->rSrc[0];
    DWORD source_y = (DWORD)data->rSrc[1];
    DWORD destination_x = (DWORD)data->rDest[0];
    DWORD destination_y = (DWORD)data->rDest[1];
    DWORD command;
    int x_positive = 1;
    int y_positive = 1;

    if ((source_pitch & ~V9X_VIRGE_STRIDE_MASK) != 0ul ||
        (destination_pitch & ~V9X_VIRGE_STRIDE_MASK) != 0ul ||
        (source_offset & 7ul) != 0ul || (destination_offset & 7ul) != 0ul ||
        width == 0ul || height == 0ul ||
        width > V9X_VIRGE_COORD_MAX || height > V9X_VIRGE_COORD_MAX ||
        data->rSrc[2] > (LONG)V9X_VIRGE_COORD_MAX ||
        data->rSrc[3] > (LONG)V9X_VIRGE_COORD_MAX ||
        data->rDest[2] > (LONG)V9X_VIRGE_COORD_MAX ||
        data->rDest[3] > (LONG)V9X_VIRGE_COORD_MAX) {
        return V9X_BLT_DECLINED;
    }

    /* Only a copy within one surface can overlap; DirectDraw's linear heap
     * hands out disjoint blocks, so distinct bases cannot alias. */
    if (source_offset == destination_offset) {
        if (destination_y > source_y) {
            y_positive = 0;
            source_y += height - 1ul;
            destination_y += height - 1ul;
        } else if (destination_y == source_y && destination_x > source_x) {
            x_positive = 0;
            source_x += width - 1ul;
            destination_x += width - 1ul;
        }
    }

    if (!v9x_wait_fifo(8ul, wait)) {
        return V9X_BLT_BUSY;
    }
    command = V9X_VIRGE_CMD_ROP_SRCCOPY | V9X_VIRGE_CMD_DRAW_ENABLE |
              ((bytes_per_pixel - 1ul) << 2) |
              (x_positive ? V9X_VIRGE_CMD_X_POSITIVE : 0ul) |
              (y_positive ? V9X_VIRGE_CMD_Y_POSITIVE : 0ul);

    v9x_mmio_write(V9X_VIRGE_SRC_BASE, source_offset);
    v9x_mmio_write(V9X_VIRGE_DEST_BASE, destination_offset);
    v9x_mmio_write(V9X_VIRGE_DEST_SRC_STRIDE,
                   (destination_pitch << 16) | source_pitch);
    v9x_mmio_write(V9X_VIRGE_RECT_WH, ((width - 1ul) << 16) | height);
    v9x_mmio_write(V9X_VIRGE_RECT_SRC_XY, (source_x << 16) | source_y);
    v9x_mmio_write(V9X_VIRGE_RECT_DEST_XY,
                   (destination_x << 16) | destination_y);
    /* Autoexecute is left clear, so this write is what starts the blit. */
    v9x_mmio_write(V9X_VIRGE_COMMAND, command);
    return V9X_BLT_DONE;
}

static int v9x_virge_fill(V9X_DDHAL_BLTDATA *data, DWORD offset,
                          DWORD bytes_per_pixel, int wait)
{
    DWORD width = (DWORD)(data->rDest[2] - data->rDest[0]);
    DWORD height = (DWORD)(data->rDest[3] - data->rDest[1]);
    DWORD command;

    if (!v9x_wait_fifo(8ul, wait)) {
        return V9X_BLT_BUSY;
    }
    command = V9X_VIRGE_CMD_ROP_PATCOPY |
              V9X_VIRGE_CMD_X_POSITIVE | V9X_VIRGE_CMD_Y_POSITIVE |
              V9X_VIRGE_CMD_MONO_PATTERN | V9X_VIRGE_CMD_DRAW_ENABLE |
              ((bytes_per_pixel - 1ul) << 2);

    v9x_mmio_write(V9X_VIRGE_DEST_BASE, offset);
    v9x_mmio_write(V9X_VIRGE_PATTERN_FG, data->bltFX.dwFillColor);
    v9x_mmio_write(V9X_VIRGE_DEST_SRC_STRIDE,
                   (DWORD)data->lpDDDestSurface->lpGbl->lPitch << 16);
    v9x_mmio_write(V9X_VIRGE_MONO_PAT_0, 0xfffffffful);
    v9x_mmio_write(V9X_VIRGE_MONO_PAT_1, 0xfffffffful);
    v9x_mmio_write(V9X_VIRGE_RECT_WH, ((width - 1ul) << 16) | height);
    v9x_mmio_write(V9X_VIRGE_RECT_DEST_XY,
                   ((DWORD)data->rDest[0] << 16) | (DWORD)data->rDest[1]);
    v9x_mmio_write(V9X_VIRGE_COMMAND, command);
    return V9X_BLT_DONE;
}

const V9X_ENGINE32_OPS v9x_engine32_virge = {
    v9x_engine_ready,
    v9x_engine_validate_status,
    v9x_engine_status_validated,
    v9x_engine_validate_status,
    v9x_wait_idle,
    v9x_virge_fill,
    v9x_virge_copy
};

