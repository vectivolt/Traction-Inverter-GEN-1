/* image.h — the FW-38 firmware image container: a 128-byte header, then the payload (tools/sign-image.mjs).
 *    off  size  field (little-endian)
 *      0     4  magic "TIFW"
 *      4     2  format version, IMG_FORMAT
 *      6     2  header length, IMG_HDR_LEN
 *      8     4  target: the SKU the payload is built for (ti_sku_t, 1..4)
 *     12     4  payload length in bytes (1 .. IMG_PAYLOAD_MAX)
 *     16     4  TI_FW_ID of the payload
 *     20     4  security version: anti-rollback, never below the device's counter (boot.h)
 *     24     8  reserved, zero
 *     32    32  SHA-256 of the payload
 *     64    64  Ed25519 signature over bytes 0..63 by the release key
 * The opposite of an unsigned bootloader that takes any CRC-correct image (docs/firmware-vs-vesc.md, gap 1): nothing
 * is installed or started unless the signature, the hash, the target and the security version all hold (verify.h). */
#ifndef IMAGE_H
#define IMAGE_H

#include "flash.h"

#define IMG_MAGIC 0x57464954u /* "TIFW" read as a little-endian u32 */
#define IMG_FORMAT 1u
#define IMG_HDR_LEN 128u
#define IMG_TBS_LEN 64u /* the signed prefix: every field and the payload hash */
#define IMG_PAYLOAD_MAX (HAL_FLASH_REGION_SIZE - IMG_HDR_LEN)

typedef struct {
    uint32_t target;
    uint32_t length;
    uint32_t fw_id;
    uint32_t sec_ver;
    uint8_t sha256[32];
    uint8_t sig[64];
} img_hdr_t;

typedef enum {
    IMG_OK = 0,
    IMG_ERR_READ,      /* the region could not be read */
    IMG_ERR_MAGIC,
    IMG_ERR_FORMAT,    /* format version or header length */
    IMG_ERR_FIELD,     /* reserved bytes not zero, or a target outside 1..4 */
    IMG_ERR_LENGTH,    /* payload length 0 or beyond the region */
    IMG_ERR_NO_KEY,    /* no release key provisioned: nothing verifies */
    IMG_ERR_SIGNATURE,
    IMG_ERR_TARGET,    /* signed for another SKU */
    IMG_ERR_ROLLBACK,  /* security version below the device's counter */
    IMG_ERR_HASH       /* the payload is not the one signed */
} img_result_t;

/* The header's structure only; nothing in it is trusted before the signature holds (img_verify). */
img_result_t img_parse(const uint8_t raw[IMG_HDR_LEN], img_hdr_t *h);

#endif /* IMAGE_H */
