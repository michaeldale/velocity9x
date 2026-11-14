#include <stdio.h>
#include <string.h>

#include "velocity9x/nv50_chan.h"

/*
 * The GT218 channel builder (src\chipsets\nvidia\gt218\nv50_chan.c), word
 * by word against nouveau's encodings as include\velocity9x\nv50_chan.h
 * cites them, at the base the step 3 probe uses (192 MiB, above the
 * driver's 128 MiB BAR1 mapping).
 */

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

#define BASE 0x0c000000ul
#define CHID 1ul

static v9x_u32 region[V9X_NV50_REGION_WORDS];
static struct v9x_nv50_chan_layout layout;

static v9x_u32 word_at(v9x_u32 byte_offset)
{
    return region[byte_offset / 4ul];
}

static v9x_status build(void)
{
    struct v9x_nv50_grctx_input input;

    input.chipset = 0xa8ul;
    input.units = 0xf3010001ul;
    input.gddr5 = 0ul;
    return v9x_nv50_chan_build(BASE, CHID, &input, region,
                               V9X_NV50_REGION_WORDS, &layout);
}

static v9x_u32 ramht_find(v9x_u32 handle)
{
    v9x_u32 slot;
    v9x_u32 at;

    for (slot = 0ul; slot < V9X_NV50_RAMHT_BYTES / 8ul; ++slot) {
        at = V9X_NV50_INST_RAMHT + slot * 8ul;
        if (word_at(at) == handle) {
            return word_at(at + 4ul);
        }
    }
    return 0xfffffffful;
}

static void test_layout(void)
{
    CHECK(build() == V9X_STATUS_OK);
    CHECK(layout.inst_addr == BASE);
    CHECK((layout.inst_addr & 0xffful) == 0ul);
    CHECK(layout.ramfc_addr == BASE + 0x4200ul);
    CHECK((layout.ramfc_addr & 0xfful) == 0ul);
    /* RAMFC >> 8 with bit 31 (g84.c bind, nv50.c start). */
    CHECK(layout.chan_entry == ((BASE + 0x4200ul) >> 8 | 0x80000000ul));
    /* The live-channel values the two sides test agree with the build,
     * and with what M-BXMFA8 read at base 07F00000h (boot 108). */
    CHECK(v9x_nv50_live_chan_entry(BASE) == layout.chan_entry);
    CHECK(v9x_nv50_live_context(BASE) ==
          (0x80000000ul | (layout.inst_addr >> 12)));
    CHECK(v9x_nv50_live_push(BASE) == layout.push_addr);
    CHECK(v9x_nv50_live_ib(BASE) == layout.ib_addr);
    CHECK(v9x_nv50_live_chan_entry(0x07f00000ul) == 0x8007f042ul);
    CHECK(v9x_nv50_live_context(0x07f00000ul) == 0x80007f00ul);
    CHECK(layout.runlist_reg == (BASE + V9X_NV50_RUNLIST) >> 12);
    CHECK(layout.runlist_count == 1ul);
    CHECK(word_at(V9X_NV50_RUNLIST) == CHID);
    CHECK(layout.program_words == 124ul);
    CHECK(layout.grctx_bytes == 77440ul * 4ul);
    CHECK(layout.grctx_bytes <= V9X_NV50_GRCTX_MAX_BYTES);
}

/* Every structure inside the instance block stays clear of its
 * neighbours, checked at compile time. */
/* The page directory is where G84 and later read it, instance + 200h
 * (measured: nouveau's channel on a GT218, mmiotrace 2026-11-13). With
 * it at NV50's 1400h and a DMA object at 200h, the chip read that object
 * as PDE 0 (runs 14-26 on M-BXMFA8). */
typedef char test_nv50_pd_at_g84_offset[
    (V9X_NV50_INST_PD == 0x200ul) ? 1 : -1];
