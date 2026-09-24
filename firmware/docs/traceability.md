# Traceability: FW-xx → code → test

Requirements from `docs/firmware-contract.md` (rev A.12) and the round-14 review of commit cfd35a7.
Paths are relative to `firmware/`.
Tests are `tests/test_<file>.c:<test name>`, and `make test` runs all of them.

The last column says what the host run proves and what only the target can prove. **Host** means the logic is exercised end to end against the simulated card (`src/platform/host`). **Target** means a hardware or HIL measurement is still needed.

## Requirement matrix

| FW | Requirement (short) | Code (file: function) | Host tests | Target / HIL still needed |
|---|---|---|---|---|
| FW-01 | Read HW_ID, classify the SKU resistor, reject open/short/unstable | `sense/hwid.c: hwid_classify, hwid_classify_stable`; `app/app.c: init_identity` | `hwid: each_sku_resistor, open_short_and_between_windows, unstable_reading_rejected`; `params: fw01_hwid_nominal_voltages`; `scenarios: hwid_wrong_open_short_never_arm` | Divider tolerance on real cards |
| FW-02 | Parameter set carries its SKU; mismatch refuses MCU_GATE_EN; discharge τ plausibility | `sense/hwid.c: hwid_identity_ok`; `app/app.c: init_identity` (forbid); `discharge/discharge.c: witness` (τ) | `hwid: fw02_identity_binding`; `discharge: wrong_bank_tau_is_a_dtc, normal_discharge_auto_release_and_tau_ok`; `scenarios: hwid_wrong_open_short_never_arm` | Measured τ per bank |
| FW-03 | P_max(V_DC) envelope; (F23) every current reference voltage-feasible, else zero torque + speed-limit request | `control/torque.c: torque_p_max_w, torque_limits, torque_to_current` (witness), `torque_v_required, torque_v_available`; `app/app.c: torque_path` | `torque: fw03_envelope_matches_contract_table, witness_motor_map_sweep_never_returns_an_infeasible_pair, field_weakening_holds_voltage_ellipse_and_demag_clamp`; `scenarios: infeasible_current_gives_zero_torque_speed_limit_and_dtc` | Dyno; `cal_vdyn_reserve_frac` |
| FW-04 | Thermal derating, 30 s peak, recovery | `control/torque.c: torque_derate_update`; `sense/temp.c: temp_module_max` | `torque: derate_hysteresis, peak_budget_30s_and_full_recovery, coolant_above_assumption_removes_peak`; `state_machine: derate_hysteresis_states` | Thermal model vs coldplate; peak recovery CAL |
| FW-05 | Phase overcurrent, hardware compare at 1.25·√2·I_pk; validity window, Σi; (F24) latent stuck channel | `sense/current.c: isns_oc_codes, isns_oc, isns_activity`; `app/app.c: set_watchdogs, app_isr_fault, stuck_check, detect`; `platform/s32k396/s32k396_adc.c: hal_adc_set_watchdog`; `s32k396_pwm.c` (FAULT1 → high sides) | `current: overcurrent_at_the_crest_8xx_both_polarities, overcurrent_4xx_threshold, activity_catches_a_stuck_channel_where_current_is_asked`; `params: fw05_trip_is_instantaneous_1p25_sqrt2_ipk`; `scenarios: overcurrent_crest_does_not_trip_620_does, all_three_current_channels_stuck_detected_under_command, one_current_channel_stuck_below_the_kcl_tolerance_detected`; `platform_cfg: fault_lock_image, adc_indices_and_thresholds` | ADC watchdog → TRGMUX/LCU → FAULT1 route and its latency; activity CALs |
| FW-06 | DC overvoltage: both channels in hardware compare; ADC watchdog → PWM-ASC request within 15.6 µs | `sense/vdc.c: vdc_ov_code`; `app/app.c: set_watchdogs, app_isr_fault`; `safety/bridge.c: br_enter_pwm_asc` | `vdc: ov_compare_code`; `scenarios: fw06_ov_to_asc_request_within_15p6us, flt_ls_at_speed_is_spo_only_even_on_overvoltage`; `bridge: pwm_asc_entry_is_break_before_make`; `safe_state: row_overvoltage_and_overcurrent` | HIL timing of the whole chain (host models the analog, sampling and ISR delays) |
| FW-06a | ASC exit only by MCU command, ASC_CLR first, first HS pulse ≥ 1 µs later | `safety/bridge.c: br_exit_asc, br_modulate`; `app/app.c: apply_decision` | `bridge: asc_exit_only_when_allowed_and_hs_after_1us`; `scenarios: asc_exit_only_below_n_x_by_mcu_command, mcu_reset_at_speed_keeps_asc_then_exits_with_battery` | Scope capture of the exit edge |
| FW-07 | V_DC plausibility: 5 % disagreement, VOFS window, V5GD window, fail-safe < 0.25 V | `sense/vdc.c: vdc_update, vdc_bms_check` | `vdc:` all 8 tests; `scenarios: vdc_disagreement_spo_and_unknown_hv, v5gd_out_of_window_forces_spo_and_unknown_hv`; `safe_state: row_v5gd_and_vdc_invalid` | Receiver offsets and gains at EOL |
| FW-08 | Regeneration with the battery lost: DC-link voltage control | `control/dclink.c: dcl_step`; `safety/safe_state.c: ss_decide` (BATTERY_LOST, BMS_LIMIT_ZERO rows); `app/app.c: torque_path` | `dclink: sign_and_bounds`; `safe_state: row_battery_lost_never_released_by_rule_b, row_bms_limit_zero_with_battery_is_never_asc`; `scenarios: bms_limit_zero_connected_is_not_asc_but_contactor_open_is` | DC-link controller gains on the real bank |
| FW-08b | "Keep HV connected" while an SPO relies on the battery (rule (a) fails, battery present) at any speed, until rule (a) holds or ASC; battery absent ⇒ "no safe state proven" (A12-R08) | `safety/safe_state.c: ss_decide` (keep_hv, energy_dtc); `safety/fault_mgr.c: combine, fm_update` (keep_hv, no_safe_state); `app/app.c: status_tx`; `comms/can_cmd.c: can_status_encode` (b1.5, b14.0) | `safe_state: keep_hv_follows_the_battery_as_the_sink_at_every_speed, row_flt_hs, row_flt_ls_is_spo_only`; `fault_mgr: keep_hv_until_asc_or_low_speed, keep_hv_held_until_rule_a_and_no_safe_state_without_battery`; `scenarios: standstill_desat_at_rated_current_reports_keep_hv_on_can, flt_hs_at_speed_spo_then_reset_then_pwm_asc`; `can_cmd: status_frame_round14_fields` | Vehicle integration |
| FW-09 | HVIL ladder signature; open ⇒ ramp to zero ≤ 100 ms | `sense/hvil.c: hvil_classify, hvil_step`; `app/app.c: detect` | `hvil: signatures, open_detected_within_100ms, short_to_ground_detected`; `scenarios: hvil_open_ramps_torque_within_100ms` | Harness signatures |
| FW-10 | Resolver: amplitude window, tracking error, rate vs model, excitation monitor, −24° compensation | `control/resolver.c: rslv_update` (observer), `rslv_rate_check, rslv_theta_e_at, rslv_swg_trim`; `app/app.c: sense_fast, sense_slow` | `resolver:` all 10 tests; `scenarios: resolver_amplitude_low_asc_at_speed_spo_below, mcu_reset_at_speed_keeps_asc_then_exits_with_battery, swg_trim_is_written_to_the_generator` | SDADC/SWG configuration, `cal_rslv_latency_us`, EOL phase trim |
| FW-11 | CAN command: E2E CRC + alive counter, ≤ 20 ms stale ⇒ ramp to zero, BMS timeout ⇒ zero regen; (A12-R06) the same across the 32-bit µs wrap | `comms/can_cmd.c: can_cmd_rx, can_e2e_crc, can_cmd_fresh, can_bms_fresh, can_dir_interlock`; `control/torque.c: torque_limits, torque_clamp`; `safety/state_machine.c: st_run`; `hal/timer.h: hal_time_us64, hal_time_ms, ti_time64_extend`; `app/app.c: app_task_1ms` | `can_cmd: valid_frame_decoded, crc_bad_rejected, frozen_counter_goes_stale_after_20ms, counter_jump_rejected_then_resynced, bms_has_its_own_timeout, direction_interlock`; `torque: bms_limits_and_timeout_zero_regen, nan_torque_command_zeroed`; `time: can_freshness_across_the_microsecond_wrap`; `scenarios: stale_can_ramps_to_zero_not_held, frozen_alive_counter_is_stale, nan_reference_never_reaches_pwm, running_across_the_microsecond_wrap_keeps_fresh_frames_fresh`; `state_machine: stale_command_ramps_then_zero_torque` | Vehicle DBC / DataID agreement |
| FW-12 | FS26: OTP/INIT readback at boot, Q&A watchdog (ERR_LIMIT 2, ≤ 3 ms window) before FS0B release, FS1B policy, GPIO1, LPOFF | `safety/fs26.c: fs26_init, fs26_wd_refresh, fs26_release_safety_outputs, fs26_request_fs0b, fs26_set_gpio1, fs26_goto_lpoff` | `fs26:` all 12 tests; `scenarios: watchdog_missed_drops_drv_en` | Real FS26 OTP image, watchdog answer width, FS0B release after RSTB, SPI timing |
| FW-13 | Temperatures: module NTC open/short/rate, motor sensors, board NTCs | `sense/temp.c: temp_ntc_c, temp_pt1000_c, temp_update` | `temp:` all 4 tests | Sensor curves of the chosen parts |
| FW-14 | Gate power: RDY low ⇒ no PWM; flyback before DRV_EN | `safety/gate_power.c: gp_request_on, gp_step, gp_rdy_both` | `gate_power: refused_while_fs1b_asserted, start_ready_loss_and_recovery, start_timeout` | RDY rise/fall times (CAL) |
| FW-15 | FLT_HS/FLT_LS: hardware PWM inhibit (FAULT0/2, fail-safe, manual clear, locked); bank to retained RAM + queued NVM; ≥ 1.5 ms reset, ASC_CLR, one-shot clear, confirm before FFLAG clear; one VCU-authorised retry; (A12-R05) no software MCU_GATE_EN drop within `cal_desat_en_hold_us` of a FLT; (F01/F02/F06) route bound, image read back, REG_PROT locked and EOL/HIL-validated before any arming | `app/app.c: app_isr_fault, fault_actions, init_evidence, evidence_watch`; `safety/fault_mgr.c: fm_desat, fm_retry_allowed, fm_retry_consumed, fm_retained_commit, fm_boot`; `safety/bridge.c: br_spo, br_service, br_rec_start, br_rec_step` (en_low / observe); `safety/arm_evidence.c`; `platform/s32k396/s32k396_pwm.c: hal_pwm_init` (lock), `hal_pwm_config_matches, hal_pwm_protection_locked, hal_pwm_fault_route_bound, hal_pwm_fault_clear`; `s32k396_board_cfg.h` | `fault_mgr: fw15_retained_first_nvm_only_queued, one_authorised_retry_after_1s_then_latch, boot_blocks_on_recent_desat_record, uncommitted_retained_record_requeued_at_boot, latched_reset_only_below_n_x_and_never_for_desat`; `bridge: fw15_recovery_waits_1p5ms_and_clears, fw15_recovery_fails_safe_on_a_hard_short, desat_hold_keeps_en_until_the_hold_then_drops_it, desat_hold_covers_br_rec_start_and_recovery_still_works, non_desat_spo_drops_en_at_once`; `safe_state: row_flt_hs, row_flt_ls_is_spo_only`; `scenarios: flt_hs_at_speed_spo_then_reset_then_pwm_asc, flt_ls_at_speed_is_spo_only_even_on_overvoltage, desat_one_authorised_retry_then_latch, desat_at_speed_holds_en_through_the_hold_then_reset_and_pwm_asc, desat_at_low_speed_spo_waits_for_the_hold, desat_retry_waits_1s_across_the_microsecond_wrap, arming_refused_without_evidence_and_the_status_names_it, unbound_fault_route_never_arms, arming_permitted_with_a_valid_record, arming_refused_on_record_identity_or_crc_mismatch, watchdog_reset_relocks_before_rearming, evidence_lost_while_armed_goes_through_section6`; `state_machine: desat_retry_runs_at_reduced_torque`; `platform_cfg: fault_lock_image, regprot_lock_decision_needs_every_bit_read_back, fault_route_unbound_by_default, protected_registers_reject_writes_from_cpu_and_dma`; `calib: validation_record_binds_image_card_and_crc`; `params: round14_cal_defaults_and_ranges` | IMCR values (`TODO(RM)`); REG_PROT offsets (`TODO(RM)`) or XRDC; the EOL/HIL rig writing the record; one-shot timing; the hold vs the measured soft turn-off |
| FW-16 | Boot self-test steps a–h through DRV_EN_RB / ASC_CMD_RB; energy precondition ≤ 0.1 J; stored pass; EN drops through the bridge (DESAT hold) | `safety/gate_selftest.c: st_conditions, st_energy, st_step` (EN drops via `br_spo`); `app/app.c: selftest_step` (run or stored pass) | `gate_selftest: healthy_chain_passes_all_steps, each_term_stuck_permissive_is_detected, energy_precondition, topup_then_run_and_not_counted, skipped_rather_than_run_on_assumption, failed_test_with_a_desat_drops_en_only_after_the_hold`; `state_machine: selftest_and_precharge_refusals`; `scenarios: boot_across_the_microsecond_wrap_reaches_armed` | Chain timings (CAL), pad-rule behaviour on silicon |
| FW-17 | QDIS only with contactors reported open, auto-release 5 s, ≤ 3 per 5 min | `discharge/discharge.c: dis_request, dis_step` | `discharge: fires_only_with_contactors_reported_open, auto_release_after_5s, three_per_five_minutes_and_uncounted_topup`; `state_machine: discharge_only_with_contactors_open`; `scenarios: key_off_discharge_witnessed_then_lpoff` | Discharge resistor thermal |
| FW-18 | Witness on both channels; stuck-off in 200 ms; invalid witness ⇒ HV UNKNOWN; stuck-on / unexpected discharge ⇒ latched service lock, "do not re-energise" + "open the contactors" (round 14) | `discharge/discharge.c: witness, stuck_on_watch` (not while modulating), `dis_hv_state`; `app/app.c: service_lock, init_service_lock`; `nvm/nvlog.h: nv_service_t` | `discharge: stuck_off_detected_in_200ms, invalid_witness_means_unknown_never_safe, stuck_on_seen_as_fast_decay_with_qdis_off, unexpected_discharge_judged_only_with_nothing_drawing_on_the_link`; `scenarios: qdis_stuck_off_detected, stuck_on_qdis_latches_service_required_and_never_rearms` | Service routine to clear the lock |
| FW-19 | Precharge plausibility: plateau ≈ 5 % low or τ too short refuses arming | `discharge/discharge.c: pch_step` | `discharge: fw19_precharge_plateau_and_tau`; `scenarios: precharge_plateau_5pct_low_refuses` | Vehicle precharge τ (CAL) |
| FW-20 | Versioned, CRC- and range-checked calibration tied to serial, SKU and motor ID | `nvm/calib.c: calib_check, calib_seal`; `nvm/nvlog.c`; `app/app.c: init_calibration` | `calib: nominal_needs_a_motor, each_failure_detected`; `nvlog:` all 4 tests; `scenarios: brownout_during_nvm_write_never_blocks` | Fee configuration, device UID read |
| FW-21 | Signed images, rollback, no-torque update state | **not implemented**: this needs a bootloader and the HSE. The application image is not the place for it. | none | All of it |

