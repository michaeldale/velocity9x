#include <stdio.h>
#include <string.h>

#include "velocity9x/ati_mach64_engine.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

struct fake_io {
    v9x_u32 status[16];
    v9x_u32 status_count;
    v9x_u32 status_index;
    v9x_u32 bus;
    v9x_u32 test;
    char events[128];
    v9x_u32 event_count;
    v9x_u32 write_offset[64];
    v9x_u32 write_value[64];
    v9x_u32 write_count;
};

static v9x_u32 fake_read(void *context, v9x_u32 offset)
{
    struct fake_io *fake = (struct fake_io *)context;
    fake->events[fake->event_count++] = 'R';
    if (offset == V9X_M64_BUS_CNTL) return fake->bus;
    if (offset == V9X_M64_GEN_TEST_CNTL) return fake->test;
    if (offset == V9X_M64_MEM_BUF_CNTL) return 0x12000000ul;
    if (fake->status_index < fake->status_count)
        return fake->status[fake->status_index++];
    return fake->status_count != 0ul
        ? fake->status[fake->status_count - 1ul] : 0ul;
}

static void fake_write(void *context, v9x_u32 offset, v9x_u32 value)
{
    struct fake_io *fake = (struct fake_io *)context;
    fake->events[fake->event_count++] = 'W';
    fake->write_offset[fake->write_count] = offset;
    fake->write_value[fake->write_count++] = value;
}

static void setup(struct v9x_m64_engine *engine, struct fake_io *fake,
                  v9x_u16 model)
{
    struct v9x_m64_io io;
    memset(fake, 0, sizeof(*fake));
    io.context = fake;
    io.read = fake_read;
    io.write = fake_write;
    CHECK(v9x_m64_engine_init(engine, &io, model) == V9X_STATUS_OK);
}

static void test_fifo_decode(void)
{
    CHECK(v9x_m64_fifo_free(V9X_M64_FIFO_VTB_PLUS, 0x00800001ul) == 128ul);
    CHECK(v9x_m64_fifo_free(V9X_M64_FIFO_PRE_VTB, 0ul) == 16ul);
    CHECK(v9x_m64_fifo_free(V9X_M64_FIFO_PRE_VTB, 0x0000000ful) == 12ul);
    CHECK(v9x_m64_fifo_free(V9X_M64_FIFO_PRE_VTB,
                            V9X_M64_FIFO_ERR) == 0ul);
}

static void test_exact_batch_has_no_inner_read(void)
{
    struct v9x_m64_engine engine;
    struct fake_io fake;
    v9x_u32 offsets[4] = { 0x500ul, 0x530ul, 0x6c8ul, 0x118ul };
    v9x_u32 values[4] = { 1ul, 2ul, 3ul, 4ul };
    setup(&engine, &fake, V9X_M64_FIFO_VTB_PLUS);
    fake.status[0] = 0x00080000ul;
    fake.status_count = 1ul;
    CHECK(v9x_m64_emit_batch(&engine, offsets, values, 4ul, 0ul) ==
          V9X_STATUS_OK);
    CHECK(fake.event_count == 5ul);
    CHECK(memcmp(fake.events, "RWWWW", 5u) == 0);
    CHECK(engine.fifo_reads == 1ul && engine.register_writes == 4ul);
    CHECK(engine.fifo_cached == 4ul && engine.shadow_count == 4ul);
    CHECK(v9x_m64_emit_batch(&engine, offsets, values, 4ul, 0ul) ==
          V9X_STATUS_OK);
    CHECK(fake.event_count == 9ul);
    CHECK(memcmp(fake.events + 5, "WWWW", 4u) == 0);
    CHECK(engine.fifo_reads == 1ul && engine.fifo_cached == 0ul);

    /* A trigger is emitted but never retained for reset replay. */
    offsets[0] = V9X_M64_DST_HEIGHT_WIDTH;
    CHECK(v9x_m64_emit_batch(&engine, offsets, values, 1ul, 0ul) ==
          V9X_STATUS_OK);
    CHECK(engine.shadow_count == 4ul);
    offsets[0] = V9X_M64_ONE_OVER_AREA;
    CHECK(v9x_m64_emit_batch(&engine, offsets, values, 1ul, 0ul) ==
          V9X_STATUS_OK);
    CHECK(engine.shadow_count == 4ul);
}

/*
 * The replay shadow keeps the first V9X_M64_SHADOW_ENTRIES registers in the
 * order first written and the latest value of each. A 3D batch writes more
 * than that, so the lookup is by register, not a search of the table.
 */
static void test_shadow_keeps_order_and_latest(void)
{
    struct v9x_m64_engine engine;
    struct fake_io fake;
    v9x_u32 offsets[40];
    v9x_u32 values[40];
    v9x_u32 index;

    setup(&engine, &fake, V9X_M64_FIFO_VTB_PLUS);
    fake.status[0] = 0x00400000ul;   /* 64 free */
    fake.status_count = 1ul;
    for (index = 0ul; index < 40ul; ++index) {
        offsets[index] = 0x400ul + index * 4ul;
        values[index] = index;
    }
    CHECK(v9x_m64_emit_batch(&engine, offsets, values, 40ul, 0ul) ==
          V9X_STATUS_OK);
    CHECK(engine.shadow_count == V9X_M64_SHADOW_ENTRIES);
    CHECK(engine.shadow[0].offset == 0x400ul &&
          engine.shadow[31].offset == 0x47cul);

    /* A rewrite updates its entry in place; one past the table is not
     * kept. */
    offsets[0] = 0x414ul;
    values[0] = 0xabcdul;
    offsets[1] = 0x4f0ul;
    values[1] = 0x1234ul;
    CHECK(v9x_m64_emit_batch(&engine, offsets, values, 2ul, 0ul) ==
          V9X_STATUS_OK);
    CHECK(engine.shadow_count == V9X_M64_SHADOW_ENTRIES);
    CHECK(engine.shadow[5].offset == 0x414ul &&
          engine.shadow[5].value == 0xabcdul);
    CHECK(engine.shadow[31].value == 31ul);
}

static void test_bounds_and_timeout(void)
{
    struct v9x_m64_engine engine;
    struct fake_io fake;
    setup(&engine, &fake, V9X_M64_FIFO_VTB_PLUS);
    fake.status[0] = 0x00010000ul;
    fake.status_count = 1ul;
    CHECK(v9x_m64_reserve(&engine, 2ul, 2ul) == V9X_STATUS_TIMEOUT);
    CHECK(engine.fifo_reads == 3ul && engine.fifo_timeouts == 1ul);
    CHECK(v9x_m64_reserve(&engine, 1024ul, 0ul) ==
          V9X_STATUS_INVALID_ARGUMENT);

    setup(&engine, &fake, V9X_M64_FIFO_PRE_VTB);
    fake.status[0] = V9X_M64_FIFO_ERR;
    fake.status_count = 1ul;
    CHECK(v9x_m64_reserve(&engine, 1ul, 0ul) ==
          V9X_STATUS_INVALID_STATE);
    CHECK(engine.quarantined == V9X_TRUE);
}

