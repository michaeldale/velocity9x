#include <stdio.h>
#include <string.h>

#include "velocity9x/r128_2d.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

#define R128_VRAM 33554432ul

/* The value a builder wrote to one register, or 0xdeadbeef if none. */
static v9x_u32 blt_value(const struct v9x_r128_blt *blt, v9x_u32 offset)
{
    v9x_u32 index;

    for (index = 0u; index < blt->count; ++index) {
        if (blt->offsets[index] == offset) {
            return blt->values[index];
        }
    }
    return 0xdeadbeeful;
}

static void base_fill(struct v9x_r128_fill *fill)
{
    memset(fill, 0, sizeof(*fill));
    fill->vram_bytes = R128_VRAM;
    fill->target_offset = 0ul;
    fill->pitch_bytes = 1600ul;
    fill->bytes_per_pixel = 2ul;
    fill->left = 10ul;
    fill->top = 20ul;
    fill->width = 40ul;
    fill->height = 30ul;
    fill->color = 0x1234ul;
}

static void base_copy(struct v9x_r128_copy *copy)
{
    memset(copy, 0, sizeof(*copy));
    copy->vram_bytes = R128_VRAM;
    copy->source_offset = 0ul;
    copy->source_pitch_bytes = 1600ul;
    copy->destination_offset = 0ul;
    copy->destination_pitch_bytes = 1600ul;
    copy->bytes_per_pixel = 2ul;
    copy->source_left = 100ul;
    copy->source_top = 100ul;
    copy->destination_left = 300ul;
    copy->destination_top = 200ul;
    copy->width = 40ul;
    copy->height = 30ul;
}

/* Init at 800x600x16: scissors open, default pitch 100 (800 / 8), the
 * host big-endian bit cleared and the rest of DP_DATATYPE kept. */
static void test_init(void)
{
    struct v9x_r128_blt blt;

    CHECK(v9x_r128_build_init(2ul, 1600ul, 0x20030d04ul, &blt) ==
          V9X_STATUS_OK);
    CHECK(blt_value(&blt, V9X_R128_SCALE_3D_CNTL) == 0ul);
    CHECK(blt_value(&blt, V9X_R128_DEFAULT_OFFSET) == 0ul);
    CHECK(blt_value(&blt, V9X_R128_DEFAULT_PITCH) == 100ul);
    CHECK(blt_value(&blt, V9X_R128_AUX_SC_CNTL) == 0ul);
    CHECK(blt_value(&blt, V9X_R128_DEFAULT_SC_BOTTOM_RIGHT) == 0x1fff1ffful);
    CHECK(blt_value(&blt, V9X_R128_SC_TOP_LEFT) == 0ul);
    CHECK(blt_value(&blt, V9X_R128_SC_BOTTOM_RIGHT) == 0x1fff1ffful);
    CHECK(blt_value(&blt, V9X_R128_DP_WRITE_MASK) == 0xfffffffful);
    CHECK(blt_value(&blt, V9X_R128_DP_DATATYPE) == 0x00030d04ul);
    CHECK(blt.count <= V9X_R128_BLT_WRITES);

    /* 32 bpp at 1024 pixels: pitch 4096 bytes is 128 units. */
    CHECK(v9x_r128_build_init(4ul, 4096ul, 0ul, &blt) == V9X_STATUS_OK);
    CHECK(blt_value(&blt, V9X_R128_DEFAULT_PITCH) == 128ul);

    /* 24 bpp, and a pitch that is not a whole number of 8-pixel units. */
    CHECK(v9x_r128_build_init(3ul, 2400ul, 0ul, &blt) ==
          V9X_STATUS_UNSUPPORTED);
    CHECK(v9x_r128_build_init(2ul, 1604ul, 0ul, &blt) ==
          V9X_STATUS_UNSUPPORTED);
}

