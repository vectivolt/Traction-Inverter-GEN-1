/* ed25519_tweetnacl.c — Ed25519 (RFC 8032) and the SHA-512 it needs, vendored for FW-38.
 *
 * ORIGIN AND LICENCE. The field arithmetic, the group law, the scalar reduction and SHA-512 below are TweetNaCl
 * (tweetnacl.c, version 20140427) by Daniel J. Bernstein, Bernard van Gastel, Wesley Janssen, Tanja Lange, Peter
 * Schwabe and Sjaak Smetsers — https://tweetnacl.cr.yp.to — released into the PUBLIC DOMAIN by its authors ("TweetNaCl
 * is a public-domain library"). No licence obligation follows; the origin is cited so a reviewer can diff it.
 *
 * CHANGES from the original (behaviour-preserving unless stated):
 *   - fixed-width types, explicit loops instead of the FOR macro, every loop bounded by a constant or by a length
 *     checked against ED25519_MSG_MAX; no recursion, no dynamic memory, no randombytes (nothing random is needed);
 *   - the left shifts of possibly negative carries (`c << 16`, `carry << 8`: undefined behaviour in C11) are
 *     multiplications; L[] is signed so modL() stays in signed 64-bit arithmetic (no overflow: |x| < 2^45);
 *   - crypto_sign_open() became ed25519_verify(): the message is not copied out, the hash input is R || A || M
 *     in a bounded buffer, and — ADDED, RFC 8032 §5.1.7 — a signature whose S is not below the group order L is
 *     refused (TweetNaCl accepted S + L, a malleability the standard forbids);
 *   - crypto_sign() / crypto_sign_keypair() became ed25519_sign() / ed25519_public_key() on a 32-byte seed, for
 *     host builds only (tests and tooling; the target image carries verification only).
 * MISRA deviations (documented, vendored code): arithmetic right shifts of negative signed values (implementation-
 * defined; every supported compiler shifts arithmetically, as TweetNaCl assumes), single-letter identifiers.
 * Timing: the ladder uses constant-time conditional swaps and the final comparison is constant-time. */
#include "ed25519.h"

#include <string.h>

typedef int64_t gf[16];

static const gf gf0 = {0};
static const gf gf1 = {1};
static const gf D = {0x78a3, 0x1359, 0x4dca, 0x75eb, 0xd8ab, 0x4141, 0x0a4d, 0x0070,
                     0xe898, 0x7779, 0x4079, 0x8cc7, 0xfe73, 0x2b6f, 0x6cee, 0x5203};
static const gf D2 = {0xf159, 0x26b2, 0x9b94, 0xebd6, 0xb156, 0x8283, 0x149a, 0x00e0,
                      0xd130, 0xeef3, 0x80f2, 0x198e, 0xfce7, 0x56df, 0xd9dc, 0x2406};
static const gf X = {0xd51a, 0x8f25, 0x2d60, 0xc956, 0xa7b2, 0x9525, 0xc760, 0x692c,
                     0xdc5c, 0xfdd6, 0xe231, 0xc0a4, 0x53fe, 0xcd6e, 0x36d3, 0x2169};
static const gf Y = {0x6658, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666,
                     0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666};
static const gf I = {0xa0b0, 0x4a0e, 0x1b27, 0xc4ee, 0xe478, 0xad2f, 0x1806, 0x2f43,
                     0xd7a7, 0x3dfb, 0x0099, 0x2b4d, 0xdf0b, 0x4fc1, 0x2480, 0x2b83};
/* the group order L = 2^252 + 27742317777372353535851937790883648493, little-endian bytes */
static const int64_t L[32] = {0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7,
                              0xa2, 0xde, 0xf9, 0xde, 0x14, 0,    0,    0,    0,    0,    0,
                              0,    0,    0,    0,    0,    0,    0,    0,    0,    0x10};

