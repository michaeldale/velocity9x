/*
 * ATI Radeon X1200 IGP, PCI 1002:791F (RS690).
 *
 * The physical target is the remote-help machine M-GB8KRE since
 * 2026-11-03: subsystem 1002:053A, at 01:05.0, a 128 MiB carve-out of
 * system memory at GPU address 78000000. Windows assigned BAR0 (the frame
 * buffer, 64-bit) at F0000000, BAR2 (the 64 KiB register window, 64-bit)
 * at F8100000, I/O at 9000 and a 1 MiB BAR5 nothing here uses
 * (docs\decisions\2026-11-04-rs690-register-probe.md).
 *
 * The RS690 keeps the Radeon 2D engine and an R300-class 3D core behind
 * AVIVO display controllers; it shares register layouts with the Rage 128
 * only by descent, and none of the code. The VBE sets modes and reports
 * the frame buffer. The engine hook claims the 2D engine for DirectDraw
 * fill and copy, whose builder values were measured byte-exact at 8, 16
 * and 32 bpp with the CP's queues off, as VBE leaves them
 * (docs\decisions\2026-11-04-rs690-2d-write-probe.md). eng_rs690.c writes
 * the init list once per mode.
 *
 * BAR0 and BAR2 are read by V9xPciReadEgBars, Evergreen's 64-bit-aware
 * read, whose alignment checks (BAR0 256 MiB, BAR2 128 KiB) M-GB8KRE's
 * BARs meet; the map is the RS690's own.
 */
#include "velocity9x/hw16.h"
#include "velocity9x/engine_abi.h"

/* runtime.asm, ati family only. */
extern unsigned short __far __pascal V9xPciReadEgBars(
    unsigned long __far *bar0, unsigned long __far *bar2);
extern unsigned short __far __pascal V9xMiniRs690MmioMap(
    unsigned long bar2, unsigned long __far *linear);

/* V9X_RS690_MMIO_BYTES in the mini-VDD: the register BAR. */
#define V9X_RS690_MMIO_BYTES 0x00010000ul

static void v9x_rs690_fill_engine(unsigned long framebuffer_linear_base,
                                  unsigned long *control_linear_base,
                                  unsigned long *mapped_aperture_bytes,
                                  unsigned long *engine_type,
                                  unsigned long *engine_caps,
                                  unsigned long *gtt_linear_base,
                                  unsigned long *ring_linear_base,
                                  unsigned long *ring_bytes)
{
    unsigned long bar0 = 0ul;
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

    if (V9xPciReadEgBars(&bar0, &bar2) == 0u) {
        return;
    }
    if (V9xMiniRs690MmioMap(bar2, &linear) == 0u || linear == 0ul) {
        return;
    }
    *control_linear_base = linear;
    *mapped_aperture_bytes = V9X_RS690_MMIO_BYTES;
    *engine_type = V9X_DD_ENGINE_TYPE_ATI_RS690;
    /* FLIP: D1GRPH, where the register probe read the VBE desktop
     * (rs690_scanout.c). D3D: the 3D core by register writes, through
     * the same window (d3d_rs690.c). */
    *engine_caps = V9X_DD_ENGINE_CAP_SOLID_FILL |
                   V9X_DD_ENGINE_CAP_SCREEN_COPY |
                   V9X_DD_ENGINE_CAP_FLIP |
                   V9X_DD_ENGINE_CAP_D3D;
}

/*
 * Not static: the per-object audit resolves this symbol by name out of the
 * link map to prove this chip's module is actually in the family image.
 */
const V9X_HW16_DEVICE v9x_rs690_device = {
    0x1002u, 0x791Fu,
    "ATI Radeon X1200 (RS690)",
    "1002", "791F",
    "ati-rs690-unavailable-v1",
    "vbe-lfb",
    "directdraw-fill-copy",
    "hardware-rs690-pio",
    0,
    v9x_rs690_fill_engine
};
