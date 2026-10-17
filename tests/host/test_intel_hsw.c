#include <stdio.h>

#include "velocity9x/intel_hsw.h"

static unsigned int failures = 0u;

#define CHECK(expression) do { \
    if (!(expression)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #expression); \
        ++failures; \
    } \
} while (0)

/*
 * The i5-4570 (Dell, IGD 8086:0412 rev 06, SUBSYS 05A41028), as V9XHSW
 * read it under Windows 98 on 2026-10-17: GGC 0209h (32 MB data-stolen,
 * 2 MB GTT-stolen, locked), BDSM DD200001, BGSM DD000001, MMIO F7800000,
 * a 256 MB aperture at E0000000. docs\probe\hsw-4570-config-2026-10-17.
 */
static void v9x_hsw_desktop(struct v9x_intel_hsw_config *config)
{
    config->ggc = (v9x_u16)0x0209u;
    config->bdsm = 0xdd200001ul;
    config->host_bgsm = 0xdd000001ul;
    config->bar0_low = 0xf7800004ul;
    config->bar0_high = 0ul;
    config->bar2_low = 0xe000000cul;
    config->bar2_high = 0ul;
    config->msac = (v9x_u8)0x02u; /* LHSASL: 256 MB */
}

static void test_ggc(void)
{
    struct v9x_intel_hsw_ggc ggc;

    /* The datasheet's reset value, 0028h: GMS 05h = 160 MB, GGMS 0. */
    CHECK(v9x_intel_hsw_decode_ggc(0x0028u, &ggc) == V9X_STATUS_OK);
    CHECK(ggc.gms == 5u && ggc.stolen_bytes == 160ul * 0x100000ul);
    CHECK(ggc.ggms == 0u && ggc.gtt_stolen_bytes == 0ul);
    CHECK(ggc.vga_disabled == 0u && ggc.locked == 0u);

    /* 10h is the last listed value: 512 MB. */
    CHECK(v9x_intel_hsw_decode_ggc((v9x_u16)((2u << 8) | (0x10u << 3)),
                                   &ggc) == V9X_STATUS_OK);
    CHECK(ggc.stolen_bytes == 0x20000000ul);
    CHECK(ggc.gtt_stolen_bytes == 0x00200000ul);

    /* 11h is not in the table, GGMS 3 is reserved. */
    CHECK(v9x_intel_hsw_decode_ggc((v9x_u16)(0x11u << 3), &ggc) ==
          V9X_STATUS_UNSUPPORTED);
    CHECK(ggc.stolen_bytes == 0ul);
    CHECK(v9x_intel_hsw_decode_ggc((v9x_u16)((3u << 8) | (1u << 3)),
                                   &ggc) == V9X_STATUS_UNSUPPORTED);

    CHECK(v9x_intel_hsw_decode_ggc(0x4003u, &ggc) == V9X_STATUS_OK);
    CHECK(ggc.versatile_mode != 0u && ggc.vga_disabled != 0u &&
          ggc.locked != 0u);

    CHECK(v9x_intel_hsw_decode_ggc(0u, 0) == V9X_STATUS_INVALID_ARGUMENT);
}

static void test_aperture(void)
{
    v9x_u32 bytes;

    CHECK(v9x_intel_hsw_aperture_bytes(0x00u, &bytes) == V9X_STATUS_OK &&
          bytes == 0x08000000ul);
    CHECK(v9x_intel_hsw_aperture_bytes(0x02u, &bytes) == V9X_STATUS_OK &&
          bytes == 0x10000000ul);
    CHECK(v9x_intel_hsw_aperture_bytes(0x06u, &bytes) == V9X_STATUS_OK &&
          bytes == 0x20000000ul);
    /* Scratch bits 7:4 do not change the size. */
    CHECK(v9x_intel_hsw_aperture_bytes(0xf2u, &bytes) == V9X_STATUS_OK &&
          bytes == 0x10000000ul);
    CHECK(v9x_intel_hsw_aperture_bytes(0x04u, &bytes) ==
          V9X_STATUS_UNSUPPORTED);
}

