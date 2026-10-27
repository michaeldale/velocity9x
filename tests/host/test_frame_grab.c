/*
 * Frame grabs, pinned. The plan is the bounds check for a direct read of
 * video memory, so its refusals come first; then the pixel decode, the BMP
 * layout and the schedule.
 */
#include <stdio.h>

#include "velocity9x/frame_grab.h"

static unsigned int grab_failures = 0u;

#define GRABCHECK(expression) do { \
    if (!(expression)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #expression); \
        ++grab_failures; \
    } \
} while (0)

#define MASK565_R 0x0000F800ul
#define MASK565_G 0x000007E0ul
#define MASK565_B 0x0000001Ful
#define MASK8888_R 0x00FF0000ul
#define MASK8888_G 0x0000FF00ul
#define MASK8888_B 0x000000FFul

/* The Ironlake's 3DMark 2000 mode: 1024x768x16, pitch 2048. */
static void test_a_mode_as_run(void)
{
    struct v9x_frame_grab_plan plan;

    GRABCHECK(v9x_frame_grab_plan(0x00180000ul, 1024ul, 768ul, 2048ul, 16ul,
                                  0x01000000ul, MASK565_R, MASK565_G,
                                  MASK565_B, &plan) == V9X_TRUE);
    GRABCHECK(plan.bytes_per_pixel == 2ul);
    GRABCHECK(plan.row_bytes == 3072ul);
    GRABCHECK(plan.file_bytes == 54ul + 768ul * 3072ul);
}

static void test_the_refusals(void)
{
    struct v9x_frame_grab_plan plan;

    plan.width = 0x5a5a5a5aul;
    /* 8 and 24 bpp. */
    GRABCHECK(v9x_frame_grab_plan(0ul, 640ul, 480ul, 640ul, 8ul, 0x400000ul,
                                  1ul, 1ul, 1ul, &plan) == V9X_FALSE);
    GRABCHECK(v9x_frame_grab_plan(0ul, 640ul, 480ul, 1920ul, 24ul,
                                  0x400000ul, 1ul, 1ul, 1ul, &plan) ==
              V9X_FALSE);
    /* No masks. */
    GRABCHECK(v9x_frame_grab_plan(0ul, 640ul, 480ul, 1280ul, 16ul,
                                  0x400000ul, 0ul, MASK565_G, MASK565_B,
                                  &plan) == V9X_FALSE);
    /* Wider than the row buffer. */
    GRABCHECK(v9x_frame_grab_plan(0ul, 2049ul, 4ul, 8192ul, 16ul,
                                  0x400000ul, MASK565_R, MASK565_G,
                                  MASK565_B, &plan) == V9X_FALSE);
    /* A row longer than the pitch. */
    GRABCHECK(v9x_frame_grab_plan(0ul, 1024ul, 768ul, 2046ul, 16ul,
                                  0x400000ul, MASK565_R, MASK565_G,
                                  MASK565_B, &plan) == V9X_FALSE);
    /* Zero sizes. */
    GRABCHECK(v9x_frame_grab_plan(0ul, 0ul, 768ul, 2048ul, 16ul, 0x400000ul,
                                  MASK565_R, MASK565_G, MASK565_B, &plan) ==
              V9X_FALSE);
    GRABCHECK(v9x_frame_grab_plan(0ul, 1024ul, 0ul, 2048ul, 16ul, 0x400000ul,
                                  MASK565_R, MASK565_G, MASK565_B, &plan) ==
              V9X_FALSE);
    GRABCHECK(v9x_frame_grab_plan(0ul, 1024ul, 768ul, 2048ul, 16ul,
                                  0x400000ul, MASK565_R, MASK565_G,
                                  MASK565_B, 0) == V9X_FALSE);
    GRABCHECK(plan.width == 0x5a5a5a5aul);
}

