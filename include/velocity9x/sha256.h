/*
 * SHA-256, as specified by FIPS 180-4 section 6.2.
 *
 * The updater (V9XUPD.EXE) hashes every downloaded zip with this and compares
 * the digest against the signed release file, so the module is the second
 * link in the chain whose first is include\velocity9x\ed25519.h.
 *
 * Pure computation: no I/O, no OS, and no C library call - the module is
 * linked into runtime-free Win32 tools (-zl, no default libraries), so even
 * memcpy is a loop here. Streaming, so a file can be fed in whatever pieces
 * it is read in; the total length is counted in 64 bits as two 32-bit
 * halves because no 64-bit type is available to this tree.
 */
#ifndef VELOCITY9X_SHA256_H
#define VELOCITY9X_SHA256_H

#include "velocity9x/types.h"

#define V9X_SHA256_DIGEST_BYTES ((v9x_u16)32u)
#define V9X_SHA256_BLOCK_BYTES  ((v9x_u16)64u)

/*
 * The running state.
 *
 * buffer holds the bytes of a block that is not yet complete; buffered is
 * how many. count_hi:count_lo is the number of message bytes absorbed so
 * far, which FIPS 180-4 5.1.1 needs (as a bit count) in the final padding.
 */
struct v9x_sha256 {
    v9x_u32 state[8];
    v9x_u32 count_lo;
    v9x_u32 count_hi;
    v9x_u16 buffered;
    v9x_u8 buffer[64];
};

/* Start a new hash with the FIPS 180-4 5.3.3 initial value. */
void v9x_sha256_init(struct v9x_sha256 *ctx);

/* Absorb length bytes. Any number of calls of any size, including zero. */
void v9x_sha256_update(struct v9x_sha256 *ctx,
                       const void *data,
                       v9x_u32 length);

/* Pad, finish, and write the 32-byte digest. The context is spent; call
 * v9x_sha256_init again before reusing it. */
void v9x_sha256_final(struct v9x_sha256 *ctx, v9x_u8 digest[32]);

/* The digest as 64 lowercase hex characters and a terminating NUL - the form
 * the release file carries. */
void v9x_sha256_hex(const v9x_u8 digest[32], char out[65]);

#endif /* VELOCITY9X_SHA256_H */
