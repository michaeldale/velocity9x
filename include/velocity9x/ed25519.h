/*
 * Ed25519 signatures, as specified by RFC 8032 section 5.1.
 *
 * The updater (V9XUPD.EXE) checks one signature over the release file before
 * it believes any SHA-256 that file lists; the developer's signing tool
 * (tools\release\v9xsign.c) produces that signature with the same code. One
 * implementation on both sides means a vector that passes in the host tests
 * passes in both.
 *
 * Written for clarity on a Pentium with no 64-bit type, not for speed: field
 * elements are 32 limbs of 8 bits each in v9x_u32, and every operation is
 * the textbook one. Pure computation, no C library call, no state between
 * calls.
 *
 * Signing is not constant-time - its run time depends on secret scalar bits.
 * That is acceptable only because signing happens offline on the developer's
 * own PC, where nobody else can time it. Verification handles only public
 * data, so the question does not arise there.
 */
#ifndef VELOCITY9X_ED25519_H
#define VELOCITY9X_ED25519_H

#include "velocity9x/types.h"

#define V9X_ED25519_SEED_BYTES       ((v9x_u16)32u)
#define V9X_ED25519_PUBLIC_KEY_BYTES ((v9x_u16)32u)
#define V9X_ED25519_SIGNATURE_BYTES  ((v9x_u16)64u)

/*
 * RFC 8032 5.1.7. V9X_TRUE only when signature (R || S) is valid for message
 * under public_key.
 *
 * Refused, each as RFC 8032 requires: S >= L (step 1, the malleability
 * check), an R or public key that does not decode under 5.1.3 - including a
 * y coordinate that is not reduced mod p - and any signature for which the
 * cofactorless equation [S]B == R + [k]A does not hold. The two sides are
 * compared by their encodings, which with strict decoding is point equality.
 *
 * message may be null only when length is zero.
 */
v9x_u16 v9x_ed25519_verify(const v9x_u8 signature[64],
                           const v9x_u8 public_key[32],
                           const v9x_u8 *message,
                           v9x_u32 length);

/* RFC 8032 5.1.5: the public key for a 32-byte private key (the "seed"). */
void v9x_ed25519_public_key(const v9x_u8 seed[32], v9x_u8 public_key[32]);

/*
 * RFC 8032 5.1.6: sign message with seed. public_key must be the one
 * v9x_ed25519_public_key derives from seed - it is hashed into k, and a
 * mismatched key produces a signature that does not verify under either.
 * Not constant-time; see the note at the top of this file.
 */
void v9x_ed25519_sign(const v9x_u8 seed[32],
                      const v9x_u8 public_key[32],
                      const v9x_u8 *message,
                      v9x_u32 length,
                      v9x_u8 signature[64]);

#endif /* VELOCITY9X_ED25519_H */
