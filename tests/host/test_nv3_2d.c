#include <stdio.h>
#include <string.h>

#include "velocity9x/nv3_2d.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

/* The value written to an offset in a list, or 0xdeadbeef if none; the
 * last write wins, as on the card. */
static v9x_u32 list_value(const struct v9x_nv3_write *list, v9x_u32 count,
                          v9x_u32 offset)
{
    v9x_u32 index;
    v9x_u32 value = 0xdeadbeeful;

    for (index = 0u; index < count; ++index) {
        if (list[index].offset == offset) {
            value = list[index].value;
        }
    }
    return value;
}

/*
 * The bring-up at 800x600x16, the mode the Phase 3 probe ran on M-7V636C
 * (docs\decisions\2026-11-02-riva128-2d-engine-writes.md): rivafb's order
 * and values, PFB_CONFIG_0 1219h as the card took it.
 */
static void test_setup_16(void)
{
    struct v9x_nv3_setup setup;

    CHECK(v9x_nv3_build_setup(2ul, 800ul, 600ul, 1600ul, &setup) ==
          V9X_STATUS_OK);

    /* PMC first: interrupts off, PFIFO and PGRAPH cycled. */
    CHECK(setup.before[0].offset == V9X_NV3_PMC_INTR_EN);
    CHECK(setup.before[1].offset == V9X_NV3_PMC_ENABLE &&
          setup.before[1].value == 0xffff00fful);
    CHECK(setup.before[2].offset == V9X_NV3_PMC_ENABLE &&
          setup.before[2].value == 0xfffffffful);
    CHECK(list_value(setup.before, setup.before_count,
                     V9X_NV3_PFB_CONFIG_0) == 0x00001219ul);
    /* PFIFO: tables placed as rivafb places them, FIFO back on last. */
    CHECK(list_value(setup.before, setup.before_count, 0x002218ul) ==
          0x00002000ul);
    CHECK(list_value(setup.before, setup.before_count, 0x002214ul) ==
          0x00002200ul);
    CHECK(list_value(setup.before, setup.before_count, 0x0032f0ul) == 0ul);
    CHECK(setup.before[setup.before_count - 1u].offset == 0x002500ul &&
          setup.before[setup.before_count - 1u].value == 1ul);
    CHECK(setup.before_count == 34u);

    /* RAMIN: the RAMHT entries, the mark first, and the X1R5G5B5 objects. */
    CHECK(setup.ramin_count == 21u);
    CHECK(setup.ramin[0].offset == V9X_NV3_RAMIN_MARK_OFFSET &&
          setup.ramin[0].value == V9X_NV3_RAMIN_MARK_VALUE);
    CHECK(list_value(setup.ramin, setup.ramin_count, 0x0914ul) ==
          0x00d00345ul);
    CHECK(list_value(setup.ramin, setup.ramin_count, 0x3460ul) ==
          0x10110200ul);
    CHECK(list_value(setup.ramin, setup.ramin_count, 0x3430ul) ==
          0x10110208ul);

    /* PGRAPH: DEBUG_0 first, the FIFO enable before the surfaces. */
    CHECK(setup.after[0].offset == 0x400080ul &&
          setup.after[0].value == 0x1230001ful);
    CHECK(list_value(setup.after, setup.after_count, 0x4006a4ul) == 1ul);
    CHECK(list_value(setup.after, setup.after_count,
                     V9X_NV3_PGRAPH_SURF_CONFIG) == 0x00002222ul);
    CHECK(list_value(setup.after, setup.after_count,
                     V9X_NV3_PGRAPH_SURF_PITCH + 4ul) == 1600ul);
    CHECK(list_value(setup.after, setup.after_count,
                     V9X_NV3_PGRAPH_SURF_OFFSET) == 0ul);

    /* Seven bindings, subchannel s at 800000h + s * 2000h. */
    CHECK(setup.bind_count == 7u);
    CHECK(setup.bind[4].offset == 0x808000ul &&
          setup.bind[4].value == 0x80000011ul);
    CHECK(setup.bind[5].offset == 0x80a000ul &&
          setup.bind[5].value == 0x80000012ul);

    /* Clip to the screen, width low; SRCCOPY last. */
    CHECK(list_value(setup.defaults, setup.defaults_count, 0x802304ul) ==
          (800ul | (600ul << 16)));
    CHECK(setup.defaults[setup.defaults_count - 1u].offset == 0x800300ul &&
          setup.defaults[setup.defaults_count - 1u].value == 0xccul);
}

