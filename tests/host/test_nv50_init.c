#include <stdio.h>
#include <string.h>

#include "velocity9x/nv50_init.h"
#include "velocity9x/gt218_reserve.h"
#include "velocity9x/nv50_rblock.h"
#include "velocity9x/nv50_crtc.h"

/*
 * The GT218 bring-up list (src\chipsets\nvidia\gt218\nv50_init.c)
 * against the sequence the step 3 probe ran on M-BXMFA8, which is
 * nouveau's nv50_fifo_init, trap enable, nv50_gr_init and channel bind.
 */

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

#define BASE 0x08000000ul
#define PMC  0xdff3f113ul

static v9x_u32 region[V9X_NV50_REGION_WORDS];
static struct v9x_nv50_chan_layout layout;
static struct v9x_nv50_init_op ops[V9X_NV50_INIT_OPS_MAX];
static struct v9x_nv50_init_list list;

static v9x_status build_for(v9x_u32 chipset, v9x_u32 units, v9x_u32 max)
{
    struct v9x_nv50_grctx_input grctx;
    struct v9x_nv50_init_input input;

    grctx.chipset = 0xa8ul;
    grctx.units = 0xf3010001ul;
    grctx.gddr5 = 0ul;
    if (v9x_nv50_chan_build(BASE, 1ul, &grctx, region,
                            V9X_NV50_REGION_WORDS, &layout) !=
        V9X_STATUS_OK) {
        return V9X_STATUS_INVALID_STATE;
    }
    input.chipset = chipset;
    input.units = units;
    input.pmc_enable = PMC;
    return v9x_nv50_init_build(&input, &layout, ops, max, &list);
}

static int is_write(v9x_u32 at, v9x_u32 reg, v9x_u32 value)
{
    return ops[at].kind == V9X_NV50_INIT_WRITE && ops[at].reg == reg &&
           ops[at].value == value;
}

/* The first index at or after from that writes reg, or count. */
static v9x_u32 find_write(v9x_u32 from, v9x_u32 reg)
{
    v9x_u32 at;

    for (at = from; at < list.count; ++at) {
        if (ops[at].kind == V9X_NV50_INIT_WRITE && ops[at].reg == reg) {
            return at;
        }
    }
    return list.count;
}

static void test_fifo(void)
{
    v9x_u32 at;
    v9x_u32 index;

    CHECK(build_for(0xa8ul, 0xf3010001ul, V9X_NV50_INIT_OPS_MAX) ==
          V9X_STATUS_OK);
    /* PFIFO reset through PMC_ENABLE, every other bit as found. */
    CHECK(is_write(0ul, 0x000200ul, PMC & ~0x00000100ul));
    CHECK(is_write(1ul, 0x000200ul, PMC | 0x00000100ul));
    CHECK(is_write(2ul, 0x00250cul, 0x6f3cfc34ul));
    CHECK(is_write(3ul, 0x002044ul, 0x01003ffful));
    CHECK(is_write(4ul, 0x002100ul, 0xfffffffful));
    CHECK(is_write(5ul, 0x002140ul, 0xbffffffful));
    /* All 128 channel table entries cleared. */
    for (index = 0ul; index < 128ul; ++index) {
        CHECK(is_write(6ul + index, 0x002600ul + index * 4ul, 0ul));
    }
    /* An empty runlist and its wait, before CACHE1 is enabled. */
    at = 6ul + 128ul;
    CHECK(is_write(at, 0x0032f4ul, layout.runlist_reg));
    CHECK(is_write(at + 1ul, 0x0032ecul, 0ul));
    CHECK(ops[at + 2ul].kind == V9X_NV50_INIT_WAIT);
    CHECK(ops[at + 2ul].reg == 0x0032ecul);
    CHECK(ops[at + 2ul].mask == 0x00000100ul);
    CHECK(ops[at + 2ul].value == 0ul);
    CHECK(is_write(at + 3ul, 0x003200ul, 1ul));
    CHECK(is_write(at + 4ul, 0x003250ul, 1ul));
    CHECK(is_write(at + 5ul, 0x002500ul, 1ul));
    /* The VM trap: acknowledged, then enabled (gt215_fb's value). */
    CHECK(is_write(at + 6ul, 0x100c90ul, 0x800d0ffful));
    CHECK(is_write(at + 7ul, 0x100c90ul, 0x000d0ffful));
    CHECK(list.fifo_end == at + 8ul);
}

