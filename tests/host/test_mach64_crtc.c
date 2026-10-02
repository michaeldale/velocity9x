/*
 * The Mach64 scanout arithmetic, on the values A8U4I5's Rage IIC read in
 * its 1024x768x16 VBE mode (ATIRX /crtcread, boot 141):
 * CRTC_V_TOTAL_DISP=0x02FF0325 (768 displayed, 806 total) and
 * CRTC_OFF_PITCH=0x20000000 (offset 0, pitch 1024).
 */
#include <stdio.h>

#include "velocity9x/ati_mach64_crtc.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

#define A8_V_TOTAL_DISP 0x02ff0325ul
#define A8_OFF_PITCH    0x20000000ul

static v9x_u32 line(v9x_u32 n)
{
    return n << 16;
}

unsigned int v9x_run_mach64_crtc_tests(void)
{
    v9x_u32 value = 7ul;

    /* The start ATIRX /crtc wrote and read back: 0x20040000 for 2 MiB. */
    CHECK(v9x_m64_crtc_start(A8_OFF_PITCH, 0x00200000ul, 0x00400000ul,
                             &value) == V9X_STATUS_OK);
    CHECK(value == 0x20040000ul);
    /* The pitch and bits above the offset kept; the old offset replaced. */
    CHECK(v9x_m64_crtc_start(0x20140008ul, 0x00180000ul, 0x00400000ul,
                             &value) == V9X_STATUS_OK);
    CHECK(value == 0x20130000ul);
    CHECK(v9x_m64_crtc_start(A8_OFF_PITCH, 0x00200004ul, 0x00400000ul,
                             &value) == V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_m64_crtc_start(A8_OFF_PITCH, 0x00400000ul, 0x00400000ul,
                             &value) == V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_m64_crtc_start(A8_OFF_PITCH, 0x00800000ul, 0x01000000ul,
                             &value) == V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_m64_crtc_start(A8_OFF_PITCH, 0ul, 0x00400000ul, 0) ==
          V9X_STATUS_INVALID_ARGUMENT);

    /* Blank is lines 768..805; the window stops four short of 806. */
    CHECK(!v9x_m64_crtc_in_blank(line(0ul), A8_V_TOTAL_DISP));
    CHECK(!v9x_m64_crtc_in_blank(line(767ul), A8_V_TOTAL_DISP));
    CHECK(v9x_m64_crtc_in_blank(line(768ul), A8_V_TOTAL_DISP));
    CHECK(v9x_m64_crtc_in_blank(line(805ul), A8_V_TOTAL_DISP));
    CHECK(!v9x_m64_crtc_in_blank(line(806ul), A8_V_TOTAL_DISP));
    CHECK(!v9x_m64_crtc_flip_window(line(767ul), A8_V_TOTAL_DISP));
    CHECK(v9x_m64_crtc_flip_window(line(768ul), A8_V_TOTAL_DISP));
    CHECK(v9x_m64_crtc_flip_window(line(801ul), A8_V_TOTAL_DISP));
    CHECK(!v9x_m64_crtc_flip_window(line(802ul), A8_V_TOTAL_DISP));
    /* The bits beside the line field do not count. */
    CHECK(v9x_m64_crtc_in_blank(line(770ul) | 0xf8000325ul,
                                A8_V_TOTAL_DISP));
    /* A timing with no blank has no window. */
    CHECK(!v9x_m64_crtc_in_blank(line(10ul), 0x00090009ul));
    return failures;
}
