/* ATI Mach64 engine wrapper for the flat DirectDraw HAL.
 *
 * Status validation, bounded idle, recovery and CPU-coherence plumbing, and
 * the fill and screen copy the Phase 2 scenes measured on the Gateway.
 */
#include "ddhal_internal.h"
#include "velocity9x/ati_mach64_engine.h"

#define V9X_M64_WAIT_SPINS 0x00200000ul

static struct v9x_m64_engine v9x_m64;
static DWORD v9x_m64_base = 0ul;

static int v9x_m64_ready(void)
{
    return v9x_hal != 0 &&
        (v9x_hal->fb.flags & V9X_DD_FB_VALID) != 0ul &&
        (v9x_hal->engine.flags & V9X_DD_ENGINE_VALID) != 0ul &&
        v9x_hal->engine.engine_type == V9X_DD_ENGINE_TYPE_ATI_MACH64 &&
        v9x_hal->engine.control_linear_base != 0ul &&
        v9x_hal->engine.mapped_aperture_bytes >= 0x1000ul;
}

static v9x_u32 v9x_m64_hal_read(void *context, v9x_u32 offset)
{
    volatile DWORD *reg;
    (void)context;
    reg = (volatile DWORD *)(v9x_hal->engine.control_linear_base + offset);
    return (v9x_u32)*reg;
}

static void v9x_m64_hal_write(void *context, v9x_u32 offset, v9x_u32 value)
{
    volatile DWORD *reg;
    (void)context;
    reg = (volatile DWORD *)(v9x_hal->engine.control_linear_base + offset);
    *reg = (DWORD)value;
}

static int v9x_m64_bind_core(void)
{
    struct v9x_m64_io io;
    if (!v9x_m64_ready()) return 0;
    if (v9x_m64_base == v9x_hal->engine.control_linear_base) return 1;
    io.context = 0;
    io.read = v9x_m64_hal_read;
    io.write = v9x_m64_hal_write;
    if (v9x_m64_engine_init(&v9x_m64, &io, V9X_M64_FIFO_VTB_PLUS) !=
        V9X_STATUS_OK) return 0;
    v9x_m64_base = v9x_hal->engine.control_linear_base;
    return 1;
}

static int v9x_m64_validate(void)
{
    DWORD chip;
    DWORD bus;
    if (!v9x_m64_bind_core()) return 0;
    if ((v9x_hal->engine.flags & V9X_DD_ENGINE_STATUS_VALIDATED) != 0ul)
        return 1;
    chip = (DWORD)v9x_m64_hal_read(0, V9X_M64_CONFIG_CHIP_ID);
    if ((chip & 0xfffful) != 0x4c4dul) return 0;
    /*
     * Block 1 on, once, after the identity is proven. Without it every
     * setup-engine write lands in a disabled block. The Phase 1-4 scenes
     * never needed this only because ATI's driver had already set it.
     */
    bus = (DWORD)v9x_m64_hal_read(0, V9X_M64_BUS_CNTL);
    if ((bus & V9X_M64_BUS_EXT_REG_EN) == 0ul) {
        v9x_m64_hal_write(0, V9X_M64_BUS_CNTL, bus | V9X_M64_BUS_EXT_REG_EN);
    }
    v9x_hal->engine.flags |= V9X_DD_ENGINE_STATUS_VALIDATED;
    return 1;
}

/*
 * The validated core, for the Direct3D engine. One instance serves 2D and
 * 3D so both reserve against the same cached FIFO count, share one
 * quarantine, and replay one shadow after a reset; two cores would each
 * believe the FIFO slots the other had used were free.
 */
struct v9x_m64_engine *v9x_m64_shared_core(void)
{
    if (!v9x_m64_validate()) {
        return 0;
    }
    return &v9x_m64;
}

static int v9x_m64_validated(void)
{
    return v9x_m64_ready() &&
        (v9x_hal->engine.flags & V9X_DD_ENGINE_STATUS_VALIDATED) != 0ul;
}

static int v9x_m64_wait(int wait)
{
    v9x_status status;
    if (!v9x_m64_validate()) return 0;
    /*
     * This is the drain every CPU access to engine-written VRAM passes
     * (Lock, the CPU fallback, the render drain), so it is the read-cache
     * boundary too: without INVALIDATE_RB_CACHE the CPU can read pixels
     * the engine has since overwritten (Phase 1 CPU coherence).
     */
    status = v9x_m64_cpu_read_barrier(&v9x_m64,
                                      wait ? V9X_M64_WAIT_SPINS : 0ul);
    if (status == V9X_STATUS_OK) return 1;
    if (!wait || status != V9X_STATUS_TIMEOUT) return 0;
    return v9x_m64_reset_replay(&v9x_m64, V9X_M64_WAIT_SPINS) ==
        V9X_STATUS_OK;
}

