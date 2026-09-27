/* sim_flash.c — see sim_flash.h. */
#include "sim_flash.h"

#include <string.h>

static uint8_t s_mem[HAL_FLASH_REGIONS][HAL_FLASH_REGION_SIZE];
static uint32_t s_writes;
static uint32_t s_left; /* write operations left before an armed power loss */
static bool s_armed;
static bool s_dead;
static uint8_t s_key[32];
static bool s_has_key;
static bool s_reset;

/* false: no power (torn = this is the operation the loss hit) */
static bool powered(bool *torn)
{
    *torn = false;
    if (s_dead) {
        return false;
    }
    if (s_armed) {
        if (s_left == 0u) {
            s_dead = true;
            *torn = true;
            return false;
        }
        s_left--;
    }
    return true;
}

bool hal_flash_erase(hal_flash_region_t r, uint32_t page)
{
    if ((r >= HAL_FLASH_REGIONS) || (page >= HAL_FLASH_PAGES)) {
        return false;
    }
    uint8_t *p = &s_mem[r][page * HAL_FLASH_PAGE];
    bool torn;
    if (!powered(&torn)) {
        if (torn) {
            (void)memset(p, 0xFF, HAL_FLASH_PAGE / 2u);
        }
        return false;
    }
    (void)memset(p, 0xFF, HAL_FLASH_PAGE);
    s_writes++;
    return true;
}

bool hal_flash_program(hal_flash_region_t r, uint32_t off, const void *src, uint32_t len)
{
    if ((r >= HAL_FLASH_REGIONS) || (len == 0u) || ((off % HAL_FLASH_WORD) != 0u) || ((len % HAL_FLASH_WORD) != 0u) ||
        (off > HAL_FLASH_REGION_SIZE) || (len > (HAL_FLASH_REGION_SIZE - off))) {
        return false;
    }
    uint8_t *d = &s_mem[r][off];
    for (uint32_t i = 0u; i < len; i++) {
        if (d[i] != 0xFFu) {
            return false; /* not erased: ECC flash cannot be programmed twice */
        }
    }
    bool torn;
    if (!powered(&torn)) {
        if (torn) {
            (void)memcpy(d, src, len / 2u);
        }
        return false;
    }
    (void)memcpy(d, src, len);
    s_writes++;
    return true;
}

bool hal_flash_read(hal_flash_region_t r, uint32_t off, void *dst, uint32_t len)
{
    if (s_dead || (r >= HAL_FLASH_REGIONS) || (off > HAL_FLASH_REGION_SIZE) || (len > (HAL_FLASH_REGION_SIZE - off))) {
        return false;
    }
    (void)memcpy(dst, &s_mem[r][off], len);
    return true;
}

bool hal_flash_pubkey(uint8_t key[32])
{
    if (s_has_key) {
        (void)memcpy(key, s_key, 32u);
    }
    return s_has_key;
}

void hal_sys_reset(void) { s_reset = true; }

void sim_flash_wipe(void)
{
    (void)memset(s_mem, 0xFF, sizeof s_mem);
    s_writes = 0u;
    s_armed = false;
    s_dead = false;
    s_has_key = false;
    s_reset = false;
}

void sim_flash_power_loss_after(uint32_t n)
{
    s_armed = true;
    s_left = n;
    s_dead = false;
}

void sim_flash_power_on(void)
{
    s_armed = false;
    s_dead = false;
}

uint32_t sim_flash_writes(void) { return s_writes; }
uint8_t *sim_flash_mem(hal_flash_region_t r) { return s_mem[r]; }

void sim_flash_set_pubkey(const uint8_t *key)
{
    s_has_key = (key != NULL);
    if (s_has_key) {
        (void)memcpy(s_key, key, 32u);
    }
}

bool sim_flash_take_reset(void)
{
    const bool r = s_reset;
    s_reset = false;
    return r;
}