static void test_layout(void)
{
    struct v9x_intel_hsw_config config;
    struct v9x_intel_hsw_layout layout;

    v9x_hsw_desktop(&config);
    CHECK(v9x_intel_hsw_decode_layout(&config, &layout) == V9X_STATUS_OK);
    CHECK(layout.refused == 0u);
    CHECK(layout.mmio_base == 0xf7800000ul);
    CHECK(layout.gtt_base == 0xf7a00000ul);
    CHECK(layout.gtt_entries == 0x80000ul);
    CHECK(layout.aperture_base == 0xe0000000ul);
    CHECK(layout.aperture_bytes == 0x10000000ul);
    CHECK(layout.stolen_base == 0xdd200000ul);
    CHECK(layout.gtt_stolen_base == 0xdd000000ul);
    CHECK(layout.ggc.stolen_bytes == 0x02000000ul && layout.ggc.locked != 0u);

    /* Not reading BGSM is allowed; disagreeing with it is not. */
    config.host_bgsm = 0ul;
    CHECK(v9x_intel_hsw_decode_layout(&config, &layout) == V9X_STATUS_OK);
    config.host_bgsm = 0xdd100001ul;
    CHECK(v9x_intel_hsw_decode_layout(&config, &layout) ==
          V9X_STATUS_UNSUPPORTED);
    CHECK(layout.refused == V9X_INTEL_HSW_LAYOUT_BGSM_MISMATCH);

    /* 1 MB of GTT reaches 1 GB: enough for 512 MB, so this passes... */
    v9x_hsw_desktop(&config);
    config.ggc = (v9x_u16)((1u << 8) | (1u << 3));
    config.host_bgsm = 0xdd100001ul;
    config.msac = 0x06u;
    config.bar2_low = 0xc000000cul;
    CHECK(v9x_intel_hsw_decode_layout(&config, &layout) == V9X_STATUS_OK);
    CHECK(layout.gtt_entries == 0x40000ul);

    /* ...and none reaches nothing. */
    config.ggc = (v9x_u16)(1u << 3);
    config.host_bgsm = 0ul;
    CHECK(v9x_intel_hsw_decode_layout(&config, &layout) ==
          V9X_STATUS_UNSUPPORTED);
    CHECK(layout.refused == V9X_INTEL_HSW_LAYOUT_GTT_TOO_SMALL);

    /* A BAR above 4 GB, an unassigned BAR0, an illegal MSAC. */
    v9x_hsw_desktop(&config);
    config.bar2_high = 0x00000004ul;
    CHECK(v9x_intel_hsw_decode_layout(&config, &layout) ==
          V9X_STATUS_UNSUPPORTED);
    CHECK(layout.refused == V9X_INTEL_HSW_LAYOUT_ABOVE_4G);

    v9x_hsw_desktop(&config);
    config.bar0_low = 0x00000004ul;
    CHECK(v9x_intel_hsw_decode_layout(&config, &layout) ==
          V9X_STATUS_UNSUPPORTED);
    CHECK(layout.refused == V9X_INTEL_HSW_LAYOUT_UNASSIGNED);
    CHECK(layout.gtt_base == 0ul);

    v9x_hsw_desktop(&config);
    config.msac = 0x04u;
    CHECK(v9x_intel_hsw_decode_layout(&config, &layout) ==
          V9X_STATUS_UNSUPPORTED);
    CHECK((layout.refused & V9X_INTEL_HSW_LAYOUT_MSAC_ILLEGAL) != 0u);

    /* A 32-bit BAR0 type is not the device the datasheet describes. */
    v9x_hsw_desktop(&config);
    config.bar0_low = 0xf7800000ul;
    CHECK(v9x_intel_hsw_decode_layout(&config, &layout) ==
          V9X_STATUS_UNSUPPORTED);
    CHECK(layout.refused == V9X_INTEL_HSW_LAYOUT_BAR0_TYPE);

    /* GMS 0 with a GTT: nothing stolen to scan out of. */
    v9x_hsw_desktop(&config);
    config.ggc = (v9x_u16)(2u << 8);
    config.host_bgsm = 0ul;
    CHECK(v9x_intel_hsw_decode_layout(&config, &layout) ==
          V9X_STATUS_UNSUPPORTED);
    CHECK(layout.refused == V9X_INTEL_HSW_LAYOUT_NO_STOLEN);

    v9x_hsw_desktop(&config);
    config.ggc = (v9x_u16)(0x12u << 3);
    CHECK(v9x_intel_hsw_decode_layout(&config, &layout) ==
          V9X_STATUS_UNSUPPORTED);
    CHECK((layout.refused & V9X_INTEL_HSW_LAYOUT_GMS_RESERVED) != 0u);

    CHECK(v9x_intel_hsw_decode_layout(0, &layout) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

static void test_pte(void)
{
    struct v9x_intel_hsw_pte pte;
    v9x_u32 raw;

    CHECK(v9x_intel_hsw_encode_pte(0x12345000ul, V9X_INTEL_HSW_CACHE_UC,
                                   &raw) == V9X_STATUS_OK);
    CHECK(raw == 0x12345001ul);
    CHECK(v9x_intel_hsw_encode_pte(0x12345000ul, V9X_INTEL_HSW_CACHE_WB_A3,
                                   &raw) == V9X_STATUS_OK);
    CHECK(raw == 0x12345801ul);
    /* 1011b, WB aged 0: bit 3 to PTE bit 11, bits 2:0 to PTE bits 3:1. */
    CHECK(v9x_intel_hsw_encode_pte(0x00001000ul, 0x0bu, &raw) ==
          V9X_STATUS_OK);
    CHECK(raw == 0x00001807ul);
    CHECK(v9x_intel_hsw_decode_pte(raw, &pte) == V9X_STATUS_OK);
    CHECK(pte.valid != 0u && pte.cache == 0x0bu);
    CHECK(pte.physical_low == 0x00001000ul && pte.physical_high == 0u);

    /* Address 38:32 in bits 10:4. */
    CHECK(v9x_intel_hsw_decode_pte(0xabcde070ul, &pte) == V9X_STATUS_OK);
    CHECK(pte.valid == 0u && pte.physical_high == 0x07u);
    CHECK(pte.physical_low == 0xabcde000ul && pte.cache == 0u);

    CHECK(v9x_intel_hsw_encode_pte(0x12345800ul, 0u, &raw) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_intel_hsw_encode_pte(0x12345000ul, 0x10u, &raw) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_intel_hsw_decode_pte(0ul, 0) == V9X_STATUS_INVALID_ARGUMENT);
}

unsigned int v9x_run_intel_hsw_tests(void)
{
    failures = 0u;
    test_ggc();
    test_aperture();
    test_layout();
    test_pte();
    return failures;
}
