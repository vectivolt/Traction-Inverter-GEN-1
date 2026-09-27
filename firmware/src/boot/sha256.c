/* sha256.c — see sha256.h. */
#include "sha256.h"

static const uint32_t K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

static uint32_t ror(uint32_t x, uint32_t c) { return (x >> c) | (x << (32u - c)); }

static void block(uint32_t h[8], const uint8_t p[64])
{
    uint32_t w[64];
    for (uint32_t i = 0u; i < 16u; i++) {
        w[i] = ((uint32_t)p[4u * i] << 24) | ((uint32_t)p[(4u * i) + 1u] << 16) | ((uint32_t)p[(4u * i) + 2u] << 8) |
               (uint32_t)p[(4u * i) + 3u];
    }
    for (uint32_t i = 16u; i < 64u; i++) {
        const uint32_t s0 = ror(w[i - 15u], 7u) ^ ror(w[i - 15u], 18u) ^ (w[i - 15u] >> 3);
        const uint32_t s1 = ror(w[i - 2u], 17u) ^ ror(w[i - 2u], 19u) ^ (w[i - 2u] >> 10);
        w[i] = w[i - 16u] + s0 + w[i - 7u] + s1;
    }
    uint32_t v[8];
    for (uint32_t i = 0u; i < 8u; i++) {
        v[i] = h[i];
    }
    for (uint32_t i = 0u; i < 64u; i++) {
        const uint32_t t1 = v[7] + (ror(v[4], 6u) ^ ror(v[4], 11u) ^ ror(v[4], 25u)) + ((v[4] & v[5]) ^ (~v[4] & v[6])) +
                            K[i] + w[i];
        const uint32_t t2 = (ror(v[0], 2u) ^ ror(v[0], 13u) ^ ror(v[0], 22u)) + ((v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]));
        for (uint32_t j = 7u; j > 0u; j--) {
            v[j] = v[j - 1u];
        }
        v[4] += t1;
        v[0] = t1 + t2;
    }
    for (uint32_t i = 0u; i < 8u; i++) {
        h[i] += v[i];
    }
}

void sha256_init(sha256_t *c)
{
    static const uint32_t IV[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                                   0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    *c = (sha256_t){0};
    for (uint32_t i = 0u; i < 8u; i++) {
        c->h[i] = IV[i];
    }
}

void sha256_update(sha256_t *c, const uint8_t *d, uint32_t n)
{
    for (uint32_t i = 0u; i < n; i++) {
        c->buf[c->fill] = d[i];
        c->fill++;
        if (c->fill == 64u) {
            block(c->h, c->buf);
            c->fill = 0u;
        }
    }
    c->bytes += n;
}

void sha256_final(sha256_t *c, uint8_t out[SHA256_LEN])
{
    const uint64_t bits = c->bytes * 8u;
    const uint8_t one = 0x80u;
    const uint8_t zero = 0u;
    sha256_update(c, &one, 1u);
    while (c->fill != 56u) { /* at most 63 iterations */
        sha256_update(c, &zero, 1u);
    }
    uint8_t len[8];
    for (uint32_t i = 0u; i < 8u; i++) {
        len[i] = (uint8_t)(bits >> (56u - (8u * i)));
    }
    sha256_update(c, len, 8u);
    for (uint32_t i = 0u; i < 8u; i++) {
        out[4u * i] = (uint8_t)(c->h[i] >> 24);
        out[(4u * i) + 1u] = (uint8_t)(c->h[i] >> 16);
        out[(4u * i) + 2u] = (uint8_t)(c->h[i] >> 8);
        out[(4u * i) + 3u] = (uint8_t)c->h[i];
    }
}

void sha256(const uint8_t *d, uint32_t n, uint8_t out[SHA256_LEN])
{
    sha256_t c;
    sha256_init(&c);
    sha256_update(&c, d, n);
    sha256_final(&c, out);
}
