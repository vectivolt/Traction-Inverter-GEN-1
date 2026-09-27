/* test_update.c — FW-38, the firmware update: SHA-256 and the vendored Ed25519 against their standards; the Node
 * tool's container verified by the firmware byte for byte; every refusal with its reason; the header parser fuzzed;
 * the bootloader's decision on the host flash model (install, trial, commit and the counter; a power loss at every
 * write of the swap and of the commit; torn record writes; a failed first boot; EXEC corrupted); the programming
 * services' protocol; and in the application: refused while armed, with HV present or while moving, a state that
 * cannot arm, an end-to-end update over UDS (and a first boot that never confirms). Images are signed at test time
 * with the TEST key (tools/sign-image.mjs --key test: a key derived from a public string, never a release key). */
#include <string.h>

#include "boot.h"
#include "can_cmd.h"
#include "dtc.h"
#include "ed25519.h"
#include "harness.h"
#include "nvlog.h"
#include "sha256.h"
#include "sim_flash.h"
#include "test.h"
#include "uds.h"
#include "uds_update.h"
#include "update.h"
#include "verify.h"

#define STAGE HAL_FLASH_STAGE
#define EXEC HAL_FLASH_EXEC
#define LKG HAL_FLASH_LKG
#define IMG_BUF (IMG_HDR_LEN + 20000u)

static uint8_t s_a[IMG_BUF];
static uint8_t s_b[IMG_BUF];
static uint32_t s_alen;
static uint32_t s_blen;
static uint32_t s_lcg;

static uint32_t rnd(void)
{
    s_lcg = (s_lcg * 1664525u) + 1013904223u;
    return s_lcg >> 8;
}

static void hex(const char *h, uint8_t *o, uint32_t n)
{
    for (uint32_t i = 0u; i < n; i++) {
        const char c[2] = {h[2u * i], h[(2u * i) + 1u]};
        uint8_t v = 0u;
        for (uint32_t k = 0u; k < 2u; k++) {
            v = (uint8_t)((v << 4) | (uint8_t)((c[k] <= '9') ? (c[k] - '0') : ((c[k] | 0x20) - 'a' + 10)));
        }
        o[i] = v;
    }
}

static void test_seed(uint8_t seed[32])
{
    static const char STR[] = "TI FW-38 TEST KEY - NOT FOR RELEASE"; /* the tool's --key test */
    sha256((const uint8_t *)STR, (uint32_t)(sizeof STR - 1u), seed);
}

static void le32put(uint8_t *p, uint32_t v)
{
    for (uint32_t i = 0u; i < 4u; i++) {
        p[i] = (uint8_t)(v >> (8u * i));
    }
}

/* A container as tools/sign-image.mjs builds it; payload byte i = i * 7 + pat; signed with seed (NULL: the test
 * key). Returns its length. */
static uint32_t build(uint8_t *out, uint32_t target, uint32_t fw_id, uint32_t sec_ver, uint32_t plen, uint8_t pat,
                      const uint8_t *seed)
{
    uint8_t k[32];
    test_seed(k);
    (void)memset(out, 0, IMG_HDR_LEN);
    (void)memcpy(out, "TIFW", 4u);
    out[4] = 1u;
    out[6] = (uint8_t)IMG_HDR_LEN;
    le32put(&out[8], target);
    le32put(&out[12], plen);
    le32put(&out[16], fw_id);
    le32put(&out[20], sec_ver);
    for (uint32_t i = 0u; i < plen; i++) {
        out[IMG_HDR_LEN + i] = (uint8_t)((i * 7u) + pat);
    }
    sha256(&out[IMG_HDR_LEN], plen, &out[32]);
    ed25519_sign(&out[64], out, IMG_TBS_LEN, (seed != NULL) ? seed : k);
    return IMG_HDR_LEN + plen;
}

static void put(hal_flash_region_t r, const uint8_t *img, uint32_t len) { (void)memcpy(sim_flash_mem(r), img, len); }
static bool holds(hal_flash_region_t r, const uint8_t *img, uint32_t len) { return memcmp(sim_flash_mem(r), img, len) == 0; }

static void drain(void)
{
    for (int k = 0; (k < 200) && !nv_idle(); k++) {
        nv_service();
    }
}

static boot_rec_t rec(void)
{
    boot_rec_t r;
    (void)memset(&r, 0, sizeof r);
    (void)boot_rec_read(&r);
    return r;
}

/* The record written directly (an EOL station, or the state a test needs). */
static void set_rec(boot_state_t st, uint32_t counter, uint8_t lkg_valid)
{
    boot_rec_t r;
    boot_rec_make(&r, TI_SKU_8XX_SIC, counter);
    r.state = (uint8_t)st;
    r.lkg_valid = lkg_valid;
    CHECK(boot_rec_queue(&r));
    drain();
}

/* The application's record write (update.c does the same): the state it asks for. */
static void request(boot_state_t st)
{
    boot_rec_t r = rec();
    r.state = (uint8_t)st;
    CHECK(boot_rec_queue(&r));
    drain();
}

/* A card in service: `a` committed in EXEC and LKG, the counter at its security version. */
static void device(const uint8_t *a, uint32_t alen, uint32_t counter)
{
    sim_flash_wipe();
    nv_init();
    put(EXEC, a, alen);
    put(LKG, a, alen);
    set_rec(BOOT_ST_IDLE, counter, 1u);
}

/* A power-up: the bootloader's decision. */
static boot_result_t reboot(boot_rec_t *r)
{
    sim_flash_power_on();
    nv_init();
    return boot_decide(r);
}

/* ---------------- the check ---------------- */

