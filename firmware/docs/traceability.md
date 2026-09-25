# Traceability: FW-xx → code → test

Requirements from `docs/firmware-contract.md` (rev A.15), the round-14 review of commit cfd35a7, the
round-15 rechecks of commit a8c75eb, the round-16 rechecks of commit 32214be and the round-17 closure of the
open items (contract §10c; FW-31, FW-32).
Paths are relative to `firmware/`.
Tests are `tests/test_<file>.c:<test name>`, and `make test` runs all of them.

The last column names the rows of the target checklist [`target-bringup.md`](target-bringup.md) that still need the hardware (silicon, RM, HIL, EOL, commissioning); everything else is proven on the host, end to end against the simulated card (`src/platform/host`).

## Requirement matrix

| FW | Requirement (short) | Code (file: function) | Host tests | Target checklist |
|---|---|---|---|---|
| FW-01 | Read HW_ID, classify the SKU resistor, reject open/short/unstable; (A13-R04) from a conversion the slow list was started for | `sense/hwid.c: hwid_classify, hwid_classify_stable`; `app/app.c: init_identity` (slow list started before each sample) | `hwid: each_sku_resistor, open_short_and_between_windows, unstable_reading_rejected`; `params: fw01_hwid_nominal_voltages`; `scenarios: hwid_wrong_open_short_never_arm, hw_id_is_converted_before_it_is_classified` | T-14 (HW_ID on real cards) |
| FW-02 | Parameter set carries its SKU; mismatch refuses MCU_GATE_EN; discharge τ plausibility | `sense/hwid.c: hwid_identity_ok`; `app/app.c: init_identity` (forbid); `discharge/discharge.c: witness` (τ) | `hwid: fw02_identity_binding`; `discharge: wrong_bank_tau_is_a_dtc, normal_discharge_auto_release_and_tau_ok`; `scenarios: hwid_wrong_open_short_never_arm` | T-37 (τ per bank) |
| FW-03 | P_max(V_DC) envelope; (F23) every current reference voltage-feasible, else zero torque + speed-limit request | `control/torque.c: torque_p_max_w, torque_limits, torque_to_current` (witness), `torque_v_required, torque_v_available`; `app/app.c: torque_path` | `torque: fw03_envelope_matches_contract_table, witness_motor_map_sweep_never_returns_an_infeasible_pair, field_weakening_holds_voltage_ellipse_and_demag_clamp`; `scenarios: infeasible_current_gives_zero_torque_speed_limit_and_dtc` | T-37 (dyno, `cal_vdyn_reserve_frac`) |
| FW-04 | Thermal derating, 30 s peak, recovery | `control/torque.c: torque_derate_update`; `sense/temp.c: temp_module_max` | `torque: derate_hysteresis, peak_budget_30s_and_full_recovery, coolant_above_assumption_removes_peak`; `state_machine: derate_hysteresis_states` | T-37 (thermal model, `cal_peak_recovery_s`) |
| FW-05 | Phase overcurrent, hardware compare at 1.25·√2·I_pk; validity window, Σi; (F24) latent stuck channel; (A14-R03) a triplet only when all three channels of one trigger arrived, a lost sample takes the current-sensor path while V_DC and the resolver continue; a stopped current loop is seen by the task | `sense/current.c: isns_oc_codes, isns_oc, isns_activity, isns_lost`; `app/app.c: set_watchdogs, app_isr_fault, stuck_check, detect, sense_fast` (triplet or lost), `app_task_1ms` (current-loop liveness); `hal/adc.h` (all three or nothing); `platform/s32k396/s32k396_adc.c: hal_adc_set_watchdog, hal_adc_read_phase`; `s32k396_pwm.c` (FAULT1 → high sides) | `current: overcurrent_at_the_crest_8xx_both_polarities, overcurrent_4xx_threshold, activity_catches_a_stuck_channel_where_current_is_asked, lost_sample_is_invalid_and_keeps_its_last_stamp`; `params: fw05_trip_is_instantaneous_1p25_sqrt2_ipk`; `scenarios: overcurrent_crest_does_not_trip_620_does, all_three_current_channels_stuck_detected_under_command, one_current_channel_stuck_below_the_kcl_tolerance_detected, lost_phase_current_triplets_take_the_failure_path, lost_triplets_across_the_microsecond_wrap_keep_a_defined_stamp, a_stopped_current_loop_is_caught_by_the_task`; `platform_cfg: fault_lock_image, adc_indices_and_thresholds` | T-08…T-10 (route, thresholds, WTISR), T-12 (BCTU list), T-14 (VALID bit), T-37 (activity CALs) |
| FW-06 | DC overvoltage: both channels in hardware compare; ADC watchdog → PWM-ASC request within 15.6 µs | `sense/vdc.c: vdc_ov_code`; `app/app.c: set_watchdogs, app_isr_fault`; `safety/bridge.c: br_enter_pwm_asc` | `vdc: ov_compare_code`; `scenarios: fw06_ov_to_asc_request_within_15p6us, flt_ls_at_speed_is_spo_only_even_on_overvoltage`; `bridge: pwm_asc_entry_is_break_before_make`; `safe_state: row_overvoltage_and_overcurrent` | T-05 (the whole chain on HIL; the host models the analog, sampling and ISR delays), T-08, T-11 |
| FW-06a | ASC exit only by MCU command, ASC_CLR first; (round 17) the first HS pulse only after the ASC pins' release (`cal_asc_release_ns`, ≥ the verifier's 1.07 µs) plus the dead time, from the clear's falling edge | `safety/bridge.c: br_exit_asc` (`asc_clear_pulse`: the falling-edge stamp), `br_modulate` (the wait); `app/app.c: apply_decision` | `bridge: asc_exit_only_when_allowed_and_hs_after_the_release`; `scenarios: asc_exit_only_below_n_x_by_mcu_command, mcu_reset_at_speed_keeps_asc_then_exits_with_battery, asc_exit_first_high_side_pulse_after_the_release_deadline` | T-36 (the exit edge) |
| FW-07 | V_DC plausibility: 5 % disagreement, VOFS window, V5GD window, fail-safe < 0.25 V | `sense/vdc.c: vdc_update, vdc_bms_check` | `vdc:` all 8 tests; `scenarios: vdc_disagreement_spo_and_unknown_hv, v5gd_out_of_window_forces_spo_and_unknown_hv`; `safe_state: row_v5gd_and_vdc_invalid` | T-07 (EOL gains and offsets) |
| FW-08 | Regeneration with the battery lost: (round 17) zero current, id = iq = 0 at the current-loop rate, reported as 0 Nm — the DC-link trim only in normal RUN with the battery path proven (a regen limiter above `vdc_max_v`); (A13-R02) the battery-path loss detected whenever armed at every speed — OPEN, PRECHARGE, INVALID or stale report, V_DC off the pack — §6 response from speed, winding current, V_DC and the actuators; no torque permission in the invocation that processes it | `control/dclink.c: dcl_trim`; `app/app.c: torque_path` (trim in RUN, reset outside; t_cmd 0 and id = iq = 0 under the row), `control_fast`; `safety/safe_state.c: ss_decide` (BATTERY_LOST, BMS_LIMIT_ZERO rows); `app/app.c: detect` (battery path), `apply_decision` (zero torque, then SPO once the current is gone), `torque_path` (the §6 current-control permission), `gather`; `safety/fault_mgr.c: fm_needs_fault_state`; `safety/state_machine.c: st_run` | `dclink: trim_takes_back_regen_only_above_the_range_maximum`; `safe_state: row_battery_lost_never_released_by_rule_b, row_bms_limit_zero_with_battery_is_never_asc, unknown_speed_takes_high_column`; `fault_mgr: battery_lost_is_a_fault_until_its_response_is_done`; `state_machine: leaving_run_grants_no_torque_in_the_same_invocation, battery_path_row_leaves_run_and_blocks_reentry`; `scenarios: bms_limit_zero_connected_is_not_asc_but_contactor_open_is, low_speed_open_contactor_is_a_battery_path_loss, battery_path_loss_while_armed_at_every_speed, zero_torque_opening_at_standstill_disarms_without_fault, dc_link_trim_limits_regen_with_the_battery_present, battery_path_loss_below_n_x_applies_and_reports_zero_current` | T-37 (trim gains on the real bank), T-38 |
| FW-08b | "Keep HV connected" while an SPO relies on the battery (rule (a) fails, battery present) at any speed, until rule (a) holds or ASC; battery absent ⇒ "no safe state proven" (A12-R08) | `safety/safe_state.c: ss_decide` (keep_hv, energy_dtc); `safety/fault_mgr.c: combine, fm_update` (keep_hv, no_safe_state); `app/app.c: status_tx`; `comms/can_cmd.c: can_status_encode` (b1.5, b14.0) | `safe_state: keep_hv_follows_the_battery_as_the_sink_at_every_speed, row_flt_hs, row_flt_ls_is_spo_only`; `fault_mgr: keep_hv_until_asc_or_low_speed, keep_hv_held_until_rule_a_and_no_safe_state_without_battery`; `scenarios: standstill_desat_at_rated_current_reports_keep_hv_on_can, flt_hs_at_speed_spo_then_reset_then_pwm_asc`; `can_cmd: status_frame_round14_fields` | T-38 (rule (b), the BMS) |
| FW-09 | HVIL ladder signature; open ⇒ ramp to zero ≤ 100 ms | `sense/hvil.c: hvil_classify, hvil_step`; `app/app.c: detect` | `hvil: signatures, open_detected_within_100ms, short_to_ground_detected`; `scenarios: hvil_open_ramps_torque_within_100ms` | T-38 (harness signatures) |
| FW-10 | Resolver: amplitude window, tracking error, rate vs model, excitation monitor, −24° compensation; (A14-R01) validity expires `cal_rslv_hold_us` after the newest coherent frame, then the resolver-invalid row and a from-scratch re-acquisition; (A14-R02) one coherent frame of the three channels per epoch or none; (A14-N01) the setpoint at the monitor plane (7.2 V pp), the 6.5 V pp floor at the winding, the SWG ramp from a low code, the trim headroom at the low corner, a DTC when the trim saturates | `control/resolver.c: rslv_update` (observer, planes), `rslv_age, rslv_rate_check, rslv_theta_e_at, rslv_swg_trim`; `hal/sdadc.h, hal/sdadc_ring.c` (frame protocol); `platform/s32k396/s32k396_resolver.c: s32k_sdadc_dma_irq, hal_sd_dma_slot, hal_sdadc_read_frame`; `app/app.c: sense_fast, sense_slow, detect`; `nvm/params.c: ti_params_validate` (planes); `nvm/calib.c` (monitor gain) | `resolver:` all 13 tests; `sdadc:` all 6 tests; `params: round16_cal_defaults_and_ranges, exciter_planes_and_trim_headroom`; `scenarios: resolver_amplitude_low_asc_at_speed_spo_below, mcu_reset_at_speed_keeps_asc_then_exits_with_battery, swg_trim_is_written_to_the_generator, resolver_frames_stopping_withdraws_the_angle_at_the_hold, temporary_empty_reads_never_fault, a_frozen_resolver_channel_is_never_read_as_fresh, a_low_impedance_resolver_saturates_the_trim_with_a_dtc, ptc_post_trip_is_flagged_at_the_winding_and_a_cool_restart_recovers` | T-28…T-31 (SDADC, eDMA, SWG, the IOAMPL law), T-07 (EOL trim, monitor gain, ratio), T-37 (`cal_rslv_latency_us`, `cal_rslv_wind_per_mon`) |
| FW-11 | CAN command: E2E CRC + alive counter, ≤ 20 ms stale ⇒ ramp to zero, BMS timeout ⇒ zero regen; (A12-R06) the same across the 32-bit µs wrap | `comms/can_cmd.c: can_cmd_rx, can_e2e_crc, can_cmd_fresh, can_bms_fresh, can_dir_interlock`; `control/torque.c: torque_limits, torque_clamp`; `safety/state_machine.c: st_run`; `hal/timer.h: hal_time_us64, hal_time_ms, ti_time64_extend`; `app/app.c: app_task_1ms` | `can_cmd: valid_frame_decoded, crc_bad_rejected, frozen_counter_goes_stale_after_20ms, counter_jump_rejected_then_resynced, bms_has_its_own_timeout, direction_interlock`; `torque: bms_limits_and_timeout_zero_regen, nan_torque_command_zeroed`; `time: can_freshness_across_the_microsecond_wrap`; `scenarios: stale_can_takes_torque_to_zero_not_held` (round 15: armed, a stale report is also a battery-path loss), `frozen_alive_counter_is_stale, nan_reference_never_reaches_pwm, running_across_the_microsecond_wrap_keeps_fresh_frames_fresh`; `state_machine: stale_command_ramps_then_zero_torque` | T-38 (DBC, DataIDs) |
| FW-12 | FS26: OTP/INIT readback at boot, Q&A watchdog (ERR_LIMIT 2, ≤ 3 ms window) before FS0B release, FS1B policy, GPIO1, LPOFF; (round 17, T-32) the answer first in the 1 ms task on every second task, 1890–2110 µs apart, inside the window at the fail-safe oscillator's ± 5 % | `safety/fs26.c: fs26_init, fs26_wd_due` (≥ 1500 µs), `fs26_wd_refresh, fs26_release_safety_outputs, fs26_request_fs0b, fs26_set_gpio1, fs26_goto_lpoff`; `app/app.c: app_task_1ms` (the answer first) | `fs26:` all 12 tests; `scenarios: watchdog_missed_drops_drv_en, fs26_is_answered_every_2ms_inside_its_window_at_both_oscillator_corners` | T-32…T-34 (answer, refresh cadence, MCU reset, OTP), T-25 (SPI) |
| FW-13 | Temperatures: module NTC open/short/rate, motor sensors, board NTCs | `sense/temp.c: temp_ntc_c, temp_pt1000_c, temp_update` | `temp:` all 4 tests | T-38 (sensor curves) |
| FW-14 | Gate power: RDY low ⇒ no PWM; flyback before DRV_EN | `safety/gate_power.c: gp_request_on, gp_step, gp_rdy_both` | `gate_power: refused_while_fs1b_asserted, start_ready_loss_and_recovery, start_timeout` | T-37 (RDY timings) |
| FW-15 | FLT_HS/FLT_LS: hardware PWM inhibit (FAULT0/2, fail-safe, manual clear, locked); bank to retained RAM + queued NVM; ≥ 1.5 ms reset, ASC_CLR, one-shot clear, confirm before FFLAG clear; one VCU-authorised retry; (A12-R05) no software MCU_GATE_EN drop within `cal_desat_en_hold_us` of a FLT; (F01/F02/F06) route bound, image read back, REG_PROT locked and EOL/HIL-validated before any arming | `app/app.c: app_isr_fault, fault_actions, init_evidence, evidence_watch`; `safety/fault_mgr.c: fm_desat, fm_retry_allowed, fm_retry_consumed, fm_retained_commit, fm_boot`; `safety/bridge.c: br_spo, br_service, br_rec_start, br_rec_step` (en_low / observe); `safety/arm_evidence.c`; `platform/s32k396/s32k396_pwm.c: hal_pwm_init` (lock), `hal_pwm_config_matches, hal_pwm_protection_locked, hal_pwm_fault_route_bound, hal_pwm_fault_clear`; `s32k396_board_cfg.h` | `fault_mgr: fw15_retained_first_nvm_only_queued, one_authorised_retry_after_1s_then_latch, boot_blocks_on_recent_desat_record, uncommitted_retained_record_requeued_at_boot, latched_reset_only_below_n_x_and_never_for_desat`; `bridge: fw15_recovery_waits_1p5ms_and_clears, fw15_recovery_fails_safe_on_a_hard_short, desat_hold_keeps_en_until_the_hold_then_drops_it, desat_hold_covers_br_rec_start_and_recovery_still_works, non_desat_spo_drops_en_at_once`; `safe_state: row_flt_hs, row_flt_ls_is_spo_only`; `scenarios: flt_hs_at_speed_spo_then_reset_then_pwm_asc, flt_ls_at_speed_is_spo_only_even_on_overvoltage, desat_one_authorised_retry_then_latch, desat_at_speed_holds_en_through_the_hold_then_reset_and_pwm_asc, desat_at_low_speed_spo_waits_for_the_hold, desat_retry_waits_1s_across_the_microsecond_wrap, arming_refused_without_evidence_and_the_status_names_it, unbound_fault_route_never_arms, arming_permitted_with_a_valid_record, arming_refused_on_record_identity_or_crc_mismatch, watchdog_reset_relocks_before_rearming, evidence_lost_while_armed_goes_through_section6`; `state_machine: desat_retry_runs_at_reduced_torque`; `platform_cfg: fault_lock_image, regprot_lock_decision_needs_every_bit_read_back, fault_route_unbound_by_default, protected_registers_reject_writes_from_cpu_and_dma`; `calib: validation_record_binds_image_card_and_crc`; `params: round14_cal_defaults_and_ranges`; (round 16: the retry gate counts one ms more — floored stamps) `fault_mgr: one_authorised_retry_after_1s_then_latch`, `scenarios: desat_retry_waits_1s_across_the_microsecond_wrap` | T-01…T-05, T-15; T-37 (one-shot, the hold vs the measured soft turn-off) |
| FW-16 | Boot self-test steps a–h through DRV_EN_RB / ASC_CMD_RB; energy precondition ≤ 0.1 J; stored pass; EN drops through the bridge (DESAT hold) | `safety/gate_selftest.c: st_conditions, st_energy, st_step` (EN drops via `br_spo`); `app/app.c: selftest_step` (run or stored pass) | `gate_selftest: healthy_chain_passes_all_steps, each_term_stuck_permissive_is_detected, energy_precondition, topup_then_run_and_not_counted, skipped_rather_than_run_on_assumption, failed_test_with_a_desat_drops_en_only_after_the_hold`; `state_machine: selftest_and_precharge_refusals`; `scenarios: boot_across_the_microsecond_wrap_reaches_armed` | T-16, T-17, T-37 (chain timings) |
| FW-17 | QDIS only with contactors reported open, auto-release 5 s, ≤ 3 per 5 min | `discharge/discharge.c: dis_request, dis_step` | `discharge: fires_only_with_contactors_reported_open, auto_release_after_5s, three_per_five_minutes_and_uncounted_topup`; `state_machine: discharge_only_with_contactors_open`; `scenarios: key_off_discharge_witnessed_then_lpoff` | T-37; the resistors' thermal limit is a hardware gate (contract §11) |
| FW-18 | Witness on both channels; stuck-off in 200 ms; invalid witness ⇒ HV UNKNOWN; stuck-on / unexpected discharge ⇒ latched service lock, "do not re-energise" + "open the contactors" (round 14), cleared only by FW-32 | `discharge/discharge.c: witness, stuck_on_watch` (not while modulating), `dis_hv_state`; `app/app.c: service_lock, init_service_lock`; `nvm/nvlog.h: nv_service_t` | `discharge: stuck_off_detected_in_200ms, invalid_witness_means_unknown_never_safe, stuck_on_seen_as_fast_decay_with_qdis_off, unexpected_discharge_judged_only_with_nothing_drawing_on_the_link`; `scenarios: qdis_stuck_off_detected, stuck_on_qdis_latches_service_required_and_never_rearms` | T-35 (the product key of the FW-32 routine) |
| FW-19 | Precharge plausibility: plateau ≈ 5 % low or τ too short refuses arming | `discharge/discharge.c: pch_step` | `discharge: fw19_precharge_plateau_and_tau`; `scenarios: precharge_plateau_5pct_low_refuses` | T-37 (precharge τ) |
| FW-20 | Versioned, CRC- and range-checked calibration tied to serial, SKU and motor ID; (round 16) layout 2: the resolver record carries the monitor chain's gain (codes per V pp at the monitor plane) | `nvm/calib.c: calib_check, calib_seal`; `nvm/nvlog.c`; `app/app.c: init_calibration` | `calib: nominal_needs_a_motor, each_failure_detected` (layout 1 refused); `nvlog:` all 4 tests; `scenarios: brownout_during_nvm_write_never_blocks` | T-07, T-25 (Fee), T-27 (UID) |
| FW-21 | Signed images, rollback, no-torque update state | not the application image's: the bootloader + HSE deliverable (contract FW-21, round 17) | none | — |
| FW-31 | Current-loop liveness (round 16, named in round 17): the 1 ms task sees a current-loop ISR older than `cal_isns_stale_us` ⇒ currents lost, resolver aged | `app/app.c: app_task_1ms` | `scenarios: a_stopped_current_loop_is_caught_by_the_task` | T-22 (ISR rate) |
| FW-32 | Service-lock clear (round 17): UDS SecurityAccess 0x27 (seed/key, build-time key hook — none by default ⇒ refused; one key per seed; three invalid keys lock out until the MCU restarts) and RoutineControl 0x31 start 0xF010; refused with HV present/unknown or the bridge armed; NVM record rewritten CLEARED + DTC; effective at the next power-up | `comms/uds.c: uds_handle`; `app/app.c: service_clear, diag, diag_seed`; `nvm/nvlog.h: NV_SERVICE_CLEARED` | `uds: security_access_refused_without_a_key_function, seed_key_unlocks_one_routine_run, wrong_keys_lock_out_and_malformed_requests_are_refused`; `scenarios: service_lock_clear_is_refused_without_a_key, service_lock_clear_is_refused_with_hv_present_or_armed, service_lock_clear_with_the_key_takes_effect_at_the_next_power_up` | T-26 (diagnostic RX), T-35 (the product key) |
| FW-33 | LV supply supervision (round 17, the let-through LV entry): VSUP through the FS26 AMUX (VSUP / 14, set at every boot); an overvoltage (> 20 V, the FS26's VSUPOV) is information — RUN, the torque unchanged, `DTC_LV_OVERVOLTAGE` stamped over the event — for `cal_vsup_ld_ms` above `cal_vsup_jump_max_v` (IR-03 test B) and `cal_vsup_jump_ms` at or below it (IR-02 jump start); longer ⇒ sustained: `DTC_LV_OV_SUSTAINED` and the §6 command-lost ramp until VSUP is back; no reading ⇒ `DTC_LV_VSUP_UNKNOWN` | `sense/vsup.c: vsup_update`; `safety/fs26.c: fs26_init` (M_AMUX_CTRL); `app/app.c: sense_slow, detect` (the command-lost row, the DTCs) | `scenarios: lv_load_dump_35v_for_400ms_is_information_not_a_fault, lv_overvoltage_beyond_its_band_takes_the_orderly_ramp, lv_24v_jump_start_is_information_for_its_60s` | T-39 (the AMUX reading, the profiles on the bench), T-37 (the bands vs the OEM's profiles) |

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
| Ball-map binding, S32K396 register images, REG_PROT layout; (A13-R04) every ADC input's instance/subtype/channel from the generated triple, the conversion schedule derived from it | `tools/gen-board-map.mjs`; `platform/s32k396/board_pins.h` (generated), `s32k396.h: TI_ADC_MAP_INIT`, `s32k396_cfg.h: S32K3_ADC_CH, adc_slow_chain, adc_chain_mask`; `s32k396_adc.c: cdr, hal_adc_init` (chain read-back), `hal_adc_start_slow` | `board_map:` 5 tests; `platform_cfg:` 10 tests (`adc_map_matches_the_ball_map, adc_schedule_follows_the_ball_map`) |
| Resolver frame protocol (A14-R02): per-channel DMA completion, epoch published when every channel has it, seqlock + DMA positions + copy-time bound | `hal/sdadc.h, hal/sdadc_ring.c`; `platform/s32k396/s32k396_resolver.c, s32k396_main.c` (three interrupts); `platform/host/sim_hal.c` (per-channel eDMA model) | `sdadc:` all 6 tests; `scenarios: a_frozen_resolver_channel_is_never_read_as_fresh, temporary_empty_reads_never_fault` |
| One time domain (A12-R06): 64-bit µs, ms = us64/1000 | `hal/timer.h`; `platform/host/sim_hal.c, platform/s32k396/s32k396_io.c: hal_time_us64`; `app/app.c`; `nvm/nvlog.c`; `comms/dtc.c: dtc_set, dtc_times` | `time:` 4 tests; `scenarios: running_across_the_microsecond_wrap_keeps_fresh_frames_fresh, boot_across_the_microsecond_wrap_reaches_armed, desat_retry_waits_1s_across_the_microsecond_wrap, dtc_time_stamps_across_the_microsecond_wrap_in_the_application` |
| INV_STATUS round-14 fields (b14 flags, b15 missing evidence) | `comms/can_cmd.c: can_status_encode`; `app/app.c: status_tx` | `can_cmd: status_frame_round14_fields`; scenarios above |
| The target checklist in step with the sources (round 17): every marker has its row, every row's `file:line` is a marker | `Makefile: target-check`; [`target-bringup.md`](target-bringup.md) | `make target-check` (fails on drift either way) |