/* ---------------- SHA-512 (TweetNaCl crypto_hashblocks / crypto_hash) ---------------- */
static const uint64_t K512[80] = {
    0x428a2f98d728ae22u, 0x7137449123ef65cdu, 0xb5c0fbcfec4d3b2fu, 0xe9b5dba58189dbbcu, 0x3956c25bf348b538u,
    0x59f111f1b605d019u, 0x923f82a4af194f9bu, 0xab1c5ed5da6d8118u, 0xd807aa98a3030242u, 0x12835b0145706fbeu,
    0x243185be4ee4b28cu, 0x550c7dc3d5ffb4e2u, 0x72be5d74f27b896fu, 0x80deb1fe3b1696b1u, 0x9bdc06a725c71235u,
    0xc19bf174cf692694u, 0xe49b69c19ef14ad2u, 0xefbe4786384f25e3u, 0x0fc19dc68b8cd5b5u, 0x240ca1cc77ac9c65u,
    0x2de92c6f592b0275u, 0x4a7484aa6ea6e483u, 0x5cb0a9dcbd41fbd4u, 0x76f988da831153b5u, 0x983e5152ee66dfabu,
    0xa831c66d2db43210u, 0xb00327c898fb213fu, 0xbf597fc7beef0ee4u, 0xc6e00bf33da88fc2u, 0xd5a79147930aa725u,
    0x06ca6351e003826fu, 0x142929670a0e6e70u, 0x27b70a8546d22ffcu, 0x2e1b21385c26c926u, 0x4d2c6dfc5ac42aedu,
    0x53380d139d95b3dfu, 0x650a73548baf63deu, 0x766a0abb3c77b2a8u, 0x81c2c92e47edaee6u, 0x92722c851482353bu,
    0xa2bfe8a14cf10364u, 0xa81a664bbc423001u, 0xc24b8b70d0f89791u, 0xc76c51a30654be30u, 0xd192e819d6ef5218u,
    0xd69906245565a910u, 0xf40e35855771202au, 0x106aa07032bbd1b8u, 0x19a4c116b8d2d0c8u, 0x1e376c085141ab53u,
    0x2748774cdf8eeb99u, 0x34b0bcb5e19b48a8u, 0x391c0cb3c5c95a63u, 0x4ed8aa4ae3418acbu, 0x5b9cca4f7763e373u,
    0x682e6ff3d6b2b8a3u, 0x748f82ee5defb2fcu, 0x78a5636f43172f60u, 0x84c87814a1f0ab72u, 0x8cc702081a6439ecu,
    0x90befffa23631e28u, 0xa4506cebde82bde9u, 0xbef9a3f7b2c67915u, 0xc67178f2e372532bu, 0xca273eceea26619cu,
    0xd186b8c721c0c207u, 0xeada7dd6cde0eb1eu, 0xf57d4f7fee6ed178u, 0x06f067aa72176fbau, 0x0a637dc5a2c898a6u,
    0x113f9804bef90daeu, 0x1b710b35131c471bu, 0x28db77f523047d84u, 0x32caab7b40c72493u, 0x3c9ebe0a15c9bebcu,
    0x431d67c49c100d4cu, 0x4cc5d4becb3e42b6u, 0x597f299cfc657e2au, 0x5fcb6fab3ad6faecu, 0x6c44198c4a475817u};

static uint64_t R64(uint64_t x, uint32_t c) { return (x >> c) | (x << (64u - c)); }
static uint64_t Ch(uint64_t x, uint64_t y, uint64_t z) { return (x & y) ^ (~x & z); }
static uint64_t Maj(uint64_t x, uint64_t y, uint64_t z) { return (x & y) ^ (x & z) ^ (y & z); }
static uint64_t Sigma0(uint64_t x) { return R64(x, 28u) ^ R64(x, 34u) ^ R64(x, 39u); }
static uint64_t Sigma1(uint64_t x) { return R64(x, 14u) ^ R64(x, 18u) ^ R64(x, 41u); }
static uint64_t sigma0(uint64_t x) { return R64(x, 1u) ^ R64(x, 8u) ^ (x >> 7); }
static uint64_t sigma1(uint64_t x) { return R64(x, 19u) ^ R64(x, 61u) ^ (x >> 6); }

