#include <stdio.h>
#include <string.h>

#include "velocity9x/eg_dma.h"
#include "velocity9x/vi_sdma.h"
#include "velocity9x/vi_sdma_microcode.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

/* A small heap at a GPU address above 4 GiB with a low half that is not
 * zero, as Carrizo's carve-out sits at F4_00000000 (M-XP7053): a packet
 * that drops either half lands outside it. */
#define VI_GPU_HIGH  0x000000f4ul
#define VI_GPU_LOW   0x00100000ul
#define VI_HEAP      65536ul

static unsigned char vram[VI_HEAP];
static unsigned char expected[VI_HEAP];

static void seed(unsigned char *buffer)
{
    v9x_u32 index;

    for (index = 0ul; index < VI_HEAP; ++index) {
        buffer[index] = (unsigned char)((index * 131ul + 7ul) >> 1);
    }
}

/* The engine as the write probe measured it
 * (docs\decisions\2026-10-28-carrizo-sdma-write-probe.md): COPY_LINEAR
 * moves a byte count with memmove semantics; CONSTANT_FILL with fill size
 * 2 writes the dword, 0 its low byte. Returns 0 on a packet outside the
 * heap, a high half other than the heap's, or a kind it does not know. */
static int execute(const v9x_u32 *packet, v9x_u32 *dwords)
{
    v9x_u32 op = packet[0] & 0xfful;
    v9x_u32 sub = (packet[0] >> 8) & 0xfful;
    v9x_u32 bytes;
    v9x_u32 destination;
    v9x_u32 source;
    v9x_u32 index;

    if (op == V9X_VI_SDMA_OP_CONST_FILL) {
        v9x_u32 size = packet[0] >> V9X_VI_SDMA_FILL_SIZE_SHIFT;

        destination = packet[1] - VI_GPU_LOW;
        bytes = packet[4];
        if (packet[2] != VI_GPU_HIGH || size != V9X_VI_SDMA_FILL_SIZE_DWORD ||
            (packet[0] & 0x3fffff00ul) != 0ul || destination > VI_HEAP ||
            bytes > VI_HEAP - destination ||
            bytes > V9X_VI_SDMA_CHUNK_BYTES_MAX) {
            return 0;
        }
        for (index = 0ul; index < bytes; ++index) {
            vram[destination + index] =
                (unsigned char)(packet[3] >> ((index & 3ul) * 8ul));
        }
        *dwords = V9X_VI_SDMA_FILL_DWORDS;
        return 1;
    }
    if (op == V9X_VI_SDMA_OP_COPY) {
        bytes = packet[1];
        source = packet[3] - VI_GPU_LOW;
        destination = packet[5] - VI_GPU_LOW;
        if (sub != V9X_VI_SDMA_SUB_LINEAR || packet[2] != 0ul ||
            packet[4] != VI_GPU_HIGH || packet[6] != VI_GPU_HIGH ||
            bytes == 0ul || bytes > V9X_VI_SDMA_CHUNK_BYTES_MAX ||
            destination > VI_HEAP || bytes > VI_HEAP - destination ||
            source > VI_HEAP || bytes > VI_HEAP - source) {
            return 0;
        }
        memmove(vram + destination, vram + source, bytes);
        *dwords = V9X_VI_SDMA_COPY_DWORDS;
        return 1;
    }
    return 0;
}

/* Every row of an Evergreen plan, emitted as SDMA packets and executed. */
static int run_plan(const struct v9x_eg_dma_plan *plan)
{
    v9x_u32 packets[V9X_VI_SDMA_ROW_DWORDS_MAX];
    v9x_u32 index;
    v9x_u32 count;
    v9x_u32 at;
    v9x_u32 used;

    if (v9x_vi_sdma_plan_check(plan) != V9X_STATUS_OK) {
        return 0;
    }
    for (index = 0ul; index < plan->rows; ++index) {
        count = v9x_vi_sdma_plan_row(plan, index, VI_GPU_HIGH, packets);
        if (count == 0ul || count > V9X_VI_SDMA_ROW_DWORDS_MAX) {
            return 0;
        }
        for (at = 0ul; at < count; at += used) {
            if (!execute(packets + at, &used)) {
                return 0;
            }
        }
    }
    return 1;
}

