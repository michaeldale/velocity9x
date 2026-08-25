#include "velocity9x/vbe_modes.h"

/* Canonical masks for a baseline row, whose depth is all the driver knows
 * about it: 8 bpp is palettized, 16 bpp is the 5:6:5 the driver programs, and
 * 24/32 bpp is byte-per-channel with blue in the low byte. A scanned row
 * overwrites these with what the BIOS reported.
 *
 * The 24-bpp arm can only be reached by a family baseline row, since admission
 * refuses scanned 24-bpp modes, and no family ships one. It stays so that the
 * function is total over the depths a row can name rather than silently
 * palettizing one. */
static void v9x_canonical_masks(v9x_u16 bits_per_pixel,
                                struct v9x_mode_masks *out)
{
    if (bits_per_pixel == 16u) {
        out->red = 0x0000f800ul;
        out->green = 0x000007e0ul;
        out->blue = 0x0000001ful;
    } else if (bits_per_pixel == 24u || bits_per_pixel == 32u) {
        out->red = 0x00ff0000ul;
        out->green = 0x0000ff00ul;
        out->blue = 0x000000fful;
    } else {
        out->red = 0ul;
        out->green = 0ul;
        out->blue = 0ul;
    }
}

void v9x_mode_english(v9x_u16 width, short *low, short *high)
{
    v9x_u32 value;

    if (low == 0 || high == 0) {
        return;
    }
    /* Rounded up, not to nearest: at 1024 the two differ (406.4) and the
     * shipped tables say 407, so the ceiling is what reproduces them. */
    value = ((v9x_u32)width * 254ul + 639ul) / 640ul;
    *low = (short)value;
    *high = (short)(value / 2ul);
}

/* The stride a mode set with the linear bit actually produces. */
static v9x_u16 v9x_effective_stride(const struct v9x_vbe_mode_summary *summary)
{
    return summary->lin_bytes_per_scan_line != 0u
               ? summary->lin_bytes_per_scan_line
               : summary->bytes_per_scan_line;
}

v9x_u16 v9x_vbe_scan_admit(const struct v9x_vbe_scan_entry *entry,
                           v9x_u32 vram_bytes)
{
    const struct v9x_vbe_mode_summary *summary;
    v9x_u16 stride;
    v9x_u32 visible;
    v9x_u32 red;
    v9x_u32 green;
    v9x_u32 blue;

    if (entry == 0) {
        return V9X_VBE_ADMIT_UNSUPPORTED;
    }
    summary = &entry->summary;
    /*
     * The drivability rules, decomposed so a refusal names the rule. The
     * fields and order are v9x_vbe_mode_summary_is_drivable's, and the host
     * suite pins admit == OK exactly where is_drivable and the rules below
     * all pass, so the two cannot drift apart silently.
     */
    if ((summary->attributes & V9X_VBE_ATTR_SUPPORTED) == 0u ||
        summary->width == 0u || summary->height == 0u ||
        summary->bits_per_pixel == 0u || summary->bytes_per_scan_line == 0u) {
        return V9X_VBE_ADMIT_UNSUPPORTED;
    }
    if ((summary->attributes & V9X_VBE_ATTR_LINEAR) == 0u) {
        return V9X_VBE_ADMIT_NON_LINEAR;
    }
    if (summary->memory_model != V9X_VBE_MODEL_PACKED_PIXEL &&
        summary->memory_model != V9X_VBE_MODEL_DIRECT_COLOR) {
        return V9X_VBE_ADMIT_MEMORY_MODEL;
    }
    if (summary->phys_base < V9X_VBE_MIN_PHYS_BASE) {
        return V9X_VBE_ADMIT_PHYS_BASE;
    }
    /*
     * The depths this driver can lay out: 8, 16 and 32.
     *
     * 24 is missing on purpose. It divides into whole bytes and the parser
     * derives its depths perfectly well, but display16 has never drawn a
     * 24-bpp surface - no family baseline table has such a row, ddi.c splits
     * three ways on 8/16/else, and the DIB Engine has no 24/32 surface flag to
     * set. Admitting one here would offer a mode the blitters cannot draw,
     * which is worse than not offering it, so 24-bpp bring-up is its own piece
     * of work. QEMU std-vga publishes 24-bpp modes, so this is the common case
     * rather than a corner: see docs\plans\dynamic-vbe-pipeline.md.
     */
    if (summary->bits_per_pixel != 8u && summary->bits_per_pixel != 16u &&
        summary->bits_per_pixel != 32u) {
        return V9X_VBE_ADMIT_DEPTH;
    }
    if (summary->width > V9X_MODE_MAX_DIMENSION ||
        summary->height > V9X_MODE_MAX_DIMENSION) {
        return V9X_VBE_ADMIT_GEOMETRY;
    }

    stride = v9x_effective_stride(summary);
    if (stride == 0u) {
        return V9X_VBE_ADMIT_STRIDE;
    }
    /*
     * The stride has to cover the pixels. A BIOS reporting a stride narrower
     * than width * bytes-per-pixel is describing a surface the driver would
     * draw off the end of every scan line.
     */
    if ((v9x_u32)stride <
        (v9x_u32)summary->width * (v9x_u32)(summary->bits_per_pixel / 8u)) {
        return V9X_VBE_ADMIT_STRIDE;
    }

    /* stride is already 16-bit, so only the product can overflow. */
    visible = (v9x_u32)stride * (v9x_u32)summary->height;
    if (vram_bytes != 0ul && visible > vram_bytes) {
        return V9X_VBE_ADMIT_VRAM;
    }

    /* 8 bpp is palettized and has no mask to express; every other depth must
     * have one, or the row could not be published to DirectDraw. */
    if (summary->bits_per_pixel != 8u &&
        v9x_vbe_masks_to_bits(summary, &red, &green, &blue) == V9X_FALSE) {
        return V9X_VBE_ADMIT_LAYOUT;
    }
    return V9X_VBE_ADMIT_OK;
}

