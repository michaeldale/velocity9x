/*
 * Tests for what the mini-VDD's reported counts and flags permit.
 *
 * These are the fixtures docs\plans\dynamic-vbe-pipeline.md wants written
 * before the ring-0 walk changes, in the form the host can actually hold. The
 * walk itself lives in assembly and is exercised by fault injection in a guest;
 * what is testable here is the other half of invariant 4 - the consumer
 * re-validating every count that crossed the ABI - and the publication rule
 * that decides whether a scan is allowed to contradict a family baseline row.
 *
 * The status combinations below are not hypothetical. A truncated list is what
 * a BIOS with more than 128 modes produces; a refused query is what the GMA950
 * produces for five of the seven standard numbers; a full cache is what any
 * BIOS listing more than 64 drivable modes produces; and "collection off" is
 * every S3 and Matrox package, by decision.
 */
#include <stdio.h>

#include "velocity9x/vbe_cache.h"

static unsigned int cache_failures = 0u;

#define CCHECK(expression) do { \
    if (!(expression)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #expression); \
        ++cache_failures; \
    } \
} while (0)

/* Everything went right: credible controller, valid terminated list. */
#define STATUS_GOOD ((v9x_u16)(V9X_VBE_ST_CTRL_VALID | V9X_VBE_ST_LIST_VALID | \
                               V9X_VBE_ST_LIST_TERM))

static void test_usable_count(void)
{
    CCHECK(v9x_vbe_scan_usable_count(STATUS_GOOD, 12u) == 12u);
    CCHECK(v9x_vbe_scan_usable_count(STATUS_GOOD, 0u) == 0u);

    /* A count past the cache bound means the two halves of the contract
     * disagree. Read the bound, never the claim. */
    CCHECK(v9x_vbe_scan_usable_count(STATUS_GOOD, V9X_VBE_CACHE_MAX) ==
           V9X_VBE_CACHE_MAX);
    CCHECK(v9x_vbe_scan_usable_count(STATUS_GOOD,
                                     (v9x_u16)(V9X_VBE_CACHE_MAX + 1u)) ==
           V9X_VBE_CACHE_MAX);
    CCHECK(v9x_vbe_scan_usable_count(STATUS_GOOD, 0xffffu) ==
           V9X_VBE_CACHE_MAX);

    /* Provenance failures contribute nothing regardless of the count. */
    CCHECK(v9x_vbe_scan_usable_count(
               (v9x_u16)(STATUS_GOOD | V9X_VBE_ST_COLLECT_OFF), 40u) == 0u);
    CCHECK(v9x_vbe_scan_usable_count(
               (v9x_u16)(STATUS_GOOD & ~V9X_VBE_ST_CTRL_VALID), 40u) == 0u);
    CCHECK(v9x_vbe_scan_usable_count(
               (v9x_u16)(STATUS_GOOD & ~V9X_VBE_ST_LIST_VALID), 40u) == 0u);
    CCHECK(v9x_vbe_scan_usable_count(0u, 40u) == 0u);

    /*
     * A refused 4F01h or a full cache does not invalidate the records that did
     * come back: each one was parsed from its own answer. They cost the right
     * to hide baseline rows, tested below, not the right to add modes.
     */
    CCHECK(v9x_vbe_scan_usable_count(
               (v9x_u16)(STATUS_GOOD | V9X_VBE_ST_QUERY_FAILED), 5u) == 5u);
    CCHECK(v9x_vbe_scan_usable_count(
               (v9x_u16)(STATUS_GOOD | V9X_VBE_ST_CACHE_FULL), 64u) == 64u);
    CCHECK(v9x_vbe_scan_usable_count(
               (v9x_u16)(STATUS_GOOD | V9X_VBE_ST_QUERY_LIMIT), 40u) == 40u);
}

