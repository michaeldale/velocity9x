#include <stdio.h>
#include <stdlib.h>

#include "velocity9x/i9xx_pool.h"

static unsigned int failures = 0u;

#define CHECK(expression) do { \
    if (!(expression)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #expression); \
        ++failures; \
    } \
} while (0)

#define NETBOOK_BSM      0x7f800000ul
#define NETBOOK_STOLEN   0x00800000ul
#define MIB              0x00100000ul

/* MICHAEL-NETBOOK as measured: 8 MiB stolen at 7F800000, 256 MiB GMADR,
 * 65,536 GTT entries, 2 GiB RAM; the mapping is the 48 MiB decision 1
 * asks for. Its GTT is BAR3, not in RAM, so stolen is the one exclusion
 * that matters; the ring and status page are inside stolen. */
static void netbook_layout(struct v9x_i9xx_pool_layout *layout)
{
    layout->bsm = NETBOOK_BSM;
    layout->stolen_bytes = NETBOOK_STOLEN;
    layout->aperture_bytes = 256ul * MIB;
    layout->gtt_entries = 65536ul;
    layout->mapped_bytes = 48ul * MIB;
    layout->ram_bytes = 2048ul * MIB;
    layout->excluded_count = 0u;
}

static void test_size_netbook(void)
{
    struct v9x_i9xx_pool_layout layout;
    struct v9x_i9xx_pool_plan plan;

    netbook_layout(&layout);
    CHECK(v9x_i9xx_pool_size(&layout, &plan) == V9X_I9XX_POOL_OK);
    CHECK(plan.reason == V9X_I9XX_POOL_OK);
    CHECK(plan.aperture_offset == 8ul * MIB);
    CHECK(plan.bytes == 32ul * MIB);
    CHECK(plan.first_entry == 2048ul);
    CHECK(plan.entry_count == 8192ul);
}

static void test_size_limits(void)
{
    struct v9x_i9xx_pool_layout layout;
    struct v9x_i9xx_pool_plan plan;

    /* Today's 16 MiB map leaves 8 MiB above stolen. */
    netbook_layout(&layout);
    layout.mapped_bytes = 16ul * MIB;
    CHECK(v9x_i9xx_pool_size(&layout, &plan) == V9X_I9XX_POOL_OK);
    CHECK(plan.bytes == 8ul * MIB && plan.entry_count == 2048ul);

    /* Exactly the minimum. */
    layout.mapped_bytes = 12ul * MIB;
    CHECK(v9x_i9xx_pool_size(&layout, &plan) == V9X_I9XX_POOL_OK);
    CHECK(plan.bytes == 4ul * MIB);

    /* Room that is not a whole number of four-page grains rounds down:
     * the mini-VDD hands pages over four at a time. */
    layout.mapped_bytes = 13ul * MIB + 12ul * 1024ul;
    CHECK(v9x_i9xx_pool_size(&layout, &plan) == V9X_I9XX_POOL_OK);
    CHECK(plan.bytes == 5ul * MIB && (plan.entry_count & 3ul) == 0ul);
    layout.mapped_bytes = 13ul * MIB + 16ul * 1024ul;
    CHECK(v9x_i9xx_pool_size(&layout, &plan) == V9X_I9XX_POOL_OK);
    CHECK(plan.bytes == 5ul * MIB + 16ul * 1024ul);

    /* Under it: no pool, and nothing to allocate. */
    layout.mapped_bytes = 12ul * MIB - 4096ul;
    CHECK(v9x_i9xx_pool_size(&layout, &plan) == V9X_I9XX_POOL_NO_ROOM);
    CHECK(plan.bytes == 0ul && plan.entry_count == 0ul);

    /* A mapping bigger than the pool still gets 32 MiB. */
    layout.mapped_bytes = 256ul * MIB;
    CHECK(v9x_i9xx_pool_size(&layout, &plan) == V9X_I9XX_POOL_OK);
    CHECK(plan.bytes == 32ul * MIB);

    /* A mapping of the whole aperture: the aperture is the bound. */
    layout.aperture_bytes = 16ul * MIB;
    layout.mapped_bytes = 16ul * MIB;
    CHECK(v9x_i9xx_pool_size(&layout, &plan) == V9X_I9XX_POOL_OK);
    CHECK(plan.bytes == 8ul * MIB);

    /* So does the GTT: 4096 entries are 16 MiB of addresses. */
    netbook_layout(&layout);
    layout.gtt_entries = 4096ul;
    CHECK(v9x_i9xx_pool_size(&layout, &plan) == V9X_I9XX_POOL_OK);
    CHECK(plan.bytes == 8ul * MIB);

    /* Under 512 MiB of RAM: none, whatever the room. */
    netbook_layout(&layout);
    layout.ram_bytes = 512ul * MIB - 4096ul;
    CHECK(v9x_i9xx_pool_size(&layout, &plan) == V9X_I9XX_POOL_NO_RAM);
    CHECK(plan.bytes == 0ul);
    layout.ram_bytes = 512ul * MIB;
    CHECK(v9x_i9xx_pool_size(&layout, &plan) == V9X_I9XX_POOL_OK);

    /* An odd stolen size rounds the offset up to the next MiB. */
    netbook_layout(&layout);
    layout.stolen_bytes = 1ul * MIB;
    layout.mapped_bytes = 16ul * MIB;
    CHECK(v9x_i9xx_pool_size(&layout, &plan) == V9X_I9XX_POOL_OK);
    CHECK(plan.aperture_offset == 1ul * MIB);
    CHECK(plan.bytes == 15ul * MIB);
}

