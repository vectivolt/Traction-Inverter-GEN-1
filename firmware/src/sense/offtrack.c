/* offtrack.c — FW-44 working offsets (see offtrack.h). */
#include "offtrack.h"

#include "ti_math.h"

void ofs_init(ofs_t *o, const isns_cal_t eol[3], const float *rec_v, const ti_params_t *p)
{
    bool ok = rec_v != NULL;
    for (uint32_t i = 0u; ok && (i < 3u); i++) {
        ok = ti_finite(rec_v[i]) && (ti_absf(rec_v[i] - eol[i].offset_v) <= p->cal_isns_offset_tol_v);
    }
    *o = (ofs_t){0};
    for (uint32_t i = 0u; i < 3u; i++) {
        o->run[i] = eol[i];
        if (ok) {
            o->run[i].offset_v = rec_v[i];
        }
    }
}

bool ofs_decide(ofs_t *o, const float mean_v[3], const isns_cal_t eol[3], bool may_adopt, const ti_params_t *p)
{
    if (o->decided) {
        return false; /* once per boot; the caller keeps it once per key cycle */
    }
    o->decided = true;
    if (!may_adopt || !isns_offset_ok(mean_v, eol, p)) {
        return false; /* outside the tolerance the key-on check refuses to arm, as before: nothing is adopted */
    }
    const float step = p->cal_isns_ofs_step_v;
    for (uint32_t i = 0u; i < 3u; i++) {
        o->run[i].offset_v += ti_clampf(mean_v[i] - o->run[i].offset_v, -step, step);
    }
    o->adopted = true;
    return true;
}