static void test_may_contradict(void)
{
    /* The only shape that earns it: valid, terminated, nothing missing. */
    CCHECK(v9x_vbe_scan_may_contradict(STATUS_GOOD) == V9X_TRUE);
    /* Including an empty one - it contradicts nothing by itself. */
    CCHECK(v9x_vbe_scan_usable_count(STATUS_GOOD, 0u) == 0u);
    CCHECK(v9x_vbe_scan_may_contradict(STATUS_GOOD) == V9X_TRUE);

    /* No collection at all: this is every S3 and Matrox package. Their
     * baseline rows must all stay published. */
    CCHECK(v9x_vbe_scan_may_contradict(V9X_VBE_ST_COLLECT_OFF) == V9X_FALSE);
    CCHECK(v9x_vbe_scan_may_contradict(
               (v9x_u16)(STATUS_GOOD | V9X_VBE_ST_COLLECT_OFF)) == V9X_FALSE);

    /* No mini-VDD, or one whose controller query failed. */
    CCHECK(v9x_vbe_scan_may_contradict(0u) == V9X_FALSE);
    CCHECK(v9x_vbe_scan_may_contradict(
               (v9x_u16)(STATUS_GOOD & ~V9X_VBE_ST_CTRL_VALID)) == V9X_FALSE);

    /* An unterminated list: the walk hit its word bound instead of FFFFh, so
     * there is no way to know what was past it. */
    CCHECK(v9x_vbe_scan_may_contradict(
               (v9x_u16)(STATUS_GOOD & ~V9X_VBE_ST_LIST_TERM)) == V9X_FALSE);
    CCHECK(v9x_vbe_scan_may_contradict(
               (v9x_u16)(V9X_VBE_ST_CTRL_VALID | V9X_VBE_ST_LIST_VALID |
                         V9X_VBE_ST_LIST_OVERFLOW)) == V9X_FALSE);

    /* More than 128 listed modes, or a pointer that could not be followed. */
    CCHECK(v9x_vbe_scan_may_contradict(
               (v9x_u16)(STATUS_GOOD | V9X_VBE_ST_LIST_OVERFLOW)) == V9X_FALSE);
    CCHECK(v9x_vbe_scan_may_contradict(
               (v9x_u16)(STATUS_GOOD | V9X_VBE_ST_LIST_UNREACHED)) == V9X_FALSE);

    /* A malformed entry, a cache that filled, a query that was refused: each
     * means "there may be a mode I did not see", and each therefore protects
     * every baseline row. */
    CCHECK(v9x_vbe_scan_may_contradict(
               (v9x_u16)(STATUS_GOOD | V9X_VBE_ST_LIST_FLAGGED)) == V9X_FALSE);
    CCHECK(v9x_vbe_scan_may_contradict(
               (v9x_u16)(STATUS_GOOD | V9X_VBE_ST_CACHE_FULL)) == V9X_FALSE);
    CCHECK(v9x_vbe_scan_may_contradict(
               (v9x_u16)(STATUS_GOOD | V9X_VBE_ST_QUERY_FAILED)) == V9X_FALSE);
    CCHECK(v9x_vbe_scan_may_contradict(
               (v9x_u16)(STATUS_GOOD | V9X_VBE_ST_QUERY_LIMIT)) == V9X_FALSE);

    /*
     * EDID state is orthogonal. A monitor with no DDC must not be able to cost
     * a machine its discovered modes, and a valid EDID must not buy the scan
     * any authority it did not earn.
     */
    CCHECK(v9x_vbe_scan_may_contradict(
               (v9x_u16)(STATUS_GOOD | V9X_VBE_ST_EDID_NO_DDC)) == V9X_TRUE);
    CCHECK(v9x_vbe_scan_may_contradict(
               (v9x_u16)(STATUS_GOOD | V9X_VBE_ST_EDID_FAILED)) == V9X_TRUE);
    CCHECK(v9x_vbe_scan_may_contradict(
               (v9x_u16)(STATUS_GOOD | V9X_VBE_ST_EDID_VALID)) == V9X_TRUE);
    CCHECK(v9x_vbe_scan_may_contradict(
               (v9x_u16)(V9X_VBE_ST_EDID_VALID | V9X_VBE_ST_CTRL_VALID)) ==
           V9X_FALSE);
}

/*
 * The shape of the contract itself, asserted at compile time rather than run
 * time: every one of these is a constant expression, and a run-time check of a
 * constant is a branch the compiler can prove dead - which this project builds
 * with warnings as errors, so it would not compile. The typedef idiom is the
 * one include\velocity9x\win9x_ddraw_abi.h already uses for its ABI sizes.
 *
 * scripts\check-tree.ps1 catches a disagreement between these numbers and the
 * assembly half of the contract. What these catch is a change that keeps the
 * two files agreeing and still breaks an assumption - a cache bound the record
 * indexing cannot express, or EDID chunks that no longer cover the block.
 */
typedef char v9x_assert_cache_bound[V9X_VBE_CACHE_MAX == 64u ? 1 : -1];
typedef char v9x_assert_list_bound[V9X_VBE_MODE_LIST_MAX == 128u ? 1 : -1];
typedef char v9x_assert_query_bound[V9X_VBE_MODE_QUERY_MAX == 128u ? 1 : -1];
typedef char v9x_assert_probe_bound[V9X_VBE_BASELINE_PROBE_MAX == 16u ? 1 : -1];
/* The chunks have to cover block 0 exactly: a short last chunk would be read
 * as data, and an extra one as a block the BIOS never returned. */
typedef char v9x_assert_edid_chunks[
    V9X_VBE_EDID_CHUNKS * V9X_VBE_EDID_CHUNK_BYTES == V9X_VBE_EDID_BYTES
        ? 1 : -1];
typedef char v9x_assert_api_versions[
    V9X_VBE_API_V2 == V9X_VBE_API_V1 + 1u ? 1 : -1];
/* A record says where it came from and where its colour layout came from at the
 * same time, so the two groups of flags may not share a bit. */
typedef char v9x_assert_origin_flags[
    (V9X_VBE_RF_ORIGIN_LIST & V9X_VBE_RF_ORIGIN_PROBE) == 0u ? 1 : -1];
typedef char v9x_assert_mask_flags[
    (V9X_VBE_RF_MASKS_LINEAR & V9X_VBE_RF_MASKS_LEGACY) == 0u ? 1 : -1];
typedef char v9x_assert_flag_groups[
    ((V9X_VBE_RF_ORIGIN_LIST | V9X_VBE_RF_ORIGIN_PROBE) &
     (V9X_VBE_RF_MASKS_LINEAR | V9X_VBE_RF_MASKS_LEGACY |
      V9X_VBE_RF_LIN_STRIDE)) == 0u ? 1 : -1];

unsigned int v9x_run_vbe_cache_tests(void)
{
    test_usable_count();
    test_may_contradict();
    return cache_failures;
}
