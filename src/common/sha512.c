/*
 * SHA-512, written from FIPS 180-4 sections 4.1.3, 4.2.3, 5.1.2, 5.3.5 and
 * 6.4, on 64-bit words held as hi/lo pairs of v9x_u32. See
 * include\velocity9x\sha512.h for why there is no native 64-bit word and no
 * C library call.
 */
#include "velocity9x/sha512.h"

/* One FIPS 180-4 64-bit word. */
struct v9x_sha512_word {
    v9x_u32 hi;
    v9x_u32 lo;
};

/* FIPS 180-4 4.2.3: the first 64 bits of the fractional parts of the cube
 * roots of the first 80 primes, as hi, lo pairs. */
static const v9x_u32 v9x_sha512_k[160] = {
    0x428A2F98ul, 0xD728AE22ul, 0x71374491ul, 0x23EF65CDul,
    0xB5C0FBCFul, 0xEC4D3B2Ful, 0xE9B5DBA5ul, 0x8189DBBCul,
    0x3956C25Bul, 0xF348B538ul, 0x59F111F1ul, 0xB605D019ul,
    0x923F82A4ul, 0xAF194F9Bul, 0xAB1C5ED5ul, 0xDA6D8118ul,
    0xD807AA98ul, 0xA3030242ul, 0x12835B01ul, 0x45706FBEul,
    0x243185BEul, 0x4EE4B28Cul, 0x550C7DC3ul, 0xD5FFB4E2ul,
    0x72BE5D74ul, 0xF27B896Ful, 0x80DEB1FEul, 0x3B1696B1ul,
    0x9BDC06A7ul, 0x25C71235ul, 0xC19BF174ul, 0xCF692694ul,
    0xE49B69C1ul, 0x9EF14AD2ul, 0xEFBE4786ul, 0x384F25E3ul,
    0x0FC19DC6ul, 0x8B8CD5B5ul, 0x240CA1CCul, 0x77AC9C65ul,
    0x2DE92C6Ful, 0x592B0275ul, 0x4A7484AAul, 0x6EA6E483ul,
    0x5CB0A9DCul, 0xBD41FBD4ul, 0x76F988DAul, 0x831153B5ul,
    0x983E5152ul, 0xEE66DFABul, 0xA831C66Dul, 0x2DB43210ul,
    0xB00327C8ul, 0x98FB213Ful, 0xBF597FC7ul, 0xBEEF0EE4ul,
    0xC6E00BF3ul, 0x3DA88FC2ul, 0xD5A79147ul, 0x930AA725ul,
    0x06CA6351ul, 0xE003826Ful, 0x14292967ul, 0x0A0E6E70ul,
    0x27B70A85ul, 0x46D22FFCul, 0x2E1B2138ul, 0x5C26C926ul,
    0x4D2C6DFCul, 0x5AC42AEDul, 0x53380D13ul, 0x9D95B3DFul,
    0x650A7354ul, 0x8BAF63DEul, 0x766A0ABBul, 0x3C77B2A8ul,
    0x81C2C92Eul, 0x47EDAEE6ul, 0x92722C85ul, 0x1482353Bul,
    0xA2BFE8A1ul, 0x4CF10364ul, 0xA81A664Bul, 0xBC423001ul,
    0xC24B8B70ul, 0xD0F89791ul, 0xC76C51A3ul, 0x0654BE30ul,
    0xD192E819ul, 0xD6EF5218ul, 0xD6990624ul, 0x5565A910ul,
    0xF40E3585ul, 0x5771202Aul, 0x106AA070ul, 0x32BBD1B8ul,
    0x19A4C116ul, 0xB8D2D0C8ul, 0x1E376C08ul, 0x5141AB53ul,
    0x2748774Cul, 0xDF8EEB99ul, 0x34B0BCB5ul, 0xE19B48A8ul,
    0x391C0CB3ul, 0xC5C95A63ul, 0x4ED8AA4Aul, 0xE3418ACBul,
    0x5B9CCA4Ful, 0x7763E373ul, 0x682E6FF3ul, 0xD6B2B8A3ul,
    0x748F82EEul, 0x5DEFB2FCul, 0x78A5636Ful, 0x43172F60ul,
    0x84C87814ul, 0xA1F0AB72ul, 0x8CC70208ul, 0x1A6439ECul,
    0x90BEFFFAul, 0x23631E28ul, 0xA4506CEBul, 0xDE82BDE9ul,
    0xBEF9A3F7ul, 0xB2C67915ul, 0xC67178F2ul, 0xE372532Bul,
    0xCA273ECEul, 0xEA26619Cul, 0xD186B8C7ul, 0x21C0C207ul,
    0xEADA7DD6ul, 0xCDE0EB1Eul, 0xF57D4F7Ful, 0xEE6ED178ul,
    0x06F067AAul, 0x72176FBAul, 0x0A637DC5ul, 0xA2C898A6ul,
    0x113F9804ul, 0xBEF90DAEul, 0x1B710B35ul, 0x131C471Bul,
    0x28DB77F5ul, 0x23047D84ul, 0x32CAAB7Bul, 0x40C72493ul,
    0x3C9EBE0Aul, 0x15C9BEBCul, 0x431D67C4ul, 0x9C100D4Cul,
    0x4CC5D4BEul, 0xCB3E42B6ul, 0x597F299Cul, 0xFC657E2Aul,
    0x5FCB6FABul, 0x3AD6FAECul, 0x6C44198Cul, 0x4A475817ul
};

