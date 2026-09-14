#include "velocity9x/intel_gma.h"

#define V9X_I9XX_FNV_OFFSET ((v9x_u32)2166136261ul)

/*
 * The FNV prime as a sum of powers of two: 16777619 = 2^24 + 2^8 + 2^7 + 2^4
 * + 2^1 + 1. Multiplying by the shifts instead of by the constant keeps this
 * unit free of __U4M, Open Watcom's 32-bit multiply helper, which lives in
 * clibc.lib's _TEXT and therefore cannot be reached by a near call from the
 * I9XXCODE segment this unit is compiled into
 * (docs\plans\intel-gma950-phase5.md). It is exact, not an approximation:
 * modular arithmetic distributes over the sum, so every bit of the result is
 * identical to the multiply it replaces. test_i9xx_gtt.c's hash expectations
 * are the proof.
 */
static v9x_u32 v9x_i9xx_hash_dword(v9x_u32 hash, v9x_u32 value)
{
    v9x_u16 byte_index;
    for (byte_index = 0u; byte_index < 4u; ++byte_index) {
        hash ^= value & 0xfful;
        hash = (hash << 24) + (hash << 8) + (hash << 7) +
               (hash << 4) + (hash << 1) + hash;
        value >>= 8;
    }
    return hash;
}

