/*
 * Runtime mode-table construction.
 *
 * This is where the dynamic-discovery judgement lives, so this is where it gets
 * tested. The ring-0 scan can only be exercised on a guest; everything it hands
 * back is decided here, against BIOS answers shaped like the ones real cards
 * give - a long QEMU list with OEM widescreen numbers that have no standard
 * VESA number at all, a short S3 list, and the malformed cases a video BIOS is
 * entirely capable of producing.
 */
#include <stdio.h>
#include <string.h>

#include "velocity9x/vbe_modes.h"

static unsigned int modes_failures = 0u;

#define MODECHECK(expression) do { \
    if (!(expression)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, \
               #expression); \
        ++modes_failures; \
    } \
} while (0)

/* The seven rows every family ships today, as the baseline block. */
static const V9X_HW16_MODE baseline_seven[] = {
    {  640u, 480u,  8u,  640u, 0x0101u, 254, 127 },
    {  800u, 600u,  8u,  800u, 0x0103u, 318, 159 },
    { 1024u, 768u,  8u, 1024u, 0x0105u, 407, 203 },
    {  640u, 400u,  8u,  640u, 0x0100u, 254, 127 },
    {  640u, 480u, 16u, 1280u, 0x0111u, 254, 127 },
    {  800u, 600u, 16u, 1600u, 0x0114u, 318, 159 },
    { 1024u, 768u, 16u, 2048u, 0x0117u, 407, 203 }
};
#define BASELINE_SEVEN_COUNT \
    ((v9x_u16)(sizeof(baseline_seven) / sizeof(baseline_seven[0])))

/*
 * A drivable scanned mode. Attributes 0x81 is supported + linear framebuffer,
 * memory model 6 is direct colour, and the physical base is a plausible PCI
 * aperture rather than real-mode memory.
 */
static void make_entry(struct v9x_vbe_scan_entry *entry, v9x_u16 number,
                       v9x_u16 width, v9x_u16 height, v9x_u16 bpp,
                       v9x_u16 stride)
{
    memset(entry, 0, sizeof(*entry));
    entry->mode_number = number;
    entry->summary.attributes = 0x0081u;
    entry->summary.memory_model = bpp == 8u ? 4u : 6u;
    entry->summary.phys_base = 0xf5000000ul;
    entry->summary.width = width;
    entry->summary.height = height;
    entry->summary.bits_per_pixel = bpp;
    entry->summary.bytes_per_scan_line = stride;
    entry->summary.lin_bytes_per_scan_line = stride;
    if (bpp == 16u) {
        entry->summary.red_mask_size = 5u;
        entry->summary.red_field_position = 11u;
        entry->summary.green_mask_size = 6u;
        entry->summary.green_field_position = 5u;
        entry->summary.blue_mask_size = 5u;
        entry->summary.blue_field_position = 0u;
    } else if (bpp == 24u || bpp == 32u) {
        entry->summary.red_mask_size = 8u;
        entry->summary.red_field_position = 16u;
        entry->summary.green_mask_size = 8u;
        entry->summary.green_field_position = 8u;
        entry->summary.blue_mask_size = 8u;
        entry->summary.blue_field_position = 0u;
        if (bpp == 32u) {
            entry->summary.rsvd_mask_size = 8u;
            entry->summary.rsvd_field_position = 24u;
        }
    }
}

static void test_english_values(void)
{
    short low = 0;
    short high = 0;

    /* Reproduces every value the hand-written tables already carry. */
    v9x_mode_english(640u, &low, &high);
    MODECHECK(low == 254 && high == 127);
    v9x_mode_english(800u, &low, &high);
    MODECHECK(low == 318 && high == 159);
    v9x_mode_english(1024u, &low, &high);
    MODECHECK(low == 407 && high == 203);
    /* And extends to the widths dynamic discovery introduces. */
    v9x_mode_english(1280u, &low, &high);
    MODECHECK(low == 508 && high == 254);
    v9x_mode_english(1152u, &low, &high);
    MODECHECK(low == 458 && high == 229);
}