static void base_fill(struct v9x_eg_dma_fill *fill)
{
    memset(fill, 0, sizeof(*fill));
    fill->gpu_base = VI_GPU_LOW;
    fill->vram_bytes = VI_HEAP;
    fill->pitch_bytes = 256ul;
    fill->bytes_per_pixel = 2ul;
    fill->left = 10ul;
    fill->top = 20ul;
    fill->width = 40ul;
    fill->height = 30ul;
    fill->color = 0x1234ul;
}

static void base_copy(struct v9x_eg_dma_copy *copy)
{
    memset(copy, 0, sizeof(*copy));
    copy->gpu_base = VI_GPU_LOW;
    copy->vram_bytes = VI_HEAP;
    copy->source_pitch_bytes = 256ul;
    copy->destination_pitch_bytes = 256ul;
    copy->bytes_per_pixel = 2ul;
    copy->source_left = 10ul;
    copy->source_top = 10ul;
    copy->destination_left = 50ul;
    copy->destination_top = 100ul;
    copy->width = 30ul;
    copy->height = 20ul;
}

static void reference_fill(const struct v9x_eg_dma_fill *fill)
{
    v9x_u32 x;
    v9x_u32 y;
    v9x_u32 byte;
    unsigned char *pixel;

    for (y = 0ul; y < fill->height; ++y) {
        for (x = 0ul; x < fill->width; ++x) {
            pixel = expected + fill->target_offset +
                    (fill->top + y) * fill->pitch_bytes +
                    (fill->left + x) * fill->bytes_per_pixel;
            for (byte = 0ul; byte < fill->bytes_per_pixel; ++byte) {
                pixel[byte] = (unsigned char)(fill->color >> (byte * 8ul));
            }
        }
    }
}

static unsigned char scratch[VI_HEAP];

static void reference_copy(const struct v9x_eg_dma_copy *copy)
{
    v9x_u32 y;
    v9x_u32 bytes = copy->width * copy->bytes_per_pixel;

    for (y = 0ul; y < copy->height; ++y) {
        memcpy(scratch + y * bytes,
               expected + copy->source_offset +
               (copy->source_top + y) * copy->source_pitch_bytes +
               copy->source_left * copy->bytes_per_pixel, bytes);
    }
    for (y = 0ul; y < copy->height; ++y) {
        memcpy(expected + copy->destination_offset +
               (copy->destination_top + y) * copy->destination_pitch_bytes +
               copy->destination_left * copy->bytes_per_pixel,
               scratch + y * bytes, bytes);
    }
}

static int check_fill(const struct v9x_eg_dma_fill *fill)
{
    struct v9x_eg_dma_plan plan;

    seed(vram);
    seed(expected);
    if (v9x_eg_dma_plan_fill(fill, &plan) != V9X_STATUS_OK ||
        !run_plan(&plan)) {
        return 0;
    }
    reference_fill(fill);
    return memcmp(vram, expected, VI_HEAP) == 0;
}

static int check_copy(const struct v9x_eg_dma_copy *copy)
{
    struct v9x_eg_dma_plan plan;

    seed(vram);
    seed(expected);
    if (v9x_eg_dma_plan_copy(copy, &plan) != V9X_STATUS_OK ||
        !run_plan(&plan)) {
        return 0;
    }
    reference_copy(copy);
    return memcmp(vram, expected, VI_HEAP) == 0;
}

/* The generated image: its size, version and edges, as read from
 * carrizo_sdma.bin's payload (offset 100h). */
typedef char v9x_vi_sdma_microcode_dwords[
    V9X_VI_SDMA_MICROCODE_DWORDS == 2592u ? 1 : -1];
typedef char v9x_vi_sdma_nop_is_zero[V9X_VI_SDMA_NOP == 0ul ? 1 : -1];

