/*
 * Which BAR holds a Matrox chip's control aperture. Pure policy: see
 * include\velocity9x\mga_bars.h for the rule and its sources.
 */
#include "velocity9x/mga_bars.h"

/*
 * Lowest control base accepted. An assigned PCI memory window sits above
 * installed RAM, and a base under 16 MiB is an unassigned or ISA-hole value
 * rather than an aperture; the bound runtime.asm applied before this split.
 */
#define V9X_MGA_CONTROL_BASE_MIN ((v9x_u32)0x01000000ul)

/* The highest base whose 16 KiB still fits under 4 GiB. */
#define V9X_MGA_CONTROL_BASE_MAX ((v9x_u32)0xFFFFC000ul)

/* A 32-bit memory BAR: not I/O, type 00. A 64-bit BAR (type 10) would make
 * the next BAR its upper half, which no Millennium or Mystique has. */
static v9x_u16 v9x_mga_bar_is_memory32(v9x_u32 bar)
{
    if ((bar & V9X_MGA_BAR_IO_SPACE) != 0ul) {
        return V9X_FALSE;
    }
    if ((bar & V9X_MGA_BAR_TYPE_MASK) != 0ul) {
        return V9X_FALSE;
    }
    return V9X_TRUE;
}

v9x_u16 v9x_matrox_mga_select_control_bar(v9x_u32 bar0, v9x_u32 bar1,
                                          v9x_u16 fallback_control_index,
                                          v9x_u16 *control_index,
                                          v9x_u32 *control_base)
{
    v9x_u16 index;
    v9x_u32 base;
    v9x_u16 prefetch0;
    v9x_u16 prefetch1;

    if (control_index == 0 || control_base == 0) {
        return V9X_FALSE;
    }
    *control_index = 0u;
    *control_base = 0ul;

    if (v9x_mga_bar_is_memory32(bar0) != V9X_TRUE ||
        v9x_mga_bar_is_memory32(bar1) != V9X_TRUE) {
        return V9X_FALSE;
    }

    /* Exactly one prefetchable BAR, the framebuffer; the other is control.
     * Neither is 86Box, where the chip's stated order decides; both is a
     * layout no source shows. */
    prefetch0 = (bar0 & V9X_MGA_BAR_PREFETCHABLE) != 0ul ? V9X_TRUE : V9X_FALSE;
    prefetch1 = (bar1 & V9X_MGA_BAR_PREFETCHABLE) != 0ul ? V9X_TRUE : V9X_FALSE;
    if (prefetch0 == V9X_TRUE && prefetch1 == V9X_TRUE) {
        return V9X_FALSE;
    }
    if (prefetch0 == V9X_TRUE) {
        index = 1u;
    } else if (prefetch1 == V9X_TRUE) {
        index = 0u;
    } else if (fallback_control_index <= 1u) {
        index = fallback_control_index;
    } else {
        return V9X_FALSE;
    }
    base = (index == 0u ? bar0 : bar1) & V9X_MGA_BAR_ADDRESS_MASK;

    if (base < V9X_MGA_CONTROL_BASE_MIN || base > V9X_MGA_CONTROL_BASE_MAX) {
        return V9X_FALSE;
    }
    if ((base & (V9X_MGA_CONTROL_APERTURE_BYTES - 1ul)) != 0ul) {
        return V9X_FALSE;
    }

    *control_index = index;
    *control_base = base;
    return V9X_TRUE;
}
