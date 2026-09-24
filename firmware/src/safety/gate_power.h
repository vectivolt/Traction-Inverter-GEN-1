/* gate_power.h — FW-14 gate-power sequencing. Flyback enables are MCU_EN_FLYBK_x OR FS_GPIO1
 * (the SBC alone keeps gate power up for FS1B-ASC through an MCU reset). §9 step 6 raises both
 * MCU enables and FS_GPIO1 — never while FS1B is still asserted at boot (FS_GPIO1 stays low from
 * POR until then). RDY_HS/RDY_LS low => no PWM (the hardware drops DRV_EN; firmware also forces
 * the PWM off); gate-supply undervoltage is signalled on RDY, not FLT, and recovers with RDY. */
#ifndef GATE_POWER_H
#define GATE_POWER_H

#include "fs26.h"

typedef enum { GP_OFF = 0, GP_STARTING, GP_READY, GP_LOST } gp_state_t;

typedef struct {
    gp_state_t st;
    uint32_t t_start_ms;
    bool timeout_dtc;
} gp_t;

void gp_init(gp_t *g);
/* Refused (false) unless FS0B/FS1B are released. */
bool gp_request_on(gp_t *g, fs26_t *fs, bool fs1b_released, uint32_t now_ms);
void gp_off(gp_t *g, fs26_t *fs);
gp_state_t gp_step(gp_t *g, uint32_t now_ms, const ti_params_t *p);
bool gp_rdy_both(void);

#endif /* GATE_POWER_H */