/* A 16-bpp fill on the 800x600 primary: every register, the size last. */
static void test_fill_primary(void)
{
    struct v9x_r128_fill fill;
    struct v9x_r128_blt blt;

    base_fill(&fill);
    CHECK(v9x_r128_build_fill(&fill, &blt) == V9X_STATUS_OK);
    CHECK(blt.count == 6u);
    CHECK(blt_value(&blt, V9X_R128_DST_PITCH_OFFSET) == (100ul << 21));
    CHECK(blt_value(&blt, V9X_R128_DP_GUI_MASTER_CNTL) ==
          (V9X_R128_GMC_DST_PITCH_OFFSET_CNTL |
           V9X_R128_GMC_BRUSH_SOLID_COLOR |
           (V9X_R128_DATATYPE_16BPP << 8) |
           V9X_R128_GMC_SRC_DATATYPE_COLOR |
           (V9X_R128_ROP3_P << 16) |
           V9X_R128_GMC_CLR_CMP_CNTL_DIS | V9X_R128_GMC_AUX_CLIP_DIS));
    CHECK(blt_value(&blt, V9X_R128_DP_BRUSH_FRGD_CLR) == 0x1234ul);
    CHECK(blt_value(&blt, V9X_R128_DP_CNTL) ==
          (V9X_R128_DST_X_LEFT_TO_RIGHT | V9X_R128_DST_Y_TOP_TO_BOTTOM));
    CHECK(blt_value(&blt, V9X_R128_DST_Y_X) == ((20ul << 16) | 10ul));
    CHECK(blt.offsets[blt.count - 1u] == V9X_R128_DST_WIDTH_HEIGHT);
    CHECK(blt.values[blt.count - 1u] == ((40ul << 16) | 30ul));
}

/* A surface off the desktop at an offset that is not 32-byte aligned: the
 * remainder becomes pixels on x, the field takes the aligned offset. */
static void test_fill_unaligned_surface(void)
{
    struct v9x_r128_fill fill;
    struct v9x_r128_blt blt;

    base_fill(&fill);
    fill.target_offset = 0x00100010ul;   /* 16 bytes past 32: 8 pixels */
    fill.pitch_bytes = 128ul;
    fill.left = 1ul;
    fill.top = 2ul;
    fill.width = 8ul;
    fill.height = 4ul;
    CHECK(v9x_r128_build_fill(&fill, &blt) == V9X_STATUS_OK);
    CHECK(blt_value(&blt, V9X_R128_DST_PITCH_OFFSET) ==
          ((8ul << 21) | (0x00100000ul >> 5)));
    CHECK(blt_value(&blt, V9X_R128_DST_Y_X) == ((2ul << 16) | 9ul));

    /* 32 bpp and an offset that is not a whole pixel past 32 bytes. */
    fill.bytes_per_pixel = 4ul;
    fill.pitch_bytes = 256ul;
    fill.target_offset = 0x00100002ul;
    fill.color = 0x00112233ul;
    CHECK(v9x_r128_build_fill(&fill, &blt) == V9X_STATUS_UNSUPPORTED);
}

static void test_fill_refusals(void)
{
    struct v9x_r128_fill fill;
    struct v9x_r128_blt blt;

    base_fill(&fill);
    fill.width = 0ul;
    CHECK(v9x_r128_build_fill(&fill, &blt) == V9X_STATUS_INVALID_ARGUMENT);

    base_fill(&fill);
    fill.left = 790ul;             /* 790 + 40 > 800 pixels */
    CHECK(v9x_r128_build_fill(&fill, &blt) == V9X_STATUS_INVALID_ARGUMENT);

    base_fill(&fill);
    fill.top = R128_VRAM / 1600ul; /* past the end of memory */
    fill.height = 1ul;
    CHECK(v9x_r128_build_fill(&fill, &blt) != V9X_STATUS_OK);

    base_fill(&fill);
    fill.color = 0x12345ul;        /* wider than 16 bits */
    CHECK(v9x_r128_build_fill(&fill, &blt) == V9X_STATUS_INVALID_ARGUMENT);

    base_fill(&fill);
    fill.bytes_per_pixel = 3ul;
    fill.pitch_bytes = 2400ul;
    CHECK(v9x_r128_build_fill(&fill, &blt) == V9X_STATUS_UNSUPPORTED);

    base_fill(&fill);
    fill.pitch_bytes = 1602ul;     /* not a whole 8-pixel unit */
    CHECK(v9x_r128_build_fill(&fill, &blt) == V9X_STATUS_UNSUPPORTED);

    base_fill(&fill);
    fill.pitch_bytes = 0x800ul * 8ul * 2ul;   /* 2048 units, one too many */
    CHECK(v9x_r128_build_fill(&fill, &blt) == V9X_STATUS_UNSUPPORTED);

    base_fill(&fill);
    fill.top = 0x2000ul;           /* past the 13-bit field */
    fill.pitch_bytes = 16ul;
    fill.left = 0ul;
    fill.width = 8ul;
    fill.height = 1ul;
    CHECK(v9x_r128_build_fill(&fill, &blt) == V9X_STATUS_UNSUPPORTED);

    CHECK(v9x_r128_build_fill(0, &blt) == V9X_STATUS_INVALID_ARGUMENT);
}

