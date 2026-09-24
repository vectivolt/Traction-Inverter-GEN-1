/* gains.c — per-f_sw PI gains with the §2 crossover ceiling enforced. */
#include "gains.h"

#include "ti_math.h"

float gains_ceiling_hz(const ti_params_t *p, uint32_t fsw_hz)
{
    for (uint32_t i = 0u; (i < p->n_fsw) && (i < 2u); i++) {
        if (p->fsw_hz[i] == fsw_hz) {
            return p->fc_ceiling_hz[i];
        }
    }
    return 0.0f;
}

bool gains_compute(const ti_params_t *p, uint32_t fsw_hz, float fc_hz, float l_h, float r_ohm, gain_set_t *g)
{
    const float ceiling = gains_ceiling_hz(p, fsw_hz);
    const bool ok = (ceiling > 0.0f) && (fc_hz > 0.0f) && (fc_hz <= ceiling) && (l_h > 1.0e-6f) &&
                    (l_h < 10.0e-3f) && (r_ohm > 1.0e-4f) && (r_ohm < 1.0f);
    if (!ok) {
        return false;
    }
    const float wc = TI_2PI * fc_hz;
    g->fsw_hz = fsw_hz;
    g->fc_hz = fc_hz;
    g->kp_v_per_a = l_h * wc;
    g->ki_v_per_as = r_ohm * wc;
    g->ts_s = 0.5f / (float)fsw_hz;
    g->delay_s = p->s6_delay_tsw / (float)fsw_hz;
    return true;
}

bool gains_default(const ti_params_t *p, uint32_t fsw_hz, float l_h, float r_ohm, gain_set_t *g)
{
    return gains_compute(p, fsw_hz, p->cal_fc_fraction * gains_ceiling_hz(p, fsw_hz), l_h, r_ohm, g);
}
