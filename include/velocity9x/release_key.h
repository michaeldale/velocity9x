/*
 * The Ed25519 public key that signs Velocity9x releases.
 *
 * build-release.ps1 signs releases\<version>\SIGNED.TXT with the matching
 * private key, which lives outside the repository (V9X_SIGNING_KEY,
 * docs\RELEASING.md); V9XUPD.EXE refuses a release whose signature does not
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

/*
 * The test key: public half of the seed 76397855 10010203 ... 1a1b that
 * tests\host\test_update_release.c signs with, so its private half is in
 * the repository and anybody can sign with it. Only a V9XUPD.EXE built
 * with build-update.ps1 -TestKey trusts it (its build id ends in -testkey),
 * for update-cycle tests against a local server; no package script passes
 * that switch.
 */
#define V9X_RELEASE_TEST_PUBLIC_KEY_HEX \
    "ae65f14d5858b388ebaf660b03a9bb902575609589198372ab7ca6b04922c9eb"

#ifdef V9X_RELEASE_TEST_KEY
#define V9X_RELEASE_PUBLIC_KEY { \
    0xae, 0x65, 0xf1, 0x4d, 0x58, 0x58, 0xb3, 0x88, \
    0xeb, 0xaf, 0x66, 0x0b, 0x03, 0xa9, 0xbb, 0x90, \
    0x25, 0x75, 0x60, 0x95, 0x89, 0x19, 0x83, 0x72, \
    0xab, 0x7c, 0xa6, 0xb0, 0x49, 0x22, 0xc9, 0xeb }
#else
#define V9X_RELEASE_PUBLIC_KEY { \
    0x23, 0x79, 0x7e, 0x1a, 0x05, 0xcd, 0x70, 0xc5, \
    0xa8, 0x15, 0x1b, 0xa0, 0xd5, 0xd1, 0xf6, 0xf9, \
    0x36, 0x7b, 0xeb, 0xba, 0x98, 0x6c, 0x74, 0xd4, \
    0xb5, 0xe7, 0x0d, 0xb4, 0xb4, 0x28, 0x51, 0xdd }
#endif

#endif