## Edge cases from the task, and where each is tested

| Edge case | Test(s) |
|---|---|
| Stale, frozen and CRC-bad CAN frames | `can_cmd: crc_bad_rejected, frozen_counter_goes_stale_after_20ms, counter_jump_rejected_then_resynced`; `scenarios: stale_can_takes_torque_to_zero_not_held, frozen_alive_counter_is_stale` |
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
| Battery path lost while armed at zero, low, high and unknown speed, contactors OPEN / INVALID / stale report, 340 A rms and regenerating (A13-R02) | `scenarios: low_speed_open_contactor_is_a_battery_path_loss, battery_path_loss_while_armed_at_every_speed`; `state_machine: leaving_run_grants_no_torque_in_the_same_invocation, battery_path_row_leaves_run_and_blocks_reentry`; `fault_mgr: battery_lost_is_a_fault_until_its_response_is_done` |
| The FW-08 zero-torque opening at standstill stays a normal disarm (no FAULT) | `scenarios: zero_torque_opening_at_standstill_disarms_without_fault` |
| A relocated ADC input keeps its generated subtype (NTC_A = ADC5_S11); relocated slow inputs are in a started chain (MT2_SIG ADC1_P0, HW_ID ADC3_P0) (A13-R04) | `platform_cfg: adc_map_matches_the_ball_map, adc_schedule_follows_the_ball_map`; `scenarios: hw_id_is_converted_before_it_is_classified` |
| Resolver delivery stops after a good acquisition at standstill, at low speed across the µs wrap and at 10 000 rpm: deadline, torque response, safe state, speed hold, re-acquisition (A14-R01) | `resolver: validity_expires_without_new_frames_and_reacquires_from_scratch`; `scenarios: resolver_frames_stopping_withdraws_the_angle_at_the_hold` |
| A current loop faster than the frames; a late channel (A14-R01) | `scenarios: temporary_empty_reads_never_fault`; `sdadc: a_late_channel_holds_the_frame_back_and_the_stamp_is_the_first` |
| Each SDADC DMA channel frozen, one delayed, completions between the reader's channel copies, a preempted reader, interrupts held off, the epoch counter wrap (A14-R02) | `sdadc:` all 6 tests; `scenarios: a_frozen_resolver_channel_is_never_read_as_fresh` |
| Each missing phase channel and every combination, repeated, recovered, across the µs wrap; the BCTU/converter stopped (A14-R03) | `scenarios: lost_phase_current_triplets_take_the_failure_path, lost_triplets_across_the_microsecond_wrap_keep_a_defined_stamp, a_stopped_current_loop_is_caught_by_the_task`; `current: lost_sample_is_invalid_and_keeps_its_last_stamp` |
| SWG at its low/typical/high MAXAPP corner; a 25 Ω resolver; a PTC at 5 Ω after a trip, restarts warm and cool (A14-N01) | `scenarios: swg_trim_is_written_to_the_generator, a_low_impedance_resolver_saturates_the_trim_with_a_dtc, ptc_post_trip_is_flagged_at_the_winding_and_a_cool_restart_recovers`; `params: exciter_planes_and_trim_headroom`; `resolver: winding_plane_flags_what_the_monitor_cannot_see, swg_trim_ramps_readies_and_saturates` |
| Battery path lost below n_x with the link above the normal range (the trim engaged in RUN the moment before), and at 7000 rpm, where zero torque alone would still ask for field-weakening current (item 26) | `scenarios: battery_path_loss_below_n_x_applies_and_reports_zero_current, battery_path_loss_while_armed_at_every_speed` |
| Braking with the battery present while the pack pushes the link above `vdc_max_v`, and back into the range (item 26) | `dclink: trim_takes_back_regen_only_above_the_range_maximum`; `scenarios: dc_link_trim_limits_regen_with_the_battery_present` |
| ASC exit into modulation (after an MCU reset at 10 000 rpm) with the current-loop ISR preempting right behind the clear, SiC and IGBT (FW-06a) | `scenarios: asc_exit_first_high_side_pulse_after_the_release_deadline`; `bridge: asc_exit_only_when_allowed_and_hs_after_the_release` |
| The FS26 answered on the target's exact 1 ms tick grid, in RUN, with its fail-safe oscillator at −5 %, 0 and +5 % (T-32) | `scenarios: fs26_is_answered_every_2ms_inside_its_window_at_both_oscillator_corners` |
| Service-lock clear: no key configured, a key without a seed, a seed reused, wrong keys up to the lockout, malformed or unsupported requests, HV present, the bridge armed while the link reads below 60 V, accepted with a key, one run per unlock, the next power-up (item 19) | `uds:` all 3 tests; `scenarios: service_lock_clear_is_refused_without_a_key, service_lock_clear_is_refused_with_hv_present_or_armed, service_lock_clear_with_the_key_takes_effect_at_the_next_power_up` |
| KL30 at 35 V for 400 ms while running (ISO 16750-2 test B, let-through), then back: RUN, the torque and the PWM unchanged, the DTC's stamps 400 ms apart; 35 V held one millisecond past `cal_vsup_ld_ms`: the ramp to zero, SPO below n_x, recovery when KL30 returns (FW-33) | `scenarios: lv_load_dump_35v_for_400ms_is_information_not_a_fault, lv_overvoltage_beyond_its_band_takes_the_orderly_ramp` |
| A 24 V jump start for 60 s while running — and the 2023 edition's 26 V at the AMUX's highest reading (26.5 V) — information for the whole minute, sustained only past `cal_vsup_jump_ms` (FW-33) | `scenarios: lv_24v_jump_start_is_information_for_its_60s` |

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

