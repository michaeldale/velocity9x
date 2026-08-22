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

    make_entry(&entry, 0x0118u, 1024u, 768u, 32u, 4096u);
    MODECHECK(v9x_vbe_scan_accept(&entry, 4ul * 1024ul * 1024ul) == V9X_TRUE);
    /* Unknown VRAM skips the memory test rather than refusing. */
    MODECHECK(v9x_vbe_scan_accept(&entry, 0ul) == V9X_TRUE);

    /* The other two admitted depths, at the geometry a panel-filtered BIOS
     * offers rather than the standard one. */
    make_entry(&entry, 0x0160u, 1024u, 576u, 8u, 1024u);
    MODECHECK(v9x_vbe_scan_accept(&entry, 4ul * 1024ul * 1024ul) == V9X_TRUE);
    make_entry(&entry, 0x0161u, 1024u, 576u, 16u, 2048u);
    MODECHECK(v9x_vbe_scan_accept(&entry, 4ul * 1024ul * 1024ul) == V9X_TRUE);
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

    /*
     * 24 bpp, refused for a different reason: the mode is perfectly well
     * described - packed RGB 8:8:8, a stride that covers it, room on the card -
     * and this driver has simply never drawn one. QEMU std-vga publishes these,
     * so the refusal is exercised on the first target rather than in theory.
     */
    make_entry(&entry, 0x0118u, 1024u, 768u, 24u, 3072u);
    MODECHECK(v9x_vbe_scan_accept(&entry, 8ul * 1024ul * 1024ul) == V9X_FALSE);
    make_entry(&entry, 0x0112u, 640u, 480u, 24u, 1920u);
    MODECHECK(v9x_vbe_scan_accept(&entry, 0ul) == V9X_FALSE);

    /* A stride past the 16-bit pitch field. 4095 wide at 32 bpp is 16380,
     * which fits; the refusal has to come from the reported stride. */
    make_entry(&entry, 0x0140u, 2048u, 1536u, 32u, 8192u);
    entry.summary.lin_bytes_per_scan_line = 0u;
    entry.summary.bytes_per_scan_line = 0u;
    MODECHECK(v9x_vbe_scan_accept(&entry, 0ul) == V9X_FALSE);

    /* A stride narrower than the pixels it must hold. */
    make_entry(&entry, 0x0118u, 1024u, 768u, 32u, 1024u);
    MODECHECK(v9x_vbe_scan_accept(&entry, 0ul) == V9X_FALSE);

    /* Bigger than the card. 1024x768x32 needs 3 MiB; a 2 MiB Trio64 refuses
     * it here rather than offering it and failing the mode set. */
    make_entry(&entry, 0x0118u, 1024u, 768u, 32u, 4096u);
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
    /* And a 32-bpp mode at the same geometry. It is a different depth, so it
     * is a new row, not an update. */
    make_entry(&scanned[1], 0x0118u, 1024u, 768u, 32u, 4096u);

    count = v9x_vbe_build_mode_table(baseline_seven, BASELINE_SEVEN_COUNT,
                                     scanned, 2u, 8ul * 1024ul * 1024ul,
                                     table, masks, V9X_MODE_TABLE_MAX, 0);
    MODECHECK(count == (v9x_u16)(BASELINE_SEVEN_COUNT + 1u));
    MODECHECK(table[6].width == 1024u && table[6].bits_per_pixel == 16u);
    MODECHECK(table[6].pitch == 2560u);
    MODECHECK(table[6].vbe_mode == 0x0217u);
    /* The new 32-bpp row landed with the BIOS's own masks. */
    MODECHECK(table[7].bits_per_pixel == 32u);
    MODECHECK(table[7].pitch == 4096u);
    MODECHECK(masks[7].red == 0x00ff0000ul);
    MODECHECK(masks[7].green == 0x0000ff00ul);
    MODECHECK(masks[7].blue == 0x000000fful);
}

