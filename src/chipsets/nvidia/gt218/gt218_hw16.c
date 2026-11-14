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
 * The VBE sets modes. The aperture mapped is BAR1, not the VBE's window:
 * with the BAR VM off, as the BIOS leaves it, BAR1 + 0 shows the same
 * desktop and BAR1 holds distinct writes at 16, 32, 64, 128 and 256 MiB -
 * 4 KiB with no alias (docs\decisions\2026-11-11-gt218-bar-windows.md), so
 * the DirectDraw heap grows past the VBE's 14 MiB through it.
 *
 * The engine hook has the mini-VDD map BAR0 and claims the 2D engine for
 * DirectDraw fill and copy as NV50; the HAL engine (engines\eng_nv50.c)
 * builds one PFIFO channel and brings PFIFO and PGRAPH up itself
 * (docs\plans\nvidia-gt218-nv50-engine.md). The channel's structures
 * live in the top megabyte of the mapping, which the reserve hook keeps
 * off the heap. No register is read or written from here. Nothing NV3
 * applies: this chip has no NV3 engine, RAMIN or PRAMDAC, and the
 * family's NV3 paths stay off. GDI fills and copies on the same channel
 * once the HAL has built it (gdi_accel.c, the NV50 arm), and on the CPU
 * before that.
 */
#include "velocity9x/hw16.h"
#include "velocity9x/engine_abi.h"
#include "velocity9x/gt218_reserve.h"

/* runtime.asm, nvidia family only. The BAR0 read is generic (a 32-bit
 * memory BAR, 16 MiB aligned); the mini-VDD's map accepts chipset A8h
 * beside NV3 since 2026-11-14 (V9XMAPI.INC). */
extern unsigned short __far __pascal V9xPciReadNv3MmioBar(
    unsigned long __far *base);
extern unsigned short __far __pascal V9xMiniNv3MmioMap(
    unsigned long bar0, unsigned long __far *linear);

/* All of BAR0, as the mini-VDD maps it. */
#define V9X_GT218_MMIO_BYTES 0x01000000ul

/* Where the reserve starts (gt218_reserve.c decides), once the heap has been cut for it; 0 before
 * that, or when there was no room, which leaves the HAL without an
 * engine. */
static unsigned long v9x_gt218_reserve_at;

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

/*
 * The heap ends where the engine's reserve starts. External for
 * nvidia_hw16.c's reserve_video_memory hook, which asks it only for this
 * chip; a family-internal symbol, as the measure hook above is. Without
 * room for the reserve above the visible screen the heap is left whole
 * and no engine is claimed.
 */
unsigned long v9x_gt218_reserve_video_memory(unsigned long usable_bytes,
                                             unsigned long visible_bytes)
{
    v9x_gt218_reserve_at = v9x_gt218_reserve_start(usable_bytes,
                                                   visible_bytes);
    if (v9x_gt218_reserve_at == 0ul) {
        return usable_bytes;
    }
    return v9x_gt218_reserve_at;
}

static unsigned long v9x_gt218_reserve_offset(void)
{
    return v9x_gt218_reserve_at;
}

/* BAR0 mapped by the mini-VDD, which hands the window over only if
 * PMC_BOOT_0 names chipset A8h. No register is touched from here. */
static void v9x_gt218_fill_engine(unsigned long framebuffer_linear_base,
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

    if (V9xPciReadNv3MmioBar(&bar0) == 0u) {
        return;
    }
    if (V9xMiniNv3MmioMap(bar0, &linear) == 0u || linear == 0ul) {
        return;
    }
    *control_linear_base = linear;
    *mapped_aperture_bytes = V9X_GT218_MMIO_BYTES;
    *engine_type = V9X_DD_ENGINE_TYPE_NV50;
    *engine_caps = V9X_DD_ENGINE_CAP_SOLID_FILL |
                   V9X_DD_ENGINE_CAP_SCREEN_COPY;
}

/* Not static: resolved by name in the link map by the per-object audit. */
const V9X_HW16_DEVICE v9x_gt218_device = {
    0x10deu, 0x0a65u,
    "NVIDIA GeForce 210",
    "10DE", "0A65",
    "nvidia-gt218-unavailable-v1",
    "vbe-lfb",
    "directdraw-fill-copy",
    "not-advertised",
    0,
    v9x_gt218_fill_engine,
    V9X_GT218_FRAMEBUFFER_BAR,
    0u,                     /* packed_pitch */
    V9X_GT218_MAP_BYTES,
    v9x_gt218_reserve_offset,
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