static void test_setup_depths(void)
{
    struct v9x_nv3_setup setup;

    CHECK(v9x_nv3_build_setup(1ul, 800ul, 600ul, 800ul, &setup) ==
          V9X_STATUS_OK);
    CHECK(list_value(setup.after, setup.after_count,
                     V9X_NV3_PGRAPH_SURF_CONFIG) == 0x00001111ul);
    CHECK(list_value(setup.ramin, setup.ramin_count, 0x3410ul) ==
          0x10110203ul);
    CHECK(v9x_nv3_build_setup(4ul, 800ul, 600ul, 3200ul, &setup) ==
          V9X_STATUS_OK);
    CHECK(list_value(setup.after, setup.after_count,
                     V9X_NV3_PGRAPH_SURF_CONFIG) == 0x00003333ul);
    CHECK(list_value(setup.ramin, setup.ramin_count, 0x3410ul) ==
          0x10110201ul);
    CHECK(v9x_nv3_build_setup(3ul, 800ul, 600ul, 2400ul, &setup) ==
          V9X_STATUS_UNSUPPORTED);
    /* 1400 bytes is not a whole number of PGRAPH's 16-byte pitch units. */
    CHECK(v9x_nv3_build_setup(1ul, 1400ul, 1050ul, 1400ul, &setup) ==
          V9X_STATUS_UNSUPPORTED);
}

/* PFB_CONFIG_0 as the card took it at each depth (the probe's readback). */
static void test_pfb_config(void)
{
    CHECK(v9x_nv3_pfb_config(1ul, 800ul) == 0x00001119ul);
    CHECK(v9x_nv3_pfb_config(2ul, 800ul) == 0x00001219ul);
    CHECK(v9x_nv3_pfb_config(4ul, 800ul) == 0x00001319ul);
    /* The BIOS's own value after POST: 640 wide, 1 byte. */
    CHECK(v9x_nv3_pfb_config(1ul, 640ul) == 0x00001114ul);
}

/* The alias the RAMIN probe measured on the 8 MiB card: RAMIN 0-Fh at
 * 7FFFF0h, 10h-1Fh at 7FFFE0h, 7FF0h-7FFFh at 7F8000h. */
static void test_ramin_mapping(void)
{
    CHECK(v9x_nv3_ramin_vram_offset(0x800000ul, 0x0000ul) == 0x7ffff0ul);
    CHECK(v9x_nv3_ramin_vram_offset(0x800000ul, 0x000cul) == 0x7ffffcul);
    CHECK(v9x_nv3_ramin_vram_offset(0x800000ul, 0x0010ul) == 0x7fffe0ul);
    CHECK(v9x_nv3_ramin_vram_offset(0x800000ul, 0x0804ul) == 0x7ff7f4ul);
    CHECK(v9x_nv3_ramin_vram_offset(0x800000ul, 0x7ff0ul) == 0x7f8000ul);
    CHECK(v9x_nv3_ramin_vram_offset(0x800000ul, 0x7ffcul) == 0x7f800cul);
}

static void base_fill(struct v9x_nv3_fill *fill)
{
    memset(fill, 0, sizeof(*fill));
    fill->vram_bytes = 0x7f8000ul;
    fill->target_offset = 0ul;
    fill->pitch_bytes = 3200ul;
    fill->bytes_per_pixel = 4ul;
    fill->left = 8ul;
    fill->top = 2ul;
    fill->width = 40ul;
    fill->height = 5ul;
    fill->color = 0xff123456ul;
}

/* The probe's fill A at 32 bpp: surface 0 at the target, the clip around
 * the rectangle, the colour, then x in the high half. */
static void test_fill(void)
{
    struct v9x_nv3_fill fill;
    struct v9x_nv3_op op;

    base_fill(&fill);
    CHECK(v9x_nv3_build_fill(&fill, &op) == V9X_STATUS_OK);
    CHECK(op.surface_count == 2u);
    CHECK(op.surface[0].offset == V9X_NV3_PGRAPH_SURF_OFFSET &&
          op.surface[0].value == 0ul);
    CHECK(op.surface[1].offset == V9X_NV3_PGRAPH_SURF_PITCH &&
          op.surface[1].value == 3200ul);
    CHECK(op.method_count == 5u);
    CHECK(op.methods[0].offset == 0x802300ul && op.methods[0].value == 0ul);
    CHECK(op.methods[1].offset == 0x802304ul &&
          op.methods[1].value == (48ul | (7ul << 16)));
    CHECK(op.methods[2].offset == 0x80a3fcul &&
          op.methods[2].value == 0xff123456ul);
    CHECK(op.methods[3].offset == 0x80a400ul &&
          op.methods[3].value == ((8ul << 16) | 2ul));
    CHECK(op.methods[4].offset == 0x80a404ul &&
          op.methods[4].value == ((40ul << 16) | 5ul));

    /* 8 and 16 bpp colours are cut to the pixel. */
    fill.bytes_per_pixel = 2ul;
    fill.pitch_bytes = 1600ul;
    fill.color = 0x1234f81ful;
    CHECK(v9x_nv3_build_fill(&fill, &op) == V9X_STATUS_OK);
    CHECK(op.methods[2].value == 0xf81ful);
}

