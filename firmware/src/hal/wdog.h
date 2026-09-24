/* hal/wdog.h — the MCU's internal software watchdog (SWT0). The FS26 challenger watchdog
 * (FW-12) is the independent one and lives in safety/fs26.c. */
#ifndef HAL_WDOG_H
#define HAL_WDOG_H

#include "ti_types.h"

bool hal_wdog_init(uint32_t timeout_ms);
void hal_wdog_kick(void);

#endif /* HAL_WDOG_H */
