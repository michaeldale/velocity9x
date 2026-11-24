#include <stdio.h>
#include <string.h>

#include "velocity9x/i9xx_gdi.h"

static unsigned int failures = 0u;

#define CHECK(expression) do { \
    if (!(expression)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #expression); \
        ++failures; \
    } \
} while (0)

/*
 * A small screen: 64 x 64 at 16 bpp, at offset 0x1000 of a 64 KiB video
 * memory, the breadcrumb past it. The simulator below runs the stream's
 * copies on it the way a blitter without scan-direction control may: in
 * raster order, and again in reverse raster order. A band that overlapped
 * itself, or read what an earlier band wrote, gives a different picture in
 * at least one of the two.
 */
#define TEST_WIDTH   64ul
#define TEST_HEIGHT  64ul
#define TEST_BPP     2ul
#define TEST_PITCH   (TEST_WIDTH * TEST_BPP)
#define TEST_BASE    0x1000ul
#define TEST_VRAM    0x10000ul
#define TEST_CRUMB   0x20100ul
#define TEST_BYTES   (TEST_PITCH * TEST_HEIGHT)

static unsigned char test_screen[TEST_BYTES];
static unsigned char test_expected[TEST_BYTES];
static unsigned char test_rows[TEST_BYTES];
static v9x_u32 test_stream[V9X_I9XX_GDI_STREAM_DWORDS];

static void test_paint(unsigned char *bytes)
{
    v9x_u32 at;

    for (at = 0ul; at < TEST_BYTES; ++at) {
        bytes[at] = (unsigned char)((at * 7ul + (at >> 7)) & 0xfful);
    }
}

/* What GDI asks for: the source rectangle as it was, at the destination. */
static void test_reference(const struct v9x_i9xx_gdi_copy *copy)
{
    v9x_u32 row;

    test_paint(test_expected);
    for (row = 0ul; row < copy->height; ++row) {
        memcpy(test_rows + row * TEST_PITCH,
               test_expected + (copy->source_y + row) * TEST_PITCH +
                   copy->source_x * TEST_BPP,
               copy->width * TEST_BPP);
    }
    for (row = 0ul; row < copy->height; ++row) {
        memcpy(test_expected + (copy->y + row) * TEST_PITCH +
                   copy->x * TEST_BPP,
               test_rows + row * TEST_PITCH, copy->width * TEST_BPP);
    }
}

/* One XY_SRC_COPY_BLT, pixel by pixel, forwards or backwards. */
static void test_run_copy(const v9x_u32 *packet, int backwards)
{
    v9x_u32 pitch = packet[1] & 0xfffful;
    v9x_u32 left = packet[2] & 0xfffful;
    v9x_u32 top = packet[2] >> 16;
    v9x_u32 right = packet[3] & 0xfffful;
    v9x_u32 bottom = packet[3] >> 16;
    v9x_u32 base = packet[4] - TEST_BASE;
    v9x_u32 source_left = packet[5] & 0xfffful;
    v9x_u32 source_top = packet[5] >> 16;
    v9x_u32 source_pitch = packet[6];
    v9x_u32 source_base = packet[7] - TEST_BASE;
    v9x_u32 count = (right - left) * (bottom - top);
    v9x_u32 step;

    for (step = 0ul; step < count; ++step) {
        v9x_u32 at = backwards ? count - 1ul - step : step;
        v9x_u32 dx = at % (right - left);
        v9x_u32 dy = at / (right - left);

        memcpy(test_screen + base + (top + dy) * pitch + (left + dx) * TEST_BPP,
               test_screen + source_base + (source_top + dy) * source_pitch +
                   (source_left + dx) * TEST_BPP,
               TEST_BPP);
    }
}

/* Run a stream's copies; anything but copies, flushes, the breadcrumb and
 * padding is a failure. */
