/*
 * Tests for the PE32 import-slot walk.
 *
 * The image is synthetic, as in test_pe_export.c: only the headers and tables
 * the walk reads, at the specification's offsets. Two imported modules, the
 * target address in one slot of each, so a walk that stops after the first
 * module or counts the wrong slot is caught. Then each rule is broken alone.
 */
#include <stdio.h>
#include <string.h>

#include "velocity9x/pe_import.h"

static unsigned int pi_failures = 0u;

#define PICHECK(expression) do { \
    if (!(expression)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #expression); \
        ++pi_failures; \
    } \
} while (0)

#define PI_IMAGE_BYTES 0x1000ul
#define PI_LFANEW      0x80ul
#define PI_OPTIONAL    (PI_LFANEW + 24ul)
#define PI_DESCRIPTORS 0x300ul
#define PI_THUNKS_A    0x400ul
#define PI_THUNKS_B    0x480ul
#define PI_NAME_A      0x500ul
#define PI_NAME_B      0x510ul
#define PI_TARGET      0xBFF71234ul

static void pi_put32(v9x_u8 *image, v9x_u32 offset, v9x_u32 value)
{
    image[offset] = (v9x_u8)(value & 0xFFul);
    image[offset + 1ul] = (v9x_u8)((value >> 8) & 0xFFul);
    image[offset + 2ul] = (v9x_u8)((value >> 16) & 0xFFul);
    image[offset + 3ul] = (v9x_u8)((value >> 24) & 0xFFul);
}

/* Module A's address table: two other functions, the target, a fourth, end.
 * Module B's: the target, end. Then the zeroed descriptor that ends the list. */
static void pi_build(v9x_u8 *image)
{
    memset(image, 0, PI_IMAGE_BYTES);
    image[0] = 'M';
    image[1] = 'Z';
    pi_put32(image, 0x3Cul, PI_LFANEW);
    pi_put32(image, PI_LFANEW, 0x00004550ul);
    image[PI_OPTIONAL] = 0x0Bu;
    image[PI_OPTIONAL + 1ul] = 0x01u;
    pi_put32(image, PI_OPTIONAL + 56ul, PI_IMAGE_BYTES);
    pi_put32(image, PI_OPTIONAL + 104ul, PI_DESCRIPTORS);
    pi_put32(image, PI_OPTIONAL + 108ul, 3ul * 20ul);

    pi_put32(image, PI_DESCRIPTORS + 12ul, PI_NAME_A);
    pi_put32(image, PI_DESCRIPTORS + 16ul, PI_THUNKS_A);
    pi_put32(image, PI_DESCRIPTORS + 20ul + 12ul, PI_NAME_B);
    pi_put32(image, PI_DESCRIPTORS + 20ul + 16ul, PI_THUNKS_B);

    pi_put32(image, PI_THUNKS_A, 0xBFF70010ul);
    pi_put32(image, PI_THUNKS_A + 4ul, 0xBFF70020ul);
    pi_put32(image, PI_THUNKS_A + 8ul, PI_TARGET);
    pi_put32(image, PI_THUNKS_A + 12ul, 0xBFF70030ul);
    pi_put32(image, PI_THUNKS_B, PI_TARGET);
}

static void test_finds_every_slot(void)
{
    v9x_u8 image[PI_IMAGE_BYTES];
    v9x_u32 slots[4] = { 0ul, 0ul, 0ul, 0ul };
    v9x_u32 found = 0ul;

    pi_build(image);
    PICHECK(v9x_pe_import_slots(image, PI_IMAGE_BYTES, PI_TARGET, slots, 4ul,
                                &found) == V9X_PE_IMPORT_OK);
    PICHECK(found == 2ul);
    PICHECK(slots[0] == PI_THUNKS_A + 8ul);
    PICHECK(slots[1] == PI_THUNKS_B);
    PICHECK(slots[2] == 0ul);

    /* An address nothing imports. */
    PICHECK(v9x_pe_import_slots(image, PI_IMAGE_BYTES, 0xBFF79999ul, slots,
                                4ul, &found) == V9X_PE_IMPORT_OK);
    PICHECK(found == 0ul);
}