v9x_u16 v9x_vbe_scan_accept(const struct v9x_vbe_scan_entry *entry,
                            v9x_u32 vram_bytes)
{
    return v9x_vbe_scan_admit(entry, vram_bytes) == V9X_VBE_ADMIT_OK
               ? V9X_TRUE
               : V9X_FALSE;
}

/* Fill one table row and its masks from a scanned mode. */
static void v9x_row_from_scan(const struct v9x_vbe_scan_entry *entry,
                              V9X_HW16_MODE *row,
                              struct v9x_mode_masks *mask)
{
    const struct v9x_vbe_mode_summary *summary = &entry->summary;

    row->width = summary->width;
    row->height = summary->height;
    row->bits_per_pixel = summary->bits_per_pixel;
    row->pitch = v9x_effective_stride(summary);
    row->vbe_mode = entry->mode_number;
    v9x_mode_english(summary->width, &row->english_low, &row->english_high);

    if (summary->bits_per_pixel == 8u ||
        v9x_vbe_masks_to_bits(summary, &mask->red, &mask->green,
                              &mask->blue) == V9X_FALSE) {
        v9x_canonical_masks(summary->bits_per_pixel, mask);
    }
}

/* Index of the row with this geometry and depth, or count when absent. */
static v9x_u16 v9x_find_row(const V9X_HW16_MODE *table, v9x_u16 count,
                            v9x_u16 width, v9x_u16 height, v9x_u16 bpp)
{
    v9x_u16 index;

    for (index = 0u; index < count; ++index) {
        if (table[index].width == width && table[index].height == height &&
            table[index].bits_per_pixel == bpp) {
            return index;
        }
    }
    return count;
}

/* Does a sort before b, by depth then width then height? */
static v9x_u16 v9x_row_precedes(const V9X_HW16_MODE *a,
                                const V9X_HW16_MODE *b)
{
    if (a->bits_per_pixel != b->bits_per_pixel) {
        return a->bits_per_pixel < b->bits_per_pixel ? V9X_TRUE : V9X_FALSE;
    }
    if (a->width != b->width) {
        return a->width < b->width ? V9X_TRUE : V9X_FALSE;
    }
    return a->height < b->height ? V9X_TRUE : V9X_FALSE;
}

/* Tally one admission outcome, when the caller asked for tallies at all. */
static void v9x_count_reason(v9x_u16 *reason_counts, v9x_u16 reason)
{
    if (reason_counts != 0 && reason < V9X_VBE_ADMIT_REASON_COUNT) {
        ++reason_counts[reason];
    }
}

