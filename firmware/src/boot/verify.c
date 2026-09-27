/* verify.c — see verify.h. */
#include "verify.h"

#include <string.h>

#include "ed25519.h"
#include "sha256.h"

#define CHUNK 256u

/* The release key's public half. A host build (tests) carries the TEST key — the public half of
 * `tools/sign-image.mjs --key test`, derived from a public string — which a target build never contains. A target
 * build takes the platform's OTP/HSE copy (hal_flash_pubkey) or the release build's -DTI_FW38_PUBKEY=<32 bytes,
 * comma-separated: `sign-image.mjs --pubkey`>; with neither the table stays zero and every image is refused. */
#if defined(TI_FW38_PUBKEY)
static const uint8_t KEY[ED25519_KEY_LEN] = {TI_FW38_PUBKEY};
#elif !defined(TI_TARGET_S32K396)
static const uint8_t KEY[ED25519_KEY_LEN] = {0x92u, 0x21u, 0x00u, 0xc3u, 0xd2u, 0x2bu, 0x42u, 0x35u,  /* TEST ONLY */
                                             0xe8u, 0xeau, 0xe4u, 0xdau, 0xc0u, 0xd4u, 0xfbu, 0x42u,
                                             0xa7u, 0x66u, 0xa0u, 0x08u, 0xd7u, 0x11u, 0xddu, 0x08u,
                                             0xc3u, 0xe9u, 0x2au, 0x65u, 0xf0u, 0x9eu, 0x7du, 0x15u};
#else
static const uint8_t KEY[ED25519_KEY_LEN] = {0u}; /* TODO(REL): -DTI_FW38_PUBKEY or the HSE/OTP copy */
#endif

bool img_pubkey(uint8_t key[32])
{
    if (!hal_flash_pubkey(key)) {
        (void)memcpy(key, KEY, ED25519_KEY_LEN);
    }
    uint8_t any = 0u;
    for (uint32_t i = 0u; i < ED25519_KEY_LEN; i++) {
        any |= key[i];
    }
    return any != 0u;
}

img_root_t img_root(uint32_t *key_id)
{
    uint8_t key[ED25519_KEY_LEN];
    uint8_t h[SHA256_LEN];
    img_root_t kind;
    if (hal_flash_pubkey(key)) {
        kind = IMG_ROOT_OTP;
    } else {
        (void)memcpy(key, KEY, ED25519_KEY_LEN);
#if defined(TI_FW38_PUBKEY)
        kind = IMG_ROOT_BUILD;
#elif !defined(TI_TARGET_S32K396)
        kind = IMG_ROOT_TEST;
#else
        kind = IMG_ROOT_NONE;
#endif
    }
    uint8_t any = 0u;
    for (uint32_t i = 0u; i < ED25519_KEY_LEN; i++) {
        any |= key[i];
    }
    if (any == 0u) {
        kind = IMG_ROOT_NONE;
        *key_id = 0u;
    } else {
        sha256(key, ED25519_KEY_LEN, h);
        *key_id = ((uint32_t)h[0] << 24) | ((uint32_t)h[1] << 16) | ((uint32_t)h[2] << 8) | (uint32_t)h[3];
    }
    return kind;
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t le16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }

img_result_t img_parse(const uint8_t raw[IMG_HDR_LEN], img_hdr_t *h)
{
    if (le32(&raw[0]) != IMG_MAGIC) {
        return IMG_ERR_MAGIC;
    }
    if ((le16(&raw[4]) != IMG_FORMAT) || (le16(&raw[6]) != IMG_HDR_LEN)) {
        return IMG_ERR_FORMAT;
    }
    h->target = le32(&raw[8]);
    h->length = le32(&raw[12]);
    h->fw_id = le32(&raw[16]);
    h->sec_ver = le32(&raw[20]);
    (void)memcpy(h->sha256, &raw[32], 32u);
    (void)memcpy(h->sig, &raw[64], 64u);
    uint8_t rsv = 0u;
    for (uint32_t i = 24u; i < 32u; i++) {
        rsv |= raw[i];
    }
    if ((rsv != 0u) || (h->target == (uint32_t)TI_SKU_NONE) || (h->target >= (uint32_t)TI_SKU_COUNT)) {
        return IMG_ERR_FIELD;
    }
    if ((h->length == 0u) || (h->length > IMG_PAYLOAD_MAX)) {
        return IMG_ERR_LENGTH;
    }
    return IMG_OK;
}

img_result_t img_verify(hal_flash_region_t r, uint32_t target, uint32_t min_sec_ver, img_hdr_t *h)
{
    uint8_t raw[IMG_HDR_LEN];
    uint8_t key[ED25519_KEY_LEN];
    if (!hal_flash_read(r, 0u, raw, IMG_HDR_LEN)) {
        return IMG_ERR_READ;
    }
    const img_result_t v = img_parse(raw, h);
    if (v != IMG_OK) {
        return v;
    }
    if (!img_pubkey(key)) {
        return IMG_ERR_NO_KEY;
    }
    if (!ed25519_verify(h->sig, raw, IMG_TBS_LEN, key)) {
        return IMG_ERR_SIGNATURE;
    }
    if (h->target != target) { /* signed fields from here on */
        return IMG_ERR_TARGET;
    }
    if (h->sec_ver < min_sec_ver) {
        return IMG_ERR_ROLLBACK;
    }
    sha256_t c;
    uint8_t buf[CHUNK];
    sha256_init(&c);
    for (uint32_t off = 0u; off < h->length; off += CHUNK) { /* length <= IMG_PAYLOAD_MAX (img_parse) */
        const uint32_t n = ((h->length - off) < CHUNK) ? (h->length - off) : CHUNK;
        if (!hal_flash_read(r, IMG_HDR_LEN + off, buf, n)) {
            return IMG_ERR_READ;
        }
        sha256_update(&c, buf, n);
    }
    uint8_t d[SHA256_LEN];
    sha256_final(&c, d);
    uint8_t diff = 0u;
    for (uint32_t i = 0u; i < SHA256_LEN; i++) {
        diff |= (uint8_t)(d[i] ^ h->sha256[i]);
    }
    return (diff == 0u) ? IMG_OK : IMG_ERR_HASH;
}