/* The last row, exactly in and one byte out. */
static void test_the_surface_must_fit_vram(void)
{
    struct v9x_frame_grab_plan plan;
    v9x_u32 fits = 0x00100000ul + 767ul * 2048ul + 2048ul;

    GRABCHECK(v9x_frame_grab_plan(0x00100000ul, 1024ul, 768ul, 2048ul, 16ul,
                                  fits, MASK565_R, MASK565_G, MASK565_B,
                                  &plan) == V9X_TRUE);
    GRABCHECK(v9x_frame_grab_plan(0x00100000ul, 1024ul, 768ul, 2048ul, 16ul,
                                  fits - 1ul, MASK565_R, MASK565_G,
                                  MASK565_B, &plan) == V9X_FALSE);
    /* An offset past the end. */
    GRABCHECK(v9x_frame_grab_plan(0x00500000ul, 4ul, 4ul, 8ul, 16ul,
                                  0x00400000ul, MASK565_R, MASK565_G,
                                  MASK565_B, &plan) == V9X_FALSE);
}

/* Height times pitch, and offset plus that, past 32 bits: a naive bound
 * wraps and accepts. */
static void test_the_products_that_overflow(void)
{
    struct v9x_frame_grab_plan plan;

    GRABCHECK(v9x_frame_grab_plan(0ul, 1024ul, 0x00100001ul, 0x00001000ul,
                                  32ul, 0x10000000ul, MASK8888_R, MASK8888_G,
                                  MASK8888_B, &plan) == V9X_FALSE);
    GRABCHECK(v9x_frame_grab_plan(0xfffff000ul, 1024ul, 2ul, 0x1000ul, 32ul,
                                  0xfffffffful, MASK8888_R, MASK8888_G,
                                  MASK8888_B, &plan) == V9X_FALSE);
    GRABCHECK(v9x_frame_grab_plan(0ul, 1024ul, 3ul, 0x80000000ul, 16ul,
                                  0xfffffffful, MASK565_R, MASK565_G,
                                  MASK565_B, &plan) == V9X_FALSE);
}

static void test_the_channels(void)
{
    /* 565 white, black and pure channels. */
    GRABCHECK(v9x_frame_grab_channel(0xFFFFul, MASK565_R) == 0xFFu);
    GRABCHECK(v9x_frame_grab_channel(0xFFFFul, MASK565_G) == 0xFFu);
    GRABCHECK(v9x_frame_grab_channel(0xFFFFul, MASK565_B) == 0xFFu);
    GRABCHECK(v9x_frame_grab_channel(0x0000ul, MASK565_G) == 0x00u);
    GRABCHECK(v9x_frame_grab_channel(0xF800ul, MASK565_G) == 0x00u);
    /* 5-bit 10000b replicates to 10000100b, not 10000000b. */
    GRABCHECK(v9x_frame_grab_channel(0x8000ul, MASK565_R) == 0x84u);
    /* 6-bit 100000b to 10000010b. */
    GRABCHECK(v9x_frame_grab_channel(0x0400ul, MASK565_G) == 0x82u);
    /* 555's green is five bits at 5. */
    GRABCHECK(v9x_frame_grab_channel(0x03E0ul, 0x03E0ul) == 0xFFu);
    /* 8888 takes the byte as it is, alpha ignored. */
    GRABCHECK(v9x_frame_grab_channel(0xFF123456ul, MASK8888_R) == 0x12u);
    GRABCHECK(v9x_frame_grab_channel(0xFF123456ul, MASK8888_B) == 0x56u);
    /* 1010102's ten bits keep their top eight. */
    GRABCHECK(v9x_frame_grab_channel(0x3FF00000ul, 0x3FF00000ul) == 0xFFu);
    GRABCHECK(v9x_frame_grab_channel(0x20000000ul, 0x3FF00000ul) == 0x80u);
    GRABCHECK(v9x_frame_grab_channel(0x1234ul, 0ul) == 0u);
}