## Round 15: before/after on the pre-fix source

Method, as in round 14: the pre-fix `firmware/` was extracted from commit a8c75eb into a scratch
directory and the round-15 `tests/` copied over it. The old tree also received the one new test hook of
the host simulation (`sim_adc_require_slow_start`: test infrastructure, not firmware), and a compatibility
header force-included into the test files only mapped each new interface onto what the old code did:
`fm_needs_fault_state(f, done)` = the old one-argument function (every battery-lost row a FAULT);
`TI_ADC_MAP_INIT` = the old private MAP[] of `s32k396_adc.c`, translated row by row by `sed`
(`{I, S32K3_ADC_CH('X', C), G_g}` → `{I, 'X', C, g}`); `adc_slow_chain` = the old hand-written start list
(normal chains of ADC0/3/4/5, nothing else); `adc_chain_mask` = what the old MAP implies through the old,
non-strict `S32K3_ADC_CH()` (the old firmware derived no masks). One new test reads a new state-machine
input with no old counterpart and was not compiled there (`state_machine:
battery_path_row_leaves_run_and_blocks_reentry`). Every pre-existing test still passed on the pre-fix
source with the new tests (222 run there, 100 checks failed, all in the tests below); this tree: 223
tests, 1882 checks, 0 failed (also under ASan/UBSan and at `-O2`).