static void test_accept_admits_a_good_mode(void)
{
    struct v9x_vbe_scan_entry entry;

    make_entry(&entry, 0x0118u, 1024u, 768u, 24u, 3072u);
    MODECHECK(v9x_vbe_scan_accept(&entry, 4ul * 1024ul * 1024ul) == V9X_TRUE);
    /* Unknown VRAM skips the memory test rather than refusing. */
    MODECHECK(v9x_vbe_scan_accept(&entry, 0ul) == V9X_TRUE);
}

static void test_accept_refuses_the_pathological(void)
{
    struct v9x_vbe_scan_entry entry;

    /* No linear framebuffer: bit 7 clear leaves a banked mode this driver
     * has no path for. */
    make_entry(&entry, 0x0117u, 1024u, 768u, 16u, 2048u);
    entry.summary.attributes = 0x0001u;
    MODECHECK(v9x_vbe_scan_accept(&entry, 0ul) == V9X_FALSE);

    /* Not supported in hardware. */
    make_entry(&entry, 0x0117u, 1024u, 768u, 16u, 2048u);
    entry.summary.attributes = 0x0080u;
    MODECHECK(v9x_vbe_scan_accept(&entry, 0ul) == V9X_FALSE);

    /* 15 bpp. Real BIOSes list these and the layout maths cannot express one. */
    make_entry(&entry, 0x0110u, 640u, 480u, 16u, 1280u);
    entry.summary.bits_per_pixel = 15u;
    MODECHECK(v9x_vbe_scan_accept(&entry, 0ul) == V9X_FALSE);

    /* A stride past the 16-bit pitch field. 4095 wide at 32 bpp is 16380,
     * which fits; the refusal has to come from the reported stride. */
    make_entry(&entry, 0x0140u, 2048u, 1536u, 32u, 8192u);
    entry.summary.lin_bytes_per_scan_line = 0u;
    entry.summary.bytes_per_scan_line = 0u;
    MODECHECK(v9x_vbe_scan_accept(&entry, 0ul) == V9X_FALSE);

    /* A stride narrower than the pixels it must hold. */
    make_entry(&entry, 0x0118u, 1024u, 768u, 24u, 1024u);
    MODECHECK(v9x_vbe_scan_accept(&entry, 0ul) == V9X_FALSE);

    /* Bigger than the card. 1024x768x24 needs 2.25 MiB; a 2 MiB Trio64
     * refuses it here rather than offering it and failing the mode set. */
    make_entry(&entry, 0x0118u, 1024u, 768u, 24u, 3072u);
    MODECHECK(v9x_vbe_scan_accept(&entry, 2ul * 1024ul * 1024ul) == V9X_FALSE);
    MODECHECK(v9x_vbe_scan_accept(&entry, 4ul * 1024ul * 1024ul) == V9X_TRUE);

    /* Geometry past what a row can describe. */
    make_entry(&entry, 0x0141u, 4096u, 4096u, 8u, 4096u);
    MODECHECK(v9x_vbe_scan_accept(&entry, 0ul) == V9X_FALSE);

    /* An aperture in real-mode memory is a read that went wrong. */
    make_entry(&entry, 0x0117u, 1024u, 768u, 16u, 2048u);
    entry.summary.phys_base = 0x000a0000ul;
    MODECHECK(v9x_vbe_scan_accept(&entry, 0ul) == V9X_FALSE);

    /* A 32-bpp mode with no channel layout at all cannot be published. */
    make_entry(&entry, 0x0142u, 800u, 600u, 32u, 3200u);
    entry.summary.red_mask_size = 0u;
    entry.summary.green_mask_size = 0u;
    entry.summary.blue_mask_size = 0u;
    MODECHECK(v9x_vbe_scan_accept(&entry, 0ul) == V9X_FALSE);

    /* A 16-bpp one may: 5:6:5 is the only layout the driver programs. */
    make_entry(&entry, 0x0117u, 1024u, 768u, 16u, 2048u);
    entry.summary.red_mask_size = 0u;
    entry.summary.green_mask_size = 0u;
    entry.summary.blue_mask_size = 0u;
    MODECHECK(v9x_vbe_scan_accept(&entry, 0ul) == V9X_TRUE);

    MODECHECK(v9x_vbe_scan_accept(0, 0ul) == V9X_FALSE);
}

