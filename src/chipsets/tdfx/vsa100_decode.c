/*
 * 3dfx VSA-100 register decodes. See include\velocity9x\tdfx_vsa100.h for
 * the contract and docs\specifications\vsa100-registers.md for the Napalm
 * spec citations.
 */
#include "velocity9x/tdfx_vsa100.h"

/* dramInit0: bit 26 selects 8 memory parts (set) or 4 (clear); bits 29:27
 * are the part density, 8 Mbit << n, defined for n = 0..4. */
#define VSA100_DRAM_PARTS_8       0x04000000ul
#define VSA100_DRAM_DENSITY_SHIFT 27
#define VSA100_DRAM_DENSITY_MASK  7ul
#define VSA100_DRAM_DENSITY_MAX   4ul
#define VSA100_DRAM_BASE_MBIT     8ul
/* One Mbit of parts is 128 KiB of memory. */
#define VSA100_BYTES_PER_MBIT     131072ul

/* lfbMemoryConfig tile compare bit 15 makes compare bits 14:0 the boundary;
 * otherwise control bits 12:0 are its low page bits and control bits 24:23
 * its bits 14:13. */
#define VSA100_TILE_COMPARE_VALID 0x00008000ul
#define VSA100_TILE_COMPARE_PAGE  0x00007ffful
#define VSA100_TILE_CONTROL_LOW   0x00001ffful
#define VSA100_TILE_CONTROL_HIGH_SHIFT 10
#define VSA100_TILE_CONTROL_HIGH  0x00006000ul

/* The desktop split is rounded to whole MiB. */
#define VSA100_MIB                0x00100000ul

/* vidProcCfg bit 7 desktop fetch, bit 8 overlay fetch, bit 24 desktop in
 * tile space; vidDesktopOverlayStride bits 14:0 the desktop stride (Napalm
 * spec 11.1.3, 11.1.26). */
#define VSA100_VPC_DESKTOP_FETCH  0x00000080ul
#define VSA100_VPC_OVERLAY_FETCH  0x00000100ul
#define VSA100_VPC_DESKTOP_TILED  0x01000000ul
#define VSA100_DESKTOP_STRIDE_MASK 0x00007ffful

/* pllCtrl fields and the 14.31818 MHz reference, held in 10 Hz units so the
 * largest numerator (N = 255) stays below 2^32. */
#define VSA100_PLL_N_SHIFT        8
#define VSA100_PLL_N_MASK         0xfful
#define VSA100_PLL_M_SHIFT        2
#define VSA100_PLL_M_MASK         0x3ful
#define VSA100_PLL_K_MASK         3ul
#define VSA100_PLL_REF_10HZ       1431818ul
#define VSA100_PLL_10HZ_PER_KHZ   100ul

v9x_u32 v9x_tdfx_vsa100_decode_memory(v9x_u32 dram_init0)
{
    v9x_u32 parts;
    v9x_u32 density;

    density = (dram_init0 >> VSA100_DRAM_DENSITY_SHIFT) &
              VSA100_DRAM_DENSITY_MASK;
    if (density > VSA100_DRAM_DENSITY_MAX) {
        return 0ul;
    }

    parts = (dram_init0 & VSA100_DRAM_PARTS_8) != 0ul ? 8ul : 4ul;
    return parts * (VSA100_DRAM_BASE_MBIT << density) * VSA100_BYTES_PER_MBIT;
}

v9x_u32 v9x_tdfx_vsa100_tile_first_page(v9x_u32 tile_control,
                                         v9x_u32 tile_compare)
{
    if ((tile_compare & VSA100_TILE_COMPARE_VALID) != 0ul) {
        return tile_compare & VSA100_TILE_COMPARE_PAGE;
    }

    return (tile_control & VSA100_TILE_CONTROL_LOW) |
           ((tile_control >> VSA100_TILE_CONTROL_HIGH_SHIFT) &
            VSA100_TILE_CONTROL_HIGH);
}

v9x_u32 v9x_tdfx_vsa100_usable_memory(v9x_u32 dram_init0,
                                       v9x_u32 tile_control,
                                       v9x_u32 tile_compare)
{
    v9x_u32 decoded;
    v9x_u32 linear;

    decoded = v9x_tdfx_vsa100_decode_memory(dram_init0);
    linear = v9x_tdfx_vsa100_tile_first_page(tile_control, tile_compare) *
             V9X_TDFX_TILE_PAGE_BYTES;
    return linear < decoded ? linear : decoded;
}

v9x_u32 v9x_tdfx_vsa100_desktop_offset(v9x_u32 linear_bytes)
{
    if (linear_bytes < V9X_TDFX_SPLIT_MIN_BYTES) {
        return 0ul;
    }
    return (linear_bytes / 2ul) & ~(VSA100_MIB - 1ul);
}

v9x_u32 v9x_tdfx_vsa100_glide_bytes(v9x_u32 linear_bytes)
{
    v9x_u32 offset = v9x_tdfx_vsa100_desktop_offset(linear_bytes);

    if (offset == 0ul) {
        return linear_bytes;
    }
    return offset - V9X_TDFX_GLIDE_GUARD_BYTES;
}

v9x_u32 v9x_tdfx_vsa100_desktop_vidproc(v9x_u32 vid_proc_cfg)
{
    return (vid_proc_cfg | VSA100_VPC_DESKTOP_FETCH) &
           ~(VSA100_VPC_OVERLAY_FETCH | VSA100_VPC_DESKTOP_TILED);
}

v9x_u32 v9x_tdfx_vsa100_desktop_stride(v9x_u32 stride_register,
                                        v9x_u32 pitch_bytes)
{
    return (stride_register & ~VSA100_DESKTOP_STRIDE_MASK) |
           (pitch_bytes & VSA100_DESKTOP_STRIDE_MASK);
}

v9x_u32 v9x_tdfx_vsa100_pll_khz(v9x_u32 pll_ctrl)
{
    v9x_u32 n;
    v9x_u32 m;
    v9x_u32 k;

    n = (pll_ctrl >> VSA100_PLL_N_SHIFT) & VSA100_PLL_N_MASK;
    m = (pll_ctrl >> VSA100_PLL_M_SHIFT) & VSA100_PLL_M_MASK;
    k = pll_ctrl & VSA100_PLL_K_MASK;

    /* M = 0 is refused, not decoded: the Mini-Me's unused PLL0 holds it,
     * and the source that gives the formula excepts it. */
    if (m == 0ul) {
        return 0ul;
    }

    return VSA100_PLL_REF_10HZ * (n + 2ul) / (((m + 2ul) << k) *
                                              VSA100_PLL_10HZ_PER_KHZ);
}