static int v9x_m64_can_blt(void)
{
    return v9x_m64_wait(0);
}

static int v9x_m64_fill(V9X_DDHAL_BLTDATA *data, DWORD offset,
                        DWORD bytes_per_pixel, int wait)
{
    struct v9x_m64_fill fill;
    v9x_u32 offsets[V9X_M64_FILL_DWORDS];
    v9x_u32 values[V9X_M64_FILL_DWORDS];
    v9x_u32 repair_offsets[V9X_M64_FILL_REPAIR_DWORDS];
    v9x_u32 repair_values[V9X_M64_FILL_REPAIR_DWORDS];
    v9x_u32 mode_offsets[V9X_M64_2D_MODE_DWORDS];
    v9x_u32 mode_values[V9X_M64_2D_MODE_DWORDS];
    v9x_u32 mode_written = 0ul;
    v9x_u32 written = 0ul;
    v9x_u32 repair_written = 0ul;
    v9x_status status;
    if (!v9x_m64_validate() || data == 0 || data->lpDDDestSurface == 0 ||
        data->lpDDDestSurface->lpGbl == 0 || bytes_per_pixel != 2ul ||
        data->lpDDDestSurface->lpGbl->lPitch <= 0l) {
        return V9X_BLT_DECLINED;
    }
    fill.vram_bytes = v9x_hal->fb.vram_bytes;
    fill.target_offset = offset;
    fill.target_pitch_bytes =
        (DWORD)data->lpDDDestSurface->lpGbl->lPitch;
    fill.target_width = fill.target_pitch_bytes >> 1;
    fill.target_height =
        (DWORD)data->lpDDDestSurface->lpGbl->wHeight;
    fill.left = (DWORD)data->rDest[0];
    fill.top = (DWORD)data->rDest[1];
    fill.right = (DWORD)data->rDest[2];
    fill.bottom = (DWORD)data->rDest[3];
    fill.color = data->bltFX.dwFillColor;
    if (v9x_m64_build_fill(&fill, offsets, values,
                            V9X_M64_FILL_DWORDS, &written) != V9X_STATUS_OK) {
        return V9X_BLT_DECLINED;
    }
    if (v9x_m64_build_fill_origin_repair(
            &fill, repair_offsets, repair_values,
            V9X_M64_FILL_REPAIR_DWORDS, &repair_written) != V9X_STATUS_OK) {
        return V9X_BLT_DECLINED;
    }
    /* 2D mode first: a Direct3D draw may have left Z, alpha test and the
     * 3D pixel pipe enabled (v9x_m64_build_2d_mode). */
    if (v9x_m64_build_2d_mode(mode_offsets, mode_values,
                              V9X_M64_2D_MODE_DWORDS,
                              &mode_written) != V9X_STATUS_OK) {
        return V9X_BLT_DECLINED;
    }
    status = v9x_m64_emit_batch(&v9x_m64, mode_offsets, mode_values,
                                mode_written,
                                wait ? V9X_M64_WAIT_SPINS : 0ul);
    if (status == V9X_STATUS_TIMEOUT) return V9X_BLT_BUSY;
    if (status != V9X_STATUS_OK) return V9X_BLT_DECLINED;
    status = v9x_m64_emit_batch(&v9x_m64, offsets, values, written,
                                V9X_M64_WAIT_SPINS);
    if (status != V9X_STATUS_OK) return V9X_BLT_DECLINED;
    v9x_present_note_submission();
    if (repair_written != 0ul) {
        status = v9x_m64_wait_idle(&v9x_m64, V9X_M64_WAIT_SPINS);
        if (status != V9X_STATUS_OK) return V9X_BLT_DONE;
        status = v9x_m64_emit_batch(&v9x_m64, repair_offsets, repair_values,
                                    repair_written, V9X_M64_WAIT_SPINS);
        if (status != V9X_STATUS_OK) return V9X_BLT_DONE;
        status = v9x_m64_wait_idle(&v9x_m64, V9X_M64_WAIT_SPINS);
        if (status != V9X_STATUS_OK) return V9X_BLT_DONE;
    }
    return V9X_BLT_DONE;
}

