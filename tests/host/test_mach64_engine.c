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

unsigned int v9x_run_mach64_engine_tests(void)
{
    test_fifo_decode();
    test_exact_batch_has_no_inner_read();
    test_bounds_and_timeout();
    test_barrier_and_reset_order();
    return failures;
}
