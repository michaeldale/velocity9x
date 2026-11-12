/*
 * The Gen3 texture pool's policy: size, place, and the page-list check.
 * See include\velocity9x\i9xx_pool.h for what each function promises and
 * docs\plans\intel-gen3-system-memory-textures.md for why the pool exists.
 */
#include "velocity9x/i9xx_pool.h"

#define V9X_I9XX_POOL_PAGE_MASK   (V9X_I9XX_POOL_PAGE_BYTES - 1ul)
/* 2^20 entries of 4 KiB are the whole 32-bit space; more cannot be a GTT
 * this driver maps, and the byte product below would wrap. */
#define V9X_I9XX_POOL_MAX_ENTRIES ((v9x_u32)0x00100000ul)

static void v9x_i9xx_pool_refuse(struct v9x_i9xx_pool_plan *plan,
                                 v9x_u16 reason)
{
    plan->aperture_offset = 0ul;
    plan->bytes = 0ul;
    plan->first_entry = 0ul;
    plan->entry_count = 0ul;
    plan->reason = reason;
}

/* Whether [base, base + bytes) holds `page`. Written as a difference so a
 * range that ends at 4 GiB does not wrap. */
static int v9x_i9xx_pool_in_range(v9x_u32 page, v9x_u32 base, v9x_u32 bytes)
{
    return page >= base && page - base < bytes;
}

/*
 * The layout is checked before the RAM rule so that a broken layout is
 * reported as broken on any machine. Stolen memory must be whole pages and
 * end at or below 4 GiB, the CPU's mapping must lie inside the aperture,
 * and the GTT must be one this driver could map.
 */
static int v9x_i9xx_pool_layout_ok(const struct v9x_i9xx_pool_layout *layout)
{
    if (layout->stolen_bytes == 0ul ||
        (layout->stolen_bytes & V9X_I9XX_POOL_PAGE_MASK) != 0ul ||
        (layout->bsm & V9X_I9XX_POOL_PAGE_MASK) != 0ul ||
        layout->stolen_bytes - 1ul > 0xfffffffful - layout->bsm) {
        return 0;
    }
    if (layout->aperture_bytes == 0ul ||
        (layout->aperture_bytes & V9X_I9XX_POOL_PAGE_MASK) != 0ul ||
        layout->mapped_bytes > layout->aperture_bytes ||
        layout->gtt_entries == 0ul ||
        layout->gtt_entries > V9X_I9XX_POOL_MAX_ENTRIES ||
        layout->excluded_count > V9X_I9XX_POOL_MAX_EXCLUDED) {
        return 0;
    }
    return 1;
}

/*
 * Where the pool may end: the least of the CPU's mapping, the aperture and
 * the GTT's reach. The CPU's mapping bounds it because every CPU write to
 * the pool goes through the aperture (decision record, Conditions).
 */
static v9x_u32 v9x_i9xx_pool_limit(const struct v9x_i9xx_pool_layout *layout)
{
    v9x_u32 limit = layout->mapped_bytes;
    v9x_u32 gtt_reach;

    if (layout->aperture_bytes < limit) {
        limit = layout->aperture_bytes;
    }
    /* At most 2^20 entries: 2^32 bytes would wrap to zero, so the full
     * GTT's reach is taken as the top page's end, 4 GiB less one page. */
    gtt_reach = layout->gtt_entries >= V9X_I9XX_POOL_MAX_ENTRIES
                    ? 0xfffff000ul
                    : layout->gtt_entries * V9X_I9XX_POOL_PAGE_BYTES;
    if (gtt_reach < limit) {
        limit = gtt_reach;
    }
    return limit;
}

v9x_u16 v9x_i9xx_pool_size(const struct v9x_i9xx_pool_layout *layout,
                           struct v9x_i9xx_pool_plan *plan)
{
    v9x_u32 offset;
    v9x_u32 limit;
    v9x_u32 bytes;

