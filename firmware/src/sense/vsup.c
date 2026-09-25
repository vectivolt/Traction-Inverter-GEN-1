/* vsup.c — LV supply supervision (round 17). */
#include "vsup.h"

void vsup_init(vsup_t *s) { *s = (vsup_t){0}; }

void vsup_update(vsup_t *s, uint16_t code, uint32_t now_ms, const ti_params_t *p)
{
    s->v = ti_code_to_v(code) * p->vsup_amux_ratio;
    s->valid = s->v >= p->vsup_valid_min_v;
    const bool ov = s->valid && (s->v > (s->ov ? (p->vsup_ov_v - p->vsup_ov_hyst_v) : p->vsup_ov_v));
    if (ov && !s->ov) {
        s->t_ov_ms = now_ms;
    }
    const bool hi = ov && (s->v > p->cal_vsup_jump_max_v);
    if (hi && !s->hi) {
        s->t_hi_ms = now_ms;
    }
    const bool too_long = (hi && ti_elapsed(now_ms, s->t_hi_ms, p->cal_vsup_ld_ms + 1u)) ||
                          (ov && ti_elapsed(now_ms, s->t_ov_ms, p->cal_vsup_jump_ms + 1u));
    s->sustained = ov && (s->sustained || too_long);
    s->ov = ov;
    s->hi = hi;
}
