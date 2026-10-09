/*
 * Zip reading over one in-memory buffer, from PKWARE's APPNOTE.TXT. See
 * include\velocity9x\zipread.h for scope and promises.
 *
 * Every helper that reads a field is handed an offset already proven to have
 * the field's bytes behind it; the proofs are the subtractions in front of
 * each read, written as "remaining < needed" so that no sum can wrap.
 */
#include "velocity9x/zipread.h"
#include "velocity9x/crc32.h"

/* APPNOTE.TXT 4.3.16: end of central directory record. 22 fixed bytes and a
 * comment of up to 65535, so the record starts in the last 65557 bytes. */
#define V9X_ZIP_END_SIGNATURE   ((v9x_u32)0x06054B50ul)
#define V9X_ZIP_END_FIXED       ((v9x_u32)22ul)
#define V9X_ZIP_END_SEARCH      ((v9x_u32)65557ul)

/* APPNOTE.TXT 4.3.12: central directory file header, 46 fixed bytes. */
#define V9X_ZIP_CENTRAL_SIGNATURE ((v9x_u32)0x02014B50ul)
#define V9X_ZIP_CENTRAL_FIXED     ((v9x_u32)46ul)

/* APPNOTE.TXT 4.3.7: local file header, 30 fixed bytes. */
#define V9X_ZIP_LOCAL_SIGNATURE ((v9x_u32)0x04034B50ul)
#define V9X_ZIP_LOCAL_FIXED     ((v9x_u32)30ul)

/* APPNOTE.TXT 4.4.4: general purpose flags that mean the data is encrypted -
 * bit 0 traditional/strong, bit 6 strong, bit 13 masked local header. */
#define V9X_ZIP_FLAGS_ENCRYPTED ((v9x_u16)0x2041u)

/* APPNOTE.TXT 4.4.5: compression methods accepted. */
#define V9X_ZIP_METHOD_STORED  ((v9x_u16)0u)
#define V9X_ZIP_METHOD_DEFLATE ((v9x_u16)8u)

/* APPNOTE.TXT 4.4.1.4 / 4.5.3: a field at its maximum means "see zip64". */
#define V9X_ZIP_U16_SATURATED ((v9x_u16)0xFFFFu)
#define V9X_ZIP_U32_SATURATED ((v9x_u32)0xFFFFFFFFul)

static v9x_u16 v9x_zip_read16(const v9x_u8 *bytes)
{
    return (v9x_u16)((v9x_u16)bytes[0] | ((v9x_u16)bytes[1] << 8));
}

static v9x_u32 v9x_zip_read32(const v9x_u8 *bytes)
{
    return (v9x_u32)bytes[0] | ((v9x_u32)bytes[1] << 8) |
           ((v9x_u32)bytes[2] << 16) | ((v9x_u32)bytes[3] << 24);
}

static v9x_u8 v9x_zip_ascii_upper(v9x_u8 c)
{
    if (c >= (v9x_u8)'a' && c <= (v9x_u8)'z') {
        return (v9x_u8)(c - ((v9x_u8)'a' - (v9x_u8)'A'));
    }
    return c;
}

static v9x_u16 v9x_zip_same_name(const v9x_u8 *a,
                                 const v9x_u8 *b,
                                 v9x_u16 length)
{
    v9x_u16 i;

    for (i = 0u; i < length; ++i) {
        if (v9x_zip_ascii_upper(a[i]) != v9x_zip_ascii_upper(b[i])) {
            return V9X_FALSE;
        }
    }
    return V9X_TRUE;
}

/*
 * Decode the central directory record at offset, which must end at or before
 * end. *next is where the following record starts.
 */