/* A family that does not scan must get its own table back, untouched. */
static void test_baseline_only(void)
{
    V9X_HW16_MODE table[V9X_MODE_TABLE_MAX];
    struct v9x_mode_masks masks[V9X_MODE_TABLE_MAX];
    v9x_u16 dropped = 0xffffu;
    v9x_u16 count;
    v9x_u16 index;

    count = v9x_vbe_build_mode_table(baseline_seven, BASELINE_SEVEN_COUNT,
                                     0, 0u, 0ul, table, masks,
                                     V9X_MODE_TABLE_MAX, &dropped);
    MODECHECK(count == BASELINE_SEVEN_COUNT);
    MODECHECK(dropped == 0u);
    for (index = 0u; index < count; ++index) {
        MODECHECK(table[index].width == baseline_seven[index].width);
        MODECHECK(table[index].height == baseline_seven[index].height);
        MODECHECK(table[index].bits_per_pixel ==
                  baseline_seven[index].bits_per_pixel);
        MODECHECK(table[index].pitch == baseline_seven[index].pitch);
        MODECHECK(table[index].vbe_mode == baseline_seven[index].vbe_mode);
    }
    /* Canonical masks: palettized at 8, 5:6:5 at 16. */
    MODECHECK(masks[0].red == 0ul && masks[0].green == 0ul &&
              masks[0].blue == 0ul);
    MODECHECK(masks[4].red == 0x0000f800ul && masks[4].green == 0x000007e0ul &&
              masks[4].blue == 0x0000001ful);
}

/*
 * A QEMU-shaped list: the standard numbers plus OEM widescreen entries above
 * 0x011F, which is the whole reason the scan exists - no VESA number describes
 * 1280x800 or 1440x900, so no static table could ever have carried them.
 */
