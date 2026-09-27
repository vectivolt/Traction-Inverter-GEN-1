/* offtrack.h — FW-44 (round 23): key-on current-offset refresh. The key-on check (§9 step 3, app.c) compares each
 * phase channel's standstill zero-current mean with the EOL calibration record's offset and refuses to arm beyond
 * cal_isns_offset_tol_v: unchanged, and always against the EOL record. In addition, when that check passes, the
 * WORKING offset — the one the current conversion (isns_update) and the FW-05 hardware compare (isns_oc_codes) use —
 * moves toward the fresh mean by at most cal_isns_ofs_step_v: once per key cycle, only with the bridge disarmed, the
 * PWM off and the rotor at standstill (never while armed or moving, never mid-run), so slow drift over the vehicle's
 * life is tracked without a jump. The working value stays inside the EOL tolerance by construction (a step from a
 * value inside it toward a mean inside it). The EOL record itself (calib_t) is never modified. The tracked offsets
 * persist in the run-time record (nvm/runstats.h) bound to the calibration record's CRC; a record for another
 * calibration, or a tracked offset outside the EOL tolerance, is ignored: the EOL values. Pure logic. */
#ifndef OFFTRACK_H
#define OFFTRACK_H

#include "current.h"

typedef struct {
    isns_cal_t run[3]; /* the working calibration: EOL gain and sign, the tracked offset */
    bool decided;      /* the key-on decision of this boot is taken */
    bool adopted;      /* ... and it adopted a step */
} ofs_t;

/* At boot: the working offsets are the tracked ones rec_v (when given, finite, each inside the EOL tolerance), else
 * the EOL offsets. */
void ofs_init(ofs_t *o, const isns_cal_t eol[3], const float *rec_v, const ti_params_t *p);
/* The key-on decision, once per boot. may_adopt: bridge disarmed, PWM off, standstill, no adoption yet this key
 * cycle. Adopts only when mean_v passes the key-on check against EOL (isns_offset_ok, the same function): each
 * working offset then moves toward mean_v by at most cal_isns_ofs_step_v. true = adopted. */
bool ofs_decide(ofs_t *o, const float mean_v[3], const isns_cal_t eol[3], bool may_adopt, const ti_params_t *p);

#endif /* OFFTRACK_H */