static uint64_t dl64(const uint8_t *x)
{
    uint64_t u = 0u;
    for (uint32_t i = 0u; i < 8u; i++) {
        u = (u << 8) | x[i];
    }
    return u;
}

static void ts64(uint8_t *x, uint64_t u)
{
    for (uint32_t i = 8u; i > 0u; i--) {
        x[i - 1u] = (uint8_t)u;
        u >>= 8;
    }
}

/* n is a multiple of 128 at every call below */
static void hashblocks(uint8_t st[64], const uint8_t *m, uint32_t n)
{
    uint64_t z[8];
    uint64_t a[8];
    uint64_t b[8];
    uint64_t w[16];
    for (uint32_t i = 0u; i < 8u; i++) {
        z[i] = dl64(&st[8u * i]);
        a[i] = z[i];
    }
    for (uint32_t off = 0u; (off + 128u) <= n; off += 128u) {
        for (uint32_t i = 0u; i < 16u; i++) {
            w[i] = dl64(&m[off + (8u * i)]);
        }
        for (uint32_t i = 0u; i < 80u; i++) {
            for (uint32_t j = 0u; j < 8u; j++) {
                b[j] = a[j];
            }
            const uint64_t t = a[7] + Sigma1(a[4]) + Ch(a[4], a[5], a[6]) + K512[i] + w[i % 16u];
            b[7] = t + Sigma0(a[0]) + Maj(a[0], a[1], a[2]);
            b[3] += t;
            for (uint32_t j = 0u; j < 8u; j++) {
                a[(j + 1u) % 8u] = b[j];
            }
            if ((i % 16u) == 15u) {
                for (uint32_t j = 0u; j < 16u; j++) {
                    w[j] += w[(j + 9u) % 16u] + sigma0(w[(j + 1u) % 16u]) + sigma1(w[(j + 14u) % 16u]);
                }
            }
        }
        for (uint32_t i = 0u; i < 8u; i++) {
            a[i] += z[i];
            z[i] = a[i];
        }
    }
    for (uint32_t i = 0u; i < 8u; i++) {
        ts64(&st[8u * i], z[i]);
    }
}

static void sha512(uint8_t out[64], const uint8_t *m, uint32_t n)
{
    static const uint8_t IV[64] = {0x6a, 0x09, 0xe6, 0x67, 0xf3, 0xbc, 0xc9, 0x08, 0xbb, 0x67, 0xae, 0x85, 0x84,
                                   0xca, 0xa7, 0x3b, 0x3c, 0x6e, 0xf3, 0x72, 0xfe, 0x94, 0xf8, 0x2b, 0xa5, 0x4f,
                                   0xf5, 0x3a, 0x5f, 0x1d, 0x36, 0xf1, 0x51, 0x0e, 0x52, 0x7f, 0xad, 0xe6, 0x82,
                                   0xd1, 0x9b, 0x05, 0x68, 0x8c, 0x2b, 0x3e, 0x6c, 0x1f, 0x1f, 0x83, 0xd9, 0xab,
                                   0xfb, 0x41, 0xbd, 0x6b, 0x5b, 0xe0, 0xcd, 0x19, 0x13, 0x7e, 0x21, 0x79};
    uint8_t h[64];
    uint8_t x[256];
    (void)memcpy(h, IV, 64u);
    const uint32_t full = n & ~127u;
    hashblocks(h, m, full);
    const uint32_t rest = n - full;
    (void)memset(x, 0, sizeof x);
    (void)memcpy(x, &m[full], rest);
    x[rest] = 128u;
    const uint32_t last = (rest < 112u) ? 128u : 256u;
    const uint64_t bits = (uint64_t)n;
    x[last - 9u] = (uint8_t)(bits >> 61);
    ts64(&x[last - 8u], bits << 3);
    hashblocks(h, x, last);
    (void)memcpy(out, h, 64u);
}

/* ---------------- field arithmetic mod 2^255 - 19 (16 limbs of 16 bits) ---------------- */
static void set25519(gf r, const gf a)
{
    for (uint32_t i = 0u; i < 16u; i++) {
        r[i] = a[i];
    }
}

