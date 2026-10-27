#include <stdio.h>
#include <string.h>

#include "velocity9x/eg_dma.h"
#include "velocity9x/eg_cp_microcode.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

/* A small heap at a GPU address that is not zero, so a packet that forgot
 * the base lands outside it. */
#define EG_GPU_BASE  0x00100000ul
#define EG_HEAP      65536ul

static unsigned char vram[EG_HEAP];
static unsigned char expected[EG_HEAP];

static void seed(unsigned char *buffer)
{
    v9x_u32 index;

    for (index = 0ul; index < EG_HEAP; ++index) {
        buffer[index] = (unsigned char)((index * 131ul + 7ul) >> 1);
    }
}

/* The engine as the write probe measured it: fills write dwords, copies
 * move bytes with memmove semantics. Returns 0 on a packet outside the
 * heap or one this model does not know. */
static int execute(const v9x_u32 *packet)
{
    v9x_u32 command = packet[0] >> 28;
    v9x_u32 sub = (packet[0] >> 20) & 0xfful;
    v9x_u32 count = packet[0] & V9X_EG_DMA_COUNT_MAX;
    v9x_u32 destination = packet[1] - EG_GPU_BASE;
    v9x_u32 bytes;
    v9x_u32 index;

    if (command == V9X_EG_DMA_CMD_CONSTANT_FILL) {
        bytes = count * 4ul;
        if (packet[3] != 0ul || destination > EG_HEAP ||
            bytes > EG_HEAP - destination) {
            return 0;
        }
        for (index = 0ul; index < bytes; ++index) {
            vram[destination + index] =
                (unsigned char)(packet[2] >> ((index & 3ul) * 8ul));
        }
        return 1;
    }
    if (command == V9X_EG_DMA_CMD_COPY) {
        v9x_u32 source = packet[2] - EG_GPU_BASE;

        bytes = sub == V9X_EG_DMA_COPY_BYTE ? count : count * 4ul;
        if ((sub != V9X_EG_DMA_COPY_BYTE && sub != V9X_EG_DMA_COPY_DWORD) ||
            packet[3] != 0ul || packet[4] != 0ul ||
            destination > EG_HEAP || bytes > EG_HEAP - destination ||
            source > EG_HEAP || bytes > EG_HEAP - source) {
            return 0;
        }
        if (sub == V9X_EG_DMA_COPY_DWORD &&
            ((packet[1] | packet[2]) & 3ul) != 0ul) {
            return 0;
        }
        memmove(vram + destination, vram + source, bytes);
        return 1;
    }
    return 0;
}

static int run_plan(const struct v9x_eg_dma_plan *plan)
{
    v9x_u32 packet[V9X_EG_DMA_PACKET_DWORDS_MAX];
    v9x_u32 index;

    for (index = 0ul; index < plan->rows; ++index) {
        if (v9x_eg_dma_plan_row(plan, index, packet) == 0ul ||
            !execute(packet)) {
            return 0;
        }
    }
    return 1;
}

static void base_fill(struct v9x_eg_dma_fill *fill)
{
    memset(fill, 0, sizeof(*fill));
    fill->gpu_base = EG_GPU_BASE;
    fill->vram_bytes = EG_HEAP;
    fill->target_offset = 0ul;
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
    copy->gpu_base = EG_GPU_BASE;
    copy->vram_bytes = EG_HEAP;
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

/* The reference: a CPU fill of the same rectangle. */
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

/* The reference: a CPU copy through a scratch buffer, so overlap does not
 * matter. */
static unsigned char scratch[EG_HEAP];

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
    return memcmp(vram, expected, EG_HEAP) == 0;
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
    return memcmp(vram, expected, EG_HEAP) == 0;
}

static void test_fill(void)
{
    struct v9x_eg_dma_fill fill;
    struct v9x_eg_dma_plan plan;

    /* 16 bpp, even left and width: one packet a row, colour doubled. */
    base_fill(&fill);
    CHECK(check_fill(&fill));
    CHECK(v9x_eg_dma_plan_fill(&fill, &plan) == V9X_STATUS_OK);
    CHECK(plan.rows == 30ul);
    CHECK(plan.row_bytes == 80ul);
    CHECK(plan.value == 0x12341234ul);
    CHECK(plan.destination == EG_GPU_BASE + 20ul * 256ul + 20ul);

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

    /* A whole surface whose pitch is its width is one run. */
    base_fill(&fill);
    fill.left = 0ul;
    fill.top = 0ul;
    fill.width = 128ul;
    fill.height = 64ul;
    CHECK(v9x_eg_dma_plan_fill(&fill, &plan) == V9X_STATUS_OK);
    CHECK(plan.rows == 1ul);
    CHECK(plan.row_bytes == 256ul * 64ul);
    CHECK(check_fill(&fill));
}

