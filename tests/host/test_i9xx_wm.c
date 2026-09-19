/*
 * Tests for the Gen3 display FIFO watermark arithmetic.
 *
 * The netbook has no network and every trial on it costs a walk, so the
 * formula is settled here rather than on the machine. The worked case is
 * the one intel91 captured: pipe B at 1344x672 total, 60 Hz, 16 bpp, which
 * is 54,190 kHz of pixel rate.
 */
#include <stdio.h>

#include "velocity9x/i9xx_wm.h"

static unsigned int wm_failures = 0u;

#define WMCHECK(expression) do { \
    if (!(expression)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #expression); \
        ++wm_failures; \
    } \
} while (0)

/*
 * The netbook's mode, against the four plausible FIFO shares. Whatever the
 * partition, the answer is tens - and intel91 read six out of the BIOS.
 */
static void test_the_netbook_mode(void)
{
    v9x_u32 rate = 54190ul;   /* 1344 * 672 * 60 / 1000 */

    WMCHECK(v9x_i9xx_wm_plane(rate, 2ul, 48ul, 5000ul) == 37ul);
    WMCHECK(v9x_i9xx_wm_plane(rate, 2ul, 64ul, 5000ul) == 53ul);
    /* Both of these exceed the field and clamp. */
    WMCHECK(v9x_i9xx_wm_plane(rate, 2ul, 96ul, 5000ul) == V9X_I9XX_WM_MAX);
    WMCHECK(v9x_i9xx_wm_plane(rate, 2ul, 127ul, 5000ul) == V9X_I9XX_WM_MAX);
}

/* A plane driving nothing gets its FIFO less the guard, clamped. */
static void test_inactive_plane(void)
{
    WMCHECK(v9x_i9xx_wm_plane(0ul, 2ul, 48ul, 5000ul) == 46ul);
    WMCHECK(v9x_i9xx_wm_plane(54190ul, 0ul, 48ul, 5000ul) == 46ul);
    WMCHECK(v9x_i9xx_wm_plane(0ul, 0ul, 127ul, 5000ul) == V9X_I9XX_WM_MAX);
    /* A FIFO with no room for the guard still gets a usable watermark. */
    WMCHECK(v9x_i9xx_wm_plane(0ul, 0ul, 2ul, 5000ul) == V9X_I9XX_WM_DEFAULT);
}

/*
 * A mode that wants more than the FIFO holds takes the default, not zero.
 * Zero is a watermark no fetch ever satisfies, which would be worse than
 * the value it replaced.
 */
static void test_demanding_mode_falls_back(void)
{
    WMCHECK(v9x_i9xx_wm_plane(400000ul, 4ul, 48ul, 5000ul) ==
            V9X_I9XX_WM_DEFAULT);
    WMCHECK(v9x_i9xx_wm_plane(54190ul, 2ul, 10ul, 5000ul) ==
            V9X_I9XX_WM_DEFAULT);
}

/* Higher latency and deeper pixels both leave less room. */
static void test_the_arithmetic_runs_one_way(void)
{
    v9x_u32 base = v9x_i9xx_wm_plane(54190ul, 2ul, 64ul, 5000ul);

    WMCHECK(v9x_i9xx_wm_plane(54190ul, 2ul, 64ul, 10000ul) < base);
    WMCHECK(v9x_i9xx_wm_plane(54190ul, 4ul, 64ul, 5000ul) < base);
    WMCHECK(v9x_i9xx_wm_plane(108380ul, 2ul, 64ul, 5000ul) < base);
    /* And a bigger share of the FIFO leaves more. */
    WMCHECK(v9x_i9xx_wm_plane(54190ul, 2ul, 48ul, 5000ul) < base);
}

/*
 * FW_BLC as i915 composes it, checked against what intel91 read out of the
 * BIOS: the burst bits match, the watermarks do not.
 */
static void test_fw_blc_composition(void)
{
    WMCHECK(v9x_i9xx_wm_fw_blc(6ul, 6ul) == 0x01060106ul);
    WMCHECK(v9x_i9xx_wm_fw_blc(63ul, 63ul) == 0x013f013ful);
    /* Watermarks wider than the field do not spill into the burst bits. */
    WMCHECK(v9x_i9xx_wm_fw_blc(0xfful, 0xfful) == 0x013f013ful);
    WMCHECK((v9x_i9xx_wm_fw_blc(1ul, 1ul) & ((v9x_u32)1ul << 8)) != 0ul);
    WMCHECK((v9x_i9xx_wm_fw_blc(1ul, 1ul) & ((v9x_u32)1ul << 24)) != 0ul);
}

/*
 * The DSPARB partition, anchored on the netbook's own register.
 *
 * intel93 read 0x00001D9C. Against the shifts this file first used - 9 and
 * 16 - that decodes to a CSTART of nothing and the split refuses, which is
 * how the wrong layout was caught rather than believed. Against i915's, it
 * is plane A 28 and plane B 31.
 */
static void test_fifo_split(void)
{
    v9x_u32 a = 0ul;
    v9x_u32 b = 0ul;

    WMCHECK(v9x_i9xx_wm_fifo_split(0x00001D9Cul, &a, &b) == V9X_TRUE);
    WMCHECK(a == 28ul);
    WMCHECK(b == 31ul);

    /* Plane A 48, plane C starting at 96: 48 entries each. */
    WMCHECK(v9x_i9xx_wm_fifo_split((96ul << 7) | 48ul, &a, &b) == V9X_TRUE);
    WMCHECK(a == 48ul);
    WMCHECK(b == 48ul);

    /* C at or below B leaves plane B nothing. */
    WMCHECK(v9x_i9xx_wm_fifo_split((48ul << 7) | 48ul, &a, &b) == V9X_FALSE);
    /* B at zero leaves plane A nothing. */
    WMCHECK(v9x_i9xx_wm_fifo_split((96ul << 7), &a, &b) == V9X_FALSE);
    /* And a refusal leaves the caller's values alone. */
    WMCHECK(a == 48ul);
    WMCHECK(b == 48ul);
    WMCHECK(v9x_i9xx_wm_fifo_split(0ul, 0, &b) == V9X_FALSE);
}

/*
 * The netbook's actual configuration, end to end: its DSPARB, its timing,
 * against what its BIOS programs. This is the comparison intel93 was taken
 * for, pinned here so it cannot drift.
 */
static void test_the_netbook_as_captured(void)
{
    v9x_u32 a = 0ul;
    v9x_u32 b = 0ul;

    WMCHECK(v9x_i9xx_wm_fifo_split(0x00001D9Cul, &a, &b) == V9X_TRUE);
    WMCHECK(v9x_i9xx_wm_plane(54180ul, 2ul, a, 5000ul) == 17ul);
    WMCHECK(v9x_i9xx_wm_plane(54180ul, 2ul, b, 5000ul) == 20ul);
    /* What the BIOS leaves there instead, from intel91 through intel93. */
    WMCHECK(v9x_i9xx_wm_fw_blc(17ul, 20ul) != 0x03060106ul);
}

unsigned int v9x_run_i9xx_wm_tests(void)
{
    test_the_netbook_mode();
    test_inactive_plane();
    test_demanding_mode_falls_back();
    test_the_arithmetic_runs_one_way();
    test_fw_blc_composition();
    test_fifo_split();
    test_the_netbook_as_captured();
    return wm_failures;
}
