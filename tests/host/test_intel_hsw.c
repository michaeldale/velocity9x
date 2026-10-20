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

static v9x_status v9x_heap(v9x_u32 stolen, v9x_u32 protected_bytes,
                           v9x_u32 mapped, v9x_u32 vbe, v9x_u32 visible,
                           struct v9x_intel_hsw_heap_plan *plan)
{
    struct v9x_intel_hsw_heap_inputs in;

    in.stolen_bytes = stolen;
    in.protected_bytes = protected_bytes;
    in.mapped_bytes = mapped;
    in.vbe_bytes = vbe;
    in.visible_bytes = visible;
    return v9x_intel_hsw_plan_heap(&in, plan);
}

static int v9x_heap_refused(const struct v9x_intel_hsw_heap_plan *plan)
{
    return plan->usable_top == 0ul && plan->reserve_offset == 0ul &&
           plan->probe_offset == 0ul && plan->heap_end == 0ul;
}

static void test_heap_plan(void)
{
    struct v9x_intel_hsw_heap_plan plan;

    /* The i5-4570: 32 MiB stolen, the top megabyte not writable, VBE's
     * 33,488,896 bytes, a 64 MiB mapping, 1024x768x32. */
    CHECK(v9x_heap(0x02000000ul, 0x00100000ul, 0x04000000ul, 0x01ff0000ul,
                   0x00300000ul, &plan) == V9X_STATUS_OK);
    CHECK(plan.usable_top == 0x01f00000ul);
    CHECK(plan.probe_offset == 0x01ef0000ul);
    CHECK(plan.reserve_offset == 0x01ed0000ul);
    CHECK(plan.heap_end == 0x01ed0000ul);
    CHECK(plan.reserve_offset + V9X_INTEL_HSW_RESERVE_BYTES ==
          plan.probe_offset);
    CHECK(plan.probe_offset + V9X_INTEL_HSW_PROBE_REGION_BYTES ==
          plan.usable_top);

    /* 64 MiB stolen, as the tester's 4F00h figure suggests. */
    CHECK(v9x_heap(0x04000000ul, 0x00100000ul, 0x04000000ul, 0x03ff0000ul,
                   0x00300000ul, &plan) == V9X_STATUS_OK);
    CHECK(plan.usable_top == 0x03f00000ul);
    CHECK(plan.heap_end == 0x03ed0000ul);

    /* A larger protected range moves everything down with it. */
    CHECK(v9x_heap(0x02000000ul, 0x00400000ul, 0x04000000ul, 0ul,
                   0x00300000ul, &plan) == V9X_STATUS_OK);
    CHECK(plan.usable_top == 0x01c00000ul);
    CHECK(plan.heap_end == 0x01bd0000ul);

    /* None protected (the register reports nothing). */
    CHECK(v9x_heap(0x02000000ul, 0ul, 0x04000000ul, 0ul, 0x00300000ul,
                   &plan) == V9X_STATUS_OK);
    CHECK(plan.usable_top == 0x02000000ul);

    /* The mapping is smaller than stolen: it sets the top. */
    CHECK(v9x_heap(0x08000000ul, 0x00100000ul, 0x04000000ul, 0ul,
                   0x00300000ul, &plan) == V9X_STATUS_OK);
    CHECK(plan.usable_top == 0x04000000ul);
    CHECK(plan.heap_end == 0x03fd0000ul);

    /* VBE claims less than either: it sets the top. */
    CHECK(v9x_heap(0x02000000ul, 0x00100000ul, 0x04000000ul, 0x01000000ul,
                   0x00300000ul, &plan) == V9X_STATUS_OK);
    CHECK(plan.usable_top == 0x01000000ul);

    /* A protected size off the 64 KB grid rounds the top down. */
    CHECK(v9x_heap(0x02000000ul, 0x00041000ul, 0x04000000ul, 0ul,
                   0x00300000ul, &plan) == V9X_STATUS_OK);
    CHECK(plan.usable_top == 0x01fb0000ul);
    CHECK(plan.reserve_offset == 0x01f80000ul);

    /* The desktop: one 64 KB block of heap is accepted, none refused. */
    CHECK(v9x_heap(0x00400000ul, 0x00100000ul, 0x04000000ul, 0ul,
                   0x002c0000ul, &plan) == V9X_STATUS_OK);
    CHECK(plan.heap_end == 0x002d0000ul);
    CHECK(v9x_heap(0x00400000ul, 0x00100000ul, 0x04000000ul, 0ul,
                   0x002d0000ul, &plan) == V9X_STATUS_UNSUPPORTED);
    CHECK(v9x_heap_refused(&plan));

    /* A top below the reserve and probe region does not wrap. */
    CHECK(v9x_heap(0x02000000ul, 0ul, 0x04000000ul, 0x00020000ul,
                   0x00001000ul, &plan) == V9X_STATUS_UNSUPPORTED);
    CHECK(v9x_heap_refused(&plan));

    /* Protected is all of stolen, or more. */
    CHECK(v9x_heap(0x02000000ul, 0x02000000ul, 0x04000000ul, 0ul,
                   0x00300000ul, &plan) == V9X_STATUS_UNSUPPORTED);
    CHECK(v9x_heap_refused(&plan));

    /* Zero where a size is required, and null pointers. */
    CHECK(v9x_heap(0ul, 0ul, 0x04000000ul, 0ul, 0x00300000ul, &plan) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_heap_refused(&plan));
    CHECK(v9x_heap(0x02000000ul, 0ul, 0ul, 0ul, 0x00300000ul, &plan) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_heap(0x02000000ul, 0ul, 0x04000000ul, 0ul, 0ul, &plan) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_heap(0x02000000ul, 0ul, 0x04000000ul, 0ul, 0x00300000ul, 0) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_intel_hsw_plan_heap(0, &plan) == V9X_STATUS_INVALID_ARGUMENT);
}

