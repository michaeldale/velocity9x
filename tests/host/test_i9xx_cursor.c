#include <stdio.h>
#include <string.h>

#include "velocity9x/i9xx_cursor.h"

static unsigned int failures = 0u;

#define CHECK(expression) do { \
    if (!(expression)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #expression); \
        ++failures; \
    } \
} while (0)

/* A 32x32 shape at Windows' 4 bytes a row, all transparent to start. */
#define TEST_WIDTH_BYTES 4u
#define TEST_SIDE        32u

static v9x_u8 test_and[TEST_WIDTH_BYTES * TEST_SIDE];
static v9x_u8 test_xor[TEST_WIDTH_BYTES * TEST_SIDE];
static v9x_u32 test_pixels[V9X_I9XX_CURSOR_SIZE];

static void test_shape(struct v9x_i9xx_cursor_shape *shape)
{
    memset(test_and, 0xff, sizeof(test_and));
    memset(test_xor, 0x00, sizeof(test_xor));
    shape->hot_x = 0u;
    shape->hot_y = 0u;
    shape->width = TEST_SIDE;
    shape->height = TEST_SIDE;
    shape->width_bytes = TEST_WIDTH_BYTES;
    shape->planes = 1u;
    shape->bits_pixel = 1u;
    shape->and_mask = test_and;
    shape->xor_mask = test_xor;
}

/* Pixel (x, y) as AND and XOR bits, most significant bit leftmost. */
static void test_set(v9x_u32 x, v9x_u32 y, int and_bit, int xor_bit)
{
    v9x_u32 at = y * TEST_WIDTH_BYTES + (x >> 3);
    v9x_u8 bit = (v9x_u8)(0x80u >> (unsigned)(x & 7ul));

    test_and[at] = (v9x_u8)(and_bit ? (test_and[at] | bit) :
                                      (test_and[at] & (v9x_u8)~bit));
    test_xor[at] = (v9x_u8)(xor_bit ? (test_xor[at] | bit) :
                                      (test_xor[at] & (v9x_u8)~bit));
}

static void test_check(void)
{
    struct v9x_i9xx_cursor_shape shape;

    test_shape(&shape);
    test_set(0ul, 0ul, 0, 0);
    test_set(1ul, 0ul, 0, 1);
    CHECK(v9x_i9xx_cursor_check(&shape) == V9X_STATUS_OK);

    /* One inverting pixel is enough to leave the shape to software. */
    test_set(31ul, 31ul, 1, 1);
    CHECK(v9x_i9xx_cursor_check(&shape) == V9X_STATUS_UNSUPPORTED);
    test_set(31ul, 31ul, 1, 0);
    CHECK(v9x_i9xx_cursor_check(&shape) == V9X_STATUS_OK);

    /* Past the width, padding bits are not pixels. */
    shape.width = 31u;
    test_set(31ul, 5ul, 1, 1);
    CHECK(v9x_i9xx_cursor_check(&shape) == V9X_STATUS_OK);
    shape.width = TEST_SIDE;
    CHECK(v9x_i9xx_cursor_check(&shape) == V9X_STATUS_UNSUPPORTED);
    test_set(31ul, 5ul, 1, 0);

    shape.bits_pixel = 8u;
    CHECK(v9x_i9xx_cursor_check(&shape) == V9X_STATUS_UNSUPPORTED);
    shape.bits_pixel = 1u;
    shape.planes = 4u;
    CHECK(v9x_i9xx_cursor_check(&shape) == V9X_STATUS_UNSUPPORTED);
    shape.planes = 1u;

    shape.width = 65u;
    shape.width_bytes = 10u;
    CHECK(v9x_i9xx_cursor_check(&shape) == V9X_STATUS_UNSUPPORTED);
    shape.width = 64u;
    shape.width_bytes = 8u;
    shape.height = 65u;
    CHECK(v9x_i9xx_cursor_check(&shape) == V9X_STATUS_UNSUPPORTED);
    /* 64 rows of 10 bytes do not fit the copy the driver keeps. */
    shape.height = 64u;
    shape.width_bytes = 10u;
    CHECK(v9x_i9xx_cursor_check(&shape) == V9X_STATUS_UNSUPPORTED);

    test_shape(&shape);
    shape.width_bytes = 3u;
    CHECK(v9x_i9xx_cursor_check(&shape) == V9X_STATUS_INVALID_ARGUMENT);
    test_shape(&shape);
    shape.height = 0u;
    CHECK(v9x_i9xx_cursor_check(&shape) == V9X_STATUS_INVALID_ARGUMENT);
    test_shape(&shape);
    shape.xor_mask = 0;
    CHECK(v9x_i9xx_cursor_check(&shape) == V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_i9xx_cursor_check(0) == V9X_STATUS_INVALID_ARGUMENT);
}

