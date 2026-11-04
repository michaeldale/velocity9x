#include <stdio.h>
#include <string.h>

#include "velocity9x/rs690_2d.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

/* M-GB8KRE's frame buffer: 128 MiB at GPU address 78000000
 * (docs\decisions\2026-11-04-rs690-register-probe.md). */
#define RS690_FB_BASE  0x78000000ul
#define RS690_FB_BYTES 0x08000000ul
#define RS690_FB_FIELD (RS690_FB_BASE >> 10)

/* The value a builder wrote to one register, or 0xdeadbeef if none. */
static v9x_u32 blt_value(const struct v9x_rs690_blt *blt, v9x_u32 offset)
{
    v9x_u32 index;

    for (index = 0u; index < blt->count; ++index) {
        if (blt->offsets[index] == offset) {
            return blt->values[index];
        }
    }
    return 0xdeadbeeful;
}

static void base_fill(struct v9x_rs690_fill *fill)
{
    memset(fill, 0, sizeof(*fill));
    fill->fb_gpu_base = RS690_FB_BASE;
    fill->fb_bytes = RS690_FB_BYTES;
    fill->target_offset = 0ul;
    fill->pitch_bytes = 1600ul;
    fill->bytes_per_pixel = 2ul;
    fill->left = 10ul;
    fill->top = 20ul;
    fill->width = 40ul;
    fill->height = 30ul;
    fill->color = 0x1234ul;
}

