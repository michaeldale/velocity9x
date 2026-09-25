/*
 * Tests for the PE32 export-by-ordinal walk.
 *
 * The image is synthetic: a byte array with only the headers the walk reads,
 * laid out at the specification's offsets, so each rule can be broken one at
 * a time - a slot that is a forwarder, an ordinal past the table, a table
 * that runs past the stated image size. The measured shape of KERNEL32 on
 * 98SE (ordinal base 1, 865 functions, ordinals 93/96/97/98 resolving) is the
 * fixture the happy path mirrors.
 */
#include <stdio.h>
#include <string.h>

#include "velocity9x/pe_export.h"

static unsigned int pe_failures = 0u;

#define PECHECK(expression) do { \
    if (!(expression)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #expression); \
        ++pe_failures; \
    } \
} while (0)

/* Large enough that the slots' RVAs (0x1000 + n * 0x10) lie inside it, as
 * every real export's must lie inside SizeOfImage. */
#define PE_IMAGE_BYTES 0x2000ul
#define PE_LFANEW      0x80ul
#define PE_OPTIONAL    (PE_LFANEW + 24ul)
#define PE_EXPORT_DIR  0x200ul
#define PE_EXPORT_SIZE 0x60ul
#define PE_FUNCTIONS   0x240ul
#define PE_BASE        1ul
#define PE_COUNT       100ul

static void pe_put32(v9x_u8 *image, v9x_u32 offset, v9x_u32 value)
{
    image[offset] = (v9x_u8)(value & 0xFFul);
    image[offset + 1ul] = (v9x_u8)((value >> 8) & 0xFFul);
    image[offset + 2ul] = (v9x_u8)((value >> 16) & 0xFFul);
    image[offset + 3ul] = (v9x_u8)((value >> 24) & 0xFFul);
}

/* A well-formed image: ordinals 1..100, slot n at RVA 0x1000 + n * 0x10,
 * except that ordinal 50 is empty and ordinal 60 forwards. */
static void pe_build(v9x_u8 *image)
{
    v9x_u32 n;

    memset(image, 0, PE_IMAGE_BYTES);
    image[0] = 'M';
    image[1] = 'Z';
    pe_put32(image, 0x3Cul, PE_LFANEW);
    pe_put32(image, PE_LFANEW, 0x00004550ul);
    image[PE_OPTIONAL] = 0x0Bu;
    image[PE_OPTIONAL + 1ul] = 0x01u;
    pe_put32(image, PE_OPTIONAL + 56ul, 0x00070000ul);
    pe_put32(image, PE_OPTIONAL + 96ul, PE_EXPORT_DIR);
    pe_put32(image, PE_OPTIONAL + 100ul, PE_EXPORT_SIZE);
    pe_put32(image, PE_EXPORT_DIR + 16ul, PE_BASE);
    pe_put32(image, PE_EXPORT_DIR + 20ul, PE_COUNT);
    pe_put32(image, PE_EXPORT_DIR + 28ul, PE_FUNCTIONS);
    for (n = 0ul; n < PE_COUNT; ++n) {
        pe_put32(image, PE_FUNCTIONS + n * 4ul, 0x1000ul + (n + PE_BASE) * 0x10ul);
    }
    pe_put32(image, PE_FUNCTIONS + (50ul - PE_BASE) * 4ul, 0ul);
    pe_put32(image, PE_FUNCTIONS + (60ul - PE_BASE) * 4ul, PE_EXPORT_DIR + 0x40ul);
}

static void test_resolves_the_measured_ordinals(void)
{
    v9x_u8 image[PE_IMAGE_BYTES];
    v9x_u32 rva = 0ul;
    v9x_u32 size = 0ul;

    pe_build(image);
    PECHECK(v9x_pe_size_of_image(image, 0x200ul, &size) == V9X_PE_EXPORT_OK);
    PECHECK(size == 0x00070000ul);
    PECHECK(v9x_pe_export_by_ordinal(image, PE_IMAGE_BYTES, 93ul, &rva) == V9X_PE_EXPORT_OK);
    PECHECK(rva == 0x1000ul + 93ul * 0x10ul);
    PECHECK(v9x_pe_export_by_ordinal(image, PE_IMAGE_BYTES, 96ul, &rva) == V9X_PE_EXPORT_OK);
    PECHECK(rva == 0x1000ul + 96ul * 0x10ul);
    PECHECK(v9x_pe_export_by_ordinal(image, PE_IMAGE_BYTES, 1ul, &rva) == V9X_PE_EXPORT_OK);
    PECHECK(rva == 0x1010ul);
    PECHECK(v9x_pe_export_by_ordinal(image, PE_IMAGE_BYTES, 100ul, &rva) == V9X_PE_EXPORT_OK);
    PECHECK(rva == 0x1000ul + 100ul * 0x10ul);
}