### Other contract items

| Item | Code | Tests |
|---|---|---|
| §6 safe-state matrix: speed columns, rule (a) winding-energy screen, rule (b) release, row ranking | `safety/safe_state.c: ss_n_x_rpm, ss_rule_a_vpk, ss_decide, ss_rank`; `safety/fault_mgr.c: fm_raise, fm_update` | `safe_state:` all 12 tests; `fault_mgr: forced_spo_wins_and_blocks_asc, highest_rank_wins` |
| §9 start-up order | `safety/state_machine.c: sm_step`; `app/app.c: arming, execute` | `state_machine: boot_order_follows_section_9, init_and_sensor_failures_never_arm`; `scenarios: boot_to_run_follows_section_9` |
| §2 per-SKU gain sets under the ceilings | `control/gains.c: gains_ceiling_hz, gains_compute, gains_default` | `gains: sic_10k_8k_and_igbt_5k, ceiling_and_fsw_enforced` |
| FOC: Clarke/Park, PI with anti-windup and decoupling, SVPWM, dead-time compensation, NaN/Inf guards | `control/foc.c` | `foc:` all 7 tests |
| MTPA (LUT and closed form), field weakening (voltage ellipse), demagnetisation clamp, current circle | `control/torque.c: torque_mtpa_id, torque_to_current` | `torque: mtpa_spm_and_ipm, mtpa_lut_interpolates, field_weakening_holds_voltage_ellipse_and_demag_clamp, current_circle` |
| IGN (KL15) | `sense/ign.c` | `ign: hysteresis_and_debounce`; `state_machine: key_off_powerdown_to_lpoff` |
| UDS DTC store | `comms/dtc.c` | `dtc: status_bits_and_occurrences` |
| Ball-map binding, S32K396 register images, REG_PROT layout | `platform/s32k396/board_pins.h` (generated), `s32k396_cfg.h` | `board_map:` 5 tests; `platform_cfg:` 8 tests |
| One time domain (A12-R06): 64-bit µs, ms = us64/1000 | `hal/timer.h`; `platform/host/sim_hal.c, platform/s32k396/s32k396_io.c: hal_time_us64`; `app/app.c`; `nvm/nvlog.c`; `comms/dtc.c: dtc_set, dtc_times` | `time:` 4 tests; `scenarios: running_across_the_microsecond_wrap_keeps_fresh_frames_fresh, boot_across_the_microsecond_wrap_reaches_armed, desat_retry_waits_1s_across_the_microsecond_wrap, dtc_time_stamps_across_the_microsecond_wrap_in_the_application` |
| INV_STATUS round-14 fields (b14 flags, b15 missing evidence) | `comms/can_cmd.c: can_status_encode`; `app/app.c: status_tx` | `can_cmd: status_frame_round14_fields`; scenarios above |

