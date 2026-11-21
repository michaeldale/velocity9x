/*
 * ATI Radeon X1300, PCI 1002:7146 (RV515, R500 class).
 *
 * The physical target is the remote-help machine M-5CAPM3 since
 * 2026-11-21: an ATI-built board (subsystem 1002:0322), BAR0 (frame
 * buffer, 64-bit, 256 MiB) at D0000000 and BAR2 (registers, 64-bit,
 * 64 KiB) at FDEF0000. 256 MiB of VRAM on a 128-bit bus, at GPU address
 * D0000000 (docs\decisions\2026-11-21-rv515-register-probe.md).
 *
 * docs\plans\ati-rv515-x1300.md. The VBE sets modes and reports the frame
 * buffer, and GDI draws into the screen shadow, which is how the vbe
 * package ran this card on M-5CAPM3. Phase 3: the engine hook claims the
 * RS690's engine (eng_rs690.c) for DirectDraw fill and copy. The RS690
 * builder's init list, fills and copies in every overlap direction were
 * measured byte-exact on this card at 8, 16 and 32 bpp with the CP's
 * queues off, as VBE leaves them
 * (docs\decisions\2026-11-21-rv515-2d-write-probe.md). Phase 4 adds FLIP
 * through D1GRPH (rs690_scanout.c). No D3D in the mask, so eng_rs690.c
 * leaves the 3D core alone.
 *
 * BAR2 is read by V9xPciReadAtiMmioBar, as the RV280's is, not
 * V9xPciReadEgBars: this card's BAR2 is 64 KiB aligned and the Evergreen
 * read demands 128 KiB. It is a 64-bit BAR whose upper dword read 0
 * (docs\decisions\2026-11-21-rv515-register-probe.md); the read takes the
 * low dword. The RS690's map in the mini-VDD then requires 64 KiB
 * alignment and a CONFIG_MEMSIZE of at most 512 MiB (256 MiB here).
 *
 * The card's second PCI function (1002:7166, the second head) is not
 * claimed; Windows leaves it disabled (problem 22), which is normal.
 */
#include "velocity9x/hw16.h"
#include "velocity9x/leaf16.h"

/* rv515_engine16.c, in LEAFCODE: the hook's body (that file says why). */
void V9X_LEAF_FAR v9x_rv515_fill_engine_leaf(
    unsigned long *control_linear_base,
    unsigned long *mapped_aperture_bytes,
    unsigned long *engine_type,
    unsigned long *engine_caps,
    unsigned long *gtt_linear_base,
    unsigned long *ring_linear_base,
    unsigned long *ring_bytes);

static void v9x_rv515_fill_engine(unsigned long framebuffer_linear_base,
                                  unsigned long *control_linear_base,
                                  unsigned long *mapped_aperture_bytes,
                                  unsigned long *engine_type,
                                  unsigned long *engine_caps,
                                  unsigned long *gtt_linear_base,
                                  unsigned long *ring_linear_base,
                                  unsigned long *ring_bytes)
{
    (void)framebuffer_linear_base;
    v9x_rv515_fill_engine_leaf(control_linear_base, mapped_aperture_bytes,
                               engine_type, engine_caps, gtt_linear_base,
                               ring_linear_base, ring_bytes);
}

/*
 * Not static: the per-object audit resolves this symbol by name out of the
 * link map to prove this chip's module is actually in the family image.
 */
const V9X_HW16_DEVICE v9x_rv515_x1300_device = {
    0x1002u, 0x7146u,
    "ATI Radeon X1300 (RV515)",
    "1002", "7146",
    "ati-rv515-unavailable-v1",
    "vbe-lfb",
    "directdraw-fill-copy",
    "hardware-rv515-pio",
    0,                      /* enable_aperture */
    v9x_rv515_fill_engine,
    0u,                     /* framebuffer_bar: BAR0 */
    0u,                     /* packed_pitch */
    0ul,                    /* map_bytes: the VBE's size */
    0,                      /* engine_reserve_offset */
    /* The vbe package wrote a write-combining MTRR over the 16 MiB
     * mapping on this machine (V9XBOOT.INI MtrrWrite=ok, boot 28). */
    V9X_HW16_WC_MAPPING,
    0,                      /* write_combine_fallback */
    /* GDI stays on the CPU, in the shadow, as the vbe package ran it. */
    V9X_HW16_SCREEN_SHADOW
};
