#include <stdio.h>
#include <string.h>

#include "velocity9x/intel_gma.h"

static unsigned int failures = 0u;

#define CHECK(expression) do { \
    if (!(expression)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #expression); \
        ++failures; \
    } \
} while (0)

static void make_native_snapshot(struct v9x_i9xx_mmio_snapshot *snapshot)
{
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->pgtbl_ctl = 0x7f840001ul;
    snapshot->pipe[1].pipe_conf = V9X_I9XX_PIPECONF_ENABLE;
    snapshot->pipe[1].htotal = ((v9x_u32)(1344u - 1u) << 16) | (1024u - 1u);
    snapshot->pipe[1].vtotal = ((v9x_u32)(672u - 1u) << 16) | (576u - 1u);
    snapshot->pipe[1].pipe_src = ((v9x_u32)(1024u - 1u) << 16) | (576u - 1u);
    snapshot->pipe[1].plane_control = V9X_I9XX_DSPCNTR_ENABLE |
                                      (5ul << 26) | (1ul << 24);
    snapshot->pipe[1].plane_address = 0x00100000ul;
    snapshot->pipe[1].plane_stride = 2048ul;
}

static void test_native_pipe_b(void)
{
    struct v9x_i9xx_mmio_snapshot first;
    struct v9x_i9xx_mmio_snapshot second;
    struct v9x_i9xx_mode_expectation expected;
    struct v9x_i9xx_fingerprint result;
    v9x_u16 all = V9X_I9XX_FP_STABLE | V9X_I9XX_FP_NONTRIVIAL |
                  V9X_I9XX_FP_LIVE_PIPE | V9X_I9XX_FP_TIMING_VALID |
                  V9X_I9XX_FP_SOURCE_MATCH | V9X_I9XX_FP_PLANE_MATCH |
                  V9X_I9XX_FP_RING_QUIESCENT;
    v9x_u16 phase1 = all & (v9x_u16)~V9X_I9XX_FP_RING_QUIESCENT;

    CHECK(V9X_I9XX_FP_PHASE1_REQUIRED == phase1);

    make_native_snapshot(&first);
    second = first;
    expected.width = 1024u;
    expected.height = 576u;
    expected.bits_per_pixel = 16u;
    expected.pitch_bytes = 2048u;
    expected.gmadr_aperture_bytes = 256ul * 1024ul * 1024ul;

    CHECK(v9x_i9xx_analyze_fingerprint(&first, &second, &expected, &result) ==
          V9X_STATUS_OK);
    CHECK(result.flags == all);
    CHECK(result.live_pipe == 1u);
    CHECK(result.timing_width == 1024u && result.total_width == 1344u);
    CHECK(result.timing_height == 576u && result.total_height == 672u);
    CHECK(result.source_width == 1024u && result.source_height == 576u);
    CHECK(result.plane_bits_per_pixel == 16u);
    CHECK(result.plane_stride == 2048u);
    CHECK(result.plane_address == 0x00100000ul);
}

/* Gen3 has a free plane-to-pipe mapping and mobile VBIOS commonly drives the
 * LVDS on pipe B through plane A. The decoder must pair the enabled pipe with
 * whichever enabled plane selects it, not assume plane N belongs to pipe N. */
static void test_plane_a_on_pipe_b(void)
{
    struct v9x_i9xx_mmio_snapshot first;
    struct v9x_i9xx_mmio_snapshot second;
    struct v9x_i9xx_mode_expectation expected;
    struct v9x_i9xx_fingerprint result;

    make_native_snapshot(&first);
    /* Move the plane registers to plane A, still selecting pipe B (1 << 24);
     * pipe B keeps its timing and PIPECONF; plane B is left disabled. */
    first.pipe[0].plane_control = first.pipe[1].plane_control;
    first.pipe[0].plane_address = first.pipe[1].plane_address;
    first.pipe[0].plane_stride = first.pipe[1].plane_stride;
    first.pipe[1].plane_control = 0ul;
    first.pipe[1].plane_address = 0ul;
    first.pipe[1].plane_stride = 0ul;
    second = first;
    expected.width = 1024u;
    expected.height = 576u;
    expected.bits_per_pixel = 16u;
    expected.pitch_bytes = 2048u;
    expected.gmadr_aperture_bytes = 256ul * 1024ul * 1024ul;

    CHECK(v9x_i9xx_analyze_fingerprint(&first, &second, &expected, &result) ==
          V9X_STATUS_OK);
    CHECK(result.live_pipe == 1u);
    CHECK((result.flags & V9X_I9XX_FP_PHASE1_REQUIRED) ==
          V9X_I9XX_FP_PHASE1_REQUIRED);
    CHECK(result.live_plane == 0u);
    CHECK(result.timing_width == 1024u && result.source_height == 576u);
    CHECK(result.plane_bits_per_pixel == 16u);
    CHECK(result.plane_stride == 2048u);
    CHECK(result.plane_address == 0x00100000ul);
}

