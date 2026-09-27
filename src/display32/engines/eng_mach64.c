/* ATI Mach64 engine wrapper for the flat DirectDraw HAL.
 *
 * Phase 1 only: status validation, bounded idle, recovery and CPU-coherence
 * plumbing. Fill and copy deliberately decline until their physical Phase 2
 * scenes pass, and no ATI manifest publishes this engine type yet.
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
    if (!v9x_m64_bind_core()) return 0;
    if ((v9x_hal->engine.flags & V9X_DD_ENGINE_STATUS_VALIDATED) != 0ul)
        return 1;
    chip = (DWORD)v9x_m64_hal_read(0, V9X_M64_CONFIG_CHIP_ID);
    if ((chip & 0xfffful) != 0x4c4dul) return 0;
    v9x_hal->engine.flags |= V9X_DD_ENGINE_STATUS_VALIDATED;
    return 1;
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
    status = v9x_m64_wait_idle(&v9x_m64, wait ? V9X_M64_WAIT_SPINS : 0ul);
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
    status = v9x_m64_emit_batch(&v9x_m64, offsets, values, written,
                                wait ? V9X_M64_WAIT_SPINS : 0ul);
    if (status == V9X_STATUS_TIMEOUT) return V9X_BLT_BUSY;
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

static int v9x_m64_no_copy(V9X_DDHAL_BLTDATA *data, DWORD source_offset,
                           DWORD destination_offset,
                           DWORD bytes_per_pixel, int wait)
{
    (void)data; (void)source_offset; (void)destination_offset;
    (void)bytes_per_pixel; (void)wait;
    return V9X_BLT_DECLINED;
}

const V9X_ENGINE32_OPS v9x_engine32_mach64 = {
    v9x_m64_ready,
    v9x_m64_validate,
    v9x_m64_validated,
    v9x_m64_can_blt,
    v9x_m64_wait,
    v9x_m64_fill,
    v9x_m64_no_copy
};
