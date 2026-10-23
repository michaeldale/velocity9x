/*
 * The Rage 128 bus-master ring's set-up arithmetic, on the values A8U4I5's
 * Rage Fury Pro measured (440BX aperture D8000000, R128CCE /hbase, boot
 * 539; docs\decisions\2026-10-23-rage128-cce-ring-agp-fetches-but-wrong-
 * data.md): a ring at D9C00000 ran with AGP_BASE D8000000 and offset
 * 01C00000, and fetched nothing with AGP_BASE D9C00000. ATI's own ring ran
 * at AGP_BASE DA000000, offset 0.
 */
#include <stdio.h>

#include "velocity9x/r128_ring.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

#define RING_64K 0x00010000ul
#define SEG V9X_R128_VTX_SEGMENT_BYTES

/* The vertex area starts behind the 64 KiB ring and its mirror page. */
typedef char v9x_test_r128_vertex_offset[
    V9X_R128_RING_VERTEX_OFFSET == RING_64K + 0x1000ul ? 1 : -1];

/* The vertex area's segments: filled in order, each closed with an age
 * the CCE writes back, and reused only once that age has come back. */
static void test_vertex_area(void)
{
    v9x_r128_vtx_area area;
    v9x_r128_vtx_grant grant;
    v9x_u32 segment;

    v9x_r128_vtx_reset(&area);
    /* Too big for a segment: declined, nothing changes. */
    CHECK(!v9x_r128_vtx_reserve(&area, SEG + 4ul, &grant));
    /* The first piece: segment 0, offset 0, no close, no wait. */
    CHECK(v9x_r128_vtx_reserve(&area, 0x1000ul, &grant));
    CHECK(grant.offset == 0ul && grant.close_age == 0ul &&
          grant.wait_age == 0ul);
    CHECK(v9x_r128_vtx_reserve(&area, 0x2000ul, &grant));
    CHECK(grant.offset == 0x1000ul && grant.close_age == 0ul);
    /* Past the segment: segment 0 closes with age 1, segment 1 is new. */
    CHECK(v9x_r128_vtx_reserve(&area, SEG - 0x1000ul, &grant));
    CHECK(grant.offset == SEG && grant.close_age == 1ul &&
          grant.wait_age == 0ul);
    /* Fill the rest round to segment 0 again: it waits for age 1. */
    for (segment = 2ul; segment < V9X_R128_VTX_SEGMENTS; ++segment) {
        CHECK(v9x_r128_vtx_reserve(&area, SEG, &grant));
        CHECK(grant.offset == segment * SEG);
        CHECK(grant.close_age == segment);
        CHECK(grant.wait_age == 0ul);
    }
    CHECK(v9x_r128_vtx_reserve(&area, 0x100ul, &grant));
    CHECK(grant.offset == 0ul);
    CHECK(grant.close_age == V9X_R128_VTX_SEGMENTS);
    CHECK(grant.wait_age == 1ul);
    /* After a drain every segment is free and nothing waits. */
    v9x_r128_vtx_all_done(&area);
    CHECK(v9x_r128_vtx_reserve(&area, SEG, &grant));
    CHECK(grant.wait_age == 0ul);
    /* An age the CCE has written back covers every age below it. */
    CHECK(v9x_r128_vtx_age_reached(5ul, 5ul));
    CHECK(v9x_r128_vtx_age_reached(6ul, 5ul));
    CHECK(!v9x_r128_vtx_age_reached(4ul, 5ul));
    CHECK(v9x_r128_vtx_age_reached(0ul, 0ul));
}