/* FIPS 180-4 5.3.5: the first 64 bits of the fractional parts of the square
 * roots of the first 8 primes, as hi, lo pairs. */
static const v9x_u32 v9x_sha512_h0[16] = {
    0x6A09E667ul, 0xF3BCC908ul, 0xBB67AE85ul, 0x84CAA73Bul,
    0x3C6EF372ul, 0xFE94F82Bul, 0xA54FF53Aul, 0x5F1D36F1ul,
    0x510E527Ful, 0xADE682D1ul, 0x9B05688Cul, 0x2B3E6C1Ful,
    0x1F83D9ABul, 0xFB41BD6Bul, 0x5BE0CD19ul, 0x137E2179ul
};

static const char v9x_sha512_hex_digits[16] = {
    '0', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
};

/* x + y mod 2^64. The low halves are added first; the sum wrapped exactly
 * when it is smaller than either addend, and that is the carry. */
static struct v9x_sha512_word v9x_sha512_add(struct v9x_sha512_word x,
                                             struct v9x_sha512_word y)
{
    struct v9x_sha512_word sum;

    sum.lo = (x.lo + y.lo) & 0xFFFFFFFFul;
    sum.hi = (x.hi + y.hi) & 0xFFFFFFFFul;
    if (sum.lo < x.lo) {
        sum.hi = (sum.hi + 1ul) & 0xFFFFFFFFul;
    }
    return sum;
}

/*
 * ROTR^n (FIPS 180-4 3.2) for 0 < n < 64, n != 32.
 *
 * For n < 32 each half takes its own bits shifted down and the other half's
 * low n bits on top. For n > 32 the rotation is a half swap followed by a
 * rotation by n - 32. n == 32 is excluded because it would ask for a 32-bit
 * shift, which C leaves undefined; SHA-512 never rotates by 0 or 32.
 */
static struct v9x_sha512_word v9x_sha512_rotr(struct v9x_sha512_word x,
                                              v9x_u16 bits)
{
    struct v9x_sha512_word out;
    v9x_u32 swap;

    if (bits > 32u) {
        swap = x.hi;
        x.hi = x.lo;
        x.lo = swap;
        bits = (v9x_u16)(bits - 32u);
    }
    out.hi = ((x.hi >> bits) | (x.lo << (32u - bits))) & 0xFFFFFFFFul;
    out.lo = ((x.lo >> bits) | (x.hi << (32u - bits))) & 0xFFFFFFFFul;
    return out;
}

/* SHR^n (FIPS 180-4 3.2) for 0 < n < 32, the only range SHA-512 uses. */
static struct v9x_sha512_word v9x_sha512_shr(struct v9x_sha512_word x,
                                             v9x_u16 bits)
{
    struct v9x_sha512_word out;

    out.hi = x.hi >> bits;
    out.lo = ((x.lo >> bits) | (x.hi << (32u - bits))) & 0xFFFFFFFFul;
    return out;
}

static struct v9x_sha512_word v9x_sha512_xor3(struct v9x_sha512_word x,
                                              struct v9x_sha512_word y,
                                              struct v9x_sha512_word z)
{
    struct v9x_sha512_word out;

    out.hi = x.hi ^ y.hi ^ z.hi;
    out.lo = x.lo ^ y.lo ^ z.lo;
    return out;
}

