#include <stdio.h>
#include <string.h>

#include "velocity9x/sis6326_engine.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

/* The value a builder wrote to one register, or 0xdeadbeef if none. */
static v9x_u32 blt_value(const struct v9x_sis_blt *blt, v9x_u32 offset)
{
    v9x_u32 index;

    for (index = 0u; index < blt->count; ++index) {
        if (blt->offsets[index] == offset) {
            return blt->values[index];
        }
    }
    return 0xdeadbeeful;
}

static void base_fill(struct v9x_sis_fill *fill)
{
    memset(fill, 0, sizeof(*fill));
    fill->vram_bytes = 4194304ul;
    fill->target_offset = 0ul;
    fill->pitch_bytes = 1024ul;
    fill->bytes_per_pixel = 1ul;
    fill->left = 4ul;
    fill->top = 458ul;
    fill->width = 50ul;
    fill->height = 18ul;
    fill->color = 7ul;
}

static void base_copy(struct v9x_sis_copy *copy)
{
    memset(copy, 0, sizeof(*copy));
    copy->vram_bytes = 4194304ul;
    copy->source_offset = 0ul;
    copy->source_pitch_bytes = 1280ul;
    copy->destination_offset = 0ul;
    copy->destination_pitch_bytes = 1280ul;
    copy->bytes_per_pixel = 2ul;
    copy->source_left = 10ul;
    copy->source_top = 20ul;
    copy->destination_left = 10ul;
    copy->destination_top = 20ul;
    copy->width = 30ul;
    copy->height = 5ul;
}

/*
 * SiS's own driver filled the Start button face this way on both measured
 * boards (docs\decisions\2026-10-04-sis6326-first-survey.md): 50x18 at
 * (4, 458), pitch 1024, palette index 7. The builder must produce the same
 * destination, size, pitch, foreground and command words.
 */
static void test_fill_matches_stock_start_button(void)
{
    struct v9x_sis_fill fill;
    struct v9x_sis_blt blt;

    base_fill(&fill);
    CHECK(v9x_sis_build_fill(&fill, &blt) == V9X_STATUS_OK);
    CHECK(blt_value(&blt, V9X_SIS_2D_DST_ADDR) == 0x00072804ul);
    CHECK(blt_value(&blt, V9X_SIS_2D_SIZE) == 0x00110031ul);
    CHECK(blt_value(&blt, V9X_SIS_2D_PITCH) == 0x04000400ul);
    CHECK(blt_value(&blt, V9X_SIS_2D_FG) == 0xcc070707ul);
    CHECK(blt_value(&blt, V9X_SIS_2D_BG) == 0x00070707ul);
    CHECK(blt.command == 0x0034u);
    CHECK(blt.count <= V9X_SIS_BLT_DWORDS);
}

static void test_fill_16bpp(void)
{
    struct v9x_sis_fill fill;
    struct v9x_sis_blt blt;

    base_fill(&fill);
    fill.pitch_bytes = 1280ul;
    fill.bytes_per_pixel = 2ul;
    fill.left = 10ul;
    fill.top = 20ul;
    fill.width = 30ul;
    fill.height = 5ul;
    fill.color = 0x0000f800ul;
    fill.target_offset = 0x00100000ul;
    CHECK(v9x_sis_build_fill(&fill, &blt) == V9X_STATUS_OK);
    CHECK(blt_value(&blt, V9X_SIS_2D_DST_ADDR) ==
          0x00100000ul + 20ul * 1280ul + 10ul * 2ul);
    CHECK(blt_value(&blt, V9X_SIS_2D_SIZE) == ((4ul << 16) | 59ul));
    CHECK(blt_value(&blt, V9X_SIS_2D_PITCH) == ((1280ul << 16) | 1280ul));
    CHECK(blt_value(&blt, V9X_SIS_2D_FG) == 0xcc00f800ul);
    CHECK(blt_value(&blt, V9X_SIS_2D_BG) == 0x0000f800ul);
}