## Edge cases from the task, and where each is tested

| Edge case | Test(s) |
|---|---|
| Stale, frozen and CRC-bad CAN frames | `can_cmd: crc_bad_rejected, frozen_counter_goes_stale_after_20ms, counter_jump_rejected_then_resynced`; `scenarios: stale_can_ramps_to_zero_not_held, frozen_alive_counter_is_stale` |
| BMS limit 0 with the contactors closed (no ASC) vs a contactor open | `safe_state: row_bms_limit_zero_with_battery_is_never_asc, row_battery_lost_never_released_by_rule_b`; `scenarios: bms_limit_zero_connected_is_not_asc_but_contactor_open_is` |
| §6 rows at n < n_x and n ≥ n_x, energy rule from the screening motor (0.35 mH, 25 mΩ): SPO refused or allowed | `safe_state: n_x_of_the_screening_motor, rule_a_winding_energy_at_standstill, row_cmd_lost, row_resolver_invalid_with_energy_rule, row_flt_hs, row_v5gd_and_vdc_invalid, unknown_speed_takes_high_column` |
| FW-06 OV → ASC request timing budget | `scenarios: fw06_ov_to_asc_request_within_15p6us` |
| FLT_HS vs FLT_LS (no LS-ASC after FLT_LS) | `safe_state: row_flt_hs, row_flt_ls_is_spo_only`; `scenarios: flt_hs_at_speed_spo_then_reset_then_pwm_asc, flt_ls_at_speed_is_spo_only_even_on_overvoltage` |
| One VCU-authorised DESAT retry, then latch | `fault_mgr: one_authorised_retry_after_1s_then_latch`; `scenarios: desat_one_authorised_retry_then_latch` |
| FW-16: each term stuck permissive is detected; the energy precondition (≤ 0.1 J) refuses the test | `gate_selftest: each_term_stuck_permissive_is_detected, energy_precondition, skipped_rather_than_run_on_assumption` |
| Open Hall wire (0 V) ⇒ invalid channel | `current: open_hall_wire_reads_0v_and_invalidates`; `scenarios: open_hall_wire_invalidates_and_stops_modulation` |
| Σi violation | `current: sum_plausibility_debounced_then_latched` |
| Overcurrent at the crest: 481 A does not trip at 601 A, 620 A does | `current: overcurrent_at_the_crest_8xx_both_polarities`; `scenarios: overcurrent_crest_does_not_trip_620_does` |
| VDC disagreement, VOFS out of window, V5GD out of window (both invalid ⇒ SPO decision, HV unknown) | `vdc: disagreement_over_5_percent_invalidates_both, vofs_out_of_window_invalidates_both, v5gd_out_of_window_invalidates_both`; `scenarios: vdc_disagreement_spo_and_unknown_hv, v5gd_out_of_window_forces_spo_and_unknown_hv` |
| Discharge stuck-off and stuck-on | `discharge: stuck_off_detected_in_200ms, stuck_on_seen_as_fast_decay_with_qdis_off`; `scenarios: qdis_stuck_off_detected` |
| Precharge plateau 5 % low | `discharge: fw19_precharge_plateau_and_tau`; `scenarios: precharge_plateau_5pct_low_refuses` |
| HW_ID open, short, wrong set | `hwid: open_short_and_between_windows, fw02_identity_binding`; `scenarios: hwid_wrong_open_short_never_arm` |
| NaN torque command | `torque: nan_torque_command_zeroed`; `foc: nonfinite_guard_blocks_pwm_write`; `scenarios: nan_reference_never_reaches_pwm` |
| Angle wrap | `resolver: tracks_constant_speed_and_wraps` |
| Resolver amplitude window low | `resolver: amplitude_window_low_invalidates`; `scenarios: resolver_amplitude_low_asc_at_speed_spo_below` |
| Watchdog missed ⇒ FS0B | `fs26: watchdog_missed_asserts_fs0b_within_two_windows`; `scenarios: watchdog_missed_drops_drv_en` |
| Brown-out during an NVM write (queued, never blocks) | `nvlog: brownout_tears_write_previous_record_survives, queue_never_blocks_and_counts_overflow`; `scenarios: brownout_during_nvm_write_never_blocks` |
| Derate hysteresis | `torque: derate_hysteresis`; `state_machine: derate_hysteresis_states` |
| ASC exit only at MCU command below n_x | `bridge: asc_exit_only_when_allowed_and_hs_after_1us`; `scenarios: asc_exit_only_below_n_x_by_mcu_command, mcu_reset_at_speed_keeps_asc_then_exits_with_battery` |
| DESAT: ISR at t = 0, hold − 1, hold + 1; §6 decision and br_rec_start inside the hold | `bridge: desat_hold_keeps_en_until_the_hold_then_drops_it, desat_hold_covers_br_rec_start_and_recovery_still_works`; `scenarios: desat_at_speed_holds_en_through_the_hold_then_reset_and_pwm_asc, desat_at_low_speed_spo_waits_for_the_hold` |
| Time stamps straddling the µs wrap; repeated wraps | `time:` all 4; `scenarios:` the four wrap scenarios |
| Low-speed, high-current SPO relying on rule (b); battery then absent | `safe_state: keep_hv_follows_the_battery_as_the_sink_at_every_speed`; `fault_mgr: keep_hv_held_until_rule_a_and_no_safe_state_without_battery`; `scenarios: standstill_desat_at_rated_current_reports_keep_hv_on_can` |
| Forbidden writes to the lock-down registers (CPU, DMA), after a watchdog reset | `platform_cfg: protected_registers_reject_writes_from_cpu_and_dma`; `scenarios: watchdog_reset_relocks_before_rearming` |
| No evidence / foreign or corrupt validation record | `scenarios: arming_refused_without_evidence_and_the_status_names_it, unbound_fault_route_never_arms, arming_refused_on_record_identity_or_crc_mismatch` |
| Infeasible current (low bus, high speed, hot magnets, restrictive demag limit) | `torque: witness_motor_map_sweep_never_returns_an_infeasible_pair`; `scenarios: infeasible_current_gives_zero_torque_speed_limit_and_dtc` |
| Current channel stuck at zero (below the KCL tolerance; all three) | `current: activity_catches_a_stuck_channel_where_current_is_asked`; `scenarios: all_three_current_channels_stuck_detected_under_command, one_current_channel_stuck_below_the_kcl_tolerance_detected` |
| Shorted QDIS seen at the next contactor opening | `discharge: unexpected_discharge_judged_only_with_nothing_drawing_on_the_link`; `scenarios: stuck_on_qdis_latches_service_required_and_never_rearms` |