static void test_rows(void)
{
    struct v9x_i9xx_cursor_shape shape;
    v9x_u32 x;

    test_shape(&shape);
    test_set(0ul, 0ul, 0, 0);
    test_set(1ul, 0ul, 0, 1);
    test_set(9ul, 0ul, 0, 1);
    test_set(31ul, 3ul, 0, 0);

    v9x_i9xx_cursor_row(&shape, 0ul, test_pixels);
    CHECK(test_pixels[0] == V9X_I9XX_CURSOR_BLACK);
    CHECK(test_pixels[1] == V9X_I9XX_CURSOR_WHITE);
    CHECK(test_pixels[2] == V9X_I9XX_CURSOR_TRANSPARENT);
    CHECK(test_pixels[8] == V9X_I9XX_CURSOR_TRANSPARENT);
    CHECK(test_pixels[9] == V9X_I9XX_CURSOR_WHITE);
    for (x = 32ul; x < V9X_I9XX_CURSOR_SIZE; ++x) {
        CHECK(test_pixels[x] == V9X_I9XX_CURSOR_TRANSPARENT);
    }

    v9x_i9xx_cursor_row(&shape, 3ul, test_pixels);
    CHECK(test_pixels[30] == V9X_I9XX_CURSOR_TRANSPARENT);
    CHECK(test_pixels[31] == V9X_I9XX_CURSOR_BLACK);

    /* A width short of the mask: the padding is transparent whatever its
     * bits say. */
    shape.width = 31u;
    v9x_i9xx_cursor_row(&shape, 3ul, test_pixels);
    CHECK(test_pixels[31] == V9X_I9XX_CURSOR_TRANSPARENT);

    /* Below the shape: transparent. */
    v9x_i9xx_cursor_row(&shape, 32ul, test_pixels);
    for (x = 0ul; x < V9X_I9XX_CURSOR_SIZE; ++x) {
        CHECK(test_pixels[x] == V9X_I9XX_CURSOR_TRANSPARENT);
    }
    v9x_i9xx_cursor_row(&shape, 63ul, test_pixels);
    CHECK(test_pixels[0] == V9X_I9XX_CURSOR_TRANSPARENT);
}

static void test_registers(void)
{
    CHECK(v9x_i9xx_cursor_position(5, 7) == 0x00070005ul);
    CHECK(v9x_i9xx_cursor_position(-3, 4) == 0x00048003ul);
    CHECK(v9x_i9xx_cursor_position(2, -1) == 0x80010002ul);
    CHECK(v9x_i9xx_cursor_position(-31, -31) == 0x801f801ful);
    CHECK(v9x_i9xx_cursor_position(0, 0) == 0ul);

    CHECK(v9x_i9xx_cursor_control(0ul) == 0x00000027ul);
    CHECK(v9x_i9xx_cursor_control(1ul) == 0x10000027ul);

    /* Plane A on pipe B, as a VBIOS drives a netbook's panel. */
    CHECK(v9x_i9xx_cursor_pipes(0x81000000ul, 0ul) == 2u);
    CHECK(v9x_i9xx_cursor_pipes(0x80000000ul, 0x81000000ul) == 3u);
    CHECK(v9x_i9xx_cursor_pipes(0x01000000ul, 0x80000000ul) == 1u);
    CHECK(v9x_i9xx_cursor_pipes(0x81000000ul, 0x81000000ul) == 2u);
    CHECK(v9x_i9xx_cursor_pipes(0ul, 0ul) == 0u);
    /* A pipe select Gen3 does not have. */
    CHECK(v9x_i9xx_cursor_pipes(0x83000000ul, 0ul) == 0u);
}