static v9x_u16 v9x_zip_read_central(const struct v9x_zip *zip,
                                    v9x_u32 offset,
                                    v9x_u32 end,
                                    v9x_u16 index,
                                    struct v9x_zip_entry *entry,
                                    v9x_u32 *next)
{
    const v9x_u8 *record;
    v9x_u32 record_length;

    if (offset > end || end - offset < V9X_ZIP_CENTRAL_FIXED) {
        return V9X_ZIP_BAD_CENTRAL;
    }
    record = zip->data + offset;
    if (v9x_zip_read32(record) != V9X_ZIP_CENTRAL_SIGNATURE) {
        return V9X_ZIP_BAD_CENTRAL;
    }

    /* Fixed part plus name, extra field and comment: at most 46 + 3 * 65535,
     * so the sum cannot wrap. */
    record_length = V9X_ZIP_CENTRAL_FIXED +
                    (v9x_u32)v9x_zip_read16(record + 28) +
                    (v9x_u32)v9x_zip_read16(record + 30) +
                    (v9x_u32)v9x_zip_read16(record + 32);
    if (end - offset < record_length) {
        return V9X_ZIP_BAD_CENTRAL;
    }

    entry->index = index;
    entry->flags = v9x_zip_read16(record + 8);
    entry->method = v9x_zip_read16(record + 10);
    entry->crc32 = v9x_zip_read32(record + 16);
    entry->compressed_size = v9x_zip_read32(record + 20);
    entry->uncompressed_size = v9x_zip_read32(record + 24);
    entry->name_length = v9x_zip_read16(record + 28);
    entry->name = record + V9X_ZIP_CENTRAL_FIXED;
    entry->local_offset = v9x_zip_read32(record + 42);

    if (v9x_zip_read16(record + 34) != 0u) {
        return V9X_ZIP_MULTI_DISK;
    }
    if (entry->compressed_size == V9X_ZIP_U32_SATURATED ||
        entry->uncompressed_size == V9X_ZIP_U32_SATURATED ||
        entry->local_offset == V9X_ZIP_U32_SATURATED) {
        return V9X_ZIP_ZIP64;
    }

    *next = offset + record_length;
    return V9X_ZIP_OK;
}

/*
 * Walk the central directory to entry index. With index equal to the entry
 * count this checks the whole directory instead: every record must decode
 * and the last must end exactly where the directory does.
 */
static v9x_u16 v9x_zip_walk(const struct v9x_zip *zip,
                            v9x_u16 index,
                            struct v9x_zip_entry *entry)
{
    v9x_u32 offset;
    v9x_u32 end;
    v9x_u32 next;
    v9x_u16 i;
    v9x_u16 status;

    offset = zip->directory_offset;
    end = zip->directory_offset + zip->directory_size;
    for (i = 0u; i < zip->entry_count; ++i) {
        status = v9x_zip_read_central(zip, offset, end, i, entry, &next);
        if (status != V9X_ZIP_OK) {
            return status;
        }
        if (i == index) {
            return V9X_ZIP_OK;
        }
        offset = next;
    }
    if (offset != end) {
        return V9X_ZIP_BAD_CENTRAL;
    }
    return V9X_ZIP_OK;
}

