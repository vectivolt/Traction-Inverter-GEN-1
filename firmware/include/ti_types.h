/* ti_types.h — base types and the unit conventions of the whole traction-inverter firmware.
 *
 * Units (integer-safe conventions, one per quantity — names carry the unit suffix):
 *   current      A    instantaneous amperes (the contract's A rms appears only as *_rms_a)
 *   voltage      V    volts (link, pin or rail; the name says which)
 *   temperature  degC float
 *   speed        rpm  mechanical; electrical rad/s exists only inside control/ (suffix _rad_s)
 *   torque       Nm   float;  power W float
 *   angle        rad  electrical unless suffixed _mech; wrapped to [0, 2*pi)
 *   time         us   uint32_t free-running microseconds, wraps every 71.6 min — compare only with
 *                     ti_elapsed()/ti_age() (unsigned subtraction), never with < or >; a SENSOR stamp's
 *                     freshness with ti_stale() (signed: the stamp may postdate the check, round 18), a
 *                     slow-list sample's acquisition with ti_acq() (new / held / expired, round 24)
 *                ms   uint32_t for slow timers (1 kHz task), same wrap rules; only from hal_time_ms()
 *                     (hal/timer.h: us64 / 1000), never hal_time_us() / 1000 (A12-R06)
 *   ADC          code uint16_t 12-bit, 0..4095 = VREFL..VREFH (VREF5 = 5.0 V, ratiometric)
 * Every sensor value that leaves sense/ carries a validity flag and a sample time (ti_meas_t).
 * MISRA-style rules used throughout: fixed-width types, no recursion, no dynamic memory, every loop
 * has a compile-time bound, single-precision float only (-Wdouble-promotion is an error). */
#ifndef TI_TYPES_H
#define TI_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    TI_SKU_NONE = 0,
    TI_SKU_8XX_SIC,
    TI_SKU_8XX_IGBT,
    TI_SKU_4XX_IGBT,
    TI_SKU_4XX_SIC,
    TI_SKU_COUNT
} ti_sku_t;

/* A measured value with its validity and the time it was sampled. */
typedef struct {
    float v;
    bool valid;
    uint32_t t_us;
} ti_meas_t;

/* HV state reported to the vehicle (FW-18: invalid witness => UNKNOWN, never SAFE). */
typedef enum { TI_HV_UNKNOWN = 0, TI_HV_SAFE, TI_HV_PRESENT } ti_hv_state_t;

/* Main-contactor state as reported by the VCU/BMS. */
typedef enum { TI_CONT_INVALID = 0, TI_CONT_OPEN, TI_CONT_PRECHARGE, TI_CONT_CLOSED } ti_contactor_t;

/* Gear / direction request (FW-11 direction interlock). */
typedef enum { TI_GEAR_N = 0, TI_GEAR_D, TI_GEAR_R, TI_GEAR_P } ti_gear_t;

#define TI_ADC_MAX_CODE 4095u
#define TI_ADC_VREF_V 5.0f

static inline bool ti_elapsed(uint32_t now, uint32_t since, uint32_t duration)
{
    return (uint32_t)(now - since) >= duration;
}

static inline uint32_t ti_age(uint32_t now, uint32_t since)
{
    return (uint32_t)(now - since);
}

/* Round 18 (A16-R01): freshness of a SENSOR stamp against a check time. The stamp may postdate `now`: the
 * target stamps a sample when it reads it, after an ISR read its entry time, and a higher-priority interrupt
 * may publish a newer stamp while a lower context holds an older time. So the age is signed: stale when the
 * stamp is `hold` or more before now — and when it is `hold` or more after it, which no ISR's execution
 * reaches (a corrupt stamp is refused either way). Wrap-safe for stamps within 2^31 us (±35 min) of now.
 * Timers (a start the same context wrote) keep ti_elapsed(). */
static inline bool ti_stale(uint32_t now, uint32_t stamp, uint32_t hold)
{
    const uint32_t age = now - stamp;
    return (age >= hold) && ((0u - age) >= hold);
}

/* Round 24 (F241): the acquisition of a slow-list sample. hal_adc_read() returns the latest conversion's code, the time
 * the platform fetched it and whether the channel ever converted — the same code and stamp, however often it is read,
 * until the next conversion. A consumer judges every read against the stamp it last took (*l):
 *   NEW      converted, another stamp than the one taken, fresh: take it (the stamp is recorded);
 *   HELD     the stamp already taken, still fresh: nothing new — nothing is counted again, the consumer's verdict stands;
 *   EXPIRED  never converted, or the stamp `hold` or more from now (ti_stale: signed, wrap-safe): the reading is
 *            withdrawn — and stays withdrawn until a NEW sample: the same stamp again, which ti_stale reads as fresh near
 *            every 2^32 us of a stopped channel's age, does not revive it.
 * A constant input converted on schedule gives a new stamp every time: NEW, never a "must change" check. */
typedef enum { TI_ACQ_NEW = 0, TI_ACQ_HELD, TI_ACQ_EXPIRED } ti_acq_t;

typedef struct {
    uint32_t t_us; /* the stamp last taken */
    bool taken;    /* a stamp was taken */
    bool expired;  /* the last judgement was EXPIRED */
} ti_acq_last_t;

static inline ti_acq_t ti_acq(ti_acq_last_t *l, bool seen, uint32_t t_us, uint32_t now_us, uint32_t hold_us)
{
    const bool same = l->taken && (t_us == l->t_us);
    if (seen) { /* every stamp seen is recorded, judged or not (F244 hardening): a stamp that is already stale the first
                 * time it is read would otherwise never be "the same" and could come back as NEW near the 32-bit wrap */
        l->t_us = t_us;
        l->taken = true;
    }
    l->expired = !seen || ti_stale(now_us, t_us, hold_us) || (same && l->expired);
    if (l->expired) {
        return TI_ACQ_EXPIRED;
    }
    if (same) {
        return TI_ACQ_HELD;
    }
    return TI_ACQ_NEW;
}

static inline float ti_code_to_v(uint16_t code)
{
    return (float)code * (TI_ADC_VREF_V / (float)TI_ADC_MAX_CODE);
}

/* Retained RAM (survives an MCU reset, not a power loss): the linker script places .ti_retained
 * in a no-init region on the target; on the host it is ordinary static storage. */
#if defined(TI_TARGET_S32K396)
#define TI_RETAINED __attribute__((section(".ti_retained")))
#else
#define TI_RETAINED
#endif

#define TI_ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

#endif /* TI_TYPES_H */
