/*
 * 3dfx VSA-100, PCI 121A:0009: every Voodoo4 and Voodoo5 board.
 *
 * Measured on a retail Voodoo4 4500 AGP in A8U4I5 on 2026-10-11, under 3dfx's
 * own 1.04.00 driver
 * (docs\decisions\2026-10-11-voodoo4-stock-driver-tiles-its-desktop-at-the-
 * top-of-memory.md): subsystem 0004121A, revision 01, one chip, 32 MiB SDRAM,
 * BAR0 registers and BAR1 framebuffer of 128 MiB each, BIOS 1.15.
 *
 * The VBE sets modes and reports the framebuffer. The engine hook claims the
 * 2D engine for DirectDraw fill and copy as TDFX. No enable step: the write
 * probe measured the engine working pixel-exact straight after the BIOS's VBE
 * mode set, with no init register written
 * (docs\decisions\2026-10-11-voodoo4-2d-engine-fills-and-copies-pixel-
 * exact.md). framebuffer_bar is 1 for the record; with no read_aperture hook
 * the aperture comes from VBE 4F01h and the field is not consulted.
 */
#include "velocity9x/hw16.h"
#include "velocity9x/engine_abi.h"

/* runtime.asm, 3dfx family only. */
extern unsigned short __far __pascal V9xPciReadTdfxMmioBar(
    unsigned long __far *base);
extern unsigned short __far __pascal V9xMiniTdfxMmioMap(
    unsigned long bar0, unsigned long __far *linear);

/* V9X_TDFX_MMIO_BYTES in the mini-VDD: the 2D block it maps. */
#define V9X_TDFX_MMIO_BYTES 0x00001000ul

/*
 * BAR0's 2D block mapped by the mini-VDD, which withholds a window whose
 * status reads all ones. Fill and copy only: page flips stay on the VGA
 * display-start path until the VSA-100's own start register is used.
 */
static void v9x_vsa100_fill_engine(unsigned long framebuffer_linear_base,
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

    if (V9xPciReadTdfxMmioBar(&bar0) == 0u) {
        return;
    }
    if (V9xMiniTdfxMmioMap(bar0, &linear) == 0u || linear == 0ul) {
        return;
    }
    *control_linear_base = linear;
    *mapped_aperture_bytes = V9X_TDFX_MMIO_BYTES;
    *engine_type = V9X_DD_ENGINE_TYPE_TDFX;
    *engine_caps = V9X_DD_ENGINE_CAP_SOLID_FILL |
                   V9X_DD_ENGINE_CAP_SCREEN_COPY;
}

/* Not static: resolved by name in the link map by the per-object audit. */
const V9X_HW16_DEVICE v9x_vsa100_device = {
    0x121au, 0x0009u,
    "3dfx VSA-100",
    "121A", "0009",
    "tdfx-vsa100-unavailable-v1",
    "vbe-lfb",
    "directdraw-fill-copy",
    0,
    0,
    v9x_vsa100_fill_engine,
    1u
};
