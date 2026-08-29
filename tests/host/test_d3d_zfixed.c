/*
 * Tests for the ViRGE 1.31 depth conversion.
 *
 * This is the one calculation in the Z path that fails silently and
 * catastrophically rather than visibly. sz = 1.0 - a cleared depth buffer's
 * far plane, and any unprojected background quad - scales to exactly 2^31,
 * which does not fit a signed 32-bit integer. The engine's x87 fistp stores
 * the integer indefinite 0x80000000 on overflow, for either sign; a plain C
 * cast is undefined and in practice does the same on x86.
 *
 * 0x80000000 is the worst possible value for this register. 86Box's model
 * forms the pixel depth as (start << 1) >> 16
 * (build\reference-vid_s3_virge.c:4261), and 0x80000000 << 1 is zero - so the
 * FAR plane becomes the NEAR plane and a background quad occludes the entire
 * scene. Nothing about that reads as a conversion bug when you see it.
 *
 * Hence a table rather than a pixel test: the boundary is exactly
 * representable and the consequence is not observable by looking at it.
 */
#include <stdio.h>

/* Reached by relative path, as test_hw16_modes.c reaches the chipset tables:
 * the header is engine-side and deliberately not published under include\. */
#include "../../src/display32/d3d/d3d_zfixed.h"

static unsigned int zfixed_failures = 0u;

#define ZCHECK(expression) do { \
    if (!(expression)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #expression); \
        ++zfixed_failures; \
    } \
} while (0)

/*
 * The depth conversion never returns a negative value, and never returns the
 * x87 integer indefinite.
 *
 * Both halves matter separately. A negative result is reinterpreted as a huge
 * unsigned depth by the hardware, and 0x80000000 specifically shifts to zero.
 */
static void test_depth_never_negative_or_indefinite(void)
{
    static const float samples[] = {
        -1.0f, -0.5f, -0.0000001f, 0.0f, 0.25f, 0.5f, 0.75f,
        0.9999f, 0.99999994f, 1.0f, 1.0000001f, 2.0f, 1000.0f
    };
    unsigned int index;

    for (index = 0u; index < sizeof(samples) / sizeof(samples[0]); ++index) {
        long result = v9x_d3d_z_to_1_31_depth(samples[index]);

        if (result < 0l) {
            printf("FAIL %s:%u: depth(%f) = %ld, negative\n",
                   __FILE__, (unsigned int)__LINE__,
                   (double)samples[index], result);
            ++zfixed_failures;
        }
        if ((unsigned long)result == 0x80000000ul) {
            printf("FAIL %s:%u: depth(%f) is the x87 indefinite\n",
                   __FILE__, (unsigned int)__LINE__,
                   (double)samples[index]);
            ++zfixed_failures;
        }
    }
}

/* The two ends of the declared domain, named explicitly because they are the
 * values real geometry actually carries. */
static void test_depth_endpoints(void)
{
    /* The far plane. This is the case that motivates the whole file. */
    ZCHECK(v9x_d3d_z_to_1_31_depth(1.0f) == V9X_D3D_Z_1_31_MAX);
    /* Anything beyond it is still the far plane, not a wrap. */
    ZCHECK(v9x_d3d_z_to_1_31_depth(2.0f) == V9X_D3D_Z_1_31_MAX);
    /* The near plane, and anything in front of it. */
    ZCHECK(v9x_d3d_z_to_1_31_depth(0.0f) == 0l);
    ZCHECK(v9x_d3d_z_to_1_31_depth(-0.5f) == 0l);
}

/*
 * Depths inside the domain land in the right 16-bit bucket.
 *
 * The hardware keeps the top bits: 86Box forms (start << 1) >> 16, so a 1.31
 * value of sz * 2^31 becomes sz * 65536. These are the five depths the probe's
 * ladders use, so a change here is a change to what the guest test means.
 */
static void test_depth_buckets(void)
{
    ZCHECK((v9x_d3d_z_to_1_31_depth(0.125f)  >> 15) == 8192l);
    ZCHECK((v9x_d3d_z_to_1_31_depth(0.1875f) >> 15) == 12288l);
    ZCHECK((v9x_d3d_z_to_1_31_depth(0.25f)   >> 15) == 16384l);
    ZCHECK((v9x_d3d_z_to_1_31_depth(0.5f)    >> 15) == 32768l);
    ZCHECK((v9x_d3d_z_to_1_31_depth(0.75f)   >> 15) == 49152l);
}

/* Gradients are signed, and saturate rather than wrap at either end. */
static void test_gradients_are_signed_and_saturate(void)
{
    ZCHECK(v9x_d3d_z_to_1_31_signed(0.0f) == 0l);
    ZCHECK(v9x_d3d_z_to_1_31_signed(0.5f) > 0l);
    ZCHECK(v9x_d3d_z_to_1_31_signed(-0.5f) < 0l);
    /* Symmetric about zero, so a triangle's depth slope does not depend on
     * which way round its vertices were handed to us. */
    ZCHECK(v9x_d3d_z_to_1_31_signed(0.25f) ==
           -v9x_d3d_z_to_1_31_signed(-0.25f));

    ZCHECK(v9x_d3d_z_to_1_31_signed(1.0f) == V9X_D3D_Z_1_31_MAX);
    ZCHECK(v9x_d3d_z_to_1_31_signed(4.0f) == V9X_D3D_Z_1_31_MAX);
    ZCHECK(v9x_d3d_z_to_1_31_signed(-1.0f) == -V9X_D3D_Z_1_31_MAX);
    ZCHECK(v9x_d3d_z_to_1_31_signed(-4.0f) == -V9X_D3D_Z_1_31_MAX);
    ZCHECK((unsigned long)v9x_d3d_z_to_1_31_signed(4.0f) != 0x80000000ul);
    ZCHECK((unsigned long)v9x_d3d_z_to_1_31_signed(-4.0f) != 0x80000000ul);
}

unsigned int v9x_run_d3d_zfixed_tests(void)
{
    test_depth_never_negative_or_indefinite();
    test_depth_endpoints();
    test_depth_buckets();
    test_gradients_are_signed_and_saturate();
    return zfixed_failures;
}
