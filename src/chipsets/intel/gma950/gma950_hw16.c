#include "velocity9x/hw16.h"
#include "velocity9x/intel_gma.h"

unsigned long v9x_gma950_reserve_video_memory(
    unsigned long usable_bytes, unsigned long visible_bytes)
{
    struct v9x_i9xx_sandbox_layout layout;

    /* Physical BSM is irrelevant to the heap boundary, so zero is the honest
     * base for this calculation.  The first-write path independently checks
     * the measured BSM before it may use the physical fields. */
    if (v9x_i9xx_sandbox_calculate(usable_bytes, 0ul, &layout) !=
            V9X_STATUS_OK ||
        layout.heap_bytes < visible_bytes) {
        return usable_bytes;
    }
    return layout.heap_bytes;
}

/* Exact physical target. Engine and aperture hooks remain absent. */
const V9X_HW16_DEVICE v9x_gma950_device = {
    0x8086u, 0x27aeu,
    "Intel GMA 950 (945GSE)",
    "8086", "27AE",
    "intel-gen3-mmio-fingerprint-v1",
    "vbe-lfb",
    0,
    0,
    0,
    0,
    /* VBE reports the framebuffer in GMADR BAR2. */
    2u
};
