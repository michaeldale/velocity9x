#include <stdio.h>

#include "velocity9x/intel_gma.h"

static unsigned int failures = 0u;

#define CHECK(expression) do { \
    if (!(expression)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #expression); \
        ++failures; \
    } \
} while (0)

static void test_sandbox_layout(void)
{
    struct v9x_i9xx_sandbox_layout layout;

    CHECK(v9x_i9xx_sandbox_calculate(
              0x007b0000ul, 0x7f800000ul, &layout) == V9X_STATUS_OK);
    CHECK(layout.heap_bytes == 0x00790000ul);
    CHECK(layout.reserve_offset == 0x00790000ul);
    CHECK(layout.reserve_physical == 0x7ff90000ul);
    CHECK(layout.ring_offset == 0x00790000ul);
    CHECK(layout.ring_physical == 0x7ff90000ul);
    CHECK(layout.ring_bytes == 0x00010000ul);
    CHECK(layout.hws_offset == 0x007a0000ul);
    CHECK(layout.hws_physical == 0x7ffa0000ul);
    CHECK(layout.scratch_offset == 0x007a1000ul);
    CHECK(layout.scratch_physical == 0x7ffa1000ul);
    CHECK(layout.scratch_bytes == 0x1000ul);
    CHECK(layout.scratch_offset + layout.scratch_bytes <= 0x007b0000ul);

    CHECK(v9x_i9xx_sandbox_calculate(0x1fffful, 0ul, &layout) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(layout.heap_bytes == 0ul);
    CHECK(v9x_i9xx_sandbox_calculate(0x20001ul, 0ul, &layout) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_i9xx_sandbox_calculate(
              0x00200000ul, 0xfff00000ul, &layout) ==
          V9X_STATUS_INTEGER_OVERFLOW);
    CHECK(v9x_i9xx_sandbox_calculate(0x20000ul, 0ul, 0) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

static void test_ring_space_and_wrap(void)
{
    struct v9x_i9xx_ring_plan plan;
    v9x_u32 free_bytes;

    CHECK(v9x_i9xx_ring_free_space(0ul, 0ul, 0x10000ul, &free_bytes) ==
          V9X_STATUS_OK);
    CHECK(free_bytes == 0xfff8ul);
    CHECK(v9x_i9xx_ring_free_space(0x100ul, 0xfff8ul, 0x10000ul,
                                    &free_bytes) == V9X_STATUS_OK);
    CHECK(free_bytes == 0x100ul);
    CHECK(v9x_i9xx_ring_free_space(0ul, 0xfff8ul, 0x10000ul, &free_bytes) ==
          V9X_STATUS_OK);
    CHECK(free_bytes == 0ul);

    CHECK(v9x_i9xx_ring_plan(0ul, 0ul, 0x10000ul, 2ul, &plan) ==
          V9X_STATUS_OK);
    CHECK(plan.command_tail == 0ul && plan.next_tail == 8ul);
    CHECK(plan.pad_dwords == 0ul && plan.consumed_bytes == 8ul);

    CHECK(v9x_i9xx_ring_plan(0x100ul, 0xfff8ul, 0x10000ul, 6ul, &plan) ==
          V9X_STATUS_OK);
    CHECK(plan.command_tail == 0ul && plan.next_tail == 24ul);
    CHECK(plan.pad_dwords == 2ul && plan.command_dwords == 6ul);
    CHECK(plan.consumed_bytes == 32ul);

    CHECK(v9x_i9xx_ring_plan(0ul, 0xfff8ul, 0x10000ul, 2ul, &plan) ==
          V9X_STATUS_INSUFFICIENT_MEMORY);
    CHECK(v9x_i9xx_ring_plan(0ul, 0ul, 0x10000ul, 1ul, &plan) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_i9xx_ring_free_space(1ul, 0ul, 0x10000ul, &free_bytes) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_i9xx_ring_free_space(0ul, 0ul, 0x18000ul, &free_bytes) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

static void test_packet_builders_and_decoder(void)
{
    v9x_u32 stream[8];
    v9x_u32 written;
    const v9x_u32 scratch = 0x007a1000ul;

    CHECK(v9x_i9xx_build_mi_probe(stream, 8ul, &written) == V9X_STATUS_OK);
    CHECK(written == 2ul);
    CHECK(stream[0] == 0x00000000ul && stream[1] == 0x02000000ul);
    CHECK(v9x_i9xx_decode_phase4_stream(
              stream, written, scratch, 0x1000ul) == V9X_STATUS_OK);

    CHECK(v9x_i9xx_build_color_blt(
              scratch + 0x100ul, 8u, 8u, 32u, 0x55aa33ccul,
              scratch, 0x1000ul, stream, 8ul, &written) == V9X_STATUS_OK);
    CHECK(written == 6ul);
    CHECK(stream[0] == 0x54300004ul);
    CHECK(stream[1] == 0x03f00020ul);
    CHECK(stream[2] == 0ul && stream[3] == 0x00080008ul);
    CHECK(stream[4] == scratch + 0x100ul);
    CHECK(stream[5] == 0x55aa33ccul);
    CHECK(v9x_i9xx_decode_phase4_stream(
              stream, written, scratch, 0x1000ul) == V9X_STATUS_OK);

    stream[0] ^= 1ul;
    CHECK(v9x_i9xx_decode_phase4_stream(
              stream, written, scratch, 0x1000ul) == V9X_STATUS_UNSUPPORTED);
    stream[0] ^= 1ul;
    stream[4] = scratch + 0xff0ul;
    CHECK(v9x_i9xx_decode_phase4_stream(
              stream, written, scratch, 0x1000ul) ==
          V9X_STATUS_INSUFFICIENT_MEMORY);

    CHECK(v9x_i9xx_build_color_blt(
              scratch + 0xff0ul, 8u, 8u, 32u, 0ul,
              scratch, 0x1000ul, stream, 8ul, &written) ==
          V9X_STATUS_INSUFFICIENT_MEMORY);
    CHECK(written == 0ul);
    CHECK(v9x_i9xx_build_color_blt(
              scratch, 9u, 1u, 32u, 0ul,
              scratch, 0x1000ul, stream, 8ul, &written) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_i9xx_build_mi_probe(stream, 1ul, &written) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_i9xx_decode_phase4_stream(
              stream, 1ul, scratch, 0x1000ul) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

unsigned int v9x_run_i9xx_ring_tests(void)
{
    test_sandbox_layout();
    test_ring_space_and_wrap();
    test_packet_builders_and_decoder();
    return failures;
}
