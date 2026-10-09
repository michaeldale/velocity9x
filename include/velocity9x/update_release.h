/*
 * The signed release file, SIGNED.TXT
 * (docs\plans\optional-update-checker-and-auto-updater.md).
 *
 *   [Velocity9xRelease]
 *   Schema=1
 *   App=velocity9x
 *   Version=0.16.0
 *   Build=1a2b3c4
 *
 *   [Family.ati]
 *   File=velocity9x-0.16.0-ati.zip
 *   Size=347696
 *   Sha256=<64 lowercase hex>
 *
 *   [Signature]
 *   Ed25519=<128 hex>
 *
 * The signature covers every byte before the line that opens [Signature].
 * Nothing may follow its Ed25519 line, and only the signed bytes are ever
 * parsed, so nothing appended to a genuine file can change what it says.
 */
#ifndef VELOCITY9X_UPDATE_RELEASE_H
#define VELOCITY9X_UPDATE_RELEASE_H

#include "velocity9x/types.h"

#define V9X_RELEASE_SCHEMA "1"

/* Longest field values accepted; longer is refused, not truncated. */
#define V9X_RELEASE_VERSION_MAX 16u
#define V9X_RELEASE_BUILD_MAX   48u
#define V9X_RELEASE_FILE_MAX    64u
#define V9X_RELEASE_FAMILY_MAX  24u

struct v9x_release_info {
    char version[V9X_RELEASE_VERSION_MAX];
    char build[V9X_RELEASE_BUILD_MAX];
    char file[V9X_RELEASE_FILE_MAX];
    v9x_u32 size;
    v9x_u8 sha256[32];
};

/* Check the Ed25519 signature over text. On success *signed_length is the
 * number of bytes it covers, which is all the caller may parse. Refuses a
 * NUL byte anywhere, a missing or repeated [Signature] section, anything
 * after the signature line, and a bad signature. */
v9x_u16 v9x_release_verify(const char *text, v9x_u32 length,
                           const v9x_u8 public_key[32],
                           v9x_u32 *signed_length);

/* Read the release fields and the package for family from the signed bytes.
 * Refuses another schema or app, an unparsable version, a missing family, a
 * file name that is not one plain name, a size of zero, and a SHA-256 that
 * is not 64 hex digits. */
v9x_u16 v9x_release_read(const char *text, v9x_u32 signed_length,
                         const char *app, const char *family,
                         struct v9x_release_info *info);

/* 64 hex digits, either case, to 32 bytes. */
v9x_u16 v9x_release_hex_digest(const char *hex, v9x_u8 digest[32]);

#endif
