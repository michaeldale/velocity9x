#include "velocity9x/ati_mach64_crtc.h"

/* CRTC_VLINE_CRNT_VLINE: the current line in 26:16. CRTC_V_TOTAL_DISP:
 * displayed lines minus one in 26:16, total minus one in 10:0 (RRG). */
#define M64_CRTC_LINE_SHIFT  16u
#define M64_CRTC_LINE_MASK   0x000007fful

v9x_status v9x_m64_crtc_start(v9x_u32 off_pitch, v9x_u32 byte_offset,
                              v9x_u32 vram_bytes, v9x_u32 *value)
{
    if (value == 0 || (byte_offset & 7ul) != 0ul ||
        byte_offset >= vram_bytes ||
        (byte_offset >> 3) > V9X_M64_CRTC_OFFSET_MASK) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    *value = (off_pitch & V9X_M64_CRTC_KEEP_MASK) | (byte_offset >> 3);
    return V9X_STATUS_OK;
}

int v9x_m64_crtc_in_blank(v9x_u32 vline, v9x_u32 v_total_disp)
{
    v9x_u32 line = (vline >> M64_CRTC_LINE_SHIFT) & M64_CRTC_LINE_MASK;
    v9x_u32 displayed =
        ((v_total_disp >> M64_CRTC_LINE_SHIFT) & M64_CRTC_LINE_MASK) + 1ul;
    v9x_u32 total = (v_total_disp & M64_CRTC_LINE_MASK) + 1ul;

    return displayed < total && line >= displayed && line < total;
}

int v9x_m64_crtc_flip_window(v9x_u32 vline, v9x_u32 v_total_disp)
{
    v9x_u32 line = (vline >> M64_CRTC_LINE_SHIFT) & M64_CRTC_LINE_MASK;
    v9x_u32 total = (v_total_disp & M64_CRTC_LINE_MASK) + 1ul;

    return v9x_m64_crtc_in_blank(vline, v_total_disp) &&
           line + V9X_M64_CRTC_FLIP_GUARD < total;
}
