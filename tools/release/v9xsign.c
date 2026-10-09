/*
 * v9xsign - sign and check Velocity9x release files on the developer's PC.
 *
 *   v9xsign public <seedhex64>                    print the public key
 *   v9xsign sign   <seedhex64> <file>             print the signature
 *   v9xsign verify <pubhex64> <sighex128> <file>  exit 0 valid, 1 not
 *
 * The updater checks these signatures with the same src\common\ed25519.c,
 * so a file this tool signs and then verifies is a file V9XUPD.EXE accepts.
 * Host-only: it uses stdio and malloc, which the shared modules may not.
 * Signing is not constant-time (see include\velocity9x\ed25519.h); run it
 * on the developer's own machine, never on a shared one.
 *
 * Build: wcl386 -bt=nt -zq -wx -we -i=include tools\release\v9xsign.c
 *        src\common\ed25519.c src\common\sha512.c
 *
 * <seedhex64> may be the word env, which reads V9X_SIGNING_KEY instead.
 *
 * Exit status: 0 success (or a valid signature), 1 an invalid signature,
 * 2 a usage, argument or file error.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "velocity9x/ed25519.h"

#define V9XSIGN_EXIT_OK      0
#define V9XSIGN_EXIT_INVALID 1
#define V9XSIGN_EXIT_ERROR   2

/* Release files are small; the cap keeps a mistaken argument (a zip, a
 * disk image) from being read whole into memory. */
#define V9XSIGN_MAX_FILE_BYTES (16ul * 1024ul * 1024ul)

static void v9xsign_usage(void)
{
    fputs("usage: v9xsign public <seedhex64|env>\n"
          "       v9xsign sign <seedhex64|env> <file>\n"
          "       v9xsign verify <pubhex64> <sighex128> <file>\n",
          stderr);
}

static int v9xsign_hex_digit(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/* Exactly 2 * count hex digits into count bytes, or V9X_FALSE. */
static v9x_u16 v9xsign_parse_hex(const char *text, v9x_u8 *out, size_t count)
{
    size_t i;
    int high;
    int low;

    if (strlen(text) != 2u * count) {
        return V9X_FALSE;
    }
    for (i = 0u; i < count; ++i) {
        high = v9xsign_hex_digit(text[2u * i]);
        low = v9xsign_hex_digit(text[2u * i + 1u]);
        if (high < 0 || low < 0) {
            return V9X_FALSE;
        }
        out[i] = (v9x_u8)((high << 4) | low);
    }
    return V9X_TRUE;
}

static void v9xsign_print_hex(const v9x_u8 *bytes, size_t count)
{
    size_t i;

    for (i = 0u; i < count; ++i) {
        printf("%02x", (unsigned int)bytes[i]);
    }
    putchar('\n');
}

/* Read a whole file in binary mode. Returns a malloc'd buffer (at least one
 * byte, so an empty file is not a null) or NULL with a message printed. */
static v9x_u8 *v9xsign_read_file(const char *path, v9x_u32 *length)
{
    FILE *file;
    long size;
    v9x_u8 *buffer;

    file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "v9xsign: cannot open %s\n", path);
        return NULL;
    }
    if (fseek(file, 0L, SEEK_END) != 0 || (size = ftell(file)) < 0L ||
        fseek(file, 0L, SEEK_SET) != 0) {
        fprintf(stderr, "v9xsign: cannot size %s\n", path);
        fclose(file);
        return NULL;
    }
    if ((unsigned long)size > V9XSIGN_MAX_FILE_BYTES) {
        fprintf(stderr, "v9xsign: %s is larger than %lu bytes\n", path,
                V9XSIGN_MAX_FILE_BYTES);
        fclose(file);
        return NULL;
    }
    buffer = (v9x_u8 *)malloc((size_t)size + 1u);
    if (buffer == NULL) {
        fprintf(stderr, "v9xsign: out of memory reading %s\n", path);
        fclose(file);
        return NULL;
    }
    if (fread(buffer, 1u, (size_t)size, file) != (size_t)size) {
        fprintf(stderr, "v9xsign: cannot read %s\n", path);
        free(buffer);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *length = (v9x_u32)size;
    return buffer;
}

