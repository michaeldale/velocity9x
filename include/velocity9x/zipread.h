/*
 * Reading a zip archive held whole in memory, for the updater.
 *
 * V9XUPD.EXE downloads a release zip (about 350 KB, written by .NET's
 * ZipArchive - see New-V9xReleaseZip in scripts\build-release.ps1), checks
 * its SHA-256 against the published sum, then extracts the files it needs.
 * The SHA-256 makes an attacker-crafted archive unlikely, not impossible -
 * a sum file can be replaced as easily as a zip - so this code assumes
 * nothing about the bytes: every offset and length is validated against the
 * buffer before use, and anything unexpected is refused with a reason.
 *
 * Deliberately narrow, from PKWARE's APPNOTE.TXT: one disk, no zip64, no
 * encryption, methods 0 (stored) and 8 (deflate) only. Sizes and CRC come
 * from the central directory, so a member written with a data descriptor
 * (general purpose flag bit 3) reads like any other. Pure: no I/O, no
 * allocation, no C runtime.
 */
#ifndef VELOCITY9X_ZIPREAD_H
#define VELOCITY9X_ZIPREAD_H

#include "velocity9x/types.h"
#include "velocity9x/inflate.h"

#define V9X_ZIP_OK            ((v9x_u16)0u)
#define V9X_ZIP_BAD_ARGUMENT  ((v9x_u16)1u)  /* null pointer passed */
#define V9X_ZIP_NO_END        ((v9x_u16)2u)  /* no end of central directory record */
#define V9X_ZIP_BAD_END       ((v9x_u16)3u)  /* EOCD fields point outside the archive */
#define V9X_ZIP_MULTI_DISK    ((v9x_u16)4u)  /* a spanned or split archive */
#define V9X_ZIP_ZIP64         ((v9x_u16)5u)  /* a zip64 marker value */
#define V9X_ZIP_BAD_CENTRAL   ((v9x_u16)6u)  /* a central directory record is malformed */
#define V9X_ZIP_BAD_INDEX     ((v9x_u16)7u)  /* index past the last entry */
#define V9X_ZIP_NOT_FOUND     ((v9x_u16)8u)  /* no entry of that name */
#define V9X_ZIP_DUPLICATE     ((v9x_u16)9u)  /* more than one entry of that name */
#define V9X_ZIP_UNSAFE_NAME   ((v9x_u16)10u) /* requested name fails v9x_zip_name_is_safe */
#define V9X_ZIP_ENCRYPTED     ((v9x_u16)11u) /* flag bit 0, 6 or 13 */
#define V9X_ZIP_BAD_METHOD    ((v9x_u16)12u) /* neither stored nor deflate */
#define V9X_ZIP_BAD_LOCAL     ((v9x_u16)13u) /* local header malformed or disagrees */
#define V9X_ZIP_NAME_MISMATCH ((v9x_u16)14u) /* local and central names differ */
#define V9X_ZIP_OUTPUT_FULL   ((v9x_u16)15u) /* capacity below the declared size */
#define V9X_ZIP_BAD_DATA      ((v9x_u16)16u) /* the deflate stream is malformed */
#define V9X_ZIP_BAD_SIZE      ((v9x_u16)17u) /* data is not the declared size */
#define V9X_ZIP_BAD_CRC       ((v9x_u16)18u) /* data does not match the declared CRC */

/*
 * Name length limits for v9x_zip_name_is_safe. The 8.3 limit is what
 * v9x_zip_find and v9x_zip_extract_named apply: the updater writes into a
 * Windows 9x directory and WININIT.INI renames take short names. The long
 * limit is for host tools only.
 */
#define V9X_ZIP_NAME_MAX_8_3  ((v9x_u16)12u)
#define V9X_ZIP_NAME_MAX_LONG ((v9x_u16)255u)

/* An opened archive. Fill with v9x_zip_open; callers only read it. */
struct v9x_zip {
    const v9x_u8 *data;
    v9x_u32 length;
    v9x_u32 directory_offset;
    v9x_u32 directory_size;
    v9x_u16 entry_count;
};

/*
 * One central directory entry. name points into the archive buffer and is
 * NOT terminated; name_length bytes of it are the stored name, unchecked
 * for safety until someone asks.
 */
struct v9x_zip_entry {
    v9x_u16 index;
    v9x_u16 flags;
    v9x_u16 method;
    v9x_u16 name_length;
    const v9x_u8 *name;
    v9x_u32 crc32;
    v9x_u32 compressed_size;
    v9x_u32 uncompressed_size;
    v9x_u32 local_offset;
};

/*
 * Find the end of central directory record and check the whole central
 * directory: every record well-formed and inside it, exactly entry_count of
 * them, nothing left over. An archive that opens can then be walked without
 * surprises; extraction checks each member's own bytes again.
 */
v9x_u16 v9x_zip_open(struct v9x_zip *zip, const v9x_u8 *data, v9x_u32 length);

/* Entry index (0 .. entry_count - 1), in central directory order. */
v9x_u16 v9x_zip_entry_at(const struct v9x_zip *zip,
                         v9x_u16 index,
                         struct v9x_zip_entry *entry);

/*
 * The one entry whose name equals name, ASCII case-insensitively. name is a
 * NUL-terminated string that must itself pass v9x_zip_name_is_safe with the
 * 8.3 limit. Two matching entries are refused rather than one picked.
 */
v9x_u16 v9x_zip_find(const struct v9x_zip *zip,
                     const char *name,
                     struct v9x_zip_entry *entry);

/*
 * Extract entry index into output. The capacity must hold the declared
 * uncompressed size; *output_length is the number of bytes written (zero on
 * any failure before decoding began). On success the bytes are exactly the
 * declared size and match the declared CRC-32. state is scratch for the
 * deflate decoder, about 1.5 KB, supplied so this works on a small stack.
 */
v9x_u16 v9x_zip_extract(const struct v9x_zip *zip,
                        v9x_u16 index,
                        struct v9x_inflate_state *state,
                        v9x_u8 *output,
                        v9x_u32 output_capacity,
                        v9x_u32 *output_length);

/* v9x_zip_find, then v9x_zip_extract. */
v9x_u16 v9x_zip_extract_named(const struct v9x_zip *zip,
                              const char *name,
                              struct v9x_inflate_state *state,
                              v9x_u8 *output,
                              v9x_u32 output_capacity,
                              v9x_u32 *output_length);

/*
 * Whether a name may become a file in the target directory.
 *
 * Refused: empty or longer than max_length; any '/', '\', ':' (no paths, no
 * drive letters, no NTFS streams); any "..", a lone "." or a trailing '.'
 * (Windows drops trailing dots, so "A.TXT." would land on "A.TXT"); space,
 * control characters and bytes above 0x7E; the wildcard and redirection
 * characters * ? " < > |; and the DOS device names CON, PRN, AUX, NUL,
 * CLOCK$, COM1-COM9 and LPT1-LPT9 with or without an extension, which on
 * Windows 9x open the device instead of a file.
 *
 * It does not enforce the 8.3 shape (eight, dot, three) - only the length.
 */
v9x_u16 v9x_zip_name_is_safe(const v9x_u8 *name,
                             v9x_u16 length,
                             v9x_u16 max_length);

#endif /* VELOCITY9X_ZIPREAD_H */