typedef char test_nv50_eng_below_pd[
    (V9X_NV50_INST_ENG + 0x200ul <= V9X_NV50_INST_PD) ? 1 : -1];
typedef char test_nv50_pd_below_ramfc[
    (V9X_NV50_INST_PD + V9X_NV50_PD_BYTES <= V9X_NV50_INST_RAMFC) ? 1 : -1];
typedef char test_nv50_ramfc_below_cache[
    (V9X_NV50_INST_RAMFC + 0x100ul <= V9X_NV50_INST_CACHE) ? 1 : -1];
typedef char test_nv50_cache_below_ramht[
    (V9X_NV50_INST_CACHE + 0x1000ul <= V9X_NV50_INST_RAMHT) ? 1 : -1];
typedef char test_nv50_ramht_below_objects[
    (V9X_NV50_INST_RAMHT + V9X_NV50_RAMHT_BYTES <= V9X_NV50_INST_DMA_VM) ?
    1 : -1];
typedef char test_nv50_ramht_inside_inst[
    (V9X_NV50_INST_RAMHT + V9X_NV50_RAMHT_BYTES <= V9X_NV50_INST_BYTES) ?
    1 : -1];
/* cl006c.h header layout: count 28:18, subchannel 15:13, method 12:2. */
typedef char test_nv50_header_bind[
    (V9X_NV50_PUSH_HEADER(3u, 0x0000ul, 1u) == 0x00046000ul) ? 1 : -1];
typedef char test_nv50_header_ref[
    (V9X_NV50_PUSH_HEADER(0u, 0x0050ul, 1u) == 0x00040050ul) ? 1 : -1];
typedef char test_nv50_header_run[
    (V9X_NV50_PUSH_HEADER(3u, 0x0200ul, 10u) == 0x00286200ul) ? 1 : -1];

static void test_vm(void)
{
    v9x_u32 page;
    v9x_u32 bad = 0ul;

    CHECK(build() == V9X_STATUS_OK);
    /* PDE 0: the page table's address | 64 KiB pages in video memory. */
    CHECK(word_at(V9X_NV50_INST_PD) == ((BASE + V9X_NV50_PT) | 1ul));
    CHECK(word_at(V9X_NV50_INST_PD + 4ul) == 0ul);
    CHECK(word_at(V9X_NV50_INST_PD + 8ul) == 0ul);
    /* The identity, valid, log2blk 7, high word (kind) 0. Every entry of
     * a 128-page block carries the block's first address, not its own
     * (vmmnv50.c nv50_vmm_pgt_pte writes one value to the whole block).
     * With its own address in each, the chip added the page's index in
     * the block again and read the IB from the wrong place: run 28 on
     * M-BXMFA8, DMA pusher error IB_EMPTY. */
    for (page = 0ul; page < V9X_NV50_VM_BYTES / V9X_NV50_PAGE_BYTES;
         ++page) {
        if (word_at(V9X_NV50_PT + page * 8ul) !=
            (((page & ~0x7ful) << 16) | 0x381ul) ||
            word_at(V9X_NV50_PT + page * 8ul + 4ul) != 0ul) {
            ++bad;
        }
    }
    CHECK(bad == 0ul);
    CHECK(word_at(V9X_NV50_PT + 0x0c00ul * 8ul) == 0x0c000381ul);
    /* Page 802h, where the probe's IB ring lives: block base 800h. */
    CHECK(word_at(V9X_NV50_PT + 0x0802ul * 8ul) == 0x08000381ul);
}