static void test_run(const v9x_u32 *stream, v9x_u32 dwords, int backwards)
{
    v9x_u32 at = 0ul;

    test_paint(test_screen);
    while (at < dwords) {
        if (stream[at] == V9X_I9XX_XY_SRC_COPY_BLT) {
            test_run_copy(stream + at, backwards);
            at += V9X_I9XX_BLT_COPY_DWORDS;
        } else if (stream[at] == V9X_I9XX_XY_COLOR_BLT) {
            CHECK(stream[at + 4ul] == TEST_CRUMB);
            at += 6ul;
        } else {
            CHECK(stream[at] == V9X_I9XX_MI_FLUSH ||
                  stream[at] == V9X_I9XX_MI_NOOP);
            ++at;
        }
    }
}

static void test_copy_defaults(struct v9x_i9xx_gdi_copy *copy)
{
    copy->bytes_per_pixel = TEST_BPP;
    copy->vram_bytes = TEST_VRAM;
    copy->base = TEST_BASE;
    copy->pitch = TEST_PITCH;
}

/* Every move inside a window: build, then run both ways against memmove. */
static void test_every_move(void)
{
    static const v9x_u32 sizes[][2] = {
        { 1ul, 1ul }, { 5ul, 3ul }, { 16ul, 16ul }, { 40ul, 30ul },
        { 3ul, 40ul }, { 44ul, 2ul }
    };
    struct v9x_i9xx_gdi_copy copy;
    v9x_u32 size;
    v9x_s32 dx;
    v9x_s32 dy;
    v9x_u32 written;
    v9x_u32 bands;
    v9x_status status;
    unsigned int built = 0u;
    unsigned int declined = 0u;

    for (size = 0ul; size < sizeof(sizes) / sizeof(sizes[0]); ++size) {
        for (dy = -9; dy <= 9; ++dy) {
            for (dx = -9; dx <= 9; ++dx) {
                test_copy_defaults(&copy);
                copy.width = sizes[size][0];
                copy.height = sizes[size][1];
                copy.source_x = 10ul;
                copy.source_y = 9ul;
                copy.x = (v9x_u32)((v9x_s32)copy.source_x + dx);
                copy.y = (v9x_u32)((v9x_s32)copy.source_y + dy);
                status = v9x_i9xx_gdi_build_copy(
                    &copy, 0ul, TEST_CRUMB, 0x1234ul, test_stream,
                    V9X_I9XX_GDI_STREAM_DWORDS, &written, &bands);
                if (status == V9X_STATUS_UNSUPPORTED) {
                    ++declined;
                    continue;
                }
                CHECK(status == V9X_STATUS_OK);
                if (status != V9X_STATUS_OK) {
                    continue;
                }
                ++built;
                CHECK((written & 1ul) == 0ul);
                CHECK(written == ((1ul + bands * 9ul + 8ul + 1ul) & ~1ul));
                test_reference(&copy);
                test_run(test_stream, written, 0);
                CHECK(memcmp(test_screen, test_expected, TEST_BYTES) == 0);
                test_run(test_stream, written, 1);
                CHECK(memcmp(test_screen, test_expected, TEST_BYTES) == 0);
            }
        }
    }
    /* None of these needs more than 128 bands. */
    CHECK(built > 2000u);
    CHECK(declined == 0u);
}