/*
 * A BIOS that offers 24 bpp alongside the depths this driver draws contributes
 * only the drawable ones. This is the QEMU std-vga shape, and the reason 24 bpp
 * had to be settled before the runtime table was wired to GDI: the refusal has
 * to be a quiet omission from the table, not a failure of the build.
 */
static void test_24bpp_is_omitted_not_fatal(void)
{
    struct v9x_vbe_scan_entry scanned[3];
    V9X_HW16_MODE table[V9X_MODE_TABLE_MAX];
    struct v9x_mode_masks masks[V9X_MODE_TABLE_MAX];
    v9x_u16 count;
    v9x_u16 dropped = 0xffffu;

    make_entry(&scanned[0], 0x0160u, 1024u, 576u, 8u, 1024u);
    make_entry(&scanned[1], 0x0165u, 1024u, 576u, 24u, 3072u);
    make_entry(&scanned[2], 0x0162u, 1024u, 576u, 32u, 4096u);

    count = v9x_vbe_build_mode_table(baseline_seven, BASELINE_SEVEN_COUNT,
                                     scanned, 3u, 8ul * 1024ul * 1024ul,
                                     table, masks, V9X_MODE_TABLE_MAX,
                                     &dropped);
    /* Two new rows, not three, and nothing was dropped for want of room. */
    MODECHECK(count == (v9x_u16)(BASELINE_SEVEN_COUNT + 2u));
    MODECHECK(dropped == 0u);
    {
        v9x_u16 index;
        for (index = 0u; index < count; ++index) {
            MODECHECK(table[index].bits_per_pixel != 24u);
        }
    }
}

/*
 * The GMA950 survey, as the mini-VDD would hand it over.
 *
 * Every field below is transcribed from a DOS survey of MICHAEL-NETBOOK
 * (HP Mini 110, 945GSE, PCI 8086:27AE, fixed 1024x576 LVDS panel) rather than
 * invented: attributes 009B, memory model 4 for the palettized mode and 6 for
 * the direct-colour ones, the aperture at D0000000, the strides the BIOS
 * reports in both the legacy and the linear field, and the channel sizes and
 * positions it reports for each depth.
 *
 * This is the machine that motivated the dynamic pipeline, and it is the one
 * case where the interesting answer is what the BIOS does *not* offer. Of the
 * 36 numbers in its mode list, six answer 4F01h with anything at all - the
 * three Intel OEM modes at the panel's native 1024x576, and three at 640x480 -
 * while five of the seven standard numbers the family baseline is built from
 * either answer with zero geometry or are absent from the list entirely.
 *
 * What this fixture pins is the admission half of that: the six live modes are
 * accepted, two of them update baseline rows in place rather than appending,
 * and the masks that arrive are the ones the BIOS reported rather than the
 * canonical ones the depth would imply. Hiding the five contradicted baseline
 * rows is the publication half and is not implemented yet.
 */
static void make_gma950_entry(struct v9x_vbe_scan_entry *entry, v9x_u16 number,
                              v9x_u16 width, v9x_u16 height, v9x_u16 bpp,
                              v9x_u16 stride)
{
    memset(entry, 0, sizeof(*entry));
    entry->mode_number = number;
    entry->summary.attributes = 0x009bu;
    entry->summary.width = width;
    entry->summary.height = height;
    entry->summary.bits_per_pixel = bpp;
    entry->summary.bytes_per_scan_line = stride;
    entry->summary.lin_bytes_per_scan_line = stride;
    entry->summary.phys_base = 0xd0000000ul;
    entry->summary.mask_flags = V9X_VBE_RF_LIN_STRIDE;
    if (bpp == 8u) {
        entry->summary.memory_model = 4u;
    } else {
        entry->summary.memory_model = 6u;
        entry->summary.mask_flags =
            (v9x_u16)(entry->summary.mask_flags | V9X_VBE_RF_MASKS_LINEAR);
        if (bpp == 16u) {
            entry->summary.red_mask_size = 5u;
            entry->summary.red_field_position = 11u;
            entry->summary.green_mask_size = 6u;
            entry->summary.green_field_position = 5u;
            entry->summary.blue_mask_size = 5u;
            entry->summary.blue_field_position = 0u;
        } else {
            entry->summary.red_mask_size = 8u;
            entry->summary.red_field_position = 16u;
            entry->summary.green_mask_size = 8u;
            entry->summary.green_field_position = 8u;
            entry->summary.blue_mask_size = 8u;
            entry->summary.blue_field_position = 0u;
        }
    }
    entry->summary.significant_depth =
        v9x_vbe_summary_significant_depth(&entry->summary);
}