/* FIPS 180-4 4.1.3, equations 4.8 to 4.13. Ch and Maj are bitwise, so they
 * apply to each half independently. */
static struct v9x_sha512_word v9x_sha512_ch(struct v9x_sha512_word x,
                                            struct v9x_sha512_word y,
                                            struct v9x_sha512_word z)
{
    struct v9x_sha512_word out;

    out.hi = (x.hi & y.hi) ^ (~x.hi & z.hi);
    out.lo = (x.lo & y.lo) ^ (~x.lo & z.lo);
    return out;
}

static struct v9x_sha512_word v9x_sha512_maj(struct v9x_sha512_word x,
                                             struct v9x_sha512_word y,
                                             struct v9x_sha512_word z)
{
    struct v9x_sha512_word out;

    out.hi = (x.hi & y.hi) ^ (x.hi & z.hi) ^ (y.hi & z.hi);
    out.lo = (x.lo & y.lo) ^ (x.lo & z.lo) ^ (y.lo & z.lo);
    return out;
}

static struct v9x_sha512_word v9x_sha512_big_sigma0(struct v9x_sha512_word x)
{
    return v9x_sha512_xor3(v9x_sha512_rotr(x, 28u), v9x_sha512_rotr(x, 34u),
                           v9x_sha512_rotr(x, 39u));
}

static struct v9x_sha512_word v9x_sha512_big_sigma1(struct v9x_sha512_word x)
{
    return v9x_sha512_xor3(v9x_sha512_rotr(x, 14u), v9x_sha512_rotr(x, 18u),
                           v9x_sha512_rotr(x, 41u));
}

static struct v9x_sha512_word v9x_sha512_small_sigma0(struct v9x_sha512_word x)
{
    return v9x_sha512_xor3(v9x_sha512_rotr(x, 1u), v9x_sha512_rotr(x, 8u),
                           v9x_sha512_shr(x, 7u));
}

static struct v9x_sha512_word v9x_sha512_small_sigma1(struct v9x_sha512_word x)
{
    return v9x_sha512_xor3(v9x_sha512_rotr(x, 19u), v9x_sha512_rotr(x, 61u),
                           v9x_sha512_shr(x, 6u));
}

/* A big-endian 32-bit load (FIPS 180-4 3.1). */
static v9x_u32 v9x_sha512_load32(const v9x_u8 *bytes)
{
    return ((v9x_u32)bytes[0] << 24) | ((v9x_u32)bytes[1] << 16) |
           ((v9x_u32)bytes[2] << 8) | (v9x_u32)bytes[3];
}

/* FIPS 180-4 6.4.2: process one 128-byte block. */
static void v9x_sha512_block(v9x_u32 state[16], const v9x_u8 block[128])
{
    struct v9x_sha512_word w[80];
    struct v9x_sha512_word v[8];
    struct v9x_sha512_word k;
    struct v9x_sha512_word t1;
    struct v9x_sha512_word t2;
    v9x_u16 i;
    v9x_u16 j;

    /* Step 1: the message schedule. */
    for (i = 0u; i < 16u; ++i) {
        w[i].hi = v9x_sha512_load32(block + 8u * i);
        w[i].lo = v9x_sha512_load32(block + 8u * i + 4u);
    }
    for (i = 16u; i < 80u; ++i) {
        w[i] = v9x_sha512_add(
            v9x_sha512_add(v9x_sha512_small_sigma1(w[i - 2u]), w[i - 7u]),
            v9x_sha512_add(v9x_sha512_small_sigma0(w[i - 15u]), w[i - 16u]));
    }

    /* Step 2: v[0..7] are FIPS's working variables a..h. */
    for (j = 0u; j < 8u; ++j) {
        v[j].hi = state[2u * j];
        v[j].lo = state[2u * j + 1u];
    }

    /* Step 3: the 80 rounds. */
    for (i = 0u; i < 80u; ++i) {
        k.hi = v9x_sha512_k[2u * i];
        k.lo = v9x_sha512_k[2u * i + 1u];
        t1 = v9x_sha512_add(
            v9x_sha512_add(v[7], v9x_sha512_big_sigma1(v[4])),
            v9x_sha512_add(v9x_sha512_ch(v[4], v[5], v[6]),
                           v9x_sha512_add(k, w[i])));
        t2 = v9x_sha512_add(v9x_sha512_big_sigma0(v[0]),
                            v9x_sha512_maj(v[0], v[1], v[2]));
        v[7] = v[6];
        v[6] = v[5];
        v[5] = v[4];
        v[4] = v9x_sha512_add(v[3], t1);
        v[3] = v[2];
        v[2] = v[1];
        v[1] = v[0];
        v[0] = v9x_sha512_add(t1, t2);
    }

    /* Step 4: the intermediate hash value. */
    for (j = 0u; j < 8u; ++j) {
        k.hi = state[2u * j];
        k.lo = state[2u * j + 1u];
        k = v9x_sha512_add(k, v[j]);
        state[2u * j] = k.hi;
        state[2u * j + 1u] = k.lo;
    }
}

