/*
 * PE32 export-by-ordinal resolution over a bounded byte range. See the
 * header for why this exists and why every read is bounds-checked first.
 *
 * Offsets are those of the PE/COFF specification: e_lfanew at 0x3C; the
 * optional header 24 bytes past the PE signature; SizeOfImage at optional
 * header offset 56; the export data directory at offset 96 (RVA) and 100
 * (size); and in the export directory, Base at 16, NumberOfFunctions at 20
 * and AddressOfFunctions at 28. The image is mapped as loaded, so RVAs are
 * offsets from image and no section walk is needed.
 */
#include "velocity9x/pe_export.h"

#define V9X_PE_OFFSET_E_LFANEW      0x3Cul
#define V9X_PE_SIGNATURE            0x00004550ul /* "PE\0\0" */
#define V9X_PE_OPTIONAL_OFFSET      24ul
#define V9X_PE_MAGIC_PE32           0x010Bu
#define V9X_PE_OPT_SIZE_OF_IMAGE    56ul
#define V9X_PE_OPT_EXPORT_RVA       96ul
#define V9X_PE_OPT_EXPORT_SIZE      100ul
#define V9X_PE_OPT_MIN_BYTES        104ul
#define V9X_PE_EXPORT_BASE          16ul
#define V9X_PE_EXPORT_COUNT         20ul
#define V9X_PE_EXPORT_FUNCTIONS     28ul
#define V9X_PE_EXPORT_DIR_BYTES     40ul

static v9x_u32 v9x_pe_read32(const v9x_u8 *image, v9x_u32 offset)
{
    return (v9x_u32)image[offset] |
           ((v9x_u32)image[offset + 1ul] << 8) |
           ((v9x_u32)image[offset + 2ul] << 16) |
           ((v9x_u32)image[offset + 3ul] << 24);
}

static v9x_u16 v9x_pe_read16(const v9x_u8 *image, v9x_u32 offset)
{
    return (v9x_u16)((v9x_u16)image[offset] |
                     ((v9x_u16)image[offset + 1ul] << 8));
}

/* True when [offset, offset + bytes) lies inside [0, limit), overflow-safe. */
static int v9x_pe_fits(v9x_u32 offset, v9x_u32 bytes, v9x_u32 limit)
{
    if (offset > limit) {
        return 0;
    }
    return bytes <= limit - offset;
}

/* Locates the optional header; the same three checks serve both entries. */
static v9x_u16 v9x_pe_optional(const v9x_u8 *image, v9x_u32 image_bytes,
                               v9x_u32 *optional_out)
{
    v9x_u32 pe;

    if (!v9x_pe_fits(V9X_PE_OFFSET_E_LFANEW, 4ul, image_bytes)) {
        return V9X_PE_EXPORT_TRUNCATED;
    }
    if (image[0] != 'M' || image[1] != 'Z') {
        return V9X_PE_EXPORT_NOT_PE32;
    }
    pe = v9x_pe_read32(image, V9X_PE_OFFSET_E_LFANEW);
    if (!v9x_pe_fits(pe, V9X_PE_OPTIONAL_OFFSET + V9X_PE_OPT_MIN_BYTES,
                     image_bytes)) {
        return V9X_PE_EXPORT_TRUNCATED;
    }
    if (v9x_pe_read32(image, pe) != V9X_PE_SIGNATURE) {
        return V9X_PE_EXPORT_NOT_PE32;
    }
    if (v9x_pe_read16(image, pe + V9X_PE_OPTIONAL_OFFSET) != V9X_PE_MAGIC_PE32) {
        return V9X_PE_EXPORT_NOT_PE32;
    }
    *optional_out = pe + V9X_PE_OPTIONAL_OFFSET;
    return V9X_PE_EXPORT_OK;
}

v9x_u16 v9x_pe_size_of_image(const v9x_u8 *image,
                             v9x_u32 header_bytes,
                             v9x_u32 *size_out)
{
    v9x_u32 optional;
    v9x_u16 status = v9x_pe_optional(image, header_bytes, &optional);

    if (status != V9X_PE_EXPORT_OK) {
        return status;
    }
    *size_out = v9x_pe_read32(image, optional + V9X_PE_OPT_SIZE_OF_IMAGE);
    return V9X_PE_EXPORT_OK;
}