static void test_barrier_and_reset_order(void)
{
    struct v9x_m64_engine engine;
    struct fake_io fake;
    v9x_u32 offsets[2] = { 0x500ul, 0x530ul };
    v9x_u32 values[2] = { 0xaaaaul, 0xbbbbul };
    setup(&engine, &fake, V9X_M64_FIFO_VTB_PLUS);
    fake.status[0] = 0x00040000ul;
    fake.status_count = 1ul;
    CHECK(v9x_m64_emit_batch(&engine, offsets, values, 2ul, 0ul) ==
          V9X_STATUS_OK);

    memset(fake.events, 0, sizeof(fake.events));
    fake.event_count = 0ul;
    fake.status_index = 0ul;
    CHECK(v9x_m64_cpu_read_barrier(&engine, 0ul) == V9X_STATUS_OK);
    CHECK(memcmp(fake.events, "RRW", 3u) == 0);
    CHECK(fake.write_offset[fake.write_count - 1ul] ==
          V9X_M64_MEM_BUF_CNTL);
    CHECK((fake.write_value[fake.write_count - 1ul] &
           V9X_M64_INVALIDATE_RB_CACHE) != 0ul);

    memset(fake.events, 0, sizeof(fake.events));
    fake.event_count = 0ul;
    fake.status_index = 0ul;
    fake.bus = V9X_M64_BUS_HOST_ERR_INT_EN;
    fake.test = V9X_M64_GEN_GUI_RESETB | 0x20ul;
    fake.status[0] = 0x00040000ul;
    fake.status_count = 1ul;
    CHECK(v9x_m64_reset_replay(&engine, 0ul) == V9X_STATUS_OK);
    CHECK(engine.reset_count == 1ul && engine.quarantined == V9X_FALSE);
    CHECK(fake.write_offset[fake.write_count - 5ul] == V9X_M64_BUS_CNTL);
    CHECK(fake.write_offset[fake.write_count - 4ul] ==
          V9X_M64_GEN_TEST_CNTL);
    CHECK((fake.write_value[fake.write_count - 4ul] &
           V9X_M64_GEN_GUI_RESETB) == 0ul);
    CHECK((fake.write_value[fake.write_count - 3ul] &
           V9X_M64_GEN_GUI_RESETB) != 0ul);
    CHECK(fake.write_offset[fake.write_count - 2ul] == offsets[0]);
    CHECK(fake.write_offset[fake.write_count - 1ul] == offsets[1]);
}

