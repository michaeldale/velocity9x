/*
 * ATI Rage Mobility-M AGP (Mach64 "LM"), PCI 1002:4C4D.
 *
 * The physical target: a Gateway Solo 2150, subsystem 107B:2150, revision 0x64,
 * BIOS "ATI MACH64 SDRAM BIOS 4.216", part MACH64LMPCIMTSDU. Its display is a
 * fixed 1024x768 LG LP141XA panel with no EDID - the panel identity was decoded
 * out of the captured video BIOS rather than read from the monitor.
 *
 * Four values are known in advance and are worth asserting the first time this
 * chip is probed, because each proves a different register window is live:
 *
 *     CONFIG_CHIP_ID   low word  == 0x4C4D   ('LM')
 *     HORZ_PANEL_SIZE            == 127      (1024 = (127 + 1) * 8)
 *     VERT_PANEL_SIZE            == 767
 *     CFG_MEM_TYPE_T             == 6        (measured twice on 2026-09-27)
 *
 * Code 6 decodes as 32-bit SGRAM at 2:1, contradicting the generic "SDRAM"
 * BIOS wording.  Keep block write disabled and resolve that pre-flight
 * contradiction before either hook can issue hardware writes.
 *
 * The aperture hook is NULL: the VBE sets modes and reports the framebuffer.
 * If it is filled in, note that this part is >= 264VTB, so it decodes video
 * memory with the four-bit CTL_MEM_SIZEB table, not the three-bit CTL_MEM_SIZE
 * one its VT2 sibling uses - the two disagree for every code >= 2, and code 3
 * means 4 MiB on a VT and 2 MiB here.
 * See docs\decisions\2026-08-16-ati-mach64-hardware-audit.md.
 */
#include "velocity9x/hw16.h"
#include "velocity9x/engine_abi.h"

/* runtime.asm, ati family only. */
extern unsigned short __far __pascal V9xPciReadAtiMmioBar(
    unsigned long __far *base);
extern unsigned short __far __pascal V9xMiniAtiMmioMap(
    unsigned long bar2, unsigned long __far *linear);

/* V9X_ATI_MMIO_BYTES in the mini-VDD: the register file eng_mach64.c and
 * d3d_mach64.c address, block 0 at +400h. */
#define V9X_MOBILITY_MMIO_BYTES 0x00001000ul

/*
 * The Mach64 engine: BAR2 mapped by the mini-VDD, which also checks that
 * CONFIG_CHIP_ID names this part before handing the window over.
 *
 * Claims D3D and nothing else. The 2D fill still reaches the engine by
 * type, because the HAL routes DirectDraw colour and depth fills by
 * engine_type; it is the Phase 2 fill/clear stream. Screen copy declines in
 * eng_mach64.c, so no copy claim is made.
 *
 * VT2 never gets this: it has no Rage setup engine, and its entry keeps a
 * NULL hook.
 */
static void v9x_mobility_fill_engine(unsigned long framebuffer_linear_base,
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
    *mapped_aperture_bytes = V9X_MOBILITY_MMIO_BYTES;
    *engine_type = V9X_DD_ENGINE_TYPE_ATI_MACH64;
    *engine_caps = V9X_DD_ENGINE_CAP_D3D;
}

/* Not static: resolved by name in the link map by the per-object audit. */
const V9X_HW16_DEVICE v9x_rage_mobility_device = {
    0x1002u, 0x4c4du,
    "ATI Rage Mobility-M AGP",
    "1002", "4C4D",
    "ati-mach64-unavailable-v1",
    "vbe-lfb",
    "directdraw-fill",
    "hardware-mach64",
    0,
    v9x_mobility_fill_engine,
    0
};
