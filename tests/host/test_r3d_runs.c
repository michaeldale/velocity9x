/*
 * Tests for the record-run accounting behind the DrawPrimitives merge
 * counters (src\display32\r3d\r3d_runs.h).
 */
#include <stdio.h>

#include "../../src/display32/r3d/r3d_runs.h"

static unsigned int runs_failures = 0u;

#define RCHECK(expression) do { \
    if (!(expression)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #expression); \
        ++runs_failures; \
    } \
} while (0)

/* The engine bound the D3D core uses (V9X_D3D_INDEXED_BATCH). */
#define TEST_CAPACITY 64ul

/* The first record of a buffer always starts a batch, state or not. */
static void test_first_record_starts_a_run(void)
{
    v9x_u32 run = 0ul;

    RCHECK(v9x_r3d_record_run_step(&run, 0ul, 2ul, TEST_CAPACITY) == V9X_TRUE);
    RCHECK(run == 2ul);
    run = 0ul;
    RCHECK(v9x_r3d_record_run_step(&run, 5ul, 2ul, TEST_CAPACITY) == V9X_TRUE);
    RCHECK(run == 2ul);
}

/* State-free records join the run and grow it. */
static void test_state_free_records_join(void)
{
    v9x_u32 run = 0ul;

    RCHECK(v9x_r3d_record_run_step(&run, 0ul, 2ul, TEST_CAPACITY) == V9X_TRUE);
    RCHECK(v9x_r3d_record_run_step(&run, 0ul, 3ul, TEST_CAPACITY) == V9X_FALSE);
    RCHECK(v9x_r3d_record_run_step(&run, 0ul, 1ul, TEST_CAPACITY) == V9X_FALSE);
    RCHECK(run == 6ul);
}

/* Any state change ends the run: the pairs apply before the vertices. */
static void test_state_change_breaks(void)
{
    v9x_u32 run = 0ul;

    (void)v9x_r3d_record_run_step(&run, 0ul, 2ul, TEST_CAPACITY);
    RCHECK(v9x_r3d_record_run_step(&run, 1ul, 2ul, TEST_CAPACITY) == V9X_TRUE);
    RCHECK(run == 2ul);
}

/* The run never exceeds capacity; exactly filling it still joins. */
static void test_capacity_bound(void)
{
    v9x_u32 run = 0ul;

    (void)v9x_r3d_record_run_step(&run, 0ul, 60ul, TEST_CAPACITY);
    RCHECK(v9x_r3d_record_run_step(&run, 0ul, 4ul, TEST_CAPACITY) == V9X_FALSE);
    RCHECK(run == 64ul);
    RCHECK(v9x_r3d_record_run_step(&run, 0ul, 1ul, TEST_CAPACITY) == V9X_TRUE);
    RCHECK(run == 1ul);
}

/* A record already over capacity stands alone, and nothing joins it - the
 * subtraction must not wrap. */
static void test_oversized_record_stands_alone(void)
{
    v9x_u32 run = 0ul;

    RCHECK(v9x_r3d_record_run_step(&run, 0ul, 190ul, TEST_CAPACITY) == V9X_TRUE);
    RCHECK(v9x_r3d_record_run_step(&run, 0ul, 1ul, TEST_CAPACITY) == V9X_TRUE);
    RCHECK(run == 1ul);
}

unsigned int v9x_run_r3d_runs_tests(void)
{
    runs_failures = 0u;
    test_first_record_starts_a_run();
    test_state_free_records_join();
    test_state_change_breaks();
    test_capacity_bound();
    test_oversized_record_stands_alone();
    if (runs_failures == 0u) {
        puts("PASS: DrawPrimitives record-run accounting");
    }
    return runs_failures;
}