/* A disjoint copy: forward in both directions, the size last, height in
 * the high half (DST_HEIGHT_WIDTH, unlike the fill's register). */
static void test_copy_disjoint(void)
{
    struct v9x_r128_copy copy;
    struct v9x_r128_blt blt;

    base_copy(&copy);
    CHECK(v9x_r128_build_copy(&copy, &blt) == V9X_STATUS_OK);
    CHECK(blt.count == 7u);
    CHECK(blt_value(&blt, V9X_R128_SRC_PITCH_OFFSET) == (100ul << 21));
    CHECK(blt_value(&blt, V9X_R128_DST_PITCH_OFFSET) == (100ul << 21));
    CHECK(blt_value(&blt, V9X_R128_DP_GUI_MASTER_CNTL) ==
          (V9X_R128_GMC_SRC_PITCH_OFFSET_CNTL |
           V9X_R128_GMC_DST_PITCH_OFFSET_CNTL |
           V9X_R128_GMC_BRUSH_NONE |
           (V9X_R128_DATATYPE_16BPP << 8) |
           V9X_R128_GMC_SRC_DATATYPE_COLOR |
           (V9X_R128_ROP3_S << 16) |
           V9X_R128_DP_SRC_SOURCE_MEMORY |
           V9X_R128_GMC_CLR_CMP_CNTL_DIS | V9X_R128_GMC_AUX_CLIP_DIS));
    CHECK(blt_value(&blt, V9X_R128_DP_CNTL) ==
          (V9X_R128_DST_X_LEFT_TO_RIGHT | V9X_R128_DST_Y_TOP_TO_BOTTOM));
    CHECK(blt_value(&blt, V9X_R128_SRC_Y_X) == ((100ul << 16) | 100ul));
    CHECK(blt_value(&blt, V9X_R128_DST_Y_X) == ((200ul << 16) | 300ul));
    CHECK(blt.offsets[blt.count - 1u] == V9X_R128_DST_HEIGHT_WIDTH);
    CHECK(blt.values[blt.count - 1u] == ((30ul << 16) | 40ul));
}

/* Overlap on one surface. Down: bottom to top from the last row. Right on
 * the same rows: right to left from the last column. Up-left: forward. */
