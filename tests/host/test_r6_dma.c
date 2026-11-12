#include <stdio.h>
#include <string.h>

#include "velocity9x/r6_dma.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

/* A small heap at a GPU address that is not zero, so a packet that forgot
 * the base lands outside it. */
#define R6_GPU_BASE  0x00100000ul
#define R6_HEAP      65536ul

static unsigned char vram[R6_HEAP];
static unsigned char before[R6_HEAP];
static unsigned char expected[R6_HEAP];

static void seed(unsigned char *buffer)
{
    v9x_u32 index;

    for (index = 0ul; index < R6_HEAP; ++index) {
        buffer[index] = (unsigned char)((index * 131ul + 7ul) >> 1);
    }
}

/*
 * The engine as M-NZHWBR's write probe measured it
 * (docs\decisions\2026-11-13-rv630-dma-write-probe.md): COPY moves dwords
 * with memmove semantics, and a one-dword COPY does nothing. With stale
 * set, every read sees memory as it stood when the submission began, as a
 * copy inside one submission did not see an earlier packet's write; a plan
 * must come out right that way and in plain order both. Returns 0 on a
 * packet outside the heap or one this model does not know.
 */
static int execute(const v9x_u32 *packet, int stale)
{
    v9x_u32 command = packet[0] >> 28;
    v9x_u32 count = packet[0] & 0xfffful;
    v9x_u32 destination = packet[1] - R6_GPU_BASE;
    v9x_u32 source = packet[2] - R6_GPU_BASE;
    v9x_u32 bytes = count * 4ul;

    if (command != V9X_R6_DMA_CMD_COPY || (packet[0] & 0x0fff0000ul) != 0ul ||
        packet[3] != 0ul || ((packet[1] | packet[2]) & 3ul) != 0ul ||
        destination > R6_HEAP || bytes > R6_HEAP - destination ||
        source > R6_HEAP || bytes > R6_HEAP - source) {
        return 0;
    }
    if (count == 1ul) {
        return 1;
    }
    memmove(vram + destination, (stale ? before : vram) + source, bytes);
    return 1;
}

/* The CPU's seed write, then the packets. */
static int run_plan(const struct v9x_r6_dma_plan *plan, int stale)
{
    v9x_u32 packet[V9X_R6_DMA_PACKET_DWORDS_MAX];
    v9x_u32 index;

    for (index = 0ul; index < plan->seed_bytes; ++index) {
        vram[plan->seed_offset + index] =
            (unsigned char)(plan->value >> ((index & 3ul) * 8ul));
    }
    memcpy(before, vram, R6_HEAP);
    for (index = 0ul; index < plan->rows; ++index) {
        if (v9x_r6_dma_plan_row(plan, index, 0ul, packet) !=
                V9X_R6_DMA_COPY_DWORDS ||
            (packet[0] & 0xfffful) < 2ul || !execute(packet, stale)) {
            return 0;
        }
    }
    return 1;
}

static void base_fill(struct v9x_r6_dma_fill *fill)
{
    memset(fill, 0, sizeof(*fill));
    fill->gpu_base = R6_GPU_BASE;
    fill->vram_bytes = R6_HEAP;
    fill->target_offset = 0ul;
    fill->pitch_bytes = 256ul;
    fill->bytes_per_pixel = 2ul;
    fill->left = 10ul;
    fill->top = 20ul;
    fill->width = 40ul;
    fill->height = 30ul;
    fill->color = 0x1234ul;
}