static void car25519(gf o)
{
    for (uint32_t i = 0u; i < 16u; i++) {
        o[i] += (int64_t)1 << 16;
        const int64_t c = o[i] >> 16;
        if (i < 15u) {
            o[i + 1u] += c - 1;
        } else {
            o[0] += (c - 1) + (37 * (c - 1));
        }
        o[i] -= c * 65536;
    }
}

static void sel25519(gf p, gf q, int64_t b)
{
    const int64_t c = ~(b - 1);
    for (uint32_t i = 0u; i < 16u; i++) {
        const int64_t t = c & (p[i] ^ q[i]);
        p[i] ^= t;
        q[i] ^= t;
    }
}

static void pack25519(uint8_t o[32], const gf n)
{
    gf m;
    gf t;
    set25519(t, n);
    car25519(t);
    car25519(t);
    car25519(t);
    for (uint32_t j = 0u; j < 2u; j++) {
        m[0] = t[0] - 0xffed;
        for (uint32_t i = 1u; i < 15u; i++) {
            m[i] = t[i] - 0xffff - ((m[i - 1u] >> 16) & 1);
            m[i - 1u] &= 0xffff;
        }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        const int64_t b = (m[15] >> 16) & 1;
        m[14] &= 0xffff;
        sel25519(t, m, 1 - b);
    }
    for (uint32_t i = 0u; i < 16u; i++) {
        o[2u * i] = (uint8_t)(t[i] & 0xff);
        o[(2u * i) + 1u] = (uint8_t)((t[i] >> 8) & 0xff);
    }
}

/* 0 when equal, -1 otherwise; constant-time over the n bytes */
static int32_t vn(const uint8_t *x, const uint8_t *y, uint32_t n)
{
    uint32_t d = 0u;
    for (uint32_t i = 0u; i < n; i++) {
        d |= (uint32_t)(x[i] ^ y[i]);
    }
    return (int32_t)(1u & ((d - 1u) >> 8)) - 1;
}

static int32_t neq25519(const gf a, const gf b)
{
    uint8_t c[32];
    uint8_t d[32];
    pack25519(c, a);
    pack25519(d, b);
    return vn(c, d, 32u);
}

static uint8_t par25519(const gf a)
{
    uint8_t d[32];
    pack25519(d, a);
    return (uint8_t)(d[0] & 1u);
}

static void unpack25519(gf o, const uint8_t n[32])
{
    for (uint32_t i = 0u; i < 16u; i++) {
        o[i] = (int64_t)n[2u * i] + ((int64_t)n[(2u * i) + 1u] << 8);
    }
    o[15] &= 0x7fff;
}

static void A(gf o, const gf a, const gf b)
{
    for (uint32_t i = 0u; i < 16u; i++) {
        o[i] = a[i] + b[i];
    }
}

static void Z(gf o, const gf a, const gf b)
{
    for (uint32_t i = 0u; i < 16u; i++) {
        o[i] = a[i] - b[i];
    }
}

static void M(gf o, const gf a, const gf b)
{
    int64_t t[31];
    for (uint32_t i = 0u; i < 31u; i++) {
        t[i] = 0;
    }
    for (uint32_t i = 0u; i < 16u; i++) {
        for (uint32_t j = 0u; j < 16u; j++) {
            t[i + j] += a[i] * b[j];
        }
    }
    for (uint32_t i = 0u; i < 15u; i++) {
        t[i] += 38 * t[i + 16u];
    }
    for (uint32_t i = 0u; i < 16u; i++) {
        o[i] = t[i];
    }
    car25519(o);
    car25519(o);
}

static void S(gf o, const gf a) { M(o, a, a); }

static void inv25519(gf o, const gf i)
{
    gf c;
    set25519(c, i);
    for (int32_t a = 253; a >= 0; a--) {
        S(c, c);
        if ((a != 2) && (a != 4)) {
            M(c, c, i);
        }
    }
    set25519(o, c);
}

static void pow2523(gf o, const gf i)
{
    gf c;
    set25519(c, i);
    for (int32_t a = 250; a >= 0; a--) {
        S(c, c);
        if (a != 1) {
            M(c, c, i);
        }
    }
    set25519(o, c);
}

