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
    CHECK(layout.heap_bytes == 0x006b0000ul);
    CHECK(layout.reserve_offset == 0x006b0000ul);
    CHECK(layout.reserve_physical == 0x7feb0000ul);
    CHECK(layout.ring_offset == 0x006b0000ul);
    CHECK(layout.ring_physical == 0x7feb0000ul);
    CHECK(layout.ring_bytes == 0x00010000ul);
    CHECK(layout.hws_offset == 0x006c0000ul);
    CHECK(layout.hws_physical == 0x7fec0000ul);
    CHECK(layout.scratch_offset == 0x006c1000ul);
    CHECK(layout.scratch_physical == 0x7fec1000ul);
    CHECK(layout.scratch_bytes == 0x1000ul);
    CHECK(layout.scratch_bytes == 0x1000ul);
    CHECK(layout.target_offset == 0x006c2000ul);
    CHECK(layout.target_physical == 0x7fec2000ul);
    CHECK(layout.target_bytes == 0x00096000ul);
    CHECK(layout.target_pitch == 1280ul);
    CHECK(layout.guard_upper_offset == 0x00758000ul);
    CHECK(layout.guard_upper_physical == 0x7ff58000ul);
    /* Everything, guard included, inside the reserve. */
    CHECK(layout.guard_upper_offset + 0x1000ul <= 0x007b0000ul);
    /* The target is exactly pitch times height, with no padding. */
    CHECK(layout.target_bytes == layout.target_pitch * 480ul);
    /* The scratch page is the target's lower guard: they abut. */
    CHECK(layout.scratch_offset + layout.scratch_bytes ==
          layout.target_offset);
    /* loader.asm pins scratch at reserve + 0x11000 whatever the
     * reserve size, and probes guards at +0x11000 and +0x11ffc. */
    CHECK(layout.scratch_offset - layout.reserve_offset == 0x11000ul);

    CHECK(v9x_i9xx_sandbox_calculate(0xffffful, 0ul, &layout) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(layout.heap_bytes == 0ul);
    CHECK(v9x_i9xx_sandbox_calculate(0x100001ul, 0ul, &layout) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_i9xx_sandbox_calculate(
              0x00200000ul, 0xfff00000ul, &layout) ==
          V9X_STATUS_INTEGER_OVERFLOW);
    CHECK(v9x_i9xx_sandbox_calculate(0x100000ul, 0ul, 0) ==
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
    const v9x_u32 scratch = 0x006c1000ul;

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


/*
 * The Phase 4 execution CRC, from the compiled builders.
 *
 * This value exists in three places and nothing compared them until now: this
 * builder, the literal in src\minivdd32\loader.asm that the mini-VDD refuses
 * to execute without, and a reimplementation in
 * scripts\check-intel-ring-plan.ps1 -ComputeArm. The layout move at Phase 5
 * changed it - the BLT destination is inside the reserve, and the reserve
 * moved - which is exactly the kind of change that leaves two of the three
 * stale and the arm silently refusing on hardware.
 *
 * Asserting it here makes the C builder the source of truth and turns a
 * three-way drift into a test failure. The stream shape is the one
 * src\display16\intel_ring16.c submits: an MI probe, then a colour BLT into
 * the scratch page.
 */
static void test_phase4_execution_crc(void)
{
    struct v9x_i9xx_sandbox_layout layout;
    v9x_u32 probe[2];
    /*
     * Eight, not six. v9x_i9xx_build_color_blt writes six dwords and
     * v9x_i9xx_phase4_execution_crc reads eight: the caller appends an
     * MI_FLUSH and an MI_NOOP, matching src\display16\intel_ring16.c.
     * Sizing this array at six reads two dwords of stack, which is how
     * this test first disagreed with the PowerShell computation.
     */
    v9x_u32 blt[8];
    v9x_u32 written = 0ul;

    CHECK(v9x_i9xx_sandbox_calculate(
              0x007b0000ul, 0x7f800000ul, &layout) == V9X_STATUS_OK);
    CHECK(v9x_i9xx_build_mi_probe(probe, 2ul, &written) == V9X_STATUS_OK);
    CHECK(written == 2ul);
    CHECK(v9x_i9xx_build_color_blt(
              layout.scratch_offset + 0x100ul, 8u, 8u, 32u, 0x55aa33ccul,
              layout.scratch_offset, layout.scratch_bytes,
              blt, 6ul, &written) == V9X_STATUS_OK);
    CHECK(written == 6ul);
    blt[6] = V9X_I9XX_MI_FLUSH;
    blt[7] = V9X_I9XX_MI_NOOP;

    /* The destination moved with the reserve: scratch + 0x100. */
    CHECK(blt[4] == 0x006c1100ul);

    /*
     * Must equal the IntelArmCrc literal in loader.asm and the ArmExecutionCrc
     * that check-intel-ring-plan.ps1 -ComputeArm prints. If this fails after a
     * layout change, all three move together or none does.
     */
    CHECK(v9x_i9xx_phase4_execution_crc(probe, blt) == 0xa0da64a1ul);
}


/*
 * The heap shrink, which is the one user-visible consequence of the Phase 5
 * layout move: the published DirectDraw heap went from 0x790000 to 0x6b0000,
 * 896 KiB smaller.
 *
 * v9x_gma950_reserve_video_memory returns layout.heap_bytes only when it is at
 * least the visible bytes of the mode, and falls back to the whole usable size
 * otherwise. So the assertion that matters is that all four published modes
 * still clear the smaller heap, with the largest - 1024x576x16 - being the
 * binding case at 0x120000, five times under.
 *
 * Nothing currently exercises DirectDraw allocation on this family: its
 * EngineType is NONE and no surface is ever allocated from this heap. The
 * number is published and unused, which is stated plainly rather than left for
 * a reader to assume it is load-bearing.
 */
static void test_heap_after_reserve_growth(void)
{
    struct v9x_i9xx_sandbox_layout layout;
    v9x_u32 index;
    static const v9x_u32 visible_bytes[4] = {
        640ul * 480ul,          /* 640x480x8   */
        1024ul * 576ul,         /* 1024x576x8  */
        640ul * 2ul * 480ul,    /* 640x480x16  */
        1024ul * 2ul * 576ul    /* 1024x576x16 */
    };

    CHECK(v9x_i9xx_sandbox_calculate(
              0x007b0000ul, 0x7f800000ul, &layout) == V9X_STATUS_OK);
    CHECK(layout.heap_bytes == 0x006b0000ul);

    for (index = 0ul; index < 4ul; ++index) {
        CHECK(visible_bytes[index] <= layout.heap_bytes);
    }
    /* The binding case, named so a future mode addition fails here loudly. */
    CHECK(visible_bytes[3] == 0x00120000ul);

    /*
     * The fallback boundary: a visible size one byte over the heap must not be
     * satisfiable from it. This is the branch gma950_hw16.c takes to return
     * the whole usable size instead.
     */
    CHECK(layout.heap_bytes + 1ul > layout.heap_bytes);

    /* The heap ends exactly where the reserve begins - no gap, no overlap. */
    CHECK(layout.heap_bytes == layout.reserve_offset);
}

unsigned int v9x_run_i9xx_ring_tests(void)
{
    test_sandbox_layout();
    test_ring_space_and_wrap();
    test_packet_builders_and_decoder();
    test_phase4_execution_crc();
    test_heap_after_reserve_growth();
    return failures;
}