v9x_status v9x_i9xx_decode_pte(v9x_u32 raw, struct v9x_i9xx_pte *pte)
{
    v9x_u32 attributes;
    if (pte == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    attributes = raw & V9X_I9XX_PTE_ATTRIBUTE_MASK;
    pte->raw = raw;
    pte->physical_page = raw & V9X_I9XX_PTE_ADDRESS_MASK;
    pte->present = (raw & V9X_I9XX_PTE_VALID) != 0ul;
    pte->cache_bits = (v9x_u16)(raw & V9X_I9XX_PTE_CACHE_MASK);
    pte->known_attributes =
        attributes == 0ul || attributes == V9X_I9XX_PTE_VALID ||
        attributes == (V9X_I9XX_PTE_VALID | V9X_I9XX_PTE_LOCAL) ||
        attributes == (V9X_I9XX_PTE_VALID | V9X_I9XX_PTE_SYSTEM_CACHED);
    return V9X_STATUS_OK;
}

v9x_status v9x_i9xx_gtt_inventory_begin(
    struct v9x_i9xx_gtt_inventory *inventory,
    v9x_u32 entry_count, v9x_u32 bsm, v9x_u32 stolen_bytes,
    v9x_u32 vbe_bytes, v9x_u32 pgtbl_ctl)
{
    v9x_u32 reserve_pages =
        V9X_I9XX_GTT_RESERVE_BYTES / V9X_I9XX_GTT_PAGE_BYTES;
    v9x_u32 vbe_pages;

    if (inventory == 0 || entry_count == 0ul ||
        entry_count > V9X_I9XX_GTT_ENTRY_COUNT ||
        (bsm & (V9X_I9XX_GTT_PAGE_BYTES - 1ul)) != 0ul ||
        stolen_bytes < V9X_I9XX_GTT_BYTES ||
        (stolen_bytes & (V9X_I9XX_GTT_PAGE_BYTES - 1ul)) != 0ul ||
        vbe_bytes < V9X_I9XX_GTT_RESERVE_BYTES ||
        (vbe_bytes & (V9X_I9XX_GTT_PAGE_BYTES - 1ul)) != 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    vbe_pages = vbe_bytes / V9X_I9XX_GTT_PAGE_BYTES;
    if (vbe_pages > entry_count || bsm > 0xfffffffful - stolen_bytes ||
        bsm > 0xfffffffful - (entry_count * V9X_I9XX_GTT_PAGE_BYTES)) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    inventory->flags = 0u;
    inventory->reserve_ok = V9X_TRUE;
    inventory->expected_entries = entry_count;
    inventory->next_entry = 0ul;
    inventory->bsm = bsm;
    inventory->stolen_bytes = stolen_bytes;
    inventory->pgtbl_ctl = pgtbl_ctl;
    inventory->gtt_storage_physical = bsm + stolen_bytes - V9X_I9XX_GTT_BYTES;
    inventory->vbe_pages = vbe_pages;
    inventory->hash_stream = V9X_I9XX_FNV_OFFSET;
    inventory->hash_first = 0ul;
    inventory->hash_second = 0ul;
    inventory->present_entries = 0ul;
    inventory->uncached_entries = 0ul;
    inventory->local_entries = 0ul;
    inventory->cached_entries = 0ul;
    inventory->unknown_attribute_entries = 0ul;
    inventory->run_count = 0ul;
    inventory->backed_prefix_entries = 0ul;
    inventory->reserve_first_entry = vbe_pages - reserve_pages;
    inventory->reserve_entry_count = reserve_pages;
    inventory->reserve_aperture_offset =
        inventory->reserve_first_entry * V9X_I9XX_GTT_PAGE_BYTES;
    inventory->reserve_physical = bsm + inventory->reserve_aperture_offset;
    inventory->first_raw = 0ul;
    inventory->previous_raw = 0ul;
    inventory->previous_physical = 0ul;
    inventory->previous_stride = 0xfffffffful;
    inventory->run_length = 0ul;
    inventory->previous_present = V9X_FALSE;
    inventory->previous_attributes = 0u;
    inventory->prefix_open = V9X_TRUE;
    inventory->all_zero = V9X_TRUE;
    inventory->all_ones = V9X_TRUE;
    return V9X_STATUS_OK;
}

v9x_status v9x_i9xx_gtt_inventory_add(
    struct v9x_i9xx_gtt_inventory *inventory,
    v9x_u32 entry, v9x_u32 raw)
{
    struct v9x_i9xx_pte pte;
    v9x_u32 expected_physical;
    v9x_u16 starts_run = V9X_FALSE;

    if (inventory == 0 || entry != inventory->next_entry ||
        entry >= inventory->expected_entries) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    v9x_i9xx_decode_pte(raw, &pte);
    inventory->hash_stream = v9x_i9xx_hash_dword(
        inventory->hash_stream, raw);
    if (raw != 0ul) { inventory->all_zero = V9X_FALSE; }
    if (raw != 0xfffffffful) { inventory->all_ones = V9X_FALSE; }

    if (pte.present != V9X_FALSE) {
        ++inventory->present_entries;
        if (pte.cache_bits == 0u) {
            ++inventory->uncached_entries;
        } else if (pte.cache_bits == V9X_I9XX_PTE_LOCAL) {
            ++inventory->local_entries;
        } else if (pte.cache_bits == V9X_I9XX_PTE_SYSTEM_CACHED) {
            ++inventory->cached_entries;
        }
    }
    if (pte.known_attributes == V9X_FALSE) {
        ++inventory->unknown_attribute_entries;
    }

    expected_physical = inventory->bsm +
        entry * V9X_I9XX_GTT_PAGE_BYTES;
    if (inventory->prefix_open != V9X_FALSE &&
        pte.present != V9X_FALSE && pte.physical_page == expected_physical) {
        ++inventory->backed_prefix_entries;
    } else {
        inventory->prefix_open = V9X_FALSE;
    }
    if (entry >= inventory->reserve_first_entry &&
        entry < inventory->reserve_first_entry + inventory->reserve_entry_count &&
        (pte.present == V9X_FALSE || pte.physical_page != expected_physical)) {
        inventory->reserve_ok = V9X_FALSE;
    }

    if (entry == 0ul) {
        inventory->first_raw = raw;
        starts_run = V9X_TRUE;
    } else if (pte.present != inventory->previous_present ||
               (v9x_u16)(raw & V9X_I9XX_PTE_ATTRIBUTE_MASK) !=
                   inventory->previous_attributes) {
        starts_run = V9X_TRUE;
    } else if (pte.present != V9X_FALSE) {
        v9x_u32 stride = pte.physical_page - inventory->previous_physical;
        if ((stride != 0ul && stride != V9X_I9XX_GTT_PAGE_BYTES) ||
            (inventory->run_length > 1ul &&
             stride != inventory->previous_stride)) {
            starts_run = V9X_TRUE;
        }
    } else if (raw != inventory->previous_raw) {
        starts_run = V9X_TRUE;
    }
    if (starts_run != V9X_FALSE) {
        ++inventory->run_count;
        inventory->run_length = 1ul;
        inventory->previous_stride = 0xfffffffful;
    } else {
        ++inventory->run_length;
        if (pte.present != V9X_FALSE && inventory->run_length == 2ul) {
            inventory->previous_stride =
                pte.physical_page - inventory->previous_physical;
        }
    }
    inventory->previous_raw = raw;
    inventory->previous_physical = pte.physical_page;
    inventory->previous_present = pte.present;
    inventory->previous_attributes =
        (v9x_u16)(raw & V9X_I9XX_PTE_ATTRIBUTE_MASK);
    ++inventory->next_entry;
    return V9X_STATUS_OK;
}

v9x_status v9x_i9xx_gtt_inventory_finish(
    struct v9x_i9xx_gtt_inventory *inventory,
    v9x_u32 hash_first, v9x_u32 hash_second)
{
    struct v9x_i9xx_pte first;
    if (inventory == 0 || inventory->next_entry != inventory->expected_entries) {
        return V9X_STATUS_INVALID_STATE;
    }
    inventory->flags |= V9X_I9XX_GTT_COMPLETE;
    inventory->hash_first = hash_first;
    inventory->hash_second = hash_second;
    if (hash_first == hash_second && hash_first == inventory->hash_stream) {
        inventory->flags |= V9X_I9XX_GTT_STABLE;
    }
    if (inventory->all_zero == V9X_FALSE &&
        inventory->all_ones == V9X_FALSE) {
        inventory->flags |= V9X_I9XX_GTT_NONTRIVIAL;
    }
    v9x_i9xx_decode_pte(inventory->first_raw, &first);
    if (first.present != V9X_FALSE && first.physical_page == inventory->bsm) {
        inventory->flags |= V9X_I9XX_GTT_BSM_START;
    }
    if (inventory->backed_prefix_entries >= inventory->vbe_pages) {
        inventory->flags |= V9X_I9XX_GTT_VBE_BACKED;
    }
    if (inventory->reserve_ok != V9X_FALSE) {
        inventory->flags |= V9X_I9XX_GTT_RESERVE_BACKED;
    }
    if ((inventory->pgtbl_ctl & V9X_I9XX_PTE_VALID) != 0ul &&
        (inventory->pgtbl_ctl & V9X_I9XX_PTE_ADDRESS_MASK) ==
            inventory->gtt_storage_physical) {
        inventory->flags |= V9X_I9XX_GTT_PGTBL_MATCH;
    }
    return V9X_STATUS_OK;
}