| Suite: test | Item | Pre-fix (a8c75eb) | This tree |
|---|---|---|---|
| platform_cfg: adc_map_matches_the_ball_map | A13-R04 | FAIL (3): NTC_A mapped ADC5 **P**11 against the ball map's ADC5_S11 (RTD channel 11, not 43); `S32K3_ADC_CH()` accepted 'P' 11, 'N' 0 | pass |
| platform_cfg: adc_schedule_follows_the_ball_map | A13-R04 | FAIL (5): INTRLOK_N (ADC1 P7), TMOD_W (ADC1 S8) and MT2_SIG (ADC1 P0) in no started chain; ADC5's chain without S11 | pass |
| scenarios: hw_id_is_converted_before_it_is_classified | A13-R04 | FAIL (2): HW_ID read "never converted" at init ⇒ DTC_HWID_SHORT, init FAIL, never armed | pass |
| scenarios: low_speed_open_contactor_is_a_battery_path_loss | A13-R02 | FAIL (3): the reviewer's reproduction — no row (cont_lost = 0), ARMED_ZERO_TORQUE with arm = 1 and torque_enable = 1, iq_ref of the 100 Nm request | pass |
| scenarios: battery_path_loss_while_armed_at_every_speed | A13-R02 | FAIL (70), all 12 cases: zero/low × OPEN/INVALID (8 each): no row, ARMED_ZERO_TORQUE with arm/torque_enable, the request still the target, then all gates off at 480 A (PRECHARGE_WAIT's SPO) instead of zero-torque current control, status not FAULT; zero/low × stale (11 each): the same plus torque kept on the FW-11 ramp and no release; high × OPEN/INVALID (1 each): arm = 1 and torque_enable = 1 on the way to FAULT; high × stale (5): no row (only the command row's ASC), RUN with torque permission; unknown × all (3 each): no row (last speed low), arm/torque_enable | pass |
| scenarios: stale_can_takes_torque_to_zero_not_held (rewritten) | A13-R02 | FAIL (5): no battery-path row, the FW-11 ramp, no release to SPO | pass |
| state_machine: leaving_run_grants_no_torque_in_the_same_invocation | A13-R02 | FAIL (11): torque_enable = 1 in every exit from RUN/DERATE, arm = 1 on the way to FAULT and PRECHARGE_WAIT | pass |
| state_machine: battery_path_row_leaves_run_and_blocks_reentry | A13-R02 | not compiled (new input) | pass |
| fault_mgr: battery_lost_is_a_fault_until_its_response_is_done | A13-R02 | FAIL (1): no "done" | pass |
| scenarios: zero_torque_opening_at_standstill_disarms_without_fault | A13-R02 guard | pass | pass |
| scenarios: dtc_time_stamps_across_the_microsecond_wrap_in_the_application (zero torque) | A12-R06 | pass | pass |
| safe_state: unknown_speed_takes_high_column (battery row added) | A13-R02 guard | pass | pass |