v9x_u16 v9x_zip_open(struct v9x_zip *zip, const v9x_u8 *data, v9x_u32 length)
{
    struct v9x_zip_entry scratch;
    const v9x_u8 *end_record;
    v9x_u32 position;
    v9x_u32 floor;
    v9x_u32 directory_offset;
    v9x_u32 directory_size;
    v9x_u16 entries_here;
    v9x_u16 entries_total;
    v9x_u16 found;

    if (zip == 0 || data == 0) {
        return V9X_ZIP_BAD_ARGUMENT;
    }
    zip->data = data;
    zip->length = length;
    zip->directory_offset = 0ul;
    zip->directory_size = 0ul;
    zip->entry_count = 0u;
    if (length < V9X_ZIP_END_FIXED) {
        return V9X_ZIP_NO_END;
    }

    /*
     * Search backwards for the signature. A candidate counts only if its
     * comment length runs exactly to the end of the buffer: the signature's
     * four bytes can occur by chance inside a comment, but a chance match
     * that also describes the bytes after it correctly is far less likely,
     * and trailing junk after a real record is not something .NET writes.
     */
    floor = 0ul;
    if (length > V9X_ZIP_END_SEARCH) {
        floor = length - V9X_ZIP_END_SEARCH;
    }
    position = length - V9X_ZIP_END_FIXED;
    found = V9X_FALSE;
    for (;;) {
        end_record = data + position;
        if (v9x_zip_read32(end_record) == V9X_ZIP_END_SIGNATURE &&
            (v9x_u32)v9x_zip_read16(end_record + 20) ==
                length - position - V9X_ZIP_END_FIXED) {
            found = V9X_TRUE;
            break;
        }
        if (position == floor) {
            break;
        }
        --position;
    }
    if (found != V9X_TRUE) {
        return V9X_ZIP_NO_END;
    }

    /* Disk numbers zero and the per-disk count equal to the total, or this
     * is one piece of a spanned set. */
    entries_here = v9x_zip_read16(end_record + 8);
    entries_total = v9x_zip_read16(end_record + 10);
    directory_size = v9x_zip_read32(end_record + 12);
    directory_offset = v9x_zip_read32(end_record + 16);
    if (entries_total == V9X_ZIP_U16_SATURATED ||
        entries_here == V9X_ZIP_U16_SATURATED ||
        directory_size == V9X_ZIP_U32_SATURATED ||
        directory_offset == V9X_ZIP_U32_SATURATED) {
        return V9X_ZIP_ZIP64;
    }
    if (v9x_zip_read16(end_record + 4) != 0u ||
        v9x_zip_read16(end_record + 6) != 0u ||
        entries_here != entries_total) {
        return V9X_ZIP_MULTI_DISK;
    }

    /* The directory lies wholly before the end record. */
    if (directory_offset > position ||
        position - directory_offset < directory_size) {
        return V9X_ZIP_BAD_END;
    }

    zip->directory_offset = directory_offset;
    zip->directory_size = directory_size;
    zip->entry_count = entries_total;
    return v9x_zip_walk(zip, entries_total, &scratch);
}

v9x_u16 v9x_zip_entry_at(const struct v9x_zip *zip,
                         v9x_u16 index,
                         struct v9x_zip_entry *entry)
{
    if (zip == 0 || entry == 0 || zip->data == 0) {
        return V9X_ZIP_BAD_ARGUMENT;
    }
    if (index >= zip->entry_count) {
        return V9X_ZIP_BAD_INDEX;
    }
    return v9x_zip_walk(zip, index, entry);
}

v9x_u16 v9x_zip_find(const struct v9x_zip *zip,
                     const char *name,
                     struct v9x_zip_entry *entry)
{
    struct v9x_zip_entry candidate;
    const v9x_u8 *wanted;
    v9x_u16 length;
    v9x_u16 index;
    v9x_u16 matches;
    v9x_u16 status;

    if (zip == 0 || name == 0 || entry == 0 || zip->data == 0) {
        return V9X_ZIP_BAD_ARGUMENT;
    }

    /* Measure the name, but stop one past the limit: a longer string is
     * refused without reading the rest of it. */
    wanted = (const v9x_u8 *)name;
    length = 0u;
    while (length <= V9X_ZIP_NAME_MAX_8_3 && wanted[length] != 0u) {
        ++length;
    }
    if (v9x_zip_name_is_safe(wanted, length, V9X_ZIP_NAME_MAX_8_3) !=
        V9X_TRUE) {
        return V9X_ZIP_UNSAFE_NAME;
    }

    matches = 0u;
    for (index = 0u; index < zip->entry_count; ++index) {
        status = v9x_zip_walk(zip, index, &candidate);
        if (status != V9X_ZIP_OK) {
            return status;
        }
        if (candidate.name_length != length ||
            v9x_zip_same_name(candidate.name, wanted, length) != V9X_TRUE) {
            continue;
        }
        ++matches;
        if (matches > 1u) {
            return V9X_ZIP_DUPLICATE;
        }
        *entry = candidate;
    }
    if (matches == 0u) {
        return V9X_ZIP_NOT_FOUND;
    }
    return V9X_ZIP_OK;
}