static void test_fill_placement(void)
{
    struct v9x_nv3_fill fill;
    struct v9x_nv3_op op;

    /* An offset off the 16-byte unit by whole pixels moves the left edge. */
    base_fill(&fill);
    fill.target_offset = 0x100008ul;
    CHECK(v9x_nv3_build_fill(&fill, &op) == V9X_STATUS_OK);
    CHECK(op.surface[0].value == 0x100000ul);
    CHECK(op.methods[3].value == ((10ul << 16) | 2ul));

    /* Off by part of a pixel: not expressible. */
    fill.target_offset = 0x100002ul;
    CHECK(v9x_nv3_build_fill(&fill, &op) == V9X_STATUS_UNSUPPORTED);

    /* Past 4 MiB: not asked of the engine until measured. */
    base_fill(&fill);
    fill.target_offset = 0x400000ul;
    CHECK(v9x_nv3_build_fill(&fill, &op) == V9X_STATUS_UNSUPPORTED);
    fill.target_offset = 0x3ffff0ul - 3200ul;
    CHECK(v9x_nv3_build_fill(&fill, &op) == V9X_STATUS_UNSUPPORTED);

    /* Past the heap, or outside the surface: the caller's mistake. */
    base_fill(&fill);
    fill.target_offset = 0x7f8000ul;
    CHECK(v9x_nv3_build_fill(&fill, &op) == V9X_STATUS_INVALID_ARGUMENT);
    base_fill(&fill);
    fill.left = 790ul;
    fill.width = 20ul;
    CHECK(v9x_nv3_build_fill(&fill, &op) == V9X_STATUS_INVALID_ARGUMENT);
    base_fill(&fill);
    fill.width = 0ul;
    CHECK(v9x_nv3_build_fill(&fill, &op) == V9X_STATUS_INVALID_ARGUMENT);
    base_fill(&fill);
    fill.bytes_per_pixel = 3ul;
    CHECK(v9x_nv3_build_fill(&fill, &op) == V9X_STATUS_UNSUPPORTED);
}

/* The probe's blit at 16 bpp, between two surfaces: surface 0 the
 * destination, surface 1 the source, y in the high half of the points. */
static void test_copy(void)
{
    struct v9x_nv3_copy copy;
    struct v9x_nv3_op op;

    memset(&copy, 0, sizeof(copy));
    copy.vram_bytes = 0x7f8000ul;
    copy.source_offset = 0x200000ul;
    copy.source_pitch_bytes = 1600ul;
    copy.destination_offset = 0x200000ul + 2ul * 1600ul;
    copy.destination_pitch_bytes = 1600ul;
    copy.bytes_per_pixel = 2ul;
    copy.source_left = 4ul;
    copy.source_top = 1ul;
    copy.destination_left = 120ul;
    copy.destination_top = 1ul;
    copy.width = 80ul;
    copy.height = 7ul;
    CHECK(v9x_nv3_build_copy(&copy, &op) == V9X_STATUS_OK);
    CHECK(op.surface_count == 4u);
    CHECK(op.surface[0].offset == V9X_NV3_PGRAPH_SURF_OFFSET &&
          op.surface[0].value == 0x200c80ul);
    CHECK(op.surface[2].offset == V9X_NV3_PGRAPH_SURF_OFFSET + 4ul &&
          op.surface[2].value == 0x200000ul);
    CHECK(op.surface[3].offset == V9X_NV3_PGRAPH_SURF_PITCH + 4ul &&
          op.surface[3].value == 1600ul);
    CHECK(op.method_count == 5u);
    CHECK(op.methods[1].value == (200ul | (8ul << 16)));
    CHECK(op.methods[2].offset == 0x808300ul &&
          op.methods[2].value == ((1ul << 16) | 4ul));
    CHECK(op.methods[3].offset == 0x808304ul &&
          op.methods[3].value == ((1ul << 16) | 120ul));
    CHECK(op.methods[4].offset == 0x808308ul &&
          op.methods[4].value == ((7ul << 16) | 80ul));

    /* A source the surface does not hold. */
    copy.source_top = 0xfffful;
    CHECK(v9x_nv3_build_copy(&copy, &op) != V9X_STATUS_OK);
}

unsigned int v9x_run_nv3_2d_tests(void)
{
    failures = 0u;
    test_setup_16();
    test_setup_depths();
    test_pfb_config();
    test_ramin_mapping();
    test_fill();
    test_fill_placement();
    test_copy();
    return failures;
}