static void test_gma950_survey(void)
{
    struct v9x_vbe_scan_entry scanned[6];
    V9X_HW16_MODE table[V9X_MODE_TABLE_MAX];
    struct v9x_mode_masks masks[V9X_MODE_TABLE_MAX];
    v9x_u16 count;
    v9x_u16 dropped = 0xffffu;
    v9x_u16 index;
    v9x_u16 seen_576;

    /* In the order the BIOS lists them: the OEM block first, the standard
     * numbers last. Nothing downstream may depend on that order, which is
     * part of what this checks. */
    make_gma950_entry(&scanned[0], 0x0160u, 1024u, 576u, 8u, 1024u);
    make_gma950_entry(&scanned[1], 0x0161u, 1024u, 576u, 16u, 2048u);
    make_gma950_entry(&scanned[2], 0x0162u, 1024u, 576u, 32u, 4096u);
    make_gma950_entry(&scanned[3], 0x0112u, 640u, 480u, 32u, 2560u);
    make_gma950_entry(&scanned[4], 0x0101u, 640u, 480u, 8u, 640u);
    make_gma950_entry(&scanned[5], 0x0111u, 640u, 480u, 16u, 1280u);

    /* The derived depths, before anything is merged. 8 bpp is palettized, the
     * 5:6:5 mode has no bits to spare, and the 32-bpp modes carry 24 colour
     * bits in a 32-bit pixel. */
    MODECHECK(scanned[0].summary.significant_depth == 8u);
    MODECHECK(scanned[1].summary.significant_depth == 16u);
    MODECHECK(scanned[2].summary.significant_depth == 24u);
    MODECHECK(scanned[3].summary.significant_depth == 24u);

    /* 4F00h reports 123 blocks of 64 KiB on this machine - 7.69 MiB, less than
     * the 8 MiB the host bridge says is stolen, and the smaller figure is the
     * one admission must use. */
    count = v9x_vbe_build_mode_table(baseline_seven, BASELINE_SEVEN_COUNT,
                                     scanned, 6u,
                                     123ul * 65536ul,
                                     table, masks, V9X_MODE_TABLE_MAX,
                                     &dropped);

    /* Four new rows: 1024x576 at three depths and 640x480x32. The other two
     * scanned modes match baseline rows and update them in place. */
    MODECHECK(count == (v9x_u16)(BASELINE_SEVEN_COUNT + 4u));
    MODECHECK(dropped == 0u);

    /* Row zero is 640x480x8, and it is alive here - the BIOS lists 0101h and
     * answers for it - so the fallback row is unchanged. */
    MODECHECK(table[0].width == 640u && table[0].height == 480u);
    MODECHECK(table[0].bits_per_pixel == 8u);
    MODECHECK(table[0].vbe_mode == 0x0101u);
    MODECHECK(table[0].pitch == 640u);

    /* The 640x480x16 baseline row keeps its position and takes the BIOS's
     * reported stride and mode number. */
    MODECHECK(table[4].bits_per_pixel == 16u);
    MODECHECK(table[4].vbe_mode == 0x0111u);
    MODECHECK(table[4].pitch == 1280u);
    MODECHECK(masks[4].red == 0x0000f800ul);
    MODECHECK(masks[4].green == 0x000007e0ul);
    MODECHECK(masks[4].blue == 0x0000001ful);

    /* The 640x400 Doom95 row and the 800x600 and 1024x768 rows are still in
     * the table, untouched, because a baseline row is never removed. They are
     * also exactly the rows this BIOS cannot set, which is what the
     * publication flag will hide once it exists. */
    MODECHECK(table[3].width == 640u && table[3].height == 400u);
    MODECHECK(table[3].vbe_mode == 0x0100u);
    MODECHECK(table[1].width == 800u && table[1].vbe_mode == 0x0103u);
    MODECHECK(table[2].width == 1024u && table[2].height == 768u);
    MODECHECK(table[6].width == 1024u && table[6].height == 768u);

    /* All three native-panel rows landed, with the strides and masks the BIOS
     * reported. 1024x576x32 is 2.25 MiB, so nothing is dropped for VRAM on a
     * machine that reports 7.69. */
    seen_576 = 0u;
    for (index = 0u; index < count; ++index) {
        if (table[index].width != 1024u || table[index].height != 576u) {
            continue;
        }
        ++seen_576;
        switch (table[index].bits_per_pixel) {
        case 8u:
            MODECHECK(table[index].vbe_mode == 0x0160u);
            MODECHECK(table[index].pitch == 1024u);
            MODECHECK(masks[index].red == 0ul);
            MODECHECK(masks[index].green == 0ul);
            MODECHECK(masks[index].blue == 0ul);
            break;
        case 16u:
            MODECHECK(table[index].vbe_mode == 0x0161u);
            MODECHECK(table[index].pitch == 2048u);
            MODECHECK(masks[index].red == 0x0000f800ul);
            break;
        case 32u:
            MODECHECK(table[index].vbe_mode == 0x0162u);
            MODECHECK(table[index].pitch == 4096u);
            MODECHECK(masks[index].red == 0x00ff0000ul);
            MODECHECK(masks[index].green == 0x0000ff00ul);
            MODECHECK(masks[index].blue == 0x000000fful);
            break;
        default:
            MODECHECK(0);
            break;
        }
        /* The GDIINFO dimensions come from the width, and 1024 is 407/203
         * whatever the height is. */
        MODECHECK(table[index].english_low == 407);
        MODECHECK(table[index].english_high == 203);
    }
    MODECHECK(seen_576 == 3u);

    /*
     * And the panel's own limit, which is the whole reason this machine is the
     * fixture: 1024x768 is in the baseline table and this BIOS cannot set it,
     * but nothing in the scan says so - the mode simply is not among the
     * records. Admission cannot infer a refusal from an absence, which is why
     * hiding is a separate decision made against a scan known to be complete.
     */
    for (index = 0u; index < 6u; ++index) {
        MODECHECK(scanned[index].summary.height <= 576u);
    }
}

