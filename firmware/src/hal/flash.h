/* hal/flash.h — the program flash of the firmware update path (FW-38). Three image regions of
 * HAL_FLASH_REGION_SIZE bytes: EXEC (the image the bootloader starts), STAGE (where a download lands) and LKG (the
 * last known good copy the bootloader falls back to). Erase is per page (HAL_FLASH_PAGE, the S32K3 code-flash
 * sector); program is per HAL_FLASH_WORD-aligned unit into erased bytes only (ECC flash: no re-programming); read
 * is anywhere. Synchronous: only the background loop (the application's update state, boot/update.c) and the
 * bootloader (boot/boot.c) call it — never an ISR or the 1 ms task. false = the operation failed: the callers take
 * it as a power loss or a flash fault and stop (fail closed). */
#ifndef HAL_FLASH_H
#define HAL_FLASH_H

#include "ti_types.h"

#define HAL_FLASH_PAGE 8192u
#define HAL_FLASH_WORD 8u
#ifndef HAL_FLASH_PAGES
#define HAL_FLASH_PAGES 128u /* per region (1 MiB; round 23: a 1 MiB image on the host); the target build sets it from its
                              * memory map (T-44) */
#endif
#define HAL_FLASH_REGION_SIZE (HAL_FLASH_PAGES * HAL_FLASH_PAGE)

typedef enum { HAL_FLASH_EXEC = 0, HAL_FLASH_STAGE, HAL_FLASH_LKG, HAL_FLASH_REGIONS } hal_flash_region_t;

bool hal_flash_erase(hal_flash_region_t r, uint32_t page);
bool hal_flash_program(hal_flash_region_t r, uint32_t off, const void *src, uint32_t len);
bool hal_flash_read(hal_flash_region_t r, uint32_t off, void *dst, uint32_t len);
/* The release key's OTP/HSE copy; false when the platform has none (verify.c then uses its const table). */
bool hal_flash_pubkey(uint8_t key[32]);
/* MCU reset into the bootloader (UDS ECUReset in the programming session). */
void hal_sys_reset(void);

#endif /* HAL_FLASH_H */
