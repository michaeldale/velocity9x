/*
 * Building the driver's runtime mode table from the family baseline and the
 * modes the video BIOS actually reports.
 *
 * Split out from both the ring-0 scan and the driver for the same reason
 * vbe_parse.c is: the BIOS call is mechanism and belongs where the mechanism
 * lives, but *which* of the modes it lists the driver should offer is judgement,
 * and judgement is worth testing on the host. The mini-VDD applies only the
 * cheapest admission rules it can at ring 0 - is the mode supported, is the
 * depth one of four - and hands everything else through its API for this file to
 * decide on.
 *
 * Nothing here calls the BIOS, allocates, or touches the OS.
 */
#ifndef VELOCITY9X_VBE_MODES_H
#define VELOCITY9X_VBE_MODES_H

#include "velocity9x/types.h"
#include "velocity9x/vbe_parse.h"
/* V9X_HW16_MODE. Plain C types and no <windows.h>, so it costs the host suite
 * nothing - and sharing the row type avoids a third representation of a mode
 * alongside the family tables and the DirectDraw block. */
#include "velocity9x/hw16.h"

/*
 * Rows the runtime table can hold. 64 is well past what any BIOS this project
 * has met reports as drivable, and bounds the driver's DGROUP cost: 64 rows is
 * 896 bytes of V9X_HW16_MODE plus 768 bytes of masks.
 */
#define V9X_MODE_TABLE_MAX ((v9x_u16)64u)

/* Widest geometry a table row may describe. The pitch field is 16-bit, and a
 * width beyond this cannot produce a usable one at 32 bpp. */
#define V9X_MODE_MAX_DIMENSION ((v9x_u16)4095u)

/* The channel layout for one row, as the 32-bit masks DirectDraw wants. All
 * three zero means palettized. */
struct v9x_mode_masks {
    v9x_u32 red;
    v9x_u32 green;
    v9x_u32 blue;
};

/* One entry of the mini-VDD's scanned-mode cache: the BIOS mode number, and
 * what 4F01h said about it. */
struct v9x_vbe_scan_entry {
    v9x_u16 mode_number;
    struct v9x_vbe_mode_summary summary;
};

/*
 * The English (logical-inch) GDIINFO dimensions for a width.
 *
 * low = ceil(width * 254 / 640), high = low / 2. This is the formula the
 * hand-written family tables were already following: it reproduces 254 at 640,
 * 318 at 800 and 407 at 1024, and gives 508 at 1280. The rounding is upward
 * rather than to nearest, which is the only thing that produces 407 rather
 * than 406 at 1024.
 */
void v9x_mode_english(v9x_u16 width, short *low, short *high);

/*
 * Is this scanned mode one the driver can put in its table?
 *
 * Stricter than v9x_vbe_mode_summary_is_drivable, which asks only whether a
 * mode could be driven at all. On top of that:
 *
 *   - the depth must divide into whole bytes (8, 16, 24, 32). A BIOS listing
 *     15-bpp modes is ordinary and none of them can be laid out.
 *   - the effective stride - the VBE 3.0 linear one where reported, otherwise
 *     BytesPerScanLine - must be non-zero and fit the 16-bit pitch field.
 *   - the whole visible surface must fit the card's memory. vram_bytes of 0
 *     means unknown and skips that test; a real figure is what keeps
 *     1024x768x24 off a 2 MiB card's list rather than on it and failing later.
 *   - geometry within V9X_MODE_MAX_DIMENSION.
 *   - the channel layout must be expressible (v9x_vbe_masks_to_bits).
 */
v9x_u16 v9x_vbe_scan_accept(const struct v9x_vbe_scan_entry *entry,
                            v9x_u32 vram_bytes);

/*
 * Merge the family's baseline rows with the scanned ones into table/masks.
 * Returns the number of rows written; *dropped, when non-null, receives the
 * count of accepted scanned modes there was no room for.
 *
 * The baseline rows come first and in their original order, unchanged except
 * where the BIOS has something better to say about one. That order is load
 * bearing: table[0] is the mode Enable falls back to, and the 640x400 row's
 * position is what makes Doom95 work.
 *
 * A scanned mode whose (width, height, depth) matches a baseline row updates
 * that row in place - taking the BIOS's mode number, stride and masks - rather
 * than being appended as a duplicate. That is also what makes a stride
 * disagreement impossible by construction: the row now states the stride the
 * BIOS reports, so the check in v9x_vbe_mode_matches cannot fail on it.
 *
 * The rest are appended, ordered among themselves by depth, then width, then
 * height. Passing scanned_count = 0 is the non-scanning family case and yields
 * the baseline table exactly.
 */
v9x_u16 v9x_vbe_build_mode_table(
    const V9X_HW16_MODE *baseline, v9x_u16 baseline_count,
    const struct v9x_vbe_scan_entry *scanned, v9x_u16 scanned_count,
    v9x_u32 vram_bytes,
    V9X_HW16_MODE *table, struct v9x_mode_masks *masks,
    v9x_u16 capacity, v9x_u16 *dropped);

/*
 * Choose which rows to publish to DirectDraw, whose shared block holds fewer
 * than the table can.
 *
 * Fills indices[] with row indices and returns how many. Everything at 8 and
 * 16 bpp first, in table order, because that is what applications of the era
 * ask for and what the driver accelerates; then 24 and 32 bpp by ascending
 * pixel area, so if the list has to be cut it keeps the modes most likely to be
 * usable. Stops at capacity.
 */
v9x_u16 v9x_vbe_dd_subset(const V9X_HW16_MODE *table, v9x_u16 count,
                          v9x_u16 *indices, v9x_u16 capacity);

#endif /* VELOCITY9X_VBE_MODES_H */
