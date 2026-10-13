#include <stdio.h>
#include <string.h>

#include "velocity9x/tdfx_2d.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

/* The value a builder wrote to one register, or 0xdeadbeef if none. */
static v9x_u32 blt_value(const struct v9x_tdfx_blt *blt, v9x_u32 offset)
{
    v9x_u32 index;

    for (index = 0u; index < blt->count; ++index) {
        if (blt->offsets[index] == offset) {
            return blt->values[index];
        }
    }
    return 0xdeadbeeful;
}

static void base_fill(struct v9x_tdfx_fill *fill)
{
    memset(fill, 0, sizeof(*fill));
    fill->vram_bytes = 33550336ul;
    fill->target_offset = 0ul;
    fill->pitch_bytes = 1280ul;
    fill->bytes_per_pixel = 2ul;
    fill->left = 10ul;
    fill->top = 20ul;
    fill->width = 40ul;
    fill->height = 30ul;
    fill->color = 0x1234ul;
    fill->rop = V9X_TDFX_ROP_SRCCOPY;
}

static void base_copy(struct v9x_tdfx_copy *copy)
{
    memset(copy, 0, sizeof(*copy));
    copy->vram_bytes = 33550336ul;
    copy->source_offset = 0ul;
    copy->source_pitch_bytes = 1280ul;
    copy->destination_offset = 0ul;
    copy->destination_pitch_bytes = 1280ul;
    copy->bytes_per_pixel = 2ul;
    copy->source_left = 100ul;
    copy->source_top = 100ul;
    copy->destination_left = 300ul;
    copy->destination_top = 200ul;
    copy->width = 40ul;
    copy->height = 30ul;
}

/*
 * A 16-bpp fill on a 640x480 desktop: every register in order, the command
 * last. Values from the Napalm section 7 encodings
 * (docs\specifications\vsa100-registers.md section 6).
 */
static void test_fill_writes(void)
{
    struct v9x_tdfx_fill fill;
    struct v9x_tdfx_blt blt;

    base_fill(&fill);
    CHECK(v9x_tdfx_build_fill(&fill, &blt) == V9X_STATUS_OK);
    CHECK(blt.count == 9u);
    CHECK(blt.offsets[0] == V9X_TDFX_2D_CLIP0_MIN && blt.values[0] == 0ul);
    CHECK(blt_value(&blt, V9X_TDFX_2D_CLIP0_MAX) == V9X_TDFX_CLIP_OPEN_MAX);
    CHECK(blt_value(&blt, V9X_TDFX_2D_COMMAND_EXTRA) == 0ul);
    CHECK(blt_value(&blt, V9X_TDFX_2D_DST_BASE) == 0ul);
    CHECK(blt_value(&blt, V9X_TDFX_2D_DST_FORMAT) == ((3ul << 16) | 1280ul));
    CHECK(blt_value(&blt, V9X_TDFX_2D_COLOR_FORE) == 0x1234ul);
    CHECK(blt_value(&blt, V9X_TDFX_2D_DST_SIZE) == ((30ul << 16) | 40ul));
    CHECK(blt_value(&blt, V9X_TDFX_2D_DST_XY) == ((20ul << 16) | 10ul));
    CHECK(blt.offsets[blt.count - 1u] == V9X_TDFX_2D_COMMAND);
    CHECK(blt.values[blt.count - 1u] == 0xcc000105ul);

    /* 8 and 32 bpp format codes. */
    fill.bytes_per_pixel = 1ul;
    fill.pitch_bytes = 640ul;
    fill.color = 0x7ful;
    CHECK(v9x_tdfx_build_fill(&fill, &blt) == V9X_STATUS_OK);
    CHECK(blt_value(&blt, V9X_TDFX_2D_DST_FORMAT) == ((1ul << 16) | 640ul));
    fill.bytes_per_pixel = 4ul;
    fill.pitch_bytes = 2560ul;
    fill.color = 0x00abcdeful;
    CHECK(v9x_tdfx_build_fill(&fill, &blt) == V9X_STATUS_OK);
    CHECK(blt_value(&blt, V9X_TDFX_2D_DST_FORMAT) == ((5ul << 16) | 2560ul));
}

/* At or below 16 pixels in either dimension, a NOP goes first. */
static void test_small_blt_nop(void)
{
    struct v9x_tdfx_fill fill;
    struct v9x_tdfx_blt blt;

    CHECK(v9x_tdfx_small_blt(16ul, 100ul) != 0ul);
    CHECK(v9x_tdfx_small_blt(100ul, 16ul) != 0ul);
    CHECK(v9x_tdfx_small_blt(17ul, 17ul) == 0ul);

    base_fill(&fill);
    fill.height = 16ul;
    CHECK(v9x_tdfx_build_fill(&fill, &blt) == V9X_STATUS_OK);
    CHECK(blt.count == 10u);
    CHECK(blt.offsets[0] == V9X_TDFX_2D_COMMAND);
    CHECK(blt.values[0] == (V9X_TDFX_CMD_NOP | V9X_TDFX_CMD_INITIATE));
    CHECK(blt.values[blt.count - 1u] == 0xcc000105ul);
}