static void test_the_bmp_header(void)
{
    struct v9x_frame_grab_plan plan;
    v9x_u8 header[V9X_FRAME_GRAB_BMP_HEADER_BYTES];

    GRABCHECK(v9x_frame_grab_plan(0ul, 3ul, 2ul, 8ul, 16ul, 0x1000ul,
                                  MASK565_R, MASK565_G, MASK565_B, &plan) ==
              V9X_TRUE);
    /* 3 pixels: 9 bytes, padded to 12. */
    GRABCHECK(plan.row_bytes == 12ul);
    v9x_frame_grab_bmp_header(&plan, header);
    GRABCHECK(header[0] == 'B' && header[1] == 'M');
    GRABCHECK(header[2] == 78u && header[3] == 0u);     /* 54 + 2 * 12 */
    GRABCHECK(header[10] == 54u);
    GRABCHECK(header[14] == 40u);
    GRABCHECK(header[18] == 3u && header[22] == 2u);
    GRABCHECK(header[26] == 1u && header[28] == 24u);
    GRABCHECK(header[30] == 0u);                        /* BI_RGB */
    GRABCHECK(header[34] == 24u);
}

static void test_a_row(void)
{
    struct v9x_frame_grab_plan plan;
    /* 565 red, green and blue, little-endian. */
    v9x_u8 line[6] = { 0x00u, 0xF8u, 0xE0u, 0x07u, 0x1Fu, 0x00u };
    v9x_u8 out[12];
    v9x_u8 quad[8] = { 0x56u, 0x34u, 0x12u, 0xFFu,
                       0x00u, 0x00u, 0x00u, 0x00u };

    GRABCHECK(v9x_frame_grab_plan(0ul, 3ul, 1ul, 6ul, 16ul, 0x1000ul,
                                  MASK565_R, MASK565_G, MASK565_B, &plan) ==
              V9X_TRUE);
    out[9] = out[10] = out[11] = 0xEEu;
    v9x_frame_grab_row(&plan, line, out);
    /* Blue, green, red per pixel. */
    GRABCHECK(out[0] == 0x00u && out[1] == 0x00u && out[2] == 0xFFu);
    GRABCHECK(out[3] == 0x00u && out[4] == 0xFFu && out[5] == 0x00u);
    GRABCHECK(out[6] == 0xFFu && out[7] == 0x00u && out[8] == 0x00u);
    GRABCHECK(out[9] == 0u && out[10] == 0u && out[11] == 0u);

    GRABCHECK(v9x_frame_grab_plan(0ul, 2ul, 1ul, 8ul, 32ul, 0x1000ul,
                                  MASK8888_R, MASK8888_G, MASK8888_B,
                                  &plan) == V9X_TRUE);
    v9x_frame_grab_row(&plan, quad, out);
    GRABCHECK(out[0] == 0x56u && out[1] == 0x34u && out[2] == 0x12u);
    GRABCHECK(out[3] == 0u && out[4] == 0u && out[5] == 0u);
}

static void test_the_schedule(void)
{
    v9x_u32 countdown = 3ul;
    unsigned int flips;
    unsigned int due = 0u;

    /* Every third flip: flips 3, 6 and 9 of nine. */
    for (flips = 1u; flips <= 9u; ++flips) {
        if (v9x_frame_grab_due(&countdown, 3ul, 5ul) == V9X_TRUE) {
            GRABCHECK(flips % 3u == 0u);
            ++due;
        }
    }
    GRABCHECK(due == 3u);
    /* Nothing once the count is spent. */
    countdown = 1ul;
    GRABCHECK(v9x_frame_grab_due(&countdown, 1ul, 0ul) == V9X_FALSE);
    /* Interval 0 is every flip. */
    countdown = 0ul;
    GRABCHECK(v9x_frame_grab_due(&countdown, 0ul, 2ul) == V9X_TRUE);
    GRABCHECK(v9x_frame_grab_due(&countdown, 0ul, 2ul) == V9X_TRUE);
    GRABCHECK(v9x_frame_grab_due(0, 1ul, 2ul) == V9X_FALSE);
}

unsigned int v9x_run_frame_grab_tests(void)
{
    grab_failures = 0u;
    test_a_mode_as_run();
    test_the_refusals();
    test_the_surface_must_fit_vram();
    test_the_products_that_overflow();
    test_the_channels();
    test_the_bmp_header();
    test_a_row();
    test_the_schedule();

    return grab_failures;
}