static void test_qemu_shaped_list(void)
{
    struct v9x_vbe_scan_entry scanned[12];
    V9X_HW16_MODE table[V9X_MODE_TABLE_MAX];
    struct v9x_mode_masks masks[V9X_MODE_TABLE_MAX];
    v9x_u16 count;
    v9x_u16 dropped = 0xffffu;
    v9x_u16 index;
    v9x_u16 widescreen_rows = 0u;

    make_entry(&scanned[0], 0x0101u,  640u, 480u,  8u,  640u);
    make_entry(&scanned[1], 0x0111u,  640u, 480u, 16u, 1280u);
    make_entry(&scanned[2], 0x0112u,  640u, 480u, 32u, 2560u);
    make_entry(&scanned[3], 0x0115u,  800u, 600u, 32u, 3200u);
    make_entry(&scanned[4], 0x0118u, 1024u, 768u, 32u, 4096u);
    make_entry(&scanned[5], 0x011Au, 1280u, 1024u, 16u, 2560u);
    make_entry(&scanned[6], 0x0140u, 1280u, 800u,  8u, 1280u);
    make_entry(&scanned[7], 0x0141u, 1280u, 800u, 32u, 5120u);
    make_entry(&scanned[8], 0x0142u, 1440u, 900u, 32u, 5760u);
    /* Two the scan must throw away, mixed in with the good ones. */
    make_entry(&scanned[9], 0x010Du, 320u, 200u, 16u, 640u);
    scanned[9].summary.bits_per_pixel = 15u;
    make_entry(&scanned[10], 0x0102u, 800u, 600u, 8u, 800u);
    scanned[10].summary.attributes = 0x0001u;   /* no linear framebuffer */
    make_entry(&scanned[11], 0x0143u, 1600u, 1200u, 32u, 6400u);

    count = v9x_vbe_build_mode_table(baseline_seven, BASELINE_SEVEN_COUNT,
                                     scanned, 12u,
                                     16ul * 1024ul * 1024ul,
                                     table, masks, V9X_MODE_TABLE_MAX,
                                     &dropped);
    MODECHECK(dropped == 0u);

    /* The baseline block survives verbatim and in order. table[0] is the boot
     * fallback and table[3] is the Doom95 row; both must stay put. */
    MODECHECK(table[0].width == 640u && table[0].height == 480u &&
              table[0].bits_per_pixel == 8u);
    MODECHECK(table[3].width == 640u && table[3].height == 400u &&
              table[3].bits_per_pixel == 8u);

    /*
     * Twelve scanned entries: two are refused (15 bpp, no linear
     * framebuffer), and two more - 640x480x8 and 640x480x16 - are already
     * baseline rows and update them in place rather than appending. The
     * remaining eight are new rows.
     */
    MODECHECK(count == (v9x_u16)(BASELINE_SEVEN_COUNT + 8u));

    /* Every (w,h,bpp) appears exactly once. */
    for (index = 0u; index < count; ++index) {
        v9x_u16 other;

        for (other = (v9x_u16)(index + 1u); other < count; ++other) {
            MODECHECK(!(table[index].width == table[other].width &&
                        table[index].height == table[other].height &&
                        table[index].bits_per_pixel ==
                            table[other].bits_per_pixel));
        }
    }

    /* The appended region is ordered by depth, then width, then height. */
    for (index = (v9x_u16)(BASELINE_SEVEN_COUNT + 1u); index < count;
         ++index) {
        const V9X_HW16_MODE *previous = &table[index - 1u];
        const V9X_HW16_MODE *current = &table[index];

        MODECHECK(previous->bits_per_pixel <= current->bits_per_pixel);
        if (previous->bits_per_pixel == current->bits_per_pixel) {
            MODECHECK(previous->width <= current->width);
        }
    }

    /* The widescreen modes are present with their OEM numbers and correct
     * English values - the point of the whole exercise. */
    for (index = 0u; index < count; ++index) {
        if (table[index].width == 1280u && table[index].height == 800u) {
            ++widescreen_rows;
            MODECHECK(table[index].english_low == 508);
            MODECHECK(table[index].vbe_mode == 0x0140u ||
                      table[index].vbe_mode == 0x0141u);
        }
        if (table[index].width == 1440u) {
            MODECHECK(table[index].vbe_mode == 0x0142u);
            MODECHECK(table[index].bits_per_pixel == 32u);
        }
        /* The refused entries must be absent. */
        MODECHECK(table[index].bits_per_pixel != 15u);
        MODECHECK(!(table[index].width == 320u));
    }
    MODECHECK(widescreen_rows == 2u);
}

/*
 * The case the doc singles out: a baseline row saying one depth for a mode
 * number while the BIOS says another, and a hand-typed stride the BIOS
 * contradicts. The scanned answer wins on stride, which is what makes the
 * v9x_vbe_mode_matches stride check unable to fail afterwards.
 */
static void test_scan_corrects_the_baseline(void)
{
    struct v9x_vbe_scan_entry scanned[2];
    V9X_HW16_MODE table[V9X_MODE_TABLE_MAX];
    struct v9x_mode_masks masks[V9X_MODE_TABLE_MAX];
    v9x_u16 count;

    /* Same geometry and depth as baseline row 6 (1024x768x16), but the BIOS
     * reports a padded stride and a different mode number. */
    make_entry(&scanned[0], 0x0217u, 1024u, 768u, 16u, 2560u);
    /* And a 24-bpp mode carrying the number a baseline row might have claimed
     * was 32 bpp. It is a different depth, so it is a new row, not an update. */
    make_entry(&scanned[1], 0x0118u, 1024u, 768u, 24u, 3072u);

    count = v9x_vbe_build_mode_table(baseline_seven, BASELINE_SEVEN_COUNT,
                                     scanned, 2u, 8ul * 1024ul * 1024ul,
                                     table, masks, V9X_MODE_TABLE_MAX, 0);
    MODECHECK(count == (v9x_u16)(BASELINE_SEVEN_COUNT + 1u));
    MODECHECK(table[6].width == 1024u && table[6].bits_per_pixel == 16u);
    MODECHECK(table[6].pitch == 2560u);
    MODECHECK(table[6].vbe_mode == 0x0217u);
    /* The new 24-bpp row landed with the BIOS's own masks. */
    MODECHECK(table[7].bits_per_pixel == 24u);
    MODECHECK(table[7].pitch == 3072u);
    MODECHECK(masks[7].red == 0x00ff0000ul);
    MODECHECK(masks[7].green == 0x0000ff00ul);
    MODECHECK(masks[7].blue == 0x000000fful);
}

