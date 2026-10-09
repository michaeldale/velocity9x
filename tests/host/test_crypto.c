/*
 * Tests for the updater's cryptography: SHA-256, SHA-512 and Ed25519.
 *
 * Every expected value here comes from a published standard, never from this
 * implementation's own output: the FIPS 180-2 Appendix B / C examples (the
 * same messages FIPS 180-4's example documents use), and RFC 8032 section
 * 7.1's Ed25519 vectors, copied from the RFC text including the 1023-byte
 * TEST 1024 message. A self-consistent sign/verify pair would pass with a
 * wrong curve constant; an RFC signature reproduced byte for byte cannot.
 *
 * The negatives are the refusals RFC 8032 5.1.7 requires and the updater
 * depends on: any single-bit change to message, R, S or key, S + L in place
 * of S, keys that do not decode, and an all-zero signature.
 */
#include <stdio.h>
#include <string.h>

#include "velocity9x/sha256.h"
#include "velocity9x/sha512.h"
#include "velocity9x/ed25519.h"

static unsigned int crypto_failures = 0u;

#define CRCHECK(expression) do { \
    if (!(expression)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #expression); \
        ++crypto_failures; \
    } \
} while (0)

/* The two-block messages of FIPS 180-2 Appendix B.2 (448 bits) and C.2
 * (896 bits). Each sits on its algorithm's padding boundary: 56 and 112
 * bytes leave no room for the length, so the padding spills a block. */
#define MSG_448 "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"
#define MSG_896 "abcdefghbcdefghicdefghijdefghijkefghijklfghijklm" \
                "ghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrs" \
                "mnopqrstnopqrstu"

struct sha_vector {
    const char *message;
    const char *sha256;
    const char *sha512;
};

static const struct sha_vector sha_vectors[] = {
    {
        "",
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce"
        "47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e"
    },
    {
        "abc",
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
        "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f"
    },
    {
        MSG_448,
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
        "204a8fc6dda82f0a0ced7beb8e08a41657c16ef468b228a8279be331a703c335"
        "96fd15c13b1b07f9aa1d3bea57789ca031ad85c7a71dd70354ec631238ca3445"
    },
    {
        MSG_896,
        "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1",
        "8e959b75dae313da8cf4f72814fc143f8f7779c6eb9f7fa17299aeadb6889018"
        "501d289e4900f7e4331b99dec4b5433ac7d329eeb6dd26545e96e55b874be909"
    }
};

/* One million 'a' (FIPS 180-2 B.3 and C.3). */
#define MILLION_A_SHA256 \
    "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"
#define MILLION_A_SHA512 \
    "e718483d0ce769644e2e42c7bc15b4638e1f98b13b2044285632a803afa973eb" \
    "de0ff244877ea60a4cb0432ce577c31beb009c5c2c49aa2e4eadb217ad8cc09b"

/* Chunk sizes cycled through when feeding the million: either side of both
 * block sizes, primes that walk the buffer through every offset, and 1 so a
 * byte-at-a-time caller is covered too. */
static const v9x_u32 chunk_sizes[] = {
    1ul, 3ul, 63ul, 64ul, 65ul, 127ul, 128ul, 129ul, 997ul, 7ul
};
#define CHUNK_SIZE_COUNT (sizeof(chunk_sizes) / sizeof(chunk_sizes[0]))
#define CHUNK_MAX 997u

struct ed25519_vector {
    const char *name;
    unsigned int length;
    const char *seed;
    const char *public_key;
    const char *message;
    const char *signature;
};