void v9x_sha512_init(struct v9x_sha512 *ctx)
{
    v9x_u16 i;

    for (i = 0u; i < 16u; ++i) {
        ctx->state[i] = v9x_sha512_h0[i];
    }
    ctx->count_lo = 0ul;
    ctx->count_hi = 0ul;
    ctx->buffered = 0u;
}

void v9x_sha512_update(struct v9x_sha512 *ctx,
                       const void *data,
                       v9x_u32 length)
{
    const v9x_u8 *bytes;
    v9x_u32 i;

    bytes = (const v9x_u8 *)data;

    ctx->count_lo = (ctx->count_lo + length) & 0xFFFFFFFFul;
    if (ctx->count_lo < length) {
        ctx->count_hi = (ctx->count_hi + 1ul) & 0xFFFFFFFFul;
    }

    for (i = 0ul; i < length; ++i) {
        ctx->buffer[ctx->buffered] = bytes[i];
        ++ctx->buffered;
        if (ctx->buffered == V9X_SHA512_BLOCK_BYTES) {
            v9x_sha512_block(ctx->state, ctx->buffer);
            ctx->buffered = 0u;
        }
    }
}

void v9x_sha512_final(struct v9x_sha512 *ctx, v9x_u8 digest[64])
{
    v9x_u32 bits_hi;
    v9x_u32 bits_lo;
    v9x_u32 bits_top;
    v9x_u16 i;

    /* FIPS 180-4 5.1.2: the length in bits as a 128-bit big-endian integer.
     * A 64-bit byte count times 8 needs 67 bits; bits_top holds the three
     * that spill past the low 64. */
    bits_top = ctx->count_hi >> 29;
    bits_hi = ((ctx->count_hi << 3) | (ctx->count_lo >> 29)) & 0xFFFFFFFFul;
    bits_lo = (ctx->count_lo << 3) & 0xFFFFFFFFul;

    /* A single 1 bit, then zeros until 16 bytes short of a block boundary. */
    ctx->buffer[ctx->buffered] = 0x80u;
    ++ctx->buffered;
    if (ctx->buffered > 112u) {
        while (ctx->buffered < V9X_SHA512_BLOCK_BYTES) {
            ctx->buffer[ctx->buffered] = 0u;
            ++ctx->buffered;
        }
        v9x_sha512_block(ctx->state, ctx->buffer);
        ctx->buffered = 0u;
    }
    while (ctx->buffered < 116u) {
        ctx->buffer[ctx->buffered] = 0u;
        ++ctx->buffered;
    }
    for (i = 0u; i < 4u; ++i) {
        ctx->buffer[116u + i] = (v9x_u8)((bits_top >> (24u - 8u * i)) & 0xFFu);
        ctx->buffer[120u + i] = (v9x_u8)((bits_hi >> (24u - 8u * i)) & 0xFFu);
        ctx->buffer[124u + i] = (v9x_u8)((bits_lo >> (24u - 8u * i)) & 0xFFu);
    }
    v9x_sha512_block(ctx->state, ctx->buffer);
    ctx->buffered = 0u;

    /* H_0..H_7, each big-endian: state[] is already in hi, lo order. */
    for (i = 0u; i < 64u; ++i) {
        digest[i] = (v9x_u8)((ctx->state[i / 4u] >> (24u - 8u * (i % 4u))) &
                             0xFFu);
    }
}

void v9x_sha512_hex(const v9x_u8 digest[64], char out[129])
{
    v9x_u16 i;

    for (i = 0u; i < 64u; ++i) {
        out[2u * i] = v9x_sha512_hex_digits[(digest[i] >> 4) & 0x0Fu];
        out[2u * i + 1u] = v9x_sha512_hex_digits[digest[i] & 0x0Fu];
    }
    out[128] = '\0';
}