/* More accepted modes than the table can hold: keep what fits, count the rest,
 * and do not write past the end. */
static void test_overflow_is_bounded(void)
{
    struct v9x_vbe_scan_entry scanned[80];
    V9X_HW16_MODE table[V9X_MODE_TABLE_MAX];
    struct v9x_mode_masks masks[V9X_MODE_TABLE_MAX];
    v9x_u16 count;
    v9x_u16 dropped = 0u;
    v9x_u16 index;

    /* 80 distinct drivable modes; heights step so each (w,h,bpp) is unique. */
    for (index = 0u; index < 80u; ++index) {
        make_entry(&scanned[index], (v9x_u16)(0x0200u + index),
                   640u, (v9x_u16)(400u + index), 8u, 640u);
    }
    count = v9x_vbe_build_mode_table(baseline_seven, BASELINE_SEVEN_COUNT,
                                     scanned, 80u, 0ul, table, masks,
                                     V9X_MODE_TABLE_MAX, &dropped);
    MODECHECK(count == V9X_MODE_TABLE_MAX);
    /* Heights run 400..479, so exactly one of the 80 - 640x400x8 - is already
     * a baseline row and updates it in place. The other 79 each want a new
     * row, and the table had room for V9X_MODE_TABLE_MAX - 7 of them. */
    MODECHECK(dropped == (v9x_u16)(79u - (V9X_MODE_TABLE_MAX -
                                         BASELINE_SEVEN_COUNT)));
    MODECHECK(table[0].width == 640u && table[0].bits_per_pixel == 8u);

    /* A capacity below the baseline count still must not overrun. */
    count = v9x_vbe_build_mode_table(baseline_seven, BASELINE_SEVEN_COUNT,
                                     scanned, 80u, 0ul, table, masks,
                                     3u, &dropped);
    MODECHECK(count == 3u);
    MODECHECK(dropped != 0u);

    /* Degenerate arguments. */
    MODECHECK(v9x_vbe_build_mode_table(baseline_seven, BASELINE_SEVEN_COUNT,
                                       0, 0u, 0ul, 0, masks,
                                       V9X_MODE_TABLE_MAX, 0) == 0u);
    MODECHECK(v9x_vbe_build_mode_table(baseline_seven, BASELINE_SEVEN_COUNT,
                                       0, 0u, 0ul, table, masks, 0u, 0) == 0u);
}

/*
 * The DirectDraw subset. The shared block holds fewer rows than the table can,
 * so the choice of which to publish is policy and gets asserted.
 */