static void test_base(void)
{
    v9x_u32 entries[V9X_I9XX_CURSOR_PAGES] = {
        0x7f8fc001ul, 0x7f8fd001ul, 0x7f8fe001ul, 0x7f8ff001ul
    };
    v9x_u32 base = 1ul;

    CHECK(v9x_i9xx_cursor_base(V9X_PCI_DEVICE_GMA950_945GSE, 0x007ac000ul,
                               entries, &base) == V9X_STATUS_OK);
    CHECK(base == 0x7f8fc000ul);
    CHECK(v9x_i9xx_cursor_base(V9X_PCI_DEVICE_GMA900_915GM, 0x007ac000ul,
                               entries, &base) == V9X_STATUS_OK);

    /* A gap, a missing page, a page above 4 GiB. */
    entries[2] = 0x7f900001ul;
    CHECK(v9x_i9xx_cursor_base(V9X_PCI_DEVICE_GMA950_945GSE, 0x007ac000ul,
                               entries, &base) ==
          V9X_STATUS_INSUFFICIENT_MEMORY);
    CHECK(base == 0ul);
    entries[2] = 0x7f8fe000ul;
    CHECK(v9x_i9xx_cursor_base(V9X_PCI_DEVICE_GMA950_945GSE, 0x007ac000ul,
                               entries, &base) ==
          V9X_STATUS_INSUFFICIENT_MEMORY);
    entries[2] = 0x7f8fe011ul;
    CHECK(v9x_i9xx_cursor_base(V9X_PCI_DEVICE_GMA950_945GSE, 0x007ac000ul,
                               entries, &base) ==
          V9X_STATUS_INSUFFICIENT_MEMORY);
    entries[2] = 0x7f8fe001ul;

    /* Pineview and G33: the graphics address, entries unread. */
    CHECK(v9x_i9xx_cursor_base(V9X_PCI_DEVICE_GMA3150_PNV, 0x007ac000ul, 0,
                               &base) == V9X_STATUS_OK);
    CHECK(base == 0x007ac000ul);
    CHECK(v9x_i9xx_cursor_base(V9X_PCI_DEVICE_GMA3100_G33, 0x007ac000ul, 0,
                               &base) == V9X_STATUS_OK);

    CHECK(v9x_i9xx_cursor_base(0x1234u, 0x007ac000ul, entries, &base) ==
          V9X_STATUS_UNSUPPORTED);
    CHECK(v9x_i9xx_cursor_base(V9X_PCI_DEVICE_GMA950_945GSE, 0x007ac100ul,
                               entries, &base) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_i9xx_cursor_base(V9X_PCI_DEVICE_GMA950_945GSE, 0x007ac000ul,
                               0, &base) == V9X_STATUS_INVALID_ARGUMENT);
}

/* The image is the reserve's last 16 KiB, whole pages, page aligned; and
 * the sandbox ends below it. The constants are checked at compile time. */
typedef char test_cursor_image_is_the_mode[
    V9X_I9XX_CURSOR_IMAGE_BYTES ==
            V9X_I9XX_CURSOR_SIZE * V9X_I9XX_CURSOR_PITCH ? 1 : -1];
typedef char test_cursor_image_is_pages[
    V9X_I9XX_CURSOR_IMAGE_BYTES ==
            V9X_I9XX_CURSOR_PAGES * V9X_I9XX_GTT_PAGE_BYTES ? 1 : -1];
typedef char test_cursor_image_ends_the_reserve[
    V9X_I9XX_CURSOR_RESERVE_OFFSET + V9X_I9XX_CURSOR_IMAGE_BYTES ==
            V9X_I9XX_GTT_RESERVE_BYTES ? 1 : -1];
typedef char test_cursor_image_is_aligned[
    (V9X_I9XX_CURSOR_RESERVE_OFFSET & (V9X_I9XX_GTT_PAGE_BYTES - 1ul)) == 0ul
        ? 1 : -1];

static void test_layout(void)
{
    struct v9x_i9xx_sandbox_layout layout;

    CHECK(v9x_i9xx_sandbox_calculate(0x007b0000ul, 0x7f800000ul, &layout) ==
          V9X_STATUS_OK);
    CHECK(layout.depth_guard_offset + V9X_I9XX_SANDBOX_PAGE_BYTES <=
          layout.reserve_offset + V9X_I9XX_CURSOR_RESERVE_OFFSET);
}

unsigned int v9x_run_i9xx_cursor_tests(void)
{
    test_check();
    test_rows();
    test_registers();
    test_base();
    test_layout();
    return failures;
}