static void test_gr(void)
{
    v9x_u32 at = list.fifo_end;
    v9x_u32 index;
    v9x_u32 program;

    /* PGRAPH reset, from PMC_ENABLE with PFIFO on. */
    CHECK(is_write(at, 0x000200ul, (PMC | 0x100ul) & ~0x00201000ul));
    CHECK(is_write(at + 1ul, 0x000200ul, PMC | 0x100ul | 0x00201000ul));
    CHECK(is_write(at + 2ul, 0x40008cul, 4ul));
    CHECK(is_write(at + 3ul, 0x400804ul, 0xc0000000ul));
    CHECK(is_write(at + 8ul, 0x402000ul, 0xc0000000ul));
    /* One TP (1540h bit 0), the A0-and-later addresses. */
    CHECK(is_write(at + 9ul, 0x408600ul, 0xc0000000ul));
    CHECK(is_write(at + 10ul, 0x408708ul, 0xc0000000ul));
    CHECK(is_write(at + 11ul, 0x40831cul, 0xc0000000ul));
    CHECK(is_write(at + 12ul, 0x400108ul, 0xfffffffful));
    CHECK(is_write(at + 13ul, 0x400138ul, 0xfffffffful));
    CHECK(is_write(at + 14ul, 0x400100ul, 0xfffffffful));
    CHECK(is_write(at + 15ul, 0x40013cul, 0xfffffffful));
    CHECK(is_write(at + 16ul, 0x400500ul, 0x00010001ul));
    /* The context program: index 0, then every word in order. */
    CHECK(is_write(at + 17ul, 0x400324ul, 0ul));
    program = at + 18ul;
    CHECK(layout.program_words == 124ul);
    for (index = 0ul; index < layout.program_words; ++index) {
        CHECK(is_write(program + index, 0x400328ul,
                       layout.program[index]));
    }
    at = program + layout.program_words;
    CHECK(is_write(at, 0x400824ul, 0ul));
    CHECK(is_write(at + 3ul, 0x400830ul, 0ul));
    CHECK(is_write(at + 4ul, 0x40032cul, 0ul));
    CHECK(is_write(at + 5ul, 0x400330ul, 0ul));
    /* Zcull for A8: 402CC0h, 402CA8h = 2, then 8 x 4 zeros. */
    CHECK(is_write(at + 6ul, 0x402cc0ul, 0ul));
    CHECK(is_write(at + 7ul, 0x402ca8ul, 2ul));
    CHECK(is_write(at + 8ul, 0x402c20ul, 0ul));
    CHECK(is_write(at + 8ul + 31ul, 0x402c9cul, 0ul));
    CHECK(list.gr_end == at + 8ul + 32ul);
}

static void test_bind(void)
{
    v9x_u32 at = list.gr_end;

    CHECK(is_write(at, 0x002604ul, layout.chan_entry));
    CHECK(is_write(at + 1ul, 0x0032f4ul, layout.runlist_reg));
    CHECK(is_write(at + 2ul, 0x0032ecul, 1ul));
    CHECK(ops[at + 3ul].kind == V9X_NV50_INIT_WAIT);
    CHECK(ops[at + 3ul].reg == 0x0032ecul);
    CHECK(ops[at + 3ul].mask == 0x00000100ul);
    CHECK(list.count == at + 4ul);
    CHECK(list.count <= V9X_NV50_INIT_OPS_MAX);
}

static void test_variants(void)
{
    v9x_u32 at;

    /* Two TPs: a second set of trap enables, 800h on. */
    CHECK(build_for(0xa8ul, 0xf3010003ul, V9X_NV50_INIT_OPS_MAX) ==
          V9X_STATUS_OK);
    CHECK(find_write(0ul, 0x408e00ul) != list.count);
    CHECK(find_write(0ul, 0x408f08ul) != list.count);
    CHECK(find_write(0ul, 0x408b1cul) != list.count);

    /* Before A0h: the other trap addresses, and zcull 800h alone. */
    CHECK(build_for(0x86ul, 0xf3010001ul, V9X_NV50_INIT_OPS_MAX) ==
          V9X_STATUS_OK);
    CHECK(find_write(0ul, 0x408900ul) != list.count);
    CHECK(find_write(0ul, 0x408600ul) == list.count);
    at = find_write(0ul, 0x402ca8ul);
    CHECK(at != list.count && ops[at].value == 0x00000800ul);
    CHECK(find_write(0ul, 0x402cc0ul) == list.count);

    /* The G96 (96h, M-Z2T02D): g84_fb's VM trap value, 001D07FFh, not
     * gt215_fb's, acknowledged then enabled; the pre-A0h TP addresses. */
    CHECK(build_for(0x96ul, 0xf3010001ul, V9X_NV50_INIT_OPS_MAX) ==
          V9X_STATUS_OK);
    at = find_write(0ul, 0x100c90ul);
    CHECK(at != list.count && ops[at].value == 0x801d07fful);
    CHECK(at + 1ul < list.count && is_write(at + 1ul, 0x100c90ul,
                                            0x001d07fful));
    CHECK(find_write(0ul, 0x408900ul) != list.count);

    /* A0h itself: zcull 802h. */
    CHECK(build_for(0xa0ul, 0xf3010001ul, V9X_NV50_INIT_OPS_MAX) ==
          V9X_STATUS_OK);
    at = find_write(0ul, 0x402ca8ul);
    CHECK(at != list.count && ops[at].value == 0x00000802ul);

    /* Too small a buffer is refused, not truncated. */
    CHECK(build_for(0xa8ul, 0xf3010001ul, 100ul) ==
          V9X_STATUS_INTEGER_OVERFLOW);
}

