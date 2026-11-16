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
 * flips apply. Its graphics CP is GFX6, not driven: the row never stamps
 * V9X_DD_ENGINE_CAP_D3D.
 *
 * The hook and the diagnostics are in oland_engine16.c, in LEAFCODE: the
 * family's _TEXT has no room for them. What stays here is the device table
 * and two thunks, because the table's hook and the publisher's write
 * function are near pointers that cannot be called across a segment.
 */
#include "velocity9x/hw16.h"
#include "velocity9x/leaf16.h"

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
    0ul,                    /* map_bytes: the family's 16 MiB */
    0,                      /* engine_reserve_offset */
    /* Write-combined as Mullins' and Carrizo's rows are: eng_vi.c drains
     * the write-combining buffers before every kick, and the 16 MiB
     * mapping lies inside BAR0 apart from the ring window at 240 MiB. Not
     * measured on this card. */
    V9X_HW16_WC_MAPPING,
    0,                      /* write_combine_fallback */
    /* GDI has no engine here, as on the APUs; whether a discrete card's
     * BAR reads are as slow as the R3E's is not measured, so the shadow
     * is kept (docs\decisions\2026-11-12-ati-screen-shadow.md). */
    V9X_HW16_SCREEN_SHADOW
};
