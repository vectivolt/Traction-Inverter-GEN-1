/* ed25519.h — Ed25519 signature verification (RFC 8032, "pure" Ed25519) for the FW-38 image check.
 * Vendored from TweetNaCl (public domain) — see ed25519_tweetnacl.c for the origin, the licence and what changed.
 * Verification only in the firmware; key derivation and signing are compiled for host builds (tests, tooling)
 * and never into a target build (TI_TARGET_S32K396). */
#ifndef ED25519_H
#define ED25519_H

#include "ti_types.h"

#define ED25519_KEY_LEN 32u
#define ED25519_SIG_LEN 64u
#define ED25519_MSG_MAX 256u /* the signed message is the 64-byte image header prefix */

/* true only for a valid signature of msg under pk: S canonical (< L), A a curve point, [S]B = R + [k]A with
 * k = SHA-512(R || A || msg) mod L, the final comparison constant-time. len > ED25519_MSG_MAX: false. */
bool ed25519_verify(const uint8_t sig[ED25519_SIG_LEN], const uint8_t *msg, uint32_t len,
                    const uint8_t pk[ED25519_KEY_LEN]);

#if !defined(TI_TARGET_S32K396)
/* Host tests only: the RFC 8032 public key of a 32-byte seed, and a signature. */
void ed25519_public_key(uint8_t pk[ED25519_KEY_LEN], const uint8_t seed[32]);
void ed25519_sign(uint8_t sig[ED25519_SIG_LEN], const uint8_t *msg, uint32_t len, const uint8_t seed[32]);
#endif

#endif /* ED25519_H */