/*
 * The QEMU std-vga mode list, from a DOS capture of the guest this family
 * ships for (SeaBIOS VBE, VBE 3.0, 16 MiB reported, 93 modes listed and
 * terminated).
 *
 * Only the modes that survive admission are listed below - the capture's other
 * 44 rows are 18 without a linear framebuffer, 19 at 24 bpp and 7 at 15 bpp,
 * each of which has its own test above. What these 49 rows are here for is the
 * pressure they put on the *published* lists, which no hand-built fixture had
 * reproduced: 48 distinct geometries and depths against 64 table rows and 32
 * DirectDraw slots.
 *
 * Two facts fall out, and both are load-bearing rather than incidental:
 *
 *   - mode 0013h and mode 0146h both describe 320x200x8, so a real BIOS list
 *     contains duplicate geometry at the same depth and the merge has to
 *     collapse it;
 *   - the DirectDraw subset fills 28 of its 32 slots with 8- and 16-bpp rows,
 *     leaving four for high colour, so an ordinary 1024x768x32 desktop is *not*
 *     in the ordinary subset. That is what makes "guarantee the current desktop
 *     row is present" a requirement and not a nicety.
 *
 * The pitch is width * bytes-per-pixel throughout, which is what the capture
 * reports for every one of these modes.
 */
