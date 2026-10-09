/*
 * Matrox Millennium, MGA-2064W, PCI 102B:0519, with a TI TVP3026 RAMDAC.
 *
 * The card in A8U4I5 from 2026-10-09: revision 01, subsystem 0000, BIOS
 * VBE 2.0. Configuration Manager places its 16 KiB control aperture
 * (MGABASE1) in BAR0 and its framebuffer (MGABASE2) in BAR1, the reverse
 * of the Millennium II; every linear-framebuffer mode the BIOS advertises
 * reports its PhysBasePtr at the BAR1 base
 * (docs\decisions\2026-09-10-the-2064w-is-drivable-by-the-vbe-path.md).
 *
 * The VBE sets modes and 4F01h reports the framebuffer. The engine hook
 * has the mini-VDD map the control aperture and claims the drawing engine
 * for DirectDraw fill and copy as MGA_2064W. No register is written here:
 * the engine's own state (MACCESS, the plane mask, the clip window) is
 * written by the HAL before its first operation in each mode
 * (src\display32\engines\eng_mga.c), and the BIOS's mode set already turns
 * mgamode on (CRTCEXT3 81h, A8U4I5 boot 356).
 */
#include "velocity9x/hw16.h"
#include "velocity9x/engine_abi.h"

/* runtime.asm, matrox family only. */
extern unsigned short __far __pascal V9xPciReadMgaMmioBar(
    unsigned long __far *base);
extern unsigned short __far __pascal V9xMiniMgaMmioMap(
    unsigned long bar0, unsigned long __far *linear);

/* V9X_MGA_MMIO_BYTES in the mini-VDD. */
#define V9X_MGA_MMIO_BYTES 0x00004000ul

/*
 * BAR0 mapped by the mini-VDD, which withholds a window whose STATUS reads
 * all ones. A refusal anywhere leaves the chip on the CPU path; it never
 * fails Enable.
 */
static void v9x_mga2064w_fill_engine(unsigned long framebuffer_linear_base,
                                     unsigned long *control_linear_base,
                                     unsigned long *mapped_aperture_bytes,
                                     unsigned long *engine_type,
                                     unsigned long *engine_caps,
                                     unsigned long *gtt_linear_base,
                                     unsigned long *ring_linear_base,
                                     unsigned long *ring_bytes)
{
    unsigned long bar0 = 0ul;
    unsigned long linear = 0ul;

    (void)framebuffer_linear_base;
    *control_linear_base = 0ul;
    *mapped_aperture_bytes = 0ul;
    *engine_type = V9X_DD_ENGINE_TYPE_NONE;
    *engine_caps = 0ul;
    *gtt_linear_base = 0ul;
    *ring_linear_base = 0ul;
    *ring_bytes = 0ul;

    if (V9xPciReadMgaMmioBar(&bar0) == 0u) {
        return;
    }
    if (V9xMiniMgaMmioMap(bar0, &linear) == 0u || linear == 0ul) {
        return;
    }
    *control_linear_base = linear;
    *mapped_aperture_bytes = V9X_MGA_MMIO_BYTES;
    *engine_type = V9X_DD_ENGINE_TYPE_MGA_2064W;
    *engine_caps = V9X_DD_ENGINE_CAP_SOLID_FILL |
                   V9X_DD_ENGINE_CAP_SCREEN_COPY;
}

/* Not static: resolved by name in the link map by the per-object audit. */
const V9X_HW16_DEVICE v9x_mga2064w_device = {
    0x102bu, 0x0519u,
    "Matrox Millennium MGA-2064W",
    "102B", "0519",
    "matrox-mga2064w-unavailable-v1",
    "vbe-lfb",
    "directdraw-fill-copy",
    0,
    0,
    v9x_mga2064w_fill_engine,
    /* MGABASE2 is BAR1 on this chip. No hook in this family reads the
     * framebuffer BAR - the BIOS reports it - so this is stated, not
     * consulted. */
    1u
};