## Phase-current diagnostic coverage per operating state (F24)

Three sensors, no fourth. "KCL" = Σi plausibility (|Σ| > `cal_isum_tol_a` 45 A, 3 samples); "window" =
0.2–4.8 V per channel; "activity" = `isns_activity` (a phase whose reference reaches 20 A must read
≥ 20 % of it, 20 applicable samples in a row); "age" = `cal_isns_stale_us`.

| Operating state | One channel stuck at its zero level | All three stuck (shared reference) | Open wire / unpowered (0 V), railed | Single-channel gain error | Equal gain error on all three | Frozen converter |
|---|---|---|---|---|---|---|
| Before arming, standstill (§9 step 3) | not observable (reads the true 0 A) | not observable | window, every sample | offset check only (`cal_isns_offset_tol_v`) | not observable | age |
| Armed, not modulating (zero current) | not observable | not observable | window | not observable | not observable | age |
| Modulating, every phase reference < 20 A | KCL only if the true phase current exceeds 45 A (it cannot here): latent | latent | window | latent | not observable | age |
| Modulating, a phase reference ≥ 20 A | activity (≈ 1–2 ms) and KCL above 45 A | activity | window | KCL when the error exceeds 45 A (e.g. 10 % at 450 A); activity only for a gain below 20 % | **not observable**: the loop regulates the measured value to the reference and the sum stays 0 | age |
| PWM-ASC or SPO with current decaying | KCL above 45 A; activity does not run (not modulating) | **not observable**: §6 would see 0 A; a fault already latched by activity makes the current count as unknown (worst case) | window | KCL above 45 A | not observable | age |