static void test_protected_bytes(void)
{
    v9x_u32 bytes;

    /* The i5-4570, 2026-10-20: BDSM DD200001, 32 MiB, 1082C0h DF100017. */
    CHECK(v9x_intel_hsw_protected_bytes(0xdd200001ul, 0x02000000ul,
                                        0xdf100017ul, &bytes) ==
          V9X_STATUS_OK);
    CHECK(bytes == 0x00100000ul);

    /* The low bits are not read: only the base sets the size. */
    CHECK(v9x_intel_hsw_protected_bytes(0xdd200001ul, 0x02000000ul,
                                        0xdf1ffffful, &bytes) ==
          V9X_STATUS_OK);
    CHECK(bytes == 0x00100000ul);

    /* Not enabled: nothing protected. */
    CHECK(v9x_intel_hsw_protected_bytes(0xdd200001ul, 0x02000000ul,
                                        0xdf100016ul, &bytes) ==
          V9X_STATUS_OK);
    CHECK(bytes == 0ul);

    /* 64 MiB stolen with the same megabyte at its top. */
    CHECK(v9x_intel_hsw_protected_bytes(0xdc000001ul, 0x04000000ul,
                                        0xdff00001ul, &bytes) ==
          V9X_STATUS_OK);
    CHECK(bytes == 0x00100000ul);

    /* A base at the bottom of stolen protects all of it; the heap plan
     * refuses that, not this decode. */
    CHECK(v9x_intel_hsw_protected_bytes(0xdd200001ul, 0x02000000ul,
                                        0xdd200001ul, &bytes) ==
          V9X_STATUS_OK);
    CHECK(bytes == 0x02000000ul);

    /* A base below stolen or at its top is not understood. */
    CHECK(v9x_intel_hsw_protected_bytes(0xdd200001ul, 0x02000000ul,
                                        0xdd100001ul, &bytes) ==
          V9X_STATUS_UNSUPPORTED);
    CHECK(bytes == 0ul);
    CHECK(v9x_intel_hsw_protected_bytes(0xdd200001ul, 0x02000000ul,
                                        0xdf200001ul, &bytes) ==
          V9X_STATUS_UNSUPPORTED);

    /* Stolen memory that would wrap past 4 GB. */
    CHECK(v9x_intel_hsw_protected_bytes(0xfff00001ul, 0x00200000ul,
                                        0xfff00001ul, &bytes) ==
          V9X_STATUS_UNSUPPORTED);

    CHECK(v9x_intel_hsw_protected_bytes(0xdd200001ul, 0ul, 0xdf100017ul,
                                        &bytes) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_intel_hsw_protected_bytes(0xdd200001ul, 0x02000000ul,
                                        0xdf100017ul, 0) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

unsigned int v9x_run_intel_hsw_tests(void)
{
    failures = 0u;
    test_ggc();
    test_aperture();
    test_layout();
    test_pte();
    test_heap_plan();
    test_protected_bytes();
    return failures;
}
