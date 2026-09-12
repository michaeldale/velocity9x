#include "velocity9x/hw16.h"

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
