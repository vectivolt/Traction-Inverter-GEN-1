/* s32k396_flash.c — hal/flash.h on the S32K396 (FW-38): a STUB until bring-up. Every operation fails, which the
 * callers treat as fail-closed — a download ends in NRC 0x72, a verification in IMG_ERR_READ, nothing is activated
 * — and there is no OTP/HSE key copy (verify.c then uses the release build's key, if it has one). The bootloader
 * binary that runs boot/boot.c at every reset is a separate bring-up item (docs/target-bringup.md, FW-38). */
#include "flash.h"

/* TODO(RM): the three regions' base addresses in the S32K39 code-flash map — EXEC at the application's link address,
 * STAGE and LKG each in a read-while-write partition other than the one the running code executes from, so an erase
 * never stalls the ISRs — and HAL_FLASH_PAGES from their size. */
bool hal_flash_read(hal_flash_region_t r, uint32_t off, void *dst, uint32_t len)
{
    (void)r;
    (void)off;
    (void)dst;
    (void)len;
    return false;
}

/* TODO(RTD): C40_Ip sector erase and program (unlock, erase or program, wait, lock), executed from RAM or the other
 * partition with the interrupts enabled; an ECC or program/erase error returns false. */
bool hal_flash_erase(hal_flash_region_t r, uint32_t page)
{
    (void)r;
    (void)page;
    return false;
}

bool hal_flash_program(hal_flash_region_t r, uint32_t off, const void *src, uint32_t len)
{
    (void)r;
    (void)off;
    (void)src;
    (void)len;
    return false;
}

/* TODO(REL): the release key's public half from the HSE key catalog (or an OTP field locked at EOL). */
bool hal_flash_pubkey(uint8_t key[32])
{
    (void)key;
    return false;
}

/* TODO(RTD): the functional reset (MC_RGM / Power_Ip) after the ECUReset response has left the CAN mailbox. */
void hal_sys_reset(void) {}