v9x_u16 v9x_zip_extract(const struct v9x_zip *zip,
                        v9x_u16 index,
                        struct v9x_inflate_state *state,
                        v9x_u8 *output,
                        v9x_u32 output_capacity,
                        v9x_u32 *output_length)
{
    struct v9x_zip_entry entry;
    const v9x_u8 *local;
    v9x_u32 local_room;
    v9x_u32 header_length;
    v9x_u32 data_offset;
    v9x_u32 i;
    v9x_u16 status;

    if (output_length == 0) {
        return V9X_ZIP_BAD_ARGUMENT;
    }
    *output_length = 0ul;
    if (zip == 0 || state == 0 || (output == 0 && output_capacity != 0ul)) {
        return V9X_ZIP_BAD_ARGUMENT;
    }
    status = v9x_zip_entry_at(zip, index, &entry);
    if (status != V9X_ZIP_OK) {
        return status;
    }

    if ((entry.flags & V9X_ZIP_FLAGS_ENCRYPTED) != 0u) {
        return V9X_ZIP_ENCRYPTED;
    }
    if (entry.method != V9X_ZIP_METHOD_STORED &&
        entry.method != V9X_ZIP_METHOD_DEFLATE) {
        return V9X_ZIP_BAD_METHOD;
    }
    if (output_capacity < entry.uncompressed_size) {
        return V9X_ZIP_OUTPUT_FULL;
    }

    /*
     * The local header, its name and extra field, and the data must all lie
     * before the central directory. The data's position comes from the local
     * header's own name and extra lengths (APPNOTE.TXT 4.3.7): the extra
     * field is allowed to differ from the central copy, and often does.
     */
    if (entry.local_offset > zip->directory_offset) {
        return V9X_ZIP_BAD_LOCAL;
    }
    local_room = zip->directory_offset - entry.local_offset;
    if (local_room < V9X_ZIP_LOCAL_FIXED) {
        return V9X_ZIP_BAD_LOCAL;
    }
    local = zip->data + entry.local_offset;
    if (v9x_zip_read32(local) != V9X_ZIP_LOCAL_SIGNATURE) {
        return V9X_ZIP_BAD_LOCAL;
    }
    header_length = V9X_ZIP_LOCAL_FIXED + (v9x_u32)v9x_zip_read16(local + 26) +
                    (v9x_u32)v9x_zip_read16(local + 28);
    if (local_room < header_length) {
        return V9X_ZIP_BAD_LOCAL;
    }
    if ((v9x_zip_read16(local + 6) & V9X_ZIP_FLAGS_ENCRYPTED) != 0u) {
        return V9X_ZIP_ENCRYPTED;
    }
    if (v9x_zip_read16(local + 8) != entry.method) {
        return V9X_ZIP_BAD_LOCAL;
    }

    /* Byte-exact, not case-folded: the two headers describe one member, and
     * a name that differs between them is a sign of a crafted archive. */
    if (v9x_zip_read16(local + 26) != entry.name_length) {
        return V9X_ZIP_NAME_MISMATCH;
    }
    for (i = 0ul; i < (v9x_u32)entry.name_length; ++i) {
        if (local[V9X_ZIP_LOCAL_FIXED + i] != entry.name[i]) {
            return V9X_ZIP_NAME_MISMATCH;
        }
    }

    if (local_room - header_length < entry.compressed_size) {
        return V9X_ZIP_BAD_LOCAL;
    }
    data_offset = entry.local_offset + header_length;

    if (entry.method == V9X_ZIP_METHOD_STORED) {
        if (entry.compressed_size != entry.uncompressed_size) {
            return V9X_ZIP_BAD_SIZE;
        }
        for (i = 0ul; i < entry.compressed_size; ++i) {
            output[i] = zip->data[data_offset + i];
        }
        *output_length = entry.compressed_size;
    } else {
        /* Capacity is limited to the declared size, so a stream that
         * decodes to more stops at it instead of filling the caller's
         * spare room. */
        status = v9x_inflate_with_state(state, zip->data + data_offset,
                                        entry.compressed_size, output,
                                        entry.uncompressed_size,
                                        output_length);
        if (status == V9X_INFLATE_OUTPUT_FULL) {
            return V9X_ZIP_BAD_SIZE;
        }
        if (status != V9X_INFLATE_OK) {
            return V9X_ZIP_BAD_DATA;
        }
        if (*output_length != entry.uncompressed_size) {
            return V9X_ZIP_BAD_SIZE;
        }
    }

    if (v9x_crc32(output, *output_length) != entry.crc32) {
        return V9X_ZIP_BAD_CRC;
    }
    return V9X_ZIP_OK;
}

