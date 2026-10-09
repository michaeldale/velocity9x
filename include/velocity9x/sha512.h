/*
 * SHA-512, as specified by FIPS 180-4 section 6.4.
 *
 * Its user is Ed25519 (RFC 8032 5.1 fixes SHA-512 as the hash H), not the
 * updater directly; the API is shaped like include\velocity9x\sha256.h so the
 * two read the same.
 *
 * SHA-512 is defined on 64-bit words and this tree has no 64-bit type - the
 * 16-bit and 32-bit Open Watcom models share these headers, and neither
 * `long long` nor `__int64` is allowed. Every word is therefore a hi/lo pair
 * of v9x_u32 inside src\common\sha512.c, with addition carrying by hand and
 * rotations split across the halves. Same constraints as SHA-256 otherwise:
 * pure, streaming, and no C library call.
 */
#ifndef VELOCITY9X_SHA512_H
#define VELOCITY9X_SHA512_H

#include "velocity9x/types.h"

#define V9X_SHA512_DIGEST_BYTES ((v9x_u16)64u)
#define V9X_SHA512_BLOCK_BYTES  ((v9x_u16)128u)

/*
 * The running state.
 *
 * state[2i] is the high half of FIPS 180-4's H_i and state[2i + 1] the low
 * half. count_hi:count_lo is the byte count absorbed so far; the 128-bit bit
 * count the padding needs (FIPS 180-4 5.1.2) is derived from it, its top 61
 * bits always zero because a 64-bit byte count cannot reach them.
 */
struct v9x_sha512 {
    v9x_u32 state[16];
    v9x_u32 count_lo;
    v9x_u32 count_hi;
    v9x_u16 buffered;
    v9x_u8 buffer[128];
};

/* Start a new hash with the FIPS 180-4 5.3.5 initial value. */
void v9x_sha512_init(struct v9x_sha512 *ctx);

/* Absorb length bytes. Any number of calls of any size, including zero. */
void v9x_sha512_update(struct v9x_sha512 *ctx,
                       const void *data,
                       v9x_u32 length);

/* Pad, finish, and write the 64-byte digest. The context is spent. */
void v9x_sha512_final(struct v9x_sha512 *ctx, v9x_u8 digest[64]);

/* The digest as 128 lowercase hex characters and a terminating NUL. */
void v9x_sha512_hex(const v9x_u8 digest[64], char out[129]);

#endif /* VELOCITY9X_SHA512_H */