static void test_size_bad_layout(void)
{
    struct v9x_i9xx_pool_layout layout;
    struct v9x_i9xx_pool_plan plan;

    CHECK(v9x_i9xx_pool_size(0, &plan) == V9X_I9XX_POOL_BAD_LAYOUT);
    netbook_layout(&layout);
    CHECK(v9x_i9xx_pool_size(&layout, 0) == V9X_I9XX_POOL_BAD_LAYOUT);

    layout.stolen_bytes = 0ul;
    CHECK(v9x_i9xx_pool_size(&layout, &plan) == V9X_I9XX_POOL_BAD_LAYOUT);
    CHECK(plan.bytes == 0ul);

    netbook_layout(&layout);
    layout.stolen_bytes = NETBOOK_STOLEN + 2ul;
    CHECK(v9x_i9xx_pool_size(&layout, &plan) == V9X_I9XX_POOL_BAD_LAYOUT);

    netbook_layout(&layout);
    layout.bsm = NETBOOK_BSM + 8ul;
    CHECK(v9x_i9xx_pool_size(&layout, &plan) == V9X_I9XX_POOL_BAD_LAYOUT);

    /* Stolen running past 4 GiB cannot be a real layout. */
    netbook_layout(&layout);
    layout.bsm = 0xfff00000ul;
    CHECK(v9x_i9xx_pool_size(&layout, &plan) == V9X_I9XX_POOL_BAD_LAYOUT);

    netbook_layout(&layout);
    layout.excluded_count = V9X_I9XX_POOL_MAX_EXCLUDED + 1u;
    CHECK(v9x_i9xx_pool_size(&layout, &plan) == V9X_I9XX_POOL_BAD_LAYOUT);

    netbook_layout(&layout);
    layout.mapped_bytes = 300ul * MIB;   /* more than the aperture */
    CHECK(v9x_i9xx_pool_size(&layout, &plan) == V9X_I9XX_POOL_BAD_LAYOUT);
}

static void test_sort(void)
{
    v9x_u32 pages[7] = {
        0x00500000ul, 0x00100000ul, 0x7f000000ul, 0x00300000ul,
        0x00100000ul, 0x00002000ul, 0x00400000ul
    };
    v9x_u32 one[1] = { 0x00123000ul };
    v9x_u32 index;

    v9x_i9xx_pool_sort(pages, 7ul);
    for (index = 1ul; index < 7ul; ++index) {
        CHECK(pages[index - 1ul] <= pages[index]);
    }
    CHECK(pages[0] == 0x00002000ul && pages[6] == 0x7f000000ul);
    v9x_i9xx_pool_sort(one, 1ul);
    CHECK(one[0] == 0x00123000ul);
    v9x_i9xx_pool_sort(pages, 0ul);
    v9x_i9xx_pool_sort(0, 0ul);
}

/* A small pool so the lists are short: 4 MiB = 1024 pages. */
static void small_layout(struct v9x_i9xx_pool_layout *layout,
                         struct v9x_i9xx_pool_plan *plan)
{
    netbook_layout(layout);
    layout->mapped_bytes = 12ul * MIB;
    (void)v9x_i9xx_pool_size(layout, plan);
}

