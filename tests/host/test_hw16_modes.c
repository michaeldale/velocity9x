/*
 * The family mode tables against the manifests.
 *
 * test_family_matrix.c already holds the backends to the manifests, but the
 * table GDI and the INF actually run on is a different one: the
 * V9X_HW16_MODE array inside each family's *_hw16.c, hand-written, and until
 * now checked by nothing. The same seven rows were stated in the C table, the
 * manifest, the INF and (until the mode list became variable) the DirectDraw
 * HAL, and any one of them could have been edited alone.
 *
 * These tables cannot simply be linked in: every family exports the same
 * v9x_hw16 symbol, so the four of them cannot coexist in one binary. Each is
 * therefore included as source under its own name, which also means the file
 * under test is the one that ships rather than a copy of its contents.
 *
 * matrox-m2 is deliberately absent. Its table is #ifdef-variant on
 * V9X_MATROX_16BPP, so there is no single row set to compare, and
 * mga2_hw16.c uses Windows types that do not belong in a host build. Its
 * manifest and INF are held together by Assert-V9xInf instead.
 */
#include <stdio.h>
#include <string.h>

#include "velocity9x/hw16.h"
#include "velocity9x/s3_regs16.h"

#include "v9x_family_matrix.h"

static unsigned int hw16_failures = 0u;

#define HCHECK(family, expression) do { \
    if (!(expression)) { \
        printf("FAIL %s:%u: %s: %s\n", __FILE__, (unsigned int)__LINE__, \
               (family), #expression); \
        ++hw16_failures; \
    } \
} while (0)

/*
 * Everything the mode tables' translation units reference and this test does
 * not exercise. They exist to satisfy the linker: a chip descriptor is
 * pointed at by the device list, and the S3 register readers are named by the
 * family ops. Nothing here is called.
 */
