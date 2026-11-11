/*
 * NVIDIA GeForce 210, PCI 10DE:0A65: a GT218 (nouveau's NVA8, Tesla).
 *
 * Seen under the vbe package on M-BXMFA8 on 2026-11-11
 * (docs\decisions\2026-11-11-m-bxmfa8-gt218-first-look.md): a Gainward
 * board, subsystem 10B0:1401, revision A2. VBE 3.0 reports 14 MiB and puts
 * every mode's linear framebuffer at CF000000h, 16 MiB into a 32 MiB
 * prefetchable range Windows allocated at CE000000h. Which BAR that range
 * is has not been read from config space, so no BAR index is relied on:
 * the aperture comes from VBE 4F01h.
 *
 * Tier-0 (docs\plans\nvidia-gt218.md, Phase 1): the VBE sets modes, the
 * CPU draws, no register is read or written. Nothing NV3 applies: this
 * chip has no NV3 engine, RAMIN or PRAMDAC, so it supplies no engine
 * descriptor and no reserve hook, and the family's NV3 paths stay off.
 */
#include "velocity9x/hw16.h"

/*
 * The VBE's 16 MiB framebuffer window, CF000000h-CFFFFFFFh, which lies
 * inside the range Windows allocated (CE000000h-CFFFFFFFh). The vbe package
 * mapped exactly this on the card (V9XBOOT.INI DpmiMap=01000000:ok, boot
 * 81). The family's 8 MiB, sized for the RIVA 128, would cut the 14 MiB the
 * BIOS hands out.
 */
#define V9X_GT218_MAP_BYTES 0x01000000ul

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
    /* NULL: no engine at tier-0. */
    0,
    /* framebuffer_bar: a record only, with no read_aperture hook; see the
     * header comment for why no index is claimed. */
    0u,
    0u,                     /* packed_pitch */
    V9X_GT218_MAP_BYTES,
    /* NULL: no engine reserve. */
    0,
    /*
     * Write-combine over the memory the BIOS reported, clamped to the
     * mapping: the plan the vbe package ran on this card, which installed
     * MTRR 0 over 8 MiB at CF000000h (boot 81, MtrrWrite=ok).
     */
    V9X_HW16_WC_VRAM,
    0
};