static void fill_pages(v9x_u32 *pages, v9x_u32 count, v9x_u32 base)
{
    v9x_u32 index;
    for (index = 0ul; index < count; ++index) {
        pages[index] = base + index * V9X_I9XX_POOL_PAGE_BYTES;
    }
}

static void test_check(void)
{
    struct v9x_i9xx_pool_layout layout;
    struct v9x_i9xx_pool_plan plan;
    static v9x_u32 pages[1024];

    small_layout(&layout, &plan);
    CHECK(plan.entry_count == 1024ul);

    fill_pages(pages, 1024ul, 0x01000000ul);
    CHECK(v9x_i9xx_pool_check(&layout, &plan, pages, 1024ul) ==
          V9X_I9XX_POOL_OK);

    /* Wrong count, either way. */
    CHECK(v9x_i9xx_pool_check(&layout, &plan, pages, 1023ul) ==
          V9X_I9XX_POOL_COUNT);
    CHECK(v9x_i9xx_pool_check(&layout, &plan, 0, 1024ul) ==
          V9X_I9XX_POOL_COUNT);

    /* Not page aligned. */
    pages[5] += 0x10ul;
    CHECK(v9x_i9xx_pool_check(&layout, &plan, pages, 1024ul) ==
          V9X_I9XX_POOL_ALIGNMENT);
    fill_pages(pages, 1024ul, 0x01000000ul);

    /* Physical page zero: what a failed lookup returns. */
    fill_pages(pages, 1024ul, 0ul);
    CHECK(v9x_i9xx_pool_check(&layout, &plan, pages, 1024ul) ==
          V9X_I9XX_POOL_ZERO_PAGE);

    /* A page listed twice, and a list not sorted. */
    fill_pages(pages, 1024ul, 0x01000000ul);
    pages[10] = pages[9];
    CHECK(v9x_i9xx_pool_check(&layout, &plan, pages, 1024ul) ==
          V9X_I9XX_POOL_ORDER);
    fill_pages(pages, 1024ul, 0x01000000ul);
    pages[0] = 0x02000000ul;
    CHECK(v9x_i9xx_pool_check(&layout, &plan, pages, 1024ul) ==
          V9X_I9XX_POOL_ORDER);

    /* A run that ends on stolen's first page, and one inside it. */
    fill_pages(pages, 1024ul, NETBOOK_BSM - 1023ul * 4096ul);
    CHECK(v9x_i9xx_pool_check(&layout, &plan, pages, 1024ul) ==
          V9X_I9XX_POOL_STOLEN);
    fill_pages(pages, 1024ul, NETBOOK_BSM - 1024ul * 4096ul);
    CHECK(v9x_i9xx_pool_check(&layout, &plan, pages, 1024ul) ==
          V9X_I9XX_POOL_OK);
    fill_pages(pages, 1024ul, NETBOOK_BSM + NETBOOK_STOLEN - 4096ul);
    CHECK(v9x_i9xx_pool_check(&layout, &plan, pages, 1024ul) ==
          V9X_I9XX_POOL_STOLEN);
    fill_pages(pages, 1024ul, NETBOOK_BSM + NETBOOK_STOLEN);
    CHECK(v9x_i9xx_pool_check(&layout, &plan, pages, 1024ul) ==
          V9X_I9XX_POOL_OK);

    /* An excluded range: a GTT in RAM, as the GM965's is in stolen. */
    layout.excluded_count = 1u;
    layout.excluded[0].base = 0x01100000ul;
    layout.excluded[0].bytes = 0x00080000ul;
    fill_pages(pages, 1024ul, 0x01000000ul);
    CHECK(v9x_i9xx_pool_check(&layout, &plan, pages, 1024ul) ==
          V9X_I9XX_POOL_EXCLUDED);
    layout.excluded[0].base = 0x01400000ul;   /* just past the run */
    CHECK(v9x_i9xx_pool_check(&layout, &plan, pages, 1024ul) ==
          V9X_I9XX_POOL_OK);
    layout.excluded_count = 0u;

    /* The top page of the 32-bit space is a page like any other, but the
     * run reaching it must not wrap. */
    fill_pages(pages, 1024ul, 0xfffff000ul - 1023ul * 4096ul);
    CHECK(v9x_i9xx_pool_check(&layout, &plan, pages, 1024ul) ==
          V9X_I9XX_POOL_OK);
}

