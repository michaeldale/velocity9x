/*
 * SHA-256, written from FIPS 180-4 sections 4.1.2, 4.2.2, 5.1.1, 5.3.3 and
 * 6.2. See include\velocity9x\sha256.h for where it is used and why it calls
 * no C library function.
 */
#include "velocity9x/sha256.h"

/* FIPS 180-4 4.2.2: the first 32 bits of the fractional parts of the cube
 * roots of the first 64 primes. */
static const v9x_u32 v9x_sha256_k[64] = {
    0x428A2F98ul, 0x71374491ul, 0xB5C0FBCFul, 0xE9B5DBA5ul,
    0x3956C25Bul, 0x59F111F1ul, 0x923F82A4ul, 0xAB1C5ED5ul,
    0xD807AA98ul, 0x12835B01ul, 0x243185BEul, 0x550C7DC3ul,
    0x72BE5D74ul, 0x80DEB1FEul, 0x9BDC06A7ul, 0xC19BF174ul,
    0xE49B69C1ul, 0xEFBE4786ul, 0x0FC19DC6ul, 0x240CA1CCul,
    0x2DE92C6Ful, 0x4A7484AAul, 0x5CB0A9DCul, 0x76F988DAul,
    0x983E5152ul, 0xA831C66Dul, 0xB00327C8ul, 0xBF597FC7ul,
    0xC6E00BF3ul, 0xD5A79147ul, 0x06CA6351ul, 0x14292967ul,
    0x27B70A85ul, 0x2E1B2138ul, 0x4D2C6DFCul, 0x53380D13ul,
    0x650A7354ul, 0x766A0ABBul, 0x81C2C92Eul, 0x92722C85ul,
    0xA2BFE8A1ul, 0xA81A664Bul, 0xC24B8B70ul, 0xC76C51A3ul,
    0xD192E819ul, 0xD6990624ul, 0xF40E3585ul, 0x106AA070ul,
    0x19A4C116ul, 0x1E376C08ul, 0x2748774Cul, 0x34B0BCB5ul,
    0x391C0CB3ul, 0x4ED8AA4Aul, 0x5B9CCA4Ful, 0x682E6FF3ul,
    0x748F82EEul, 0x78A5636Ful, 0x84C87814ul, 0x8CC70208ul,
    0x90BEFFFAul, 0xA4506CEBul, 0xBEF9A3F7ul, 0xC67178F2ul
};

/* FIPS 180-4 5.3.3: the first 32 bits of the fractional parts of the square
 * roots of the first 8 primes. */
static const v9x_u32 v9x_sha256_h0[8] = {
    0x6A09E667ul, 0xBB67AE85ul, 0x3C6EF372ul, 0xA54FF53Aul,
    0x510E527Ful, 0x9B05688Cul, 0x1F83D9ABul, 0x5BE0CD19ul
};

static const char v9x_sha256_hex_digits[16] = {
    '0', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
};

/* ROTR^n for 0 < n < 32. The explicit mask keeps the result to 32 bits
 * wherever v9x_u32 is wider than that. */
static v9x_u32 v9x_sha256_rotr(v9x_u32 value, v9x_u16 bits)
{
    return ((value >> bits) | (value << (32u - bits))) & 0xFFFFFFFFul;
}

/* FIPS 180-4 4.1.2, equations 4.2 to 4.7. */
static v9x_u32 v9x_sha256_ch(v9x_u32 x, v9x_u32 y, v9x_u32 z)
{
    return (x & y) ^ (~x & z);
}

static v9x_u32 v9x_sha256_maj(v9x_u32 x, v9x_u32 y, v9x_u32 z)
{
    return (x & y) ^ (x & z) ^ (y & z);
}

static v9x_u32 v9x_sha256_big_sigma0(v9x_u32 x)
{
    return v9x_sha256_rotr(x, 2u) ^ v9x_sha256_rotr(x, 13u) ^
           v9x_sha256_rotr(x, 22u);
}

static v9x_u32 v9x_sha256_big_sigma1(v9x_u32 x)
{
    return v9x_sha256_rotr(x, 6u) ^ v9x_sha256_rotr(x, 11u) ^
           v9x_sha256_rotr(x, 25u);
}

static v9x_u32 v9x_sha256_small_sigma0(v9x_u32 x)
{
    return v9x_sha256_rotr(x, 7u) ^ v9x_sha256_rotr(x, 18u) ^ (x >> 3);
}

static v9x_u32 v9x_sha256_small_sigma1(v9x_u32 x)
{
    return v9x_sha256_rotr(x, 17u) ^ v9x_sha256_rotr(x, 19u) ^ (x >> 10);
}