static void test_ring(void)
{
    CHECK(v9x_nv50_ib_next(0ul) == 1ul);
    CHECK(v9x_nv50_ib_next(510ul) == 511ul);
    CHECK(v9x_nv50_ib_next(511ul) == 0ul);
}

/* The channel's region fits the reserve, and a megabyte boundary suits
 * the page table's 64 KiB pages; both at compile time. */
typedef char test_nv50_region_fits_reserve[
    (V9X_NV50_REGION_BYTES <= V9X_GT218_RESERVE_BYTES) ? 1 : -1];
typedef char test_nv50_reserve_page_aligned[
    ((V9X_GT218_RESERVE_BYTES & (V9X_NV50_PAGE_BYTES - 1ul)) == 0ul) ?
    1 : -1];

/* gt218_reserve.c: the engine's 2 MiB at the top of the mapping (2 MiB
 * since the 3D alias windows' page tables, Michael's choice 2026-11-14,
 * docs\plans\nvidia-gt218-3d-z.md). */
static void test_reserve(void)
{
    /* The 128 MiB mapping measured on M-BXMFA8: the last 2 MiB. */
    CHECK(v9x_gt218_reserve_start(0x08000000ul, 0x00180000ul) ==
          0x07e00000ul);
    /* An odd-sized mapping: the last whole 2 MiB that fits. */
    CHECK(v9x_gt218_reserve_start(0x040c0000ul, 0x00180000ul) ==
          0x03e00000ul);
    /* No room: smaller than the reserve, or it would cover the screen. */
    CHECK(v9x_gt218_reserve_start(0x000c0000ul, 0ul) == 0ul);
    CHECK(v9x_gt218_reserve_start(0x00200000ul, 0ul) == 0ul);
    CHECK(v9x_gt218_reserve_start(0x00400000ul, 0x00300000ul) == 0ul);
    CHECK(v9x_gt218_reserve_start(0x00400000ul, 0x00200000ul) ==
          0x00200000ul);
    /* Outside the 512 MiB the channel's identity VM maps. */
    CHECK(v9x_gt218_reserve_start(0x20000000ul, 0ul) == 0x1fe00000ul);
    CHECK(v9x_gt218_reserve_start(0x20200000ul, 0ul) == 0ul);

    /* The heap's end: 32 MiB below the reserve for the tiled texture pool
     * (docs\plans\nvidia-gt218-mipmaps.md) where it fits above the
     * screen; at the reserve where it does not; the whole mapping when
     * there is no reserve. */
    CHECK(v9x_gt218_heap_end(0x08000000ul, 0x00180000ul) == 0x05e00000ul);
    CHECK(v9x_gt218_heap_end(0x02800000ul, 0x00300000ul) == 0x00600000ul);
    CHECK(v9x_gt218_heap_end(0x02000000ul, 0x00300000ul) == 0x01e00000ul);
    CHECK(v9x_gt218_heap_end(0x000c0000ul, 0ul) == 0x000c0000ul);
}

/* nv50_rblock.c: the row block nouveau's nv50_fb_vram_rblock computes,
 * and Z and colour shadows in whole units of their own. */