static void test_fill_refusals(void)
{
    struct v9x_eg_dma_fill fill;
    struct v9x_eg_dma_plan plan;

    /* A row that does not start or end on a dword is the CPU's. */
    base_fill(&fill);
    fill.left = 11ul;
    CHECK(v9x_eg_dma_plan_fill(&fill, &plan) == V9X_STATUS_UNSUPPORTED);
    base_fill(&fill);
    fill.width = 41ul;
    CHECK(v9x_eg_dma_plan_fill(&fill, &plan) == V9X_STATUS_UNSUPPORTED);
    base_fill(&fill);
    fill.bytes_per_pixel = 3ul;
    CHECK(v9x_eg_dma_plan_fill(&fill, &plan) == V9X_STATUS_UNSUPPORTED);

    /* Outside the surface's pitch or the heap. */
    base_fill(&fill);
    fill.left = 120ul;
    CHECK(v9x_eg_dma_plan_fill(&fill, &plan) == V9X_STATUS_INVALID_ARGUMENT);
    base_fill(&fill);
    fill.top = 250ul;
    CHECK(v9x_eg_dma_plan_fill(&fill, &plan) == V9X_STATUS_INVALID_ARGUMENT);
    base_fill(&fill);
    fill.height = 0ul;
    CHECK(v9x_eg_dma_plan_fill(&fill, &plan) == V9X_STATUS_INVALID_ARGUMENT);
    base_fill(&fill);
    fill.top = 0xfffffff0ul;
    CHECK(v9x_eg_dma_plan_fill(&fill, &plan) == V9X_STATUS_INVALID_ARGUMENT);

    /* A heap that would run past 4 GiB in the GPU's address space. */
    base_fill(&fill);
    fill.gpu_base = 0xffffff00ul;
    CHECK(v9x_eg_dma_plan_fill(&fill, &plan) == V9X_STATUS_INVALID_ARGUMENT);
}

static void test_copy(void)
{
    struct v9x_eg_dma_copy copy;
    struct v9x_eg_dma_plan plan;

    /* Disjoint, dword-aligned: dword copies a row at a time. */
    base_copy(&copy);
    CHECK(check_copy(&copy));
    CHECK(v9x_eg_dma_plan_copy(&copy, &plan) == V9X_STATUS_OK);
    CHECK(plan.byte_copy == 0ul);
    CHECK(plan.rows == 20ul);

    /* An odd pixel at 16 bpp takes byte mode, still disjoint. */
    base_copy(&copy);
    copy.source_left = 11ul;
    CHECK(check_copy(&copy));
    CHECK(v9x_eg_dma_plan_copy(&copy, &plan) == V9X_STATUS_OK);
    CHECK(plan.byte_copy != 0ul);

    /* 8 and 32 bpp, between two surfaces of different pitch. */
    base_copy(&copy);
    copy.bytes_per_pixel = 1ul;
    copy.destination_offset = 32768ul;
    copy.destination_pitch_bytes = 200ul;
    copy.source_left = 3ul;
    copy.destination_left = 7ul;
    copy.destination_top = 5ul;
    CHECK(check_copy(&copy));
    base_copy(&copy);
    copy.bytes_per_pixel = 4ul;
    copy.width = 20ul;
    copy.destination_left = 30ul;
    CHECK(check_copy(&copy));
}

static void test_copy_overlap(void)
{
    struct v9x_eg_dma_copy copy;
    struct v9x_eg_dma_plan plan;

    /* Down by three rows: bottom-up. */
    base_copy(&copy);
    copy.destination_left = copy.source_left;
    copy.destination_top = copy.source_top + 3ul;
    CHECK(v9x_eg_dma_plan_copy(&copy, &plan) == V9X_STATUS_OK);
    CHECK(plan.destination_step == 0ul - 256ul);
    CHECK(check_copy(&copy));

    /* Up by three rows: top-down. */
    base_copy(&copy);
    copy.source_top = 40ul;
    copy.destination_left = copy.source_left;
    copy.destination_top = 37ul;
    CHECK(v9x_eg_dma_plan_copy(&copy, &plan) == V9X_STATUS_OK);
    CHECK(plan.destination_step == 256ul);
    CHECK(check_copy(&copy));

    /* Sideways within the same rows, both directions, dword-aligned: the
     * engine's memmove does it. */
    base_copy(&copy);
    copy.destination_top = copy.source_top;
    copy.destination_left = copy.source_left + 4ul;
    CHECK(check_copy(&copy));
    base_copy(&copy);
    copy.source_left = 20ul;
    copy.destination_top = copy.source_top;
    copy.destination_left = 14ul;
    CHECK(check_copy(&copy));

    /* Overlapping in byte mode was never measured: refused. */
    base_copy(&copy);
    copy.destination_top = copy.source_top + 1ul;
    copy.destination_left = copy.source_left + 1ul;
    CHECK(v9x_eg_dma_plan_copy(&copy, &plan) == V9X_STATUS_UNSUPPORTED);
}