struct qemu_row {
    v9x_u16 mode;
    v9x_u16 width;
    v9x_u16 height;
    v9x_u16 bpp;
};

static const struct qemu_row qemu_stdvga[] = {
    /* 8 bpp: nine rows, two of them the same 320x200. */
    { 0x0100u,  640u,  400u,  8u }, { 0x0101u,  640u,  480u,  8u },
    { 0x0103u,  800u,  600u,  8u }, { 0x0105u, 1024u,  768u,  8u },
    { 0x0107u, 1280u, 1024u,  8u }, { 0x011Cu, 1600u, 1200u,  8u },
    { 0x0146u,  320u,  200u,  8u }, { 0x0148u, 1152u,  864u,  8u },
    { 0x0013u,  320u,  200u,  8u },
    /* 16 bpp: twenty rows. */
    { 0x010Eu,  320u,  200u, 16u }, { 0x0111u,  640u,  480u, 16u },
    { 0x0114u,  800u,  600u, 16u }, { 0x0117u, 1024u,  768u, 16u },
    { 0x011Au, 1280u, 1024u, 16u }, { 0x011Eu, 1600u, 1200u, 16u },
    { 0x014Au, 1152u,  864u, 16u }, { 0x0175u, 1280u,  768u, 16u },
    { 0x0178u, 1280u,  800u, 16u }, { 0x017Bu, 1280u,  960u, 16u },
    { 0x017Eu, 1440u,  900u, 16u }, { 0x0181u, 1400u, 1050u, 16u },
    { 0x0184u, 1680u, 1050u, 16u }, { 0x0187u, 1920u, 1200u, 16u },
    { 0x018Au, 2560u, 1600u, 16u }, { 0x018Du, 1280u,  720u, 16u },
    { 0x0190u, 1920u, 1080u, 16u }, { 0x0193u, 1600u,  900u, 16u },
    { 0x0196u, 2560u, 1440u, 16u }, { 0x0199u, 3840u, 2160u, 16u },
    /* 32 bpp: twenty rows. */
    { 0x0140u,  320u,  200u, 32u }, { 0x0141u,  640u,  400u, 32u },
    { 0x0142u,  640u,  480u, 32u }, { 0x0143u,  800u,  600u, 32u },
    { 0x0144u, 1024u,  768u, 32u }, { 0x0145u, 1280u, 1024u, 32u },
    { 0x0147u, 1600u, 1200u, 32u }, { 0x014Cu, 1152u,  864u, 32u },
    { 0x0177u, 1280u,  768u, 32u }, { 0x017Au, 1280u,  800u, 32u },
    { 0x017Du, 1280u,  960u, 32u }, { 0x0180u, 1440u,  900u, 32u },
    { 0x0183u, 1400u, 1050u, 32u }, { 0x0186u, 1680u, 1050u, 32u },
    { 0x0189u, 1920u, 1200u, 32u }, { 0x018Cu, 2560u, 1600u, 32u },
    { 0x018Fu, 1280u,  720u, 32u }, { 0x0192u, 1920u, 1080u, 32u },
    { 0x0195u, 1600u,  900u, 32u }, { 0x0198u, 2560u, 1440u, 32u }
};
#define QEMU_STDVGA_COUNT \
    ((v9x_u16)(sizeof(qemu_stdvga) / sizeof(qemu_stdvga[0])))

/* V9X_DD_MODE_COUNT, mirrored rather than included: it lives in
 * include\velocity9x\win9x_ddraw_abi.h, which is a Windows-facing header the
 * host suite deliberately stays out of. The build checks assert the ABI's own
 * value; what this pins is the arithmetic that value forces on this list. */
#define QEMU_DD_SLOTS ((v9x_u16)32u)

