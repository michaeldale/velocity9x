/*
 * EDID base-block parsing.
 *
 * The negative corpus is the point: monitors and BIOSes hand back blocks
 * with bad checksums, version-2 layouts, descriptor-first slots and
 * interlaced preferred timings, and every one of those must read as "no
 * preference" rather than as a geometry to act on.
 */
#include <stdio.h>
#include <string.h>

#include "velocity9x/edid.h"

static unsigned int edid_failures = 0u;

#define EDIDCHECK(expression) do { \
    if (!(expression)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, \
               #expression); \
        ++edid_failures; \
    } \
} while (0)

/* A minimal valid 1.4 block whose preferred timing is width x height. */
static void make_block(v9x_u8 *block, v9x_u16 width, v9x_u16 height)
{
    static const v9x_u8 header[8] = {
        0x00u, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0x00u
    };
    v9x_u8 *dtd = block + 54;
    unsigned int index;
    unsigned int sum;

    memset(block, 0, 128);
    memcpy(block, header, 8);
    block[18] = 1u;  /* version */
    block[19] = 4u;  /* revision */
    dtd[0] = 0x28u;  /* a plausible nonzero pixel clock */
    dtd[1] = 0x3cu;
    dtd[2] = (v9x_u8)(width & 0xffu);
    dtd[4] = (v9x_u8)((width >> 4) & 0xf0u);
    dtd[5] = (v9x_u8)(height & 0xffu);
    dtd[7] = (v9x_u8)((height >> 4) & 0xf0u);
    block[126] = 0u; /* extensions */

    sum = 0u;
    for (index = 0u; index < 127u; ++index) {
        sum += block[index];
    }
    block[127] = (v9x_u8)(0x100u - (sum & 0xffu));
}

static void test_edid_accepts_a_valid_block(void)
{
    v9x_u8 block[128];
    struct v9x_edid_summary summary;

    /* The QEMU monitor's preferred timing, from the DOS capture. */
    make_block(block, 1280u, 800u);
    EDIDCHECK(v9x_edid_parse(block, &summary) == V9X_TRUE);
    EDIDCHECK(summary.preferred_width == 1280u);
    EDIDCHECK(summary.preferred_height == 800u);
    EDIDCHECK(summary.version == 0x0104u);
    EDIDCHECK(summary.extension_count == 0u);

    /* Wide geometry survives the split upper nibbles. */
    make_block(block, 1920u, 1200u);
    EDIDCHECK(v9x_edid_parse(block, &summary) == V9X_TRUE);
    EDIDCHECK(summary.preferred_width == 1920u);
    EDIDCHECK(summary.preferred_height == 1200u);
}

static void test_edid_refuses_the_malformed(void)
{
    v9x_u8 block[128];
    struct v9x_edid_summary summary;

    EDIDCHECK(v9x_edid_parse(0, &summary) == V9X_FALSE);
    EDIDCHECK(v9x_edid_parse((const v9x_u8 *)"", 0) == V9X_FALSE);

    /* A zeroed block - what a lying BIOS that answers 004Fh without writing
     * produces - fails on the header. */
    memset(block, 0, sizeof(block));
    EDIDCHECK(v9x_edid_parse(block, &summary) == V9X_FALSE);
    EDIDCHECK(summary.preferred_width == 0u);

    /* One flipped byte breaks the checksum. */
    make_block(block, 1280u, 800u);
    block[40] ^= 0x01u;
    EDIDCHECK(v9x_edid_parse(block, &summary) == V9X_FALSE);

    /* Version 2 has a different layout; refusing beats misreading. */
    make_block(block, 1280u, 800u);
    block[18] = 2u;
    block[127] = (v9x_u8)(block[127] - 1u); /* keep the checksum true */
    EDIDCHECK(v9x_edid_parse(block, &summary) == V9X_FALSE);

    /* First slot holds a display descriptor, not a timing. */
    make_block(block, 1280u, 800u);
    block[54] = 0u;
    block[55] = 0u;
    block[127] = (v9x_u8)(block[127] + 0x28u + 0x3cu);
    EDIDCHECK(v9x_edid_parse(block, &summary) == V9X_FALSE);

    /* An interlaced preferred timing is one no runtime row can describe. */
    make_block(block, 1280u, 800u);
    block[71] = 0x80u;
    block[127] = (v9x_u8)(block[127] - 0x80u);
    EDIDCHECK(v9x_edid_parse(block, &summary) == V9X_FALSE);

    /* Zero geometry in an otherwise-true block. */
    make_block(block, 0u, 800u);
    EDIDCHECK(v9x_edid_parse(block, &summary) == V9X_FALSE);
}

unsigned int v9x_run_edid_tests(void)
{
    edid_failures = 0u;
    test_edid_accepts_a_valid_block();
    test_edid_refuses_the_malformed();
    return edid_failures;
}
