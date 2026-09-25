/* params.c — the four generated parameter sets, the build's active set and validation. */
#include "ti_params.h"

#include "cal_ranges.h"
#include "params_4xx_igbt.h"
#include "params_4xx_sic.h"
#include "params_8xx_igbt.h"
#include "params_8xx_sic.h"

static const ti_params_t PARAMS[TI_SKU_COUNT - 1] = {
    TI_PARAMS_8XX_SIC_INIT,
    TI_PARAMS_8XX_IGBT_INIT,
    TI_PARAMS_4XX_IGBT_INIT,
    TI_PARAMS_4XX_SIC_INIT,
};

static const ti_cal_range_t RANGES[] = TI_CAL_RANGES_INIT;

#ifndef TI_SKU_BUILD
#define TI_SKU_BUILD TI_SKU_8XX_SIC
#endif

static ti_sku_t s_active = TI_SKU_BUILD;

const ti_params_t *ti_params_get(ti_sku_t sku)
{
    if ((sku <= TI_SKU_NONE) || (sku >= TI_SKU_COUNT)) {
        return NULL;
    }
    return &PARAMS[(uint32_t)sku - 1u];
}

const ti_params_t *ti_params_active(void) { return ti_params_get(s_active); }

void ti_params_select(ti_sku_t sku)
{
    if (ti_params_get(sku) != NULL) {
        s_active = sku;
    }
}

static float cal_value(const ti_params_t *p, const ti_cal_range_t *r)
{
    const uint8_t *base = (const uint8_t *)p + r->offset;
    switch (r->type) {
    case TI_CAL_F32: return *(const float *)(const void *)base;
    case TI_CAL_U32: return (float)*(const uint32_t *)(const void *)base;
    case TI_CAL_U16: return (float)*(const uint16_t *)(const void *)base;
    case TI_CAL_U8: return (float)*base;
    default: return -1.0e30f;
    }
}

uint32_t ti_params_validate(const ti_params_t *p)
{
    uint32_t bad = 0u;
    if ((p == NULL) || (p->sku <= TI_SKU_NONE) || (p->sku >= TI_SKU_COUNT)) {
        return 1u;
    }
    /* physical ordering the rest of the code relies on */
    bad += (p->vdc_min_v < p->vdc_max_v) ? 0u : 1u;
    bad += (p->vdc_max_v < p->ov_trip_v) ? 0u : 1u;
    bad += (p->ov_trip_v < p->cap_un_v) ? 0u : 1u;
    bad += ((p->c_min_f < p->c_nom_f) && (p->c_nom_f < p->c_max_f)) ? 0u : 1u;
    bad += (p->i_crest_a < p->i_oc_trip_a) ? 0u : 1u; /* FW-05: the crest must not trip */
    bad += ((p->n_fsw >= 1u) && (p->n_fsw <= 2u)) ? 0u : 1u;
    for (uint32_t i = 0u; (i < p->n_fsw) && (i < 2u); i++) {
        bad += ((p->fsw_hz[i] >= 2000u) && (p->fc_ceiling_hz[i] > 0.0f) &&
                (p->fc_ceiling_hz[i] < ((float)p->fsw_hz[i] * 0.2f))) ? 0u : 1u;
    }
    bad += (p->dead_time_ns > 0u) ? 0u : 1u;
    bad += (p->vofs_min_v < p->vofs_max_v) ? 0u : 1u;
    bad += (p->v5gd_min_v < p->v5gd_max_v) ? 0u : 1u;
    bad += (p->isns_valid_min_v < p->isns_valid_max_v) ? 0u : 1u;
    bad += (p->cal_ign_off_v < p->cal_ign_on_v) ? 0u : 1u;
    bad += (p->cal_tmod_derate_start_c < p->cal_tmod_derate_end_c) ? 0u : 1u;
    bad += (p->cal_coolant_derate_start_c < p->cal_coolant_derate_end_c) ? 0u : 1u;
    bad += (p->cal_rslv_amp_min < p->cal_rslv_amp_max) ? 0u : 1u;
    bad += (p->fs26_wd_err_limit == 2u) ? 0u : 1u; /* FW-12 fixes it */
    /* round 16 (A14-N01): the monitor-plane setpoint must be reachable by the SWG at its LOW corner (the
     * trim's headroom), keep the amplifier under its -40 degC slew ceiling, and give the cold winding at
     * least the resolver floor */
    const float amp_vpp = p->cal_rslv_exc_target_vpp * p->exc_amp_per_mon;
    bad += ((amp_vpp / p->exc_gain) <= p->swg_maxapp_min_vpp) ? 0u : 1u;
    bad += (amp_vpp <= p->exc_slew_max_vpp) ? 0u : 1u;
    bad += ((p->cal_rslv_exc_target_vpp * p->cal_rslv_wind_per_mon) >= p->rslv_floor_vpp) ? 0u : 1u;
    for (uint32_t i = 0u; i < TI_ARRAY_LEN(RANGES); i++) {
        const float v = cal_value(p, &RANGES[i]);
        bad += ((v >= RANGES[i].min) && (v <= RANGES[i].max)) ? 0u : 1u;
    }
    return bad;
}