static void test_copy_refusals(void)
{
    struct v9x_eg_dma_copy copy;
    struct v9x_eg_dma_plan plan;

    base_copy(&copy);
    copy.source_left = 120ul;
    CHECK(v9x_eg_dma_plan_copy(&copy, &plan) == V9X_STATUS_INVALID_ARGUMENT);
    base_copy(&copy);
    copy.destination_top = 250ul;
    CHECK(v9x_eg_dma_plan_copy(&copy, &plan) == V9X_STATUS_INVALID_ARGUMENT);
    base_copy(&copy);
    copy.bytes_per_pixel = 3ul;
    CHECK(v9x_eg_dma_plan_copy(&copy, &plan) == V9X_STATUS_UNSUPPORTED);
}

static void test_ring_helpers(void)
{
    v9x_u32 packet[V9X_EG_DMA_FENCE_DWORDS];
    v9x_u32 ok;

    CHECK(v9x_eg_dma_pad(0ul) == 0ul);
    CHECK(v9x_eg_dma_pad(5ul) == 11ul);
    CHECK(v9x_eg_dma_pad(16ul) == 0ul);
    CHECK(v9x_eg_dma_pad(31ul) == 1ul);

    /* The position after a submission: padded, and back to 0 when it ends
     * exactly at the ring's end. M-BXMFA8 wrote DMA_RB_WPTR 8000h, the
     * ring's size, in 3DMark 2000 (boot 28); the engine stopped at 6E90h
     * and the idle wait never saw its read pointer reach 8000h. */
    CHECK(v9x_eg_dma_ring_advance(0ul, 4ul) == 16ul);
    CHECK(v9x_eg_dma_ring_advance(32ul, 16ul) == 48ul);
    CHECK(v9x_eg_dma_ring_advance(V9X_EG_RING_DWORDS - 32ul, 5ul) ==
          V9X_EG_RING_DWORDS - 16ul);
    CHECK(v9x_eg_dma_ring_advance(V9X_EG_RING_DWORDS - 16ul, 16ul) == 0ul);
    CHECK(v9x_eg_dma_ring_advance(V9X_EG_RING_DWORDS - 16ul, 3ul) == 0ul);

    /* The write probe's fence, byte for byte. */
    CHECK(v9x_eg_dma_fence(0x0f001000ul, 0x11111111ul, packet) == 4ul);
    CHECK(packet[0] == 0x60000000ul);
    CHECK(packet[1] == 0x0f001000ul);
    CHECK(packet[2] == 0ul);
    CHECK(packet[3] == 0x11111111ul);

    /* M-BXMFA8's MC_VM_FB_LOCATION, then one past 4 GiB. */
    CHECK(v9x_eg_fb_start(0x003f0000ul, &ok) == 0ul && ok == 1ul);
    CHECK(v9x_eg_fb_start(0x00ff00f0ul, &ok) == 0xf0000000ul && ok == 1ul);
    CHECK(v9x_eg_fb_start(0x01ff0100ul, &ok) == 0ul && ok == 0ul);
}

/* The write probe's packet headers, so the builder and the measured
 * stream agree. */
static void test_probe_headers(void)
{
    struct v9x_eg_dma_plan plan;
    v9x_u32 packet[V9X_EG_DMA_PACKET_DWORDS_MAX];

    memset(&plan, 0, sizeof(plan));
    plan.kind = V9X_EG_DMA_PLAN_COPY;
    plan.rows = 1ul;
    plan.row_bytes = 1024ul;
    CHECK(v9x_eg_dma_plan_row(&plan, 0ul, packet) == 5ul);
    CHECK(packet[0] == 0x30000100ul);
    plan.byte_copy = 1ul;
    plan.row_bytes = 37ul;
    CHECK(v9x_eg_dma_plan_row(&plan, 0ul, packet) == 5ul);
    CHECK(packet[0] == 0x34000025ul);
    plan.kind = V9X_EG_DMA_PLAN_FILL;
    plan.row_bytes = 1024ul;
    CHECK(v9x_eg_dma_plan_row(&plan, 0ul, packet) == 4ul);
    CHECK(packet[0] == 0xd0000100ul);
    CHECK(v9x_eg_dma_plan_row(&plan, 1ul, packet) == 0ul);
}

/* The generated microcode: radeon_ucode.h's sizes, and the first and last
 * dwords of linux-firmware's images converted from big-endian, the first
 * as V9XEGCP read it from the files on M-BXMFA8 (PfpFirst, MeFirst). */
typedef char v9x_eg_pfp_size_matches[
    sizeof(v9x_eg_pfp_microcode) == 1120u * sizeof(v9x_u32) ? 1 : -1];
typedef char v9x_eg_me_size_matches[
    sizeof(v9x_eg_me_microcode) == 1376u * sizeof(v9x_u32) ? 1 : -1];

static void test_cp_microcode(void)
{
    CHECK(v9x_eg_pfp_microcode[0] == 0x7c408000ul);
    CHECK(v9x_eg_me_microcode[0] == 0x7c408000ul);
}

unsigned int v9x_run_eg_dma_tests(void)
{
    failures = 0u;
    test_cp_microcode();
    test_fill();
    test_fill_refusals();
    test_copy();
    test_copy_overlap();
    test_copy_refusals();
    test_ring_helpers();
    test_probe_headers();
    return failures;
}