static void test_rblock(void)
{
    v9x_u32 z = 0ul;
    v9x_u32 colour = 0ul;
    v9x_u32 span;

    /* W9QI0DAD's GT210 as nouveau read it (bringupkit trace, 2026-11-13):
     * 1540h F3010001 (one partition), 100204h 0166A020 (2^10 columns,
     * 8 banks), 100250h 1 (three): 192 KiB, the size V9XN5CH /z:map's
     * Z16 kept inside. */
    CHECK(v9x_nv50_rblock_bytes(0xf3010001ul, 0x0166a020ul, 1ul) ==
          0x30000ul);
    CHECK(v9x_nv50_rblock_bytes(0xf3010001ul, 0x0166a020ul, 0ul) ==
          0x10000ul);
    /* Two partitions, 16 banks. */
    CHECK(v9x_nv50_rblock_bytes(0x00030000ul, 0x0200a000ul, 0ul) ==
          0x40000ul);
    /* Not a configuration: no partition. */
    CHECK(v9x_nv50_rblock_bytes(0x00000001ul, 0x0166a020ul, 1ul) == 0ul);

    /* The unit is a whole number of row blocks and of 64 KiB pages. */
    CHECK(v9x_nv50_rblock_unit(0x30000ul) == 0x30000ul);
    CHECK(v9x_nv50_rblock_unit(0x10000ul) == 0x10000ul);
    CHECK(v9x_nv50_rblock_unit(0x4000ul) == 0x10000ul);
    CHECK(v9x_nv50_rblock_unit(0xc000ul) == 0x30000ul);
    CHECK(v9x_nv50_rblock_unit(0ul) == 0ul);
    CHECK(v9x_nv50_rblock_round(0xea600ul, 0x30000ul) == 0xf0000ul);
    CHECK(v9x_nv50_rblock_round(0x30000ul, 0x30000ul) == 0x30000ul);

    /* Quake 3 at 800x600 x 32 on W9QI0DAD (boot 39): the heap gave the
     * block at 3AB000h (on 4 KiB), the Z16 is EA600h, its colour shadow
     * 1D4C00h. The old 64 KiB placement put the Z at 3B0000h, 64 KiB
     * into the row block that starts at 390000h, which the back buffer
     * ends in. */
    span = v9x_nv50_rblock_span(0x30000ul, 0x1000ul, 0xea600ul, 0x1d4c00ul);
    CHECK(span == 0x2ff000ul);
    CHECK(v9x_nv50_rblock_place(0x3ab000ul, span, 0x30000ul, 0xea600ul,
                                0x1d4c00ul, &z, &colour) == 1);
    CHECK(z == 0x3c0000ul);
    CHECK(colour == 0x4b0000ul);
    /* The worst start, one granule past a boundary, still fits the span;
     * a byte less does not. */
    CHECK(v9x_nv50_rblock_place(0x31000ul, span, 0x30000ul, 0xea600ul,
                                0x1d4c00ul, &z, &colour) == 1);
    CHECK(z == 0x60000ul);
    CHECK(colour == 0x150000ul);
    CHECK(v9x_nv50_rblock_place(0x31000ul, span - 1ul, 0x30000ul,
                                0xea600ul, 0x1d4c00ul, &z, &colour) == 0);
    /* No unit, no placement. */
    CHECK(v9x_nv50_rblock_place(0x30000ul, span, 0ul, 0xea600ul, 0x1d4c00ul,
                                &z, &colour) == 0);
    CHECK(v9x_nv50_rblock_span(0ul, 0x1000ul, 1ul, 1ul) == 0ul);
}

/* nv50_crtc.c: the start address in the layout W9QI0DAD's VBIOS writes
 * and the /startmove and /starthigh probes confirmed. */
static void test_crtc_start(void)
{
    struct v9x_nv50_crtc_start start;

    /* The /startmove value: 2000h dwords is 32 KiB. */
    CHECK(v9x_nv50_build_crtc_start(0x8000ul, 0x20000000ul, &start) ==
          V9X_STATUS_OK);
    CHECK(start.cr0c == 0x20u && start.cr0d == 0x00u &&
          start.cr35 == 0x00u && start.cr34 == 0x00u);

    /* The /starthigh value: 10000h dwords, CR35 bit 0. */
    CHECK(v9x_nv50_build_crtc_start(0x40000ul, 0x20000000ul, &start) ==
          V9X_STATUS_OK);
    CHECK(start.cr0c == 0u && start.cr0d == 0u && start.cr35 == 0x01u &&
          start.cr34 == 0u);

    /* A 1024x768x16 back buffer behind the primary: 180000h bytes. */
    CHECK(v9x_nv50_build_crtc_start(0x180000ul, 0x20000000ul, &start) ==
          V9X_STATUS_OK);
    CHECK(start.cr0c == 0x00u && start.cr0d == 0x00u &&
          start.cr35 == 0x06u && start.cr34 == 0u);

    /* Every field at once: 1FFFFFFCh bytes is 07FFFFFFh dwords. */
    CHECK(v9x_nv50_build_crtc_start(0x1ffffffcul, 0x20000000ul, &start) ==
          V9X_STATUS_OK);
    CHECK(start.cr0c == 0xffu && start.cr0d == 0xffu &&
          start.cr35 == 0xffu && start.cr34 == 0x07u);

    /* Refused: not a dword, past the memory, no output. */
    CHECK(v9x_nv50_build_crtc_start(0x8002ul, 0x20000000ul, &start) !=
          V9X_STATUS_OK);
    CHECK(v9x_nv50_build_crtc_start(0x20000000ul, 0x20000000ul, &start) !=
          V9X_STATUS_OK);
    CHECK(v9x_nv50_build_crtc_start(0ul, 0x20000000ul, 0) != V9X_STATUS_OK);
}

unsigned int v9x_run_nv50_init_tests(void)
{
    failures = 0u;
    test_crtc_start();
    test_reserve();
    test_rblock();
    test_fifo();
    test_gr();
    test_bind();
    test_variants();
    test_ring();
    return failures;
}