v9x_u16 v9x_vbe_build_mode_table(
    const V9X_HW16_MODE *baseline, v9x_u16 baseline_count,
    const struct v9x_vbe_scan_entry *scanned, v9x_u16 scanned_count,
    v9x_u32 vram_bytes,
    V9X_HW16_MODE *table, struct v9x_mode_masks *masks,
    v9x_u16 capacity, v9x_u16 *dropped)
{
    return v9x_vbe_build_mode_table_ex(baseline, baseline_count,
                                       scanned, scanned_count, vram_bytes, 0,
                                       table, masks, capacity, dropped, 0);
}

v9x_u16 v9x_vbe_build_mode_table_ex(
    const V9X_HW16_MODE *baseline, v9x_u16 baseline_count,
    const struct v9x_vbe_scan_entry *scanned, v9x_u16 scanned_count,
    v9x_u32 vram_bytes, v9x_vbe_distrust_fn distrust,
    V9X_HW16_MODE *table, struct v9x_mode_masks *masks,
    v9x_u16 capacity, v9x_u16 *dropped, v9x_u16 *reason_counts)
{
    v9x_u16 count = 0u;
    v9x_u16 appended_from;
    v9x_u16 index;
    v9x_u16 lost = 0u;

    if (dropped != 0) {
        *dropped = 0u;
    }
    if (reason_counts != 0) {
        for (index = 0u; index < V9X_VBE_ADMIT_REASON_COUNT; ++index) {
            reason_counts[index] = 0u;
        }
    }
    if (table == 0 || masks == 0 || capacity == 0u) {
        return 0u;
    }

    /* The baseline block, verbatim and in order. */
    if (baseline != 0) {
        for (index = 0u; index < baseline_count && count < capacity; ++index) {
            table[count] = baseline[index];
            v9x_canonical_masks(baseline[index].bits_per_pixel, &masks[count]);
            ++count;
        }
        if (index < baseline_count) {
            lost = (v9x_u16)(baseline_count - index);
        }
    }
    appended_from = count;

    if (scanned == 0) {
        if (dropped != 0) {
            *dropped = lost;
        }
        return count;
    }

    for (index = 0u; index < scanned_count; ++index) {
        V9X_HW16_MODE row;
        struct v9x_mode_masks mask;
        v9x_u16 existing;
        v9x_u16 at;
        v9x_u16 reason;

        reason = v9x_vbe_scan_admit(&scanned[index], vram_bytes);
        if (reason != V9X_VBE_ADMIT_OK) {
            v9x_count_reason(reason_counts, reason);
            continue;
        }
        /* The family's distrust predicate can only narrow: it runs after the
         * generic rules and its refusal carries its own reason. */
        if (distrust != 0 && distrust(&scanned[index]) == V9X_TRUE) {
            v9x_count_reason(reason_counts, V9X_VBE_ADMIT_KNOWN_DEFECT);
            continue;
        }
        v9x_row_from_scan(&scanned[index], &row, &mask);

        /*
         * Already in the table? Take the BIOS's mode number, stride and masks
         * onto the existing row rather than adding a second row for a mode the
         * list already offers. A baseline row updated this way is one whose
         * hand-typed stride can no longer disagree with the hardware.
         *
         * The geometry and the English values are identical by construction,
         * so only these three fields move.
         */
        existing = v9x_find_row(table, count, row.width, row.height,
                                row.bits_per_pixel);
        if (existing < count) {
            table[existing].pitch = row.pitch;
            table[existing].vbe_mode = row.vbe_mode;
            masks[existing] = mask;
            v9x_count_reason(reason_counts, V9X_VBE_ADMIT_DUPLICATE);
            continue;
        }

        if (count >= capacity) {
            ++lost;
            v9x_count_reason(reason_counts, V9X_VBE_ADMIT_TABLE_FULL);
            continue;
        }
        v9x_count_reason(reason_counts, V9X_VBE_ADMIT_OK);

        /* Insert into the appended region only, keeping it sorted. The
         * baseline block above it is deliberately left as the family wrote
         * it. */
        at = count;
        while (at > appended_from &&
               v9x_row_precedes(&row, &table[at - 1u]) == V9X_TRUE) {
            table[at] = table[at - 1u];
            masks[at] = masks[at - 1u];
            --at;
        }
        table[at] = row;
        masks[at] = mask;
        ++count;
    }

    if (dropped != 0) {
        *dropped = lost;
    }
    return count;
}