static void test_fill_refusals(void)
{
    struct v9x_tdfx_fill fill;
    struct v9x_tdfx_blt blt;

    base_fill(&fill);
    fill.bytes_per_pixel = 3ul;
    CHECK(v9x_tdfx_build_fill(&fill, &blt) == V9X_STATUS_UNSUPPORTED);

    base_fill(&fill);
    fill.pitch_bytes = 0x4000ul;
    CHECK(v9x_tdfx_build_fill(&fill, &blt) == V9X_STATUS_UNSUPPORTED);

    base_fill(&fill);
    fill.width = 0ul;
    CHECK(v9x_tdfx_build_fill(&fill, &blt) == V9X_STATUS_INVALID_ARGUMENT);

    /* Wider than the pitch. */
    base_fill(&fill);
    fill.left = 630ul;
    fill.width = 20ul;
    CHECK(v9x_tdfx_build_fill(&fill, &blt) == V9X_STATUS_INVALID_ARGUMENT);

    /* Past the end of memory. */
    base_fill(&fill);
    fill.target_offset = 33550336ul - 1280ul;
    CHECK(v9x_tdfx_build_fill(&fill, &blt) == V9X_STATUS_INVALID_ARGUMENT);

    /* A colour wider than the pixel. */
    base_fill(&fill);
    fill.color = 0x10000ul;
    CHECK(v9x_tdfx_build_fill(&fill, &blt) == V9X_STATUS_INVALID_ARGUMENT);
}

static void test_copy_directions(void)
{
    struct v9x_tdfx_copy copy;
    struct v9x_tdfx_blt blt;

    /* Disjoint on one surface: left to right, top to bottom. */
    base_copy(&copy);
    CHECK(v9x_tdfx_build_copy(&copy, &blt) == V9X_STATUS_OK);
    CHECK(blt.count == 11u);
    CHECK(blt_value(&blt, V9X_TDFX_2D_SRC_BASE) == 0ul);
    CHECK(blt_value(&blt, V9X_TDFX_2D_SRC_FORMAT) == ((3ul << 16) | 1280ul));
    CHECK(blt_value(&blt, V9X_TDFX_2D_SRC_XY) == ((100ul << 16) | 100ul));
    CHECK(blt_value(&blt, V9X_TDFX_2D_DST_XY) == ((200ul << 16) | 300ul));
    CHECK(blt_value(&blt, V9X_TDFX_2D_DST_SIZE) == ((30ul << 16) | 40ul));
    CHECK(blt.offsets[blt.count - 1u] == V9X_TDFX_2D_COMMAND);
    CHECK(blt.values[blt.count - 1u] == 0xcc000101ul);

    /* Overlapping, destination to the right on the same rows: right to
     * left, both corners at the right edge. */
    base_copy(&copy);
    copy.destination_left = 110ul;
    copy.destination_top = 100ul;
    CHECK(v9x_tdfx_build_copy(&copy, &blt) == V9X_STATUS_OK);
    CHECK(blt_value(&blt, V9X_TDFX_2D_SRC_XY) == ((100ul << 16) | 139ul));
    CHECK(blt_value(&blt, V9X_TDFX_2D_DST_XY) == ((100ul << 16) | 149ul));
    CHECK(blt.values[blt.count - 1u] ==
          (0xcc000101ul | V9X_TDFX_CMD_X_RIGHT_TO_LEFT));

    /* Overlapping, destination below: bottom to top, both corners on the
     * bottom row. */
    base_copy(&copy);
    copy.destination_left = 100ul;
    copy.destination_top = 110ul;
    CHECK(v9x_tdfx_build_copy(&copy, &blt) == V9X_STATUS_OK);
    CHECK(blt_value(&blt, V9X_TDFX_2D_SRC_XY) == ((129ul << 16) | 100ul));
    CHECK(blt_value(&blt, V9X_TDFX_2D_DST_XY) == ((139ul << 16) | 100ul));
    CHECK(blt.values[blt.count - 1u] ==
          (0xcc000101ul | V9X_TDFX_CMD_Y_BOTTOM_TO_TOP));

    /* Overlapping, destination up and left: the forward order is safe. */
    base_copy(&copy);
    copy.destination_left = 90ul;
    copy.destination_top = 90ul;
    CHECK(v9x_tdfx_build_copy(&copy, &blt) == V9X_STATUS_OK);
    CHECK(blt.values[blt.count - 1u] == 0xcc000101ul);

    /* Different surfaces never need a direction. */
    base_copy(&copy);
    copy.destination_offset = 0x00800000ul;
    copy.destination_left = 110ul;
    copy.destination_top = 110ul;
    CHECK(v9x_tdfx_build_copy(&copy, &blt) == V9X_STATUS_OK);
    CHECK(blt_value(&blt, V9X_TDFX_2D_DST_BASE) == 0x00800000ul);
    CHECK(blt.values[blt.count - 1u] == 0xcc000101ul);
}

static void test_copy_refusals(void)
{
    struct v9x_tdfx_copy copy;
    struct v9x_tdfx_blt blt;

    base_copy(&copy);
    copy.bytes_per_pixel = 3ul;
    CHECK(v9x_tdfx_build_copy(&copy, &blt) == V9X_STATUS_UNSUPPORTED);

    base_copy(&copy);
    copy.source_left = 620ul;
    CHECK(v9x_tdfx_build_copy(&copy, &blt) == V9X_STATUS_INVALID_ARGUMENT);

    base_copy(&copy);
    copy.source_offset = 33550336ul;
    CHECK(v9x_tdfx_build_copy(&copy, &blt) == V9X_STATUS_INVALID_ARGUMENT);
}

static void test_status_busy(void)
{
    CHECK(v9x_tdfx_status_busy(0x0000021ful) != 0ul);
    CHECK(v9x_tdfx_status_busy(0x0000005ful) == 0ul);
}

unsigned int v9x_run_tdfx_2d_tests(void)
{
    failures = 0u;
    test_fill_writes();
    test_small_blt_nop();
    test_fill_refusals();
    test_copy_directions();
    test_copy_refusals();
    test_status_busy();
    return failures;
}