/*
 * A screen-to-screen copy on the engine: the Phase 2 stream
 * (v9x_m64_build_copy), which matched a CPU memmove for all four overlap
 * directions and presented 1,001 back-to-front copies across live mode
 * switches on the Gateway (docs/probe/ati-rage-mobility-m-phase2-copy-
 * 2026-09-27). Until 2026-09-29 this declined and every Blt was a CPU copy:
 * 88 ms a 640x480 frame for the OpenGL ICD's SwapBuffers, reading the back
 * buffer across the bus. The idle wait after it is the plan's workaround for
 * the documented Mobility screen-copy commit race, kept until a capture
 * shows it can go.
 */
static int v9x_m64_copy(V9X_DDHAL_BLTDATA *data, DWORD source_offset,
                        DWORD destination_offset,
                        DWORD bytes_per_pixel, int wait)
{
    struct v9x_m64_copy copy;
    v9x_u32 offsets[V9X_M64_COPY_DWORDS];
    v9x_u32 values[V9X_M64_COPY_DWORDS];
    v9x_u32 mode_offsets[V9X_M64_2D_MODE_DWORDS];
    v9x_u32 mode_values[V9X_M64_2D_MODE_DWORDS];
    v9x_u32 mode_written = 0ul;
    v9x_u32 written = 0ul;
    v9x_status status;

    if (!v9x_m64_validate() || data == 0 || bytes_per_pixel != 2ul ||
        data->lpDDSrcSurface == 0 || data->lpDDSrcSurface->lpGbl == 0 ||
        data->lpDDDestSurface == 0 || data->lpDDDestSurface->lpGbl == 0 ||
        data->lpDDSrcSurface->lpGbl->lPitch <= 0l ||
        data->lpDDDestSurface->lpGbl->lPitch <= 0l) {
        return V9X_BLT_DECLINED;
    }
    copy.vram_bytes = v9x_hal->fb.vram_bytes;
    copy.source_offset = source_offset;
    copy.source_pitch_bytes = (DWORD)data->lpDDSrcSurface->lpGbl->lPitch;
    copy.source_width = copy.source_pitch_bytes >> 1;
    copy.source_height = (DWORD)data->lpDDSrcSurface->lpGbl->wHeight;
    copy.destination_offset = destination_offset;
    copy.destination_pitch_bytes =
        (DWORD)data->lpDDDestSurface->lpGbl->lPitch;
    copy.destination_width = copy.destination_pitch_bytes >> 1;
    copy.destination_height = (DWORD)data->lpDDDestSurface->lpGbl->wHeight;
    copy.source_left = (DWORD)data->rSrc[0];
    copy.source_top = (DWORD)data->rSrc[1];
    copy.destination_left = (DWORD)data->rDest[0];
    copy.destination_top = (DWORD)data->rDest[1];
    copy.width = (DWORD)(data->rSrc[2] - data->rSrc[0]);
    copy.height = (DWORD)(data->rSrc[3] - data->rSrc[1]);
    if (v9x_m64_build_copy(&copy, offsets, values, V9X_M64_COPY_DWORDS,
                           &written) != V9X_STATUS_OK) {
        return V9X_BLT_DECLINED;
    }
    /* 2D mode first, as for a fill (v9x_m64_build_2d_mode). */
    if (v9x_m64_build_2d_mode(mode_offsets, mode_values,
                              V9X_M64_2D_MODE_DWORDS,
                              &mode_written) != V9X_STATUS_OK) {
        return V9X_BLT_DECLINED;
    }
    status = v9x_m64_emit_batch(&v9x_m64, mode_offsets, mode_values,
                                mode_written,
                                wait ? V9X_M64_WAIT_SPINS : 0ul);
    if (status == V9X_STATUS_TIMEOUT) return V9X_BLT_BUSY;
    if (status != V9X_STATUS_OK) return V9X_BLT_DECLINED;
    status = v9x_m64_emit_batch(&v9x_m64, offsets, values, written,
                                V9X_M64_WAIT_SPINS);
    if (status != V9X_STATUS_OK) return V9X_BLT_DECLINED;
    v9x_present_note_submission();
    (void)v9x_m64_wait_idle(&v9x_m64, V9X_M64_WAIT_SPINS);
    return V9X_BLT_DONE;
}

const V9X_ENGINE32_OPS v9x_engine32_mach64 = {
    v9x_m64_ready,
    v9x_m64_validate,
    v9x_m64_validated,
    v9x_m64_can_blt,
    v9x_m64_wait,
    v9x_m64_fill,
    v9x_m64_copy
};