static void test_objects(void)
{
    CHECK(build() == V9X_STATUS_OK);
    CHECK(word_at(V9X_NV50_INST_DMA_VM + 0x00ul) == 0x7fc0003dul);
    CHECK(word_at(V9X_NV50_INST_DMA_VM + 0x04ul) == 0x1ffffffful);
    CHECK(word_at(V9X_NV50_INST_DMA_VM + 0x08ul) == 0ul);
    CHECK(word_at(V9X_NV50_INST_DMA_VM + 0x0cul) == 0ul);
    CHECK(word_at(V9X_NV50_INST_DMA_VM + 0x14ul) == 0ul);
    CHECK(word_at(V9X_NV50_INST_OBJ_2D + 0x00ul) == 0x502dul);
    CHECK(word_at(V9X_NV50_INST_OBJ_2D + 0x04ul) == 0ul);

    /* RAMHT: engine << 20 | offset >> 4. */
    CHECK(ramht_find(V9X_NV50_HANDLE_DMA_VM) == (0ul << 20 | 0xe000ul >> 4));
    CHECK(ramht_find(V9X_NV50_HANDLE_2D) == (1ul << 20 | 0xe020ul >> 4));
    /* The DMA object lands at its own hash slot. */
    CHECK(word_at(V9X_NV50_INST_RAMHT +
                  v9x_nv50_ramht_hash(V9X_NV50_HANDLE_DMA_VM) * 8ul) ==
          V9X_NV50_HANDLE_DMA_VM);
}

static void test_hash(void)
{
    /* ramht.c: XOR of 12-bit chunks. */
    CHECK(v9x_nv50_ramht_hash(0x00000123ul) == 0x123ul);
    CHECK(v9x_nv50_ramht_hash(0x00456123ul) == (0x123ul ^ 0x456ul));
    CHECK(v9x_nv50_ramht_hash(0xbeef0201ul) ==
          (0x201ul ^ 0xef0ul ^ 0xbeul));
}

static void test_engine_and_ramfc(void)
{
    v9x_u32 eng = V9X_NV50_INST_ENG + V9X_NV50_ENG_GR;
    v9x_u32 ramfc = V9X_NV50_INST_RAMFC;
    v9x_u32 grctx = BASE + V9X_NV50_GRCTX;

    CHECK(build() == V9X_STATUS_OK);
    CHECK(word_at(eng + 0x00ul) == 0x00190000ul);
    CHECK(word_at(eng + 0x04ul) == grctx + 77440ul * 4ul - 1ul);
    CHECK(word_at(eng + 0x08ul) == grctx);
    CHECK(word_at(eng + 0x0cul) == grctx);
    CHECK(word_at(eng + 0x10ul) == 0ul);
    CHECK(word_at(eng + 0x14ul) == 0ul);

    CHECK(word_at(ramfc + 0x3cul) == 0x403f6078ul);
    CHECK(word_at(ramfc + 0x44ul) == 0x01003ffful);
    CHECK(word_at(ramfc + 0x48ul) == 0xe000ul >> 4);
    CHECK(word_at(ramfc + 0x50ul) == BASE + V9X_NV50_IB);
    CHECK(word_at(ramfc + 0x54ul) == (9ul << 16));
    CHECK(word_at(ramfc + 0x60ul) == 0x7ffffffful);
    CHECK(word_at(ramfc + 0x7cul) == 0x30000ffful);
    CHECK(word_at(ramfc + 0x80ul) ==
          ((3ul << 27) | (4ul << 24) | (0x5400ul >> 4)));
    CHECK(word_at(ramfc + 0x88ul) == (BASE + 0x4400ul) >> 10);
    CHECK(word_at(ramfc + 0x98ul) == BASE >> 12);
}

/* The context image in the region is the generator's, at its offset. */
static v9x_u32 image[V9X_NV50_GRCTX_MAX_BYTES / 4ul];
static v9x_u32 program[V9X_NV50_GRCTX_PROGRAM_MAX];