static void test_locates_the_table(void)
{
    v9x_u8 image[PE_IMAGE_BYTES];
    v9x_u32 directory = 0ul;
    v9x_u32 functions = 0ul;
    v9x_u32 count = 0ul;

    pe_build(image);
    PECHECK(v9x_pe_export_table(image, PE_IMAGE_BYTES, &directory, &functions, &count) == V9X_PE_EXPORT_OK);
    PECHECK(directory == PE_EXPORT_DIR);
    PECHECK(functions == PE_FUNCTIONS);
    PECHECK(count == PE_COUNT);

    /* The directory itself past the stated size, and no directory. */
    PECHECK(v9x_pe_export_table(image, PE_EXPORT_DIR + 8ul, &directory, &functions, &count) == V9X_PE_EXPORT_TRUNCATED);
    pe_put32(image, PE_OPTIONAL + 96ul, 0ul);
    PECHECK(v9x_pe_export_table(image, PE_IMAGE_BYTES, &directory, &functions, &count) == V9X_PE_EXPORT_NO_EXPORTS);
}

static void test_refuses_what_is_not_there(void)
{
    v9x_u8 image[PE_IMAGE_BYTES];
    v9x_u32 rva = 0xDEADul;

    pe_build(image);
    PECHECK(v9x_pe_export_by_ordinal(image, PE_IMAGE_BYTES, 0ul, &rva) == V9X_PE_EXPORT_OUT_OF_RANGE);
    PECHECK(v9x_pe_export_by_ordinal(image, PE_IMAGE_BYTES, 101ul, &rva) == V9X_PE_EXPORT_OUT_OF_RANGE);
    PECHECK(v9x_pe_export_by_ordinal(image, PE_IMAGE_BYTES, 50ul, &rva) == V9X_PE_EXPORT_EMPTY);
    PECHECK(v9x_pe_export_by_ordinal(image, PE_IMAGE_BYTES, 60ul, &rva) == V9X_PE_EXPORT_FORWARDER);
    PECHECK(rva == 0xDEADul);
}

static void test_refuses_a_malformed_image(void)
{
    v9x_u8 image[PE_IMAGE_BYTES];
    v9x_u32 rva = 0xDEADul;
    v9x_u32 size = 0ul;

    /* Too short for e_lfanew. */
    pe_build(image);
    PECHECK(v9x_pe_export_by_ordinal(image, 0x20ul, 93ul, &rva) == V9X_PE_EXPORT_TRUNCATED);
    PECHECK(v9x_pe_size_of_image(image, 0x20ul, &size) == V9X_PE_EXPORT_TRUNCATED);

    /* Headers present but the export table runs past the stated size. */
    pe_build(image);
    PECHECK(v9x_pe_export_by_ordinal(image, PE_FUNCTIONS + 8ul, 93ul, &rva) == V9X_PE_EXPORT_TRUNCATED);

    /* A slot whose RVA lies past the image. */
    pe_build(image);
    pe_put32(image, PE_FUNCTIONS + (7ul - PE_BASE) * 4ul, PE_IMAGE_BYTES + 0x10ul);
    PECHECK(v9x_pe_export_by_ordinal(image, PE_IMAGE_BYTES, 7ul, &rva) == V9X_PE_EXPORT_TRUNCATED);

    /* Not MZ; not PE; not PE32. */
    pe_build(image);
    image[0] = 'X';
    PECHECK(v9x_pe_export_by_ordinal(image, PE_IMAGE_BYTES, 93ul, &rva) == V9X_PE_EXPORT_NOT_PE32);
    pe_build(image);
    pe_put32(image, PE_LFANEW, 0x00004E45ul);
    PECHECK(v9x_pe_export_by_ordinal(image, PE_IMAGE_BYTES, 93ul, &rva) == V9X_PE_EXPORT_NOT_PE32);
    pe_build(image);
    image[PE_OPTIONAL] = 0x0Bu;
    image[PE_OPTIONAL + 1ul] = 0x02u;
    PECHECK(v9x_pe_export_by_ordinal(image, PE_IMAGE_BYTES, 93ul, &rva) == V9X_PE_EXPORT_NOT_PE32);

    /* An e_lfanew that points past the image. */
    pe_build(image);
    pe_put32(image, 0x3Cul, PE_IMAGE_BYTES - 4ul);
    PECHECK(v9x_pe_export_by_ordinal(image, PE_IMAGE_BYTES, 93ul, &rva) == V9X_PE_EXPORT_TRUNCATED);

    /* No export directory at all. */
    pe_build(image);
    pe_put32(image, PE_OPTIONAL + 96ul, 0ul);
    PECHECK(v9x_pe_export_by_ordinal(image, PE_IMAGE_BYTES, 93ul, &rva) == V9X_PE_EXPORT_NO_EXPORTS);

    PECHECK(rva == 0xDEADul);
}

unsigned int v9x_run_pe_export_tests(void)
{
    pe_failures = 0u;
    test_resolves_the_measured_ordinals();
    test_locates_the_table();
    test_refuses_what_is_not_there();
    test_refuses_a_malformed_image();
    if (pe_failures == 0u) {
        puts("PASS: PE32 export-by-ordinal walk");
    }
    return pe_failures;
}