/* RFC 8032 section 7.1, in the RFC's order. */
static const struct ed25519_vector ed25519_vectors[] = {
    {
        "TEST 1", 0u,
        "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60",
        "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a",
        "",
        "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e06522490155"
        "5fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"
    },
    {
        "TEST 2", 1u,
        "4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb",
        "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c",
        "72",
        "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da"
        "085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00"
    },
    {
        "TEST 3", 2u,
        "c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7",
        "fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025",
        "af82",
        "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac"
        "18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a"
    },
    {
        "TEST 1024", 1023u,
        "f5e5767cf153319517630f226876b86c8160cc583bc013744c6bf255f5cc0ee5",
        "278117fc144c72340f67d0f2316e8386ceffbf2b2428c9c51fef7c597f1d426e",
        "08b8b2b733424243760fe426a4b54908632110a66c2f6591eabd3345e3e4eb98"
        "fa6e264bf09efe12ee50f8f54e9f77b1e355f6c50544e23fb1433ddf73be84d8"
        "79de7c0046dc4996d9e773f4bc9efe5738829adb26c81b37c93a1b270b20329d"
        "658675fc6ea534e0810a4432826bf58c941efb65d57a338bbd2e26640f89ffbc"
        "1a858efcb8550ee3a5e1998bd177e93a7363c344fe6b199ee5d02e82d522c4fe"
        "ba15452f80288a821a579116ec6dad2b3b310da903401aa62100ab5d1a36553e"
        "06203b33890cc9b832f79ef80560ccb9a39ce767967ed628c6ad573cb116dbef"
        "efd75499da96bd68a8a97b928a8bbc103b6621fcde2beca1231d206be6cd9ec7"
        "aff6f6c94fcd7204ed3455c68c83f4a41da4af2b74ef5c53f1d8ac70bdcb7ed1"
        "85ce81bd84359d44254d95629e9855a94a7c1958d1f8ada5d0532ed8a5aa3fb2"
        "d17ba70eb6248e594e1a2297acbbb39d502f1a8c6eb6f1ce22b3de1a1f40cc24"
        "554119a831a9aad6079cad88425de6bde1a9187ebb6092cf67bf2b13fd65f270"
        "88d78b7e883c8759d2c4f5c65adb7553878ad575f9fad878e80a0c9ba63bcbcc"
        "2732e69485bbc9c90bfbd62481d9089beccf80cfe2df16a2cf65bd92dd597b07"
        "07e0917af48bbb75fed413d238f5555a7a569d80c3414a8d0859dc65a46128ba"
        "b27af87a71314f318c782b23ebfe808b82b0ce26401d2e22f04d83d1255dc51a"
        "ddd3b75a2b1ae0784504df543af8969be3ea7082ff7fc9888c144da2af58429e"
        "c96031dbcad3dad9af0dcbaaaf268cb8fcffead94f3c7ca495e056a9b47acdb7"
        "51fb73e666c6c655ade8297297d07ad1ba5e43f1bca32301651339e22904cc8c"
        "42f58c30c04aafdb038dda0847dd988dcda6f3bfd15c4b4c4525004aa06eeff8"
        "ca61783aacec57fb3d1f92b0fe2fd1a85f6724517b65e614ad6808d6f6ee34df"
        "f7310fdc82aebfd904b01e1dc54b2927094b2db68d6f903b68401adebf5a7e08"
        "d78ff4ef5d63653a65040cf9bfd4aca7984a74d37145986780fc0b16ac451649"
        "de6188a7dbdf191f64b5fc5e2ab47b57f7f7276cd419c17a3ca8e1b939ae49e4"
        "88acba6b965610b5480109c8b17b80e1b7b750dfc7598d5d5011fd2dcc5600a3"
        "2ef5b52a1ecc820e308aa342721aac0943bf6686b64b2579376504ccc493d97e"
        "6aed3fb0f9cd71a43dd497f01f17c0e2cb3797aa2a2f256656168e6c496afc5f"
        "b93246f6b1116398a346f1a641f3b041e989f7914f90cc2c7fff357876e506b5"
        "0d334ba77c225bc307ba537152f3f1610e4eafe595f6d9d90d11faa933a15ef1"
        "369546868a7f3a45a96768d40fd9d03412c091c6315cf4fde7cb68606937380d"
        "b2eaaa707b4c4185c32eddcdd306705e4dc1ffc872eeee475a64dfac86aba41c"
        "0618983f8741c5ef68d3a101e8a3b8cac60c905c15fc910840b94c00a0b9d0",
        "0aab4c900501b3e24d7cdf4663326a3a87df5e4843b2cbdb67cbf6e460fec350"
        "aa5371b1508f9f4528ecea23c436d94b5e8fcd4f681e30a6ac00a9704a188a03"
    },
    {
        "TEST SHA(abc)", 64u,
        "833fe62409237b9d62ec77587520911e9a759cec1d19755b7da901b96dca3d42",
        "ec172b93ad5e563bf4932c70e1245034c35467ef2efd4d64ebf819683467e2bf",
        "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
        "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f",
        "dc2a4459e7369633a52b1bf277839a00201009a3efbf3ecb69bea2186c26b589"
        "09351fc9ac90b3ecfdfbc7c66431e0303dca179c138ac17ad9bef1177331a704"
    }
};
#define ED25519_VECTOR_COUNT \
    (sizeof(ed25519_vectors) / sizeof(ed25519_vectors[0]))

