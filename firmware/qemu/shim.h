/* qemu/shim.h — what newlib 4.5 lacks, force-included (-include) only where make qemu-test needs it.
 * C11 timespec_get: newlib has no TIME_UTC. The calendar clock of a semihosted target is SYS_TIME, whole
 * seconds, so a host-timing measurement taken with it reads 0 under QEMU (see qemu.mk). */
#ifndef QEMU_SHIM_H
#define QEMU_SHIM_H
#include <time.h>
#ifndef TIME_UTC
#define TIME_UTC 1
static inline int timespec_get(struct timespec *ts, int base)
{
    if (base != TIME_UTC) {
        return 0;
    }
    ts->tv_sec = time(NULL);
    ts->tv_nsec = 0;
    return base;
}
#endif
#endif /* QEMU_SHIM_H */
