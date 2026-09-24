/* hal/timer.h — time base, bounded busy-wait and critical sections.
 * Target: STM free-running counter at 1 MHz (see platform/s32k396/s32k396_io.c). */
#ifndef HAL_TIMER_H
#define HAL_TIMER_H

#include "ti_types.h"

/* Free-running microseconds (wraps; compare with ti_elapsed()). */
uint32_t hal_time_us(void);

/* Busy-wait; callers keep us <= 1000 (FW-16 µs checks, §4c dead-time wait). */
void hal_delay_us(uint32_t us);

/* Nested critical section (PRIMASK on the M7). Used only around the NVM queue indices. */
void hal_crit_enter(void);
void hal_crit_exit(void);

#endif /* HAL_TIMER_H */