static void test_counts_past_capacity(void)
{
    v9x_u8 image[PI_IMAGE_BYTES];
    v9x_u32 slots[2] = { 0ul, 0xDEADul };
    v9x_u32 found = 0ul;

    pi_build(image);
    PICHECK(v9x_pe_import_slots(image, PI_IMAGE_BYTES, PI_TARGET, slots, 1ul,
                                &found) == V9X_PE_IMPORT_OK);
    PICHECK(found == 2ul);
    PICHECK(slots[0] == PI_THUNKS_A + 8ul);
    PICHECK(slots[1] == 0xDEADul);
}

static void test_skips_a_descriptor_without_a_table(void)
{
    v9x_u8 image[PI_IMAGE_BYTES];
    v9x_u32 slots[2] = { 0ul, 0ul };
    v9x_u32 found = 0ul;

    /* Module A names a module and has no address table: not the end of the
     * list, which is the zeroed descriptor, so B is still walked. */
    pi_build(image);
    pi_put32(image, PI_DESCRIPTORS + 16ul, 0ul);
    PICHECK(v9x_pe_import_slots(image, PI_IMAGE_BYTES, PI_TARGET, slots, 2ul,
                                &found) == V9X_PE_IMPORT_OK);
    PICHECK(found == 1ul);
    PICHECK(slots[0] == PI_THUNKS_B);
}

static void test_refuses_a_malformed_image(void)
{
    v9x_u8 image[PI_IMAGE_BYTES];
    v9x_u32 slots[2] = { 0xDEADul, 0xDEADul };
    v9x_u32 found = 0xDEADul;

    /* No import directory. */
    pi_build(image);
    pi_put32(image, PI_OPTIONAL + 104ul, 0ul);
    PICHECK(v9x_pe_import_slots(image, PI_IMAGE_BYTES, PI_TARGET, slots, 2ul,
                                &found) == V9X_PE_IMPORT_NO_IMPORTS);
    PICHECK(found == 0ul);

    /* Not MZ; not PE32; headers past the stated size. */
    pi_build(image);
    image[0] = 'X';
    PICHECK(v9x_pe_import_slots(image, PI_IMAGE_BYTES, PI_TARGET, slots, 2ul,
                                &found) == V9X_PE_IMPORT_NOT_PE32);
    pi_build(image);
    image[PI_OPTIONAL + 1ul] = 0x02u;
    PICHECK(v9x_pe_import_slots(image, PI_IMAGE_BYTES, PI_TARGET, slots, 2ul,
                                &found) == V9X_PE_IMPORT_NOT_PE32);
    pi_build(image);
    PICHECK(v9x_pe_import_slots(image, 0x20ul, PI_TARGET, slots, 2ul,
                                &found) == V9X_PE_IMPORT_TRUNCATED);

    /* The descriptor list runs past the image before its terminator. */
    pi_build(image);
    PICHECK(v9x_pe_import_slots(image, PI_DESCRIPTORS + 30ul, PI_TARGET,
                                slots, 2ul, &found) ==
            V9X_PE_IMPORT_TRUNCATED);

    /* B's address table has no terminator before the image ends: the slot
     * found in A still counts, and the walk says it stopped early. */
    pi_build(image);
    memset(image + PI_THUNKS_B, 0x11, PI_IMAGE_BYTES - PI_THUNKS_B);
    PICHECK(v9x_pe_import_slots(image, PI_IMAGE_BYTES, PI_TARGET, slots, 2ul,
                                &found) == V9X_PE_IMPORT_TRUNCATED);
    PICHECK(found == 1ul);

    /* A descriptor list with no terminator within the module limit. */
    {
        static v9x_u8 big[0x4000ul];
        v9x_u32 index;

        pi_build(big);
        pi_put32(big, PI_OPTIONAL + 104ul, 0x1000ul);
        for (index = 0ul; index < (0x4000ul - 0x1000ul) / 20ul; ++index) {
            pi_put32(big, 0x1000ul + index * 20ul + 12ul, PI_NAME_A);
        }
        PICHECK(v9x_pe_import_slots(big, 0x4000ul, PI_TARGET, slots, 2ul,
                                    &found) == V9X_PE_IMPORT_UNENDED);
    }
}

unsigned int v9x_run_pe_import_tests(void)
{
    pi_failures = 0u;
    test_finds_every_slot();
    test_counts_past_capacity();
    test_skips_a_descriptor_without_a_table();
    test_refuses_a_malformed_image();
    if (pi_failures == 0u) {
        puts("PASS: PE32 import-slot walk");
    }
    return pi_failures;
}
