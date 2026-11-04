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
 *   HardwareIds=PCI\VEN_1002&DEV_4742,PCI\VEN_1002&DEV_9874
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
/* "PCI\VEN_xxxx&DEV_xxxx" and its NUL. */
#define V9X_RELEASE_PCI_ID_MAX  22u

/* The family that drives any VGA-class card through its BIOS (PCI\CC_0300).
 * It is the one family an update may move a card off: see
 * v9x_release_family_for. */
#define V9X_RELEASE_FALLBACK_FAMILY "vbe"

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

/* The vendor and device part of a PnP hardware key, "PCI\VEN_1002&DEV_9874"
 * from "PCI\VEN_1002&DEV_9874&SUBSYS_...\BUS_00&DEV_01&FUNC_00", with the
 * hex in upper case. V9X_FALSE for a key that does not start that way. */
v9x_u16 v9x_release_pci_id(const char *hardware_key,
                           char pci_id[V9X_RELEASE_PCI_ID_MAX]);

/*
 * The family an update installs. A card installed on the fallback family
 * moves to the one chip family whose HardwareIds lists pci_id: the INF binds
 * vbe through the PCI\CC_0300 compatible ID, so a card on it either had no
 * chip family when it was installed or was put there by hand, and the
 * family that names the card is the driver meant for it. Any other family
 * stays as installed - one family per vendor means a chip family is never
 * replaced by another. Two families listing the same ID (the build refuses
 * that) leave the card where it is. family receives installed or the new
 * family; V9X_TRUE only when they differ.
 */
v9x_u16 v9x_release_family_for(const char *text, v9x_u32 signed_length,
                               const char *installed, const char *pci_id,
                               char *family, v9x_u32 capacity);

/* 64 hex digits, either case, to 32 bytes. */
v9x_u16 v9x_release_hex_digest(const char *hex, v9x_u8 digest[32]);

#endif