v9x_u16 v9x_pe_export_table(const v9x_u8 *image,
                            v9x_u32 image_bytes,
                            v9x_u32 *directory_rva_out,
                            v9x_u32 *functions_rva_out,
                            v9x_u32 *function_count_out)
{
    v9x_u32 optional;
    v9x_u32 export_rva;
    v9x_u16 status = v9x_pe_optional(image, image_bytes, &optional);

    if (status != V9X_PE_EXPORT_OK) {
        return status;
    }
    export_rva = v9x_pe_read32(image, optional + V9X_PE_OPT_EXPORT_RVA);
    if (export_rva == 0ul) {
        return V9X_PE_EXPORT_NO_EXPORTS;
    }
    if (!v9x_pe_fits(export_rva, V9X_PE_EXPORT_DIR_BYTES, image_bytes)) {
        return V9X_PE_EXPORT_TRUNCATED;
    }
    *directory_rva_out = export_rva;
    *functions_rva_out = v9x_pe_read32(image, export_rva + V9X_PE_EXPORT_FUNCTIONS);
    *function_count_out = v9x_pe_read32(image, export_rva + V9X_PE_EXPORT_COUNT);
    return V9X_PE_EXPORT_OK;
}

v9x_u16 v9x_pe_export_by_ordinal(const v9x_u8 *image,
                                 v9x_u32 image_bytes,
                                 v9x_u32 ordinal,
                                 v9x_u32 *rva_out)
{
    v9x_u32 optional;
    v9x_u32 export_rva;
    v9x_u32 export_size;
    v9x_u32 base;
    v9x_u32 count;
    v9x_u32 functions;
    v9x_u32 index;
    v9x_u32 rva;
    v9x_u16 status = v9x_pe_optional(image, image_bytes, &optional);

    if (status != V9X_PE_EXPORT_OK) {
        return status;
    }
    export_rva = v9x_pe_read32(image, optional + V9X_PE_OPT_EXPORT_RVA);
    export_size = v9x_pe_read32(image, optional + V9X_PE_OPT_EXPORT_SIZE);
    if (export_rva == 0ul) {
        return V9X_PE_EXPORT_NO_EXPORTS;
    }
    if (!v9x_pe_fits(export_rva, V9X_PE_EXPORT_DIR_BYTES, image_bytes)) {
        return V9X_PE_EXPORT_TRUNCATED;
    }
    base = v9x_pe_read32(image, export_rva + V9X_PE_EXPORT_BASE);
    count = v9x_pe_read32(image, export_rva + V9X_PE_EXPORT_COUNT);
    functions = v9x_pe_read32(image, export_rva + V9X_PE_EXPORT_FUNCTIONS);
    if (ordinal < base) {
        return V9X_PE_EXPORT_OUT_OF_RANGE;
    }
    index = ordinal - base;
    if (index >= count) {
        return V9X_PE_EXPORT_OUT_OF_RANGE;
    }
    /* index * 4 cannot overflow here: index < count and the table of
     * count entries must itself fit, which the next check establishes. */
    if (!v9x_pe_fits(functions, count * 4ul, image_bytes) ||
        count > 0x3FFFFFFFul) {
        return V9X_PE_EXPORT_TRUNCATED;
    }
    rva = v9x_pe_read32(image, functions + index * 4ul);
    if (rva == 0ul) {
        return V9X_PE_EXPORT_EMPTY;
    }
    /* A function RVA inside the export directory is a forwarder string. */
    if (rva >= export_rva && rva < export_rva + export_size) {
        return V9X_PE_EXPORT_FORWARDER;
    }
    if (rva >= image_bytes) {
        return V9X_PE_EXPORT_TRUNCATED;
    }
    *rva_out = rva;
    return V9X_PE_EXPORT_OK;
}