Two more runs outside the suite. A mutation of this tree forcing NTC_A's subtype back to `'P'` in
`TI_ADC_MAP_INIT` fails `adc_map_matches_the_ball_map` (and `adc_schedule_follows_the_ball_map`: the pair is
invalid, so the input is in no chain). And both versions of `s32k396_adc.c` were compiled with
`TI_RTD_AVAILABLE` against stand-in RTD headers and fake ADC registers (UBSan on): the pre-fix driver read
NTC_A from `PCDR[11]` (UBSan: index 11 out of bounds for `uint32_t[8]`; it returned ICDR3's value), started
`0N 3N 4N 5N` and accepted a chain configuration without S11; this tree reads ICDR11, starts
`0N 1J 3N 4N 5N` and refuses that configuration.

## Round 16: before/after on the pre-fix source

Method, as in rounds 14 and 15: the pre-fix `firmware/` was extracted from commit 32214be into a scratch directory
and the round-16 `tests/` copied over it, with the round-16 host simulation (`sim_hal.c`, `sim.h`: test
infrastructure — the per-channel eDMA model, the excitation chain, `sim_adc_phase_stop`). Behind the simulation the
pre-fix DRIVERS' behaviour was kept, re-expressed on it (a compatibility file, not firmware of this tree):
- SDADC: the pre-fix `s32k396_resolver.c` protocol — a two-block ring, the SIN channel's completion interrupt alone
  counting and stamping "a block of all three channels", each channel's reader copying its slot for the newest count,
  and `sense_fast()`'s three reads in turn keeping the last stamp (`hal_sdadc_read_frame()` of the tests is that
  composition; the pre-fix application kept calling `hal_sdadc_read_block()`). The pre-fix HOST simulation computed
  every block on demand, always coherent, which is why the pre-fix suite never saw A14-R02;