What bounds the gaps: the EOL calibration record (FW-20: gain 1.9–2.6 mV/A, offset 2.2–2.8 V, CRC and
serial-bound), the FW-05 compare built from the same calibrated gains (an equal gain error moves the
software and hardware trip together), the NSI6611 DESAT (≈ 2.8× the rated peak) as the independent
short-circuit path, and FW-10's angle-rate check, whose back-EMF model uses the measured currents only
through the R_s·i_q and L_d·i_d terms (weak). An equal gain error on all three channels therefore
remains an EOL/periodic-calibration item, not a run-time diagnostic.

## Round 14: before/after on the pre-fix source

Each new regression test was run against commit cfd35a7 (the pre-fix firmware) and against this
tree. Method: the pre-fix `firmware/` was extracted from git into a scratch directory, the new
`tests/` copied over it, and a compatibility header force-included into the test files only mapped
each new interface onto what the old code did (e.g. `br_service` a no-op, `hal_time_ms()` =
`hal_time_us()/1000`, `hal_pwm_protection_locked()` = the old `hal_pwm_config_locked()`, every
validation record accepted, register writes always landing). The old tree additionally received the
five new CAL rows (data it never reads), a `sim_reset_at_us` test hook and a read-only `dtc_times`
accessor. Two new tests exercise pure new interfaces with no old counterpart and were not compiled
there (`platform_cfg: regprot_lock_decision_needs_every_bit_read_back`,
`can_cmd: status_frame_round14_fields`). Every pre-existing test still passed on the pre-fix source
with the new harness (212 run there, 128 checks failed, all in the tests below).