static void test_qemu_stdvga_list(void)
{
    struct v9x_vbe_scan_entry scanned[QEMU_STDVGA_COUNT];
    V9X_HW16_MODE table[V9X_MODE_TABLE_MAX];
    struct v9x_mode_masks masks[V9X_MODE_TABLE_MAX];
    v9x_u16 indices[QEMU_DD_SLOTS];
    v9x_u16 count;
    v9x_u16 chosen;
    v9x_u16 dropped = 0xffffu;
    v9x_u16 index;
    v9x_u16 low_depth;
    v9x_u16 desktop_1024x768x32;

    for (index = 0u; index < QEMU_STDVGA_COUNT; ++index) {
        make_entry(&scanned[index], qemu_stdvga[index].mode,
                   qemu_stdvga[index].width, qemu_stdvga[index].height,
                   qemu_stdvga[index].bpp,
                   (v9x_u16)(qemu_stdvga[index].width *
                             (qemu_stdvga[index].bpp / 8u)));
    }

    /* 16 MiB, as 4F00h reports on this guest. 3840x2160 at 16 bpp needs
     * 15.8 MiB of it and is admitted; the same geometry at 32 bpp is not in
     * the list at all, which is the BIOS being sensible rather than us. */
    count = v9x_vbe_build_mode_table(baseline_seven, BASELINE_SEVEN_COUNT,
                                     scanned, QEMU_STDVGA_COUNT,
                                     256ul * 65536ul,
                                     table, masks, V9X_MODE_TABLE_MAX,
                                     &dropped);

    /* 49 scanned rows, one a duplicate geometry, seven matching baseline rows
     * in place: 48 rows, and room to spare in a 64-row table. */
    MODECHECK(count == 48u);
    MODECHECK(dropped == 0u);

    /* Every baseline row was corroborated, so none of them is contradicted on
     * this target and row zero keeps its place and its mode number. */
    MODECHECK(table[0].vbe_mode == 0x0101u);
    MODECHECK(table[3].width == 640u && table[3].height == 400u);
    MODECHECK(table[3].vbe_mode == 0x0100u);

    /* The duplicate collapsed: exactly one 320x200x8 row. */
    {
        v9x_u16 seen = 0u;
        for (index = 0u; index < count; ++index) {
            if (table[index].width == 320u && table[index].height == 200u &&
                table[index].bits_per_pixel == 8u) {
                ++seen;
            }
        }
        MODECHECK(seen == 1u);
    }

    /* No 24-bpp row reached the table from a list that offered nineteen. */
    for (index = 0u; index < count; ++index) {
        MODECHECK(table[index].bits_per_pixel != 24u);
        MODECHECK(table[index].bits_per_pixel != 15u);
    }

    /*
     * DirectDraw now has to choose, which on this target it has never had to
     * do in a test before: 48 rows into 32 slots.
     */
    chosen = v9x_vbe_dd_subset(table, count, indices, QEMU_DD_SLOTS);
    MODECHECK(chosen == QEMU_DD_SLOTS);

    low_depth = 0u;
    desktop_1024x768x32 = 0u;
    for (index = 0u; index < chosen; ++index) {
        const V9X_HW16_MODE *row = &table[indices[index]];
        if (row->bits_per_pixel == 8u || row->bits_per_pixel == 16u) {
            ++low_depth;
        }
        if (row->width == 1024u && row->height == 768u &&
            row->bits_per_pixel == 32u) {
            desktop_1024x768x32 = 1u;
        }
    }
    /* Twenty-eight 8- and 16-bpp rows take priority, leaving four slots. */
    MODECHECK(low_depth == 28u);
    /*
     * And this is the finding: an ordinary 1024x768x32 desktop does not make
     * the cut, because four smaller high-colour modes come first. Nothing is
     * wrong with the subset policy - the list is simply longer than the block.
     * It is why dd16.c must substitute the active row rather than trust the
     * ordinary selection, and why that rule needs its own test rather than a
     * comment.
     */
    MODECHECK(desktop_1024x768x32 == 0u);
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
    test_24bpp_is_omitted_not_fatal();
    test_gma950_survey();
    test_qemu_stdvga_list();
    test_overflow_is_bounded();
    test_dd_subset();
    test_masks_to_bits();
    return modes_failures;
}