const V9X_HW16_DEVICE v9x_virge_device = {
    0u, 0u, 0, 0, 0, 0, 0, 0, 0, 0, 0
};
const V9X_HW16_DEVICE v9x_trio_device = {
    0u, 0u, 0, 0, 0, 0, 0, 0, 0, 0, 0
};
const V9X_HW16_DEVICE v9x_mach64_vt2_device = {
    0u, 0u, 0, 0, 0, 0, 0, 0, 0, 0, 0
};
const V9X_HW16_DEVICE v9x_rage_mobility_device = {
    0u, 0u, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

void v9x_s3_publish_diagnostics(const V9X_HW16_DEVICE *device,
                                v9x_hw16_write_fn write)
{
    (void)device;
    (void)write;
}

unsigned long v9x_s3_read_aperture(void) { return 0ul; }
unsigned long v9x_s3_read_video_memory(void) { return 0ul; }
/* Port I/O, so it cannot run here; the family table only needs the symbol. The
 * behaviour this stands in for is asserted in test_family_matrix.c against the
 * manifest instead. */
unsigned short v9x_s3_identify_without_pci(void) { return 0xffffu; }

unsigned long v9x_vbe_vram_reported = 0ul;
unsigned short v9x_pci_match = 0u;
unsigned short v9x_vbe_scan_bytes = 0u;
unsigned short v9x_vbe_scan_pixels = 0u;
unsigned short v9x_vbe_scan_lines = 0u;
unsigned short v9x_vbe_pitch_before = 0u;
unsigned short v9x_active_pitch = 0u;
unsigned short v9x_active_width = 0u;

#define v9x_hw16 v9x_hw16_s3
#include "../../src/chipsets/s3/s3_hw16.c"
#undef v9x_hw16

#define v9x_hw16 v9x_hw16_vbe
#include "../../src/chipsets/generic/vbe/vbe_hw16.c"
#undef v9x_hw16

#define v9x_hw16 v9x_hw16_ati
#include "../../src/chipsets/ati/ati_hw16.c"
#undef v9x_hw16

static const struct {
    const char *family_id;
    const V9X_HW16_OPS *ops;
} v9x_hw16_tables[] = {
    { "s3",  &v9x_hw16_s3  },
    { "vbe", &v9x_hw16_vbe },
    { "ati", &v9x_hw16_ati }
};

#define V9X_HW16_TABLE_COUNT \
    (sizeof(v9x_hw16_tables) / sizeof(v9x_hw16_tables[0]))

static const V9X_HW16_OPS *ops_for_family(const char *family_id)
{
    unsigned int index;

    for (index = 0u; index < V9X_HW16_TABLE_COUNT; ++index) {
        if (strcmp(v9x_hw16_tables[index].family_id, family_id) == 0) {
            return v9x_hw16_tables[index].ops;
        }
    }
    return 0;
}

/*
 * Every chip's manifest mode list must be the family's C table, row for row
 * and in the same order.
 *
 * Order is part of the contract, not an incidental: GDI enumerates the MODES
 * registry key in the order the INF writes it, which Get-V9xInfModeLines
 * sorts by depth then width then height, and the driver's own list has to
 * agree with that enumeration. The 640x400 Doom95 row sitting after the other
 * 8-bpp rows is the visible consequence.
 */
static void test_mode_tables_match_manifests(void)
{
    unsigned int chip_index;
    unsigned int checked = 0u;

    for (chip_index = 0u; chip_index < V9X_FAMILY_MATRIX_COUNT; ++chip_index) {
        const struct v9x_family_matrix_chip *chip =
            &v9x_family_matrix[chip_index];
        const V9X_HW16_OPS *ops = ops_for_family(chip->family_id);
        unsigned int mode_index;

        if (ops == 0) {
            continue; /* matrox-m2, per the note at the top of this file. */
        }
        ++checked;
        HCHECK(chip->family_id, strcmp(ops->family_id, chip->family_id) == 0);
        HCHECK(chip->family_id, ops->mode_count == chip->mode_count);
        if (ops->mode_count != chip->mode_count) {
            continue;
        }
        for (mode_index = 0u; mode_index < chip->mode_count; ++mode_index) {
            const struct v9x_family_matrix_mode *want =
                &chip->modes[mode_index];
            const V9X_HW16_MODE *got = &ops->modes[mode_index];

            HCHECK(chip->family_id, got->width == want->width);
            HCHECK(chip->family_id, got->height == want->height);
            HCHECK(chip->family_id,
                   got->bits_per_pixel == want->bits_per_pixel);
            HCHECK(chip->family_id, got->vbe_mode == want->vbe_mode);
        }
    }

    /* If the family ids ever stop matching, every loop above goes vacuous and
     * this file would pass while checking nothing. */
    HCHECK("matrix", checked != 0u);
}

/*
 * The pitch is the one field the manifest does not carry, so it is checked
 * against the geometry instead: these are packed linear modes, so a scan line
 * is exactly width * bytes-per-pixel. A pitch that disagrees would put every
 * scan line in the wrong place, and it is a plain hand-typed number in the
 * table.
 */
static void test_mode_pitches_are_packed(void)
{
    unsigned int table_index;

    for (table_index = 0u; table_index < V9X_HW16_TABLE_COUNT; ++table_index) {
        const V9X_HW16_OPS *ops = v9x_hw16_tables[table_index].ops;
        unsigned int mode_index;

        for (mode_index = 0u; mode_index < ops->mode_count; ++mode_index) {
            const V9X_HW16_MODE *mode = &ops->modes[mode_index];
            unsigned long expected =
                (unsigned long)mode->width *
                (unsigned long)(mode->bits_per_pixel / 8u);

            HCHECK(ops->family_id, (mode->bits_per_pixel % 8u) == 0u);
            HCHECK(ops->family_id, (unsigned long)mode->pitch == expected);
            /* pitch is a 16-bit field; a mode whose scan line does not fit in
             * it would be silently truncated. */
            HCHECK(ops->family_id, expected <= 0xffffuL);
        }
    }
}

/*
 * modes[0] is the mode Enable falls back to when the registry names none, and
 * every family's boot path depends on it being the safest one there is.
 */
static void test_first_mode_is_the_fallback(void)
{
    unsigned int table_index;

    for (table_index = 0u; table_index < V9X_HW16_TABLE_COUNT; ++table_index) {
        const V9X_HW16_OPS *ops = v9x_hw16_tables[table_index].ops;

        HCHECK(ops->family_id, ops->mode_count != 0u);
        if (ops->mode_count == 0u) {
            continue;
        }
        HCHECK(ops->family_id, ops->modes[0].width == 640u);
        HCHECK(ops->family_id, ops->modes[0].height == 480u);
        HCHECK(ops->family_id, ops->modes[0].bits_per_pixel == 8u);
    }
}

/*
 * The two ways a family can survive a PCI scan that matched nothing are
 * alternatives, not layers.
 *
 * pci_match_optional says "proceed without knowing which card this is", and a
 * tier-0 family can afford that because it touches no chip register.
 * identify_without_pci says "find out another way", and a family with
 * chip-specific pokes needs that narrower answer instead. Setting both would
 * make the identification pointless - the tolerance would accept the card
 * whatever the hook concluded - which is a quiet way to lose a safety property
 * while appearing to add one.
 *
 * A hook with no devices to match against can never succeed, so that pairing is
 * refused too.
 */
static void test_pci_miss_strategies_are_exclusive(void)
{
    unsigned int table_index;

    for (table_index = 0u; table_index < V9X_HW16_TABLE_COUNT; ++table_index) {
        const V9X_HW16_OPS *ops = v9x_hw16_tables[table_index].ops;

        HCHECK(ops->family_id,
               !(ops->pci_match_optional != 0u &&
                 ops->identify_without_pci != 0));
        if (ops->identify_without_pci != 0) {
            HCHECK(ops->family_id, ops->device_count != 0u);
        }
    }
}

unsigned int v9x_run_hw16_mode_tests(void)
{
    hw16_failures = 0u;
    test_mode_tables_match_manifests();
    test_mode_pitches_are_packed();
    test_first_mode_is_the_fallback();
    test_pci_miss_strategies_are_exclusive();
    return hw16_failures;
}
