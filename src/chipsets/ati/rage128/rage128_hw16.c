/*
 * ATI Rage 128 Pro, PCI 1002:5046 ("PF", AGP 4x).
 *
 * The physical target is A8U4I5 since 2026-10-22: subsystem 0018:1002,
 * revision 00, on the AGP bus (Enum \000800), 32 MiB SDR. Windows assigned
 * a 64 MiB prefetchable framebuffer window at DC000000 (BAR0) and a 16 KiB
 * register window at E1000000 (BAR2) (docs\plans\ati-rage128.md).
 *
 * The Rage 128 is not a Mach64. Its register file, 2D engine (GUI_* and
 * DP_*), CRTC and PLL are a new design, its register window is 16 KiB where
 * the Mach64's is 4 KiB, and it has no CONFIG_CHIP_ID at the Mach64's
 * offset. So it has a map of its own in the mini-VDD and an engine type of
 * its own, ATI_RAGE128, and never reaches mach64_engine.c.
 *
 * The VBE sets modes and reports the framebuffer. The engine hook claims the
 * 2D engine for DirectDraw fill and copy, whose builder values were
 * measured byte-exact at 8, 16 and 32 bpp with the engine in NONPM4, as POST
 * leaves it (docs\decisions\2026-10-22-rage128-2d-write-probe.md). Its init
 * list (scissors, default pitch) is written by eng_r128.c once per mode.
 */
#include "velocity9x/hw16.h"
#include "velocity9x/engine_abi.h"

/* runtime.asm, ati family only. BAR2 is read by the same config access the
 * Mach64 parts use; the map is the Rage 128's own. */
extern unsigned short __far __pascal V9xPciReadAtiMmioBar(
    unsigned long __far *base);
extern unsigned short __far __pascal V9xMiniR128MmioMap(
    unsigned long bar2, unsigned long __far *linear);
extern unsigned short __far __pascal V9xMiniR128AgpRing(
    unsigned long __far *linear, unsigned long __far *bus,
    unsigned long __far *bytes);

/* V9X_R128_MMIO_BYTES in the mini-VDD: the register BAR. */
#define V9X_RAGE128_MMIO_BYTES 0x00004000ul

/* The CCE ring's bus address in the AGP aperture, for
 * v9x_rage128_ring_bus; 0 without one. */
static unsigned long v9x_rage128_ring_bus_address = 0ul;

static void v9x_rage128_fill_engine(unsigned long framebuffer_linear_base,
                                    unsigned long *control_linear_base,
                                    unsigned long *mapped_aperture_bytes,
                                    unsigned long *engine_type,
                                    unsigned long *engine_caps,
                                    unsigned long *gtt_linear_base,
                                    unsigned long *ring_linear_base,
                                    unsigned long *ring_bytes)
{
    unsigned long bar2 = 0ul;
    unsigned long linear = 0ul;
    unsigned long ring_linear = 0ul;
    unsigned long ring_bus = 0ul;
    unsigned long bytes = 0ul;

    (void)framebuffer_linear_base;
    *control_linear_base = 0ul;
    *mapped_aperture_bytes = 0ul;
    *engine_type = V9X_DD_ENGINE_TYPE_NONE;
    *engine_caps = 0ul;
    *gtt_linear_base = 0ul;
    *ring_linear_base = 0ul;
    *ring_bytes = 0ul;

    if (V9xPciReadAtiMmioBar(&bar2) == 0u) {
        return;
    }
    if (V9xMiniR128MmioMap(bar2, &linear) == 0u || linear == 0ul) {
        return;
    }
    *control_linear_base = linear;
    *mapped_aperture_bytes = V9X_RAGE128_MMIO_BYTES;
    *engine_type = V9X_DD_ENGINE_TYPE_ATI_RAGE128;
    *engine_caps = V9X_DD_ENGINE_CAP_SOLID_FILL |
                   V9X_DD_ENGINE_CAP_SCREEN_COPY |
                   V9X_DD_ENGINE_CAP_FLIP |
                   V9X_DD_ENGINE_CAP_VBLANK |
                   V9X_DD_ENGINE_CAP_D3D;

    /* The bus-master ring's pages, when the chipset has a GART driver.
     * The HAL decides whether the card can fetch from them (AGP mode) and
     * otherwise keeps the PIO CCE; here they are only passed on. */
    if (V9xMiniR128AgpRing(&ring_linear, &ring_bus, &bytes) != 0u &&
        ring_linear != 0ul && ring_bus != 0ul) {
        *ring_linear_base = ring_linear;
        *ring_bytes = bytes;
        v9x_rage128_ring_bus_address = ring_bus;
    }
}

/* engine.reserve_offset: the ring's bus address, from which the HAL
 * derives AGP_BASE and PM4_BUFFER_OFFSET. */
static unsigned long v9x_rage128_ring_bus(void)
{
    return v9x_rage128_ring_bus_address;
}

/*
 * Not static: the per-object audit resolves this symbol by name out of the
 * link map to prove this chip's module is actually in the family image.
 */
const V9X_HW16_DEVICE v9x_rage128_device = {
    0x1002u, 0x5046u,
    "ATI Rage 128 Pro",
    "1002", "5046",
    "ati-rage128-unavailable-v1",
    "vbe-lfb",
    "directdraw-fill-copy",
    "hardware-rage128-cce",
    0,
    v9x_rage128_fill_engine,
    0u,
    0u,
    0ul,
    v9x_rage128_ring_bus
};
