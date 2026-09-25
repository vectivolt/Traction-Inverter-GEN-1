/* ti_params.h — the per-SKU parameter set (FW-02: it carries its SKU).
 *
 * Every threshold of the firmware lives here. The four instances are GENERATED into
 * include/params_<sku>.h by tools/gen-params.mjs from the contract tables
 * (docs/firmware-contract.md rev A.12 + the design-verify SKU data); do not edit them by hand.
 * Fields named cal_* are calibration values the contract does not fix (hardware timings and
 * tolerances nobody can know before HIL/bench): each has a default and a [min, max] range in
 * include/cal_ranges.h, and params_validate() rejects a set that leaves its range. */
#ifndef TI_PARAMS_H
#define TI_PARAMS_H

#include "ti_types.h"

typedef struct {
    /* ---- identity (FW-02) ---- */
    ti_sku_t sku;
    const char *name;
    bool sic;    /* SiC module (true) or IGBT */
    bool class8; /* 8XX (850 V) or 4XX (500 V) voltage class */

    /* ---- §2 link voltage and bank ---- */
    float vdc_min_v;
    float vdc_max_v;
    float ov_trip_v; /* both channels, hardware compare (FW-06) */
    float cap_un_v;  /* can U_N at 85 degC: 1000 V 8XX / 600 V 4XX (§6 rule (a)) */
    float c_nom_f;
    float c_min_f; /* -10 % + local: the §6 rule (a) C_min */
    float c_max_f; /* +10 % + local: FW-16 energy bound */

    /* ---- §2 currents ---- */
    float i_pk_rms_a;
    float i_cont_rms_a;
    float i_crest_a;   /* sqrt(2) * i_pk_rms: the current-circle limit */
    float i_oc_trip_a; /* FW-05: 1.25 * sqrt(2) * i_pk_rms, instantaneous, both polarities */

    /* ---- §2 switching and the S6 current-loop ceiling ---- */
    uint8_t n_fsw;
    uint32_t fsw_hz[2];
    float fc_ceiling_hz[2]; /* per f_sw (round 7 RR08) */
    uint32_t dead_time_ns;
    float s6_delay_tsw; /* sample-to-actuation delay in T_sw (0.75 = double update, round 12 R2-F24) */

    /* ---- §3 FW-03 power envelope ---- */
    float p_peak_w;
    float p_cont_w;
    float fw03_mod_reserve; /* 0.95 */
    float fw03_pf;          /* 0.85 */
    float peak_time_s;      /* the 30 s peak */

    /* ---- FW-01 identity resistor ---- */
    float hwid_r_ohm;
    float hwid_pullup_ohm;
    float hwid_ratio_nom; /* V_ID / VREF5 */
    float hwid_window_frac;
    float hwid_open_v;
    float hwid_short_v;

    /* ---- FW-02 / FW-17 / FW-18 discharge ---- */
    float r_active_ohm;
    float r_bleed_ohm;
    float tau_dis_s;
    float tau_band_frac; /* FW-02: > 20 % off => DTC */
    float qdis_2tau_s;   /* FW-16 top-up duration */
    float passive_60v_s; /* passive bleeder worst case to 60 V */
    uint32_t qdis_on_max_ms;
    uint32_t qdis_window_ms;
    uint8_t qdis_max_per_window;
    uint32_t qdis_nodecay_ms;
    float hv_safe_v;

    /* ---- FW-05 phase current chain (HC5FW + unity buffers) ---- */
    float isns_valid_min_v;
    float isns_valid_max_v;
    float isns_zero_v;       /* nominal zero-current output (cal default) */
    float isns_sens_v_per_a; /* nominal sensitivity (cal default) */

    /* ---- FW-06 over-voltage path budget (HIL-measured) ---- */
    float fw06_budget_us;    /* link crossing -> ASC_REQ edge */
    float fw06_analog_us;    /* divider lag + AMC1311B + receiver allocation */
    float fw06_sample_hz;    /* free-running V_DC sampling per channel */
    float fw06_conv_us;
    float fw06_action_us;
    float asc_entry_max_us;  /* hardware ASC entry, break-before-make */

    /* ---- FW-07 V_DC chain ---- */
    float vdc_div_ratio; /* V_link per V at the AMC1311 input */
    float vofs_nom_v;
    float vofs_min_v;
    float vofs_max_v;
    float v5gd_min_v;
    float v5gd_max_v;
    float v5gd_sns_ratio; /* V5GD_SNS = V5GD * ratio (47k/47k) */
    float vdc_disagree_frac;
    float vdc_bms_frac;
    float vdc_failsafe_v; /* channel pin below this = AMC1311 fail-safe */

    /* ---- FW-09 HVIL ---- */
    float hvil_closed_hi_v;
    float hvil_closed_lo_v;
    float hvil_open_v;
    uint32_t hvil_reaction_ms;

    /* ---- FW-11 CAN ---- */
    uint32_t can_stale_ms;

    /* ---- FW-12 FS26 ---- */
    uint8_t fs26_wd_err_limit;
    uint8_t fs26_wdw_period_ms;
    uint8_t fs26_fs1b_tdelay_ms;
    uint8_t fs26_fs1b_tdur_ms;
    bool fs26_backup_fs0b;
    bool fs26_backup_fs1b;

    /* ---- LV supervision (round 17): VSUP through the FS26 AMUX ---- */
    float vsup_amux_ratio;
    float vsup_valid_min_v;
    float vsup_ov_v;      /* the FS26 VSUP_OV threshold: VSUPOV */
    float vsup_ov_hyst_v;

    /* ---- FW-13 temperature chains ---- */
    float ntc_b_k;           /* module NTC B25/50 */
    float ntc_r25_ohm;
    float ntc_pullup_ohm;    /* card RSN<ph>P */
    float ntc_series_ohm;    /* power board R<ph>TS */
    float bntc_b_k;          /* board NTC (RTAMB/RTHS) */
    float bntc_r25_ohm;
    float bntc_pullup_ohm;
    float mt_pullup_ohm;     /* motor sensor pull-up (RMT<k>P) */

    /* ---- FW-15 / §7 ---- */
    uint32_t fw15_low_us;
    uint32_t desat_retry_min_ms;

    /* ---- FW-16 ---- */
    float fw16_both_below_v;
    float fw16_topup_below_v;
    float fw16_energy_max_j;
    float fw16_ss_ell_v; /* standstill: E_LL,pk(n_ss) <= this */
    uint32_t fw16_asc_rb_us;
    uint32_t fw16_drven_rb_us;

    /* ---- FW-19 ---- */
    float precharge_sig_frac; /* the ~5 % plateau signature of a shorted QDIS */

    /* ---- FW-10 excitation planes (round 16, A14-N01): SWG -> amplifier -> RSX -> monitor -> PTC -> winding ---- */
    float rslv_floor_vpp;     /* resolver minimum excitation AT THE WINDING (gate 25) */
    float exc_gain;           /* SWG V pp -> amplifier differential V pp (MFB |H(10 kHz)| x 2) */
    float exc_amp_per_mon;    /* amplifier / monitor plane (RSX upstream of the tap; screening primary) */
    float swg_maxapp_min_vpp; /* SWG maximum amplitude at its low corner (DS Table 40 MAXAPP min) */
    float exc_slew_max_vpp;   /* amplifier differential ceiling: ALM2402 slew at -40 degC */

    /* ---- CAL (defaults + ranges: include/cal_ranges.h) ---- */
    float cal_vdc_disagree_floor_v;
    uint32_t cal_vdc_stale_us;
    uint32_t cal_vdc_bms_debounce_ms;
    float cal_isum_tol_a;
    uint8_t cal_isum_debounce;
    float cal_isns_offset_tol_v;
    float cal_dtcomp_band_a;
    uint32_t cal_isns_stale_us;
    float cal_ntc_open_v;
    float cal_ntc_short_v;
    float cal_temp_rate_c_s;
    float cal_tmod_derate_start_c;
    float cal_tmod_derate_end_c;
    float cal_derate_hyst_c;
    float cal_coolant_derate_start_c;
    float cal_coolant_derate_end_c;
    float cal_peak_recovery_s;
    float cal_ign_on_v;
    float cal_ign_off_v;
    uint32_t cal_ign_debounce_ms;
    float cal_hvil_tol_v;
    uint32_t cal_hvil_period_ms;
    uint8_t cal_hvil_debounce;
    uint8_t cal_can_ctr_max_jump;
    uint32_t cal_bms_timeout_ms;
    float cal_torque_ramp_nm_s;
    float cal_dir_change_rpm;
    float cal_rslv_amp_min;
    float cal_rslv_amp_max;
    float cal_rslv_exc_min;
    float cal_rslv_exc_max;
    float cal_rslv_track_err_rad;
    uint8_t cal_rslv_debounce;
    float cal_rslv_phase_comp_deg;
    float cal_rslv_bw_hz;
    float cal_rslv_rate_tol_frac;
    float cal_rslv_rate_tol_rad_s;
    float cal_rslv_rate_min_rad_s;
    float cal_rslv_accel_max_rad_s2;
    float cal_rslv_latency_us;
    uint32_t cal_speed_hold_ms;
    uint32_t cal_rdy_timeout_ms;
    float cal_spo_release_a;
    float cal_precharge_low_frac;
    float cal_precharge_tau_min_s;
    uint32_t cal_precharge_plateau_ms;
    uint32_t cal_precharge_timeout_ms;
    float cal_qdis_decay_min_frac;
    float cal_qdis_stuck_on_frac;
    float cal_fc_fraction;
    float cal_mod_index_max;
    float cal_dcl_kp_nm_v;
    float cal_dcl_ki_nm_vs;
    float cal_dcl_tmax_nm;
    uint32_t cal_sensor_selftest_ms;
    uint32_t cal_fs0b_release_ms;
    uint16_t cal_fs26_prog_id; /* expected M_PROGID of the procured OTP variant; 0xFFFF = unbound */
    float cal_desat_retry_torque_frac;
    uint32_t cal_fw16_rdy_drop_ms;
    uint32_t cal_fw16_rdy_rise_ms;
    uint32_t cal_oneshot_wait_us;
    float cal_torque_max_nm;
    uint32_t cal_desat_en_hold_us; /* A12-R05: MCU_GATE_EN held this long after a FLT is first seen */
    uint32_t cal_asc_release_ns;   /* FW-06a step 3 (round 17): ASC pins' release after ASC_CLR, + margin */
    uint32_t cal_vsup_ld_ms;       /* LV supervision (round 17): tolerated time above the jump-start ceiling */
    uint32_t cal_vsup_jump_ms;     /* ... tolerated VSUPOV event at or below it (24 V jump start) */
    float cal_vsup_jump_max_v;     /* ... the jump-start band ceiling */
    float cal_vdyn_reserve_frac;   /* F23: dynamic voltage reserve of the torque->current witness */
    float cal_isns_act_min_a;      /* F24: latent stuck-channel detector */
    float cal_isns_act_frac;
    uint8_t cal_isns_act_debounce;
    uint32_t cal_rslv_hold_us;     /* A14-R01: age limit of the newest coherent resolver frame */
    float cal_rslv_exc_target_vpp; /* A14-N01: SWG trim setpoint at the MONITOR plane (V pp) */
    float cal_rslv_wind_per_mon;   /* A14-N01: monitor -> winding allowance (PTCs cold) */
    uint8_t cal_swg_code_init;     /* A14-N01: SWG code at start; the trim ramps up from it */
    uint32_t cal_sd_irq_lat_max_us; /* A16-R02: servicing deadline of a resolver block's first SDADC completion */
} ti_params_t;

/* Range metadata for cal_* fields (generated into cal_ranges.h). */
typedef enum { TI_CAL_F32 = 0, TI_CAL_U32, TI_CAL_U16, TI_CAL_U8 } ti_cal_type_t;

typedef struct {
    const char *name;
    size_t offset;
    ti_cal_type_t type;
    float min;
    float max;
} ti_cal_range_t;

/* The four generated sets (src/nvm/params.c). */
const ti_params_t *ti_params_get(ti_sku_t sku);

/* The set this image was built for (TI_SKU_BUILD; host tests may select any). */
const ti_params_t *ti_params_active(void);
void ti_params_select(ti_sku_t sku);

/* Physical sanity + every cal_* field inside its range. Returns the number of violations. */
uint32_t ti_params_validate(const ti_params_t *p);

#endif /* TI_PARAMS_H */
