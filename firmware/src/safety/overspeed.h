/* overspeed.h — FW-42 (round 23): overspeed protection. The measured speed — the resolver's, and only while the
 * resolver is valid — against the calibration record's n_max_rpm, in the 1 ms task (app.c, before detect()):
 *   OVS_WARN  from cal_ovs_warn_frac x n_max (1.00): the §6 command-lost row (the torque ramped to zero: SPO below
 *             n_x, current control kept above it with the battery present), the speed-limit request on CAN, no
 *             arming (MCU_GATE_EN is not raised from DISARMED), DTC_OVERSPEED;
 *   OVS_TRIP  from cal_ovs_trip_frac x n_max (1.05): in addition the §6 "Resolver invalid, or control lost" row —
 *             SPO below n_x under the energy rule (LS-ASC where neither rule (a) nor (b) holds), LS-ASC at or above
 *             it — latched until a VCU fault reset below n_x. Never an ASC of its own: the matrix decides.
 * A band is entered or left only after cal_ovs_debounce_ms consecutive samples asking for it (whichever band that
 * is), and left cal_ovs_hyst_frac x n_max below its start. Both directions of rotation count (|n|). Without a
 * measured speed the band is held and nothing counts: an invalid resolver is its own §6 row, and the speed bound
 * after it (round 23: cal_speed_accel_max_rpm_s) is a column choice, never a measurement. Pure logic. */
#ifndef OVERSPEED_H
#define OVERSPEED_H

#include "ti_params.h"

typedef enum { OVS_NONE = 0, OVS_WARN, OVS_TRIP } ovs_band_t;

typedef struct {
    ovs_band_t band; /* debounced, with hysteresis */
    uint32_t n_ms;   /* consecutive samples asking for another band */
} ovs_t;

void ovs_init(ovs_t *o);
/* One 1 ms sample. measured: speed_rpm is the valid resolver's speed of this tick. */
ovs_band_t ovs_step(ovs_t *o, float speed_rpm, bool measured, float n_max_rpm, const ti_params_t *p);

#endif /* OVERSPEED_H */