static void test_check_plan_bounds(void)
{
    struct v9x_i9xx_pool_layout layout;
    struct v9x_i9xx_pool_plan plan;
    static v9x_u32 pages[1024];

    small_layout(&layout, &plan);
    fill_pages(pages, 1024ul, 0x01000000ul);

    /* A plan that overlaps the stolen run's entries. */
    plan.first_entry = 2047ul;
    plan.aperture_offset = plan.first_entry * 4096ul;
    CHECK(v9x_i9xx_pool_check(&layout, &plan, pages, 1024ul) ==
          V9X_I9XX_POOL_APERTURE);

    /* One whose end passes the CPU's mapping. */
    small_layout(&layout, &plan);
    layout.mapped_bytes = 12ul * MIB - 4096ul;
    CHECK(v9x_i9xx_pool_check(&layout, &plan, pages, 1024ul) ==
          V9X_I9XX_POOL_APERTURE);

    /* One whose offset and entry disagree. */
    small_layout(&layout, &plan);
    plan.aperture_offset += 4096ul;
    CHECK(v9x_i9xx_pool_check(&layout, &plan, pages, 1024ul) ==
          V9X_I9XX_POOL_APERTURE);

    /* One past the GTT. */
    small_layout(&layout, &plan);
    layout.gtt_entries = 2048ul + 1023ul;
    CHECK(v9x_i9xx_pool_check(&layout, &plan, pages, 1024ul) ==
          V9X_I9XX_POOL_APERTURE);

    /* A refused plan accepts no list. */
    small_layout(&layout, &plan);
    plan.reason = V9X_I9XX_POOL_NO_ROOM;
    CHECK(v9x_i9xx_pool_check(&layout, &plan, pages, 1024ul) ==
          V9X_I9XX_POOL_NO_ROOM);
}

/*
 * The streaming form the DRV uses: the plan checked once, then each page
 * as the mini-VDD hands it over, with no sort. It must refuse exactly what
 * the whole-list check refuses for a single page.
 */
static void test_streaming(void)
{
    struct v9x_i9xx_pool_layout layout;
    struct v9x_i9xx_pool_plan plan;

    small_layout(&layout, &plan);
    CHECK(v9x_i9xx_pool_check_plan(&layout, &plan) == V9X_I9XX_POOL_OK);
    CHECK(v9x_i9xx_pool_check_page(&layout, 0x01000000ul) ==
          V9X_I9XX_POOL_OK);
    CHECK(v9x_i9xx_pool_check_page(&layout, 0x01000010ul) ==
          V9X_I9XX_POOL_ALIGNMENT);
    CHECK(v9x_i9xx_pool_check_page(&layout, 0ul) == V9X_I9XX_POOL_ZERO_PAGE);
    CHECK(v9x_i9xx_pool_check_page(&layout, NETBOOK_BSM) ==
          V9X_I9XX_POOL_STOLEN);
    CHECK(v9x_i9xx_pool_check_page(&layout,
                                   NETBOOK_BSM + NETBOOK_STOLEN - 4096ul) ==
          V9X_I9XX_POOL_STOLEN);
    CHECK(v9x_i9xx_pool_check_page(&layout, NETBOOK_BSM + NETBOOK_STOLEN) ==
          V9X_I9XX_POOL_OK);
    layout.excluded_count = 1u;
    layout.excluded[0].base = 0x02000000ul;
    layout.excluded[0].bytes = 0x1000ul;
    CHECK(v9x_i9xx_pool_check_page(&layout, 0x02000000ul) ==
          V9X_I9XX_POOL_EXCLUDED);
    CHECK(v9x_i9xx_pool_check_page(&layout, 0x02001000ul) ==
          V9X_I9XX_POOL_OK);
    CHECK(v9x_i9xx_pool_check_page(0, 0x01000000ul) ==
          V9X_I9XX_POOL_BAD_LAYOUT);

    /* The plan alone: refused, overlapping stolen, past the mapping. */
    small_layout(&layout, &plan);
    plan.reason = V9X_I9XX_POOL_NO_ROOM;
    CHECK(v9x_i9xx_pool_check_plan(&layout, &plan) == V9X_I9XX_POOL_NO_ROOM);
    small_layout(&layout, &plan);
    plan.first_entry = 2047ul;
    plan.aperture_offset = plan.first_entry * 4096ul;
    CHECK(v9x_i9xx_pool_check_plan(&layout, &plan) == V9X_I9XX_POOL_APERTURE);
    small_layout(&layout, &plan);
    layout.mapped_bytes -= 4096ul;
    CHECK(v9x_i9xx_pool_check_plan(&layout, &plan) == V9X_I9XX_POOL_APERTURE);
    small_layout(&layout, &plan);
    plan.entry_count = 0ul;
    plan.bytes = 0ul;
    CHECK(v9x_i9xx_pool_check_plan(&layout, &plan) == V9X_I9XX_POOL_COUNT);
}