- ADC: the pre-fix `s32k396_adc.c` — a missing channel zero-filled, `*t_us` not written, false returned (ignored by
  the pre-fix caller). The pre-fix firmware was compiled with `-ftrivial-auto-var-init=pattern`, so its uninitialised
  `t` reads 0xAAAAAAAA on every run instead of whatever the stack held;
- data the pre-fix firmware never reads: the round-16 parameter rows (regenerated), the two DTC ids, `HAL_SWG_CODE_MAX`;
  the resolver fields the tests inspect (`mon_vpp`, `wind_vpp`, `exc_ready`, `swg_sat`, `stale`, never written there)
  and `rslv_age()` as nothing (the pre-fix resolver had no age check); `t_frame_us` read as the pre-fix `t_ref_us`;
- the calibration field translation: the round-16 record gives the monitor gain (2500 codes per V pp); the pre-fix
  code divided the monitor amplitude by its EOL amplitude, which that record implies as 2500 × 7.2 = 18000 codes.
One new test exercises a pure new interface and was not compiled there (`current: lost_sample_is_invalid_and_keeps_its_last_stamp`).
Every pre-existing test still passed on the pre-fix source with the new tests and simulation (242 run there, 122
checks failed, all in the tests below); this tree: 243 tests, 2192 checks, 0 failed (also under ASan/UBSan and at `-O2`).

