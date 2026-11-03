/*
 * NVIDIA RIVA 128 and RIVA 128ZX, PCI 12D2:0018.
 *
 * Measured as an STB Velocity 128 ZXV on M-7V636C on 2026-11-02
 * (docs\decisions\2026-11-02-m-7v636c-riva128-first-look.md): subsystem
 * 10B4:222A, revision 22 (a 128ZX), 8 MiB, AGP, BAR0 16 MiB of registers
 * and BAR1 the prefetchable framebuffer, BIOS "STB Velocity 128 (RIVA ZXV)".
 *
 * The VBE sets modes and reports the framebuffer. The engine hook has the
 * mini-VDD map BAR0 and claims the 2D engine for DirectDraw fill and copy
 * as NV3; the HAL engine (engines\eng_nv3.c) brings PFIFO, PGRAPH and RAMIN
 * up itself, because every VBE mode set wipes RAMIN and leaves PFB stale
 * (docs\decisions\2026-11-02-riva128-2d-engine-writes.md). No register is
 * written from here.
 *
 * RAMIN, the engine's object tables, is the top 32 KiB of video memory
 * (docs\decisions\2026-11-02-riva128-register-probe.md). The engine is
 * claimed only on a card whose VBE reports 8 MiB, the one size where that
 * was measured; the reserve hook names where RAMIN starts.
 */
#include "velocity9x/hw16.h"
#include "velocity9x/engine_abi.h"

/* runtime.asm, nvidia family only. */
extern unsigned short __far __pascal V9xPciReadNv3MmioBar(
    unsigned long __far *base);
extern unsigned short __far __pascal V9xMiniNv3MmioMap(
    unsigned long bar0, unsigned long __far *linear);

/* enable16.c, filled in by the tier-0 stage-3 default from VBE 4F00h. */
extern unsigned long v9x_vbe_vram_reported;

/* V9X_NV3_MMIO_BYTES in the mini-VDD: all of BAR0. */
#define V9X_RIVA128_MMIO_BYTES  0x01000000ul
/* The one memory size RAMIN's place was measured on, and RAMIN's size. */
#define V9X_RIVA128_VRAM_BYTES  0x00800000ul
#define V9X_RIVA128_RAMIN_BYTES 0x00008000ul

static void v9x_riva128_fill_engine(unsigned long framebuffer_linear_base,
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

    if (v9x_vbe_vram_reported != V9X_RIVA128_VRAM_BYTES) {
        return;
    }
    if (V9xPciReadNv3MmioBar(&bar0) == 0u) {
        return;
    }
    if (V9xMiniNv3MmioMap(bar0, &linear) == 0u || linear == 0ul) {
        return;
    }
    *control_linear_base = linear;
    *mapped_aperture_bytes = V9X_RIVA128_MMIO_BYTES;
    *engine_type = V9X_DD_ENGINE_TYPE_NV3;
    /* FLIP: the HAL writes the CRTC start (eng_nv3.c); no BAR0 register is
     * involved, but it rides with the engine's claim on the measured card. */
    *engine_caps = V9X_DD_ENGINE_CAP_SOLID_FILL |
                   V9X_DD_ENGINE_CAP_SCREEN_COPY |
                   V9X_DD_ENGINE_CAP_FLIP;
}

/* Where RAMIN starts in video memory, for the HAL; 0 off the measured
 * size, which leaves the HAL without an engine. */
static unsigned long v9x_riva128_ramin_offset(void)
{
    if (v9x_vbe_vram_reported != V9X_RIVA128_VRAM_BYTES) {
        return 0ul;
    }
    return V9X_RIVA128_VRAM_BYTES - V9X_RIVA128_RAMIN_BYTES;
}

/* Not static: resolved by name in the link map by the per-object audit. */
const V9X_HW16_DEVICE v9x_riva128_device = {
    0x12d2u, 0x0018u,
    "NVIDIA RIVA 128",
    "12D2", "0018",
    "nvidia-riva128-unavailable-v1",
    "vbe-lfb",
    "directdraw-fill-copy",
    0,
    0,
    v9x_riva128_fill_engine,
    /* framebuffer_bar: BAR1. A record only: with no read_aperture hook the
     * aperture comes from VBE 4F01h. */
    1u,
    0u,                     /* packed_pitch */
    0ul,                    /* map_bytes: the family's */
    v9x_riva128_ramin_offset
};
