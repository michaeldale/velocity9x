/*
 * Resolving a PE32 export by ordinal from the image's own bytes.
 *
 * KERNEL32 exports the Win16 mutex functions by ordinal only, and Windows 98
 * SE's GetProcAddress refuses an ordinal into KERNEL32 with
 * ERROR_NOT_SUPPORTED - measured 2026-09-26 on two guests
 * (docs\decisions\2026-09-26-98se-confirmwin16lock-is-one-when-held-and-getprocaddress-refuses-the-ordinals.md).
 * DirectDraw links those ordinals through its import table, which a DLL the
 * runtime loads cannot do without a matching import library. What is left is
 * the walk every such caller has done since Schulman documented it: read the
 * export directory of the mapped image and index AddressOfFunctions by
 * ordinal minus base.
 *
 * The walk is arithmetic over a byte range with a stated length, so it lives
 * here with no OS header and a host test, and the HAL supplies the range: the
 * module base from GetModuleHandleA and SizeOfImage from the optional header,
 * which the loader guarantees is mapped. Every offset is checked against that
 * length before it is read, so a malformed image answers with a status rather
 * than a fault - in a shared-arena DLL a fault is a wedge with the Win16 mutex
 * possibly held, which is the failure this whole exercise exists to avoid.
 */
#ifndef VELOCITY9X_PE_EXPORT_H
#define VELOCITY9X_PE_EXPORT_H

#include "velocity9x/types.h"

#define V9X_PE_EXPORT_OK           ((v9x_u16)0u) /* rva_out is the function's RVA  */
#define V9X_PE_EXPORT_TRUNCATED    ((v9x_u16)1u) /* a header lies past image_bytes */
#define V9X_PE_EXPORT_NOT_PE32     ((v9x_u16)2u) /* no MZ/PE signature or not PE32 */
#define V9X_PE_EXPORT_NO_EXPORTS   ((v9x_u16)3u) /* the export directory is absent  */
#define V9X_PE_EXPORT_OUT_OF_RANGE ((v9x_u16)4u) /* ordinal outside [base, base+n)  */
#define V9X_PE_EXPORT_FORWARDER    ((v9x_u16)5u) /* the slot names another module   */
#define V9X_PE_EXPORT_EMPTY        ((v9x_u16)6u) /* the slot is zero: never exported */

/*
 * image points at the first byte of a mapped PE32 image (the MZ header) and
 * image_bytes says how many bytes from there may be read. On success rva_out
 * is the export's RVA; the caller adds it to image. On any other status
 * rva_out is untouched.
 */
v9x_u16 v9x_pe_export_by_ordinal(const v9x_u8 *image,
                                 v9x_u32 image_bytes,
                                 v9x_u32 ordinal,
                                 v9x_u32 *rva_out);

/*
 * SizeOfImage from the optional header, or 0 with the same statuses as above
 * for an image whose headers do not fit in header_bytes. The HAL calls this
 * with the first page only, then calls the resolver with the answer.
 */
v9x_u16 v9x_pe_size_of_image(const v9x_u8 *image,
                             v9x_u32 header_bytes,
                             v9x_u32 *size_out);

/*
 * Where the export directory and its AddressOfFunctions table lie, so a
 * caller that must prove readability before reading (the HAL, with
 * IsBadReadPtr) can test exactly those two ranges and nothing else. Testing
 * the whole of SizeOfImage is wrong: 98SE's KERNEL32 has a page inside its
 * image that IsBadReadPtr refuses, measured 2026-09-26 when the first HAL
 * build resolved nothing for that reason. The directory is validated against
 * image_bytes here; the function table is validated in the resolver.
 */
v9x_u16 v9x_pe_export_table(const v9x_u8 *image,
                            v9x_u32 image_bytes,
                            v9x_u32 *directory_rva_out,
                            v9x_u32 *functions_rva_out,
                            v9x_u32 *function_count_out);

#endif
