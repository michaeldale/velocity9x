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

unsigned int v9x_run_mach64_engine_tests(void)
{
    test_fifo_decode();
    test_exact_batch_has_no_inner_read();
    test_bounds_and_timeout();
    test_barrier_and_reset_order();
    test_fill_builder();
    test_copy_builder();
    test_flat_triangle_builder();
    return failures;
}
