/*
 * Tests for the signed release file (update_release.c).
 *
 * The fixtures are signed here, at run time, with a fixed test seed - never
 * the release key, whose private half is not in the repository. What is
 * under test is the framing around the signature: what is covered, what may
 * follow it, and what is read back.
 */
#include <stdio.h>
#include <string.h>

#include "velocity9x/ed25519.h"
#include "velocity9x/update_release.h"

static unsigned int release_failures = 0u;

#define RLCHECK(expression) do { \
    if (!(expression)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #expression); \
        ++release_failures; \
    } \
} while (0)

static const v9x_u8 test_seed[32] = {
    0x76, 0x39, 0x78, 0x55, 0x10, 0x01, 0x02, 0x03,
    0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,
    0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13,
    0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b
};

static const char release_body[] =
    "[Velocity9xRelease]\r\n"
    "Schema=1\r\n"
    "App=velocity9x\r\n"
    "Version=0.16.0\r\n"
    "Build=1a2b3c4\r\n"
    "\r\n"
    "[Family.ati]\r\n"
    "File=velocity9x-0.16.0-ati.zip\r\n"
    "Size=347696\r\n"
    "Sha256=8c48b14bb0526fd5f8a2ab682b648c87aaa2a08224a6d1744270add941c15b69\r\n"
    "\r\n"
    "[Family.matrox]\r\n"
    "File=velocity9x-0.16.0-matrox.zip\r\n"
    "Size=12\r\n"
    "Sha256=00000000000000000000000000000000000000000000000000000000000000FF\r\n"
    "\r\n";

static v9x_u8 test_public[32];
static char signed_text[2048];

/* body + [Signature] + Ed25519=<hex over body> + tail. */
static unsigned int build_signed(const char *body, const char *tail)
{
    static const char hex[] = "0123456789abcdef";
    v9x_u8 signature[64];
    unsigned int used;
    unsigned int index;

    v9x_ed25519_sign(test_seed, test_public, (const v9x_u8 *)body,
                     (v9x_u32)strlen(body), signature);
    strcpy(signed_text, body);
    strcat(signed_text, "[Signature]\r\nEd25519=");
    used = (unsigned int)strlen(signed_text);
    for (index = 0u; index < 64u; ++index) {
        signed_text[used++] = hex[signature[index] >> 4];
        signed_text[used++] = hex[signature[index] & 0x0Fu];
    }
    signed_text[used] = '\0';
    strcat(signed_text, tail);
    return (unsigned int)strlen(signed_text);
}

static void test_verify(void)
{
    v9x_u32 covered = 0u;
    unsigned int length;
    v9x_u8 wrong_key[32];

    length = build_signed(release_body, "\r\n");
    RLCHECK(v9x_release_verify(signed_text, length, test_public, &covered));
    RLCHECK(covered == strlen(release_body));

    /* With no final line end, and with a bare LF. */
    length = build_signed(release_body, "");
    RLCHECK(v9x_release_verify(signed_text, length, test_public, &covered));
    length = build_signed(release_body, "\n");
    RLCHECK(v9x_release_verify(signed_text, length, test_public, &covered));

    /* Anything after the signature line: a second section that would
     * otherwise be read, or even a blank line. */
    length = build_signed(release_body,
                          "\r\n[Family.ati]\r\nSha256=00\r\n");
    RLCHECK(!v9x_release_verify(signed_text, length, test_public, &covered));
    length = build_signed(release_body, "\r\n\r\n");
    RLCHECK(!v9x_release_verify(signed_text, length, test_public, &covered));

    /* One changed byte of the signed text. */
    length = build_signed(release_body, "\r\n");
    signed_text[40] ^= 0x01;
    RLCHECK(!v9x_release_verify(signed_text, length, test_public, &covered));

    /* One changed signature digit. */
    length = build_signed(release_body, "\r\n");
    signed_text[strlen(release_body) + 25u] =
        signed_text[strlen(release_body) + 25u] == '0' ? '1' : '0';
    RLCHECK(!v9x_release_verify(signed_text, length, test_public, &covered));

    /* Another key. */
    length = build_signed(release_body, "\r\n");
    memcpy(wrong_key, test_public, sizeof(wrong_key));
    wrong_key[0] ^= 0x80u;
    RLCHECK(!v9x_release_verify(signed_text, length, wrong_key, &covered));

    /* Truncated at every point: never accepted. */
    {
        unsigned int cut;
        unsigned int accepted = 0u;

        length = build_signed(release_body, "\r\n");
        for (cut = 0u; cut + 2u < length; ++cut) {
            if (v9x_release_verify(signed_text, cut, test_public,
                                   &covered)) {
                ++accepted;
            }
        }
        RLCHECK(accepted == 0u);
    }

    /* Two [Signature] sections: refused even though the first is genuine. */
    {
        char doubled[2048];

        length = build_signed(release_body, "\r\n");
        strcpy(doubled, signed_text);
        strcat(doubled, "[Signature]\r\nEd25519=00\r\n");
        RLCHECK(!v9x_release_verify(doubled, (v9x_u32)strlen(doubled),
                                    test_public, &covered));
    }

    /* A NUL inside the covered bytes. */
    length = build_signed(release_body, "\r\n");
    signed_text[10] = '\0';
    RLCHECK(!v9x_release_verify(signed_text, length, test_public, &covered));

    /* [Signature] not at a line start does not count as the section. */
    {
        static const char inline_body[] =
            "[Velocity9xRelease]\r\nNote=x[Signature]\r\n";

        length = build_signed(inline_body, "\r\n");
        RLCHECK(v9x_release_verify(signed_text, length, test_public,
                                   &covered));
        RLCHECK(covered == strlen(inline_body));
    }
}