v9x_u16 v9x_zip_extract_named(const struct v9x_zip *zip,
                              const char *name,
                              struct v9x_inflate_state *state,
                              v9x_u8 *output,
                              v9x_u32 output_capacity,
                              v9x_u32 *output_length)
{
    struct v9x_zip_entry entry;
    v9x_u16 status;

    if (output_length == 0) {
        return V9X_ZIP_BAD_ARGUMENT;
    }
    *output_length = 0ul;
    status = v9x_zip_find(zip, name, &entry);
    if (status != V9X_ZIP_OK) {
        return status;
    }
    return v9x_zip_extract(zip, entry.index, state, output, output_capacity,
                           output_length);
}

/*
 * The device names Windows 9x resolves in any directory and with any
 * extension. Upper case; compared against the part of the name before the
 * first dot.
 */
static v9x_u16 v9x_zip_is_device(const v9x_u8 *name, v9x_u16 length)
{
    static const char *const fixed[] = { "CON", "PRN", "AUX", "NUL", "CLOCK$" };
    const char *device;
    v9x_u16 stem;
    v9x_u16 i;
    v9x_u16 k;
    v9x_u8 c;

    stem = 0u;
    while (stem < length && name[stem] != (v9x_u8)'.') {
        ++stem;
    }

    for (i = 0u; i < (v9x_u16)(sizeof(fixed) / sizeof(fixed[0])); ++i) {
        device = fixed[i];
        k = 0u;
        while (k < stem && device[k] != '\0' &&
               v9x_zip_ascii_upper(name[k]) == (v9x_u8)device[k]) {
            ++k;
        }
        if (k == stem && device[k] == '\0') {
            return V9X_TRUE;
        }
    }

    /* COM1-COM9, LPT1-LPT9. */
    if (stem != 4u) {
        return V9X_FALSE;
    }
    c = name[3];
    if (c < (v9x_u8)'1' || c > (v9x_u8)'9') {
        return V9X_FALSE;
    }
    if (v9x_zip_same_name(name, (const v9x_u8 *)"COM", 3u) == V9X_TRUE ||
        v9x_zip_same_name(name, (const v9x_u8 *)"LPT", 3u) == V9X_TRUE) {
        return V9X_TRUE;
    }
    return V9X_FALSE;
}

v9x_u16 v9x_zip_name_is_safe(const v9x_u8 *name,
                             v9x_u16 length,
                             v9x_u16 max_length)
{
    v9x_u16 i;
    v9x_u8 c;

    if (name == 0 || length == 0u || length > max_length) {
        return V9X_FALSE;
    }

    for (i = 0u; i < length; ++i) {
        c = name[i];
        if (c <= (v9x_u8)' ' || c > (v9x_u8)'~') {
            return V9X_FALSE;
        }
        if (c == (v9x_u8)'/' || c == (v9x_u8)'\\' || c == (v9x_u8)':' ||
            c == (v9x_u8)'*' || c == (v9x_u8)'?' || c == (v9x_u8)'"' ||
            c == (v9x_u8)'<' || c == (v9x_u8)'>' || c == (v9x_u8)'|') {
            return V9X_FALSE;
        }
        if (c == (v9x_u8)'.' && i + 1u < length &&
            name[i + 1u] == (v9x_u8)'.') {
            return V9X_FALSE;
        }
    }

    /* Windows strips trailing dots, so "A.TXT." would name "A.TXT", and a
     * lone "." names the directory itself. */
    if (name[length - 1u] == (v9x_u8)'.') {
        return V9X_FALSE;
    }
    if (v9x_zip_is_device(name, length) == V9X_TRUE) {
        return V9X_FALSE;
    }
    return V9X_TRUE;
}