TEST(sha256_and_ed25519_match_the_standard_vectors)
{
    static const struct {
        const char *m;
        const char *d;
    } V[3] = {{"", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
              {"abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
              {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
               "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"}};
    uint8_t d[32];
    uint8_t e[32];
    for (uint32_t i = 0u; i < 3u; i++) {
        sha256((const uint8_t *)V[i].m, (uint32_t)strlen(V[i].m), d);
        hex(V[i].d, e, 32u);
        CHECK(memcmp(d, e, 32u) == 0);
    }
    uint8_t a[1000]; /* one million 'a' in 1000 updates: the incremental path across every block boundary */
    (void)memset(a, 'a', sizeof a);
    sha256_t c;
    sha256_init(&c);
    for (uint32_t k = 0u; k < 1000u; k++) {
        sha256_update(&c, a, (uint32_t)sizeof a);
    }
    sha256_final(&c, d);
    hex("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", e, 32u);
    CHECK(memcmp(d, e, 32u) == 0);
    /* RFC 8032 §7.1, TEST 1: the key, the (deterministic) signature, the verification */
    uint8_t seed[32];
    uint8_t pk[32];
    uint8_t sig[64];
    uint8_t mine[64];
    hex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60", seed, 32u);
    hex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", pk, 32u);
    hex("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe"
        "24655141438e7a100b",
        sig, 64u);
    ed25519_public_key(mine, seed);
    CHECK(memcmp(mine, pk, 32u) == 0);
    const uint8_t none = 0u;
    ed25519_sign(mine, &none, 0u, seed);
    CHECK(memcmp(mine, sig, 64u) == 0);
    CHECK(ed25519_verify(sig, &none, 0u, pk));
    /* the message, R, S and the key each matter */
    const uint8_t r1 = 0x72u;
    CHECK(!ed25519_verify(sig, &r1, 1u, pk));
    const uint32_t at[3] = {0u, 40u, 63u};
    for (uint32_t k = 0u; k < 3u; k++) {
        sig[at[k]] ^= 0x01u;
        CHECK(!ed25519_verify(sig, &none, 0u, pk));
        sig[at[k]] ^= 0x01u;
    }
    pk[5] ^= 0x10u;
    CHECK(!ed25519_verify(sig, &none, 0u, pk));
    pk[5] ^= 0x10u;
    /* RFC 8032 §5.1.7: S + L satisfies the same equation mod L — refused (the TweetNaCl original accepted it) */
    static const uint8_t L[32] = {0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7,
                                  0xa2, 0xde, 0xf9, 0xde, 0x14, 0,    0,    0,    0,    0,    0,
                                  0,    0,    0,    0,    0,    0,    0,    0,    0,    0x10};
    uint32_t carry = 0u;
    for (uint32_t i = 0u; i < 32u; i++) {
        carry += (uint32_t)sig[32u + i] + L[i];
        mine[32u + i] = (uint8_t)carry;
        carry >>= 8;
    }
    (void)memcpy(mine, sig, 32u);
    CHECK(carry == 0u && !ed25519_verify(mine, &none, 0u, pk));
    CHECK(!ed25519_verify(sig, s_a, ED25519_MSG_MAX + 1u, pk));
}

/* node -e "const b=Buffer.alloc(300);for(let i=0;i<300;i++)b[i]=(i*7+3)&255;require('fs').writeFileSync('p.bin',b)"
 * node tools/sign-image.mjs --in p.bin --out p.tifw --key test --target 8xx-sic --fw-id 0x0A0F0014 --sec-ver 7
 * — the first 128 bytes of p.tifw (the header; the payload is the pattern above). */
static const uint8_t GOLDEN[IMG_HDR_LEN] = {
    0x54u, 0x49u, 0x46u, 0x57u, 0x01u, 0x00u, 0x80u, 0x00u, 0x01u, 0x00u, 0x00u, 0x00u, 0x2cu, 0x01u, 0x00u, 0x00u,
    0x14u, 0x00u, 0x0fu, 0x0au, 0x07u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    0x04u, 0x77u, 0x3fu, 0x87u, 0x26u, 0xc8u, 0x1cu, 0xafu, 0xcfu, 0xa1u, 0xa0u, 0x9au, 0x82u, 0x66u, 0x4bu, 0x98u,
    0xb0u, 0x0du, 0x20u, 0x21u, 0x03u, 0x1au, 0x17u, 0x15u, 0xbcu, 0xa1u, 0x15u, 0x4fu, 0x2du, 0xadu, 0x34u, 0x72u,
    0x30u, 0x76u, 0x45u, 0xd0u, 0xfbu, 0x24u, 0xebu, 0x4cu, 0xedu, 0x31u, 0x89u, 0x78u, 0x37u, 0x2au, 0x1eu, 0xe3u,
    0x73u, 0xc1u, 0xc6u, 0xa8u, 0x52u, 0xffu, 0xdfu, 0x35u, 0xafu, 0x0du, 0x70u, 0xcdu, 0x80u, 0xfbu, 0xdau, 0xf7u,
    0xdcu, 0x18u, 0xf4u, 0x0du, 0xabu, 0xd7u, 0x31u, 0x56u, 0xe5u, 0xf0u, 0xd8u, 0x36u, 0x13u, 0x95u, 0xfeu, 0x0eu,
    0x8du, 0xf3u, 0xe7u, 0x66u, 0xe6u, 0xbfu, 0x1cu, 0xfdu, 0xd0u, 0xa0u, 0x4fu, 0xe6u, 0xf6u, 0x2cu, 0xc1u, 0x0du};

TEST(the_tool_and_the_firmware_agree_on_format_key_and_signature)
{
    uint8_t seed[32];
    uint8_t pk[32];
    uint8_t key[32];
    img_hdr_t h;
    sim_flash_wipe();
    test_seed(seed);
    ed25519_public_key(pk, seed);
    CHECK(img_pubkey(key) && (memcmp(key, pk, 32u) == 0)); /* the host build's table holds the test key */
    const uint32_t n = build(s_a, TI_SKU_8XX_SIC, 0x0A0F0014u, 7u, 300u, 3u, NULL);
    CHECK(n == 428u && memcmp(s_a, GOLDEN, IMG_HDR_LEN) == 0); /* byte for byte, the signature included */
    put(STAGE, GOLDEN, IMG_HDR_LEN); /* the tool's header over the same payload */
    (void)memcpy(&sim_flash_mem(STAGE)[IMG_HDR_LEN], &s_a[IMG_HDR_LEN], 300u);
    CHECK(img_verify(STAGE, TI_SKU_8XX_SIC, 7u, &h) == IMG_OK);
    CHECK(h.target == 1u && h.length == 300u && h.fw_id == 0x0A0F0014u && h.sec_ver == 7u);
    /* the platform's OTP/HSE copy takes precedence: another key refuses the image, an all-zero copy refuses all */
    uint8_t other[32];
    seed[0] ^= 0x01u;
    ed25519_public_key(other, seed);
    sim_flash_set_pubkey(other);
    CHECK(img_verify(STAGE, TI_SKU_8XX_SIC, 7u, &h) == IMG_ERR_SIGNATURE);
    const uint8_t zero[32] = {0u};
    sim_flash_set_pubkey(zero);
    CHECK(img_verify(STAGE, TI_SKU_8XX_SIC, 7u, &h) == IMG_ERR_NO_KEY && !img_pubkey(key));
    sim_flash_set_pubkey(pk);
    CHECK(img_verify(STAGE, TI_SKU_8XX_SIC, 7u, &h) == IMG_OK);
}

TEST(every_refusal_names_its_reason)
{
    img_hdr_t h;
    sim_flash_wipe();
    s_alen = build(s_a, TI_SKU_8XX_SIC, 0x100u, 5u, 1000u, 1u, NULL);
    put(STAGE, s_a, s_alen);
    uint8_t *m = sim_flash_mem(STAGE);
    CHECK(img_verify(STAGE, TI_SKU_8XX_SIC, 5u, &h) == IMG_OK);             /* equal to the counter: accepted */
    CHECK(img_verify(STAGE, TI_SKU_8XX_SIC, 6u, &h) == IMG_ERR_ROLLBACK);    /* older than the counter */
    CHECK(img_verify(STAGE, TI_SKU_8XX_IGBT, 5u, &h) == IMG_ERR_TARGET);     /* signed for another SKU */
    m[IMG_HDR_LEN + 500u] ^= 0x01u;
    CHECK(img_verify(STAGE, TI_SKU_8XX_SIC, 5u, &h) == IMG_ERR_HASH);        /* one payload bit */
    m[IMG_HDR_LEN + 500u] ^= 0x01u;
    m[s_alen - 1u] ^= 0x80u;
    CHECK(img_verify(STAGE, TI_SKU_8XX_SIC, 5u, &h) == IMG_ERR_HASH);        /* its last byte */
    m[s_alen - 1u] ^= 0x80u;
    m[64u + 10u] ^= 0x01u;
    CHECK(img_verify(STAGE, TI_SKU_8XX_SIC, 5u, &h) == IMG_ERR_SIGNATURE);   /* the signature */
    m[64u + 10u] ^= 0x01u;
    m[20] ^= 0x10u;
    CHECK(img_verify(STAGE, TI_SKU_8XX_SIC, 0u, &h) == IMG_ERR_SIGNATURE);   /* a signed field raised after signing */
    m[20] ^= 0x10u;
    m[8] = 2u;
    CHECK(img_verify(STAGE, TI_SKU_8XX_IGBT, 0u, &h) == IMG_ERR_SIGNATURE);  /* the target re-labelled */
    m[8] = 1u;
    CHECK(img_verify(STAGE, TI_SKU_8XX_SIC, 5u, &h) == IMG_OK);
    uint8_t other[32];
    test_seed(other);
    other[31] ^= 0x80u;
    s_blen = build(s_b, TI_SKU_8XX_SIC, 0x100u, 5u, 1000u, 1u, other);
    put(STAGE, s_b, s_blen);
    CHECK(img_verify(STAGE, TI_SKU_8XX_SIC, 5u, &h) == IMG_ERR_SIGNATURE);   /* another key's signature */
    /* the structure, before anything is trusted */
    uint8_t raw[IMG_HDR_LEN];
    const struct {
        uint32_t at;
        uint32_t v; /* little-endian u32 written at `at` */
        img_result_t r;
    } S[] = {{0u, 0x57464955u, IMG_ERR_MAGIC},  {4u, 0x00800002u, IMG_ERR_FORMAT}, {4u, 0x007F0001u, IMG_ERR_FORMAT},
             {24u, 1u, IMG_ERR_FIELD},          {28u, 0x100u, IMG_ERR_FIELD},     {8u, 0u, IMG_ERR_FIELD},
             {8u, 5u, IMG_ERR_FIELD},           {12u, 0u, IMG_ERR_LENGTH},        {12u, IMG_PAYLOAD_MAX + 1u, IMG_ERR_LENGTH},
             {12u, IMG_PAYLOAD_MAX, IMG_OK},    {8u, 4u, IMG_OK}};
    for (uint32_t k = 0u; k < TI_ARRAY_LEN(S); k++) {
        (void)memcpy(raw, s_a, IMG_HDR_LEN);
        le32put(&raw[S[k].at], S[k].v);
        CHECK(img_parse(raw, &h) == S[k].r);
    }
    put(STAGE, s_a, s_alen);
    m[1] = 'X';
    CHECK(img_verify(STAGE, TI_SKU_8XX_SIC, 0u, &h) == IMG_ERR_MAGIC);
    m[1] = 'I';
    sim_flash_power_loss_after(0u); /* no power: nothing reads */
    (void)hal_flash_erase(LKG, 0u);
    CHECK(img_verify(STAGE, TI_SKU_8XX_SIC, 0u, &h) == IMG_ERR_READ);
    sim_flash_power_on();
    CHECK(img_verify(STAGE, TI_SKU_8XX_SIC, 0u, &h) == IMG_OK);
}

/* Bounded random headers — random, then with the magic and format valid, then the reserved bytes zero, then the
 * target and length in range too: the parser accepts exactly the structurally valid ones and nothing unsigned ever
 * verifies; and one bit flipped in each byte of a valid header is always refused. */
TEST(header_parser_fuzz_and_every_header_bit_flip_is_refused)
{
    img_hdr_t h;
    uint8_t raw[IMG_HDR_LEN];
    sim_flash_wipe();
    s_lcg = 38u;
    uint32_t wrong = 0u;
    uint32_t parsed = 0u;
    uint32_t accepted = 0u;
    for (uint32_t k = 0u; k < 3000u; k++) {
        for (uint32_t i = 0u; i < IMG_HDR_LEN; i++) {
            raw[i] = (uint8_t)rnd();
        }
        const uint32_t mode = k % 4u;
        if (mode >= 1u) {
            (void)memcpy(raw, GOLDEN, 8u);
        }
        if (mode >= 2u) {
            (void)memset(&raw[24], 0, 8u);
        }
        if (mode == 3u) {
            le32put(&raw[8], 1u + (rnd() % 4u));
            le32put(&raw[12], 1u + (rnd() % IMG_PAYLOAD_MAX));
        }
        const uint32_t t = (uint32_t)raw[8] | ((uint32_t)raw[9] << 8) | ((uint32_t)raw[10] << 16) | ((uint32_t)raw[11] << 24);
        const uint32_t n = (uint32_t)raw[12] | ((uint32_t)raw[13] << 8) | ((uint32_t)raw[14] << 16) | ((uint32_t)raw[15] << 24);
        uint8_t rsv = 0u;
        for (uint32_t i = 24u; i < 32u; i++) {
            rsv |= raw[i];
        }
        const bool valid = (memcmp(raw, GOLDEN, 8u) == 0) && (rsv == 0u) && (t >= 1u) && (t <= 4u) && (n >= 1u) &&
                           (n <= IMG_PAYLOAD_MAX);
        const img_result_t p = img_parse(raw, &h);
        wrong += ((p == IMG_OK) != valid) ? 1u : 0u;
        parsed += (p == IMG_OK) ? 1u : 0u;
        if ((mode == 3u) || ((k % 16u) == 0u)) { /* through the flash path as well */
            put(STAGE, raw, IMG_HDR_LEN);
            accepted += (img_verify(STAGE, 1u + (k % 4u), 0u, &h) == IMG_OK) ? 1u : 0u;
        }
    }
    CHECK(wrong == 0u && parsed >= 750u && accepted == 0u);
    s_alen = build(s_a, TI_SKU_8XX_SIC, 1u, 1u, 200u, 9u, NULL);
    put(STAGE, s_a, s_alen);
    CHECK(img_verify(STAGE, TI_SKU_8XX_SIC, 0u, &h) == IMG_OK);
    uint8_t *m = sim_flash_mem(STAGE);
    for (uint32_t i = 0u; i < IMG_HDR_LEN; i++) {
        const uint8_t bit = (uint8_t)(1u << (rnd() % 8u));
        m[i] ^= bit;
        accepted += (img_verify(STAGE, TI_SKU_8XX_SIC, 0u, &h) == IMG_OK) ? 1u : 0u;
        m[i] ^= bit;
    }
    CHECK(accepted == 0u && img_verify(STAGE, TI_SKU_8XX_SIC, 0u, &h) == IMG_OK);
}

/* ---------------- the bootloader's decision ---------------- */

TEST(accepted_image_installs_runs_its_trial_and_commits_with_the_counter)
{
    boot_rec_t r;
    s_alen = build(s_a, TI_SKU_8XX_SIC, 0x0A0F0014u, 5u, 3000u, 1u, NULL);
    s_blen = build(s_b, TI_SKU_8XX_SIC, 0x0A0F0015u, 6u, 20000u, 2u, NULL); /* three pages */
    /* EOL programs EXEC only: the first boot adopts it under its own target and version and makes the LKG copy */
    sim_flash_wipe();
    put(EXEC, s_a, s_alen);
    CHECK(reboot(&r) == BOOT_RUN && r.state == BOOT_ST_IDLE && r.lkg_valid == 1u && r.target == 1u && r.sec_counter == 5u);
    CHECK(holds(LKG, s_a, s_alen) && r.last == BOOT_LAST_NONE);
    put(STAGE, s_b, s_blen);
    request(BOOT_ST_ACTIVATE);
    CHECK(reboot(&r) == BOOT_RUN && r.state == BOOT_ST_TRIAL && r.last == BOOT_LAST_INSTALLED && r.sec_counter == 5u);
    CHECK(holds(EXEC, s_b, s_blen) && holds(LKG, s_a, s_alen)); /* the counter waits for the confirmation */
    request(BOOT_ST_CONFIRMED);                                  /* update.c, after UPD_CONFIRM_MS */
    CHECK(reboot(&r) == BOOT_RUN && r.state == BOOT_ST_IDLE && r.sec_counter == 6u && r.lkg_valid == 1u);
    CHECK(holds(LKG, s_b, s_blen) && holds(EXEC, s_b, s_blen));
    const uint32_t w = sim_flash_writes();
    CHECK(reboot(&r) == BOOT_RUN && sim_flash_writes() == w && r.state == BOOT_ST_IDLE); /* an ordinary boot writes nothing */
    /* the old image, staged again, is now below the counter */
    put(STAGE, s_a, s_alen);
    request(BOOT_ST_ACTIVATE);
    CHECK(reboot(&r) == BOOT_RUN && r.last == BOOT_LAST_REJECTED && r.last_err == (uint8_t)IMG_ERR_ROLLBACK);
    CHECK(holds(EXEC, s_b, s_blen) && sim_flash_writes() == w && boot_last_failed(r.last));
}

TEST(refused_images_leave_exec_untouched_with_the_reason)
{
    const struct {
        uint32_t target;
        uint32_t sec;
        uint32_t flip; /* byte flipped after signing; 0 = none */
        img_result_t why;
    } C[4] = {{TI_SKU_8XX_SIC, 6u, 64u + 3u, IMG_ERR_SIGNATURE},     /* wrong signature */
              {TI_SKU_8XX_SIC, 6u, IMG_HDR_LEN + 77u, IMG_ERR_HASH}, /* tampered payload */
              {TI_SKU_8XX_IGBT, 6u, 0u, IMG_ERR_TARGET},              /* wrong target */
              {TI_SKU_8XX_SIC, 4u, 0u, IMG_ERR_ROLLBACK}};            /* older than the counter */
    boot_rec_t r;
    s_alen = build(s_a, TI_SKU_8XX_SIC, 0x100u, 5u, 3000u, 1u, NULL);
    for (uint32_t k = 0u; k < 4u; k++) {
        device(s_a, s_alen, 5u);
        s_blen = build(s_b, C[k].target, 0x200u, C[k].sec, 5000u, 2u, NULL);
        if (C[k].flip != 0u) {
            s_b[C[k].flip] ^= 0x20u;
        }
        put(STAGE, s_b, s_blen);
        request(BOOT_ST_ACTIVATE);
        const uint32_t w = sim_flash_writes();
        CHECK(reboot(&r) == BOOT_RUN && r.state == BOOT_ST_IDLE && r.last == BOOT_LAST_REJECTED &&
              r.last_err == (uint8_t)C[k].why);
        CHECK(sim_flash_writes() == w && holds(EXEC, s_a, s_alen) && r.sec_counter == 5u);
    }
    /* a valid image, but no valid fallback copy: refused, and the copy is made first */
    device(s_a, s_alen, 5u);
    set_rec(BOOT_ST_ACTIVATE, 5u, 0u);
    s_blen = build(s_b, TI_SKU_8XX_SIC, 0x200u, 6u, 5000u, 2u, NULL);
    put(STAGE, s_b, s_blen);
    CHECK(reboot(&r) == BOOT_RUN && r.last == BOOT_LAST_NO_FALLBACK && holds(EXEC, s_a, s_alen) && r.lkg_valid == 1u);
}

/* The swap: a power loss after every one of its flash writes. The record says INSTALL before the copy starts, so the
 * next power-up sees an interrupted install, abandons it and restores the last known good; a second loss during that
 * restore restores again. The record write itself torn: nothing was copied yet, the request stands. */
TEST(power_loss_at_every_write_of_the_swap_boots_the_last_known_good)
{
    boot_rec_t r;
    s_alen = build(s_a, TI_SKU_8XX_SIC, 0x100u, 5u, 9000u, 1u, NULL); /* two pages */
    s_blen = build(s_b, TI_SKU_8XX_SIC, 0x200u, 6u, 9000u, 2u, NULL);
    device(s_a, s_alen, 5u);
    put(STAGE, s_b, s_blen);
    request(BOOT_ST_ACTIVATE);
    uint32_t w0 = sim_flash_writes();
    CHECK(reboot(&r) == BOOT_RUN && holds(EXEC, s_b, s_blen));
    const uint32_t W = sim_flash_writes() - w0;
    CHECK(W >= 30u);
    uint32_t bad = 0u;
    for (uint32_t k = 0u; k < W; k++) {
        device(s_a, s_alen, 5u);
        put(STAGE, s_b, s_blen);
        request(BOOT_ST_ACTIVATE);
        sim_flash_power_loss_after(k);
        const bool torn = (boot_decide(&r) == BOOT_HALT) && (r.state == BOOT_ST_INSTALL) && !holds(EXEC, s_a, s_alen);
        const bool back = (reboot(&r) == BOOT_RUN) && holds(EXEC, s_a, s_alen) && (r.state == BOOT_ST_IDLE) &&
                          (r.last == BOOT_LAST_ABANDONED) && (r.sec_counter == 5u) && (r.lkg_valid == 1u);
        if (!(torn && back) && (bad == 0u)) {
            printf("    swap loss after %u writes: torn %d back %d\n", (unsigned)k, (int)torn, (int)back);
        }
        bad += (torn && back) ? 0u : 1u;
    }
    CHECK(bad == 0u);
    device(s_a, s_alen, 5u); /* a second loss, during the restore */
    put(STAGE, s_b, s_blen);
    request(BOOT_ST_ACTIVATE);
    sim_flash_power_loss_after(W / 2u);
    CHECK(boot_decide(&r) == BOOT_HALT);
    for (uint32_t k = 1u; k < W; k += 6u) {
        sim_flash_power_on();
        nv_init();
        sim_flash_power_loss_after(k);
        CHECK(boot_decide(&r) == BOOT_HALT && r.state == BOOT_ST_RESTORE);
    }
    CHECK(reboot(&r) == BOOT_RUN && holds(EXEC, s_a, s_alen) && r.last == BOOT_LAST_ABANDONED && r.state == BOOT_ST_IDLE);
    /* the INSTALL record write torn (a brown-out in the NVM write; the A/B slots keep ACTIVATE) */
    device(s_a, s_alen, 5u);
    put(STAGE, s_b, s_blen);
    request(BOOT_ST_ACTIVATE);
    sim_nvm_set_write_polls(2000u); /* longer than the bootloader waits */
    w0 = sim_flash_writes();
    CHECK(boot_decide(&r) == BOOT_HALT && sim_flash_writes() == w0); /* nothing copied before the record is durable */
    sim_nvm_power_loss();
    sim_nvm_set_write_polls(3u);
    CHECK(reboot(&r) == BOOT_RUN && r.state == BOOT_ST_TRIAL && holds(EXEC, s_b, s_blen));
    /* the TRIAL write torn after a complete copy (the record still INSTALL): abandoned, the last known good */
    request(BOOT_ST_INSTALL);
    CHECK(reboot(&r) == BOOT_RUN && holds(EXEC, s_a, s_alen) && r.last == BOOT_LAST_ABANDONED);
}

TEST(a_first_boot_that_never_confirms_falls_back_to_the_last_known_good)
{
    boot_rec_t r;
    s_alen = build(s_a, TI_SKU_8XX_SIC, 0x100u, 5u, 3000u, 1u, NULL);
    s_blen = build(s_b, TI_SKU_8XX_SIC, 0x200u, 6u, 4000u, 2u, NULL);
    device(s_a, s_alen, 5u);
    put(STAGE, s_b, s_blen);
    request(BOOT_ST_ACTIVATE);
    CHECK(reboot(&r) == BOOT_RUN && r.state == BOOT_ST_TRIAL && holds(EXEC, s_b, s_blen));
    /* a crash, a watchdog reset or a power cycle before the image confirmed itself */
    CHECK(reboot(&r) == BOOT_RUN && holds(EXEC, s_a, s_alen) && r.state == BOOT_ST_IDLE);
    CHECK(r.last == BOOT_LAST_FIRST_BOOT_FAILED && r.sec_counter == 5u && r.lkg_valid == 1u && boot_last_failed(r.last));
    request(BOOT_ST_ACTIVATE); /* the staged image is still there: activated again, it installs again */
    CHECK(reboot(&r) == BOOT_RUN && r.state == BOOT_ST_TRIAL && holds(EXEC, s_b, s_blen));
}

TEST(power_loss_during_the_commit_resumes_it_and_the_counter_never_goes_back)
{
    boot_rec_t r;
    s_alen = build(s_a, TI_SKU_8XX_SIC, 0x100u, 5u, 9000u, 1u, NULL);
    s_blen = build(s_b, TI_SKU_8XX_SIC, 0x200u, 6u, 9000u, 2u, NULL);
    device(s_a, s_alen, 5u); /* B confirmed in EXEC, A still in LKG: an uninterrupted commit first */
    put(EXEC, s_b, s_blen);
    set_rec(BOOT_ST_CONFIRMED, 5u, 1u);
    const uint32_t w0 = sim_flash_writes();
    CHECK(reboot(&r) == BOOT_RUN && holds(LKG, s_b, s_blen) && r.sec_counter == 6u);
    const uint32_t W = sim_flash_writes() - w0;
    CHECK(W >= 30u);
    uint32_t bad = 0u;
    for (uint32_t k = 0u; k < W; k += 2u) {
        device(s_a, s_alen, 5u);
        put(EXEC, s_b, s_blen);
        set_rec(BOOT_ST_CONFIRMED, 5u, 1u);
        sim_flash_power_loss_after(k);
        const bool halted = (boot_decide(&r) == BOOT_HALT) && (r.state == BOOT_ST_COMMIT) && (r.sec_counter == 6u);
        const bool done = (reboot(&r) == BOOT_RUN) && holds(EXEC, s_b, s_blen) && holds(LKG, s_b, s_blen) &&
                          (r.state == BOOT_ST_IDLE) && (r.sec_counter == 6u) && (r.lkg_valid == 1u);
        bad += (halted && done) ? 0u : 1u;
    }
    CHECK(bad == 0u);
}

/* The record under a torn write (the nvlog brown-out model): a commit raised the counter 5 -> 6, the next record write
 * is torn — the read gives the last durable record (6), never the one before it (5). A record ending in its own
 * CRC-32 made the slot's CRC-32 blind to its content: the torn slot validated with those stale bytes and the counter
 * went back to 5 (boot.h). */
TEST(a_torn_record_write_never_takes_the_counter_back)
{
    sim_flash_wipe();
    nv_init();
    set_rec(BOOT_ST_COMMIT, 5u, 0u);
    set_rec(BOOT_ST_IDLE, 6u, 1u);
    boot_rec_t r = rec();
    CHECK(r.sec_counter == 6u);
    r.state = (uint8_t)BOOT_ST_ACTIVATE;
    sim_nvm_set_write_polls(50u);
    CHECK(boot_rec_queue(&r));
    for (uint32_t k = 0u; k < 10u; k++) {
        nv_service(); /* started, in flight */
    }
    CHECK(!nv_idle());
    sim_nvm_power_loss(); /* torn: the slot of the counter-5 record holds half of the new one */
    sim_nvm_set_write_polls(3u);
    nv_init();
    CHECK(boot_rec_read(&r) && r.sec_counter == 6u && r.state == BOOT_ST_IDLE && r.lkg_valid == 1u);
}

TEST(an_exec_image_that_fails_its_check_is_restored_and_nothing_valid_stays_in_the_bootloader)
{
    boot_rec_t r;
    s_alen = build(s_a, TI_SKU_8XX_SIC, 0x100u, 5u, 3000u, 1u, NULL);
    device(s_a, s_alen, 5u);
    sim_flash_mem(EXEC)[IMG_HDR_LEN + 100u] ^= 0x04u; /* a flipped bit: an ECC escape, or a debug-port write */
    CHECK(reboot(&r) == BOOT_RUN && holds(EXEC, s_a, s_alen) && r.state == BOOT_ST_IDLE);
    CHECK(r.last == BOOT_LAST_EXEC_INVALID && r.last_err == (uint8_t)IMG_ERR_HASH);
    sim_flash_mem(EXEC)[200] ^= 0x01u; /* both copies bad: nothing starts */
    sim_flash_mem(LKG)[200] ^= 0x01u;
    CHECK(reboot(&r) == BOOT_NO_IMAGE);
    CHECK(reboot(&r) == BOOT_NO_IMAGE); /* and stays so */
    /* no record, and an EXEC signed by another key: nothing is adopted, nothing starts */
    uint8_t other[32];
    test_seed(other);
    other[0] ^= 0x55u;
    sim_flash_wipe();
    sim_nvm_wipe();
    s_blen = build(s_b, TI_SKU_8XX_SIC, 0x100u, 5u, 3000u, 1u, other);
    put(EXEC, s_b, s_blen);
    boot_rec_t x;
    CHECK(reboot(&r) == BOOT_NO_IMAGE && !boot_rec_read(&x));
}

/* ---------------- the programming services (no application: test hooks) ---------------- */

static uint8_t s_cond;
static unsigned s_entered;
static uint8_t cond_hook(void *ctx)
{
    (void)ctx;
    return s_cond;
}
static void enter_hook(void *ctx)
{
    (void)ctx;
    s_entered++;
}

static bool rq(uds_t *u, const uint8_t *req, uint8_t n, uint8_t out[8])
{
    hal_can_frame_t f = {.id = UDS_ID_REQ, .len = 8u};
    (void)memset(f.data, 0xAA, 8u);
    f.data[0] = n;
    (void)memcpy(&f.data[1], req, n);
    hal_can_frame_t r;
    if (!uds_handle(u, &f, 0x1234u, &r)) {
        return false;
    }
    (void)memcpy(out, r.data, 8u);
    return (r.id == UDS_ID_RSP) && (r.len == 8u);
}

static bool neg(const uint8_t r[8], uint8_t sid, uint8_t code) { return (r[0] == 3u) && (r[1] == 0x7Fu) && (r[2] == sid) && (r[3] == code); }

/* round 23, second pass (found by the service tool over CAN): a programming request of 8..62 bytes is an ISO 15765-2
 * escape single frame (00 LL …) on CAN-FD — the firmware dropped it (only the FW-40 services parsed the escape), so an
 * image whose last block was 8..62 bytes could not be transferred. It is answered like the classic frame now. */
static bool rq_esc(uds_t *u, const uint8_t *req, uint8_t n, uint8_t out[8])
{
    hal_can_frame_t f = {.id = UDS_ID_REQ, .len = (uint8_t)(n + 2u)};
    (void)memset(f.data, 0xAA, sizeof f.data);
    f.data[0] = 0u;
    f.data[1] = n;
    (void)memcpy(&f.data[2], req, n);
    hal_can_frame_t r;
    if (!uds_handle(u, &f, 0x1234u, &r)) {
        return false;
    }
    (void)memcpy(out, r.data, 8u);
    return (r.id == UDS_ID_RSP) && (r.len == 8u);
}
TEST(an_escape_single_frame_reaches_the_programming_services)
{
    uds_t u;
    (void)memset(&u, 0, sizeof u);
    uint8_t r[8];
    uint8_t td[12] = {0x36u, 0x01u, 0x5Au, 0x5Au, 0x5Au, 0x5Au, 0x5Au, 0x5Au, 0x5Au, 0x5Au, 0x5Au, 0x5Au};
    CHECK(rq_esc(&u, td, 12u, r) && neg(r, 0x36u, UPD_NRC_NOT_IN_SESSION)); /* parsed and dispatched: the session NRC, not silence */
    CHECK(rq(&u, td, 3u, r) && neg(r, 0x36u, UPD_NRC_NOT_IN_SESSION));       /* the classic frame as before */
    uint8_t sa[10] = {0x27u, 0x01u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};
    CHECK(rq_esc(&u, sa, 10u, r) && (r[1] == 0x67u || r[1] == 0x7Fu));       /* any service: answered */
    hal_can_frame_t bad = {.id = UDS_ID_REQ, .len = 8u};                       /* an 8-byte frame cannot be an escape */
    (void)memset(bad.data, 0, 8u);
    bad.data[1] = 6u;
    hal_can_frame_t rr;
    CHECK(!uds_handle(&u, &bad, 0x1234u, &rr));
}

static const uint8_t PROG[2] = {0x10u, 0x02u};
static const uint8_t EXIT[1] = {0x37u};
static const uint8_t VSTART[4] = {0x31u, 0x01u, 0xFFu, 0x01u};
static const uint8_t VRES[4] = {0x31u, 0x03u, 0xFFu, 0x01u};
static const uint8_t ACT[4] = {0x31u, 0x01u, 0xF0u, 0x38u};
static const uint8_t RESET[2] = {0x11u, 0x01u};

static void rd_req(uint8_t q[7], uint32_t size)
{
    q[0] = 0x34u; /* dataFormat 0, address 1 byte, size 3 bytes */
    q[1] = 0x00u;
    q[2] = 0x31u;
    q[3] = 0x00u;
    q[4] = (uint8_t)(size >> 16);
    q[5] = (uint8_t)(size >> 8);
    q[6] = (uint8_t)size;
}

TEST(programming_services_follow_the_protocol)
{
    uds_t u;
    uint8_t r[8];
    uint8_t q[7];
    sim_flash_wipe();
    nv_init();
    s_alen = build(s_a, TI_SKU_8XX_SIC, 0x100u, 5u, 3000u, 1u, NULL);
    device(s_a, s_alen, 5u);
    s_blen = build(s_b, TI_SKU_8XX_SIC, 0x200u, 6u, 13000u, 2u, NULL); /* 2626 blocks: the counter wraps; 4 chunks */
    uds_init(&u, NULL, NULL, NULL);
    s_cond = UDS_NRC_CONDITIONS;
    s_entered = 0u;
    upd_init(cond_hook, enter_hook, NULL);
    /* outside the programming session */
    rd_req(q, s_blen);
    CHECK(rq(&u, q, 7u, r) && neg(r, 0x34u, UPD_NRC_NOT_IN_SESSION));
    const uint8_t td[3] = {0x36u, 0x01u, 0x00u};
    CHECK(rq(&u, td, 3u, r) && neg(r, 0x36u, UPD_NRC_NOT_IN_SESSION));
    CHECK(rq(&u, EXIT, 1u, r) && neg(r, 0x37u, UPD_NRC_NOT_IN_SESSION));
    CHECK(rq(&u, VSTART, 4u, r) && neg(r, 0x31u, UDS_NRC_OUT_OF_RANGE));
    (void)rq(&u, RESET, 2u, r); /* not claimed outside the session: whatever the base server answers, no reset here */
    upd_service();
    CHECK(!sim_flash_take_reset() && r[1] != 0x51u);
    /* the session: refused while the conditions do not hold, the entry hook not called */
    CHECK(rq(&u, PROG, 2u, r) && neg(r, 0x10u, UDS_NRC_CONDITIONS) && s_entered == 0u && !upd_session());
    s_cond = 0u;
    CHECK(rq(&u, PROG, 2u, r) && r[0] == 6u && r[1] == 0x50u && r[2] == 0x02u && r[3] == 0x00u && r[4] == 0x32u &&
          r[5] == 0x01u && r[6] == 0xF4u);
    CHECK(s_entered == 1u && upd_session() && rq(&u, PROG, 2u, r) && r[1] == 0x50u && s_entered == 1u);
    const uint8_t ext[2] = {0x10u, 0x03u};
    CHECK(rq(&u, ext, 2u, r) && neg(r, 0x10u, UDS_NRC_SUBFUNCTION_NOT_SUPPORTED));
    /* RequestDownload: the unlock, the format, the range */
    CHECK(rq(&u, q, 7u, r) && neg(r, 0x34u, UDS_NRC_SECURITY_DENIED));
    u.unlocked = true;
    CHECK(rq(&u, q, 4u, r) && neg(r, 0x34u, UDS_NRC_LENGTH));
    q[1] = 0x11u; /* compressed + encrypted */
    CHECK(rq(&u, q, 7u, r) && neg(r, 0x34u, UDS_NRC_OUT_OF_RANGE));
    q[1] = 0x00u;
    q[2] = 0x05u; /* a zero-length size field */
    CHECK(rq(&u, q, 7u, r) && neg(r, 0x34u, UDS_NRC_OUT_OF_RANGE));
    q[2] = 0x22u; /* 2 + 2 bytes announced, 3 sent */
    CHECK(rq(&u, q, 6u, r) && neg(r, 0x34u, UDS_NRC_LENGTH));
    q[2] = 0x31u;
    q[3] = 0x01u; /* not the start of STAGE */
    CHECK(rq(&u, q, 7u, r) && neg(r, 0x34u, UDS_NRC_OUT_OF_RANGE));
    rd_req(q, HAL_FLASH_REGION_SIZE + 1u);
    CHECK(rq(&u, q, 7u, r) && neg(r, 0x34u, UDS_NRC_OUT_OF_RANGE));
    rd_req(q, IMG_HDR_LEN);
    CHECK(rq(&u, q, 7u, r) && neg(r, 0x34u, UDS_NRC_OUT_OF_RANGE));
    CHECK(rq(&u, EXIT, 1u, r) && neg(r, 0x37u, UDS_NRC_SEQUENCE));
    rd_req(q, s_blen);
    CHECK(rq(&u, q, 7u, r) && r[0] == 4u && r[1] == 0x74u && r[2] == 0x20u && r[3] == (uint8_t)(UPD_BLOCK_MAX >> 8) &&
          r[4] == (uint8_t)(UPD_BLOCK_MAX & 0xFFu));
    CHECK(UPD_BLOCK_MAX >= 4095u); /* round 23 (item 11): the ISO 15765-2 payload, not the single frame's 7 bytes */
    CHECK(rq(&u, q, 7u, r) && neg(r, 0x34u, UDS_NRC_CONDITIONS)); /* one download at a time */
    /* TransferData: the counter, the length, the repeat, the busy background */
    uint8_t b[7] = {0x36u, 0x02u, 1u, 2u, 3u, 4u, 5u};
    CHECK(rq(&u, b, 7u, r) && neg(r, 0x36u, UPD_NRC_WRONG_BSC));
    CHECK(rq(&u, b, 2u, r) && neg(r, 0x36u, UDS_NRC_LENGTH));
    uint32_t off = 0u;
    uint8_t bsc = 1u;
    uint32_t busy = 0u;
    uint32_t other = 0u;
    uint32_t repeats = 0u;
    { /* round 23, second pass (F238): the first block as a CAN-FD escape single frame (00 07 36 01 + 5 data bytes, a
       * 9-byte frame) — accepted like the classic frame; its "lost" response is repeated below as the loop does at off 0,
       * so the block count and the repeat count are the loop's */
        uint8_t e[7] = {0x36u, 1u};
        (void)memcpy(&e[2], &s_b[0], 5u);
        bool ok = rq_esc(&u, e, 7u, r);
        while (ok && neg(r, 0x36u, UPD_NRC_BUSY)) { /* the background may be busy, as the loop below tolerates */
            busy++;
            upd_service();
            ok = rq_esc(&u, e, 7u, r);
        }
        CHECK(ok && (r[0] == 2u) && (r[1] == 0x76u) && (r[2] == 1u));
        repeats += (rq(&u, e, 7u, r) && (r[1] == 0x76u) && (r[2] == 1u)) ? 1u : 0u;
        off = 5u;
        bsc = 2u;
    }
    while (off < s_blen) {
        const uint32_t n = ((s_blen - off) < 5u) ? (s_blen - off) : 5u;
        b[1] = bsc;
        (void)memcpy(&b[2], &s_b[off], n);
        if (!rq(&u, b, (uint8_t)(n + 2u), r)) {
            other++;
            break;
        }
        if (neg(r, 0x36u, UPD_NRC_BUSY)) {
            busy++;
            upd_service(); /* the background programs what it owes; the tester repeats the block */
            continue;
        }
        if ((r[0] != 2u) || (r[1] != 0x76u) || (r[2] != bsc)) {
            other++;
            break;
        }
        if ((off % 5000u) == 0u) { /* now and then the response is "lost": the same block again, not written twice */
            repeats += (rq(&u, b, (uint8_t)(n + 2u), r) && (r[1] == 0x76u) && (r[2] == bsc)) ? 1u : 0u;
        }
        off += n;
        bsc++;
    }
    CHECK(other == 0u && busy >= 2u && repeats == 3u && bsc == (uint8_t)(1u + ((s_blen + 4u) / 5u)));
    b[1] = bsc;
    CHECK(rq(&u, b, 3u, r) && neg(r, 0x36u, UPD_NRC_SUSPENDED)); /* beyond the announced size */
    CHECK(rq(&u, VRES, 4u, r) && neg(r, 0x31u, UDS_NRC_SEQUENCE)); /* nothing verified yet */
    CHECK(rq(&u, EXIT, 1u, r) && r[0] == 1u && r[1] == 0x77u);
    CHECK(rq(&u, ACT, 4u, r) && neg(r, 0x31u, UDS_NRC_SEQUENCE)); /* not verified */
    CHECK(rq(&u, VSTART, 4u, r) && r[0] == 4u && r[1] == 0x71u && r[2] == 0x01u && r[3] == 0xFFu && r[4] == 0x01u);
    CHECK(rq(&u, VRES, 4u, r) && r[0] == 6u && r[1] == 0x71u && r[2] == 0x03u && r[5] == 0u); /* running */
    upd_service();
    CHECK(rq(&u, VRES, 4u, r) && r[5] == 1u && r[6] == (uint8_t)IMG_OK && holds(STAGE, s_b, s_blen));
    s_cond = UDS_NRC_CONDITIONS; /* the activation checks the conditions again */
    CHECK(rq(&u, ACT, 4u, r) && neg(r, 0x31u, UDS_NRC_CONDITIONS) && rec().state == BOOT_ST_IDLE);
    s_cond = 0u;
    CHECK(rq(&u, ACT, 4u, r) && r[0] == 4u && r[1] == 0x71u && r[3] == 0xF0u && r[4] == 0x38u);
    CHECK(rq(&u, RESET, 2u, r) && neg(r, 0x11u, UPD_NRC_BUSY)); /* the record not yet in NVM */
    drain();
    CHECK(rec().state == BOOT_ST_ACTIVATE);
    const uint8_t soft[2] = {0x11u, 0x03u};
    CHECK(rq(&u, soft, 2u, r) && neg(r, 0x11u, UDS_NRC_SUBFUNCTION_NOT_SUPPORTED));
    CHECK(rq(&u, RESET, 2u, r) && r[0] == 2u && r[1] == 0x51u && r[2] == 0x01u);
    upd_service();
    CHECK(!sim_flash_take_reset()); /* after the response has left */
    sim_advance_us(UPD_RESET_DELAY_MS * 1000u);
    upd_service();
    CHECK(sim_flash_take_reset());
    /* a tampered download fails its verification with the reason; no activation; the default session ends it */
    device(s_a, s_alen, 5u);
    upd_init(cond_hook, enter_hook, NULL);
    CHECK(rq(&u, PROG, 2u, r) && r[1] == 0x50u);
    rd_req(q, s_alen);
    CHECK(rq(&u, q, 7u, r) && r[1] == 0x74u);
    s_a[IMG_HDR_LEN + 9u] ^= 0x01u;
    bsc = 1u;
    for (off = 0u; off < s_alen; off += 5u) {
        const uint32_t n = ((s_alen - off) < 5u) ? (s_alen - off) : 5u;
        b[1] = bsc++;
        (void)memcpy(&b[2], &s_a[off], n);
        (void)rq(&u, b, (uint8_t)(n + 2u), r);
        upd_service();
    }
    s_a[IMG_HDR_LEN + 9u] ^= 0x01u;
    CHECK(rq(&u, EXIT, 1u, r) && r[1] == 0x77u && rq(&u, VSTART, 4u, r) && r[1] == 0x71u);
    upd_service();
    CHECK(rq(&u, VRES, 4u, r) && r[5] == 2u && r[6] == (uint8_t)IMG_ERR_HASH);
    CHECK(rq(&u, ACT, 4u, r) && neg(r, 0x31u, UDS_NRC_SEQUENCE));
    const uint8_t dflt[2] = {0x10u, 0x01u};
    CHECK(rq(&u, dflt, 2u, r) && r[1] == 0x50u && r[2] == 0x01u && !upd_session());
    CHECK(rq(&u, VRES, 4u, r) && neg(r, 0x31u, UDS_NRC_OUT_OF_RANGE));
}

/* ---------------- in the application ---------------- */

static bool app_key(const uint8_t seed[UDS_SA_LEN], uint8_t key[UDS_SA_LEN])
{
    for (uint32_t i = 0u; i < UDS_SA_LEN; i++) {
        key[i] = (uint8_t)(seed[(i + 1u) % UDS_SA_LEN] ^ (0xA5u + i));
    }
    return true;
}

/* One single-frame request on the diagnostic bus; the response the next tick sends. */
static bool app_rq(const uint8_t *req, uint8_t n, uint8_t rsp[8])
{
    hal_can_frame_t f = {.id = UDS_ID_REQ, .len = 8u};
    (void)memset(f.data, 0xAA, 8u);
    f.data[0] = n;
    (void)memcpy(&f.data[1], req, n);
    hal_can_frame_t r;
    while (sim_can_pop_tx(HAL_CAN_DIAG, &r)) {
    }
    sim_can_inject(HAL_CAN_DIAG, &f);
    h_run_ms(1u);
    if (!sim_can_pop_tx(HAL_CAN_DIAG, &r) || (r.id != UDS_ID_RSP)) {
        return false;
    }
    (void)memcpy(rsp, r.data, 8u);
    return true;
}

static bool app_unlock(void)
{
    uint8_t r[8];
    const uint8_t sq[2] = {0x27u, 0x01u};
    g_app.uds.key_fn = app_key;
    if (!app_rq(sq, 2u, r) || (r[1] != 0x67u)) {
        return false;
    }
    uint8_t kq[2u + UDS_SA_LEN] = {0x27u, 0x02u};
    (void)app_key(&r[3], &kq[2]);
    return app_rq(kq, (uint8_t)sizeof kq, r) && (r[1] == 0x67u) && (r[2] == 0x02u);
}

/* A card in service with image A (the running build's identity) and the application booted, at standstill. */
static void app_device(void)
{
    h_setup(TI_SKU_8XX_SIC);
    s_alen = build(s_a, TI_SKU_8XX_SIC, TI_FW_ID, 5u, 600u, 1u, NULL);
    device(s_a, s_alen, 5u);
    h_boot();
    h_run_ms(300u);
}

static bool untouched(void)
{
    return !dtc_active(DTC_FW_UPDATE) && !upd_session() && (g_app.evidence == ARM_EV_ALL);
}

TEST(update_refused_while_armed_with_hv_present_or_moving)
{
    uint8_t r[8];
    app_device();
    CHECK(!g_app.no_arm && untouched());
    h_ramp_speed(1000.0f, 200u); /* moving: HV absent, bridge disarmed */
    h_run_ms(100u);
    CHECK(g_app.rslv.valid && dis_hv_state(&g_app.vdc) == TI_HV_SAFE && g_app.br.mode == BR_DISARMED);
    CHECK(app_rq(PROG, 2u, r) && neg(r, 0x10u, UDS_NRC_CONDITIONS) && untouched());
    h_ramp_speed(0.0f, 200u);
    h_run_ms(100u);
    CHECK(h_run_until(SM_PRECHARGE_WAIT, 3000u));
    H.contactors = TI_CONT_PRECHARGE; /* HV present: the link charging, the bridge still disarmed */
    h_run_ms(800u);
    CHECK(dis_hv_state(&g_app.vdc) == TI_HV_PRESENT && g_app.br.mode == BR_DISARMED);
    CHECK(app_rq(PROG, 2u, r) && neg(r, 0x10u, UDS_NRC_CONDITIONS) && untouched());
    H.contactors = TI_CONT_CLOSED; /* armed */
    CHECK(h_run_until(SM_ARMED_ZERO_TORQUE, 500u));
    h_run_ms(20u);
    CHECK(g_app.sm.st == SM_ARMED_ZERO_TORQUE && g_app.br.mode != BR_DISARMED);
    CHECK(app_rq(PROG, 2u, r) && neg(r, 0x10u, UDS_NRC_CONDITIONS) && untouched());
    H.link_override = true; /* armed while the link reads 20 V for a moment: "HV safe", still armed */
    sim_set_link_v(20.0f, 20.0f);
    h_run_ms(3u);
    CHECK(dis_hv_state(&g_app.vdc) == TI_HV_SAFE && g_app.br.mode != BR_DISARMED);
    CHECK(app_rq(PROG, 2u, r) && neg(r, 0x10u, UDS_NRC_CONDITIONS) && untouched());
}

/* The no-torque state, proven through the arming evidence: once the session is entered the validated evidence is
 * withdrawn and arming forbidden; the VCU then precharges, closes the contactors, enables and asks for torque — the
 * bridge never leaves DISARMED, MCU_GATE_EN never rises, no PWM, 0 N·m applied, INV_STATUS b15 names the missing
 * evidence. The next power-up is an ordinary one. */
TEST(the_update_state_cannot_arm_and_refuses_torque)
{
    uint8_t r[8];
    app_device();
    CHECK(app_rq(PROG, 2u, r) && r[1] == 0x50u && r[2] == 0x02u);
    CHECK(dtc_active(DTC_FW_UPDATE) && g_app.no_arm && ((g_app.evidence & ARM_EV_VALIDATED) == 0u) && upd_session());
    CHECK(h_run_until(SM_FAULT, 3000u));
    H.contactors = TI_CONT_PRECHARGE;
    h_run_ms(800u);
    H.contactors = TI_CONT_CLOSED;
    H.enable = true;
    H.torque_nm = 100.0f;
    bool armed = false;
    for (uint32_t k = 0u; k < 1500u; k++) {
        h_run_ms(1u);
        armed = armed || (g_app.br.mode != BR_DISARMED) || hal_gpio_out_state(HAL_DO_MCU_GATE_EN) ||
                (hal_pwm_mode() != HAL_PWM_OFF) || (g_app.sm.st == SM_ARMED_ZERO_TORQUE) || (g_app.sm.st == SM_RUN) ||
                (g_app.t_act_nm != 0.0f) || g_app.mod_req;
    }
    CHECK(!armed && g_app.sm.st == SM_FAULT && dis_hv_state(&g_app.vdc) == TI_HV_PRESENT);
    hal_can_frame_t f;
    bool got = false;
    while (sim_can_pop_tx(HAL_CAN_VEHICLE, &f)) {
        got = got || (f.id == CAN_ID_INV_STATUS);
        if (f.id == CAN_ID_INV_STATUS) {
            CHECK((f.data[15] & ARM_EV_VALIDATED) == ARM_EV_VALIDATED);
        }
    }
    CHECK(got);
    sim_reset(); /* the next key cycle */
    (void)memset(&g_app_session, 0, sizeof g_app_session);
    dtc_init();
    h_setup(TI_SKU_8XX_SIC);
    h_boot();
    CHECK(!upd_session() && !dtc_active(DTC_FW_UPDATE) && h_to_armed());
}

/* Round 23 (item 11): a request of any length as a tester sends it (ISO 15765-2 on the CAN-FD bus): up to 7 bytes one
 * classic single frame; longer a first frame (12-bit length, 62 bytes) in a 64-byte frame, then consecutive frames of 63
 * bytes, as many per flow control as its block size asks (its STmin 0), the ECU's flow control and its response read
 * after each 1 ms task. The response is a single frame here (its first 8 bytes returned). *ms: the tasks it took. */
static uint8_t s_iso_bs; /* the block size of the last flow control */
static bool app_rq_iso(const uint8_t *req, uint32_t n, uint8_t rsp[8], uint32_t *ms)
{
    if (n <= 7u) {
        *ms += 1u;
        return app_rq(req, (uint8_t)n, rsp);
    }
    hal_can_frame_t f = {.id = UDS_ID_REQ, .len = 64u};
    hal_can_frame_t r;
    while (sim_can_pop_tx(HAL_CAN_DIAG, &r)) {
    }
    (void)memset(f.data, 0xAA, sizeof f.data);
    f.data[0] = (uint8_t)(0x10u | (n >> 8));
    f.data[1] = (uint8_t)n;
    (void)memcpy(&f.data[2], req, 62u);
    sim_can_inject(HAL_CAN_DIAG, &f);
    uint32_t off = 62u;
    uint8_t sn = 1u;
    for (uint32_t t = 0u; t < 2000u; t++) {
        h_run_ms(1u);
        *ms += 1u;
        while (sim_can_pop_tx(HAL_CAN_DIAG, &r)) {
            if (r.id != UDS_ID_RSP) {
                continue; /* a periodic frame */
            }
            if (r.data[0] == 0x30u) { /* ContinueToSend: the next block */
                s_iso_bs = r.data[1];
                for (uint32_t k = 0u; (off < n) && ((r.data[1] == 0u) || (k < r.data[1])); k++) {
                    const uint32_t take = ((n - off) < 63u) ? (n - off) : 63u;
                    (void)memset(f.data, 0xAA, sizeof f.data);
                    f.data[0] = (uint8_t)(0x20u | sn);
                    (void)memcpy(&f.data[1], &req[off], take);
                    sim_can_inject(HAL_CAN_DIAG, &f);
                    off += take;
                    sn = (uint8_t)((sn + 1u) & 0x0Fu);
                }
            } else if ((r.data[0] >= 1u) && (r.data[0] <= 7u)) {
                (void)memcpy(rsp, r.data, 8u);
                return true;
            } else {
                return false; /* an overflow or wait flow control, or a segmented response: not expected here */
            }
        }
    }
    return false;
}

/* Session, unlock, download, exit, verify, activate, ECUReset — the complete tester sequence through the app. Round 23
 * (item 11): TransferData carries UPD_BLOCK_MAX - 2 bytes per block over the ISO 15765-2 transport (FW-40's). *ms: the
 * tasks the download took (RequestDownload to RequestTransferExit). */
static bool stage_over_uds_ms(const uint8_t *img, uint32_t len, uint32_t *ms)
{
    uint8_t r[8];
    uint8_t q[7];
    bool ok = app_unlock() && app_rq(PROG, 2u, r) && (r[1] == 0x50u);
    rd_req(q, len);
    *ms = 0u;
    ok = ok && app_rq_iso(q, 7u, r, ms) && (r[1] == 0x74u) && ((((uint32_t)r[3] << 8) | r[4]) == UPD_BLOCK_MAX);
    static uint8_t b[UPD_BLOCK_MAX];
    uint8_t bsc = 1u;
    for (uint32_t off = 0u; ok && (off < len);) {
        const uint32_t n = ((len - off) < (UPD_BLOCK_MAX - 2u)) ? (len - off) : (UPD_BLOCK_MAX - 2u);
        b[0] = 0x36u;
        b[1] = bsc;
        (void)memcpy(&b[2], &img[off], n);
        ok = app_rq_iso(b, n + 2u, r, ms);
        if (ok && neg(r, 0x36u, UPD_NRC_BUSY)) {
            continue;
        }
        ok = ok && (r[1] == 0x76u) && (r[2] == bsc);
        off += n;
        bsc++;
    }
    ok = ok && app_rq_iso(EXIT, 1u, r, ms) && (r[1] == 0x77u);
    ok = ok && app_rq(VSTART, 4u, r) && (r[1] == 0x71u);
    h_run_ms(2u);
    ok = ok && app_rq(VRES, 4u, r) && (r[5] == 1u) && app_rq(ACT, 4u, r) && (r[1] == 0x71u);
    h_run_ms(5u);
    ok = ok && app_rq(RESET, 2u, r) && (r[1] == 0x51u);
    h_run_ms(UPD_RESET_DELAY_MS + 5u);
    return ok && sim_flash_take_reset();
}

static bool stage_over_uds(const uint8_t *img, uint32_t len)
{
    uint32_t ms = 0u;
    return stage_over_uds_ms(img, len, &ms);
}

static void app_power_up(void)
{
    sim_reset();
    (void)memset(&g_app_session, 0, sizeof g_app_session);
    dtc_init();
    h_setup(TI_SKU_8XX_SIC);
    h_boot();
}

TEST(an_update_over_uds_installs_confirms_itself_and_commits)
{
    boot_rec_t r;
    app_device();
    s_blen = build(s_b, TI_SKU_8XX_SIC, TI_FW_ID, 6u, 1400u, 2u, NULL);
    CHECK(stage_over_uds(s_b, s_blen) && rec().state == BOOT_ST_ACTIVATE && holds(STAGE, s_b, s_blen));
    CHECK(reboot(&r) == BOOT_RUN && r.state == BOOT_ST_TRIAL && holds(EXEC, s_b, s_blen)); /* the bootloader */
    app_power_up();                                                                     /* the new image's first boot */
    h_run_ms(UPD_CONFIRM_MS - 100u);
    CHECK(rec().state == BOOT_ST_TRIAL); /* not before UPD_CONFIRM_MS */
    h_run_ms(200u);
    CHECK(rec().state == BOOT_ST_CONFIRMED && !dtc_active(DTC_FW_FALLBACK));
    CHECK(reboot(&r) == BOOT_RUN && r.state == BOOT_ST_IDLE && r.sec_counter == 6u && holds(LKG, s_b, s_blen));
    app_power_up();
    CHECK(!dtc_active(DTC_FW_FALLBACK) && h_to_armed()); /* an ordinary key cycle, the validation record bound to TI_FW_ID */
}

TEST(an_update_whose_first_boot_never_confirms_is_rolled_back_and_reported)
{
    boot_rec_t r;
    app_device();
    s_blen = build(s_b, TI_SKU_8XX_SIC, TI_FW_ID, 6u, 700u, 2u, NULL);
    CHECK(stage_over_uds(s_b, s_blen));
    CHECK(reboot(&r) == BOOT_RUN && r.state == BOOT_ST_TRIAL);
    app_power_up();
    h_run_ms(1000u); /* reset (a crash, a watchdog) before UPD_CONFIRM_MS */
    CHECK(rec().state == BOOT_ST_TRIAL);
    CHECK(reboot(&r) == BOOT_RUN && holds(EXEC, s_a, s_alen) && r.last == BOOT_LAST_FIRST_BOOT_FAILED && r.sec_counter == 5u);
    app_power_up();
    CHECK(dtc_active(DTC_FW_FALLBACK));
    h_run_ms(UPD_CONFIRM_MS + 100u);
    CHECK(rec().state == BOOT_ST_IDLE); /* the last known good is committed already: nothing to confirm */
}

/* ---------------- round 23 (item 11): the update blocks over ISO 15765-2 ---------------- */

/* A 1 MiB image (the region; the header included) downloaded end to end: 4093-byte blocks, each a first frame and 65
 * consecutive frames at 4 per flow control (the frames the 1 ms task reads), then verified, activated and reset — the
 * host-simulated download time is 4.6 s (it was 5 bytes per block, one block per task: 1 MiB in ~210 s). */
static uint8_t s_big[HAL_FLASH_REGION_SIZE];
TEST(a_1_mib_image_downloads_over_iso_tp_in_seconds)
{
    app_device();
    s_blen = build(s_big, TI_SKU_8XX_SIC, TI_FW_ID, 6u, HAL_FLASH_REGION_SIZE - IMG_HDR_LEN, 3u, NULL);
    CHECK(s_blen == (1024u * 1024u));
    uint32_t ms = 0u;
    CHECK(stage_over_uds_ms(s_big, s_blen, &ms) && holds(STAGE, s_big, s_blen) && rec().state == BOOT_ST_ACTIVATE);
    CHECK(s_iso_bs == UDS_DIAG_RX_BS && ms < 6000u);
    printf("    (item 11: a 1 MiB image in %u.%03u s of host-simulated time, %u blocks)\n", (unsigned)(ms / 1000u),
           (unsigned)(ms % 1000u), (unsigned)((s_blen + UPD_BLOCK_MAX - 3u) / (UPD_BLOCK_MAX - 2u)));
}

/* The segmented request's transport: a first frame longer than the receive buffer is refused with an overflow flow
 * control and nothing answers; a consecutive frame out of sequence abandons the reception (no response); one that never
 * comes abandons it after N_Cr (1 s) — the next request is served; a segmented request that is not a programming
 * service is NRC 0x13; TransferData outside the programming session is NRC 0x7F (the reassembled request reached it). */
TEST(segmented_requests_are_received_under_iso_15765_2)
{
    app_device();
    hal_can_frame_t f = {.id = UDS_ID_REQ, .len = 64u};
    hal_can_frame_t r;
    uint8_t rsp[8];
    uint32_t ms = 0u;
    static uint8_t q[UPD_BLOCK_MAX];
    (void)memset(q, 0x5Au, sizeof q);
    q[0] = 0x36u;
    q[1] = 0x01u;
    CHECK(app_rq_iso(q, UPD_BLOCK_MAX, rsp, &ms) && neg(rsp, 0x36u, UPD_NRC_NOT_IN_SESSION));
    q[0] = 0x22u; /* a long ReadDataByIdentifier: not a service that takes a segmented request */
    CHECK(app_rq_iso(q, 100u, rsp, &ms) && neg(rsp, 0x22u, UDS_NRC_LENGTH));
    (void)memset(f.data, 0xAA, sizeof f.data); /* an overflow: 4096 bytes announced in an escape first frame */
    f.data[0] = 0x10u;
    f.data[1] = 0x00u;
    f.data[2] = 0x00u;
    f.data[3] = 0x00u;
    f.data[4] = 0x10u;
    f.data[5] = 0x00u;
    sim_can_inject(HAL_CAN_DIAG, &f);
    h_run_ms(1u);
    CHECK(sim_can_pop_tx(HAL_CAN_DIAG, &r) && r.id == UDS_ID_RSP && r.data[0] == 0x32u);
    h_run_ms(5u);
    CHECK(!sim_can_pop_tx(HAL_CAN_DIAG, &r));
    f.data[0] = 0x10u; /* 200 bytes, then a consecutive frame with the wrong sequence number */
    f.data[1] = 200u;
    f.data[2] = 0x36u;
    sim_can_inject(HAL_CAN_DIAG, &f);
    h_run_ms(1u);
    CHECK(sim_can_pop_tx(HAL_CAN_DIAG, &r) && r.data[0] == 0x30u);
    f.data[0] = 0x22u; /* SN 2 where 1 is due */
    sim_can_inject(HAL_CAN_DIAG, &f);
    h_run_ms(1u);
    CHECK(!g_app.udsd.rx_on); /* abandoned at the wrong sequence number */
    f.data[0] = 0x21u;
    sim_can_inject(HAL_CAN_DIAG, &f);
    h_run_ms(5u);
    CHECK(!sim_can_pop_tx(HAL_CAN_DIAG, &r) && !g_app.udsd.rx_on); /* the later frames are nobody's */
    f.data[0] = 0x10u; /* a first frame, then silence */
    sim_can_inject(HAL_CAN_DIAG, &f);
    h_run_ms(1u);
    CHECK(sim_can_pop_tx(HAL_CAN_DIAG, &r) && r.data[0] == 0x30u);
    h_run_ms(1100u);
    CHECK(!g_app.udsd.rx_on && app_rq(PROG, 2u, rsp) && rsp[1] == 0x50u); /* N_Cr: abandoned; the bus serves on */
}

void suite_update(void)
{
    RUN(sha256_and_ed25519_match_the_standard_vectors);
    RUN(the_tool_and_the_firmware_agree_on_format_key_and_signature);
    RUN(every_refusal_names_its_reason);
    RUN(an_escape_single_frame_reaches_the_programming_services);
    RUN(header_parser_fuzz_and_every_header_bit_flip_is_refused);
    RUN(accepted_image_installs_runs_its_trial_and_commits_with_the_counter);
    RUN(refused_images_leave_exec_untouched_with_the_reason);
    RUN(power_loss_at_every_write_of_the_swap_boots_the_last_known_good);
    RUN(a_first_boot_that_never_confirms_falls_back_to_the_last_known_good);
    RUN(power_loss_during_the_commit_resumes_it_and_the_counter_never_goes_back);
    RUN(a_torn_record_write_never_takes_the_counter_back);
    RUN(an_exec_image_that_fails_its_check_is_restored_and_nothing_valid_stays_in_the_bootloader);
    RUN(programming_services_follow_the_protocol);
    RUN(update_refused_while_armed_with_hv_present_or_moving);
    RUN(the_update_state_cannot_arm_and_refuses_torque);
    RUN(an_update_over_uds_installs_confirms_itself_and_commits);
    RUN(an_update_whose_first_boot_never_confirms_is_rolled_back_and_reported);
    RUN(a_1_mib_image_downloads_over_iso_tp_in_seconds);
    RUN(segmented_requests_are_received_under_iso_15765_2);
}