static void test_fill_refusals(void)
{
    struct v9x_sis_fill fill;
    struct v9x_sis_blt blt;

    CHECK(v9x_sis_build_fill(0, &blt) == V9X_STATUS_INVALID_ARGUMENT);
    base_fill(&fill);
    CHECK(v9x_sis_build_fill(&fill, 0) == V9X_STATUS_INVALID_ARGUMENT);

    /* 24 bpp: the datasheet allows no register-sourced BitBlt there. */
    base_fill(&fill);
    fill.bytes_per_pixel = 3ul;
    fill.pitch_bytes = 1920ul;
    CHECK(v9x_sis_build_fill(&fill, &blt) == V9X_STATUS_UNSUPPORTED);

    base_fill(&fill);
    fill.width = 0ul;
    CHECK(v9x_sis_build_fill(&fill, &blt) == V9X_STATUS_INVALID_ARGUMENT);
    base_fill(&fill);
    fill.height = 0ul;
    CHECK(v9x_sis_build_fill(&fill, &blt) == V9X_STATUS_INVALID_ARGUMENT);

    /* Wider than the pitch holds. */
    base_fill(&fill);
    fill.left = 1000ul;
    fill.width = 25ul;
    CHECK(v9x_sis_build_fill(&fill, &blt) == V9X_STATUS_INVALID_ARGUMENT);

    /* Past the end of VRAM. */
    base_fill(&fill);
    fill.vram_bytes = 458ul * 1024ul;
    CHECK(v9x_sis_build_fill(&fill, &blt) == V9X_STATUS_INVALID_ARGUMENT);

    /* The 12-bit pitch field. */
    base_fill(&fill);
    fill.pitch_bytes = 4096ul;
    fill.top = 0ul;
    CHECK(v9x_sis_build_fill(&fill, &blt) == V9X_STATUS_UNSUPPORTED);

    /* The 22-bit address field, even with more VRAM claimed. */
    base_fill(&fill);
    fill.vram_bytes = 8388608ul;
    fill.target_offset = 0x00400000ul;
    CHECK(v9x_sis_build_fill(&fill, &blt) == V9X_STATUS_UNSUPPORTED);

    /* The 12-bit height field holds 4096 at most. */
    base_fill(&fill);
    fill.top = 0ul;
    fill.left = 0ul;
    fill.width = 1ul;
    fill.height = 4097ul;
    fill.pitch_bytes = 16ul;
    CHECK(v9x_sis_build_fill(&fill, &blt) == V9X_STATUS_UNSUPPORTED);
}

/* Different surfaces never overlap: forward in both axes. */
static void test_copy_forward(void)
{
    struct v9x_sis_copy copy;
    struct v9x_sis_blt blt;

    base_copy(&copy);
    copy.destination_offset = 0x00200000ul;
    copy.destination_pitch_bytes = 2048ul;
    copy.destination_left = 3ul;
    copy.destination_top = 4ul;
    CHECK(v9x_sis_build_copy(&copy, &blt) == V9X_STATUS_OK);
    CHECK(blt_value(&blt, V9X_SIS_2D_SRC_ADDR) == 20ul * 1280ul + 20ul);
    CHECK(blt_value(&blt, V9X_SIS_2D_DST_ADDR) ==
          0x00200000ul + 4ul * 2048ul + 6ul);
    CHECK(blt_value(&blt, V9X_SIS_2D_PITCH) == ((2048ul << 16) | 1280ul));
    CHECK(blt_value(&blt, V9X_SIS_2D_SIZE) == ((4ul << 16) | 59ul));
    CHECK(blt_value(&blt, V9X_SIS_2D_FG) == 0xcc000000ul);
    CHECK(blt.command == (V9X_SIS_CMD0_SOURCE_VRAM |
                          V9X_SIS_CMD0_X_INCREASE |
                          V9X_SIS_CMD0_Y_INCREASE));
}

/* Same surface, destination above: forward is safe. */
static void test_copy_up_is_forward(void)
{
    struct v9x_sis_copy copy;
    struct v9x_sis_blt blt;

    base_copy(&copy);
    copy.destination_top = 18ul;
    copy.destination_left = 40ul;
    CHECK(v9x_sis_build_copy(&copy, &blt) == V9X_STATUS_OK);
    CHECK(blt.command == 0x0032u);
}