    if (plan == 0) {
        return V9X_I9XX_POOL_BAD_LAYOUT;
    }
    if (layout == 0 || !v9x_i9xx_pool_layout_ok(layout)) {
        v9x_i9xx_pool_refuse(plan, V9X_I9XX_POOL_BAD_LAYOUT);
        return plan->reason;
    }
    if (layout->ram_bytes < V9X_I9XX_POOL_MIN_RAM_BYTES) {
        v9x_i9xx_pool_refuse(plan, V9X_I9XX_POOL_NO_RAM);
        return plan->reason;
    }

    /* Stolen ends at or below 4 GiB, so its size is at most 4 GiB less a
     * page and the round-up cannot wrap past the alignment's own size. */
    offset = (layout->stolen_bytes + V9X_I9XX_POOL_ALIGN_BYTES - 1ul) &
             ~(V9X_I9XX_POOL_ALIGN_BYTES - 1ul);
    limit = v9x_i9xx_pool_limit(layout);
    if (offset == 0ul || limit <= offset) {
        v9x_i9xx_pool_refuse(plan, V9X_I9XX_POOL_NO_ROOM);
        return plan->reason;
    }
    bytes = (limit - offset) & ~(V9X_I9XX_POOL_GRAIN_BYTES - 1ul);
    if (bytes > V9X_I9XX_POOL_BYTES) {
        bytes = V9X_I9XX_POOL_BYTES;
    }
    if (bytes < V9X_I9XX_POOL_MIN_BYTES) {
        v9x_i9xx_pool_refuse(plan, V9X_I9XX_POOL_NO_ROOM);
        return plan->reason;
    }

    plan->aperture_offset = offset;
    plan->bytes = bytes;
    plan->first_entry = offset / V9X_I9XX_POOL_PAGE_BYTES;
    plan->entry_count = bytes / V9X_I9XX_POOL_PAGE_BYTES;
    plan->reason = V9X_I9XX_POOL_OK;
    return V9X_I9XX_POOL_OK;
}

static void v9x_i9xx_pool_sift(v9x_u32 *pages, v9x_u32 root, v9x_u32 count)
{
    v9x_u32 child;
    v9x_u32 value = pages[root];

    for (;;) {
        child = root * 2ul + 1ul;
        if (child >= count) {
            break;
        }
        if (child + 1ul < count && pages[child + 1ul] > pages[child]) {
            ++child;
        }
        if (pages[child] <= value) {
            break;
        }
        pages[root] = pages[child];
        root = child;
    }
    pages[root] = value;
}

void v9x_i9xx_pool_sort(v9x_u32 *pages, v9x_u32 count)
{
    v9x_u32 index;
    v9x_u32 top;

    if (pages == 0 || count < 2ul) {
        return;
    }
    for (index = count / 2ul; index > 0ul; --index) {
        v9x_i9xx_pool_sift(pages, index - 1ul, count);
    }
    for (index = count - 1ul; index > 0ul; --index) {
        top = pages[0];
        pages[0] = pages[index];
        pages[index] = top;
        v9x_i9xx_pool_sift(pages, 0ul, index);
    }
}

/*
 * The plan against the layout it is to be applied to. A plan is built by
 * v9x_i9xx_pool_size, but the check does not trust that: the entries must
 * start at or above the stolen run's, match the offset, and end inside the
 * GTT, the aperture and the CPU's mapping.
 */
static int v9x_i9xx_pool_plan_fits(const struct v9x_i9xx_pool_layout *layout,
                                   const struct v9x_i9xx_pool_plan *plan)
{
    v9x_u32 end;

    if (plan->first_entry > V9X_I9XX_POOL_MAX_ENTRIES ||
        plan->entry_count > V9X_I9XX_POOL_MAX_ENTRIES ||
        plan->aperture_offset !=
            plan->first_entry * V9X_I9XX_POOL_PAGE_BYTES ||
        plan->bytes != plan->entry_count * V9X_I9XX_POOL_PAGE_BYTES ||
        plan->aperture_offset < layout->stolen_bytes) {
        return 0;
    }
    if (plan->first_entry > layout->gtt_entries ||
        plan->entry_count > layout->gtt_entries - plan->first_entry) {
        return 0;
    }
    if (plan->bytes > 0xfffffffful - plan->aperture_offset) {
        return 0;
    }
    end = plan->aperture_offset + plan->bytes;
    return end <= layout->aperture_bytes && end <= layout->mapped_bytes;
}