static void test_band_counts(void)
{
    struct v9x_i9xx_gdi_copy copy;
    v9x_u32 count;

    test_copy_defaults(&copy);
    copy.width = 20ul;
    copy.height = 20ul;
    copy.source_x = 0ul;
    copy.source_y = 0ul;

    /* Apart: one band. */
    copy.x = 20ul;
    copy.y = 0ul;
    CHECK(v9x_i9xx_gdi_band_count(&copy, &count) == V9X_STATUS_OK);
    CHECK(count == 1ul);

    /* Onto itself: nothing to do. */
    copy.x = 0ul;
    CHECK(v9x_i9xx_gdi_band_count(&copy, &count) == V9X_STATUS_OK);
    CHECK(count == 0ul);

    /* Down by 7: bands of 7, 7 and 6 rows. */
    copy.y = 7ul;
    CHECK(v9x_i9xx_gdi_band_count(&copy, &count) == V9X_STATUS_OK);
    CHECK(count == 3ul);

    /* Right by 3 with no vertical move: seven columns of 3, one of 2. */
    copy.x = 3ul;
    copy.y = 0ul;
    CHECK(v9x_i9xx_gdi_band_count(&copy, &count) == V9X_STATUS_OK);
    CHECK(count == 7ul);

    /* Up by one row in a tall window 20 pixels wide: one stream holds
     * it at 128 rows, and past that bands of 20 pixels are the CPU's. */
    copy.x = 0ul;
    copy.y = 0ul;
    copy.source_y = 1ul;
    copy.height = 129ul;
    CHECK(v9x_i9xx_gdi_band_count(&copy, &count) == V9X_STATUS_UNSUPPORTED);
    copy.height = 128ul;
    CHECK(v9x_i9xx_gdi_band_count(&copy, &count) == V9X_STATUS_OK);
    CHECK(count == 128ul);

    /* Past one stream, bands of 256 pixels and more go to the blitter. */
    copy.height = 129ul;
    copy.width = V9X_I9XX_GDI_MIN_BAND_PIXELS - 1ul;
    CHECK(v9x_i9xx_gdi_band_count(&copy, &count) == V9X_STATUS_UNSUPPORTED);
    copy.width = V9X_I9XX_GDI_MIN_BAND_PIXELS;
    CHECK(v9x_i9xx_gdi_band_count(&copy, &count) == V9X_STATUS_OK);
    CHECK(count == 129ul);

    /* And at most 2,048 bands a copy. */
    copy.width = 800ul;
    copy.height = V9X_I9XX_GDI_MAX_COPY_BANDS;
    CHECK(v9x_i9xx_gdi_band_count(&copy, &count) == V9X_STATUS_OK);
    CHECK(count == V9X_I9XX_GDI_MAX_COPY_BANDS);
    copy.height = V9X_I9XX_GDI_MAX_COPY_BANDS + 1ul;
    CHECK(v9x_i9xx_gdi_band_count(&copy, &count) == V9X_STATUS_UNSUPPORTED);
    copy.height = 20ul;

    /* An extent the blitter's 15-bit fields cannot carry; it would also
     * take the 16-bit arithmetic past its operands. */
    copy.width = V9X_I9XX_BLT_FIELD_LIMIT;
    CHECK(v9x_i9xx_gdi_band_count(&copy, &count) == V9X_STATUS_UNSUPPORTED);
    copy.width = 20ul;
    copy.height = V9X_I9XX_BLT_FIELD_LIMIT;
    CHECK(v9x_i9xx_gdi_band_count(&copy, &count) == V9X_STATUS_UNSUPPORTED);
    copy.height = 20ul;

    /* Empty. */
    copy.width = 0ul;
    CHECK(v9x_i9xx_gdi_band_count(&copy, &count) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

static void test_build_refusals(void)
{
    struct v9x_i9xx_gdi_copy copy;
    v9x_u32 written = 1ul;

    test_copy_defaults(&copy);
    copy.width = 16ul;
    copy.height = 16ul;
    copy.source_x = 0ul;
    copy.source_y = 0ul;
    copy.x = 0ul;
    copy.y = 4ul;

    /* Too small a buffer for three bands. */
    CHECK(v9x_i9xx_gdi_build_copy(&copy, 0ul, TEST_CRUMB, 1ul, test_stream,
                                  30ul,
                                  &written, 0) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(written == 0ul);

    /* Past video memory. */
    copy.vram_bytes = TEST_BASE + TEST_PITCH * 8ul;
    CHECK(v9x_i9xx_gdi_build_copy(&copy, 0ul, TEST_CRUMB, 1ul, test_stream,
                                  V9X_I9XX_GDI_STREAM_DWORDS, &written, 0) ==
          V9X_STATUS_INSUFFICIENT_MEMORY);

    /* A breadcrumb in the first page. */
    copy.vram_bytes = TEST_VRAM;
    CHECK(v9x_i9xx_gdi_build_copy(&copy, 0ul, 0x100ul, 1ul, test_stream,
                                  V9X_I9XX_GDI_STREAM_DWORDS, &written, 0) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

/* The 32-bpp form carries the ARGB write enables; the decoder takes it. */
static void test_32bpp(void)
{
    struct v9x_i9xx_gdi_copy copy;
    v9x_u32 written;
    v9x_u32 bands;

    test_copy_defaults(&copy);
    copy.bytes_per_pixel = 4ul;
    copy.pitch = TEST_WIDTH * 4ul;
    copy.width = 16ul;
    copy.height = 16ul;
    copy.source_x = 0ul;
    copy.source_y = 8ul;
    copy.x = 0ul;
    copy.y = 0ul;
    CHECK(v9x_i9xx_gdi_build_copy(&copy, 0ul, TEST_CRUMB, 7ul, test_stream,
                                  V9X_I9XX_GDI_STREAM_DWORDS, &written,
                                  &bands) == V9X_STATUS_OK);
    CHECK(bands == 2ul);
    CHECK(test_stream[0] == V9X_I9XX_MI_FLUSH);
    CHECK(test_stream[1] ==
          (V9X_I9XX_XY_SRC_COPY_BLT | V9X_I9XX_BLT_WRITE_ARGB));
}

/*
 * A copy of more than one stream (Internet Explorer's scroll by a row or
 * two of a wide window): a 320 x 300 screen of its own, every stream built
 * from where the last ended and run in order, in raster order and again in
 * reverse, against the same picture moved with a row buffer.
 */
#define BIG_WIDTH   320ul
#define BIG_HEIGHT  300ul
#define BIG_PITCH   (BIG_WIDTH * TEST_BPP)
#define BIG_BASE    0x1000ul
#define BIG_BYTES   (BIG_PITCH * BIG_HEIGHT)
#define BIG_VRAM    (BIG_BASE + BIG_BYTES + 0x10000ul)
#define BIG_CRUMB   (BIG_VRAM + 0x100ul)

static unsigned char big_screen[BIG_BYTES];
static unsigned char big_expected[BIG_BYTES];
static unsigned char big_rows[BIG_BYTES];

static void big_paint(unsigned char *bytes)
{
    v9x_u32 at;

    for (at = 0ul; at < BIG_BYTES; ++at) {
        bytes[at] = (unsigned char)((at * 13ul + (at >> 9) * 5ul) & 0xfful);
    }
}

static void big_run(const v9x_u32 *stream, v9x_u32 dwords, int backwards)
{
    v9x_u32 at = 0ul;

    while (at < dwords) {
        if (stream[at] == V9X_I9XX_XY_SRC_COPY_BLT) {
            const v9x_u32 *packet = stream + at;
            v9x_u32 pitch = packet[1] & 0xfffful;
            v9x_u32 left = packet[2] & 0xfffful;
            v9x_u32 top = packet[2] >> 16;
            v9x_u32 width = (packet[3] & 0xfffful) - left;
            v9x_u32 count = width * ((packet[3] >> 16) - top);
            v9x_u32 base = packet[4] - BIG_BASE;
            v9x_u32 source_left = packet[5] & 0xfffful;
            v9x_u32 source_top = packet[5] >> 16;
            v9x_u32 source_base = packet[7] - BIG_BASE;
            v9x_u32 step;

            for (step = 0ul; step < count; ++step) {
                v9x_u32 pixel = backwards ? count - 1ul - step : step;
                v9x_u32 dx = pixel % width;
                v9x_u32 dy = pixel / width;

                memcpy(big_screen + base + (top + dy) * pitch +
                           (left + dx) * TEST_BPP,
                       big_screen + source_base +
                           (source_top + dy) * packet[6] +
                           (source_left + dx) * TEST_BPP,
                       TEST_BPP);
            }
            at += V9X_I9XX_BLT_COPY_DWORDS;
        } else if (stream[at] == V9X_I9XX_XY_COLOR_BLT) {
            CHECK(stream[at + 4ul] == BIG_CRUMB);
            at += 6ul;
        } else {
            CHECK(stream[at] == V9X_I9XX_MI_FLUSH ||
                  stream[at] == V9X_I9XX_MI_NOOP);
            ++at;
        }
    }
}

static void test_many_bands(void)
{
    static const v9x_s32 moves[][2] = {
        { 0, 1 }, { 0, -1 }, { 0, 2 }, { 0, -2 }, { 1, 0 }, { -1, 0 },
        { 3, 1 }, { -2, -1 }
    };
    struct v9x_i9xx_gdi_copy copy;
    v9x_u32 move;
    v9x_u32 row;
    v9x_u32 first;
    v9x_u32 written;
    v9x_u32 bands;
    v9x_u32 streams;
    v9x_status status;
    int backwards;

    for (move = 0ul; move < sizeof(moves) / sizeof(moves[0]); ++move) {
        copy.bytes_per_pixel = TEST_BPP;
        copy.vram_bytes = BIG_VRAM;
        copy.base = BIG_BASE;
        copy.pitch = BIG_PITCH;
        copy.width = BIG_WIDTH - 8ul;
        copy.height = BIG_HEIGHT - 8ul;
        copy.source_x = 4ul;
        copy.source_y = 4ul;
        copy.x = (v9x_u32)((v9x_s32)copy.source_x + moves[move][0]);
        copy.y = (v9x_u32)((v9x_s32)copy.source_y + moves[move][1]);

        big_paint(big_expected);
        for (row = 0ul; row < copy.height; ++row) {
            memcpy(big_rows + row * BIG_PITCH,
                   big_expected + (copy.source_y + row) * BIG_PITCH +
                       copy.source_x * TEST_BPP,
                   copy.width * TEST_BPP);
        }
        for (row = 0ul; row < copy.height; ++row) {
            memcpy(big_expected + (copy.y + row) * BIG_PITCH +
                       copy.x * TEST_BPP,
                   big_rows + row * BIG_PITCH, copy.width * TEST_BPP);
        }

        for (backwards = 0; backwards < 2; ++backwards) {
            big_paint(big_screen);
            first = 0ul;
            streams = 0ul;
            bands = 0ul;
            do {
                status = v9x_i9xx_gdi_build_copy(
                    &copy, first, BIG_CRUMB, 0x10ul + streams, test_stream,
                    V9X_I9XX_GDI_STREAM_DWORDS, &written, &bands);
                CHECK(status == V9X_STATUS_OK);
                if (status != V9X_STATUS_OK) {
                    break;
                }
                CHECK((written & 1ul) == 0ul);
                big_run(test_stream, written, backwards);
                ++streams;
                first += V9X_I9XX_GDI_MAX_BANDS;
            } while (first < bands);
            CHECK(bands > V9X_I9XX_GDI_MAX_BANDS);
            CHECK(streams == (bands + V9X_I9XX_GDI_MAX_BANDS - 1ul) /
                                 V9X_I9XX_GDI_MAX_BANDS);
            CHECK(memcmp(big_screen, big_expected, BIG_BYTES) == 0);
        }
        /* Past the last band: refused, nothing written. */
        written = 1ul;
        CHECK(v9x_i9xx_gdi_build_copy(&copy, bands, BIG_CRUMB, 1ul,
                                      test_stream,
                                      V9X_I9XX_GDI_STREAM_DWORDS, &written,
                                      0) == V9X_STATUS_INVALID_ARGUMENT);
        CHECK(written == 0ul);
    }
}

unsigned int v9x_run_i9xx_gdi_tests(void)
{
    test_every_move();
    test_many_bands();
    test_band_counts();
    test_build_refusals();
    test_32bpp();
    return failures;
}
