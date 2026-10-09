/*
 * The Matrox Millennium generations: the MGA-2064W (102B:0519) and the
 * MGA-2164W (102B:051B).
 *
 * One module, the way the Trio64 aliases share theirs: both chips take the
 * same path and the same drawing engine, and differ in their strings and in
 * which BAR holds which aperture. The 2064W puts its 16 KiB control aperture
 * (MGABASE1) in BAR0 and its framebuffer (MGABASE2) in BAR1; the 2164W the
 * other way round (docs\decisions\2026-09-09-millennium-2064w-bar-ordering.md,
 * docs\specifications\matrox-millennium2-bringup.md). The BAR not holding the
 * framebuffer is the control aperture on both.
 *
 * The VBE sets modes and 4F01h reports the framebuffer. The engine hook has
 * the mini-VDD map the control aperture and claims the drawing engine for
 * DirectDraw fill and copy as MGA. No register is written here: the
 * engine's own state (MACCESS, the plane mask, the clip window) is written
 * by the HAL before its first operation in each mode
 * (src\display32\engines\eng_mga.c).
 *
 * Measured on the 2064W in A8U4I5 (2026-10-09): the BIOS's mode set turns
 * mgamode on (CRTCEXT3 81h) and the engine path passes on silicon. The
 * 2164W has not run on this path. Its one physical sample, in August, drew
 * corrupt surfaces with the Velocity9x mini-VDD of that time and clean ones
 * with Matrox's own MGAPDX64.VXD kept beside our driver
 * (docs\specifications\matrox-millennium2-bringup.md, candidates 7-8). This
 * family installs V9XMINI.VXD for both chips, so that is the first thing to
 * check when a 2164W boots here.
 */
#include "velocity9x/hw16.h"
#include "velocity9x/engine_abi.h"

/* runtime.asm, matrox family only. */
extern unsigned short __far __pascal V9xPciReadMgaMmioBar(
    unsigned long __far *base, unsigned short bar_index);
extern unsigned short __far __pascal V9xMiniMgaMmioMap(
    unsigned long control_bar, unsigned long __far *linear);

/* Which chip the PCI scan matched. Published by ddi.c. */
extern const V9X_HW16_DEVICE *v9x_hw16_active_device(void);

/* V9X_MGA_MMIO_BYTES in the mini-VDD. */
#define V9X_MGA_MMIO_BYTES 0x00004000ul

/*
 * The control aperture mapped by the mini-VDD, which withholds a window
 * whose STATUS reads all ones. A refusal anywhere leaves the chip on the CPU
 * path; it never fails Enable.
 */
static void v9x_mga_fill_engine(unsigned long framebuffer_linear_base,
                                unsigned long *control_linear_base,
                                unsigned long *mapped_aperture_bytes,
                                unsigned long *engine_type,
                                unsigned long *engine_caps,
                                unsigned long *gtt_linear_base,
                                unsigned long *ring_linear_base,
                                unsigned long *ring_bytes)
{
    const V9X_HW16_DEVICE *device = v9x_hw16_active_device();
    unsigned long control_bar = 0ul;
    unsigned long linear = 0ul;
    unsigned short control_index;

    (void)framebuffer_linear_base;
    *control_linear_base = 0ul;
    *mapped_aperture_bytes = 0ul;
    *engine_type = V9X_DD_ENGINE_TYPE_NONE;
    *engine_caps = 0ul;
    *gtt_linear_base = 0ul;
    *ring_linear_base = 0ul;
    *ring_bytes = 0ul;

    if (device == 0) {
        return;
    }
    control_index = device->framebuffer_bar == 0u ? 1u : 0u;
    if (V9xPciReadMgaMmioBar(&control_bar, control_index) == 0u) {
        return;
    }
    if (V9xMiniMgaMmioMap(control_bar, &linear) == 0u || linear == 0ul) {
        return;
    }
    *control_linear_base = linear;
    *mapped_aperture_bytes = V9X_MGA_MMIO_BYTES;
    *engine_type = V9X_DD_ENGINE_TYPE_MGA;
    *engine_caps = V9X_DD_ENGINE_CAP_SOLID_FILL |
                   V9X_DD_ENGINE_CAP_SCREEN_COPY;
}

/* Not static: resolved by name in the link map by the per-object audit.
 * framebuffer_bar is stated for the engine hook, which maps the other one;
 * no hook in this family reads the framebuffer BAR, the BIOS reports it. */
const V9X_HW16_DEVICE v9x_mga2064w_device = {
    0x102bu, 0x0519u,
    "Matrox Millennium MGA-2064W",
    "102B", "0519",
    "matrox-mga2064w-unavailable-v1",
    "vbe-lfb",
    "directdraw-fill-copy",
    0,
    0,
    v9x_mga_fill_engine,
    1u
};

const V9X_HW16_DEVICE v9x_mga2164w_device = {
    0x102bu, 0x051bu,
    "Matrox Millennium II MGA-2164W",
    "102B", "051B",
    "matrox-mga2164w-unavailable-v1",
    "vbe-lfb",
    "directdraw-fill-copy",
    0,
    0,
    v9x_mga_fill_engine,
    0u
};
