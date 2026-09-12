#include <stdio.h>

#include "velocity9x/intel_gma.h"

static unsigned int failures = 0u;

#define CHECK(expression) do { \
    if (!(expression)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #expression); \
        ++failures; \
    } \
} while (0)

typedef char v9x_event_contract_is_stable[
    V9X_I9XX_EVENT_MAX == 32u && V9X_I9XX_EVENT_DWORDS == 20u &&
    V9X_I9XX_EVENT_SEQUENCE == 0u && V9X_I9XX_EVENT_FENCE0 == 10u &&
    V9X_I9XX_EVENT_GTT_HASH_A == 18u &&
    V9X_I9XX_EVENT_GTT_HASH_B == 19u &&
    V9X_I9XX_EVENT_REQUIRED == 0x003fu ? 1 : -1];

static void test_pte_decode(void)
{
    struct v9x_i9xx_pte pte;
    CHECK(v9x_i9xx_decode_pte(0x7f800001ul, &pte) == V9X_STATUS_OK);
    CHECK(pte.present != 0u && pte.physical_page == 0x7f800000ul);
    CHECK(pte.cache_bits == 0u && pte.known_attributes != 0u);
    CHECK(v9x_i9xx_decode_pte(0x00123007ul, &pte) == V9X_STATUS_OK);
    CHECK(pte.cache_bits == V9X_I9XX_PTE_SYSTEM_CACHED);
    CHECK(pte.physical_page == 0x00123000ul);
    CHECK(v9x_i9xx_decode_pte(0x00123005ul, &pte) == V9X_STATUS_OK);
    CHECK(pte.known_attributes == 0u);
    CHECK(v9x_i9xx_decode_pte(0ul, 0) == V9X_STATUS_INVALID_ARGUMENT);
}

static void test_inventory(void)
{
    struct v9x_i9xx_gtt_inventory inventory;
    v9x_u32 index;
    CHECK(v9x_i9xx_gtt_inventory_begin(
              &inventory, 8ul, 0x10000000ul, 0x00100000ul,
              8ul * 4096ul, 0x100c0001ul) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_i9xx_gtt_inventory_begin(
              &inventory, 64ul, 0x10000000ul, 0x00100000ul,
              64ul * 4096ul, 0x100c0001ul) ==
          V9X_STATUS_OK);
    for (index = 0ul; index < 64ul; ++index) {
        v9x_u32 raw = 0x10000001ul + index * 4096ul;
        CHECK(v9x_i9xx_gtt_inventory_add(
                  &inventory, index, raw) == V9X_STATUS_OK);
    }
    CHECK(v9x_i9xx_gtt_inventory_finish(
              &inventory, inventory.hash_stream, inventory.hash_stream) ==
          V9X_STATUS_OK);
    CHECK(inventory.flags == V9X_I9XX_GTT_PHASE2_REQUIRED);
    CHECK(inventory.present_entries == 64ul);
    CHECK(inventory.uncached_entries == 64ul);
    CHECK(inventory.run_count == 1ul);
    CHECK(inventory.backed_prefix_entries == 64ul);
    CHECK(inventory.reserve_first_entry == 32ul);
    CHECK(inventory.reserve_entry_count == 32ul);
    CHECK(inventory.reserve_physical == 0x10020000ul);
    CHECK(inventory.hash_first == inventory.hash_second);

    CHECK(v9x_i9xx_gtt_inventory_begin(
              &inventory, 64ul, 0x10000000ul, 0x00100000ul,
              64ul * 4096ul, 0x100c0001ul) ==
          V9X_STATUS_OK);
    for (index = 0ul; index < 64ul; ++index) {
        v9x_u32 raw = 0x10000001ul + index * 4096ul;
        if (index == 40ul) { raw = 0ul; }
        CHECK(v9x_i9xx_gtt_inventory_add(
                  &inventory, index, raw) == V9X_STATUS_OK);
    }
    CHECK(v9x_i9xx_gtt_inventory_finish(
              &inventory, inventory.hash_stream,
              inventory.hash_stream ^ 1ul) == V9X_STATUS_OK);
    CHECK((inventory.flags & V9X_I9XX_GTT_STABLE) == 0u);
    CHECK((inventory.flags & V9X_I9XX_GTT_VBE_BACKED) == 0u);
    CHECK((inventory.flags & V9X_I9XX_GTT_RESERVE_BACKED) == 0u);
    CHECK(inventory.backed_prefix_entries == 40ul);
    CHECK(inventory.run_count == 3ul);
}

/* The shape Phase 0 and Phase 1 predict for the netbook: stolen memory mapped
 * linearly from BSM for the VBE-reported size, then a scratch page. This is a
 * hypothesis until INTELGTT.BIN is read off the machine, not a measurement. */
static void test_expected_netbook_shape(void)
{
    struct v9x_i9xx_gtt_inventory inventory;
    v9x_u32 index;
    const v9x_u32 bsm = 0x7f800000ul;
    const v9x_u32 vbe_bytes = 8060928ul;
    const v9x_u32 vbe_pages = vbe_bytes / V9X_I9XX_GTT_PAGE_BYTES;

    CHECK(v9x_i9xx_gtt_inventory_begin(
              &inventory, V9X_I9XX_GTT_ENTRY_COUNT, bsm, 0x00800000ul,
              vbe_bytes, 0x7ffc0001ul) ==
          V9X_STATUS_OK);
    for (index = 0ul; index < V9X_I9XX_GTT_ENTRY_COUNT; ++index) {
        v9x_u32 raw = index < vbe_pages
            ? bsm + index * V9X_I9XX_GTT_PAGE_BYTES + V9X_I9XX_PTE_VALID
            : 0x7ffb0001ul;
        CHECK(v9x_i9xx_gtt_inventory_add(
                  &inventory, index, raw) == V9X_STATUS_OK);
    }
    CHECK(v9x_i9xx_gtt_inventory_finish(
              &inventory, inventory.hash_stream, inventory.hash_stream) ==
          V9X_STATUS_OK);
    CHECK(inventory.flags == V9X_I9XX_GTT_PHASE2_REQUIRED);
    CHECK(inventory.backed_prefix_entries == vbe_pages + 1ul);
    CHECK(inventory.reserve_first_entry == vbe_pages - 32ul);
    CHECK(inventory.reserve_physical == 0x7ff90000ul);
    CHECK(inventory.run_count == 2ul);
}

unsigned int v9x_run_i9xx_gtt_tests(void)
{
    test_pte_decode();
    test_inventory();
    test_expected_netbook_shape();
    return failures;
}