/* ---------------- the twisted Edwards group (extended coordinates) ---------------- */
static void add(gf p[4], gf q[4])
{
    gf a;
    gf b;
    gf c;
    gf d;
    gf t;
    gf e;
    gf f;
    gf g;
    gf h;
    Z(a, p[1], p[0]);
    Z(t, q[1], q[0]);
    M(a, a, t);
    A(b, p[0], p[1]);
    A(t, q[0], q[1]);
    M(b, b, t);
    M(c, p[3], q[3]);
    M(c, c, D2);
    M(d, p[2], q[2]);
    A(d, d, d);
    Z(e, b, a);
    Z(f, d, c);
    A(g, d, c);
    A(h, b, a);
    M(p[0], e, f);
    M(p[1], h, g);
    M(p[2], g, f);
    M(p[3], e, h);
}

static void cswap(gf p[4], gf q[4], uint8_t b)
{
    for (uint32_t i = 0u; i < 4u; i++) {
        sel25519(p[i], q[i], (int64_t)b);
    }
}

static void pack(uint8_t r[32], gf p[4])
{
    gf tx;
    gf ty;
    gf zi;
    inv25519(zi, p[2]);
    M(tx, p[0], zi);
    M(ty, p[1], zi);
    pack25519(r, ty);
    r[31] ^= (uint8_t)(par25519(tx) << 7);
}

static void scalarmult(gf p[4], gf q[4], const uint8_t s[32])
{
    set25519(p[0], gf0);
    set25519(p[1], gf1);
    set25519(p[2], gf1);
    set25519(p[3], gf0);
    for (int32_t i = 255; i >= 0; i--) {
        const uint8_t b = (uint8_t)((s[(uint32_t)i / 8u] >> ((uint32_t)i & 7u)) & 1u);
        cswap(p, q, b);
        add(q, p);
        add(p, p);
        cswap(p, q, b);
    }
}

static void scalarbase(gf p[4], const uint8_t s[32])
{
    gf q[4];
    set25519(q[0], X);
    set25519(q[1], Y);
    set25519(q[2], gf1);
    M(q[3], X, Y);
    scalarmult(p, q, s);
}

/* ---------------- scalars mod L ---------------- */
static void modL(uint8_t r[32], int64_t x[64])
{
    int64_t carry;
    for (int32_t i = 63; i >= 32; i--) {
        carry = 0;
        int32_t j = i - 32;
        for (; j < (i - 12); j++) {
            x[j] += carry - (16 * x[i] * L[j - (i - 32)]);
            carry = (x[j] + 128) >> 8;
            x[j] -= carry * 256;
        }
        x[j] += carry;
        x[i] = 0;
    }
    carry = 0;
    for (uint32_t j = 0u; j < 32u; j++) {
        x[j] += carry - ((x[31] >> 4) * L[j]);
        carry = x[j] >> 8;
        x[j] &= 255;
    }
    for (uint32_t j = 0u; j < 32u; j++) {
        x[j] -= carry * L[j];
    }
    for (uint32_t i = 0u; i < 32u; i++) {
        x[i + 1u] += x[i] >> 8;
        r[i] = (uint8_t)(x[i] & 255);
    }
}

static void reduce(uint8_t r[64])
{
    int64_t x[64];
    for (uint32_t i = 0u; i < 64u; i++) {
        x[i] = (int64_t)r[i];
        r[i] = 0u;
    }
    modL(r, x);
}

/* ADDED (RFC 8032 §5.1.7): S < L, compared from the most significant byte, branch-free */
static bool scalar_canonical(const uint8_t s[32])
{
    uint32_t lt = 0u;
    uint32_t eq = 1u;
    for (uint32_t k = 32u; k > 0u; k--) {
        const uint32_t si = s[k - 1u];
        const uint32_t li = (uint32_t)L[k - 1u];
        lt |= eq & ((si - li) >> 31);   /* si < li while every more significant byte was equal */
        eq &= ((si ^ li) - 1u) >> 31;   /* si == li */
    }
    return lt != 0u;
}