static void test_read(void)
{
    struct v9x_release_info info;
    v9x_u32 covered = (v9x_u32)strlen(release_body);

    RLCHECK(v9x_release_read(release_body, covered, "velocity9x", "ati",
                             &info));
    RLCHECK(strcmp(info.version, "0.16.0") == 0);
    RLCHECK(strcmp(info.build, "1a2b3c4") == 0);
    RLCHECK(strcmp(info.file, "velocity9x-0.16.0-ati.zip") == 0);
    RLCHECK(info.size == 347696ul);
    RLCHECK(info.sha256[0] == 0x8cu && info.sha256[31] == 0x69u);

    RLCHECK(v9x_release_read(release_body, covered, "velocity9x", "matrox",
                             &info));
    RLCHECK(info.size == 12ul && info.sha256[31] == 0xFFu);

    RLCHECK(!v9x_release_read(release_body, covered, "velocity9x", "s3",
                              &info));
    RLCHECK(!v9x_release_read(release_body, covered, "velocitynt", "ati",
                              &info));
    /* A family that would form another section name. */
    RLCHECK(!v9x_release_read(release_body, covered, "velocity9x",
                              "ati]\r\n[x", &info));
    RLCHECK(!v9x_release_read(release_body, covered, "velocity9x", "",
                              &info));
    /* Only the covered length is read. */
    RLCHECK(!v9x_release_read(release_body, 120u, "velocity9x", "matrox",
                              &info));

    {
        static const char schema2[] =
            "[Velocity9xRelease]\r\nSchema=2\r\nApp=velocity9x\r\n"
            "Version=0.16.0\r\n[Family.ati]\r\nFile=a.zip\r\nSize=1\r\n"
            "Sha256=0000000000000000000000000000000000000000000000000000000000000000\r\n";
        static const char traversal[] =
            "[Velocity9xRelease]\r\nSchema=1\r\nApp=velocity9x\r\n"
            "Version=0.16.0\r\n[Family.ati]\r\nFile=..\\a.zip\r\nSize=1\r\n"
            "Sha256=0000000000000000000000000000000000000000000000000000000000000000\r\n";
        static const char short_hash[] =
            "[Velocity9xRelease]\r\nSchema=1\r\nApp=velocity9x\r\n"
            "Version=0.16.0\r\n[Family.ati]\r\nFile=a.zip\r\nSize=1\r\n"
            "Sha256=00\r\n";
        static const char zero_size[] =
            "[Velocity9xRelease]\r\nSchema=1\r\nApp=velocity9x\r\n"
            "Version=0.16.0\r\n[Family.ati]\r\nFile=a.zip\r\nSize=0\r\n"
            "Sha256=0000000000000000000000000000000000000000000000000000000000000000\r\n";
        static const char bad_version[] =
            "[Velocity9xRelease]\r\nSchema=1\r\nApp=velocity9x\r\n"
            "Version=next\r\n[Family.ati]\r\nFile=a.zip\r\nSize=1\r\n"
            "Sha256=0000000000000000000000000000000000000000000000000000000000000000\r\n";

        RLCHECK(!v9x_release_read(schema2, (v9x_u32)strlen(schema2),
                                  "velocity9x", "ati", &info));
        RLCHECK(!v9x_release_read(traversal, (v9x_u32)strlen(traversal),
                                  "velocity9x", "ati", &info));
        RLCHECK(!v9x_release_read(short_hash, (v9x_u32)strlen(short_hash),
                                  "velocity9x", "ati", &info));
        RLCHECK(!v9x_release_read(zero_size, (v9x_u32)strlen(zero_size),
                                  "velocity9x", "ati", &info));
        RLCHECK(!v9x_release_read(bad_version,
                                  (v9x_u32)strlen(bad_version),
                                  "velocity9x", "ati", &info));
    }
}

unsigned int v9x_run_update_release_tests(void)
{
    v9x_ed25519_public_key(test_seed, test_public);
    test_verify();
    test_read();
    return release_failures;
}