| Suite: test | Item | Pre-fix (32214be) | This tree |
|---|---|---|---|
| resolver: validity_expires_without_new_frames_and_reacquires_from_scratch | A14-R01 | FAIL (8): the reviewer's reproduction — valid 1 at the hold and still a second later, at standstill and 3000 rpm, with and without the µs wrap | pass |
| scenarios: resolver_frames_stopping_withdraws_the_angle_at_the_hold | A14-R01 | FAIL (15): in all three cases the resolver stayed valid and the bridge kept modulating on the extrapolated angle — RUN with 200 / 100 Nm, field weakening at 10 000 rpm — no row, no FAULT, no SPO/PWM-ASC; still so 1 s later | pass |
| scenarios: temporary_empty_reads_never_fault | A14-R01/R02 guard | FAIL (2): with COS 30 µs late the pre-fix reader paired COS's previous block with the new EXC/SIN: an acceleration fault 8 ms in, FAULT (SiC and IGBT) | pass |
| sdadc: every_frame_is_one_epoch_of_all_three_channels | A14-R02 guard | pass (a healthy DMA) | pass |
| sdadc: a_frozen_channel_yields_no_frame | A14-R02 | FAIL (8): EXC or COS frozen — frames kept coming with the frozen block (tags disagree); nothing ever detects it | pass |
| sdadc: a_late_channel_holds_the_frame_back_and_the_stamp_is_the_first | A14-R02 | FAIL (3): a frame before COS completed, mixing COS's previous block | pass |
| sdadc: a_completion_between_channel_reads_never_mixes_epochs | A14-R02 | FAIL (7): EXC of one epoch with SIN/COS of the next (the reviewer's case), and the preempted reader's frames accepted | pass |
| sdadc: held_off_completion_interrupts_never_yield_a_fresh_frame | A14-R02 | FAIL (2): a frame whose slot the DMA was about to rewrite, accepted | pass |
| sdadc: the_epoch_counter_wraps | A14-R02 guard | pass | pass |
| scenarios: a_frozen_resolver_channel_is_never_read_as_fresh | A14-R02 | FAIL (10): EXC frozen — the frozen block read as fresh, valid, RUN at 100 Nm (the excitation supervision blind); SIN frozen — no heartbeat, validity never expired, RUN; COS frozen — frozen blocks paired with fresh SIN until the amplitude window tripped | pass |
| current: lost_sample_is_invalid_and_keeps_its_last_stamp | A14-R03 | not compiled (new interface) | pass |
| scenarios: lost_phase_current_triplets_take_the_failure_path | A14-R03 | FAIL (28), all 7 combinations: the sample taken as an open wire (zero-filled codes), DTC_ISNS_OPEN not ISNS_STALE, its stamp the uninitialised 0xAAAAAAAA, never the last good one. (The zero-fill made the channel invalid, so no FOC ran on it — by accident of the diagnosis.) | pass |
| scenarios: lost_triplets_across_the_microsecond_wrap_keep_a_defined_stamp | A14-R03 | FAIL (1): undefined stamps across the wrap | pass |
| scenarios: a_stopped_current_loop_is_caught_by_the_task | A14-R03 | FAIL (4): with no current-loop interrupt the currents stayed valid, no row, the PWM kept its last duty cycle | pass |
| params: round16_cal_defaults_and_ranges | A14-R01/N01 CALs | pass (rows added to the old tree) | pass |
| params: exciter_planes_and_trim_headroom | A14-N01 | FAIL (4): no plane checks — setpoints beyond the low corner, the slew ceiling and the cold floor, and a credited post-trip PTC, all accepted | pass |
| resolver: winding_plane_flags_what_the_monitor_cannot_see | A14-N01 | FAIL (8): no planes; the post-trip winding (6.3 V pp) not flagged | pass |
| resolver: swg_trim_ramps_readies_and_saturates | A14-N01 | FAIL (3): no ready/saturation state | pass |
| scenarios: swg_trim_is_written_to_the_generator (rewritten) | A14-N01 | FAIL (10): the SWG started at code 12, not the low initial code; no monitor-plane reading; the high corner settled elsewhere | pass |
| scenarios: a_low_impedance_resolver_saturates_the_trim_with_a_dtc | A14-N01 | FAIL (2): no saturation DTC, and the pre-fix firmware armed with 6.1 V pp at the winding | pass |
| scenarios: ptc_post_trip_is_flagged_at_the_winding_and_a_cool_restart_recovers | A14-N01 | FAIL (5): the tripped PTC not flagged (the monitor saw nothing); the warm restart armed | pass |
| calib: each_failure_detected (layout 2) | A14-N01 | FAIL (1): a layout-1 record accepted | pass |
| fault_mgr: one_authorised_retry_after_1s_then_latch (boundary) | FW-15 incidental | FAIL (1): retry allowed at 1000 floored ms | pass |

Three more runs outside the suite:
- Both S32K396 drivers were compiled with `TI_RTD_AVAILABLE` against stand-in RTD headers and fake registers (ASan/UBSan
  on; stand-in IMCR values, the `.ti_nocache` section given the host's Mach-O form). `hal_adc_read_phase()` with V's
  data register lacking VALID: the pre-fix driver returned false but wrote `{2100, 0, 1900}` into the caller's codes (a
  partial triplet) and left `*t_us`; this tree writes nothing. The SDADC driver with COS's DMA frozen for 20 carrier
  periods: the pre-fix driver reported 20 "fresh" COS blocks (its buffer never written); this tree returned no frame,
  and with all three DMAs moving (positive control) 20 frames, each with its epoch and block-start stamp.
- Mutations of this tree, each caught by the suite: no age check per tick (24 checks fail), publishing on SIN alone (2),
  no DMA-position check (2), no seqlock / no copy-time bound (1 each), a lost triplet not marked (57), no current-loop
  liveness (4), no winding-plane floor (5), no saturation flag (2), no headroom check (2), SWG started at code 15 (8).
- A latent interaction the round-16 timing exposed (README "Round 16", incidental rows): the resolver's first
  acquisition now waits for the SWG ramp, and a never-acquired resolver raised the "control lost" row in FAULT with
  MCU_GATE_EN for PWM-ASC in a no-arm state; five existing no-arm scenarios caught it, fixed by requiring a resolver
  that was valid once.

## Round 17: before/after on the pre-fix source

Round 17 closes the firmware's 35 open items (README, "Decisions recorded in the contract"); two decisions
change the image's behaviour — item 26 (FW-08: zero current under the battery-lost row, the DC-link trim only
in RUN) and item 19 (FW-32: the UDS service routine) — and three more changes came in the same round: the FS26
watchdog answer cadence (FW-12, checklist T-32: a defect found on the way), the ASC-exit release wait (FW-06a
step 3) and the LV supply supervision of the let-through LV entry (FW-33), the last two from the
qualification-plan review. Method, as in rounds 14–16: the pre-fix `firmware/` (the tree of this round's start,
243 tests / 2192 checks / 0 failed) was copied into a scratch directory and the round-17 `tests/` copied over it,
with the round-17 host simulation (`sim.h`, `sim_fs26.c`, `sim_hal.c`: test infrastructure — the FS26's
fail-safe oscillator tolerance and its AMUX, the time of the first modulated edge; the AMUX register address is
written as a literal there, the pre-fix `fs26_regs.h` does not name it). The tests bring the harness with them:
its two exact clocks (`tests/harness.c`: the current-loop trigger and the 1 ms tick on their own grids, run in
time order; ISR triggers a long task covers are dropped, neither grid ever shifts) and the plant's ADC-level
noise (± 0.55 A per phase, deterministic). A compatibility header force-included into the test files only
mapped the new interfaces onto what the pre-fix firmware did: `SS_ACT_ZERO_CURRENT` = the old
`SS_ACT_ZERO_TORQUE_DCL`; `dcl_trim()` = the command unchanged (the pre-fix firmware applied no trim in RUN);
`cal_asc_release_ns` = the old `asc_exit_hs_delay_ns` (its 1 µs wait after the pulse); `DTC_SERVICE_LOCK_CLEARED`
and the three `DTC_LV_*` = `DTC_COUNT` (no such record: `dtc_active()` false); `NV_SERVICE_CLEARED` its value;
the new `uds.h` supplied for its constants. The pre-fix `app_t` has no UDS server and no VSUP supervision, so
`sed` removed from the copied scenarios the one line installing the test key (`g_app.uds.key_fn = test_key`) and
one `key_fn == NULL` check, and pointed `g_app.vsup` at an all-false record and the two `cal_vsup_*` durations
at their defaults (500 ms, 65 s); the UDS unit suite exercises a module the pre-fix tree does not have and was
not compiled there. Every pre-existing test still passed on the pre-fix source with the new tests and harness
(253 run there, 2281 checks, 52 failed, all in the tests below); this tree: 256 tests, 2310 checks, 0 failed
(also under ASan/UBSan and at `-O2`).

