/* sha256.h — SHA-256 (FIPS 180-4), incremental: the FW-38 image hash (the payload is read from flash in chunks).
 * Written for this repository from the standard; no dynamic memory, every loop bounded by its input length. */
#ifndef SHA256_H
#define SHA256_H

#include "ti_types.h"

#define SHA256_LEN 32u

typedef struct {
    uint32_t h[8];
    uint64_t bytes;  /* message length so far */
    uint8_t buf[64];
    uint32_t fill;   /* bytes in buf */
} sha256_t;

void sha256_init(sha256_t *c);
void sha256_update(sha256_t *c, const uint8_t *d, uint32_t n);
void sha256_final(sha256_t *c, uint8_t out[SHA256_LEN]);
void sha256(const uint8_t *d, uint32_t n, uint8_t out[SHA256_LEN]);

#endif /* SHA256_H */
