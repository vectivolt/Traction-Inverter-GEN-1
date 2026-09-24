/* calib.h — FW-20 calibration record: versioned, CRC-32 and range checked, bound to the hardware
 * serial, the SKU (FW-02) and a motor ID. Any failure => no torque. Contents: current offset/gain/
 * sign per phase, V_DC gain/offset per channel, resolver gains/offsets/phase/electrical zero and
 * pole pairs, the motor data the §6 energy rule and n_x need, motor temperature sensor type, an
 * optional MTPA table and the selected f_sw. */
#ifndef CALIB_H
#define CALIB_H

#include "current.h"
#include "motor.h"
#include "resolver.h"
#include "temp.h"
#include "torque.h"
#include "vdc.h"

#define CALIB_LAYOUT_VERSION 1u

#define CAL_ERR_MISSING 0x001u
#define CAL_ERR_VERSION 0x002u
#define CAL_ERR_CRC 0x004u
#define CAL_ERR_RANGE 0x008u
#define CAL_ERR_SKU 0x010u
#define CAL_ERR_SERIAL 0x020u
#define CAL_ERR_MOTOR_ID 0x040u
#define CAL_ERR_FSW 0x080u

typedef struct {
    uint16_t layout_version;
    uint8_t sku;
    uint8_t pad0;
    uint8_t hw_serial[8];
    uint32_t motor_id;
    uint32_t fsw_hz;
    isns_cal_t isns[3];
    vdc_cal_t vdc[2];
    rslv_cal_t rslv;
    motor_t motor;
    temp_mt_cal_t mt;
    mtpa_lut_t mtpa;
    uint32_t crc32; /* over every byte above */
} calib_t;

/* Nominal record for a SKU (datasheet scalings + the S6 screening motor). motor_id = 0 (unbound):
 * it never passes calib_check() until commissioning binds a motor. */
void calib_nominal(calib_t *c, const ti_params_t *p, const uint8_t serial[8]);
void calib_seal(calib_t *c);
uint32_t calib_check(const calib_t *c, const ti_params_t *p, const uint8_t serial[8]);

#endif /* CALIB_H */
