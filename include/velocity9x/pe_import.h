/*
 * Finding a PE32 image's import slots that hold a given address.
 *
 * The HAL records why a game process ends (src\display32\ddhal_watch.c). A
 * program reaches ExitProcess, TerminateProcess, MessageBoxA and
 * SetUnhandledExceptionFilter through its own import address table, so
 * replacing those slots in the program's image is the one place a DLL loaded
 * after it can see the calls. The Carmageddon II reports of 2026-10-11 are
 * why: the game ended in both renderers with the driver's counters clean, and
 * nothing said whether it quit, crashed or showed a message first
 * (docs\issues\2026-10-10-carmageddon2-exits-after-intro-gma900.md).
 *
 * Slots are matched by the value the loader wrote, not by the imported name:
 * a program whose lookup table is absent (some linkers emit only the address
 * table) still has a slot holding KERNEL32's ExitProcess, and the HAL gets
 * that address from GetProcAddress. The walk is arithmetic over a byte range
 * with a stated length, as in pe_export.h, so it lives here with a host test
 * and every read is checked against image_bytes before it is made.
 */
#ifndef VELOCITY9X_PE_IMPORT_H
#define VELOCITY9X_PE_IMPORT_H

#include "velocity9x/types.h"

#define V9X_PE_IMPORT_OK         ((v9x_u16)0u) /* the walk ran to the end      */
#define V9X_PE_IMPORT_TRUNCATED  ((v9x_u16)1u) /* a table lies past image_bytes */
#define V9X_PE_IMPORT_NOT_PE32   ((v9x_u16)2u) /* no MZ/PE signature or not PE32 */
#define V9X_PE_IMPORT_NO_IMPORTS ((v9x_u16)3u) /* the import directory is absent */
#define V9X_PE_IMPORT_UNENDED    ((v9x_u16)4u) /* a table hit its count limit   */

/* Limits a real table stays far under; a table without its terminator past
 * these is treated as damaged rather than read to the end of the image. */
#define V9X_PE_IMPORT_MODULES_MAX 256ul
#define V9X_PE_IMPORT_THUNKS_MAX  8192ul

/*
 * image points at the first byte of a mapped PE32 image and image_bytes says
 * how many bytes from there may be read. Every import address table slot
 * holding target is counted in *found_out; the RVAs of the first capacity of
 * them are written to slot_rvas. On a status other than OK, *found_out still
 * counts what was found before the walk stopped.
 */
v9x_u16 v9x_pe_import_slots(const v9x_u8 *image,
                            v9x_u32 image_bytes,
                            v9x_u32 target,
                            v9x_u32 *slot_rvas,
                            v9x_u32 capacity,
                            v9x_u32 *found_out);

#endif
