#include <stdio.h>
#include <string.h>

#include "velocity9x/eg_dma.h"
#include "velocity9x/si_dma.h"
#include "velocity9x/si_gfx_microcode.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

/* A small heap whose GPU address has a high half, though a discrete
 * Oland's VRAM is expected at GPU 0 (unmeasured): a packet that drops
 * either half lands outside it. */
#define SI_GPU_HIGH  0x0000000ful
#define SI_GPU_LOW   0x00100000ul
#define SI_HEAP      65536ul

static unsigned char vram[SI_HEAP];
static unsigned char expected[SI_HEAP];

static void seed(unsigned char *buffer)
{
    v9x_u32 index;

    for (index = 0ul; index < SI_HEAP; ++index) {
        buffer[index] = (unsigned char)((index * 131ul + 7ul) >> 1);
    }
}

/* The engine as Linux drives it (amdgpu si_dma.c): a copy with bit 26 set
 * moves a byte count, a fill writes a dword count of its value, each at
 * most FFFF8h bytes. The model copies forward, byte by byte, so a packet
 * whose spans overlap would show it. Returns 0 on a packet outside the
 * heap, a high half other than the heap's, or anything else. */
static int execute(const v9x_u32 *packet, v9x_u32 *dwords)
{
    v9x_u32 command = packet[0] >> 28;
    v9x_u32 count = packet[0] & 0x000ffffful;
    v9x_u32 destination;
    v9x_u32 source;
    v9x_u32 index;

    if (command == V9X_EG_DMA_CMD_CONSTANT_FILL) {
        destination = packet[1] - SI_GPU_LOW;
        if ((packet[0] & 0x0ff00000ul) != 0ul ||
            packet[3] != (SI_GPU_HIGH << 16) || count == 0ul ||
            count * 4ul > V9X_SI_DMA_CHUNK_BYTES_MAX ||
            destination > SI_HEAP || count * 4ul > SI_HEAP - destination) {
            return 0;
        }
        for (index = 0ul; index < count * 4ul; ++index) {
            vram[destination + index] =
                (unsigned char)(packet[2] >> ((index & 3ul) * 8ul));
        }
        *dwords = V9X_SI_DMA_FILL_DWORDS;
        return 1;
    }
    if (command == V9X_EG_DMA_CMD_COPY) {
        destination = packet[1] - SI_GPU_LOW;
        source = packet[2] - SI_GPU_LOW;
        if ((packet[0] & 0x0ff00000ul) != V9X_SI_DMA_COPY_BYTES_BIT ||
            packet[3] != SI_GPU_HIGH || packet[4] != SI_GPU_HIGH ||
            count == 0ul || count > V9X_SI_DMA_CHUNK_BYTES_MAX ||
            destination > SI_HEAP || count > SI_HEAP - destination ||
            source > SI_HEAP || count > SI_HEAP - source) {
            return 0;
        }
        for (index = 0ul; index < count; ++index) {
            vram[destination + index] = vram[source + index];
        }
        *dwords = V9X_SI_DMA_COPY_DWORDS;
        return 1;
    }
    return 0;
}

