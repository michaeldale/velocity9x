/*
 * The Rage 128 scanout arithmetic, on the values A8U4I5's Rage Fury Pro
 * read (V9XR1MM, boots 519 and 520): CRTC_OFFSET_CNTL=0 under both
 * drivers, CRTC_OFFSET=0 under ours and 0x00010000 under ATI's, and
 * CRTC_STATUS=0x80000002 (FIX_VSYNC_TIMING and a saved blank, not in one).
 */
#include <stdio.h>

#include "velocity9x/r128_crtc.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

#define A8_VRAM 0x02000000ul

unsigned int v9x_run_r128_crtc_tests(void)
{
    v9x_u32 value = 7ul;

    /* The measured control word flips at the blank; tiled, per-line or
     * locked do not. */
    CHECK(v9x_r128_crtc_flips_at_blank(0ul));
    CHECK(!v9x_r128_crtc_flips_at_blank(0x00010000ul));
    CHECK(!v9x_r128_crtc_flips_at_blank(0x00008000ul));
    CHECK(!v9x_r128_crtc_flips_at_blank(0x80000000ul));
    /* The pending bit read back is not a control bit. */
    CHECK(v9x_r128_crtc_flips_at_blank(0x40000000ul));

    /* The start is the byte offset itself: ATI's desktop base, and a back
     * buffer 1.5 MiB in. */
    CHECK(v9x_r128_crtc_start(0x00010000ul, A8_VRAM, 2ul, &value) ==
          V9X_STATUS_OK);
    CHECK(value == 0x00010000ul);
    CHECK(v9x_r128_crtc_start(0x00180000ul, A8_VRAM, 4ul, &value) ==
          V9X_STATUS_OK);
    CHECK(value == 0x00180000ul);
    /* Bits 2:0 are hardwired to zero; past VRAM; 24 bpp off a pixel. */
    CHECK(v9x_r128_crtc_start(0x00180004ul, A8_VRAM, 2ul, &value) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_r128_crtc_start(A8_VRAM, A8_VRAM, 2ul, &value) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_r128_crtc_start(0x02000000ul, 0x04000000ul, 2ul, &value) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_r128_crtc_start(0x00000008ul, A8_VRAM, 3ul, &value) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_r128_crtc_start(0x00000018ul, A8_VRAM, 3ul, &value) ==
          V9X_STATUS_OK);
    CHECK(v9x_r128_crtc_start(0ul, A8_VRAM, 2ul, 0) ==
          V9X_STATUS_INVALID_ARGUMENT);

    /* CRTC_GUI_TRIG_OFFSET, bit 30. */
    CHECK(!v9x_r128_crtc_flip_pending(0x00010000ul));
    CHECK(v9x_r128_crtc_flip_pending(0x40180000ul));

    /* CRTC_VBLANK_CUR, bit 0; the saved bit 1 is not "in the blank". */
    CHECK(!v9x_r128_crtc_in_vblank(0x80000002ul));
    CHECK(v9x_r128_crtc_in_vblank(0x80000003ul));
    return failures;
}