static void test_microcode(void)
{
    CHECK(v9x_vi_sdma_microcode_version == 0x00000022ul);
    CHECK(v9x_vi_sdma_microcode[0] == 0x00000000ul);
    CHECK(v9x_vi_sdma_microcode[1] == 0x040cd800ul);
    CHECK(v9x_vi_sdma_microcode[2] == 0xc8d50000ul);
    CHECK(v9x_vi_sdma_microcode[V9X_VI_SDMA_MICROCODE_DWORDS - 1u] ==
          0x01fc0529ul);
}

/* The packets word for word, as the write probe sent them. */
static void test_packets(void)
{
    struct v9x_eg_dma_plan plan;
    v9x_u32 out[V9X_VI_SDMA_ROW_DWORDS_MAX];

    memset(&plan, 0, sizeof(plan));
    plan.kind = V9X_EG_DMA_PLAN_COPY;
    plan.rows = 1ul;
    plan.row_bytes = 37ul;
    plan.source = 0x0f002001ul;
    plan.destination = 0x0f004403ul;
    plan.byte_copy = 1ul;
    CHECK(v9x_vi_sdma_plan_check(&plan) == V9X_STATUS_OK);
    CHECK(v9x_vi_sdma_plan_row(&plan, 0ul, 0xf4ul, out) ==
          V9X_VI_SDMA_COPY_DWORDS);
    CHECK(out[0] == 0x00000001ul);
    CHECK(out[1] == 37ul);
    CHECK(out[2] == 0ul);
    CHECK(out[3] == 0x0f002001ul);
    CHECK(out[4] == 0xf4ul);
    CHECK(out[5] == 0x0f004403ul);
    CHECK(out[6] == 0xf4ul);
    CHECK(v9x_vi_sdma_plan_row(&plan, 1ul, 0xf4ul, out) == 0ul);

    memset(&plan, 0, sizeof(plan));
    plan.kind = V9X_EG_DMA_PLAN_FILL;
    plan.rows = 1ul;
    plan.row_bytes = 1024ul;
    plan.destination = 0x0f006010ul;
    plan.value = 0x11223344ul;
    CHECK(v9x_vi_sdma_plan_row(&plan, 0ul, 0xf4ul, out) ==
          V9X_VI_SDMA_FILL_DWORDS);
    CHECK(out[0] == 0x8000000bul);
    CHECK(out[1] == 0x0f006010ul);
    CHECK(out[2] == 0xf4ul);
    CHECK(out[3] == 0x11223344ul);
    CHECK(out[4] == 1024ul);

    CHECK(v9x_vi_sdma_fence(0x0f001003ul, 0xf4ul, 0x11111111ul, out) ==
          V9X_VI_SDMA_FENCE_DWORDS);
    CHECK(out[0] == 0x00000005ul);
    CHECK(out[1] == 0x0f001000ul);
    CHECK(out[2] == 0xf4ul);
    CHECK(out[3] == 0x11111111ul);
}

static void test_fill(void)
{
    struct v9x_eg_dma_fill fill;

    base_fill(&fill);
    CHECK(check_fill(&fill));
    fill.bytes_per_pixel = 4ul;
    fill.color = 0x00a1b2c3ul;
    CHECK(check_fill(&fill));
    fill.bytes_per_pixel = 1ul;
    fill.left = 8ul;
    fill.width = 64ul;
    fill.color = 0x5aul;
    CHECK(check_fill(&fill));
    /* Contiguous rows: one run. */
    base_fill(&fill);
    fill.left = 0ul;
    fill.width = 128ul;
    CHECK(check_fill(&fill));
}

static void test_copy(void)
{
    struct v9x_eg_dma_copy copy;

    base_copy(&copy);
    CHECK(check_copy(&copy));
    /* Overlapping, down and up, and sideways inside one row. */
    copy.destination_left = 12ul;
    copy.destination_top = 14ul;
    CHECK(check_copy(&copy));
    copy.destination_left = 8ul;
    copy.destination_top = 6ul;
    CHECK(check_copy(&copy));
    copy.destination_left = 14ul;
    copy.destination_top = 10ul;
    CHECK(check_copy(&copy));
    /* The same overlap a pixel off the dword is a byte copy, which was not
     * measured overlapping: the planner refuses it. */
    copy.destination_left = 13ul;
    CHECK(!check_copy(&copy));
    /* Odd widths at 16 bpp: byte rows. */
    base_copy(&copy);
    copy.width = 31ul;
    copy.source_left = 11ul;
    CHECK(check_copy(&copy));
}