| Suite: test | Item | Pre-fix | This tree |
|---|---|---|---|
| dclink: trim_takes_back_regen_only_above_the_range_maximum (rewritten; replaces `sign_and_bounds`) | 26 | FAIL (6): no regen taken back above `vdc_max_v` in either direction, no bound, a link that cannot be judged keeps its regen, a non-finite command or speed passed through | pass |
| scenarios: battery_path_loss_while_armed_at_every_speed (tightened) | 26 | FAIL (6): all six slow cases (zero/low speed × OPEN/INVALID/stale) — `t_cmd_nm` −50 Nm, the DC-link PI at full authority toward 850 V from a 750 V link, while iq was held at 0 | pass |
| scenarios: dc_link_trim_limits_regen_with_the_battery_present | 26 | FAIL (1): with the battery present and the link at 870 V (above the 850 V range maximum) the full −150 Nm regen was applied — no trim in RUN | pass |
| scenarios: battery_path_loss_below_n_x_applies_and_reports_zero_current | 26 | FAIL (7): 1000 rpm, link at 870 V: `t_cmd_nm` +10.4 Nm (the PI demanding motoring), INV_STATUS +17.5 Nm with the zero-torque bit clear while zero iq was applied, the PI integrator winding up; 7000 rpm, 750 V: `t_cmd_nm` −50 Nm, id_ref −52 A (the field-weakening/MTPA id of that output) under the "zero torque" row, INV_STATUS −50 Nm | pass |
| scenarios: service_lock_clear_is_refused_without_a_key | 19 | FAIL (3): no response on the diagnostic bus (no UDS server) | pass |
| scenarios: service_lock_clear_is_refused_with_hv_present_or_armed | 19 | FAIL (5): no response, no unlock | pass |
| scenarios: service_lock_clear_with_the_key_takes_effect_at_the_next_power_up | 19 | FAIL (7): no response, no record of a clear, the lock still set at the next power-up, never armed again | pass |
| uds: the three unit tests | 19 | not compiled (new module) | pass |
| bridge: asc_exit_only_when_allowed_and_hs_after_the_release (renamed from `..._hs_after_1us`) | FW-06a | FAIL (1): the first high-side pulse 2000 ns after the clear's falling edge (a 1 µs wait after the 1 µs pulse), short of 1.07 µs + the 1.0 µs dead time | pass: 4000 ns |
| scenarios: asc_exit_first_high_side_pulse_after_the_release_deadline | FW-06a | FAIL (2): 2000 ns on SiC (deadline 2070 ns) and on IGBT (deadline 3570 ns: the IGBT's 2.5 µs turn-off allowance was never waited for) | pass: 4000 ns SiC, 5000 ns IGBT |
| scenarios: fs26_is_answered_every_2ms_inside_its_window_at_both_oscillator_corners | T-32 | FAIL (6): answers every 3000 µs at all three corners — the due test first holds on the third task; at +5 % the window expires at 2857 µs, the late answer then lands 142 µs into the next window's closed half: two errors per cycle, FS0B from the first, RUN never reached. At −5 % and nominal the 3.0 ms answer sits at or just inside the window's end (the model accepts an answer at exactly 3000 µs; silicon at ± 5 % is a coin toss) | pass: every 2000 µs, no watchdog error, RUN |
| scenarios: lv_load_dump_35v_for_400ms_is_information_not_a_fault | FW-33 | FAIL (2): the 35 V / 400 ms pulse left no trace — no DTC, no duration, no VSUP reading at all (RUN, the torque and the PWM were already undisturbed: the firmware never read VSUP) | pass: RUN, 100 Nm, no row; DTC_LV_OVERVOLTAGE stamps 399 ms apart |
| scenarios: lv_overvoltage_beyond_its_band_takes_the_orderly_ramp | FW-33 | FAIL (2): 35 V held past 500 ms was never acted on — no sustained state, no ramp: 100 Nm throughout | pass: sustained at 501 ms, the command-lost ramp to 0 Nm and SPO (armed, below n_x), 100 Nm again once KL30 is back |
| scenarios: lv_24v_jump_start_is_information_for_its_60s | FW-33 | FAIL (4): at 24 V and at 26.5 V, no record of the minute and nothing past 65 s | pass: information for 60 s at both levels, sustained at 65 s |

Test infrastructure changed with it: the harness keeps the target's two clocks (above) instead of advancing time
in relative steps, in which every FS26 SPI transfer shifted all later ticks (the answers read 2.03 ms apart and
long FW-16/FW-15 busy waits shifted the grid too); the scenarios' `tick_1ms()` now runs one grid tick
(`h_tick()`) instead of 1000 µs of ISRs followed by a hand-called task. No expectation of an existing test
changed. On the exact grid the current samples are phase-locked to the rotor (at 1000 rpm on the same electrical
angles every period), so without noise their quantisation error repeated as a constant dq offset, which the
current PIs integrated against a plant that does not respond to voltage, until FW-10's rate check tripped after
3.4 s — a harness artefact (a real converter dithers it away; the relative-step harness never locked): the plant
now adds ± 0.55 A per phase (one LSB of the 2.22 mV/A sensor), and a 60 s run at 1000 rpm is as steady as before.
The FS26 model gained its AMUX (the selected input over its divider, 0 V when not selected: `sim_fs26_vsup()`
sets KL30) and `sim_hal.c` the time of the first modulated edge after an ASC. The host sim now measures the
answers exactly 2000 µs apart on all four SKUs (ISR every 50 µs SiC,
100 µs IGBT; 3000 µs with the pre-fix firmware). The set of tests that see a watchdog error at all is the same
as at the round's start (the deliberate watchdog tests, and scenarios that run long ISR-only stretches without
the 1 ms task).

Mutations of this tree, each caught by the suite (failed checks): id not zeroed under the row (2), the trim not
reset outside RUN (2), the trim's output used under the row as before (11), the trim's reference 30 V above the
range maximum (4), the trim allowed to add motoring torque (1); the key not compared (6), the routine without the
unlock (9), no HV check (2), no armed check (1), the unlock not consumed by a run (4), a seed handed out with no
key function (4), the clear not written to NVM (3), no attempt lockout (1); the watchdog due threshold back at
2000 µs (6), at 900 µs — an answer in the closed window (371); the ASC exit's wait back at 2 µs from the clear's
falling edge (4), without the dead time (1), without the release (3); the LV supervision's sustained state not in
the command-lost row (4), no tolerance at all (4), no load-dump band (2), no jump-start band (2), a sustained
state that never ends (1), no information DTC (4), the AMUX not configured (14), the AMUX at ratio 7.5 read as
14 (11). Placing the answer first in the task bounds its offset on the target; the host's offset is the SPI time only, so that
placement is proven by the T-32/T-36 measurements, not by a host test.
