/*
 * ATI Radeon X1300, PCI 1002:7146 (RV515, R500 class).
 *
 * The physical target is the remote-help machine M-5CAPM3 since
 * 2026-11-21: an ATI-built board (subsystem 1002:0322), BAR0 (frame
 * buffer, 64-bit, 256 MiB) at D0000000 and BAR2 (registers, 64-bit,
 * 64 KiB) at FDEF0000. 256 MiB of VRAM on a 128-bit bus, at GPU address
 * D0000000 (docs\decisions\2026-11-21-rv515-register-probe.md).
 *
 * docs\plans\ati-rv515-x1300.md, phase 1: tier-0. The VBE sets modes and
 * reports the frame buffer, the CPU draws, and GDI draws into the screen
 * shadow, which is how the vbe package ran this card on M-5CAPM3 (V9XGDI,
 * V9XMSW and V9XDDP complete there). No hook: the engine waits for the
 * 2D write probe (phase 3).
 *
 * When a hook comes, BAR2 must be read by V9xPciReadAtiMmioBar, as the
 * RV280's is, not V9xPciReadEgBars: this card's BAR2 is 64 KiB aligned
 * and the Evergreen read demands 128 KiB.
 *
 * The card's second PCI function (1002:7166, the second head) is not
 * claimed; Windows leaves it disabled (problem 22), which is normal.
 */
#include "velocity9x/hw16.h"

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
    0,                      /* acceleration: none, as the VT2's row */
    0,                      /* direct3d: not advertised */
    0,                      /* enable_aperture */
    0,                      /* fill_engine_descriptor: no engine yet */
    0u,                     /* framebuffer_bar: BAR0 */
    0u,                     /* packed_pitch */
    0ul,                    /* map_bytes: the VBE's size */
    0,                      /* engine_reserve_offset */
    /* The vbe package wrote a write-combining MTRR over the 16 MiB
     * mapping on this machine (V9XBOOT.INI MtrrWrite=ok, boot 28). */
    V9X_HW16_WC_MAPPING,
    0,                      /* write_combine_fallback */
    /* GDI has no engine here; the vbe package ran it in the shadow. */
    V9X_HW16_SCREEN_SHADOW
};
