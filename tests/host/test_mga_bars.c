#include <stdio.h>

#include "velocity9x/mga_bars.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

static int selects_with(v9x_u32 bar0, v9x_u32 bar1, v9x_u16 fallback,
                        v9x_u16 index, v9x_u32 base)
{
    v9x_u16 got_index = 0xffffu;
    v9x_u32 got_base = 0xfffffffful;

    if (v9x_matrox_mga_select_control_bar(bar0, bar1, fallback, &got_index,
                                          &got_base) != V9X_TRUE) {
        return 0;
    }
    return got_index == index && got_base == base;
}

static int refuses_with(v9x_u32 bar0, v9x_u32 bar1, v9x_u16 fallback)
{
    v9x_u16 got_index = 0xffffu;
    v9x_u32 got_base = 0xfffffffful;

    return v9x_matrox_mga_select_control_bar(bar0, bar1, fallback,
                                             &got_index, &got_base) ==
               V9X_FALSE &&
           got_index == 0u && got_base == 0ul;
}

/* Silicon sets the prefetchable bit, so the fallback must never be what
 * decides there: every measured case is checked with no fallback. */
static int selects(v9x_u32 bar0, v9x_u32 bar1, v9x_u16 index, v9x_u32 base)
{
    return selects_with(bar0, bar1, V9X_MGA_NO_FALLBACK_BAR, index, base) &&
           selects_with(bar0, bar1, 0u, index, base) &&
           selects_with(bar0, bar1, 1u, index, base);
}

static int refuses(v9x_u32 bar0, v9x_u32 bar1)
{
    return refuses_with(bar0, bar1, V9X_MGA_NO_FALLBACK_BAR) &&
           refuses_with(bar0, bar1, 0u) &&
           refuses_with(bar0, bar1, 1u);
}

/* The values each source recorded. */
static void test_measured_orders(void)
{
    /* MGA-2064W, BringupKit host, 2026-09-09: control BAR0, FB BAR1. */
    CHECK(selects(0xFDCFC000ul, 0xFD000008ul, 0u, 0xFDCFC000ul));
    /* MGA-2064W, A8U4I5 (Configuration Manager, 2026-10-09). */
    CHECK(selects(0xDC000000ul, 0xDD000008ul, 0u, 0xDC000000ul));
    /* MGA-1164SG, Match451's survey 2026-10-17: FB BAR0, control BAR1 - the
     * MGA-2164W's order. */
    CHECK(selects(0xF5000008ul, 0xF4800000ul, 1u, 0xF4800000ul));
}

/*
 * 86Box returns 00h for the low byte of BAR0 and BAR1 on every Millennium
 * and Mystique (vid_mga.c, PCI config read cases 10h and 14h), so neither
 * BAR is prefetchable there. Measured 2026-10-18 on Win98SE-Millennium: with
 * no fallback the engine hook refused and EngineCaps read 0. The chip's
 * stated framebuffer_bar decides then, and only then.
 */
static void test_neither_prefetchable_takes_the_fallback(void)
{
    /* 86Box 2064W: framebuffer_bar 1, so control is BAR0. */
    CHECK(selects_with(0xF8000000ul, 0xF9000000ul, 0u, 0u, 0xF8000000ul));
    /* 86Box 2164W: framebuffer_bar 0, so control is BAR1. */
    CHECK(selects_with(0xF9000000ul, 0xF8000000ul, 1u, 1u, 0xF8000000ul));
    /* No fallback stated: refused. */
    CHECK(refuses_with(0xF8000000ul, 0xF9000000ul, V9X_MGA_NO_FALLBACK_BAR));
    /* A fallback that is not 0 or 1 is no fallback. */
    CHECK(refuses_with(0xF8000000ul, 0xF9000000ul, 2u));
    /* The fallback names a BAR, and that BAR must still pass the checks. */
    CHECK(refuses_with(0xF8002000ul, 0xF9000000ul, 0u));
    CHECK(refuses_with(0xF8000000ul, 0x0000D001ul, 0u));
}

static void test_refusals(void)
{
    /* Both prefetchable: no fallback overrides that. */
    CHECK(refuses(0xF5000008ul, 0xF4800008ul));
    /* An I/O BAR in either slot. */
    CHECK(refuses(0x0000D001ul, 0xF4800008ul));
    CHECK(refuses(0xF5000008ul, 0x0000D001ul));
    /* A 64-bit framebuffer BAR makes BAR1 its upper half, not an aperture. */
    CHECK(refuses(0xF500000Cul, 0x00000000ul));
    CHECK(refuses(0xDC000000ul, 0xDD00000Cul));
    /* Control base not 16 KiB aligned, unassigned, or under 16 MiB. */
    CHECK(refuses(0xF5000008ul, 0xF4802000ul));
    CHECK(refuses(0xF5000008ul, 0x00000000ul));
    CHECK(refuses(0x00800000ul, 0xDD000008ul));
    /* Control base above the last 16 KiB of the address space. */
    CHECK(refuses(0xF5000008ul, 0xFFFFFFF0ul));
}

static void test_null_outputs(void)
{
    v9x_u16 index;
    v9x_u32 base;

    CHECK(v9x_matrox_mga_select_control_bar(0xF5000008ul, 0xF4800000ul,
                                            V9X_MGA_NO_FALLBACK_BAR, 0,
                                            &base) == V9X_FALSE);
    CHECK(v9x_matrox_mga_select_control_bar(0xF5000008ul, 0xF4800000ul,
                                            V9X_MGA_NO_FALLBACK_BAR, &index,
                                            0) == V9X_FALSE);
}

unsigned int v9x_run_mga_bars_tests(void)
{
    test_measured_orders();
    test_neither_prefetchable_takes_the_fallback();
    test_refusals();
    test_null_outputs();
    return failures;
}
