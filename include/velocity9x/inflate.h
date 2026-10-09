/*
 * A raw Deflate decoder (RFC 1951), for the updater's release zips.
 *
 * The updater, V9XUPD.EXE, holds a downloaded release zip in one buffer and
 * extracts named files from it. The zips are written by .NET's ZipArchive at
 * CompressionLevel.Optimal, so every non-empty member is a Deflate stream.
 * This is the half of that work that turns one such stream into bytes.
 *
 * It is pure: no I/O, no allocation, no C runtime, no static mutable state -
 * the modules in src\common are linked into runtime-free Win32 tools. It is
 * written for robustness rather than speed. Every input read is checked
 * against input_length and every output write against output_capacity; a
 * malformed stream ends in a status code, never in a read or write outside
 * the two buffers, and never in a loop that consumes no input. Speed is not a
 * goal: one release zip is a few hundred kilobytes, decoded once.
 *
 * Only the raw format is accepted. There is no zlib (RFC 1950) or gzip
 * (RFC 1952) wrapper to strip; zip method 8 stores the raw stream.
 */
#ifndef VELOCITY9X_INFLATE_H
#define VELOCITY9X_INFLATE_H

#include "velocity9x/types.h"

/*
 * Why decoding stopped. Distinct codes rather than a boolean because a field
 * report of a failed update needs to say whether the download was cut short
 * (TRUNCATED) or the bytes themselves are wrong (everything else).
 */
#define V9X_INFLATE_OK           ((v9x_u16)0u)
#define V9X_INFLATE_TRUNCATED    ((v9x_u16)1u)  /* input ended inside the stream */
#define V9X_INFLATE_BAD_BLOCK    ((v9x_u16)2u)  /* BTYPE 11, or HLIT/HCLEN out of range */
#define V9X_INFLATE_BAD_STORED   ((v9x_u16)3u)  /* stored block LEN != ~NLEN */
#define V9X_INFLATE_BAD_LENGTHS  ((v9x_u16)4u)  /* code length set over-subscribed, incomplete or malformed */
#define V9X_INFLATE_BAD_CODE     ((v9x_u16)5u)  /* bit pattern not in the code, or symbol 286/287 */
#define V9X_INFLATE_BAD_DISTANCE ((v9x_u16)6u)  /* distance symbol 30/31, or before start of output */
#define V9X_INFLATE_OUTPUT_FULL  ((v9x_u16)7u)  /* stream decodes to more than output_capacity */
#define V9X_INFLATE_BAD_ARGUMENT ((v9x_u16)8u)  /* null pointer passed */

/*
 * Code sizes from RFC 1951 3.2.5-3.2.7. 288 literal/length symbols because
 * the fixed code assigns lengths to 286 and 287 (they complete the code) even
 * though neither may appear in data; 32 distance symbols for the same reason
 * with 30 and 31. 19 is the code length alphabet. 15 is the longest code.
 */
#define V9X_INFLATE_MAX_BITS        ((v9x_u16)15u)
#define V9X_INFLATE_LITLEN_SYMBOLS  ((v9x_u16)288u)
#define V9X_INFLATE_DIST_SYMBOLS    ((v9x_u16)32u)
#define V9X_INFLATE_CLEN_SYMBOLS    ((v9x_u16)19u)

/*
 * One canonical Huffman code, as RFC 1951 3.2.2 defines it: how many codes
 * there are of each length, and the symbols in code order. That pair is the
 * whole code - the codes themselves follow from the counts - so nothing
 * larger needs building, and decoding walks the lengths shortest first.
 */
struct v9x_inflate_code {
    v9x_u16 count[V9X_INFLATE_MAX_BITS + 1];
    v9x_u16 symbol[V9X_INFLATE_LITLEN_SYMBOLS];
};

/*
 * The whole decoder state: about 1.5 KB. It is public so a caller with a
 * small stack can supply it from elsewhere; v9x_inflate() simply puts one on
 * its own stack. Callers do not initialise or read it.
 */
struct v9x_inflate_state {
    const v9x_u8 *input;
    v9x_u32 input_length;
    v9x_u32 input_position;
    v9x_u32 bit_buffer;
    v9x_u16 bit_count;

    v9x_u8 *output;
    v9x_u32 output_capacity;
    v9x_u32 output_position;

    struct v9x_inflate_code litlen;
    struct v9x_inflate_code distance;
    /* Code lengths for one dynamic block's literal/length and distance
     * alphabets, decoded as the single sequence RFC 1951 3.2.7 describes. */
    v9x_u8 lengths[V9X_INFLATE_LITLEN_SYMBOLS + V9X_INFLATE_DIST_SYMBOLS];
};

/*
 * Decode the raw Deflate stream input[0 .. input_length) into
 * output[0 .. output_capacity).
 *
 * *output_length is always written: on success the decoded size, on failure
 * how many bytes had been written when decoding stopped (those bytes are not
 * to be trusted). Bytes after the final block's end are ignored, as a zip
 * member may legitimately be padded.
 *
 * An empty input is TRUNCATED: the shortest valid stream is two bytes.
 */
v9x_u16 v9x_inflate(const v9x_u8 *input,
                    v9x_u32 input_length,
                    v9x_u8 *output,
                    v9x_u32 output_capacity,
                    v9x_u32 *output_length);

/* The same, with the decoder state supplied by the caller. */
v9x_u16 v9x_inflate_with_state(struct v9x_inflate_state *state,
                               const v9x_u8 *input,
                               v9x_u32 input_length,
                               v9x_u8 *output,
                               v9x_u32 output_capacity,
                               v9x_u32 *output_length);

#endif /* VELOCITY9X_INFLATE_H */
