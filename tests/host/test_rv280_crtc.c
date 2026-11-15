/*
 * The Radeon 9250 (RV280) scanout arithmetic, on the values M-HNBJNV's
 * card read under VBE (V9XR2MM, 2026-11-15): CRTC_OFFSET_CNTL = 10000000h,
 * CRTC_OFFSET = 0, MC_FB_LOCATION's start 0, 128 MiB.
 */
#include <stdio.h>

#include "velocity9x/rv280_crtc.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

#define HN_FB_BYTES 0x01000000ul

unsigned int v9x_run_rv280_crtc_tests(void)
{
    v9x_u32 value = 7ul;

    /* The measured control word flips at the blank and reports pending. */
    CHECK(v9x_rv280_crtc_flips_at_blank(0x10000000ul));
    /* Without GUI_TRIG_OFFSET_LEFT_EN a flip could not be seen pending. */
    CHECK(!v9x_rv280_crtc_flips_at_blank(0ul));
    /* Per-line, tiled, stereo or locked are not this model. */
    CHECK(!v9x_rv280_crtc_flips_at_blank(0x10010000ul));
    CHECK(!v9x_rv280_crtc_flips_at_blank(0x10008000ul));
    CHECK(!v9x_rv280_crtc_flips_at_blank(0x10004000ul));
    CHECK(!v9x_rv280_crtc_flips_at_blank(0x10020000ul));
    CHECK(!v9x_rv280_crtc_flips_at_blank(0x90000000ul));

    /* At GPU base 0 the start is the byte offset: a back buffer 1.5 MiB
     * in at 16 bpp, and one at 32 bpp. */
    CHECK(v9x_rv280_crtc_start(0ul, HN_FB_BYTES, 0x00180000ul, 2ul,
                               &value) == V9X_STATUS_OK);
    CHECK(value == 0x00180000ul);
    CHECK(v9x_rv280_crtc_start(0ul, HN_FB_BYTES, 0x00300000ul, 4ul,
                               &value) == V9X_STATUS_OK);
    CHECK(value == 0x00300000ul);
    /* A non-zero frame buffer base is added. */
    CHECK(v9x_rv280_crtc_start(0x00100000ul, HN_FB_BYTES, 0x00000800ul, 2ul,
                               &value) == V9X_STATUS_OK);
    CHECK(value == 0x00100800ul);

    /* Bits 2:0 are zero; past the frame buffer; 24 bpp off a pixel; an
     * address past the 27-bit field; no output. */
    value = 7ul;
    CHECK(v9x_rv280_crtc_start(0ul, HN_FB_BYTES, 0x00180004ul, 2ul,
                               &value) == V9X_STATUS_INVALID_ARGUMENT);
    CHECK(value == 7ul);
    CHECK(v9x_rv280_crtc_start(0ul, HN_FB_BYTES, HN_FB_BYTES, 2ul,
                               &value) == V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_rv280_crtc_start(0ul, HN_FB_BYTES, 0x00000008ul, 3ul,
                               &value) == V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_rv280_crtc_start(0ul, HN_FB_BYTES, 0x00000018ul, 3ul,
                               &value) == V9X_STATUS_OK);
    CHECK(v9x_rv280_crtc_start(0x08000000ul, HN_FB_BYTES, 0ul, 2ul,
                               &value) == V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_rv280_crtc_start(0ul, HN_FB_BYTES, 0ul, 2ul, 0) ==
          V9X_STATUS_INVALID_ARGUMENT);

    /* The pending and blank bits. */
    CHECK(!v9x_rv280_crtc_flip_pending(0ul));
    CHECK(v9x_rv280_crtc_flip_pending(0x40180000ul));
    CHECK(v9x_rv280_crtc_in_vblank(0x00000001ul));
    CHECK(!v9x_rv280_crtc_in_vblank(0x80000002ul));

    return failures;
}