static void base_copy(struct v9x_rs690_copy *copy)
{
    memset(copy, 0, sizeof(*copy));
    copy->fb_gpu_base = RS690_FB_BASE;
    copy->fb_bytes = RS690_FB_BYTES;
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

/* The scissor opened, every plane writable, the host big-endian bit
 * cleared and the rest of DP_DATATYPE kept. */
static void test_init(void)
{
    struct v9x_rs690_blt blt;

    CHECK(v9x_rs690_build_init(0x20010f06ul, &blt) == V9X_STATUS_OK);
    CHECK(blt.count == 3u);
    CHECK(blt_value(&blt, V9X_RS690_DP_DATATYPE) == 0x00010f06ul);
    CHECK(blt_value(&blt, V9X_RS690_DEFAULT_SC_BOTTOM_RIGHT) == 0x1fff1ffful);
    CHECK(blt_value(&blt, V9X_RS690_DP_WRITE_MASK) == 0xfffffffful);
    CHECK(v9x_rs690_build_init(0ul, 0) == V9X_STATUS_INVALID_ARGUMENT);
}

/* A 16-bpp fill on an 800x600 primary at the frame buffer's start: every
 * register Emit2DState writes, the size last. */
static void test_fill_primary(void)
{
    struct v9x_rs690_fill fill;
    struct v9x_rs690_blt blt;

    base_fill(&fill);
    CHECK(v9x_rs690_build_fill(&fill, &blt) == V9X_STATUS_OK);
    CHECK(blt.count == 11u);
    CHECK(blt.offsets[0] == V9X_RS690_DEFAULT_SC_BOTTOM_RIGHT);
    CHECK(blt_value(&blt, V9X_RS690_DST_PITCH_OFFSET) ==
          ((25ul << 22) | RS690_FB_FIELD));
    CHECK(blt_value(&blt, V9X_RS690_DP_GUI_MASTER_CNTL) ==
          (V9X_RS690_GMC_DST_PITCH_OFFSET_CNTL |
           V9X_RS690_GMC_BRUSH_SOLID_COLOR |
           (V9X_RS690_DATATYPE_16BPP << 8) |
           V9X_RS690_GMC_SRC_DATATYPE_COLOR |
           (V9X_RS690_ROP3_P << 16) |
           V9X_RS690_GMC_CLR_CMP_CNTL_DIS));
    CHECK(blt_value(&blt, V9X_RS690_DP_BRUSH_FRGD_CLR) == 0x1234ul);
    CHECK(blt_value(&blt, V9X_RS690_DP_BRUSH_BKGD_CLR) == 0ul);
    CHECK(blt_value(&blt, V9X_RS690_DP_SRC_FRGD_CLR) == 0xfffffffful);
    CHECK(blt_value(&blt, V9X_RS690_DP_SRC_BKGD_CLR) == 0ul);
    CHECK(blt_value(&blt, V9X_RS690_DP_WRITE_MASK) == 0xfffffffful);
    CHECK(blt_value(&blt, V9X_RS690_DP_CNTL) ==
          (V9X_RS690_DST_X_LEFT_TO_RIGHT | V9X_RS690_DST_Y_TOP_TO_BOTTOM));
    CHECK(blt_value(&blt, V9X_RS690_SRC_PITCH_OFFSET) == 0xdeadbeeful);
    CHECK(blt_value(&blt, V9X_RS690_DST_Y_X) == ((20ul << 16) | 10ul));
    CHECK(blt.offsets[blt.count - 1u] == V9X_RS690_DST_HEIGHT_WIDTH);
    CHECK(blt.values[blt.count - 1u] == ((30ul << 16) | 40ul));
}

/* A surface off the desktop at an offset that is not 1 KiB aligned: the
 * remainder becomes pixels on x, the field takes the aligned address. */
static void test_fill_unaligned_surface(void)
{
    struct v9x_rs690_fill fill;
    struct v9x_rs690_blt blt;

    base_fill(&fill);
    fill.target_offset = 0x01000010ul;   /* 16 bytes past 1 KiB: 8 pixels */
    fill.pitch_bytes = 128ul;
    fill.left = 1ul;
    fill.top = 2ul;
    fill.width = 8ul;
    fill.height = 4ul;
    CHECK(v9x_rs690_build_fill(&fill, &blt) == V9X_STATUS_OK);
    CHECK(blt_value(&blt, V9X_RS690_DST_PITCH_OFFSET) ==
          ((2ul << 22) | ((RS690_FB_BASE + 0x01000000ul) >> 10)));
    CHECK(blt_value(&blt, V9X_RS690_DST_Y_X) == ((2ul << 16) | 9ul));

    /* 32 bpp and an offset that is not a whole pixel past 1 KiB. */
    fill.bytes_per_pixel = 4ul;
    fill.pitch_bytes = 256ul;
    fill.target_offset = 0x01000002ul;
    fill.color = 0x00112233ul;
    CHECK(v9x_rs690_build_fill(&fill, &blt) == V9X_STATUS_UNSUPPORTED);
}

static void test_fill_refusals(void)
{
    struct v9x_rs690_fill fill;
    struct v9x_rs690_blt blt;

    base_fill(&fill);
    fill.width = 0ul;
    CHECK(v9x_rs690_build_fill(&fill, &blt) == V9X_STATUS_INVALID_ARGUMENT);

    base_fill(&fill);
    fill.left = 790ul;             /* 790 + 40 > 800 pixels */
    CHECK(v9x_rs690_build_fill(&fill, &blt) == V9X_STATUS_INVALID_ARGUMENT);

    base_fill(&fill);
    fill.top = 0x1000ul;           /* 4096 rows of 1600 bytes, then more */
    fill.pitch_bytes = 0x8000ul / 2ul;
    fill.height = 1ul;
    fill.target_offset = RS690_FB_BYTES - 0x1000ul;
    CHECK(v9x_rs690_build_fill(&fill, &blt) == V9X_STATUS_INVALID_ARGUMENT);

    base_fill(&fill);
    fill.target_offset = RS690_FB_BYTES;  /* past the frame buffer */
    CHECK(v9x_rs690_build_fill(&fill, &blt) == V9X_STATUS_INVALID_ARGUMENT);

    base_fill(&fill);
    fill.color = 0x12345ul;        /* wider than 16 bits */
    CHECK(v9x_rs690_build_fill(&fill, &blt) == V9X_STATUS_INVALID_ARGUMENT);

    base_fill(&fill);
    fill.bytes_per_pixel = 3ul;
    fill.pitch_bytes = 2400ul;
    CHECK(v9x_rs690_build_fill(&fill, &blt) == V9X_STATUS_UNSUPPORTED);

    base_fill(&fill);
    fill.pitch_bytes = 1608ul;     /* not a whole 64-byte unit */
    CHECK(v9x_rs690_build_fill(&fill, &blt) == V9X_STATUS_UNSUPPORTED);

    base_fill(&fill);
    fill.pitch_bytes = 256ul * 64ul;   /* 256 units, one too many */
    CHECK(v9x_rs690_build_fill(&fill, &blt) == V9X_STATUS_UNSUPPORTED);

    base_fill(&fill);
    fill.top = 0x2000ul;           /* past the 13-bit field */
    fill.pitch_bytes = 64ul;
    fill.left = 0ul;
    fill.width = 8ul;
    fill.height = 1ul;
    CHECK(v9x_rs690_build_fill(&fill, &blt) == V9X_STATUS_UNSUPPORTED);

    base_fill(&fill);
    fill.fb_gpu_base = 0x78000200ul;   /* a base the field cannot hold */
    CHECK(v9x_rs690_build_fill(&fill, &blt) == V9X_STATUS_INVALID_ARGUMENT);

    base_fill(&fill);
    fill.fb_gpu_base = 0xfc000000ul;   /* base + 128 MiB wraps 4 GiB */
    CHECK(v9x_rs690_build_fill(&fill, &blt) == V9X_STATUS_INVALID_ARGUMENT);

    CHECK(v9x_rs690_build_fill(0, &blt) == V9X_STATUS_INVALID_ARGUMENT);
}

/* A disjoint copy: forward in both directions, both pitch-offsets after
 * the state, the size last with the height in the high half. */
static void test_copy_disjoint(void)
{
    struct v9x_rs690_copy copy;
    struct v9x_rs690_blt blt;

    base_copy(&copy);
    CHECK(v9x_rs690_build_copy(&copy, &blt) == V9X_STATUS_OK);
    CHECK(blt.count == 13u);
    CHECK(blt_value(&blt, V9X_RS690_SRC_PITCH_OFFSET) ==
          ((25ul << 22) | RS690_FB_FIELD));
    CHECK(blt_value(&blt, V9X_RS690_DST_PITCH_OFFSET) ==
          ((25ul << 22) | RS690_FB_FIELD));
    CHECK(blt_value(&blt, V9X_RS690_DP_GUI_MASTER_CNTL) ==
          (V9X_RS690_GMC_SRC_PITCH_OFFSET_CNTL |
           V9X_RS690_GMC_DST_PITCH_OFFSET_CNTL |
           V9X_RS690_GMC_BRUSH_NONE |
           (V9X_RS690_DATATYPE_16BPP << 8) |
           V9X_RS690_GMC_SRC_DATATYPE_COLOR |
           (V9X_RS690_ROP3_S << 16) |
           V9X_RS690_DP_SRC_SOURCE_MEMORY |
           V9X_RS690_GMC_CLR_CMP_CNTL_DIS));
    CHECK(blt_value(&blt, V9X_RS690_DP_BRUSH_FRGD_CLR) == 0xfffffffful);
    CHECK(blt_value(&blt, V9X_RS690_DP_CNTL) ==
          (V9X_RS690_DST_X_LEFT_TO_RIGHT | V9X_RS690_DST_Y_TOP_TO_BOTTOM));
    CHECK(blt_value(&blt, V9X_RS690_SRC_Y_X) == ((100ul << 16) | 100ul));
    CHECK(blt_value(&blt, V9X_RS690_DST_Y_X) == ((200ul << 16) | 300ul));
    CHECK(blt.offsets[blt.count - 1u] == V9X_RS690_DST_HEIGHT_WIDTH);
    CHECK(blt.values[blt.count - 1u] == ((30ul << 16) | 40ul));
}

/* Overlap on one surface. Down: bottom to top from the last row. Right on
 * the same rows: right to left from the last column. Up-left: forward. */
static void test_copy_overlap(void)
{
    struct v9x_rs690_copy copy;
    struct v9x_rs690_blt blt;

    base_copy(&copy);
    copy.destination_left = 100ul;
    copy.destination_top = 104ul;
    CHECK(v9x_rs690_build_copy(&copy, &blt) == V9X_STATUS_OK);
    CHECK(blt_value(&blt, V9X_RS690_DP_CNTL) == V9X_RS690_DST_X_LEFT_TO_RIGHT);
    CHECK(blt_value(&blt, V9X_RS690_SRC_Y_X) == ((129ul << 16) | 100ul));
    CHECK(blt_value(&blt, V9X_RS690_DST_Y_X) == ((133ul << 16) | 100ul));

    base_copy(&copy);
    copy.destination_left = 104ul;
    copy.destination_top = 100ul;
    CHECK(v9x_rs690_build_copy(&copy, &blt) == V9X_STATUS_OK);
    CHECK(blt_value(&blt, V9X_RS690_DP_CNTL) == V9X_RS690_DST_Y_TOP_TO_BOTTOM);
    CHECK(blt_value(&blt, V9X_RS690_SRC_Y_X) == ((100ul << 16) | 139ul));
    CHECK(blt_value(&blt, V9X_RS690_DST_Y_X) == ((100ul << 16) | 143ul));

    /* Down and right together: both directions reversed. */
    base_copy(&copy);
    copy.destination_left = 104ul;
    copy.destination_top = 104ul;
    CHECK(v9x_rs690_build_copy(&copy, &blt) == V9X_STATUS_OK);
    CHECK(blt_value(&blt, V9X_RS690_DP_CNTL) == 0ul);
    CHECK(blt_value(&blt, V9X_RS690_SRC_Y_X) == ((129ul << 16) | 139ul));
    CHECK(blt_value(&blt, V9X_RS690_DST_Y_X) == ((133ul << 16) | 143ul));

    base_copy(&copy);
    copy.destination_left = 96ul;
    copy.destination_top = 96ul;
    CHECK(v9x_rs690_build_copy(&copy, &blt) == V9X_STATUS_OK);
    CHECK(blt_value(&blt, V9X_RS690_DP_CNTL) ==
          (V9X_RS690_DST_X_LEFT_TO_RIGHT | V9X_RS690_DST_Y_TOP_TO_BOTTOM));
}

/* Two surfaces at different offsets and pitches, and an 8 bpp copy. */
static void test_copy_between_surfaces(void)
{
    struct v9x_rs690_copy copy;
    struct v9x_rs690_blt blt;

    base_copy(&copy);
    copy.source_offset = 0x00200000ul;
    copy.source_pitch_bytes = 512ul;
    copy.source_left = 0ul;
    copy.source_top = 0ul;
    CHECK(v9x_rs690_build_copy(&copy, &blt) == V9X_STATUS_OK);
    CHECK(blt_value(&blt, V9X_RS690_SRC_PITCH_OFFSET) ==
          ((8ul << 22) | ((RS690_FB_BASE + 0x00200000ul) >> 10)));
    CHECK(blt_value(&blt, V9X_RS690_DP_CNTL) ==
          (V9X_RS690_DST_X_LEFT_TO_RIGHT | V9X_RS690_DST_Y_TOP_TO_BOTTOM));

    base_copy(&copy);
    copy.bytes_per_pixel = 1ul;
    copy.source_pitch_bytes = 832ul;      /* 800x600x8 as this BIOS sets it */
    copy.destination_pitch_bytes = 832ul;
    CHECK(v9x_rs690_build_copy(&copy, &blt) == V9X_STATUS_OK);
    CHECK(blt_value(&blt, V9X_RS690_DST_PITCH_OFFSET) ==
          ((13ul << 22) | RS690_FB_FIELD));
    CHECK((blt_value(&blt, V9X_RS690_DP_GUI_MASTER_CNTL) >> 8 & 0xful) ==
          V9X_RS690_DATATYPE_8BPP);

    base_copy(&copy);
    copy.source_left = 780ul;       /* past the source's width */
    CHECK(v9x_rs690_build_copy(&copy, &blt) == V9X_STATUS_INVALID_ARGUMENT);
}

/* The status helpers, against what the register probe read. */
static void test_status(void)
{
    CHECK(v9x_rs690_gui_idle(0x10000140ul) != 0ul);
    CHECK(v9x_rs690_gui_idle(0x90000140ul) == 0ul);
    CHECK(v9x_rs690_gui_idle(0x1000013ful) == 0ul);
    CHECK(v9x_rs690_fb_gpu_base(0x00007800ul) == RS690_FB_BASE);
    CHECK(v9x_rs690_fb_gpu_base(0xffff7800ul) == RS690_FB_BASE);
    CHECK(v9x_rs690_cp_off(0x00000000ul) != 0ul);
    CHECK(v9x_rs690_cp_off(0x40000000ul) == 0ul);
    CHECK(v9x_rs690_cp_off(0x0ffffffful) != 0ul);
}

unsigned int v9x_run_rs690_2d_tests(void)
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