v9x_u16 v9x_i9xx_pool_check_plan(const struct v9x_i9xx_pool_layout *layout,
                                 const struct v9x_i9xx_pool_plan *plan)
{
    if (layout == 0 || plan == 0 || !v9x_i9xx_pool_layout_ok(layout)) {
        return V9X_I9XX_POOL_BAD_LAYOUT;
    }
    if (plan->reason != V9X_I9XX_POOL_OK) {
        return plan->reason;
    }
    if (plan->entry_count == 0ul) {
        return V9X_I9XX_POOL_COUNT;
    }
    if (!v9x_i9xx_pool_plan_fits(layout, plan)) {
        return V9X_I9XX_POOL_APERTURE;
    }
    return V9X_I9XX_POOL_OK;
}

v9x_u16 v9x_i9xx_pool_check_page(const struct v9x_i9xx_pool_layout *layout,
                                 v9x_u32 page)
{
    v9x_u16 range;

    if (layout == 0) {
        return V9X_I9XX_POOL_BAD_LAYOUT;
    }
    if ((page & V9X_I9XX_POOL_PAGE_MASK) != 0ul) {
        return V9X_I9XX_POOL_ALIGNMENT;
    }
    if (page == 0ul) {
        return V9X_I9XX_POOL_ZERO_PAGE;
    }
    if (v9x_i9xx_pool_in_range(page, layout->bsm, layout->stolen_bytes)) {
        return V9X_I9XX_POOL_STOLEN;
    }
    for (range = 0u; range < layout->excluded_count &&
                     range < V9X_I9XX_POOL_MAX_EXCLUDED; ++range) {
        if (v9x_i9xx_pool_in_range(page, layout->excluded[range].base,
                                   layout->excluded[range].bytes)) {
            return V9X_I9XX_POOL_EXCLUDED;
        }
    }
    return V9X_I9XX_POOL_OK;
}

v9x_u16 v9x_i9xx_pool_check(const struct v9x_i9xx_pool_layout *layout,
                            const struct v9x_i9xx_pool_plan *plan,
                            const v9x_u32 *pages, v9x_u32 count)
{
    v9x_u32 index;
    v9x_u16 reason;

    reason = v9x_i9xx_pool_check_plan(layout, plan);
    if (reason != V9X_I9XX_POOL_OK) {
        return reason;
    }
    if (pages == 0 || count != plan->entry_count) {
        return V9X_I9XX_POOL_COUNT;
    }

    for (index = 0ul; index < count; ++index) {
        v9x_u32 page = pages[index];

        /* Alignment and zero first, so a bad entry is named for what it is
         * rather than for being out of order. */
        if ((page & V9X_I9XX_POOL_PAGE_MASK) != 0ul) {
            return V9X_I9XX_POOL_ALIGNMENT;
        }
        if (page == 0ul) {
            return V9X_I9XX_POOL_ZERO_PAGE;
        }
        /* Strictly ascending is what proves no page is listed twice. */
        if (index > 0ul && page <= pages[index - 1ul]) {
            return V9X_I9XX_POOL_ORDER;
        }
        reason = v9x_i9xx_pool_check_page(layout, page);
        if (reason != V9X_I9XX_POOL_OK) {
            return reason;
        }
    }
    return V9X_I9XX_POOL_OK;
}

v9x_u32 v9x_i9xx_pool_pte(v9x_u32 physical_page)
{
    return (physical_page & ~V9X_I9XX_POOL_PAGE_MASK) |
           V9X_I9XX_POOL_PTE_VALID;
}