static void test_copy_overlap(void)
{
    struct v9x_r128_copy copy;
    struct v9x_r128_blt blt;

    base_copy(&copy);
    copy.destination_left = 100ul;
    copy.destination_top = 104ul;
    CHECK(v9x_r128_build_copy(&copy, &blt) == V9X_STATUS_OK);
    CHECK(blt_value(&blt, V9X_R128_DP_CNTL) == V9X_R128_DST_X_LEFT_TO_RIGHT);
    CHECK(blt_value(&blt, V9X_R128_SRC_Y_X) == ((129ul << 16) | 100ul));
    CHECK(blt_value(&blt, V9X_R128_DST_Y_X) == ((133ul << 16) | 100ul));

    base_copy(&copy);
    copy.destination_left = 104ul;
    copy.destination_top = 100ul;
    CHECK(v9x_r128_build_copy(&copy, &blt) == V9X_STATUS_OK);
    CHECK(blt_value(&blt, V9X_R128_DP_CNTL) == V9X_R128_DST_Y_TOP_TO_BOTTOM);
    CHECK(blt_value(&blt, V9X_R128_SRC_Y_X) == ((100ul << 16) | 139ul));
    CHECK(blt_value(&blt, V9X_R128_DST_Y_X) == ((100ul << 16) | 143ul));

    /* Down and right together: both directions reversed. */
    base_copy(&copy);
    copy.destination_left = 104ul;
    copy.destination_top = 104ul;
    CHECK(v9x_r128_build_copy(&copy, &blt) == V9X_STATUS_OK);
    CHECK(blt_value(&blt, V9X_R128_DP_CNTL) == 0ul);
    CHECK(blt_value(&blt, V9X_R128_SRC_Y_X) == ((129ul << 16) | 139ul));
    CHECK(blt_value(&blt, V9X_R128_DST_Y_X) == ((133ul << 16) | 143ul));

    base_copy(&copy);
    copy.destination_left = 96ul;
    copy.destination_top = 96ul;
    CHECK(v9x_r128_build_copy(&copy, &blt) == V9X_STATUS_OK);
    CHECK(blt_value(&blt, V9X_R128_DP_CNTL) ==
          (V9X_R128_DST_X_LEFT_TO_RIGHT | V9X_R128_DST_Y_TOP_TO_BOTTOM));
}

/* Two surfaces at different offsets and pitches. */
static void test_copy_between_surfaces(void)
{
    struct v9x_r128_copy copy;
    struct v9x_r128_blt blt;

    base_copy(&copy);
    copy.source_offset = 0x00200000ul;
    copy.source_pitch_bytes = 512ul;
    copy.source_left = 0ul;
    copy.source_top = 0ul;
    CHECK(v9x_r128_build_copy(&copy, &blt) == V9X_STATUS_OK);
    CHECK(blt_value(&blt, V9X_R128_SRC_PITCH_OFFSET) ==
          ((32ul << 21) | (0x00200000ul >> 5)));
    CHECK(blt_value(&blt, V9X_R128_DP_CNTL) ==
          (V9X_R128_DST_X_LEFT_TO_RIGHT | V9X_R128_DST_Y_TOP_TO_BOTTOM));

    base_copy(&copy);
    copy.bytes_per_pixel = 1ul;
    copy.source_pitch_bytes = 800ul;
    copy.destination_pitch_bytes = 800ul;
    CHECK(v9x_r128_build_copy(&copy, &blt) == V9X_STATUS_OK);
    CHECK(blt_value(&blt, V9X_R128_DST_PITCH_OFFSET) == (100ul << 21));
    CHECK((blt_value(&blt, V9X_R128_DP_GUI_MASTER_CNTL) >> 8 & 0xful) ==
          V9X_R128_DATATYPE_8BPP);

    base_copy(&copy);
    copy.source_left = 780ul;       /* past the source's width */
    CHECK(v9x_r128_build_copy(&copy, &blt) == V9X_STATUS_INVALID_ARGUMENT);
}

static void test_status(void)
{
    CHECK(v9x_r128_gui_idle(0x00000040ul) != 0ul);
    CHECK(v9x_r128_gui_idle(0x80000040ul) == 0ul);
    CHECK(v9x_r128_gui_idle(0x0000003ful) == 0ul);
    CHECK(v9x_r128_gui_fifo_free(0x8000002aul) == 42ul);
    CHECK(v9x_r128_pm4_direct(0x00000000ul) != 0ul);
    CHECK(v9x_r128_pm4_direct(0x80000011ul) == 0ul);
    CHECK(v9x_r128_pm4_direct(0x08000000ul) != 0ul);
}

unsigned int v9x_run_r128_2d_tests(void)
{
    failures = 0u;
    test_init();
    test_fill_primary();
    test_fill_unaligned_surface();
    test_fill_refusals();
    test_copy_disjoint();
    test_copy_overlap();
    test_copy_between_surfaces();
    test_status();
    return failures;
}