static void test_grctx_placed(void)
{
    struct v9x_nv50_grctx_input input;
    v9x_u32 words = 0ul;
    v9x_u32 image_words = 0ul;

    CHECK(build() == V9X_STATUS_OK);
    input.chipset = 0xa8ul;
    input.units = 0xf3010001ul;
    input.gddr5 = 0ul;
    CHECK(v9x_nv50_grctx_build(&input, program, &words, image,
                               V9X_NV50_GRCTX_MAX_BYTES / 4ul,
                               &image_words) == V9X_STATUS_OK);
    CHECK(memcmp(region + V9X_NV50_GRCTX / 4ul, image,
                 image_words * 4ul) == 0);
    CHECK(memcmp(layout.program, program, words * 4ul) == 0);
}

static void test_refusals(void)
{
    struct v9x_nv50_grctx_input input;

    input.chipset = 0xa8ul;
    input.units = 0xf3010001ul;
    input.gddr5 = 0ul;
    CHECK(v9x_nv50_chan_build(BASE + 0x1000ul, CHID, &input, region,
                              V9X_NV50_REGION_WORDS, &layout) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_nv50_chan_build(BASE, 0ul, &input, region,
                              V9X_NV50_REGION_WORDS, &layout) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_nv50_chan_build(BASE, 127ul, &input, region,
                              V9X_NV50_REGION_WORDS, &layout) ==
          V9X_STATUS_INVALID_ARGUMENT);
    /* B0000h bytes from 1FF60000h would end past the 512 MiB mapped. */
    CHECK(v9x_nv50_chan_build(0x1ff60000ul, CHID, &input, region,
                              V9X_NV50_REGION_WORDS, &layout) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_nv50_chan_build(BASE, CHID, &input, region, 16ul, &layout) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

static void test_push(void)
{
    v9x_u32 words[64];
    v9x_u32 entry[2];
    struct v9x_nv50_push push;

    v9x_nv50_push_init(&push, words, 64ul);
    v9x_nv50_push_bind_2d(&push);
    CHECK(push.count == 4ul);
    CHECK(words[0] == 0x00046000ul);
    CHECK(words[1] == V9X_NV50_HANDLE_2D);
    CHECK(words[2] == 0x00046184ul);
    CHECK(words[3] == V9X_NV50_HANDLE_DMA_VM);

    v9x_nv50_push_fill(&push, 0x0c200000ul, 256ul, 64ul, 64ul,
                       V9X_502D_FORMAT_X8R8G8B8, 0x00a5c3e1ul,
                       0ul, 0ul, 64ul, 64ul);
    CHECK(push.overflow == 0ul);
    /* Surface run: header, 10 words; then five single methods; then the
     * point run: header, 4 words. */
    CHECK(push.count == 4ul + 11ul + 10ul + 5ul);
    CHECK(words[4] == 0x00286200ul);
    CHECK(words[5] == 0xe6ul);
    CHECK(words[6] == 1ul);
    CHECK(words[8] == 1ul);
    CHECK(words[9] == 0ul);
    CHECK(words[10] == 256ul);
    CHECK(words[14] == 0x0c200000ul);
    CHECK(words[15] == 0x00046290ul);
    CHECK(words[25] == 0x00106600ul);
    CHECK(words[26] == 0ul);
    CHECK(words[28] == 64ul);
    CHECK(words[29] == 64ul);

    /* A 1:1 copy inside one surface: source DMA bind; the destination
     * run (11 words); source format and layout (3); source pitch to
     * address (6); clip, operation, safe overlap, blit control (8); the
     * twelve blit words from 8B0h (13). */
    v9x_nv50_push_init(&push, words, 64ul);
    v9x_nv50_push_copy(&push, 0x08200000ul, 512ul, 128ul, 72ul,
                       V9X_502D_FORMAT_X8R8G8B8,
                       32ul, 4ul, 8ul, 6ul, 64ul, 64ul);
    CHECK(push.overflow == 0ul);
    CHECK(push.count == 2ul + 11ul + 3ul + 6ul + 8ul + 13ul);
    CHECK(words[0] == 0x00046188ul);
    CHECK(words[1] == V9X_NV50_HANDLE_DMA_VM);
    CHECK(words[2] == 0x00286200ul);
    CHECK(words[12] == 0x08200000ul);
    CHECK(words[13] == 0x00086230ul);
    CHECK(words[14] == 0xe6ul);
    CHECK(words[15] == 1ul);
    CHECK(words[16] == 0x00146244ul);
    CHECK(words[17] == 512ul);
    CHECK(words[18] == 128ul);
    CHECK(words[19] == 72ul);
    CHECK(words[20] == 0ul);
    CHECK(words[21] == 0x08200000ul);
    CHECK(words[22] == 0x00046290ul);
    CHECK(words[24] == 0x000462acul);
    CHECK(words[25] == 3ul);
    CHECK(words[26] == 0x00046888ul);
    CHECK(words[27] == 1ul);
    CHECK(words[28] == 0x0004688cul);
    CHECK(words[29] == 0ul);
    /* 8B0h: dst x, y, w, h; du/dx and dv/dy = 1.0; src x, y. */
    CHECK(words[30] == 0x003068b0ul);
    CHECK(words[31] == 8ul);
    CHECK(words[32] == 6ul);
    CHECK(words[33] == 64ul);
    CHECK(words[34] == 64ul);
    CHECK(words[35] == 0ul);
    CHECK(words[36] == 1ul);
    CHECK(words[37] == 0ul);
    CHECK(words[38] == 1ul);
    CHECK(words[39] == 0ul);
    CHECK(words[40] == 32ul);
    CHECK(words[41] == 0ul);
    CHECK(words[42] == 4ul);

    v9x_nv50_push_init(&push, words, 2ul);
    v9x_nv50_push_bind_2d(&push);
    CHECK(push.overflow == 1ul);

    /* chan506f.c: low address; high | dwords << 10. */
    v9x_nv50_ib_entry(0x0c022000ul, 30ul, entry);
    CHECK(entry[0] == 0x0c022000ul);
    CHECK(entry[1] == (30ul << 10));

    /* A copy between two surfaces: the same words as the one-surface
     * copy, the destination run naming one and the source run the other. */
    v9x_nv50_push_init(&push, words, 64ul);
    v9x_nv50_push_blit(&push, 0x00400000ul, 1280ul, 320ul, 200ul,
                       0x00100000ul, 4096ul, 1024ul, 768ul,
                       V9X_502D_FORMAT_X8R8G8B8,
                       0ul, 0ul, 100ul, 50ul, 320ul, 200ul);
    CHECK(push.overflow == 0ul);
    CHECK(push.count == 43ul);
    CHECK(words[8] == 4096ul);
    CHECK(words[9] == 1024ul);
    CHECK(words[10] == 768ul);
    CHECK(words[12] == 0x00100000ul);
    CHECK(words[17] == 1280ul);
    CHECK(words[18] == 320ul);
    CHECK(words[19] == 200ul);
    CHECK(words[21] == 0x00400000ul);
    CHECK(words[31] == 100ul);
    CHECK(words[32] == 50ul);
    CHECK(words[33] == 320ul);
    CHECK(words[34] == 200ul);
    CHECK(words[40] == 0ul);
    CHECK(words[42] == 0ul);

    /* The three depths measured, and nothing else. */
    CHECK(v9x_nv50_2d_format(1ul) == 0xf3ul);
    CHECK(v9x_nv50_2d_format(2ul) == 0xe8ul);
    CHECK(v9x_nv50_2d_format(4ul) == 0xe6ul);
    CHECK(v9x_nv50_2d_format(3ul) == 0ul);
    CHECK(v9x_nv50_2d_format(0ul) == 0ul);
}

unsigned int v9x_run_nv50_chan_tests(void)
{
    failures = 0u;
    test_layout();
    test_vm();
    test_objects();
    test_hash();
    test_engine_and_ramfc();
    test_grctx_placed();
    test_refusals();
    test_push();
    return failures;
}
