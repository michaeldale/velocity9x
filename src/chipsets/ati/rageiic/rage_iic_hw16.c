/*
 * ATI 3D Rage IIC AGP (Mach64 "GW", 264GT2C), PCI 1002:4757.
 *
 * The physical target is A8U4I5: subsystem 1002:4757, revision 0x7A, 4 MiB
 * SDRAM, BIOS "ATI MACH64 SDRAM BIOS 3.096", part MACH64GWPCIMTSDU. Measured
 * read-only under ATI's driver on 2026-10-02
 * (docs\decisions\2026-10-02-rage-iic-register-survey.md):
 *
 *     CONFIG_CHIP_ID   == 0x7A004757   ('GW', revision 7A)
 *     BAR2             4 KiB register window, block 0 at +400h
 *     MEM_CNTL code 7  == 4 MiB in the four-bit CTL_MEM_SIZEB table
 *     CFG_MEM_TYPE_T   == 4            (SDRAM)
 *
 * The VBE sets modes and reports the framebuffer; the aperture hook stays
 * NULL. The engine hook claims the Mach64 2D engine for DirectDraw fill and
 * copy, as ATI_RAGE2 - deliberately not ATI_MACH64. This is a Rage II part
 * with no triangle setup engine, and d3d_select.c routes ATI_MACH64 to
 * d3d_mach64.c, whose every triangle is a setup-engine packet. No D3D
 * capability is claimed until a Rage II back-end exists and has been
 * measured (docs\plans\ati-rage-iic-hardware-3d.md).
 */
#include "velocity9x/hw16.h"
#include "velocity9x/engine_abi.h"

/* runtime.asm, ati family only. */
extern unsigned short __far __pascal V9xPciReadAtiMmioBar(
    unsigned long __far *base);
extern unsigned short __far __pascal V9xMiniAtiMmioMap(
    unsigned long bar2, unsigned long __far *linear);

/* V9X_ATI_MMIO_BYTES in the mini-VDD: the 4 KiB window eng_mach64.c
 * addresses, block 0 at +400h. */
#define V9X_RAGE_IIC_MMIO_BYTES 0x00001000ul

/*
 * BAR2 mapped by the mini-VDD, which checks that CONFIG_CHIP_ID names a part
 * it knows before handing the window over; eng_mach64.c then checks it names
 * this one, 4757, for the ATI_RAGE2 type.
 */
static void v9x_rage_iic_fill_engine(unsigned long framebuffer_linear_base,
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
    if (V9xMiniAtiMmioMap(bar2, &linear) == 0u || linear == 0ul) {
        return;
    }
    *control_linear_base = linear;
    *mapped_aperture_bytes = V9X_RAGE_IIC_MMIO_BYTES;
    *engine_type = V9X_DD_ENGINE_TYPE_ATI_RAGE2;
    /* Direct3D through d3d_rage2.c: the CPU-setup engine Phase 5 built on
     * the Phase 1-4 measurements (docs\plans\ati-rage-iic-hardware-3d.md). */
    /* Flips and the vertical blank through CRTC_OFF_PITCH and
     * CRTC_VLINE (m64_scanout.c), measured on the monitor with ATIRX
     * /crtc (docs\decisions\2026-10-03-rage-iic-scanout-start.md). */
    *engine_caps = V9X_DD_ENGINE_CAP_SOLID_FILL |
                   V9X_DD_ENGINE_CAP_SCREEN_COPY |
                   V9X_DD_ENGINE_CAP_FLIP |
                   V9X_DD_ENGINE_CAP_VBLANK |
                   V9X_DD_ENGINE_CAP_D3D;
}

/* Not static: resolved by name in the link map by the per-object audit. */
const V9X_HW16_DEVICE v9x_rage_iic_device = {
    0x1002u, 0x4757u,
    "ATI 3D Rage IIC AGP",
    "1002", "4757",
    "ati-mach64-unavailable-v1",
    "vbe-lfb",
    "directdraw-fill-copy",
    "hardware-rage2",
    0,
    v9x_rage_iic_fill_engine,
    0
};
