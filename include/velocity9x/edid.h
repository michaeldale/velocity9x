/*
 * EDID base-block parsing.
 *
 * Split from the collection for the reason vbe_parse.c is: the 4F15h call is
 * mechanism and lives in the mini-VDD, but whether 128 bytes are a usable
 * EDID and what geometry the panel prefers is judgement, and judgement is
 * worth testing on the host against the malformed blocks real monitors and
 * real BIOSes produce. Nothing here does I/O, allocates, or touches the OS.
 */
#ifndef VELOCITY9X_EDID_H
#define VELOCITY9X_EDID_H

#include "velocity9x/types.h"

#define V9X_EDID_BLOCK_BYTES ((v9x_u16)128u)

struct v9x_edid_summary {
    v9x_u16 version;          /* (version << 8) | revision, e.g. 0x0104 */
    v9x_u16 preferred_width;  /* the first detailed timing's active pixels */
    v9x_u16 preferred_height;
    v9x_u16 extension_count;  /* advertised, for diagnostics; never fetched */
};

/*
 * V9X_TRUE when the block is a usable EDID base block, with the summary
 * filled; V9X_FALSE, summary zeroed, otherwise. Usable requires:
 *
 *   - the fixed 00 FF FF FF FF FF FF 00 header;
 *   - the byte checksum: all 128 bytes sum to zero mod 256;
 *   - structure version 1.x - version 2 blocks have a different layout and
 *     refusing them beats misreading them;
 *   - the first detailed-timing descriptor is a timing (nonzero pixel
 *     clock), which the 1.3+ feature bit and every panel this project has
 *     evidence of make the preferred mode;
 *   - nonzero active geometry from it, and not interlaced: no runtime table
 *     row can describe an interlaced mode, so a preferred timing this driver
 *     cannot match is reported as no preference rather than half a one.
 */
v9x_u16 v9x_edid_parse(const v9x_u8 *block, struct v9x_edid_summary *out);

#endif /* VELOCITY9X_EDID_H */