static void test_fill_builder(void)
{
    struct v9x_m64_fill fill;
    v9x_u32 offsets[V9X_M64_FILL_DWORDS];
    v9x_u32 values[V9X_M64_FILL_DWORDS];
    v9x_u32 written = 99ul;
    v9x_u32 repair_offsets[V9X_M64_FILL_REPAIR_DWORDS];
    v9x_u32 repair_values[V9X_M64_FILL_REPAIR_DWORDS];
    fill.vram_bytes = 4ul * 1024ul * 1024ul;
    fill.target_offset = 0x00200000ul;
    fill.target_pitch_bytes = 128ul;
    fill.target_width = 64ul;
    fill.target_height = 32ul;
    fill.left = 8ul; fill.top = 8ul;
    fill.right = 24ul; fill.bottom = 24ul;
    fill.color = 0x0000f81ful;
    CHECK(v9x_m64_build_fill(&fill, offsets, values,
                             V9X_M64_FILL_DWORDS, &written) == V9X_STATUS_OK);
    CHECK(written == 12ul);
    CHECK(offsets[0] == V9X_M64_DST_OFF_PITCH &&
          values[0] == 0x02040000ul);
    CHECK(offsets[10] == V9X_M64_DST_Y_X && values[10] == 0x00080008ul);
    CHECK(offsets[11] == V9X_M64_DST_HEIGHT_WIDTH &&
          values[11] == 0x00100010ul);
    CHECK(values[8] == 0x00000100ul && values[7] == 0x00070003ul);

    fill.target_offset++;
    CHECK(v9x_m64_build_fill(&fill, offsets, values, 12ul, &written) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(written == 0ul);
    fill.target_offset = 0x00200000ul;
    fill.right = 65ul;
    CHECK(v9x_m64_build_fill(&fill, offsets, values, 12ul, &written) ==
          V9X_STATUS_INVALID_ARGUMENT);
    fill.right = 24ul;
    fill.vram_bytes = 0x00200010ul;
    CHECK(v9x_m64_build_fill(&fill, offsets, values, 12ul, &written) ==
          V9X_STATUS_INSUFFICIENT_MEMORY);

    fill.vram_bytes = 4ul * 1024ul * 1024ul;
    fill.left = 0ul; fill.top = 0ul;
    fill.right = 32ul; fill.bottom = 16ul;
    CHECK(v9x_m64_build_fill_origin_repair(
              &fill, repair_offsets, repair_values,
              V9X_M64_FILL_REPAIR_DWORDS, &written) == V9X_STATUS_OK);
    CHECK(written == V9X_M64_FILL_REPAIR_DWORDS);
    CHECK(repair_offsets[0] == V9X_M64_DST_OFF_PITCH &&
          repair_values[0] == 0x0203fffeul);
    CHECK(repair_offsets[1] == V9X_M64_DST_Y_X &&
          repair_values[1] == 0x00080000ul);
    CHECK(repair_offsets[2] == V9X_M64_DST_HEIGHT_WIDTH &&
          repair_values[2] == 0x00010001ul);

    fill.left = 1ul;
    CHECK(v9x_m64_build_fill_origin_repair(
              &fill, repair_offsets, repair_values,
              V9X_M64_FILL_REPAIR_DWORDS, &written) == V9X_STATUS_OK);
    CHECK(written == 0ul);
    fill.left = 0ul; fill.target_offset = 8ul;
    CHECK(v9x_m64_build_fill_origin_repair(
              &fill, repair_offsets, repair_values,
              V9X_M64_FILL_REPAIR_DWORDS, &written) ==
          V9X_STATUS_UNSUPPORTED);
}

static void test_copy_builder(void)
{
    struct v9x_m64_copy copy;
    v9x_u32 offsets[V9X_M64_COPY_DWORDS];
    v9x_u32 values[V9X_M64_COPY_DWORDS];
    v9x_u32 written;
    memset(&copy, 0, sizeof(copy));
    copy.vram_bytes = 4ul * 1024ul * 1024ul;
    copy.source_offset = copy.destination_offset = 0x00200000ul;
    copy.source_pitch_bytes = copy.destination_pitch_bytes = 128ul;
    copy.source_width = copy.destination_width = 64ul;
    copy.source_height = copy.destination_height = 32ul;
    copy.width = 16ul; copy.height = 8ul;

    copy.source_left = 16ul; copy.source_top = 12ul;
    copy.destination_left = 12ul; copy.destination_top = 8ul;
    CHECK(v9x_m64_build_copy(&copy, offsets, values,
                             V9X_M64_COPY_DWORDS, &written) ==
          V9X_STATUS_OK);
    CHECK(written == 14ul && values[7] == 3ul);
    CHECK(values[10] == 0x0010000cul && values[12] == 0x000c0008ul);
    CHECK(values[8] == 0x003f0000ul && values[9] == 0x001f0000ul);
    CHECK(offsets[2] == V9X_M64_SRC_OFF_PITCH &&
          values[2] == 0x02040000ul);
    CHECK(values[1] == 0x00040404ul && values[4] == 0x00000300ul);

    copy.destination_left = 20ul;
    CHECK(v9x_m64_build_copy(&copy, offsets, values, 14ul, &written) ==
          V9X_STATUS_OK);
    CHECK(values[7] == 2ul);
    CHECK(values[10] == 0x001f000cul && values[12] == 0x00230008ul);

    copy.destination_left = 12ul; copy.destination_top = 16ul;
    CHECK(v9x_m64_build_copy(&copy, offsets, values, 14ul, &written) ==
          V9X_STATUS_OK);
    CHECK(values[7] == 1ul);
    CHECK(values[10] == 0x00100013ul && values[12] == 0x000c0017ul);

    copy.destination_left = 20ul;
    CHECK(v9x_m64_build_copy(&copy, offsets, values, 14ul, &written) ==
          V9X_STATUS_OK);
    CHECK(values[7] == 0ul);
    CHECK(values[10] == 0x001f0013ul && values[12] == 0x00230017ul);

    copy.destination_left = 60ul;
    CHECK(v9x_m64_build_copy(&copy, offsets, values, 14ul, &written) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(written == 0ul);
    copy.destination_left = 20ul;
    copy.destination_offset = 0x003ffff8ul;
    CHECK(v9x_m64_build_copy(&copy, offsets, values, 14ul, &written) ==
          V9X_STATUS_INSUFFICIENT_MEMORY);
}

static void test_flat_triangle_builder(void)
{
    struct v9x_m64_flat_triangle triangle;
    v9x_u32 offsets[V9X_M64_FLAT_TRIANGLE_DWORDS];
    v9x_u32 values[V9X_M64_FLAT_TRIANGLE_DWORDS];
    v9x_u32 written = 99ul;
    triangle.vertex[0].x = 8ul; triangle.vertex[0].y = 8ul;
    triangle.vertex[1].x = 24ul; triangle.vertex[1].y = 8ul;
    triangle.vertex[2].x = 8ul; triangle.vertex[2].y = 24ul;
    triangle.color = 0xffff00fful;
    CHECK(v9x_m64_build_flat_triangle(
              &triangle, offsets, values, V9X_M64_FLAT_TRIANGLE_DWORDS,
              &written) == V9X_STATUS_OK);
    CHECK(written == V9X_M64_FLAT_TRIANGLE_DWORDS);
    CHECK(offsets[0] == V9X_M64_VERTEX_1_S && values[0] == 0ul);
    CHECK(offsets[0] == 0x00000240ul);
    CHECK(offsets[1] == V9X_M64_VERTEX_1_T && values[1] == 0ul);
    CHECK(offsets[2] == V9X_M64_VERTEX_1_W &&
          values[2] == 0x3f800000ul);
    CHECK(offsets[3] == V9X_M64_VERTEX_1_Z &&
          values[3] == 0x7fff8000ul);
    CHECK(offsets[4] == V9X_M64_VERTEX_1_ARGB &&
          values[4] == triangle.color);
    CHECK(offsets[5] == V9X_M64_VERTEX_1_X_Y &&
          values[5] == 0x00200020ul);
    CHECK(offsets[11] == V9X_M64_VERTEX_2_X_Y &&
          values[11] == 0x00600020ul);
    CHECK(offsets[17] == V9X_M64_VERTEX_3_X_Y &&
          values[17] == 0x00200060ul);
    CHECK(offsets[18] == V9X_M64_ONE_OVER_AREA &&
          values[18] == 0x3b800000ul);
    CHECK(offsets[18] == 0x0000029cul);

    triangle.vertex[1].x = 8ul; triangle.vertex[1].y = 24ul;
    triangle.vertex[2].x = 24ul; triangle.vertex[2].y = 8ul;
    CHECK(v9x_m64_build_flat_triangle(
              &triangle, offsets, values, V9X_M64_FLAT_TRIANGLE_DWORDS,
              &written) == V9X_STATUS_OK);
    CHECK(values[18] == 0xbb800000ul);

    triangle.vertex[2].x = 8ul; triangle.vertex[2].y = 40ul;
    CHECK(v9x_m64_build_flat_triangle(
              &triangle, offsets, values, V9X_M64_FLAT_TRIANGLE_DWORDS,
              &written) == V9X_STATUS_INVALID_ARGUMENT);
    CHECK(written == 0ul);
    triangle.vertex[2].x = 16384ul;
    CHECK(v9x_m64_build_flat_triangle(
              &triangle, offsets, values, V9X_M64_FLAT_TRIANGLE_DWORDS,
              &written) == V9X_STATUS_INVALID_ARGUMENT);
}

static void test_flat_state_builder(void)
{
    struct v9x_m64_flat_state state;
    v9x_u32 offsets[V9X_M64_FLAT_STATE_DWORDS];
    v9x_u32 values[V9X_M64_FLAT_STATE_DWORDS];
    v9x_u32 written = 99ul;
    state.vram_bytes = 4ul * 1024ul * 1024ul;
    state.target_offset = 0x00200000ul;
    state.target_pitch_bytes = 128ul;
    state.target_width = 64ul;
    state.target_height = 32ul;
    state.scissor_left = 4ul; state.scissor_top = 5ul;
    state.scissor_right = 60ul; state.scissor_bottom = 28ul;
    CHECK(v9x_m64_build_flat_state(
              &state, offsets, values, V9X_M64_FLAT_STATE_DWORDS,
              &written) == V9X_STATUS_OK);
    CHECK(written == V9X_M64_FLAT_STATE_DWORDS);
    CHECK(offsets[0] == V9X_M64_DP_MIX && values[0] == 0x00070007ul);
    CHECK(offsets[1] == V9X_M64_DP_SRC && values[1] == 0x00000505ul);
    CHECK(offsets[4] == V9X_M64_SC_LEFT_RIGHT &&
          values[4] == 0x003b0004ul);
    CHECK(offsets[5] == V9X_M64_SC_TOP_BOTTOM &&
          values[5] == 0x001b0005ul);
    CHECK(offsets[6] == V9X_M64_DST_OFF_PITCH &&
          values[6] == 0x02040000ul);
    CHECK(offsets[7] == V9X_M64_Z_OFF_PITCH &&
          values[7] == values[6]);
    CHECK(offsets[8] == V9X_M64_Z_CNTL && values[8] == 0ul);
    CHECK(offsets[9] == V9X_M64_ALPHA_TST_CNTL && values[9] == 0ul);
    CHECK(offsets[10] == V9X_M64_SCALE_3D_CNTL &&
          values[10] == 0x000100c1ul);
    CHECK(offsets[13] == V9X_M64_DP_PIX_WIDTH &&
          values[13] == 0x40040444ul);
    CHECK(offsets[14] == V9X_M64_SETUP_CNTL && values[14] == 0x18ul);
    CHECK(offsets[14] == 0x00000304ul);
    CHECK(offsets[15] == V9X_M64_TEX_SIZE_PITCH && values[15] == 0ul);
    CHECK(offsets[16] == V9X_M64_TEX_CNTL && values[16] == 0ul);

    state.scissor_right = 65ul;
    CHECK(v9x_m64_build_flat_state(
              &state, offsets, values, V9X_M64_FLAT_STATE_DWORDS,
              &written) == V9X_STATUS_INVALID_ARGUMENT);
    CHECK(written == 0ul);
    state.scissor_right = 60ul; state.target_offset = 0x003ffff8ul;
    CHECK(v9x_m64_build_flat_state(
              &state, offsets, values, V9X_M64_FLAT_STATE_DWORDS,
              &written) == V9X_STATUS_INSUFFICIENT_MEMORY);
}

static void test_gouraud_builders(void)
{
    struct v9x_m64_flat_state state;
    struct v9x_m64_gouraud_triangle triangle;
    v9x_u32 state_offsets[V9X_M64_GOURAUD_STATE_DWORDS];
    v9x_u32 state_values[V9X_M64_GOURAUD_STATE_DWORDS];
    v9x_u32 setup_offsets[V9X_M64_GOURAUD_TRIANGLE_DWORDS];
    v9x_u32 setup_values[V9X_M64_GOURAUD_TRIANGLE_DWORDS];
    v9x_u32 written = 99ul;
    state.vram_bytes = 4ul * 1024ul * 1024ul;
    state.target_offset = 0x00200100ul;
    state.target_pitch_bytes = 128ul;
    state.target_width = 64ul;
    state.target_height = 28ul;
    state.scissor_left = 0ul; state.scissor_top = 0ul;
    state.scissor_right = 64ul; state.scissor_bottom = 28ul;
    CHECK(v9x_m64_build_gouraud_state(
              &state, state_offsets, state_values,
              V9X_M64_GOURAUD_STATE_DWORDS, &written) == V9X_STATUS_OK);
    CHECK(written == V9X_M64_GOURAUD_STATE_DWORDS);
    CHECK(state_offsets[14] == V9X_M64_SETUP_CNTL);
    CHECK(state_values[14] == V9X_M64_SETUP_GOURAUD);
    CHECK(state_values[10] == 0x000100c1ul);

    triangle.vertex[0].x = 8ul; triangle.vertex[0].y = 6ul;
    triangle.vertex[1].x = 40ul; triangle.vertex[1].y = 6ul;
    triangle.vertex[2].x = 8ul; triangle.vertex[2].y = 22ul;
    triangle.color[0] = 0xffff0000ul;
    triangle.color[1] = 0xff00ff00ul;
    triangle.color[2] = 0xff0000fful;
    CHECK(v9x_m64_build_gouraud_triangle(
              &triangle, setup_offsets, setup_values,
              V9X_M64_GOURAUD_TRIANGLE_DWORDS, &written) == V9X_STATUS_OK);
    CHECK(written == V9X_M64_GOURAUD_TRIANGLE_DWORDS);
    CHECK(setup_offsets[4] == V9X_M64_VERTEX_1_ARGB &&
          setup_values[4] == 0xffff0000ul);
    CHECK(setup_offsets[10] == V9X_M64_VERTEX_2_ARGB &&
          setup_values[10] == 0xff00ff00ul);
    CHECK(setup_offsets[16] == V9X_M64_VERTEX_3_ARGB &&
          setup_values[16] == 0xff0000fful);
    CHECK(setup_values[5] == 0x00200018ul);
    CHECK(setup_values[11] == 0x00a00018ul);
    CHECK(setup_values[17] == 0x00200058ul);
    CHECK(setup_values[18] == 0x3b000000ul);

    triangle.vertex[2].x = 8ul; triangle.vertex[2].y = 6ul;
    CHECK(v9x_m64_build_gouraud_triangle(
              &triangle, setup_offsets, setup_values,
              V9X_M64_GOURAUD_TRIANGLE_DWORDS, &written) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(written == 0ul);
}

static int z_compare_cpu(v9x_u32 compare, v9x_u16 incoming,
                         v9x_u16 stored)
{
    switch (compare) {
    case 1ul: return 0;
    case 2ul: return incoming < stored;
    case 3ul: return incoming == stored;
    case 4ul: return incoming <= stored;
    case 5ul: return incoming > stored;
    case 6ul: return incoming != stored;
    case 7ul: return incoming >= stored;
    case 8ul: return 1;
    default: return -1;
    }
}

static void test_z_control_truth_table(void)
{
    static const struct {
        v9x_u32 compare;
        v9x_u32 encoded;
        int low;
        int equal;
        int high;
    } cases[] = {
        { 1ul, 0x01ul, 0, 0, 0 },
        { 2ul, 0x11ul, 1, 0, 0 },
        { 3ul, 0x31ul, 0, 1, 0 },
        { 4ul, 0x21ul, 1, 1, 0 },
        { 5ul, 0x51ul, 0, 0, 1 },
        { 6ul, 0x61ul, 1, 0, 1 },
        { 7ul, 0x41ul, 0, 1, 1 },
        { 8ul, 0x71ul, 1, 1, 1 }
    };
    v9x_u32 value = 99ul;
    v9x_u32 index;
    for (index = 0ul; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        CHECK(v9x_m64_build_z_control(cases[index].compare, 0ul,
                                     &value) == V9X_STATUS_OK);
        CHECK(value == cases[index].encoded);
        CHECK(z_compare_cpu(cases[index].compare, 99u, 100u) ==
              cases[index].low);
        CHECK(z_compare_cpu(cases[index].compare, 100u, 100u) ==
              cases[index].equal);
        CHECK(z_compare_cpu(cases[index].compare, 101u, 100u) ==
              cases[index].high);
        CHECK(v9x_m64_build_z_control(cases[index].compare, 1ul,
                                     &value) == V9X_STATUS_OK);
        CHECK(value == (cases[index].encoded | 0x100ul));
    }
    CHECK(v9x_m64_build_z_control(0ul, 0ul, &value) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(value == 0ul);
    value = 99ul;
    CHECK(v9x_m64_build_z_control(9ul, 0ul, &value) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(value == 0ul);
    CHECK(v9x_m64_build_z_control(2ul, 2ul, &value) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_m64_build_z_control(2ul, 0ul, 0) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

static void test_alpha_control_truth_table(void)
{
    static const struct {
        v9x_u32 compare;
        v9x_u32 encoded;
        int low;
        int equal;
        int high;
    } cases[] = {
        { 1ul, 0x01ul, 0, 0, 0 },
        { 2ul, 0x11ul, 1, 0, 0 },
        { 3ul, 0x31ul, 0, 1, 0 },
        { 4ul, 0x21ul, 1, 1, 0 },
        { 5ul, 0x51ul, 0, 0, 1 },
        { 6ul, 0x61ul, 1, 0, 1 },
        { 7ul, 0x41ul, 0, 1, 1 },
        { 8ul, 0x71ul, 1, 1, 1 }
    };
    v9x_u32 value = 99ul;
    v9x_u32 index;
    for (index = 0ul; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        CHECK(v9x_m64_build_alpha_control(cases[index].compare, 127ul, 0ul,
                                         &value) == V9X_STATUS_OK);
        CHECK(value == (0x007f0000ul | cases[index].encoded));
        CHECK(z_compare_cpu(cases[index].compare, 126u, 127u) ==
              cases[index].low);
        CHECK(z_compare_cpu(cases[index].compare, 127u, 127u) ==
              cases[index].equal);
        CHECK(z_compare_cpu(cases[index].compare, 128u, 127u) ==
              cases[index].high);
        CHECK(v9x_m64_build_alpha_control(cases[index].compare, 255ul, 1ul,
                                         &value) == V9X_STATUS_OK);
        CHECK(value == (0x00ff1000ul | cases[index].encoded));
    }
    CHECK(v9x_m64_build_alpha_control(0ul, 0ul, 0ul, &value) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(value == 0ul);
    value = 99ul;
    CHECK(v9x_m64_build_alpha_control(9ul, 0ul, 0ul, &value) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(value == 0ul);
    CHECK(v9x_m64_build_alpha_control(2ul, 256ul, 0ul, &value) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_m64_build_alpha_control(2ul, 0ul, 2ul, &value) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_m64_build_alpha_control(2ul, 0ul, 0ul, 0) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

static void test_blend_control_table(void)
{
    static const struct { v9x_u32 factor; v9x_u32 encoded; } sources[] = {
        { 1ul, 0x00000000ul }, { 2ul, 0x00010000ul },
        { 5ul, 0x00040000ul }, { 6ul, 0x00050000ul },
        { 7ul, 0x00060000ul }, { 8ul, 0x00070000ul },
        { 9ul, 0x00020000ul }, { 10ul, 0x00030000ul },
        { 11ul, 0x00042000ul }
    };
    static const struct { v9x_u32 factor; v9x_u32 encoded; } destinations[] = {
        { 1ul, 0x00000000ul }, { 2ul, 0x00080000ul },
        { 3ul, 0x00100000ul }, { 4ul, 0x00180000ul },
        { 5ul, 0x00200000ul }, { 6ul, 0x00280000ul },
        { 7ul, 0x00300000ul }, { 8ul, 0x00380000ul }
    };
    v9x_u32 value = 99ul;
    v9x_u32 source;
    v9x_u32 destination;
    for (source = 0ul;
         source < sizeof(sources) / sizeof(sources[0]); ++source) {
        for (destination = 0ul;
             destination < sizeof(destinations) / sizeof(destinations[0]);
             ++destination) {
            CHECK(v9x_m64_build_blend_control(
                      sources[source].factor, destinations[destination].factor,
                      &value) == V9X_STATUS_OK);
            CHECK(value == (0x00000800ul | sources[source].encoded |
                            destinations[destination].encoded));
        }
    }
    CHECK(v9x_m64_build_blend_control(3ul, 1ul, &value) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(value == 0ul);
    value = 99ul;
    CHECK(v9x_m64_build_blend_control(4ul, 1ul, &value) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(value == 0ul);
    CHECK(v9x_m64_build_blend_control(2ul, 9ul, &value) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_m64_build_blend_control(2ul, 10ul, &value) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_m64_build_blend_control(2ul, 11ul, &value) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_m64_build_blend_control(0ul, 1ul, &value) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_m64_build_blend_control(12ul, 1ul, &value) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_m64_build_blend_control(2ul, 0ul, &value) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_m64_build_blend_control(2ul, 2ul, 0) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

static void test_depth_builders(void)
{
    struct v9x_m64_depth_state state;
    struct v9x_m64_depth_triangle triangle;
    v9x_u32 offsets[V9X_M64_FLAT_STATE_DWORDS];
    v9x_u32 values[V9X_M64_FLAT_STATE_DWORDS];
    v9x_u32 setup_offsets[V9X_M64_FLAT_TRIANGLE_DWORDS];
    v9x_u32 setup_values[V9X_M64_FLAT_TRIANGLE_DWORDS];
    v9x_u32 written = 99ul;
    state.color.vram_bytes = 4ul * 1024ul * 1024ul;
    state.color.target_offset = 0x00200100ul;
    state.color.target_pitch_bytes = 128ul;
    state.color.target_width = 64ul;
    state.color.target_height = 28ul;
    state.color.scissor_left = 0ul; state.color.scissor_top = 0ul;
    state.color.scissor_right = 64ul; state.color.scissor_bottom = 28ul;
    state.depth_offset = 0x00202100ul;
    state.depth_pitch_bytes = 128ul;
    state.depth_width = 64ul;
    state.depth_height = 28ul;
    state.compare = 4ul;
    state.write_enable = 0ul;
    CHECK(v9x_m64_build_depth_state(
              &state, offsets, values, V9X_M64_FLAT_STATE_DWORDS,
              &written) == V9X_STATUS_OK);
    CHECK(written == V9X_M64_FLAT_STATE_DWORDS);
    CHECK(offsets[7] == V9X_M64_Z_OFF_PITCH);
    CHECK(values[7] == 0x02040420ul);
    CHECK(offsets[8] == V9X_M64_Z_CNTL && values[8] == 0x21ul);

    state.write_enable = 1ul;
    CHECK(v9x_m64_build_depth_state(
              &state, offsets, values, V9X_M64_FLAT_STATE_DWORDS,
              &written) == V9X_STATUS_OK);
    CHECK(values[8] == 0x121ul);
    state.depth_offset = state.color.target_offset;
    CHECK(v9x_m64_build_depth_state(
              &state, offsets, values, V9X_M64_FLAT_STATE_DWORDS,
              &written) == V9X_STATUS_INVALID_ARGUMENT);
    CHECK(written == 0ul);
    state.depth_offset = 0x003ffff8ul;
    CHECK(v9x_m64_build_depth_state(
              &state, offsets, values, V9X_M64_FLAT_STATE_DWORDS,
              &written) == V9X_STATUS_INSUFFICIENT_MEMORY);

    triangle.vertex[0].x = 8ul; triangle.vertex[0].y = 6ul;
    triangle.vertex[1].x = 40ul; triangle.vertex[1].y = 6ul;
    triangle.vertex[2].x = 8ul; triangle.vertex[2].y = 22ul;
    triangle.color = 0xffff00fful;
    triangle.depth[0] = 0u;
    triangle.depth[1] = 0x8000u;
    triangle.depth[2] = 0xffffu;
    CHECK(v9x_m64_build_depth_triangle(
              &triangle, setup_offsets, setup_values,
              V9X_M64_FLAT_TRIANGLE_DWORDS, &written) == V9X_STATUS_OK);
    CHECK(written == V9X_M64_FLAT_TRIANGLE_DWORDS);
    CHECK(setup_values[3] == 0ul);
    CHECK(setup_values[9] == 0x80000000ul);
    CHECK(setup_values[15] == 0xffff0000ul);
}

static void test_texture_builders(void)
{
    struct v9x_m64_texture_state state;
    struct v9x_m64_textured_triangle triangle;
    v9x_u32 offsets[V9X_M64_TEXTURED_STATE_DWORDS];
    v9x_u32 values[V9X_M64_TEXTURED_STATE_DWORDS];
    v9x_u32 setup_offsets[V9X_M64_TEXTURED_TRIANGLE_DWORDS];
    v9x_u32 setup_values[V9X_M64_TEXTURED_TRIANGLE_DWORDS];
    v9x_u32 written = 99ul;
    v9x_u32 index;
    memset(&state, 0, sizeof(state));
    state.color.vram_bytes = 4ul * 1024ul * 1024ul;
    state.color.target_offset = 0x00200100ul;
    state.color.target_pitch_bytes = 128ul;
    state.color.target_width = 64ul;
    state.color.target_height = 28ul;
    state.color.scissor_right = 64ul;
    state.color.scissor_bottom = 28ul;
    state.texture_offset = 0x00204000ul;
    state.texture_pitch_bytes = 16ul;
    state.texture_width = 8ul;
    state.texture_height = 8ul;
    CHECK(v9x_m64_build_texture_state(
              &state, offsets, values, V9X_M64_TEXTURED_STATE_DWORDS,
              &written) == V9X_STATUS_OK);
    CHECK(written == V9X_M64_TEXTURED_STATE_DWORDS);
    CHECK(offsets[10] == V9X_M64_SCALE_3D_CNTL &&
          values[10] == 0x01010081ul);
    CHECK(values[13] == 0x40040444ul);
    CHECK(offsets[14] == V9X_M64_SETUP_CNTL && values[14] == 0ul);
    CHECK(offsets[15] == V9X_M64_TEX_SIZE_PITCH &&
          values[15] == 0x00000333ul);
    CHECK(offsets[16] == V9X_M64_TEX_CNTL &&
          values[16] == 0x40860000ul);
    CHECK(offsets[17] == V9X_M64_SECONDARY_TEX_OFF && values[17] == 0ul);
    CHECK(offsets[18] == V9X_M64_TEX_0_OFF + 12ul &&
          values[18] == 0x00204000ul);
    /* Only TEX_<max_log2>_OFF is written, so a minified draw that picked a
     * smaller level read an unwritten register: the Gateway's 64 and 256
     * minified scenes read the wrong half (2026-09-29). X.Org sets
     * MACH64_MIP_MAP_DISABLE on every texture (atimach64render.c). */
    CHECK((values[10] & V9X_M64_MIP_MAP_DISABLE) != 0ul);

    state.wrap_s = 1ul;
    state.wrap_t = 1ul;
    state.bilinear_min = 1ul;
    state.bilinear_mag = 1ul;
    CHECK(v9x_m64_build_texture_state(
              &state, offsets, values, V9X_M64_TEXTURED_STATE_DWORDS,
              &written) == V9X_STATUS_OK);
    CHECK(values[10] == 0x0b010081ul);
    CHECK(values[16] == 0x40800000ul);
    state.wrap_s = 0ul;
    state.wrap_t = 0ul;
    state.bilinear_min = 0ul;
    state.bilinear_mag = 0ul;

    state.texture_format = V9X_M64_TEXTURE_FORMAT_ARGB1555;
    CHECK(v9x_m64_build_texture_state(
              &state, offsets, values, V9X_M64_TEXTURED_STATE_DWORDS,
              &written) == V9X_STATUS_OK);
    CHECK(values[10] == 0x41010081ul);
    CHECK(values[13] == 0x30040444ul);
    state.texture_format = V9X_M64_TEXTURE_FORMAT_ARGB4444;
    CHECK(v9x_m64_build_texture_state(
              &state, offsets, values, V9X_M64_TEXTURED_STATE_DWORDS,
              &written) == V9X_STATUS_OK);
    CHECK(values[10] == 0x41010081ul);
    CHECK(values[13] == 0xf0040444ul);
    state.texture_format = V9X_M64_TEXTURE_FORMAT_RGB565;

    state.texture_width = 16ul;
    state.texture_pitch_bytes = 32ul;
    CHECK(v9x_m64_build_texture_state(
              &state, offsets, values, V9X_M64_TEXTURED_STATE_DWORDS,
              &written) == V9X_STATUS_OK);
    CHECK(values[15] == 0x00000344ul);
    CHECK(offsets[18] == V9X_M64_TEX_0_OFF + 16ul);
    state.texture_width = 12ul;
    CHECK(v9x_m64_build_texture_state(
              &state, offsets, values, V9X_M64_TEXTURED_STATE_DWORDS,
              &written) == V9X_STATUS_INVALID_ARGUMENT);
    CHECK(written == 0ul);
    state.texture_width = 8ul;
    state.texture_pitch_bytes = 15ul;
    CHECK(v9x_m64_build_texture_state(
              &state, offsets, values, V9X_M64_TEXTURED_STATE_DWORDS,
              &written) == V9X_STATUS_INVALID_ARGUMENT);
    state.texture_pitch_bytes = 16ul;
    /* A base on V9X_M64_TEXTURE_BASE_ALIGN (64) binds, and one off it
     * does not. */
    state.texture_offset = 0x00204040ul;
    CHECK(v9x_m64_build_texture_state(
              &state, offsets, values, V9X_M64_TEXTURED_STATE_DWORDS,
              &written) == V9X_STATUS_OK);
    state.texture_offset = 0x00204020ul;
    CHECK(v9x_m64_build_texture_state(
              &state, offsets, values, V9X_M64_TEXTURED_STATE_DWORDS,
              &written) == V9X_STATUS_INVALID_ARGUMENT);
    state.texture_offset = state.color.target_offset;
    CHECK(v9x_m64_build_texture_state(
              &state, offsets, values, V9X_M64_TEXTURED_STATE_DWORDS,
              &written) == V9X_STATUS_INVALID_ARGUMENT);
    state.texture_offset = 0x00400000ul;
    CHECK(v9x_m64_build_texture_state(
              &state, offsets, values, V9X_M64_TEXTURED_STATE_DWORDS,
              &written) == V9X_STATUS_INSUFFICIENT_MEMORY);
    state.texture_offset = 0x00204000ul;
    state.wrap_s = 2ul;
    CHECK(v9x_m64_build_texture_state(
              &state, offsets, values, V9X_M64_TEXTURED_STATE_DWORDS,
              &written) == V9X_STATUS_INVALID_ARGUMENT);
    CHECK(written == 0ul);
    state.wrap_s = 0ul;
    state.texture_format = 3ul;
    CHECK(v9x_m64_build_texture_state(
              &state, offsets, values, V9X_M64_TEXTURED_STATE_DWORDS,
              &written) == V9X_STATUS_INVALID_ARGUMENT);
    CHECK(written == 0ul);

    memset(&triangle, 0, sizeof(triangle));
    triangle.vertex[0].x = 8ul; triangle.vertex[0].y = 6ul;
    triangle.vertex[1].x = 40ul; triangle.vertex[1].y = 6ul;
    triangle.vertex[2].x = 8ul; triangle.vertex[2].y = 22ul;
    triangle.s[0] = 0.0f; triangle.t[0] = 0.0f;
    triangle.s[1] = 1.0f; triangle.t[1] = 0.0f;
    triangle.s[2] = 0.0f; triangle.t[2] = 1.0f;
    triangle.color = 0xfffffffful;
    for (index = 0ul; index < 3ul; ++index) triangle.w[index] = 1.0f;
    CHECK(v9x_m64_build_textured_triangle(
              &triangle, setup_offsets, setup_values,
              V9X_M64_TEXTURED_TRIANGLE_DWORDS, &written) == V9X_STATUS_OK);
    CHECK(written == V9X_M64_TEXTURED_TRIANGLE_DWORDS);
    CHECK(setup_values[0] == 0ul && setup_values[1] == 0ul &&
          setup_values[2] == 0x3f800000ul);
    CHECK(setup_values[6] == 0x3f800000ul && setup_values[7] == 0ul &&
          setup_values[8] == 0x3f800000ul);
    CHECK(setup_values[12] == 0ul &&
          setup_values[13] == 0x3f800000ul &&
          setup_values[14] == 0x3f800000ul);
    triangle.w[1] = 0.25f;
    triangle.w[2] = 0.25f;
    CHECK(v9x_m64_build_textured_triangle(
              &triangle, setup_offsets, setup_values,
              V9X_M64_TEXTURED_TRIANGLE_DWORDS, &written) == V9X_STATUS_OK);
    CHECK(setup_values[2] == 0x3f800000ul);
    CHECK(setup_values[8] == 0x3e800000ul);
    CHECK(setup_values[14] == 0x3e800000ul);
    triangle.w[2] = 0.0f;
    CHECK(v9x_m64_build_textured_triangle(
              &triangle, setup_offsets, setup_values,
              V9X_M64_TEXTURED_TRIANGLE_DWORDS, &written) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(written == 0ul);
}

/*
 * The register offsets the diagnostic VxD wrote on the Gateway, in order:
 * AtiE4StateOffsets and ATIE1_GUI_STAT in tools\diag\ati_mach64_phase1.asm,
 * which drew every Phase 3 and 4 scene. The C header once had GUI_STAT,
 * FIFO_STAT and GUI_TRAJ_CNTL without block 0's 0x400, which no host test
 * could see because the builders and the fake MMIO shared the wrong names;
 * this pins them to the physically proven table instead.
 */
/* The FIFO wait's status words, block 0 like the diagnostic's. */
typedef char v9x_m64_gui_stat_is_block0[
    V9X_M64_GUI_STAT == 0x738ul ? 1 : -1];
typedef char v9x_m64_fifo_stat_is_block0[
    V9X_M64_FIFO_STAT == 0x710ul ? 1 : -1];
typedef char v9x_m64_gui_traj_is_block0[
    V9X_M64_GUI_TRAJ_CNTL == 0x730ul ? 1 : -1];

/*
 * A mip chain writes TEX_n_OFF for every level size from the top down to
 * 1x1, padding a short chain with its smallest level, and clears
 * MIP_MAP_DISABLE; a single level keeps the one register and the bit.
 */
static void test_mip_texture_state(void)
{
    struct v9x_m64_texture_state state;
    v9x_u32 offsets[V9X_M64_DRAW_STATE_DWORDS];
    v9x_u32 values[V9X_M64_DRAW_STATE_DWORDS];
    v9x_u32 written = 99ul;
    v9x_u32 level;

    memset(&state, 0, sizeof(state));
    state.color.vram_bytes = 4ul * 1024ul * 1024ul;
    state.color.target_offset = 0x00200100ul;
    state.color.target_pitch_bytes = 128ul;
    state.color.target_width = 64ul;
    state.color.target_height = 28ul;
    state.color.scissor_right = 64ul;
    state.color.scissor_bottom = 28ul;
    state.texture_offset = 0x00210000ul;
    state.texture_pitch_bytes = 128ul;
    state.texture_width = 64ul;
    state.texture_height = 64ul;

    /* 64 down to 1: seven levels, one 8 KiB block apart. */
    state.level_count = 7ul;
    for (level = 0ul; level < 7ul; ++level) {
        state.level_offsets[level] = 0x00210000ul + level * 0x2000ul;
    }
    CHECK(v9x_m64_build_texture_state(
              &state, offsets, values, V9X_M64_DRAW_STATE_DWORDS,
              &written) == V9X_STATUS_OK);
    CHECK(written == V9X_M64_TEXTURED_STATE_DWORDS + 6ul);
    CHECK((values[10] & V9X_M64_MIP_MAP_DISABLE) == 0ul);
    for (level = 0ul; level < 7ul; ++level) {
        CHECK(offsets[18ul + level] ==
              V9X_M64_TEX_0_OFF + (6ul - level) * 4ul);
        CHECK(values[18ul + level] == 0x00210000ul + level * 0x2000ul);
    }

    /* Three levels of seven: TEX_3_OFF..TEX_0_OFF take the smallest. */
    state.level_count = 3ul;
    CHECK(v9x_m64_build_texture_state(
              &state, offsets, values, V9X_M64_DRAW_STATE_DWORDS,
              &written) == V9X_STATUS_OK);
    CHECK(written == V9X_M64_TEXTURED_STATE_DWORDS + 6ul);
    for (level = 3ul; level < 7ul; ++level) {
        CHECK(offsets[18ul + level] ==
              V9X_M64_TEX_0_OFF + (6ul - level) * 4ul);
        CHECK(values[18ul + level] == 0x00214000ul);
    }

    /* A chain needs room for every register. */
    CHECK(v9x_m64_build_texture_state(
              &state, offsets, values, V9X_M64_TEXTURED_STATE_DWORDS,
              &written) == V9X_STATUS_INVALID_ARGUMENT);
    CHECK(written == 0ul);

    /* More levels than the edge has, a level off its 64 bytes, a first
     * level that is not the texture, and a level past VRAM all refuse. */
    state.level_count = 8ul;
    CHECK(v9x_m64_build_texture_state(
              &state, offsets, values, V9X_M64_DRAW_STATE_DWORDS,
              &written) == V9X_STATUS_INVALID_ARGUMENT);
    state.level_count = 3ul;
    state.level_offsets[1] = 0x00212010ul;
    CHECK(v9x_m64_build_texture_state(
              &state, offsets, values, V9X_M64_DRAW_STATE_DWORDS,
              &written) == V9X_STATUS_INVALID_ARGUMENT);
    state.level_offsets[1] = 0x00212000ul;
    state.level_offsets[0] = 0x00220000ul;
    CHECK(v9x_m64_build_texture_state(
              &state, offsets, values, V9X_M64_DRAW_STATE_DWORDS,
              &written) == V9X_STATUS_INVALID_ARGUMENT);
    state.level_offsets[0] = 0x00210000ul;
    state.level_offsets[2] = 0x00400000ul;
    CHECK(v9x_m64_build_texture_state(
              &state, offsets, values, V9X_M64_DRAW_STATE_DWORDS,
              &written) == V9X_STATUS_INSUFFICIENT_MEMORY);
    CHECK(written == 0ul);

    /* Trilinear, bilinear_min 2, is TEX_BLEND_FCN 3 on a chain and
     * refuses on a single level. */
    state.level_offsets[2] = 0x00214000ul;
    state.bilinear_min = 2ul;
    CHECK(v9x_m64_build_texture_state(
              &state, offsets, values, V9X_M64_DRAW_STATE_DWORDS,
              &written) == V9X_STATUS_OK);
    CHECK((values[10] & 0x0c000000ul) == V9X_M64_TEX_BLEND_FCN_TRILINEAR);
    state.level_count = 1ul;
    CHECK(v9x_m64_build_texture_state(
              &state, offsets, values, V9X_M64_DRAW_STATE_DWORDS,
              &written) == V9X_STATUS_INVALID_ARGUMENT);
    state.bilinear_min = 0ul;

    /* One level is the single-texture state, bit and all. */
    state.level_count = 1ul;
    CHECK(v9x_m64_build_texture_state(
              &state, offsets, values, V9X_M64_DRAW_STATE_DWORDS,
              &written) == V9X_STATUS_OK);
    CHECK(written == V9X_M64_TEXTURED_STATE_DWORDS);
    CHECK((values[10] & V9X_M64_MIP_MAP_DISABLE) != 0ul);
}

/*
 * Rectangular textures (2026-10-01): the row pitch is the width, which is
 * what DirectDraw gives a texture surface, and TEX_SIZE_PITCH carries the
 * width, the larger edge and the height. A chain halves each edge to one
 * texel and keeps its TEX_n_OFF by the larger edge.
 */
static void test_rectangular_texture_state(void)
{
    struct v9x_m64_texture_state state;
    v9x_u32 offsets[V9X_M64_DRAW_STATE_DWORDS];
    v9x_u32 values[V9X_M64_DRAW_STATE_DWORDS];
    v9x_u32 written = 99ul;
    v9x_u32 level;

    memset(&state, 0, sizeof(state));
    state.color.vram_bytes = 4ul * 1024ul * 1024ul;
    state.color.target_offset = 0x00200100ul;
    state.color.target_pitch_bytes = 128ul;
    state.color.target_width = 64ul;
    state.color.target_height = 28ul;
    state.color.scissor_right = 64ul;
    state.color.scissor_bottom = 28ul;
    state.texture_offset = 0x00210000ul;

    /* Tall: 32 wide, 64 high, rows of 64 bytes. */
    state.texture_width = 32ul;
    state.texture_height = 64ul;
    state.texture_pitch_bytes = 64ul;
    CHECK(v9x_m64_build_texture_state(
              &state, offsets, values, V9X_M64_TEXTURED_STATE_DWORDS,
              &written) == V9X_STATUS_OK);
    CHECK(values[15] == 0x00000665ul);
    CHECK(offsets[18] == V9X_M64_TEX_0_OFF + 6ul * 4ul);
    /* The larger edge's rows are not this texture's. */
    state.texture_pitch_bytes = 128ul;
    CHECK(v9x_m64_build_texture_state(
              &state, offsets, values, V9X_M64_TEXTURED_STATE_DWORDS,
              &written) == V9X_STATUS_INVALID_ARGUMENT);

    /* Wide 64x16, a chain to 1x1: 64x16 32x8 16x4 8x2 4x1 2x1 1x1, each
     * level on its own TEX_n_OFF from TEX_6 down. */
    state.texture_width = 64ul;
    state.texture_height = 16ul;
    state.texture_pitch_bytes = 128ul;
    state.level_count = 7ul;
    for (level = 0ul; level < 7ul; ++level) {
        state.level_offsets[level] = 0x00210000ul + level * 0x1000ul;
    }
    CHECK(v9x_m64_build_texture_state(
              &state, offsets, values, V9X_M64_DRAW_STATE_DWORDS,
              &written) == V9X_STATUS_OK);
    CHECK(values[15] == 0x00000466ul);
    CHECK(written == V9X_M64_TEXTURED_STATE_DWORDS + 6ul);
    for (level = 0ul; level < 7ul; ++level) {
        CHECK(offsets[18ul + level] ==
              V9X_M64_TEX_0_OFF + (6ul - level) * 4ul);
        CHECK(values[18ul + level] == 0x00210000ul + level * 0x1000ul);
    }
    /* One level more than the larger edge allows. */
    state.level_count = 8ul;
    CHECK(v9x_m64_build_texture_state(
              &state, offsets, values, V9X_M64_DRAW_STATE_DWORDS,
              &written) == V9X_STATUS_INVALID_ARGUMENT);
    /* A level is sized by its own edges: the 32x8 level (512 bytes) fits
     * in the last 512 bytes of VRAM, where a 32x32 one would not. */
    state.level_count = 2ul;
    state.level_offsets[1] = state.color.vram_bytes - 512ul;
    CHECK(v9x_m64_build_texture_state(
              &state, offsets, values, V9X_M64_DRAW_STATE_DWORDS,
              &written) == V9X_STATUS_OK);
    state.level_offsets[1] = state.color.vram_bytes - 448ul;
    CHECK(v9x_m64_build_texture_state(
              &state, offsets, values, V9X_M64_DRAW_STATE_DWORDS,
              &written) == V9X_STATUS_INSUFFICIENT_MEMORY);
}

static void test_2d_mode_builder(void)
{
    v9x_u32 offsets[4];
    v9x_u32 values[4];
    v9x_u32 written = 7ul;

    CHECK(v9x_m64_build_2d_mode(offsets, values, 2ul, &written) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(written == 0ul);
    CHECK(v9x_m64_build_2d_mode(offsets, values, 4ul, &written) ==
          V9X_STATUS_OK);
    CHECK(written == 3ul);
    CHECK(offsets[0] == 0x550ul && values[0] == 0ul);   /* ALPHA_TST_CNTL */
    CHECK(offsets[1] == 0x54cul && values[1] == 0ul);   /* Z_CNTL */
    CHECK(offsets[2] == 0x5fcul && values[2] == 0ul);   /* SCALE_3D_CNTL */
}

static void test_offsets_match_diagnostic(void)
{
    static const v9x_u32 proven[19] = {
        0x6d4ul, 0x6d8ul, 0x708ul, 0x730ul, 0x6a8ul, 0x6b4ul,
        0x500ul, 0x548ul, 0x54cul, 0x550ul, 0x5fcul, 0x6c4ul,
        0x6c8ul, 0x6d0ul, 0x304ul, 0x770ul, 0x774ul, 0x778ul, 0x5ccul
    };
    struct v9x_m64_texture_state texture;
    v9x_u32 offsets[32];
    v9x_u32 values[32];
    v9x_u32 written;
    unsigned int index;

    memset(&texture, 0, sizeof(texture));
    texture.color.vram_bytes = 0x00400000ul;
    texture.color.target_offset = 0x00200100ul;
    texture.color.target_pitch_bytes = 128ul;
    texture.color.target_width = 64ul;
    texture.color.target_height = 28ul;
    texture.color.scissor_right = 64ul;
    texture.color.scissor_bottom = 28ul;
    texture.texture_offset = 0x00204000ul;
    texture.texture_pitch_bytes = 16ul;
    texture.texture_width = 8ul;
    texture.texture_height = 8ul;
    CHECK(v9x_m64_build_texture_state(&texture, offsets, values, 32ul,
                                      &written) == V9X_STATUS_OK);
    CHECK(written == 19ul);
    for (index = 0u; index < 19u; ++index) {
        CHECK(offsets[index] == proven[index]);
    }
}

static void test_phase3_triangle_golden(void)
{
    struct v9x_m64_flat_state state;
    struct v9x_m64_flat_triangle triangle;
    v9x_u32 state_offsets[V9X_M64_FLAT_STATE_DWORDS];
    v9x_u32 state_values[V9X_M64_FLAT_STATE_DWORDS];
    v9x_u32 setup_offsets[V9X_M64_FLAT_TRIANGLE_DWORDS];
    v9x_u32 setup_values[V9X_M64_FLAT_TRIANGLE_DWORDS];
    v9x_u32 written;
    state.vram_bytes = 4ul * 1024ul * 1024ul;
    state.target_offset = 0x00200100ul;
    state.target_pitch_bytes = 128ul;
    state.target_width = 64ul;
    state.target_height = 28ul;
    state.scissor_left = 0ul; state.scissor_top = 0ul;
    state.scissor_right = 64ul; state.scissor_bottom = 28ul;
    CHECK(v9x_m64_build_flat_state(
              &state, state_offsets, state_values,
              V9X_M64_FLAT_STATE_DWORDS, &written) == V9X_STATUS_OK);
    CHECK(written == 17ul);
    CHECK(state_values[4] == 0x003f0000ul);
    CHECK(state_values[5] == 0x001b0000ul);
    CHECK(state_values[6] == 0x02040020ul);
    CHECK(state_values[7] == 0x02040020ul);

    triangle.vertex[0].x = 8ul; triangle.vertex[0].y = 6ul;
    triangle.vertex[1].x = 40ul; triangle.vertex[1].y = 6ul;
    triangle.vertex[2].x = 8ul; triangle.vertex[2].y = 22ul;
    triangle.color = 0xffff00fful;
    CHECK(v9x_m64_build_flat_triangle(
              &triangle, setup_offsets, setup_values,
              V9X_M64_FLAT_TRIANGLE_DWORDS, &written) == V9X_STATUS_OK);
    CHECK(written == 19ul);
    CHECK(setup_values[5] == 0x00200018ul);
    CHECK(setup_values[11] == 0x00a00018ul);
    CHECK(setup_values[17] == 0x00200058ul);
    CHECK(setup_offsets[18] == 0x0000029cul);
    CHECK(setup_values[18] == 0x3b000000ul);
}

unsigned int v9x_run_mach64_engine_tests(void)
{
    test_fifo_decode();
    test_exact_batch_has_no_inner_read();
    test_shadow_keeps_order_and_latest();
    test_bounds_and_timeout();
    test_barrier_and_reset_order();
    test_fill_builder();
    test_copy_builder();
    test_flat_triangle_builder();
    test_flat_state_builder();
    test_gouraud_builders();
    test_z_control_truth_table();
    test_alpha_control_truth_table();
    test_blend_control_table();
    test_depth_builders();
    test_texture_builders();
    test_phase3_triangle_golden();
    test_offsets_match_diagnostic();
    test_2d_mode_builder();
    test_mip_texture_state();
    test_rectangular_texture_state();
    return failures;
}