static void test_deltas_and_contradictions(void)
{
    struct v9x_i9xx_mmio_snapshot first;
    struct v9x_i9xx_mmio_snapshot second;
    struct v9x_i9xx_mode_expectation expected;
    struct v9x_i9xx_fingerprint result;

    make_native_snapshot(&first);
    second = first;
    second.ring_head = 8ul;
    expected.width = 640u;
    expected.height = 480u;
    expected.bits_per_pixel = 8u;
    expected.pitch_bytes = 640u;
    expected.gmadr_aperture_bytes = 0x00080000ul;

    CHECK(v9x_i9xx_analyze_fingerprint(&first, &second, &expected, &result) ==
          V9X_STATUS_OK);
    CHECK((result.flags & V9X_I9XX_FP_STABLE) == 0u);
    CHECK((result.flags & V9X_I9XX_FP_SOURCE_MATCH) == 0u);
    CHECK((result.flags & V9X_I9XX_FP_PLANE_MATCH) == 0u);
    CHECK((result.flags & V9X_I9XX_FP_RING_QUIESCENT) != 0u);

    first.ring_ctl = V9X_I9XX_RING_CTL_VALID;
    second = first;
    CHECK(v9x_i9xx_analyze_fingerprint(&first, &second, &expected, &result) ==
          V9X_STATUS_OK);
    CHECK((result.flags & V9X_I9XX_FP_RING_QUIESCENT) == 0u);
}

static void test_refusals_and_ambiguity(void)
{
    struct v9x_i9xx_mmio_snapshot first;
    struct v9x_i9xx_mmio_snapshot second;
    struct v9x_i9xx_mode_expectation expected;
    struct v9x_i9xx_fingerprint result;

    make_native_snapshot(&first);
    second = first;
    expected.width = 1024u;
    expected.height = 576u;
    expected.bits_per_pixel = 16u;
    expected.pitch_bytes = 2048u;
    expected.gmadr_aperture_bytes = 256ul * 1024ul * 1024ul;

    first.pipe[0] = first.pipe[1];
    first.pipe[0].plane_control &= ~V9X_I9XX_DSPCNTR_PIPE_MASK;
    second = first;
    CHECK(v9x_i9xx_analyze_fingerprint(&first, &second, &expected, &result) ==
          V9X_STATUS_OK);
    CHECK(result.live_pipe == V9X_I9XX_PIPE_NONE);
    CHECK((result.flags & V9X_I9XX_FP_LIVE_PIPE) == 0u);

    memset(&first, 0, sizeof(first));
    second = first;
    CHECK(v9x_i9xx_analyze_fingerprint(&first, &second, &expected, &result) ==
          V9X_STATUS_OK);
    CHECK((result.flags & V9X_I9XX_FP_NONTRIVIAL) == 0u);

    CHECK(v9x_i9xx_analyze_fingerprint(0, &second, &expected, &result) ==
          V9X_STATUS_INVALID_ARGUMENT);
    expected.gmadr_aperture_bytes = 0ul;
    CHECK(v9x_i9xx_analyze_fingerprint(&first, &second, &expected, &result) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

unsigned int v9x_run_i9xx_mmio_tests(void)
{
    failures = 0u;
    test_native_pipe_b();
    test_plane_a_on_pipe_b();
    test_deltas_and_contradictions();
    test_refusals_and_ambiguity();
    return failures;
}