/* L, little-endian, for the S + L negative. */
static const v9x_u8 group_order[32] = {
    0xED, 0xD3, 0xF5, 0x5C, 0x1A, 0x63, 0x12, 0x58,
    0xD6, 0x9C, 0xF7, 0xA2, 0xDE, 0xF9, 0xDE, 0x14,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10
};

static unsigned int hex_value(char c)
{
    if (c >= '0' && c <= '9') {
        return (unsigned int)(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return (unsigned int)(c - 'a' + 10);
    }
    return (unsigned int)(c - 'A' + 10);
}

/* Decode a hex string into out; returns the byte count. */
static unsigned int hex_decode(const char *hex, v9x_u8 *out)
{
    unsigned int count;

    count = 0u;
    while (hex[0] != '\0' && hex[1] != '\0') {
        out[count] = (v9x_u8)((hex_value(hex[0]) << 4) | hex_value(hex[1]));
        ++count;
        hex += 2;
    }
    return count;
}

static void test_sha256_vectors(void)
{
    struct v9x_sha256 ctx;
    v9x_u8 digest[32];
    char hex[65];
    unsigned int i;

    for (i = 0u; i < sizeof(sha_vectors) / sizeof(sha_vectors[0]); ++i) {
        v9x_sha256_init(&ctx);
        v9x_sha256_update(&ctx, sha_vectors[i].message,
                          (v9x_u32)strlen(sha_vectors[i].message));
        v9x_sha256_final(&ctx, digest);
        v9x_sha256_hex(digest, hex);
        CRCHECK(strcmp(hex, sha_vectors[i].sha256) == 0);
        if (strcmp(hex, sha_vectors[i].sha256) != 0) {
            printf("  sha256 vector %u: got %s\n", i, hex);
        }
    }

    /* "abc" in three calls, one empty, must match the one-shot digest. */
    v9x_sha256_init(&ctx);
    v9x_sha256_update(&ctx, "a", 1ul);
    v9x_sha256_update(&ctx, "", 0ul);
    v9x_sha256_update(&ctx, "bc", 2ul);
    v9x_sha256_final(&ctx, digest);
    v9x_sha256_hex(digest, hex);
    CRCHECK(strcmp(hex, sha_vectors[1].sha256) == 0);
}

static void test_sha512_vectors(void)
{
    struct v9x_sha512 ctx;
    v9x_u8 digest[64];
    char hex[129];
    unsigned int i;

    for (i = 0u; i < sizeof(sha_vectors) / sizeof(sha_vectors[0]); ++i) {
        v9x_sha512_init(&ctx);
        v9x_sha512_update(&ctx, sha_vectors[i].message,
                          (v9x_u32)strlen(sha_vectors[i].message));
        v9x_sha512_final(&ctx, digest);
        v9x_sha512_hex(digest, hex);
        CRCHECK(strcmp(hex, sha_vectors[i].sha512) == 0);
        if (strcmp(hex, sha_vectors[i].sha512) != 0) {
            printf("  sha512 vector %u: got %s\n", i, hex);
        }
    }
}

static void test_sha_million_a(void)
{
    static v9x_u8 a_block[CHUNK_MAX];
    struct v9x_sha256 ctx256;
    struct v9x_sha512 ctx512;
    v9x_u8 digest256[32];
    v9x_u8 digest512[64];
    char hex256[65];
    char hex512[129];
    v9x_u32 remaining;
    v9x_u32 chunk;
    unsigned int which;

    memset(a_block, 'a', sizeof(a_block));
    v9x_sha256_init(&ctx256);
    v9x_sha512_init(&ctx512);
    remaining = 1000000ul;
    which = 0u;
    while (remaining > 0ul) {
        chunk = chunk_sizes[which % CHUNK_SIZE_COUNT];
        if (chunk > remaining) {
            chunk = remaining;
        }
        v9x_sha256_update(&ctx256, a_block, chunk);
        v9x_sha512_update(&ctx512, a_block, chunk);
        remaining -= chunk;
        ++which;
    }
    v9x_sha256_final(&ctx256, digest256);
    v9x_sha512_final(&ctx512, digest512);
    v9x_sha256_hex(digest256, hex256);
    v9x_sha512_hex(digest512, hex512);
    CRCHECK(strcmp(hex256, MILLION_A_SHA256) == 0);
    CRCHECK(strcmp(hex512, MILLION_A_SHA512) == 0);
}

static void test_ed25519_rfc8032_vectors(void)
{
    static v9x_u8 message[1024];
    v9x_u8 seed[32];
    v9x_u8 public_key[32];
    v9x_u8 expected_public[32];
    v9x_u8 expected_signature[64];
    v9x_u8 signature[64];
    unsigned int i;
    unsigned int length;

    for (i = 0u; i < ED25519_VECTOR_COUNT; ++i) {
        CRCHECK(hex_decode(ed25519_vectors[i].seed, seed) == 32u);
        CRCHECK(hex_decode(ed25519_vectors[i].public_key, expected_public) ==
                32u);
        CRCHECK(hex_decode(ed25519_vectors[i].signature, expected_signature) ==
                64u);
        length = hex_decode(ed25519_vectors[i].message, message);
        CRCHECK(length == ed25519_vectors[i].length);

        v9x_ed25519_public_key(seed, public_key);
        CRCHECK(memcmp(public_key, expected_public, 32u) == 0);

        v9x_ed25519_sign(seed, expected_public, message, (v9x_u32)length,
                         signature);
        CRCHECK(memcmp(signature, expected_signature, 64u) == 0);

        CRCHECK(v9x_ed25519_verify(expected_signature, expected_public,
                                   message, (v9x_u32)length) == V9X_TRUE);

        if (memcmp(public_key, expected_public, 32u) != 0 ||
            memcmp(signature, expected_signature, 64u) != 0) {
            printf("  ed25519 %s mismatched\n", ed25519_vectors[i].name);
        }
    }

    /* An empty message may be passed as a null pointer. */
    CRCHECK(hex_decode(ed25519_vectors[0].public_key, expected_public) == 32u);
    CRCHECK(hex_decode(ed25519_vectors[0].signature, expected_signature) ==
            64u);
    CRCHECK(v9x_ed25519_verify(expected_signature, expected_public, 0, 0ul) ==
            V9X_TRUE);
}

static void test_ed25519_refusals(void)
{
    static v9x_u8 message[1024];
    v9x_u8 public_key[32];
    v9x_u8 signature[64];
    v9x_u8 tampered[64];
    v9x_u8 bad_key[32];
    unsigned int length;
    unsigned int i;
    unsigned int sum;

    /* TEST 3: a two-byte message, so the message flip has room. */
    CRCHECK(hex_decode(ed25519_vectors[2].public_key, public_key) == 32u);
    CRCHECK(hex_decode(ed25519_vectors[2].signature, signature) == 64u);
    length = hex_decode(ed25519_vectors[2].message, message);
    CRCHECK(v9x_ed25519_verify(signature, public_key, message,
                               (v9x_u32)length) == V9X_TRUE);

    /* One flipped bit in the message. */
    message[1] ^= 0x01u;
    CRCHECK(v9x_ed25519_verify(signature, public_key, message,
                               (v9x_u32)length) == V9X_FALSE);
    message[1] ^= 0x01u;

    /* One flipped bit in R. */
    memcpy(tampered, signature, 64u);
    tampered[5] ^= 0x10u;
    CRCHECK(v9x_ed25519_verify(tampered, public_key, message,
                               (v9x_u32)length) == V9X_FALSE);

    /* One flipped bit in S, low enough that S stays below L, so this is the
     * equation refusing rather than the range check. */
    memcpy(tampered, signature, 64u);
    tampered[32] ^= 0x01u;
    CRCHECK(v9x_ed25519_verify(tampered, public_key, message,
                               (v9x_u32)length) == V9X_FALSE);

    /* One flipped bit in the public key. */
    memcpy(bad_key, public_key, 32u);
    bad_key[0] ^= 0x01u;
    CRCHECK(v9x_ed25519_verify(signature, bad_key, message,
                               (v9x_u32)length) == V9X_FALSE);

    /* S + L: the same point equation holds, since L B is the identity, so
     * only RFC 8032 5.1.7's S < L check stands between it and acceptance. */
    memcpy(tampered, signature, 64u);
    sum = 0u;
    for (i = 0u; i < 32u; ++i) {
        sum += (unsigned int)tampered[32u + i] + (unsigned int)group_order[i];
        tampered[32u + i] = (v9x_u8)(sum & 0xFFu);
        sum >>= 8;
    }
    CRCHECK(sum == 0u);
    CRCHECK(v9x_ed25519_verify(tampered, public_key, message,
                               (v9x_u32)length) == V9X_FALSE);

    /* S == L exactly, the boundary of that check. */
    memcpy(tampered, signature, 32u);
    memcpy(tampered + 32, group_order, 32u);
    CRCHECK(v9x_ed25519_verify(tampered, public_key, message,
                               (v9x_u32)length) == V9X_FALSE);

    /* Public keys that do not decode (RFC 8032 5.1.3). y = 2 gives a u/v
     * that is not a square mod p, so there is no x at all. */
    memset(bad_key, 0, sizeof(bad_key));
    bad_key[0] = 2u;
    CRCHECK(v9x_ed25519_verify(signature, bad_key, message,
                               (v9x_u32)length) == V9X_FALSE);

    /* y = p: not reduced, refused by step 1 even though y = 0 would decode. */
    memset(bad_key, 0xFF, sizeof(bad_key));
    bad_key[0] = 0xEDu;
    bad_key[31] = 0x7Fu;
    CRCHECK(v9x_ed25519_verify(signature, bad_key, message,
                               (v9x_u32)length) == V9X_FALSE);

    /* y = 1 with the sign bit set: x = 0 cannot be "negative" (step 4). */
    memset(bad_key, 0, sizeof(bad_key));
    bad_key[0] = 1u;
    bad_key[31] = 0x80u;
    CRCHECK(v9x_ed25519_verify(signature, bad_key, message,
                               (v9x_u32)length) == V9X_FALSE);

    /* The same y >= p refusal applies to R. */
    memcpy(tampered, signature, 64u);
    memset(tampered, 0xFF, 32u);
    tampered[0] = 0xEDu;
    tampered[31] = 0x7Fu;
    CRCHECK(v9x_ed25519_verify(tampered, public_key, message,
                               (v9x_u32)length) == V9X_FALSE);

    /* An all-zero signature: R = (sqrt(-1), 0) decodes and S = 0 is in
     * range, so this exercises the equation, which must refuse it. */
    memset(tampered, 0, sizeof(tampered));
    CRCHECK(v9x_ed25519_verify(tampered, public_key, message,
                               (v9x_u32)length) == V9X_FALSE);

    /* The long vector with its last byte changed. */
    CRCHECK(hex_decode(ed25519_vectors[3].public_key, public_key) == 32u);
    CRCHECK(hex_decode(ed25519_vectors[3].signature, signature) == 64u);
    length = hex_decode(ed25519_vectors[3].message, message);
    message[length - 1u] ^= 0x80u;
    CRCHECK(v9x_ed25519_verify(signature, public_key, message,
                               (v9x_u32)length) == V9X_FALSE);
}

/*
 * The decoding refusals, isolated so the equation cannot hide them.
 *
 * With A the identity (y = 1) and R the identity, S = 0 satisfies [S]B ==
 * R + [k]A for every k, so ((identity, 0), identity) verifies over any
 * message. That makes it the probe: each case below changes only how the
 * identity is spelled, and is accepted unless RFC 8032 5.1.3's strict
 * decoding refuses the spelling. Without the identity trick a broken
 * decoder would still be caught by the equation failing, which tests
 * nothing about the decoder.
 *
 * (A signature under the identity key proves nothing about anyone; the
 * updater pins its own key, so accepting it is RFC behaviour, not a hole.)
 */
static void test_ed25519_strict_decoding(void)
{
    static const v9x_u8 message[] = "any message at all";
    v9x_u8 identity[32];
    v9x_u8 y_plus_p[32];
    v9x_u8 negative_zero[32];
    v9x_u8 signature[64];
    v9x_u32 length;

    length = (v9x_u32)(sizeof(message) - 1u);

    /* 01 00 .. 00: y = 1, x = 0. */
    memset(identity, 0, sizeof(identity));
    identity[0] = 1u;

    /* y = 1 + p = 2^255 - 18: the identity's y, not reduced. */
    memset(y_plus_p, 0xFF, sizeof(y_plus_p));
    y_plus_p[0] = 0xEEu;
    y_plus_p[31] = 0x7Fu;

    /* y = 1 with x_0 set: x = 0 claimed negative. */
    memcpy(negative_zero, identity, sizeof(negative_zero));
    negative_zero[31] = 0x80u;

    /* The canonical spelling is accepted - the probe works. */
    memset(signature, 0, sizeof(signature));
    memcpy(signature, identity, 32u);
    CRCHECK(v9x_ed25519_verify(signature, identity, message, length) ==
            V9X_TRUE);

    /* Step 1, y >= p, for the key and for R. */
    CRCHECK(v9x_ed25519_verify(signature, y_plus_p, message, length) ==
            V9X_FALSE);
    memcpy(signature, y_plus_p, 32u);
    CRCHECK(v9x_ed25519_verify(signature, identity, message, length) ==
            V9X_FALSE);

    /* Step 4, x = 0 with x_0 = 1, for the key and for R. */
    memcpy(signature, identity, 32u);
    CRCHECK(v9x_ed25519_verify(signature, negative_zero, message, length) ==
            V9X_FALSE);
    memcpy(signature, negative_zero, 32u);
    CRCHECK(v9x_ed25519_verify(signature, identity, message, length) ==
            V9X_FALSE);
}

/* Sign and verify with a key that is not an RFC vector, and check the
 * signature is refused under a different message length - the length is
 * hashed, not just the bytes. */
static void test_ed25519_round_trip(void)
{
    v9x_u8 seed[32];
    v9x_u8 public_key[32];
    v9x_u8 signature[64];
    static const char text[] = "Velocity9x release 0.15.0";
    unsigned int i;

    for (i = 0u; i < 32u; ++i) {
        seed[i] = (v9x_u8)(i * 7u + 1u);
    }
    v9x_ed25519_public_key(seed, public_key);
    v9x_ed25519_sign(seed, public_key, (const v9x_u8 *)text,
                     (v9x_u32)(sizeof(text) - 1u), signature);
    CRCHECK(v9x_ed25519_verify(signature, public_key, (const v9x_u8 *)text,
                               (v9x_u32)(sizeof(text) - 1u)) == V9X_TRUE);
    CRCHECK(v9x_ed25519_verify(signature, public_key, (const v9x_u8 *)text,
                               (v9x_u32)(sizeof(text) - 2u)) == V9X_FALSE);
}

unsigned int v9x_run_crypto_tests(void)
{
    test_sha256_vectors();
    test_sha512_vectors();
    test_sha_million_a();
    test_ed25519_rfc8032_vectors();
    test_ed25519_refusals();
    test_ed25519_strict_decoding();
    test_ed25519_round_trip();
    return crypto_failures;
}