static int32_t unpackneg(gf r[4], const uint8_t p[32])
{
    gf t;
    gf chk;
    gf num;
    gf den;
    gf den2;
    gf den4;
    gf den6;
    set25519(r[2], gf1);
    unpack25519(r[1], p);
    S(num, r[1]);
    M(den, num, D);
    Z(num, num, r[2]);
    A(den, r[2], den);
    S(den2, den);
    S(den4, den2);
    M(den6, den4, den2);
    M(t, den6, num);
    M(t, t, den);
    pow2523(t, t);
    M(t, t, num);
    M(t, t, den);
    M(t, t, den);
    M(r[0], t, den);
    S(chk, r[0]);
    M(chk, chk, den);
    if (neq25519(chk, num) != 0) {
        M(r[0], r[0], I);
    }
    S(chk, r[0]);
    M(chk, chk, den);
    if (neq25519(chk, num) != 0) {
        return -1;
    }
    if (par25519(r[0]) == (p[31] >> 7)) {
        Z(r[0], gf0, r[0]);
    }
    M(r[3], r[0], r[1]);
    return 0;
}

bool ed25519_verify(const uint8_t sig[ED25519_SIG_LEN], const uint8_t *msg, uint32_t len,
                    const uint8_t pk[ED25519_KEY_LEN])
{
    gf p[4];
    gf q[4];
    uint8_t buf[64u + ED25519_MSG_MAX];
    uint8_t h[64];
    uint8_t t[32];
    if ((len > ED25519_MSG_MAX) || !scalar_canonical(&sig[32]) || (unpackneg(q, pk) != 0)) {
        return false;
    }
    (void)memcpy(buf, sig, 32u);
    (void)memcpy(&buf[32], pk, 32u);
    (void)memcpy(&buf[64], msg, len);
    sha512(h, buf, 64u + len);
    reduce(h);
    scalarmult(p, q, h); /* [k](-A) */
    scalarbase(q, &sig[32]);
    add(p, q);           /* [S]B - [k]A */
    pack(t, p);
    return vn(sig, t, 32u) == 0;
}

#if !defined(TI_TARGET_S32K396)
static void expand(uint8_t d[64], const uint8_t seed[32])
{
    sha512(d, seed, 32u);
    d[0] &= 248u;
    d[31] &= 127u;
    d[31] |= 64u;
}

void ed25519_public_key(uint8_t pk[ED25519_KEY_LEN], const uint8_t seed[32])
{
    uint8_t d[64];
    gf p[4];
    expand(d, seed);
    scalarbase(p, d);
    pack(pk, p);
}

void ed25519_sign(uint8_t sig[ED25519_SIG_LEN], const uint8_t *msg, uint32_t len, const uint8_t seed[32])
{
    uint8_t d[64];
    uint8_t r[64];
    uint8_t h[64];
    uint8_t buf[64u + ED25519_MSG_MAX];
    int64_t x[64];
    gf p[4];
    if (len > ED25519_MSG_MAX) {
        (void)memset(sig, 0, ED25519_SIG_LEN); /* never a valid signature */
        return;
    }
    expand(d, seed);
    (void)memcpy(&buf[32], &d[32], 32u); /* r = H(prefix || M) */
    (void)memcpy(&buf[64], msg, len);
    sha512(r, &buf[32], 32u + len);
    reduce(r);
    scalarbase(p, r);
    pack(sig, p); /* R */
    (void)memcpy(buf, sig, 32u);
    ed25519_public_key(&buf[32], seed);
    sha512(h, buf, 64u + len); /* k = H(R || A || M) */
    reduce(h);
    for (uint32_t i = 0u; i < 64u; i++) {
        x[i] = 0;
    }
    for (uint32_t i = 0u; i < 32u; i++) {
        x[i] = (int64_t)r[i];
    }
    for (uint32_t i = 0u; i < 32u; i++) {
        for (uint32_t j = 0u; j < 32u; j++) {
            x[i + j] += (int64_t)h[i] * (int64_t)d[j];
        }
    }
    modL(&sig[32], x); /* S = r + k*a mod L */
}
#endif
