/*
 * NVIDIA RIVA 128 and RIVA 128ZX, PCI 12D2:0018.
 *
 * Measured as an STB Velocity 128 ZXV on M-7V636C on 2026-11-02
 * (docs\decisions\2026-11-02-m-7v636c-riva128-first-look.md): subsystem
 * 10B4:222A, revision 22 (a 128ZX), 8 MiB, AGP, BAR0 16 MiB of registers
 * and BAR1 the prefetchable framebuffer, BIOS "STB Velocity 128 (RIVA ZXV)".
 *
 * Tier-0, both hooks NULL: the VBE sets modes and reports the framebuffer,
 * and the CPU draws. No NVIDIA register is written, so PFIFO, PGRAPH and
 * RAMIN are left however the BIOS left them.
 */
#include "velocity9x/hw16.h"

/* Not static: resolved by name in the link map by the per-object audit. */
const V9X_HW16_DEVICE v9x_riva128_device = {
    0x12d2u, 0x0018u,
    "NVIDIA RIVA 128",
    "12D2", "0018",
    "nvidia-riva128-unavailable-v1",
    "vbe-lfb",
    0,
    0,
    0,
    0
};