unsigned int v9x_run_r128_ring_tests(void)
{
    v9x_r128_ring_plan plan;

    /* The measured placement: the base is the 32 MiB window holding the
     * ring, not the ring rounded to AGP_BASE's 4 MiB granularity. */
    CHECK(v9x_r128_ring_plan_make(0xd9c00000ul, RING_64K, &plan) ==
          V9X_STATUS_OK);
    CHECK(plan.agp_base == 0xd8000000ul);
    CHECK(plan.buffer_offset == 0x01c00000ul);
    CHECK(plan.size_l2qw == 13ul);
    CHECK(plan.dword_mask == 0x3ffful);

    /* ATI's: a ring at the window's start. */
    CHECK(v9x_r128_ring_plan_make(0xda000000ul, RING_64K, &plan) ==
          V9X_STATUS_OK);
    CHECK(plan.agp_base == 0xda000000ul);
    CHECK(plan.buffer_offset == 0ul);

    /* 8 KiB, Linux's smallest, and 1 MiB, ATI's. */
    CHECK(v9x_r128_ring_plan_make(0xd8000000ul, 0x2000ul, &plan) ==
          V9X_STATUS_OK);
    CHECK(plan.size_l2qw == 10ul);
    CHECK(plan.dword_mask == 0x7fful);
    CHECK(v9x_r128_ring_plan_make(0xd8000000ul, 0x00100000ul, &plan) ==
          V9X_STATUS_OK);
    CHECK(plan.size_l2qw == 17ul);

    /* Refused: no address, not page aligned, not a power of two, too small
     * or too large, a ring (and its 32-dword wrap mirror) running past the
     * window's end, and no plan to fill. */
    CHECK(v9x_r128_ring_plan_make(0ul, RING_64K, &plan) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_r128_ring_plan_make(0xd9c00800ul, RING_64K, &plan) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_r128_ring_plan_make(0xd9c00000ul, 0x00018000ul, &plan) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_r128_ring_plan_make(0xd9c00000ul, 0x1000ul, &plan) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_r128_ring_plan_make(0xd9c00000ul, 0x00200000ul, &plan) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_r128_ring_plan_make(0xd9ff0000ul, RING_64K, &plan) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_r128_ring_plan_make(0xd9fe0000ul, RING_64K, &plan) ==
          V9X_STATUS_OK);
    CHECK(v9x_r128_ring_plan_make(0xd9c00000ul, RING_64K, 0) ==
          V9X_STATUS_INVALID_ARGUMENT);

    /* Free dwords, one kept empty so full and empty differ. */
    CHECK(v9x_r128_ring_free(0ul, 0ul, 0x3ffful) == 0x3ffful);
    CHECK(v9x_r128_ring_free(10ul, 10ul, 0x3ffful) == 0x3ffful);
    CHECK(v9x_r128_ring_free(10ul, 9ul, 0x3ffful) == 0ul);
    CHECK(v9x_r128_ring_free(0ul, 0x3ffful, 0x3ffful) == 0ul);
    CHECK(v9x_r128_ring_free(100ul, 40ul, 0x3ffful) == 59ul);
    CHECK(v9x_r128_ring_free(40ul, 100ul, 0x3ffful) == 0x3ffful - 60ul);

    /* The bus control A8U4I5 ran the ring with: ours read 7033A340 and
     * ATI's 5033A300, bus mastering on (bit 6 clear) and bit 29 clear. */
    CHECK(v9x_r128_ring_bus_cntl(0x7033a340ul) == 0x5033a300ul);
    CHECK(v9x_r128_ring_bus_cntl(0x5033a300ul) == 0x5033a300ul);
    /* AGP_CNTL: ours 00700000, ATI's 00000038 - a 32 MiB window and the
     * FIFO sensing bits 22:20 clear; the other bits kept. */
    CHECK(v9x_r128_ring_agp_cntl(0x00700000ul) == 0x00000038ul);
    CHECK(v9x_r128_ring_agp_cntl(0x0070003ful) == 0x00000038ul);
    CHECK(v9x_r128_ring_agp_cntl(0x00810000ul) == 0x00810038ul);

    /* AGP_COMMAND from the register file's configuration copy: A8U4I5's
     * 1F000302 has AGP_EN (bit 8). */
    CHECK(v9x_r128_ring_agp_enabled(0x1f000302ul));
    CHECK(!v9x_r128_ring_agp_enabled(0x1f000002ul));
    CHECK(!v9x_r128_ring_agp_enabled(0xfffffffful));

    /* The reservation behind the ring: the vertex area after the ring and
     * its mirror page (asserted at compile time above), inside the window
     * with the ring. */
    CHECK(v9x_r128_ring_plan_make(0xda000000ul, RING_64K, &plan) ==
          V9X_STATUS_OK);
    CHECK(plan.buffer_offset + V9X_R128_RING_RESERVE_BYTES <=
          V9X_R128_RING_WINDOW_BYTES);

    test_vertex_area();
    return failures;
}