static void test_dd_subset(void)
{
    V9X_HW16_MODE table[10];
    v9x_u16 indices[10];
    v9x_u16 chosen;
    v9x_u16 index;

    memset(table, 0, sizeof(table));
    /* Three 8/16-bpp rows and four high-colour ones in deliberately
     * non-ascending area order. */
    table[0].width =  640u; table[0].height = 480u; table[0].bits_per_pixel =  8u;
    table[1].width = 1024u; table[1].height = 768u; table[1].bits_per_pixel = 16u;
    table[2].width =  800u; table[2].height = 600u; table[2].bits_per_pixel = 16u;
    table[3].width = 1280u; table[3].height = 1024u; table[3].bits_per_pixel = 32u;
    table[4].width =  640u; table[4].height = 480u; table[4].bits_per_pixel = 32u;
    table[5].width = 1024u; table[5].height = 768u; table[5].bits_per_pixel = 24u;
    table[6].width =  800u; table[6].height = 600u; table[6].bits_per_pixel = 32u;

    chosen = v9x_vbe_dd_subset(table, 7u, indices, 10u);
    MODECHECK(chosen == 7u);
    /* 8/16 bpp first, in table order. */
    MODECHECK(indices[0] == 0u);
    MODECHECK(indices[1] == 1u);
    MODECHECK(indices[2] == 2u);
    /* Then high colour by ascending area: 640x480, 800x600, 1024x768,
     * 1280x1024. */
    MODECHECK(indices[3] == 4u);
    MODECHECK(indices[4] == 6u);
    MODECHECK(indices[5] == 5u);
    MODECHECK(indices[6] == 3u);

    /* Truncation keeps the low depths and the smallest high-colour modes. */
    chosen = v9x_vbe_dd_subset(table, 7u, indices, 5u);
    MODECHECK(chosen == 5u);
    MODECHECK(indices[3] == 4u);
    MODECHECK(indices[4] == 6u);
    for (index = 0u; index < chosen; ++index) {
        MODECHECK(indices[index] != 3u); /* the largest was cut */
    }

    MODECHECK(v9x_vbe_dd_subset(table, 7u, indices, 0u) == 0u);
    MODECHECK(v9x_vbe_dd_subset(0, 7u, indices, 10u) == 0u);
}

static void test_masks_to_bits(void)
{
    struct v9x_vbe_scan_entry entry;
    v9x_u32 red = 1ul;
    v9x_u32 green = 1ul;
    v9x_u32 blue = 1ul;

    /* 5:6:5 as a BIOS reports it. */
    make_entry(&entry, 0x0111u, 640u, 480u, 16u, 1280u);
    MODECHECK(v9x_vbe_masks_to_bits(&entry.summary, &red, &green, &blue) ==
              V9X_TRUE);
    MODECHECK(red == 0x0000f800ul && green == 0x000007e0ul &&
              blue == 0x0000001ful);

    /* 32 bpp with a reserved byte. */
    make_entry(&entry, 0x0118u, 1024u, 768u, 32u, 4096u);
    MODECHECK(v9x_vbe_masks_to_bits(&entry.summary, &red, &green, &blue) ==
              V9X_TRUE);
    MODECHECK(red == 0x00ff0000ul && green == 0x0000ff00ul &&
              blue == 0x000000fful);

    /* 8 bpp is palettized: refused, masks zeroed. */
    make_entry(&entry, 0x0101u, 640u, 480u, 8u, 640u);
    MODECHECK(v9x_vbe_masks_to_bits(&entry.summary, &red, &green, &blue) ==
              V9X_FALSE);
    MODECHECK(red == 0ul && green == 0ul && blue == 0ul);

    /* Overlapping channels describe nothing usable. */
    make_entry(&entry, 0x0118u, 1024u, 768u, 32u, 4096u);
    entry.summary.green_field_position = 16u;
    MODECHECK(v9x_vbe_masks_to_bits(&entry.summary, &red, &green, &blue) ==
              V9X_FALSE);

    /* A channel that does not fit inside the pixel. */
    make_entry(&entry, 0x0111u, 640u, 480u, 16u, 1280u);
    entry.summary.red_field_position = 14u;
    MODECHECK(v9x_vbe_masks_to_bits(&entry.summary, &red, &green, &blue) ==
              V9X_FALSE);

    MODECHECK(v9x_vbe_masks_to_bits(0, &red, &green, &blue) == V9X_FALSE);
}

unsigned int v9x_run_vbe_modes_tests(void)
{
    modes_failures = 0u;
    test_english_values();
    test_accept_admits_a_good_mode();
    test_accept_refuses_the_pathological();
    test_baseline_only();
    test_qemu_shaped_list();
    test_scan_corrects_the_baseline();
    test_overflow_is_bounded();
    test_dd_subset();
    test_masks_to_bits();
    return modes_failures;
}