static void test_pte(void)
{
    CHECK(v9x_i9xx_pool_pte(0x01234000ul) == 0x01234001ul);
    CHECK(v9x_i9xx_pool_pte(0xfffff000ul) == 0xfffff001ul);
}

/*
 * The property the plan asks for: no accepted map touches a stolen or
 * excluded page, lists a page twice, or leaves the GTT, the aperture or the
 * mapping. Random lists drawn near the forbidden ranges, sorted as the
 * driver will sort them, then checked; every accepted one is re-verified
 * here by brute force.
 */
static v9x_u32 lcg_state = 12345ul;

static v9x_u32 lcg_next(void)
{
    lcg_state = lcg_state * 1103515245ul + 12345ul;
    return lcg_state & 0xfffffffful;
}

static void test_property(void)
{
    struct v9x_i9xx_pool_layout layout;
    struct v9x_i9xx_pool_plan plan;
    static v9x_u32 pages[1024];
    v9x_u32 trial;
    v9x_u32 accepted = 0ul;
    v9x_u32 refused = 0ul;

    for (trial = 0ul; trial < 4000ul; ++trial) {
        v9x_u32 index;
        v9x_u32 base = NETBOOK_BSM - 8ul * MIB + (lcg_next() % 6144ul) * 4096ul;
        v9x_u16 reason;

        small_layout(&layout, &plan);
        layout.excluded_count = 1u;
        layout.excluded[0].base = 0x7f000000ul;
        layout.excluded[0].bytes = 0x00040000ul;
        for (index = 0ul; index < 1024ul; ++index) {
            pages[index] = base + (lcg_next() % 4096ul) * 4096ul;
            if ((lcg_next() & 1023ul) == 0ul) {
                pages[index] |= 0x800ul;
            }
        }
        v9x_i9xx_pool_sort(pages, 1024ul);
        reason = v9x_i9xx_pool_check(&layout, &plan, pages, 1024ul);
        if (reason != V9X_I9XX_POOL_OK) {
            ++refused;
            continue;
        }
        ++accepted;
        for (index = 0ul; index < 1024ul; ++index) {
            CHECK((pages[index] & 0xffful) == 0ul);
            CHECK(pages[index] != 0ul);
            CHECK(pages[index] < NETBOOK_BSM ||
                  pages[index] >= NETBOOK_BSM + NETBOOK_STOLEN);
            CHECK(pages[index] < 0x7f000000ul ||
                  pages[index] >= 0x7f040000ul);
            if (index > 0ul) {
                CHECK(pages[index] != pages[index - 1ul]);
            }
        }
    }
    /* Random lists of 1024 from 4096 pages nearly always repeat a page, so
     * build accepted lists too: distinct, clear of both ranges. */
    for (trial = 0ul; trial < 200ul; ++trial) {
        v9x_u32 index;
        v9x_u32 page = 0x00100000ul + (lcg_next() % 1024ul) * 4096ul;

        small_layout(&layout, &plan);
        for (index = 0ul; index < 1024ul; ++index) {
            pages[index] = page;
            page += (1ul + lcg_next() % 3ul) * 4096ul;
        }
        CHECK(v9x_i9xx_pool_check(&layout, &plan, pages, 1024ul) ==
              V9X_I9XX_POOL_OK);
        ++accepted;
    }
    CHECK(refused > 0ul);
    CHECK(accepted >= 200ul);
}

unsigned int v9x_run_i9xx_pool_tests(void)
{
    test_size_netbook();
    test_size_limits();
    test_size_bad_layout();
    test_sort();
    test_check();
    test_check_plan_bounds();
    test_streaming();
    test_pte();
    test_property();
    return failures;
}
