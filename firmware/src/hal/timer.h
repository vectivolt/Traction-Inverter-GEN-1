/* hal/timer.h — time base, bounded busy-wait and critical sections.
 * Target: STM free-running counter at 1 MHz (see platform/s32k396/s32k396_io.c).
 *
 * One time domain (A12-R06). The 32-bit microsecond counter wraps every 4294.967 s. Dividing it by
 * 1000 gave a millisecond value that jumped from 4294967 to 0 instead of wrapping modulo 2^32, so
 * ti_elapsed() on it read an 8 ms-old frame as 4 290 672 337 ms old. Now:
 *   - microsecond intervals (< 71 min) use hal_time_us() and ti_elapsed(): modulo 2^32, correct;
 *   - every millisecond time stamp comes from hal_time_ms() = (uint32_t)(us64 / 1000), which wraps
 *     at 2^32 ms (49.7 days) like any other uint32 and so keeps ti_elapsed()/ti_age() valid;
 *   - us64 is the 32-bit counter extended by a high word (ti_time64_extend below, the same code on
 *     both platforms). It needs one read per counter wrap: the 1 ms task reads it every millisecond.
 *     The read and the extension run inside hal_crit_enter/exit, so the fault ISR, the current-loop
 *     ISR and the task see one consistent value. */
#ifndef HAL_TIMER_H
#define HAL_TIMER_H

#include "ti_types.h"

/* Free-running microseconds (wraps; compare with ti_elapsed()). Short intervals only. */
uint32_t hal_time_us(void);

/* Monotonic microseconds since the time base started (never wraps in service). */
uint64_t hal_time_us64(void);

/* n / d for a 64-bit n and a divisor 1..65535 with 32-bit arithmetic only: four 16-bit digits, one hardware UDIV each.
 * The current-loop ISR (a DTC's time stamp) and the SDADC completion ISR (resync: the block a completion belongs to)
 * divide 64-bit microseconds; a plain `/` there links libgcc's __aeabi_uldivmod (a ~100-cycle software loop, T-36) —
 * `make target-size` refuses that symbol in every object outside the task/UDS ones. Precondition: d in 1..65535 (the
 * callers' divisors are 1000 and the SDADC block period, 100 µs). Exact: tests/test_time.c against the 64-bit `/`. */
static inline uint64_t ti_udiv64_16(uint64_t n, uint32_t d, uint32_t *rem)
{
    uint32_t r = 0u;
    uint64_t q = 0u;
    for (uint32_t i = 4u; i > 0u; i--) {
        const uint32_t cur = (r << 16) | (uint32_t)((n >> (16u * (i - 1u))) & 0xFFFFu);
        q = (q << 16) | (uint64_t)(cur / d);
        r = cur % d;
    }
    if (rem != NULL) {
        *rem = r;
    }
    return q;
}

/* The one millisecond domain. */
static inline uint32_t ti_ms_from_us64(uint64_t us) { return (uint32_t)ti_udiv64_16(us, 1000u, NULL); }
static inline uint32_t hal_time_ms(void) { return ti_ms_from_us64(hal_time_us64()); }

/* The extension shared by both platforms: the high word advances when the raw counter reads below
 * the previous read. Call it with the raw counter inside hal_crit_enter/exit. */
typedef struct {
    uint32_t last;
    uint32_t hi;
} ti_time64_t;

static inline uint64_t ti_time64_extend(ti_time64_t *s, uint32_t raw)
{
    if (raw < s->last) {
        s->hi++;
    }
    s->last = raw;
    return ((uint64_t)s->hi << 32) | (uint64_t)raw;
}

/* Busy-wait; callers keep us <= 1000 (FW-16 µs checks, §4c dead-time wait). */
void hal_delay_us(uint32_t us);

/* Nested critical section (PRIMASK on the M7): the NVM queue indices and the 64-bit time read. */
void hal_crit_enter(void);
void hal_crit_exit(void);

#endif /* HAL_TIMER_H */