static int v9xsign_public(const char *seed_hex)
{
    v9x_u8 seed[32];
    v9x_u8 public_key[32];

    if (!v9xsign_parse_hex(seed_hex, seed, sizeof(seed))) {
        fputs("v9xsign: the seed must be 64 hex digits\n", stderr);
        return V9XSIGN_EXIT_ERROR;
    }
    v9x_ed25519_public_key(seed, public_key);
    v9xsign_print_hex(public_key, sizeof(public_key));
    return V9XSIGN_EXIT_OK;
}

static int v9xsign_sign(const char *seed_hex, const char *path)
{
    v9x_u8 seed[32];
    v9x_u8 public_key[32];
    v9x_u8 signature[64];
    v9x_u8 *message;
    v9x_u32 length;

    if (!v9xsign_parse_hex(seed_hex, seed, sizeof(seed))) {
        fputs("v9xsign: the seed must be 64 hex digits\n", stderr);
        return V9XSIGN_EXIT_ERROR;
    }
    message = v9xsign_read_file(path, &length);
    if (message == NULL) {
        return V9XSIGN_EXIT_ERROR;
    }
    v9x_ed25519_public_key(seed, public_key);
    v9x_ed25519_sign(seed, public_key, message, length, signature);

    /* Check the signature before printing it: a signature that does not
     * verify here would be published and then refused by every updater. */
    if (!v9x_ed25519_verify(signature, public_key, message, length)) {
        fputs("v9xsign: internal error, the new signature does not verify\n",
              stderr);
        free(message);
        return V9XSIGN_EXIT_ERROR;
    }
    free(message);
    v9xsign_print_hex(signature, sizeof(signature));
    return V9XSIGN_EXIT_OK;
}

static int v9xsign_verify(const char *public_hex,
                          const char *signature_hex,
                          const char *path)
{
    v9x_u8 public_key[32];
    v9x_u8 signature[64];
    v9x_u8 *message;
    v9x_u32 length;
    v9x_u16 valid;

    if (!v9xsign_parse_hex(public_hex, public_key, sizeof(public_key))) {
        fputs("v9xsign: the public key must be 64 hex digits\n", stderr);
        return V9XSIGN_EXIT_ERROR;
    }
    if (!v9xsign_parse_hex(signature_hex, signature, sizeof(signature))) {
        fputs("v9xsign: the signature must be 128 hex digits\n", stderr);
        return V9XSIGN_EXIT_ERROR;
    }
    message = v9xsign_read_file(path, &length);
    if (message == NULL) {
        return V9XSIGN_EXIT_ERROR;
    }
    valid = v9x_ed25519_verify(signature, public_key, message, length);
    free(message);
    if (!valid) {
        puts("INVALID");
        return V9XSIGN_EXIT_INVALID;
    }
    puts("VALID");
    return V9XSIGN_EXIT_OK;
}

/* "env" in place of a seed reads it from V9X_SIGNING_KEY, so the private
 * key never appears on a command line another process can list. */
static const char *v9xsign_seed_text(const char *argument)
{
    const char *value;

    if (strcmp(argument, "env") != 0) {
        return argument;
    }
    value = getenv("V9X_SIGNING_KEY");
    return value != 0 ? value : "";
}

int main(int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[1], "public") == 0) {
        return v9xsign_public(v9xsign_seed_text(argv[2]));
    }
    if (argc == 4 && strcmp(argv[1], "sign") == 0) {
        return v9xsign_sign(v9xsign_seed_text(argv[2]), argv[3]);
    }
    if (argc == 5 && strcmp(argv[1], "verify") == 0) {
        return v9xsign_verify(argv[2], argv[3], argv[4]);
    }
    v9xsign_usage();
    return V9XSIGN_EXIT_ERROR;
}
