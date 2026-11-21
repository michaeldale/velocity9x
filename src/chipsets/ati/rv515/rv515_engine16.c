/*
 * The RV515 row's engine hook, in LEAFCODE (leaf16.h).
 *
 * The ati family's _TEXT went 155 bytes past its 57,344-byte budget with
 * this hook in rv515_hw16.c (2026-11-21), so the body lives here and
 * rv515_hw16.c keeps the device table and a thunk, as oland_engine16.c
 * does for the Oland row. The device table's hook is a near pointer and
 * cannot point across a segment; this module calls only runtime.asm's FAR
 * entries. See rv515_hw16.c for what the hook claims and why.
 */
#include "velocity9x/hw16.h"
#include "velocity9x/engine_abi.h"
#include "velocity9x/leaf16.h"

/* runtime.asm, ati family only. */
extern unsigned short __far __pascal V9xPciReadAtiMmioBar(
    unsigned long __far *base);
extern unsigned short __far __pascal V9xMiniRs690MmioMap(
    unsigned long bar2, unsigned long __far *linear);

/* V9X_RS690_MMIO_BYTES in the mini-VDD: the register BAR. */
#define V9X_RV515_MMIO_BYTES 0x00010000ul

void V9X_LEAF_FAR v9x_rv515_fill_engine_leaf(
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
    if (V9xMiniRs690MmioMap(bar2, &linear) == 0u || linear == 0ul) {
        return;
    }
    *control_linear_base = linear;
    *mapped_aperture_bytes = V9X_RV515_MMIO_BYTES;
    *engine_type = V9X_DD_ENGINE_TYPE_ATI_RS690;
    /* FLIP: D1GRPH, through rs690_scanout.c, which is Linux's
     * rs600_page_flip; radeon_asic.c gives the RV515 the same routine
     * (rv515_asic). The register probe read the VBE desktop on D1GRPH at
     * the frame buffer's GPU base, D0000000h
     * (docs\decisions\2026-11-21-rv515-register-probe.md). */
    *engine_caps = V9X_DD_ENGINE_CAP_SOLID_FILL |
                   V9X_DD_ENGINE_CAP_SCREEN_COPY |
                   V9X_DD_ENGINE_CAP_FLIP;
}
