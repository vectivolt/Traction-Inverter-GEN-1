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
 *                     ti_elapsed()/ti_age() (unsigned subtraction), never with < or >
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
