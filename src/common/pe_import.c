/*
 * PE32 import address table slots holding one address, over a bounded byte
 * range. See the header for why this exists.
 *
 * Offsets are those of the PE/COFF specification: e_lfanew at 0x3C; the
 * optional header 24 bytes past the PE signature; the import data directory
 * at optional header offset 104 (RVA). An import descriptor is 20 bytes, with
 * Name at 12 and FirstThunk (the address table) at 16; the list ends at a
 * descriptor whose Name and FirstThunk are both zero, and each address table
 * ends at a zero slot. The image is mapped as loaded, so RVAs are offsets
 * from image and no section walk is needed.
 */
#include "velocity9x/pe_import.h"

#define V9X_PE_OFFSET_E_LFANEW   0x3Cul
#define V9X_PE_SIGNATURE         0x00004550ul /* "PE\0\0" */
#define V9X_PE_OPTIONAL_OFFSET   24ul
#define V9X_PE_MAGIC_PE32        0x010Bu
#define V9X_PE_OPT_IMPORT_RVA    104ul
#define V9X_PE_OPT_IMPORT_BYTES  112ul
#define V9X_PE_DESCRIPTOR_BYTES  20ul
#define V9X_PE_DESCRIPTOR_NAME   12ul
#define V9X_PE_DESCRIPTOR_THUNKS 16ul
#define V9X_PE_THUNK_BYTES       4ul

static v9x_u32 v9x_pe_import_read32(const v9x_u8 *image, v9x_u32 offset)
{
    return (v9x_u32)image[offset] |
           ((v9x_u32)image[offset + 1ul] << 8) |
           ((v9x_u32)image[offset + 2ul] << 16) |
           ((v9x_u32)image[offset + 3ul] << 24);
}

/* True when [offset, offset + bytes) lies inside [0, limit), overflow-safe. */
static int v9x_pe_import_fits(v9x_u32 offset, v9x_u32 bytes, v9x_u32 limit)
{
    if (offset > limit) {
        return 0;
    }
    return bytes <= limit - offset;
}

/* One module's address table: count the slots holding target, until the
 * zero slot that ends it. */
static v9x_u16 v9x_pe_import_table(const v9x_u8 *image, v9x_u32 image_bytes,
                                   v9x_u32 thunks, v9x_u32 target,
                                   v9x_u32 *slot_rvas, v9x_u32 capacity,
                                   v9x_u32 *found)
{
    v9x_u32 index;
    v9x_u32 slot;
    v9x_u32 value;

    for (index = 0ul; index < V9X_PE_IMPORT_THUNKS_MAX; ++index) {
        slot = thunks + index * V9X_PE_THUNK_BYTES;
        if (slot < thunks ||
            !v9x_pe_import_fits(slot, V9X_PE_THUNK_BYTES, image_bytes)) {
            return V9X_PE_IMPORT_TRUNCATED;
        }
        value = v9x_pe_import_read32(image, slot);
        if (value == 0ul) {
            return V9X_PE_IMPORT_OK;
        }
        if (value != target) {
            continue;
        }
        if (*found < capacity) {
            slot_rvas[*found] = slot;
        }
        ++*found;
    }
    return V9X_PE_IMPORT_UNENDED;
}

v9x_u16 v9x_pe_import_slots(const v9x_u8 *image,
                            v9x_u32 image_bytes,
                            v9x_u32 target,
                            v9x_u32 *slot_rvas,
                            v9x_u32 capacity,
                            v9x_u32 *found_out)
{
    v9x_u32 pe;
    v9x_u32 optional;
    v9x_u32 descriptors;
    v9x_u32 module;
    v9x_u32 descriptor;
    v9x_u32 name;
    v9x_u32 thunks;
    v9x_u16 status;

    *found_out = 0ul;
    if (!v9x_pe_import_fits(V9X_PE_OFFSET_E_LFANEW, 4ul, image_bytes)) {
        return V9X_PE_IMPORT_TRUNCATED;
    }
    if (image[0] != 'M' || image[1] != 'Z') {
        return V9X_PE_IMPORT_NOT_PE32;
    }
    pe = v9x_pe_import_read32(image, V9X_PE_OFFSET_E_LFANEW);
    if (!v9x_pe_import_fits(pe, V9X_PE_OPTIONAL_OFFSET +
                                V9X_PE_OPT_IMPORT_BYTES, image_bytes)) {
        return V9X_PE_IMPORT_TRUNCATED;
    }
    optional = pe + V9X_PE_OPTIONAL_OFFSET;
    if (v9x_pe_import_read32(image, pe) != V9X_PE_SIGNATURE ||
        (v9x_u16)(image[optional] | (image[optional + 1ul] << 8)) !=
            V9X_PE_MAGIC_PE32) {
        return V9X_PE_IMPORT_NOT_PE32;
    }
    descriptors = v9x_pe_import_read32(image, optional + V9X_PE_OPT_IMPORT_RVA);
    if (descriptors == 0ul) {
        return V9X_PE_IMPORT_NO_IMPORTS;
    }

    for (module = 0ul; module < V9X_PE_IMPORT_MODULES_MAX; ++module) {
        descriptor = descriptors + module * V9X_PE_DESCRIPTOR_BYTES;
        if (descriptor < descriptors ||
            !v9x_pe_import_fits(descriptor, V9X_PE_DESCRIPTOR_BYTES,
                                image_bytes)) {
            return V9X_PE_IMPORT_TRUNCATED;
        }
        name = v9x_pe_import_read32(image, descriptor + V9X_PE_DESCRIPTOR_NAME);
        thunks = v9x_pe_import_read32(image,
                                      descriptor + V9X_PE_DESCRIPTOR_THUNKS);
        if (name == 0ul && thunks == 0ul) {
            return V9X_PE_IMPORT_OK;
        }
        /* A descriptor with no address table imports nothing; the list goes
         * on to the zeroed one. */
        if (thunks == 0ul) {
            continue;
        }
        status = v9x_pe_import_table(image, image_bytes, thunks, target,
                                     slot_rvas, capacity, found_out);
        if (status != V9X_PE_IMPORT_OK) {
            return status;
        }
    }
    return V9X_PE_IMPORT_UNENDED;
}
