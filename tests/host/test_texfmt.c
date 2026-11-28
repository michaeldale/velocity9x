#include <stdio.h>

#include "velocity9x/texfmt.h"

/*
 * src\common\texfmt.c against the published lists of the engines V9XD32's
 * T22 measured: the Rage 128 publishes ARGB8888 and drew XRGB8888 black;
 * the SiS, Voodoo4, Mach64 and RIVA 128 publish three 16-bit layouts and
 * drew both 32-bit ones black; Evergreen and the rest publish both and
 * draw both.
 */

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

#define RGB    0x40ul
#define ALPHA  0x01ul

static const struct v9x_texfmt rgb565 = { RGB, 16ul, 0xf800ul, 0x07e0ul,
                                          0x001ful, 0ul };
static const struct v9x_texfmt argb1555 = { RGB | ALPHA, 16ul, 0x7c00ul,
                                            0x03e0ul, 0x001ful, 0x8000ul };
static const struct v9x_texfmt argb4444 = { RGB | ALPHA, 16ul, 0x0f00ul,
                                            0x00f0ul, 0x000ful, 0xf000ul };
static const struct v9x_texfmt argb8888 = { RGB | ALPHA, 32ul, 0x00ff0000ul,
                                            0x0000ff00ul, 0x000000fful,
                                            0xff000000ul };
static const struct v9x_texfmt xrgb8888 = { RGB, 32ul, 0x00ff0000ul,
                                            0x0000ff00ul, 0x000000fful, 0ul };

static void test_sixteen_bit_engine(void)
{
    struct v9x_texfmt list[3];
    struct v9x_texfmt rgb555 = { RGB, 16ul, 0x7c00ul, 0x03e0ul, 0x001ful,
                                 0ul };

    list[0] = rgb565;
    list[1] = argb1555;
    list[2] = argb4444;
    CHECK(v9x_texfmt_unpublished_32(&argb8888, list, 3ul) != 0ul);
    CHECK(v9x_texfmt_unpublished_32(&xrgb8888, list, 3ul) != 0ul);
    CHECK(v9x_texfmt_unpublished_32(&rgb565, list, 3ul) == 0ul);
    /* A 16-bit layout is not judged, published or not. */
    CHECK(v9x_texfmt_unpublished_32(&rgb555, list, 3ul) == 0ul);
}

static void test_rage128(void)
{
    struct v9x_texfmt list[4];

    list[0] = rgb565;
    list[1] = argb1555;
    list[2] = argb4444;
    list[3] = argb8888;
    CHECK(v9x_texfmt_unpublished_32(&argb8888, list, 4ul) == 0ul);
    CHECK(v9x_texfmt_unpublished_32(&xrgb8888, list, 4ul) != 0ul);
}

static void test_both_published(void)
{
    struct v9x_texfmt list[5];
    struct v9x_texfmt xrgb_stray_alpha = xrgb8888;

    list[0] = rgb565;
    list[1] = argb1555;
    list[2] = argb4444;
    list[3] = argb8888;
    list[4] = xrgb8888;
    CHECK(v9x_texfmt_unpublished_32(&argb8888, list, 5ul) == 0ul);
    CHECK(v9x_texfmt_unpublished_32(&xrgb8888, list, 5ul) == 0ul);
    /* Without ALPHAPIXELS an alpha mask means nothing. */
    xrgb_stray_alpha.alpha = 0xff000000ul;
    CHECK(v9x_texfmt_unpublished_32(&xrgb_stray_alpha, list, 5ul) == 0ul);
}

static void test_not_judged(void)
{
    struct v9x_texfmt list[1];
    struct v9x_texfmt fourcc = { 0x04ul, 32ul, 0ul, 0ul, 0ul, 0ul };
    struct v9x_texfmt bgr = { RGB, 32ul, 0x000000fful, 0x0000ff00ul,
                              0x00ff0000ul, 0ul };

    list[0] = rgb565;
    /* No list: nothing to judge against. */
    CHECK(v9x_texfmt_unpublished_32(&xrgb8888, list, 0ul) == 0ul);
    CHECK(v9x_texfmt_unpublished_32(0, list, 1ul) == 0ul);
    CHECK(v9x_texfmt_unpublished_32(&fourcc, list, 1ul) == 0ul);
    /* Any unpublished 32-bit RGB order is refused. */
    CHECK(v9x_texfmt_unpublished_32(&bgr, list, 1ul) != 0ul);
}

unsigned int v9x_run_texfmt_tests(void)
{
    failures = 0u;
    test_sixteen_bit_engine();
    test_rage128();
    test_both_published();
    test_not_judged();
    return failures;
}