/* A fill row past one packet's reach is two packets; a copy row is
 * refused, since splitting an overlapping copy would change what it
 * reads. */
static void test_long_rows(void)
{
    struct v9x_eg_dma_plan plan;
    v9x_u32 out[V9X_VI_SDMA_ROW_DWORDS_MAX];

    memset(&plan, 0, sizeof(plan));
    plan.kind = V9X_EG_DMA_PLAN_FILL;
    plan.rows = 1ul;
    plan.row_bytes = V9X_VI_SDMA_CHUNK_BYTES_MAX + 16ul;
    plan.destination = 0x00001000ul;
    plan.value = 0x01020304ul;
    CHECK(v9x_vi_sdma_plan_check(&plan) == V9X_STATUS_OK);
    CHECK(v9x_vi_sdma_plan_row(&plan, 0ul, 0xf4ul, out) ==
          2ul * V9X_VI_SDMA_FILL_DWORDS);
    CHECK(out[4] == V9X_VI_SDMA_CHUNK_BYTES_MAX);
    CHECK(out[6] == 0x00001000ul + V9X_VI_SDMA_CHUNK_BYTES_MAX);
    CHECK(out[9] == 16ul);
    plan.row_bytes = 2ul * V9X_VI_SDMA_CHUNK_BYTES_MAX + 4ul;
    CHECK(v9x_vi_sdma_plan_check(&plan) == V9X_STATUS_UNSUPPORTED);

    plan.kind = V9X_EG_DMA_PLAN_COPY;
    plan.row_bytes = V9X_VI_SDMA_CHUNK_BYTES_MAX;
    CHECK(v9x_vi_sdma_plan_check(&plan) == V9X_STATUS_OK);
    plan.row_bytes = V9X_VI_SDMA_CHUNK_BYTES_MAX + 4ul;
    CHECK(v9x_vi_sdma_plan_check(&plan) == V9X_STATUS_UNSUPPORTED);
    CHECK(v9x_vi_sdma_plan_check(0) == V9X_STATUS_INVALID_ARGUMENT);
}

static void test_ring(void)
{
    CHECK(v9x_vi_sdma_pad(0ul) == 0ul);
    CHECK(v9x_vi_sdma_pad(5ul) == 11ul);
    CHECK(v9x_vi_sdma_pad(16ul) == 0ul);
    CHECK(v9x_vi_sdma_ring_advance(0ul, 5ul) == 16ul);
    CHECK(v9x_vi_sdma_ring_advance(V9X_VI_SDMA_RING_DWORDS - 16ul, 7ul) ==
          0ul);
}

/* MC_VM_FB_LOCATION's start, as M-XP7053 read it: F41FF400h is
 * F4_00000000. A low byte that the window offset would carry out of is
 * refused. */
static void test_fb_start(void)
{
    v9x_u32 high = 0ul;
    v9x_u32 low = 1ul;

    CHECK(v9x_vi_fb_start(0xf41ff400ul, &high, &low) == V9X_STATUS_OK);
    CHECK(high == 0xf4ul);
    CHECK(low == 0ul);
    CHECK(v9x_vi_fb_start(0x00ff0010ul, &high, &low) == V9X_STATUS_OK);
    CHECK(high == 0ul);
    CHECK(low == 0x10000000ul);
    CHECK(v9x_vi_fb_start(0x00ff00f8ul, &high, &low) ==
          V9X_STATUS_UNSUPPORTED);
    CHECK(v9x_vi_fb_start(0xf41ff400ul, 0, &low) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

unsigned int v9x_run_vi_sdma_tests(void)
{
    failures = 0u;
    test_microcode();
    test_packets();
    test_fill();
    test_copy();
    test_long_rows();
    test_ring();
    test_fb_start();
    return failures;
}