| Suite: test | Item | Pre-fix (cfd35a7) | This tree |
|---|---|---|---|
| bridge: desat_hold_keeps_en_until_the_hold_then_drops_it | A12-R05 | FAIL: EN low at t = 0 | pass |
| bridge: desat_hold_covers_br_rec_start_and_recovery_still_works | A12-R05 | FAIL: EN low at br_rec_start | pass |
| bridge: non_desat_spo_drops_en_at_once | A12-R05 guard | pass | pass |
| gate_selftest: failed_test_with_a_desat_drops_en_only_after_the_hold | A12-R05 | FAIL: EN dropped inside the hold | pass |
| scenarios: desat_at_speed_holds_en_through_the_hold_then_reset_and_pwm_asc | A12-R05 | FAIL: the ISR dropped EN at t = 0 | pass |
| scenarios: desat_at_low_speed_spo_waits_for_the_hold | A12-R05 | FAIL: the ISR dropped EN at t = 0 | pass |
| params: round14_cal_defaults_and_ranges | A12-R05/F23/F24 CALs | pass (table added to the old tree) | pass |
| time: us64_extension_survives_repeated_wraps | A12-R06 | FAIL: no extension | pass |
| time: sim_clock_across_the_microsecond_wrap | A12-R06 | FAIL: the clock wrapped | pass |
| time: can_freshness_across_the_microsecond_wrap | A12-R06 | FAIL: an 8 ms-old frame stale | pass |
| time: dtc_time_stamps_across_the_microsecond_wrap | A12-R06 | FAIL: 12 ms read as ≈ 4.29e9 ms | pass |
| scenarios: running_across_the_microsecond_wrap_keeps_fresh_frames_fresh | A12-R06 | FAIL: false staleness, CAN timeout DTC, torque ramp | pass |
| scenarios: boot_across_the_microsecond_wrap_reaches_armed | A12-R06 | FAIL: all 5 boots stopped at a dwell timer | pass |
| scenarios: desat_retry_waits_1s_across_the_microsecond_wrap | A12-R06 | FAIL: FW-15 retry ≈ 100 ms after the DESAT | pass |
| scenarios: dtc_time_stamps_across_the_microsecond_wrap_in_the_application | A12-R06 | FAIL | pass |
| safe_state: keep_hv_follows_the_battery_as_the_sink_at_every_speed | A12-R08 | FAIL: 0 rpm, 340 A rms, rule (b): keep_hv 0 | pass |
| safe_state: row_flt_ls_is_spo_only (assertion changed) | A12-R08 | FAIL: keep_hv 1 with no battery | pass |
| fault_mgr: keep_hv_held_until_rule_a_and_no_safe_state_without_battery | A12-R08 | FAIL | pass |
| scenarios: standstill_desat_at_rated_current_reports_keep_hv_on_can | A12-R08 | FAIL: status b1.5 = 0 | pass |
| platform_cfg: fault_route_unbound_by_default | F01 | FAIL: route taken as bound | pass |
| scenarios: unbound_fault_route_never_arms | F01 | FAIL: armed | pass |
| platform_cfg: regprot_lock_decision_needs_every_bit_read_back | F02 | not compiled (new pure helper) | pass |
| platform_cfg: protected_registers_reject_writes_from_cpu_and_dma | F02 | FAIL: writes landed; "locked" after a reset | pass |
| scenarios: watchdog_reset_relocks_before_rearming | F02 | FAIL: "locked" with no lock | pass |
| calib: validation_record_binds_image_card_and_crc | F06 | FAIL: every record accepted | pass |
| scenarios: arming_refused_without_evidence_and_the_status_names_it | F06 | FAIL: armed, FS0B released, FW-16 ran | pass |
| scenarios: arming_permitted_with_a_valid_record | F06 control | pass | pass |
| scenarios: arming_refused_on_record_identity_or_crc_mismatch | F06 | FAIL: armed in all 6 cases | pass |
| scenarios: evidence_lost_while_armed_goes_through_section6 | F06 | FAIL: kept modulating | pass |
| can_cmd: status_frame_round14_fields | status | not compiled (new fields) | pass |
| torque: witness_motor_map_sweep_never_returns_an_infeasible_pair | F23 | FAIL: infeasible pairs returned | pass |
| torque: field_weakening_holds_voltage_ellipse_and_demag_clamp (tightened) | F23 | FAIL: no infeasible status | pass |
| scenarios: infeasible_current_gives_zero_torque_speed_limit_and_dtc | F23 | FAIL: torque kept | pass |
| current: activity_catches_a_stuck_channel_where_current_is_asked | F24 | FAIL: not detected | pass |
| scenarios: all_three_current_channels_stuck_detected_under_command | F24 | FAIL: kept modulating on 0 A readings | pass |
| scenarios: one_current_channel_stuck_below_the_kcl_tolerance_detected | F24 | FAIL: currents stayed valid (the harness later reported a resolver-rate fault instead) | pass |
| discharge: unexpected_discharge_judged_only_with_nothing_drawing_on_the_link | 9 | FAIL: stuck-on verdict while the motor drew the link | pass |
| scenarios: stuck_on_qdis_latches_service_required_and_never_rearms | 9 | FAIL: DTC only, re-armed, no status bits | pass |
