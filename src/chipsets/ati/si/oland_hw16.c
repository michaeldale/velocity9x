/*
 * AMD Oland XT, PCI 1002:6610 (GCN 1), which Linux binds as CHIP_OLAND, a
 * Southern Islands part: GFX6, the SI async DMA engine, DCE 6.4. A
 * discrete PCIe card with its own VRAM.
 *
 * The id, the BARs and the VRAM placement were read from W9QI0DAD's card
 * (Dell subsystem 1028:2120, 2 GiB) by V9XSIMM on 2026-11-16: BAR0 is a
 * 64-bit prefetchable 256 MiB window below 4 GiB, BAR2 the 256 KiB
 * register window, HDP_NONSURFACE_BASE equals MC_VM_FB_LOCATION's start,
 * and VM context 0 is off (docs\decisions\2026-11-16-oland-first-survey.md).
 * The engine path below is still unrun (docs\plans\ati-oland-gfx6.md).
 *
 * DirectDraw fill, copy and flips go to Carrizo's engine type
 * (engines\eng_vi.c, V9X_DD_ENGINE_TYPE_ATI_VI) with the Oland identity
 * bit, which makes it start SI's DMA ring and write si_dma.c's packets in
 * place of SDMA's. The register window is BAR2 and goes to the mini-VDD
 * map in the slot Carrizo's BAR5 takes; the map checks only its size and
 * alignment. The ring window is BAR0 + 240 MiB, as on Carrizo, which
 * assumes BAR0's first byte is VRAM's first (HDP_NONSURFACE_BASE at the
 * start of VRAM, as Linux sets it); the register probe is to confirm it.
 * Its display is DCE 6, whose first controller is at Evergreen's offsets
 * (GRPH_PRIMARY_SURFACE_ADDRESS 6810h in dce_6_0_d.h), so eg_scanout.c's
 * flips apply. Its graphics CP is GFX6, driven by vi_cp.c's Oland mode:
 * the hook stamps V9X_DD_ENGINE_CAP_D3D when the whole window is mapped.
 *
 * The hook and the diagnostics are in oland_engine16.c, in LEAFCODE: the
 * family's _TEXT has no room for them. What stays here is the device table
 * and two thunks, because the table's hook and the publisher's write
 * function are near pointers that cannot be called across a segment.
 */
#include "velocity9x/hw16.h"
#include "velocity9x/leaf16.h"

/*
 * The DirectDraw heap's mapping: 128 MiB of BAR0, as the Evergreen rows'
 * (cedar_hw16.c), not the family's 16 MiB. The VBE reports 16 MiB on this
 * 2 GiB card (its field's cap), and with DXT published 3DMark 2001 SE's
 * Dragothic High and Nature textures thrashed the 10 MB that left after a
 * 1024x768x32 flip chain: 130,775 texture locks and 23,760 draws refused
 * in one run (docs\decisions\2026-11-17-oland-bc-1d-tiled.md, boot 63).
 * 128 MiB lies inside the 256 MiB BAR0 below the ring window at BAR0 +
 * 240 MiB, and the card's 2 GiB is more than the mini-VDD's map requires.
 * The family's measure_video_memory hook takes the whole mapping as the
 * heap for this chip (ati_hw16.c).
 */
#define V9X_OLAND_MAP_BYTES 0x08000000ul

/* oland_engine16.c, in LEAFCODE. */
extern void V9X_LEAF_FAR v9x_oland_fill_engine_leaf(
    unsigned long *control_linear_base,
    unsigned long *mapped_aperture_bytes,
    unsigned long *engine_type,
    unsigned long *engine_caps,
    unsigned long *gtt_linear_base,
    unsigned long *ring_linear_base,
    unsigned long *ring_bytes);
extern const char * V9X_LEAF_FAR v9x_oland_diagnostic(unsigned short index,
                                                      const char **key);

static void v9x_oland_fill_engine(unsigned long framebuffer_linear_base,
                                  unsigned long *control_linear_base,
                                  unsigned long *mapped_aperture_bytes,
                                  unsigned long *engine_type,
                                  unsigned long *engine_caps,
                                  unsigned long *gtt_linear_base,
                                  unsigned long *ring_linear_base,
                                  unsigned long *ring_bytes)
{
    (void)framebuffer_linear_base;
    v9x_oland_fill_engine_leaf(control_linear_base, mapped_aperture_bytes,
                               engine_type, engine_caps, gtt_linear_base,
                               ring_linear_base, ring_bytes);
}

/*
 * The engine hook's outcome, for the family's V9XHW.INI publisher, which
 * runs after the hook. External because ati_hw16.c calls it for this chip;
 * a family-internal symbol, not an export, as Mullins' is.
 */
void v9x_oland_publish_diagnostics(v9x_hw16_write_fn write)
{
    const char *key;
    const char *value;
    unsigned short index;

    for (index = 0u; (value = v9x_oland_diagnostic(index, &key)) != 0;
         ++index) {
        write(key, value);
    }
}

/*
 * Not static: the per-object audit resolves this symbol by name out of the
 * link map to prove this chip's module is actually in the family image.
 */
const V9X_HW16_DEVICE v9x_si_oland_device = {
    0x1002u, 0x6610u,
    "AMD Radeon Oland XT",
    "1002", "6610",
    "ati-si-unavailable-v1",
    "vbe-lfb",
    "directdraw-fill-copy",
    "not-advertised",
    0,
    v9x_oland_fill_engine,
    0u,                     /* framebuffer_bar: BAR0 */
    0u,                     /* packed_pitch */
    V9X_OLAND_MAP_BYTES,
    0,                      /* engine_reserve_offset */
    /* Write-combined over the whole 256 MiB BAR0, the ring window at 240
     * MiB included: eng_vi.c and vi_cp.c drain the write-combining buffers
     * before every kick. Over the 128 MiB mapping alone the window stayed
     * uncached (boot 101, MtrrWrite b=d0000001 m=f8000800), and the CPU
     * copied vertices into it at about 400 MB/s (docs\decisions\
     * 2026-11-18-gcn-batch-cost.md). */
    V9X_HW16_WC_BAR256,
    0,                      /* write_combine_fallback */
    /* GDI has no engine here, as on the APUs; whether a discrete card's
     * BAR reads are as slow as the R3E's is not measured, so the shadow
     * is kept (docs\decisions\2026-11-12-ati-screen-shadow.md). */
    V9X_HW16_SCREEN_SHADOW
};
