/*
 * The Ed25519 public key that signs Velocity9x releases.
 *
 * build-release.ps1 signs releases\<version>\SIGNED.TXT with the matching
 * private key, which lives outside the repository (V9X_SIGNING_KEY in the
 * developer's .env); V9XUPD.EXE refuses a release whose signature does not
 * verify under this key. Generated 2026-10-09. Replacing it means every
 * installed updater from then on refuses releases signed with the new key
 * until it is itself updated by hand, so a rotation ships the new key in a
 * release still signed with the old one.
 *
 * build-release.ps1 checks that the private key derives exactly these bytes
 * before it signs anything.
 */
#ifndef VELOCITY9X_RELEASE_KEY_H
#define VELOCITY9X_RELEASE_KEY_H

#define V9X_RELEASE_PUBLIC_KEY_HEX \
    "23797e1a05cd70c5a8151ba0d5d1f6f9367bebba986c74d4b5e70db4b42851dd"

#define V9X_RELEASE_PUBLIC_KEY { \
    0x23, 0x79, 0x7e, 0x1a, 0x05, 0xcd, 0x70, 0xc5, \
    0xa8, 0x15, 0x1b, 0xa0, 0xd5, 0xd1, 0xf6, 0xf9, \
    0x36, 0x7b, 0xeb, 0xba, 0x98, 0x6c, 0x74, 0xd4, \
    0xb5, 0xe7, 0x0d, 0xb4, 0xb4, 0x28, 0x51, 0xdd }

#endif