/* Same surface, destination lower: bottom-up, starting on the last row. */
static void test_copy_down_is_bottom_up(void)
{
    struct v9x_sis_copy copy;
    struct v9x_sis_blt blt;

    base_copy(&copy);
    copy.destination_top = 22ul;
    copy.destination_left = 50ul;
    CHECK(v9x_sis_build_copy(&copy, &blt) == V9X_STATUS_OK);
    CHECK(blt.command == (V9X_SIS_CMD0_SOURCE_VRAM |
                          V9X_SIS_CMD0_X_INCREASE));
    CHECK(blt_value(&blt, V9X_SIS_2D_SRC_ADDR) == 24ul * 1280ul + 20ul);
    CHECK(blt_value(&blt, V9X_SIS_2D_DST_ADDR) == 26ul * 1280ul + 100ul);
}

/*
 * Same row, destination to the right: right-to-left, starting on the last
 * byte of the last pixel, as xf86-video-sis does.
 */
static void test_copy_right_is_right_to_left(void)
{
    struct v9x_sis_copy copy;
    struct v9x_sis_blt blt;

    base_copy(&copy);
    copy.destination_left = 12ul;
    CHECK(v9x_sis_build_copy(&copy, &blt) == V9X_STATUS_OK);
    CHECK(blt.command == (V9X_SIS_CMD0_SOURCE_VRAM |
                          V9X_SIS_CMD0_Y_INCREASE));
    CHECK(blt_value(&blt, V9X_SIS_2D_SRC_ADDR) ==
          20ul * 1280ul + (10ul + 30ul) * 2ul - 1ul);
    CHECK(blt_value(&blt, V9X_SIS_2D_DST_ADDR) ==
          20ul * 1280ul + (12ul + 30ul) * 2ul - 1ul);
}

static void test_copy_refusals(void)
{
    struct v9x_sis_copy copy;
    struct v9x_sis_blt blt;

    CHECK(v9x_sis_build_copy(0, &blt) == V9X_STATUS_INVALID_ARGUMENT);
    base_copy(&copy);
    copy.width = 0ul;
    CHECK(v9x_sis_build_copy(&copy, &blt) == V9X_STATUS_INVALID_ARGUMENT);
    base_copy(&copy);
    copy.source_top = 4000ul;
    CHECK(v9x_sis_build_copy(&copy, &blt) == V9X_STATUS_INVALID_ARGUMENT);
    base_copy(&copy);
    copy.destination_left = 630ul;
    CHECK(v9x_sis_build_copy(&copy, &blt) == V9X_STATUS_INVALID_ARGUMENT);
    base_copy(&copy);
    copy.bytes_per_pixel = 4ul;
    CHECK(v9x_sis_build_copy(&copy, &blt) == V9X_STATUS_UNSUPPORTED);

    /* 24 bpp copies are allowed: source/destination BitBlt is. */
    base_copy(&copy);
    copy.bytes_per_pixel = 3ul;
    copy.source_pitch_bytes = 1920ul;
    copy.destination_pitch_bytes = 1920ul;
    CHECK(v9x_sis_build_copy(&copy, &blt) == V9X_STATUS_OK);
}

static void test_status(void)
{
    /* The idle word SiS's driver left: 80340FC0h, queue empty, not busy. */
    CHECK(v9x_sis_status_busy(0x80340fc0ul) == 0ul);
    CHECK(v9x_sis_status_busy(0x40000000ul) != 0ul);
}

unsigned int v9x_run_sis6326_engine_tests(void)
{
    test_fill_matches_stock_start_button();
    test_fill_16bpp();
    test_fill_refusals();
    test_copy_forward();
    test_copy_up_is_forward();
    test_copy_down_is_bottom_up();
    test_copy_right_is_right_to_left();
    test_copy_refusals();
    test_status();
    return failures;
}
