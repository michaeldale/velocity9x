/*
 * NVIDIA GeForce 210, PCI 10DE:0A65: a GT218 (nouveau's NVA8, Tesla).
 *
 * Seen under the vbe package on M-BXMFA8 on 2026-11-11
 * (docs\decisions\2026-11-11-m-bxmfa8-gt218-first-look.md): a Gainward
 * board, subsystem 10B0:1401, revision A2. VBE 3.0 reports 14 MiB and puts
 * every mode's linear framebuffer at CF000000h, which configuration space
 * places 16 MiB into BAR3 (docs\decisions\2026-11-11-gt218-register-
 * probe.md). The card has 512 MiB.
 *
 * The VBE sets modes and the CPU draws (docs\plans\nvidia-gt218.md,
 * Phase 1). The aperture mapped is BAR1, not the VBE's window: with the
 * BAR VM off, as the BIOS leaves it, BAR1 + 0 shows the same desktop and
 * BAR1 holds distinct writes at 16, 32, 64, 128 and 256 MiB - 4 KiB with
 * no alias (docs\decisions\2026-11-11-gt218-bar-windows.md), so the
 * DirectDraw heap grows past the VBE's 14 MiB through it. No register is
 * read or written. Nothing NV3 applies: this chip has no NV3 engine, RAMIN
 * or PRAMDAC, so it supplies no engine descriptor and no reserve hook, and
 * the family's NV3 paths stay off.
 */
#include "velocity9x/hw16.h"

/*
 * 128 MiB of BAR1's 256 MiB: the size the Evergreen rows map on the same
 * host (cedar_hw16.c), which Windows 98 granted there, and well inside the
 * span the write test covered. A host that refuses the mapping is asked
 * again for less (enable16.c, v9x_vbe_map_smaller).
 */
#define V9X_GT218_MAP_BYTES 0x08000000ul

/* BAR1, configuration offset 14h: the VRAM aperture (nouveau), 256 MiB on
 * M-BXMFA8 at D0000000h. */
#define V9X_GT218_FRAMEBUFFER_BAR 1u

/*
 * The heap is the whole mapping: the card holds 512 MiB (PFB 10020Ch on
 * boot 82), so every byte of the 128 MiB mapped is memory, whatever 4F00h
 * said. External for nvidia_hw16.c's measure_video_memory hook, which asks
 * it only for this chip; a family-internal symbol, as the Cedar's is.
 */
unsigned long v9x_gt218_measure_video_memory(unsigned long mapped_bytes,
                                             unsigned long reported_bytes)
{
    (void)reported_bytes;
    return mapped_bytes;
}

/* Not static: resolved by name in the link map by the per-object audit. */
const V9X_HW16_DEVICE v9x_gt218_device = {
    0x10deu, 0x0a65u,
    "NVIDIA GeForce 210",
    "10DE", "0A65",
    "nvidia-gt218-unavailable-v1",
    "vbe-lfb",
    "none",
    "not-advertised",
    0,
    /* NULL: no engine yet. */
    0,
    V9X_GT218_FRAMEBUFFER_BAR,
    0u,                     /* packed_pitch */
    V9X_GT218_MAP_BYTES,
    /* NULL: no engine reserve. */
    0,
    /*
     * Write-combine over the memory reported, clamped to the mapping, as
     * the vbe package did on this card (boot 81: MTRR 0, 8 MiB at
     * CF000000h, MtrrWrite=ok). Over BAR1 it is unmeasured.
     */
    V9X_HW16_WC_VRAM,
    0,
    /* aperture_from_bar: map BAR1, not the VBE's BAR3 window. */
    1u
};