static void base_copy(struct v9x_r6_dma_copy *copy)
{
    memset(copy, 0, sizeof(*copy));
    copy->gpu_base = R6_GPU_BASE;
    copy->vram_bytes = R6_HEAP;
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

static void reference_fill(const struct v9x_r6_dma_fill *fill)
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

/* A CPU copy through a scratch buffer, so overlap does not matter. */
static unsigned char scratch[R6_HEAP];

static void reference_copy(const struct v9x_r6_dma_copy *copy)
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

/* Right in order and right with stale reads. */
static int check_fill(const struct v9x_r6_dma_fill *fill)
{
    struct v9x_r6_dma_plan plan;
    int stale;

    for (stale = 0; stale < 2; ++stale) {
        seed(vram);
        seed(expected);
        if (v9x_r6_dma_plan_fill(fill, &plan) != V9X_STATUS_OK ||
            !run_plan(&plan, stale)) {
            return 0;
        }
        reference_fill(fill);
        if (memcmp(vram, expected, R6_HEAP) != 0) {
            return 0;
        }
    }
    return 1;
}

static int check_copy(const struct v9x_r6_dma_copy *copy)
{
    struct v9x_r6_dma_plan plan;
    int stale;

    for (stale = 0; stale < 2; ++stale) {
        seed(vram);
        seed(expected);
        if (v9x_r6_dma_plan_copy(copy, &plan) != V9X_STATUS_OK ||
            !run_plan(&plan, stale)) {
            return 0;
        }
        reference_copy(copy);
        if (memcmp(vram, expected, R6_HEAP) != 0) {
            return 0;
        }
    }
    return 1;
}

static void test_fill(void)
{
    struct v9x_r6_dma_fill fill;
    struct v9x_r6_dma_plan plan;

    /* 16 bpp: the first row seeded, the other 29 copied from it. */
    base_fill(&fill);
    CHECK(check_fill(&fill));
    CHECK(v9x_r6_dma_plan_fill(&fill, &plan) == V9X_STATUS_OK);
    CHECK(plan.seed_offset == 20ul * 256ul + 20ul);
    CHECK(plan.seed_bytes == 80ul);
    CHECK(plan.value == 0x12341234ul);
    CHECK(plan.rows == 29ul);
    CHECK(plan.row_dwords == 20ul);
    CHECK(plan.source == R6_GPU_BASE + 20ul * 256ul + 20ul);
    CHECK(plan.source_step == 0ul);
    CHECK(plan.destination == R6_GPU_BASE + 21ul * 256ul + 20ul);
    CHECK(plan.destination_step == 256ul);

    /* 8 and 32 bpp. */
    base_fill(&fill);
    fill.bytes_per_pixel = 1ul;
    fill.left = 12ul;
    fill.width = 64ul;
    fill.color = 0xa5ul;
    CHECK(check_fill(&fill));
    base_fill(&fill);
    fill.bytes_per_pixel = 4ul;
    fill.width = 33ul;
    fill.color = 0x00c0ffeeul;
    CHECK(check_fill(&fill));

    /* One row: the seed alone, no packets. */
    base_fill(&fill);
    fill.height = 1ul;
    CHECK(v9x_r6_dma_plan_fill(&fill, &plan) == V9X_STATUS_OK);
    CHECK(plan.rows == 0ul && plan.seed_bytes == 80ul);
    CHECK(check_fill(&fill));

    /* A whole surface whose pitch is its width: still a row a packet, each
     * from the seed row, never from a row a copy wrote. */
    base_fill(&fill);
    fill.left = 0ul;
    fill.top = 0ul;
    fill.width = 128ul;
    fill.height = 64ul;
    CHECK(v9x_r6_dma_plan_fill(&fill, &plan) == V9X_STATUS_OK);
    CHECK(plan.rows == 63ul && plan.source_step == 0ul);
    CHECK(check_fill(&fill));

    /* Two dwords a row is the narrowest the engine copied. */
    base_fill(&fill);
    fill.width = 4ul;
    CHECK(check_fill(&fill));
}

static void test_fill_refusals(void)
{
    struct v9x_r6_dma_fill fill;
    struct v9x_r6_dma_plan plan;

    /* Off dwords is the CPU's: there is no byte copy. */
    base_fill(&fill);
    fill.left = 11ul;
    CHECK(v9x_r6_dma_plan_fill(&fill, &plan) == V9X_STATUS_UNSUPPORTED);
    base_fill(&fill);
    fill.width = 41ul;
    CHECK(v9x_r6_dma_plan_fill(&fill, &plan) == V9X_STATUS_UNSUPPORTED);
    base_fill(&fill);
    fill.bytes_per_pixel = 3ul;
    CHECK(v9x_r6_dma_plan_fill(&fill, &plan) == V9X_STATUS_UNSUPPORTED);

    /* A row of one dword would be a one-dword COPY, which did nothing. */
    base_fill(&fill);
    fill.width = 2ul;
    CHECK(v9x_r6_dma_plan_fill(&fill, &plan) == V9X_STATUS_UNSUPPORTED);

    /* Outside the surface's pitch, the heap, or 4 GiB. */
    base_fill(&fill);
    fill.left = 120ul;
    CHECK(v9x_r6_dma_plan_fill(&fill, &plan) == V9X_STATUS_INVALID_ARGUMENT);
    base_fill(&fill);
    fill.top = 250ul;
    CHECK(v9x_r6_dma_plan_fill(&fill, &plan) == V9X_STATUS_INVALID_ARGUMENT);
    base_fill(&fill);
    fill.height = 0ul;
    CHECK(v9x_r6_dma_plan_fill(&fill, &plan) == V9X_STATUS_INVALID_ARGUMENT);
    base_fill(&fill);
    fill.top = 0xfffffff0ul;
    CHECK(v9x_r6_dma_plan_fill(&fill, &plan) == V9X_STATUS_INVALID_ARGUMENT);
    base_fill(&fill);
    fill.gpu_base = 0xffffff00ul;
    CHECK(v9x_r6_dma_plan_fill(&fill, &plan) == V9X_STATUS_INVALID_ARGUMENT);
}

static void test_copy(void)
{
    struct v9x_r6_dma_copy copy;
    struct v9x_r6_dma_plan plan;

    /* Disjoint: a row a packet, no seed. */
    base_copy(&copy);
    CHECK(check_copy(&copy));
    CHECK(v9x_r6_dma_plan_copy(&copy, &plan) == V9X_STATUS_OK);
    CHECK(plan.rows == 20ul && plan.row_dwords == 15ul);
    CHECK(plan.seed_bytes == 0ul);

    /* 8 and 32 bpp, between two surfaces of different pitch. */
    base_copy(&copy);
    copy.bytes_per_pixel = 1ul;
    copy.destination_offset = 32768ul;
    copy.destination_pitch_bytes = 200ul;
    copy.source_left = 4ul;
    copy.destination_left = 8ul;
    copy.width = 32ul;
    copy.destination_top = 5ul;
    CHECK(check_copy(&copy));
    base_copy(&copy);
    copy.bytes_per_pixel = 4ul;
    copy.width = 20ul;
    copy.destination_left = 30ul;
    CHECK(check_copy(&copy));

    /* Contiguous rows merge into one packet. */
    base_copy(&copy);
    copy.source_left = 0ul;
    copy.destination_left = 0ul;
    copy.width = 128ul;
    copy.destination_top = 120ul;
    CHECK(v9x_r6_dma_plan_copy(&copy, &plan) == V9X_STATUS_OK);
    CHECK(plan.rows == 1ul && plan.row_dwords == 64ul * 20ul);
    CHECK(check_copy(&copy));
}

static void test_copy_overlap(void)
{
    struct v9x_r6_dma_copy copy;
    struct v9x_r6_dma_plan plan;

    /* Down by three rows: bottom-up. */
    base_copy(&copy);
    copy.destination_left = copy.source_left;
    copy.destination_top = copy.source_top + 3ul;
    CHECK(v9x_r6_dma_plan_copy(&copy, &plan) == V9X_STATUS_OK);
    CHECK(plan.destination_step == 0ul - 256ul);
    CHECK(check_copy(&copy));

    /* Down by one row and right by two pixels. */
    base_copy(&copy);
    copy.destination_left = copy.source_left + 2ul;
    copy.destination_top = copy.source_top + 1ul;
    CHECK(check_copy(&copy));

    /* Up by three rows: top-down. */
    base_copy(&copy);
    copy.source_top = 40ul;
    copy.destination_left = copy.source_left;
    copy.destination_top = 37ul;
    CHECK(v9x_r6_dma_plan_copy(&copy, &plan) == V9X_STATUS_OK);
    CHECK(plan.destination_step == 256ul);
    CHECK(check_copy(&copy));

    /* Sideways within the same rows, both directions: one packet a row,
     * whose memmove the probe measured. */
    base_copy(&copy);
    copy.destination_top = copy.source_top;
    copy.destination_left = copy.source_left + 4ul;
    CHECK(check_copy(&copy));
    base_copy(&copy);
    copy.source_left = 20ul;
    copy.destination_top = copy.source_top;
    copy.destination_left = 14ul;
    CHECK(check_copy(&copy));

    /* Overlapping between two pitches: the row order cannot be chosen. */
    base_copy(&copy);
    copy.destination_pitch_bytes = 512ul;
    copy.destination_left = copy.source_left;
    copy.destination_top = 5ul;
    CHECK(v9x_r6_dma_plan_copy(&copy, &plan) == V9X_STATUS_UNSUPPORTED);
}

static void test_copy_refusals(void)
{
    struct v9x_r6_dma_copy copy;
    struct v9x_r6_dma_plan plan;

    /* Off dwords: no byte copy on R6xx. */
    base_copy(&copy);
    copy.source_left = 11ul;
    CHECK(v9x_r6_dma_plan_copy(&copy, &plan) == V9X_STATUS_UNSUPPORTED);
    base_copy(&copy);
    copy.width = 31ul;
    CHECK(v9x_r6_dma_plan_copy(&copy, &plan) == V9X_STATUS_UNSUPPORTED);
    base_copy(&copy);
    copy.bytes_per_pixel = 3ul;
    CHECK(v9x_r6_dma_plan_copy(&copy, &plan) == V9X_STATUS_UNSUPPORTED);

    /* One dword a row. */
    base_copy(&copy);
    copy.width = 2ul;
    CHECK(v9x_r6_dma_plan_copy(&copy, &plan) == V9X_STATUS_UNSUPPORTED);

    base_copy(&copy);
    copy.source_left = 120ul;
    CHECK(v9x_r6_dma_plan_copy(&copy, &plan) == V9X_STATUS_INVALID_ARGUMENT);
    base_copy(&copy);
    copy.destination_top = 250ul;
    CHECK(v9x_r6_dma_plan_copy(&copy, &plan) == V9X_STATUS_INVALID_ARGUMENT);
}

/* A whole 1600x1200x32 surface is 1,920,000 dwords, past one packet's
 * 0FFFEh: a row a packet. */
static void test_copy_past_one_packet(void)
{
    struct v9x_r6_dma_copy copy;
    struct v9x_r6_dma_plan plan;

    memset(&copy, 0, sizeof(copy));
    copy.gpu_base = 0xe0000000ul;
    copy.vram_bytes = 0x01000000ul;
    copy.source_offset = 0x00000000ul;
    copy.source_pitch_bytes = 6400ul;
    copy.destination_offset = 0x00800000ul;
    copy.destination_pitch_bytes = 6400ul;
    copy.bytes_per_pixel = 4ul;
    copy.width = 1600ul;
    copy.height = 1200ul;
    CHECK(v9x_r6_dma_plan_copy(&copy, &plan) == V9X_STATUS_OK);
    CHECK(plan.rows == 1200ul && plan.row_dwords == 1600ul);
    CHECK(plan.source == 0xe0000000ul && plan.destination == 0xe0800000ul);
}

/* The write probe's packets, so the builder and the measured stream agree. */
static void test_probe_packets(void)
{
    struct v9x_r6_dma_plan plan;
    v9x_u32 packet[V9X_R6_DMA_PACKET_DWORDS_MAX];
    v9x_u32 high;
    v9x_u32 low;

    memset(&plan, 0, sizeof(plan));
    plan.rows = 1ul;
    plan.row_dwords = 256ul;
    plan.destination = 0xef003010ul;
    plan.source = 0xef002000ul;
    CHECK(v9x_r6_dma_plan_row(&plan, 0ul, 0ul, packet) == 4ul);
    CHECK(packet[0] == 0x30000100ul);
    CHECK(packet[1] == 0xef003010ul && packet[2] == 0xef002000ul);
    CHECK(packet[3] == 0ul);
    CHECK(v9x_r6_dma_plan_row(&plan, 0ul, 0x0ful, packet) == 4ul);
    CHECK(packet[3] == 0x000f000ful);
    CHECK(v9x_r6_dma_plan_row(&plan, 1ul, 0ul, packet) == 0ul);

    CHECK(v9x_r6_dma_fence(0ul, 0xef001000ul, 0x11111111ul, packet) == 4ul);
    CHECK(packet[0] == 0x60000000ul && packet[1] == 0xef001000ul);
    CHECK(packet[2] == 0ul && packet[3] == 0x11111111ul);

    /* DMA_RB_BASE for the probe's ring at EF000000. */
    CHECK(v9x_r6_dma_ring_base(0ul, 0xef000000ul) == 0x00ef0000ul);

    CHECK(v9x_r6_dma_pad(0ul) == 0ul && v9x_r6_dma_pad(5ul) == 11ul);
    CHECK(v9x_r6_dma_ring_advance(0ul, 4ul) == 16ul);
    CHECK(v9x_r6_dma_ring_advance(V9X_R6_RING_DWORDS - 16ul, 16ul) == 0ul);

    /* M-NZHWBR's MC_VM_FB_LOCATION, and one whose window would carry. */
    CHECK(v9x_r6_fb_start(0x00ef00e0ul, &high, &low) == V9X_STATUS_OK &&
          high == 0ul && low == 0xe0000000ul);
    CHECK(v9x_r6_fb_start(0x00ff00f0ul, &high, &low) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

unsigned int v9x_run_r6_dma_tests(void)
{
    failures = 0u;
    test_fill();
    test_fill_refusals();
    test_copy();
    test_copy_overlap();
    test_copy_refusals();
    test_copy_past_one_packet();
    test_probe_packets();
    return failures;
}
