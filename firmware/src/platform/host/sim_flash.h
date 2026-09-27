/* sim_flash.h — host model of the FW-38 program-flash regions (hal/flash.h), tests only. Pages erase to 0xFF;
 * programming needs word alignment and erased bytes (ECC flash). A power loss can be injected after any number of
 * erase/program operations: that operation is torn — an erase leaves only the first half of its page erased, a
 * program writes only the first half of its data (the NVM model's pattern, sim_nvm_power_loss) — and every later
 * operation, reads included, fails until the power comes back. */
#ifndef SIM_FLASH_H
#define SIM_FLASH_H

#include "flash.h"

void sim_flash_wipe(void);                    /* every region erased, power on, no OTP key, counters cleared */
void sim_flash_power_loss_after(uint32_t n);  /* n more erase/program operations complete, the next is torn */
void sim_flash_power_on(void);                /* the next power-up */
uint32_t sim_flash_writes(void);              /* erase + program operations completed since the wipe */
uint8_t *sim_flash_mem(hal_flash_region_t r); /* direct access: a test inspects or corrupts a region */
void sim_flash_set_pubkey(const uint8_t *key); /* the platform's OTP/HSE key copy; NULL = none */
bool sim_flash_take_reset(void);              /* hal_sys_reset() was called since the last take */

#endif /* SIM_FLASH_H */