/* FIPS 180-4 6.2.2: process one 64-byte block, big-endian words (3.1). */
static void v9x_sha256_block(v9x_u32 state[8], const v9x_u8 block[64])
{
    v9x_u32 w[64];
    v9x_u32 a;
    v9x_u32 b;
    v9x_u32 c;
    v9x_u32 d;
    v9x_u32 e;
    v9x_u32 f;
    v9x_u32 g;
    v9x_u32 h;
    v9x_u32 t1;
    v9x_u32 t2;
    v9x_u16 i;

    /* Step 1: the message schedule. */
    for (i = 0u; i < 16u; ++i) {
        w[i] = ((v9x_u32)block[4u * i] << 24) |
               ((v9x_u32)block[4u * i + 1u] << 16) |
               ((v9x_u32)block[4u * i + 2u] << 8) |
               (v9x_u32)block[4u * i + 3u];
    }
    for (i = 16u; i < 64u; ++i) {
        w[i] = (v9x_sha256_small_sigma1(w[i - 2u]) + w[i - 7u] +
                v9x_sha256_small_sigma0(w[i - 15u]) + w[i - 16u]) &
               0xFFFFFFFFul;
    }

    /* Steps 2 and 3: the 64 rounds. */
    a = state[0];
    b = state[1];
    c = state[2];
    d = state[3];
    e = state[4];
    f = state[5];
    g = state[6];
    h = state[7];
    for (i = 0u; i < 64u; ++i) {
        t1 = (h + v9x_sha256_big_sigma1(e) + v9x_sha256_ch(e, f, g) +
              v9x_sha256_k[i] + w[i]) & 0xFFFFFFFFul;
        t2 = (v9x_sha256_big_sigma0(a) + v9x_sha256_maj(a, b, c)) &
             0xFFFFFFFFul;
        h = g;
        g = f;
        f = e;
        e = (d + t1) & 0xFFFFFFFFul;
        d = c;
        c = b;
        b = a;
        a = (t1 + t2) & 0xFFFFFFFFul;
    }

    /* Step 4: the intermediate hash value. */
    state[0] = (state[0] + a) & 0xFFFFFFFFul;
    state[1] = (state[1] + b) & 0xFFFFFFFFul;
    state[2] = (state[2] + c) & 0xFFFFFFFFul;
    state[3] = (state[3] + d) & 0xFFFFFFFFul;
    state[4] = (state[4] + e) & 0xFFFFFFFFul;
    state[5] = (state[5] + f) & 0xFFFFFFFFul;
    state[6] = (state[6] + g) & 0xFFFFFFFFul;
    state[7] = (state[7] + h) & 0xFFFFFFFFul;
}

void v9x_sha256_init(struct v9x_sha256 *ctx)
{
    v9x_u16 i;

    for (i = 0u; i < 8u; ++i) {
        ctx->state[i] = v9x_sha256_h0[i];
    }
    ctx->count_lo = 0ul;
    ctx->count_hi = 0ul;
    ctx->buffered = 0u;
}

void v9x_sha256_update(struct v9x_sha256 *ctx,
                       const void *data,
                       v9x_u32 length)
{
    const v9x_u8 *bytes;
    v9x_u32 i;

    bytes = (const v9x_u8 *)data;

    /* The byte count is 64 bits wide in two halves; a low half that wraps
     * carries one into the high half. */
    ctx->count_lo = (ctx->count_lo + length) & 0xFFFFFFFFul;
    if (ctx->count_lo < length) {
        ctx->count_hi = (ctx->count_hi + 1ul) & 0xFFFFFFFFul;
    }

    /* Byte at a time into the block buffer. Not the fastest shape, but the
     * one with no partial-block arithmetic to get wrong, and the updater's
     * cost is the download, not this loop. */
    for (i = 0ul; i < length; ++i) {
        ctx->buffer[ctx->buffered] = bytes[i];
        ++ctx->buffered;
        if (ctx->buffered == V9X_SHA256_BLOCK_BYTES) {
            v9x_sha256_block(ctx->state, ctx->buffer);
            ctx->buffered = 0u;
        }
    }
}

void v9x_sha256_final(struct v9x_sha256 *ctx, v9x_u8 digest[32])
{
    v9x_u32 bits_hi;
    v9x_u32 bits_lo;
    v9x_u16 i;

    /* FIPS 180-4 5.1.1: the message length in bits, a 64-bit big-endian
     * integer. Bytes to bits is a 3-bit shift across the two halves. */
    bits_hi = ((ctx->count_hi << 3) | (ctx->count_lo >> 29)) & 0xFFFFFFFFul;
    bits_lo = (ctx->count_lo << 3) & 0xFFFFFFFFul;

    /* A single 1 bit, then zeros until 8 bytes short of a block boundary -
     * spilling into a further block when fewer than 8 bytes remain. */
    ctx->buffer[ctx->buffered] = 0x80u;
    ++ctx->buffered;
    if (ctx->buffered > 56u) {
        while (ctx->buffered < V9X_SHA256_BLOCK_BYTES) {
            ctx->buffer[ctx->buffered] = 0u;
            ++ctx->buffered;
        }
        v9x_sha256_block(ctx->state, ctx->buffer);
        ctx->buffered = 0u;
    }
    while (ctx->buffered < 56u) {
        ctx->buffer[ctx->buffered] = 0u;
        ++ctx->buffered;
    }
    for (i = 0u; i < 4u; ++i) {
        ctx->buffer[56u + i] = (v9x_u8)((bits_hi >> (24u - 8u * i)) & 0xFFu);
        ctx->buffer[60u + i] = (v9x_u8)((bits_lo >> (24u - 8u * i)) & 0xFFu);
    }
    v9x_sha256_block(ctx->state, ctx->buffer);
    ctx->buffered = 0u;

    /* The digest is H_0..H_7, each big-endian (FIPS 180-4 6.2.2). */
    for (i = 0u; i < 32u; ++i) {
        digest[i] = (v9x_u8)((ctx->state[i / 4u] >> (24u - 8u * (i % 4u))) &
                             0xFFu);
    }
}

void v9x_sha256_hex(const v9x_u8 digest[32], char out[65])
{
    v9x_u16 i;

    for (i = 0u; i < 32u; ++i) {
        out[2u * i] = v9x_sha256_hex_digits[(digest[i] >> 4) & 0x0Fu];
        out[2u * i + 1u] = v9x_sha256_hex_digits[digest[i] & 0x0Fu];
    }
    out[64] = '\0';
}
