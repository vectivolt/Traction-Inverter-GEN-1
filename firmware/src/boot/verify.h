/* verify.h — the FW-38 image check, one implementation for the bootloader (boot.c) and the application
 * (update.c): the header's structure, the Ed25519 signature over its first 64 bytes by the release key, then the
 * signed target and security version, then the SHA-256 of the payload read back from flash. IMG_OK only if every
 * check holds; *h is the parsed header either way. */
#ifndef VERIFY_H
#define VERIFY_H

#include "image.h"

img_result_t img_verify(hal_flash_region_t r, uint32_t target, uint32_t min_sec_ver, img_hdr_t *h);
/* The release key: the platform's OTP/HSE copy if it has one, else this build's const table; false when neither
 * holds a key (all zero: the unprovisioned default — refused, never tried as a curve point). */
bool img_pubkey(uint8_t key[32]);

/* Which root of trust verifies images (round 23, second pass): reported by DID 0xFD23 (kind, key id) and by the
 * simulator's hello — a unit whose verifier holds the host build's public TEST key is never a shippable unit
 * (QP-EOL-13 reads the DID and refuses kind 1; the tool shows a banner). Not a DTC: an always-active information DTC
 * would fail every "no active DTC" precondition (FW-39's among them) on every development unit. key_id = the first
 * four bytes of SHA-256(public key), big-endian; 0 with no key. */
typedef enum {
    IMG_ROOT_NONE = 0u,  /* no key: every image is refused */
    IMG_ROOT_TEST = 1u,  /* the host build's TEST key (tools/sign-image.mjs --key test) */
    IMG_ROOT_BUILD = 2u, /* the release build's -DTI_FW38_PUBKEY */
    IMG_ROOT_OTP = 3u    /* the platform's OTP/HSE copy (hal_flash_pubkey) */
} img_root_t;
img_root_t img_root(uint32_t *key_id);

#endif /* VERIFY_H */
