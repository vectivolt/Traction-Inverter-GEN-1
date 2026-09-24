/* hal/nvm.h — record slots in data flash (target: Fee blocks on the S32K3 data flash).
 * Writes are asynchronous: start, then poll from the background loop. Nothing on a safety path
 * ever waits for NVM (FW-15, round 12 R2-F19). A power loss during a write may leave that slot
 * torn; the layer above (nvm/nvlog.c) keeps two slots per record and a CRC. */
#ifndef HAL_NVM_H
#define HAL_NVM_H

#include "ti_types.h"

#define HAL_NVM_SLOT_SIZE 512u
#define HAL_NVM_SLOTS 32u

typedef enum { HAL_NVM_IDLE = 0, HAL_NVM_BUSY, HAL_NVM_DONE_OK, HAL_NVM_DONE_ERR } hal_nvm_status_t;

bool hal_nvm_read(uint16_t slot, void *buf, uint32_t len);
bool hal_nvm_write_start(uint16_t slot, const void *buf, uint32_t len); /* false if busy */
hal_nvm_status_t hal_nvm_poll(void);

#endif /* HAL_NVM_H */