static int run_plan(const struct v9x_eg_dma_plan *plan)
{
    v9x_u32 packets[V9X_SI_DMA_ROW_DWORDS_MAX];
    v9x_u32 index;
    v9x_u32 count;
    v9x_u32 at;
    v9x_u32 used;

    if (v9x_si_dma_plan_check(plan) != V9X_STATUS_OK) {
        return 0;
    }
    for (index = 0ul; index < plan->rows; ++index) {
        count = v9x_si_dma_plan_row(plan, index, SI_GPU_HIGH, packets);
        if (count == 0ul || count > V9X_SI_DMA_ROW_DWORDS_MAX) {
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
    fill->gpu_base = SI_GPU_LOW;
    fill->vram_bytes = SI_HEAP;
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
    copy->gpu_base = SI_GPU_LOW;
    copy->vram_bytes = SI_HEAP;
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

static void reference_copy(const struct v9x_eg_dma_copy *copy)
{
    v9x_u32 y;
    v9x_u32 bytes = copy->width * copy->bytes_per_pixel;

    for (y = 0ul; y < copy->height; ++y) {
        memmove(expected + copy->destination_offset +
                (copy->destination_top + y) * copy->destination_pitch_bytes +
                copy->destination_left * copy->bytes_per_pixel,
                expected + copy->source_offset +
                (copy->source_top + y) * copy->source_pitch_bytes +
                copy->source_left * copy->bytes_per_pixel, bytes);
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
    return memcmp(vram, expected, SI_HEAP) == 0;
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
    return memcmp(vram, expected, SI_HEAP) == 0;
}

/* The generated images: sizes, versions and edges, as read from
 * linux-firmware's amdgpu/oland_*.bin payloads (offset 100h). */
typedef char v9x_si_rlc_dwords[V9X_SI_RLC_MICROCODE_DWORDS == 2048u ? 1 : -1];
typedef char v9x_si_pfp_dwords[V9X_SI_PFP_MICROCODE_DWORDS == 2144u ? 1 : -1];

static void test_microcode(void)
{
    CHECK(v9x_si_rlc_microcode_version == 0x00000001ul);
    CHECK(v9x_si_pfp_microcode_version == 0x00000054ul);
    CHECK(v9x_si_ce_microcode_version == 0x0000003dul);
    CHECK(v9x_si_me_microcode_version == 0x00000091ul);
    CHECK(v9x_si_rlc_microcode[0] == 0xc4080027ul);
    CHECK(v9x_si_rlc_microcode[1] == 0x308c0002ul);
    CHECK(v9x_si_pfp_microcode[0] == 0x7c408001ul);
    CHECK(v9x_si_pfp_microcode[V9X_SI_PFP_MICROCODE_DWORDS - 1u] ==
          0x00000007ul);
    CHECK(v9x_si_ce_microcode[V9X_SI_CE_MICROCODE_DWORDS - 1u] ==
          0x00000002ul);
    CHECK(v9x_si_me_microcode[V9X_SI_ME_MICROCODE_DWORDS - 1u] ==
          0x000f05fcul);
}

/* The packets word for word, as amdgpu's si_dma.c builds them. */
static void test_packets(void)
{
    struct v9x_eg_dma_plan plan;
    v9x_u32 out[V9X_SI_DMA_ROW_DWORDS_MAX];

    memset(&plan, 0, sizeof(plan));
    plan.kind = V9X_EG_DMA_PLAN_COPY;
    plan.rows = 1ul;
    plan.row_bytes = 37ul;
    plan.source = 0x0f002001ul;
    plan.destination = 0x0f004403ul;
    plan.byte_copy = 1ul;
    CHECK(v9x_si_dma_plan_check(&plan) == V9X_STATUS_OK);
    CHECK(v9x_si_dma_plan_row(&plan, 0ul, 0x1ful, out) ==
          V9X_SI_DMA_COPY_DWORDS);
    CHECK(out[0] == 0x34000025ul);
    CHECK(out[1] == 0x0f004403ul);
    CHECK(out[2] == 0x0f002001ul);
    CHECK(out[3] == 0x1ful);
    CHECK(out[4] == 0x1ful);
    CHECK(v9x_si_dma_plan_row(&plan, 1ul, 0x1ful, out) == 0ul);

    /* A dword plan is sent as the same byte copy: Linux never sends SI a
     * dword count. */
    plan.row_bytes = 64ul;
    plan.byte_copy = 0ul;
    CHECK(v9x_si_dma_plan_row(&plan, 0ul, 0ul, out) ==
          V9X_SI_DMA_COPY_DWORDS);
    CHECK(out[0] == 0x34000040ul);

    memset(&plan, 0, sizeof(plan));
    plan.kind = V9X_EG_DMA_PLAN_FILL;
    plan.rows = 1ul;
    plan.row_bytes = 1024ul;
    plan.destination = 0x0f006010ul;
    plan.value = 0x11223344ul;
    CHECK(v9x_si_dma_plan_row(&plan, 0ul, 0x1ful, out) ==
          V9X_SI_DMA_FILL_DWORDS);
    CHECK(out[0] == 0xd0000100ul);
    CHECK(out[1] == 0x0f006010ul);
    CHECK(out[2] == 0x11223344ul);
    CHECK(out[3] == 0x001f0000ul);

    /* The high half is eight bits in both packets. */
    CHECK(v9x_si_dma_plan_row(&plan, 0ul, 0x1ffu, out) ==
          V9X_SI_DMA_FILL_DWORDS);
    CHECK(out[3] == 0x00ff0000ul);

    CHECK(v9x_si_dma_plan_check(0) == V9X_STATUS_INVALID_ARGUMENT);
}

static void test_fill(void)
{
    struct v9x_eg_dma_fill fill;

    base_fill(&fill);
    CHECK(check_fill(&fill));
    fill.bytes_per_pixel = 4ul;
    fill.color = 0x89abcdeful;
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
    copy.bytes_per_pixel = 4ul;
    copy.width = 20ul;
    copy.destination_left = 30ul;
    CHECK(check_copy(&copy));
    /* A row off the dword: a byte copy on Evergreen, the same packet
     * here. */
    base_copy(&copy);
    copy.source_left = 3ul;
    copy.width = 13ul;
    CHECK(check_copy(&copy));
}

/* An overlapping copy is refused, whichever way it overlaps: the plan's
 * bottom-up order is right for a dword copy on Evergreen, but no byte
 * copy has run overlapping. */
static void test_overlap_refused(void)
{
    struct v9x_eg_dma_copy copy;
    struct v9x_eg_dma_plan plan;

    base_copy(&copy);
    copy.destination_left = 12ul;
    copy.destination_top = 12ul;
    CHECK(v9x_eg_dma_plan_copy(&copy, &plan) == V9X_STATUS_OK);
    CHECK(v9x_si_dma_plan_check(&plan) == V9X_STATUS_UNSUPPORTED);

    copy.destination_left = 8ul;
    copy.destination_top = 9ul;
    CHECK(v9x_eg_dma_plan_copy(&copy, &plan) == V9X_STATUS_OK);
    CHECK(v9x_si_dma_plan_check(&plan) == V9X_STATUS_UNSUPPORTED);

    /* Inside one row (a dword apart, so Evergreen plans it). */
    copy.destination_left = 12ul;
    copy.destination_top = 10ul;
    CHECK(v9x_eg_dma_plan_copy(&copy, &plan) == V9X_STATUS_OK);
    CHECK(v9x_si_dma_plan_check(&plan) == V9X_STATUS_UNSUPPORTED);

    /* Side by side on one surface: the spans interleave, no row shares a
     * byte, so it is drawn. Then one pixel closer, refused. */
    copy.destination_left = 40ul;
    copy.destination_top = 10ul;
    CHECK(check_copy(&copy));
    copy.destination_top = 15ul;
    CHECK(check_copy(&copy));
    copy.destination_left = 38ul;
    CHECK(v9x_eg_dma_plan_copy(&copy, &plan) == V9X_STATUS_OK);
    CHECK(v9x_si_dma_plan_check(&plan) == V9X_STATUS_UNSUPPORTED);

    /* Below, sharing no row: drawn. */
    copy.destination_left = 10ul;
    copy.destination_top = 30ul;
    CHECK(check_copy(&copy));
}

/* A row longer than one packet goes in FFFF8h-byte chunks, in order, each
 * carrying both high halves; past five chunks the plan is refused. */
static void test_long_rows(void)
{
    struct v9x_eg_dma_plan plan;
    v9x_u32 out[V9X_SI_DMA_ROW_DWORDS_MAX];

    memset(&plan, 0, sizeof(plan));
    plan.kind = V9X_EG_DMA_PLAN_FILL;
    plan.rows = 1ul;
    plan.row_bytes = 0x003ffffcul;
    plan.destination = 0x01000000ul;
    plan.value = 0x55aa55aaul;
    CHECK(v9x_si_dma_plan_check(&plan) == V9X_STATUS_OK);
    CHECK(v9x_si_dma_plan_row(&plan, 0ul, 0ul, out) ==
          5ul * V9X_SI_DMA_FILL_DWORDS);
    CHECK(out[0] == 0xd003fffeul);
    CHECK(out[4 + 1] == 0x01000000ul + 0x000ffff8ul);
    CHECK(out[16] == (0xd0000000ul | ((0x003ffffcul - 4ul * 0x000ffff8ul) /
                                      4ul)));

    memset(&plan, 0, sizeof(plan));
    plan.kind = V9X_EG_DMA_PLAN_COPY;
    plan.rows = 1ul;
    plan.row_bytes = 0x00100000ul;
    plan.source = 0x01000000ul;
    plan.destination = 0x02000000ul;
    CHECK(v9x_si_dma_plan_row(&plan, 0ul, 0ul, out) ==
          2ul * V9X_SI_DMA_COPY_DWORDS);
    CHECK(out[0] == 0x340ffff8ul);
    CHECK(out[5] == 0x34000008ul);
    CHECK(out[6] == 0x02000000ul + 0x000ffff8ul);
    CHECK(out[7] == 0x01000000ul + 0x000ffff8ul);

    plan.row_bytes = 5ul * 0x000ffff8ul + 4ul;
    CHECK(v9x_si_dma_plan_check(&plan) == V9X_STATUS_UNSUPPORTED);
}

unsigned int v9x_run_si_dma_tests(void)
{
    failures = 0u;
    test_microcode();
    test_packets();
    test_fill();
    test_copy();
    test_overlap_refused();
    test_long_rows();
    return failures;
}