v9x_u16 v9x_vbe_publish_rows(
    const V9X_HW16_MODE *table, v9x_u16 count, v9x_u16 baseline_rows,
    const struct v9x_vbe_scan_entry *scanned, v9x_u16 scanned_count,
    v9x_u32 vram_bytes, v9x_u16 scan_trustworthy,
    v9x_u8 *publication, v9x_u16 *first_published)
{
    v9x_u16 index;
    v9x_u16 published = 0u;
    v9x_u16 first = 0u;
    v9x_u16 hiding;

    if (first_published != 0) {
        *first_published = 0u;
    }
    if (table == 0 || publication == 0) {
        return 0u;
    }

    /* Hiding needs a scan the caller vouches for and that says something. An
     * absent, refused or empty scan contradicts nothing, so everything stays
     * published exactly as the static path publishes it. */
    hiding = (scan_trustworthy == V9X_TRUE && scanned != 0 &&
              scanned_count != 0u)
                 ? V9X_TRUE
                 : V9X_FALSE;

    for (index = 0u; index < count; ++index) {
        v9x_u16 hide = V9X_FALSE;

        if (hiding == V9X_TRUE && index < baseline_rows) {
            /* A baseline row is scan-contradicted when no admitted record
             * describes its geometry at its storage depth. Its slot, order
             * and masks stay; only publication is withdrawn. */
            v9x_u16 scan;

            hide = V9X_TRUE;
            for (scan = 0u; scan < scanned_count; ++scan) {
                const struct v9x_vbe_mode_summary *summary =
                    &scanned[scan].summary;

                if (summary->width == table[index].width &&
                    summary->height == table[index].height &&
                    summary->bits_per_pixel == table[index].bits_per_pixel &&
                    v9x_vbe_scan_accept(&scanned[scan], vram_bytes) ==
                        V9X_TRUE) {
                    hide = V9X_FALSE;
                    break;
                }
            }
        }
        if (hide == V9X_TRUE) {
            publication[index] = V9X_MODE_PUB_HIDE_SCAN;
        } else {
            publication[index] = V9X_MODE_PUB_PUBLISHED;
            if (published == 0u) {
                first = index;
            }
            ++published;
        }
    }

    /* Nothing published is a defect, not a policy outcome: a trustworthy scan
     * that admitted rows cannot contradict every one of them and the appended
     * rows besides. Stand the whole table up rather than commit an empty
     * offer. */
    if (published == 0u) {
        for (index = 0u; index < count; ++index) {
            publication[index] = V9X_MODE_PUB_PUBLISHED;
        }
        published = count;
        first = 0u;
    }

    if (first_published != 0) {
        *first_published = first;
    }
    return published;
}

v9x_u16 v9x_vbe_dd_subset(const V9X_HW16_MODE *table, v9x_u16 count,
                          v9x_u16 *indices, v9x_u16 capacity)
{
    v9x_u16 chosen = 0u;
    v9x_u16 index;

    if (table == 0 || indices == 0 || capacity == 0u) {
        return 0u;
    }

    /* 8 and 16 bpp, in table order. */
    for (index = 0u; index < count && chosen < capacity; ++index) {
        if (table[index].bits_per_pixel == 8u ||
            table[index].bits_per_pixel == 16u) {
            indices[chosen++] = index;
        }
    }

    /* Then the high-colour rows by ascending area. Selection rather than a
     * sort: the list is short, and this leaves the chosen order explicit. */
    for (;;) {
        v9x_u16 best = count;
        v9x_u32 best_area = 0ul;

        if (chosen >= capacity) {
            break;
        }
        for (index = 0u; index < count; ++index) {
            v9x_u32 area;
            v9x_u16 seen;
            v9x_u16 already = V9X_FALSE;

            if (table[index].bits_per_pixel != 24u &&
                table[index].bits_per_pixel != 32u) {
                continue;
            }
            for (seen = 0u; seen < chosen; ++seen) {
                if (indices[seen] == index) {
                    already = V9X_TRUE;
                    break;
                }
            }
            if (already == V9X_TRUE) {
                continue;
            }
            area = (v9x_u32)table[index].width * (v9x_u32)table[index].height;
            if (best == count || area < best_area) {
                best = index;
                best_area = area;
            }
        }
        if (best == count) {
            break;
        }
        indices[chosen++] = best;
    }
    return chosen;
}
