# Traceability: FW-xx → code → test

Requirements from `docs/firmware-contract.md` (rev A.17), the round-14 review of commit cfd35a7, the
round-15 rechecks of commit a8c75eb, the round-16 rechecks of commit 32214be, the round-17 closure of the
open items (contract §10c; FW-31, FW-32) and the round-18 rechecks of commit 4425af9 (contract §10d; FW-34…FW-36) and the round-23 torque solver (contract §10e; FW-37) and round-23 overspeed, run-time statistics and offset refresh (contract §10j; FW-42…FW-44), and the round-23 fixes (contract §10k; rows R23-1…R23-14 after FW-44, and "Round 23 — Fixes"), and round-23 FW-45 / FW-46 (contract §10l / §10m; "Round 23 — FW-45/FW-46" at the end), and round 24's FW-45 flux slope and FW-46 whole interval (contract §10l / §10m; "Round 24 — FW-45 flux slope, FW-46 whole interval" at the end), and round 24's acquisition freshness of the slow channels (contract §10n; row F241 after R23-14, and "Round 24 — acquisition freshness (F241)" at the end), with its follow-ups F243 (FW-13's rate latch) and F244 (the V_DC stamps; rows F243, F244 after F241, and "Follow-ups F243, F244" at the end).
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
| FW-05 | Phase overcurrent, hardware compare at 1.25·√2·I_pk; validity window, Σi; (F24) latent stuck channel; (A14-R03) a triplet only when all three channels of one trigger arrived, a lost sample takes the current-sensor path while V_DC and the resolver continue; a stopped current loop is seen by the task; (round 18) the triplet's freshness judged after it was read, signed (FW-34) | `sense/current.c: isns_oc_codes, isns_oc, isns_activity, isns_lost`; `app/app.c: set_watchdogs, app_isr_fault, stuck_check, detect, sense_fast` (triplet or lost), `app_task_1ms` (current-loop liveness); `hal/adc.h` (all three or nothing); `platform/s32k396/s32k396_adc.c: hal_adc_set_watchdog, hal_adc_read_phase`; `s32k396_pwm.c` (FAULT1 → high sides) | `current: overcurrent_at_the_crest_8xx_both_polarities, overcurrent_4xx_threshold, activity_catches_a_stuck_channel_where_current_is_asked, lost_sample_is_invalid_and_keeps_its_last_stamp`; `params: fw05_trip_is_instantaneous_1p25_sqrt2_ipk`; `scenarios: overcurrent_crest_does_not_trip_620_does, all_three_current_channels_stuck_detected_under_command, one_current_channel_stuck_below_the_kcl_tolerance_detected, lost_phase_current_triplets_take_the_failure_path, lost_triplets_across_the_microsecond_wrap_keep_a_defined_stamp, a_stopped_current_loop_is_caught_by_the_task`; `platform_cfg: fault_lock_image, adc_indices_and_thresholds` | T-08…T-10 (route, thresholds, WTISR), T-12 (BCTU list), T-14 (VALID bit), T-37 (activity CALs) |
| FW-06 | DC overvoltage: both channels in hardware compare; ADC watchdog → PWM-ASC request within 15.6 µs | `sense/vdc.c: vdc_ov_code`; `app/app.c: set_watchdogs, app_isr_fault`; `safety/bridge.c: br_enter_pwm_asc` | `vdc: ov_compare_code`; `scenarios: fw06_ov_to_asc_request_within_15p6us, flt_ls_at_speed_is_spo_only_even_on_overvoltage`; `bridge: pwm_asc_entry_is_break_before_make`; `safe_state: row_overvoltage_and_overcurrent` | T-05 (the whole chain on HIL; the host models the analog, sampling and ISR delays), T-08, T-11 |
| FW-06a | ASC exit only by MCU command, ASC_CLR first; (round 17) the first HS pulse only after the ASC pins' release (`cal_asc_release_ns`, ≥ the verifier's 1.07 µs) plus the dead time, from the clear's falling edge | `safety/bridge.c: br_exit_asc` (`asc_clear_pulse`: the falling-edge stamp), `br_modulate` (the wait); `app/app.c: apply_decision` | `bridge: asc_exit_only_when_allowed_and_hs_after_the_release`; `scenarios: asc_exit_only_below_n_x_by_mcu_command, mcu_reset_at_speed_keeps_asc_then_exits_with_battery, asc_exit_first_high_side_pulse_after_the_release_deadline` | T-36 (the exit edge) |
| FW-07 | V_DC plausibility: 5 % disagreement, VOFS window, V5GD window, fail-safe < 0.25 V; (round 18) channel freshness judged after the reads, signed (FW-34); (F244) a stopped channel stale until a new stamp, also where its 32-bit age wraps (row F244) | `sense/vdc.c: vdc_update, vdc_bms_check` | `vdc:` all 10 tests; `scenarios: vdc_disagreement_spo_and_unknown_hv, v5gd_out_of_window_forces_spo_and_unknown_hv`; `safe_state: row_v5gd_and_vdc_invalid` | T-07 (EOL gains and offsets) |
| FW-08 | Regeneration with the battery lost: (round 17) zero current, id = iq = 0 at the current-loop rate, reported as 0 Nm — the DC-link trim only in normal RUN with the battery path proven (a regen limiter above `vdc_max_v`); (A13-R02) the battery-path loss detected whenever armed at every speed — OPEN, PRECHARGE, INVALID or stale report, V_DC off the pack — §6 response from speed, winding current, V_DC and the actuators; no torque permission in the invocation that processes it | `control/dclink.c: dcl_trim`; `app/app.c: torque_path` (trim in RUN, reset outside; t_cmd 0 and id = iq = 0 under the row), `control_fast`; `safety/safe_state.c: ss_decide` (BATTERY_LOST, BMS_LIMIT_ZERO rows); `app/app.c: detect` (battery path), `apply_decision` (zero torque, then SPO once the current is gone), `torque_path` (the §6 current-control permission), `gather`; `safety/fault_mgr.c: fm_needs_fault_state`; `safety/state_machine.c: st_run` | `dclink: trim_takes_back_regen_only_above_the_range_maximum`; `safe_state: row_battery_lost_never_released_by_rule_b, row_bms_limit_zero_with_battery_is_never_asc, unknown_speed_takes_high_column`; `fault_mgr: battery_lost_is_a_fault_until_its_response_is_done`; `state_machine: leaving_run_grants_no_torque_in_the_same_invocation, battery_path_row_leaves_run_and_blocks_reentry`; `scenarios: bms_limit_zero_connected_is_not_asc_but_contactor_open_is, low_speed_open_contactor_is_a_battery_path_loss, battery_path_loss_while_armed_at_every_speed, zero_torque_opening_at_standstill_disarms_without_fault, dc_link_trim_limits_regen_with_the_battery_present, battery_path_loss_below_n_x_applies_and_reports_zero_current` | T-37 (trim gains on the real bank), T-38 |
| FW-08b | "Keep HV connected" while an SPO relies on the battery (rule (a) fails, battery present) at any speed, until rule (a) holds or ASC; battery absent ⇒ "no safe state proven" (A12-R08) | `safety/safe_state.c: ss_decide` (keep_hv, energy_dtc); `safety/fault_mgr.c: combine, fm_update` (keep_hv, no_safe_state); `app/app.c: status_tx`; `comms/can_cmd.c: can_status_encode` (b1.5, b14.0) | `safe_state: keep_hv_follows_the_battery_as_the_sink_at_every_speed, row_flt_hs, row_flt_ls_is_spo_only`; `fault_mgr: keep_hv_until_asc_or_low_speed, keep_hv_held_until_rule_a_and_no_safe_state_without_battery`; `scenarios: standstill_desat_at_rated_current_reports_keep_hv_on_can, flt_hs_at_speed_spo_then_reset_then_pwm_asc`; `can_cmd: status_frame_round14_fields` | T-38 (rule (b), the BMS) |
| FW-09 | HVIL ladder signature; open ⇒ ramp to zero ≤ 100 ms | `sense/hvil.c: hvil_classify, hvil_step`; `app/app.c: detect` | `hvil: signatures, open_detected_within_100ms, short_to_ground_detected`; `scenarios: hvil_open_ramps_torque_within_100ms` | T-38 (harness signatures) |
| FW-10 | Resolver: amplitude window, tracking error, rate vs model, excitation monitor, −24° compensation; (A14-R01) validity expires `cal_rslv_hold_us` after the newest coherent frame, then the resolver-invalid row and a from-scratch re-acquisition; (A14-R02) one coherent frame of the three channels per epoch or none; (A14-N01) the setpoint at the monitor plane (7.2 V pp), the 6.5 V pp floor at the winding, the SWG ramp from a low code, the trim headroom at the low corner, a DTC when the trim saturates; (round 18) the frame stamped on the SDADC cadence (FW-35), the chain latency added (FW-36); (round 19) the cadence dated by the SWG start, the synchronized producer restart (FW-35) | `control/resolver.c: rslv_update` (observer, planes), `rslv_age, rslv_rate_check, rslv_theta_e_at, rslv_swg_trim`; `hal/sdadc.h, hal/sdadc_ring.c` (frame protocol); `platform/s32k396/s32k396_resolver.c: s32k_sdadc_dma_irq, hal_sd_dma_slot, hal_sdadc_read_frame`; `app/app.c: sense_fast, sense_slow, detect`; `nvm/params.c: ti_params_validate` (planes); `nvm/calib.c` (monitor gain) | `resolver:` all 15 tests; `sdadc:` all 24 tests; `params: round16_cal_defaults_and_ranges, exciter_planes_and_trim_headroom`; `scenarios: resolver_amplitude_low_asc_at_speed_spo_below, mcu_reset_at_speed_keeps_asc_then_exits_with_battery, swg_trim_is_written_to_the_generator, resolver_frames_stopping_withdraws_the_angle_at_the_hold, temporary_empty_reads_never_fault, a_frozen_resolver_channel_is_never_read_as_fresh, a_low_impedance_resolver_saturates_the_trim_with_a_dtc, ptc_post_trip_is_flagged_at_the_winding_and_a_cool_restart_recovers` | T-28…T-31 (SDADC, eDMA, SWG, the IOAMPL law), T-07 (EOL trim, monitor gain, ratio), T-37 (`cal_rslv_latency_us`, `cal_rslv_wind_per_mon`) |
| FW-11 | CAN command: E2E CRC + alive counter, ≤ 20 ms stale ⇒ ramp to zero, BMS timeout ⇒ zero regen; (A12-R06) the same across the 32-bit µs wrap | `comms/can_cmd.c: can_cmd_rx, can_e2e_crc, can_cmd_fresh, can_bms_fresh, can_dir_interlock`; `control/torque.c: torque_limits, torque_clamp`; `safety/state_machine.c: st_run`; `hal/timer.h: hal_time_us64, hal_time_ms, ti_time64_extend`; `app/app.c: app_task_1ms` | `can_cmd: valid_frame_decoded, crc_bad_rejected, frozen_counter_goes_stale_after_20ms, counter_jump_rejected_then_resynced, bms_has_its_own_timeout, direction_interlock`; `torque: bms_limits_and_timeout_zero_regen, nan_torque_command_zeroed`; `time: can_freshness_across_the_microsecond_wrap`; `scenarios: stale_can_takes_torque_to_zero_not_held` (round 15: armed, a stale report is also a battery-path loss), `frozen_alive_counter_is_stale, nan_reference_never_reaches_pwm, running_across_the_microsecond_wrap_keeps_fresh_frames_fresh`; `state_machine: stale_command_ramps_then_zero_torque` | T-38 (DBC, DataIDs) |
| FW-12 | FS26: OTP/INIT readback at boot, Q&A watchdog (ERR_LIMIT 2, ≤ 3 ms window) before FS0B release, FS1B policy, GPIO1, LPOFF; (round 17, T-32) the answer first in the 1 ms task on every second task, 1890–2110 µs apart, inside the window at the fail-safe oscillator's ± 5 % | `safety/fs26.c: fs26_init, fs26_wd_due` (≥ 1500 µs), `fs26_wd_refresh, fs26_release_safety_outputs, fs26_request_fs0b, fs26_set_gpio1, fs26_goto_lpoff`; `app/app.c: app_task_1ms` (the answer first) | `fs26:` all 12 tests; `scenarios: watchdog_missed_drops_drv_en, fs26_is_answered_every_2ms_inside_its_window_at_both_oscillator_corners` | T-32…T-34 (answer, refresh cadence, MCU reset, OTP), T-25 (SPI) |
| FW-13 | Temperatures: module NTC open/short/rate, motor sensors, board NTCs; round 24: a channel whose conversions stopped is withdrawn (row F241); F243: a latched rate fault is never replaced by a later sample, an open or short read meanwhile recorded as `DTC_TEMP_OPEN_SHORT` (row F243) | `sense/temp.c: temp_ntc_c, temp_pt1000_c, temp_update` | `temp:` all 10 tests; `acq_fresh: a_rate_latched_module_channel_that_reads_open_or_short_stays_latched` | T-38 (sensor curves) |
| FW-14 | Gate power: RDY low ⇒ no PWM; flyback before DRV_EN | `safety/gate_power.c: gp_request_on, gp_step, gp_rdy_both` | `gate_power: refused_while_fs1b_asserted, start_ready_loss_and_recovery, start_timeout` | T-37 (RDY timings) |
| FW-15 | FLT_HS/FLT_LS: hardware PWM inhibit (FAULT0/2, fail-safe, manual clear, locked); bank to retained RAM + queued NVM; ≥ 1.5 ms reset, ASC_CLR, one-shot clear, confirm before FFLAG clear; one VCU-authorised retry; (A12-R05) no software MCU_GATE_EN drop within `cal_desat_en_hold_us` of a FLT; (F01/F02/F06) route bound, image read back, REG_PROT locked and EOL/HIL-validated before any arming | `app/app.c: app_isr_fault, fault_actions, init_evidence, evidence_watch`; `safety/fault_mgr.c: fm_desat, fm_retry_allowed, fm_retry_consumed, fm_retained_commit, fm_boot`; `safety/bridge.c: br_spo, br_service, br_rec_start, br_rec_step` (en_low / observe); `safety/arm_evidence.c`; `platform/s32k396/s32k396_pwm.c: hal_pwm_init` (lock), `hal_pwm_config_matches, hal_pwm_protection_locked, hal_pwm_fault_route_bound, hal_pwm_fault_clear`; `s32k396_board_cfg.h` | `fault_mgr: fw15_retained_first_nvm_only_queued, one_authorised_retry_after_1s_then_latch, boot_blocks_on_recent_desat_record, uncommitted_retained_record_requeued_at_boot, latched_reset_only_below_n_x_and_never_for_desat`; `bridge: fw15_recovery_waits_1p5ms_and_clears, fw15_recovery_fails_safe_on_a_hard_short, desat_hold_keeps_en_until_the_hold_then_drops_it, desat_hold_covers_br_rec_start_and_recovery_still_works, non_desat_spo_drops_en_at_once, fw15_low_wait_runs_on_the_bridges_own_clock` (round 18: the low wait on the bridge's own clock); `safe_state: row_flt_hs, row_flt_ls_is_spo_only`; `scenarios: flt_hs_at_speed_spo_then_reset_then_pwm_asc, flt_ls_at_speed_is_spo_only_even_on_overvoltage, desat_one_authorised_retry_then_latch, desat_at_speed_holds_en_through_the_hold_then_reset_and_pwm_asc, desat_at_low_speed_spo_waits_for_the_hold, desat_retry_waits_1s_across_the_microsecond_wrap, arming_refused_without_evidence_and_the_status_names_it, unbound_fault_route_never_arms, arming_permitted_with_a_valid_record, arming_refused_on_record_identity_or_crc_mismatch, watchdog_reset_relocks_before_rearming, evidence_lost_while_armed_goes_through_section6`; `state_machine: desat_retry_runs_at_reduced_torque`; `platform_cfg: fault_lock_image, regprot_lock_decision_needs_every_bit_read_back, fault_route_unbound_by_default, protected_registers_reject_writes_from_cpu_and_dma`; `calib: validation_record_binds_image_card_and_crc`; `params: round14_cal_defaults_and_ranges`; (round 16: the retry gate counts one ms more — floored stamps) `fault_mgr: one_authorised_retry_after_1s_then_latch`, `scenarios: desat_retry_waits_1s_across_the_microsecond_wrap` | T-01…T-05, T-15; T-37 (one-shot, the hold vs the measured soft turn-off) |
| FW-16 | Boot self-test steps a–h through DRV_EN_RB / ASC_CMD_RB; energy precondition ≤ 0.1 J; stored pass; EN drops through the bridge (DESAT hold) | `safety/gate_selftest.c: st_conditions, st_energy, st_step` (EN drops via `br_spo`); `app/app.c: selftest_step` (run or stored pass) | `gate_selftest: healthy_chain_passes_all_steps, each_term_stuck_permissive_is_detected, energy_precondition, topup_then_run_and_not_counted, skipped_rather_than_run_on_assumption, failed_test_with_a_desat_drops_en_only_after_the_hold`; `state_machine: selftest_and_precharge_refusals`; `scenarios: boot_across_the_microsecond_wrap_reaches_armed` | T-16, T-17, T-37 (chain timings) |
| FW-17 | QDIS only with contactors reported open, auto-release 5 s, ≤ 3 per 5 min | `discharge/discharge.c: dis_request, dis_step` | `discharge: fires_only_with_contactors_reported_open, auto_release_after_5s, three_per_five_minutes_and_uncounted_topup`; `state_machine: discharge_only_with_contactors_open`; `scenarios: key_off_discharge_witnessed_then_lpoff` | T-37; the resistors' thermal limit is a hardware gate (contract §11) |
| FW-18 | Witness on both channels; stuck-off in 200 ms; invalid witness ⇒ HV UNKNOWN; stuck-on / unexpected discharge ⇒ latched service lock, "do not re-energise" + "open the contactors" (round 14), cleared only by FW-32 | `discharge/discharge.c: witness, stuck_on_watch` (not while modulating), `dis_hv_state`; `app/app.c: service_lock, init_service_lock`; `nvm/nvlog.h: nv_service_t` | `discharge: stuck_off_detected_in_200ms, invalid_witness_means_unknown_never_safe, stuck_on_seen_as_fast_decay_with_qdis_off, unexpected_discharge_judged_only_with_nothing_drawing_on_the_link`; `scenarios: qdis_stuck_off_detected, stuck_on_qdis_latches_service_required_and_never_rearms` | T-35 (the product key of the FW-32 routine) |
| FW-19 | Precharge plausibility: plateau ≈ 5 % low or τ too short refuses arming | `discharge/discharge.c: pch_step` | `discharge: fw19_precharge_plateau_and_tau`; `scenarios: precharge_plateau_5pct_low_refuses` | T-37 (precharge τ) |
| FW-20 | Versioned, CRC- and range-checked calibration tied to serial, SKU and motor ID; (round 16) layout 2: the resolver record carries the monitor chain's gain (codes per V pp at the monitor plane); (round 23) layout 4: FW-45's saturation maps and FW-46's ripple table (rows FW-45, FW-46) | `nvm/calib.c: calib_check, calib_seal`; `nvm/nvlog.c`; `app/app.c: init_calibration` | `calib: nominal_needs_a_motor, each_failure_detected` (layout 1 refused); `nvlog:` all 4 tests; `scenarios: brownout_during_nvm_write_never_blocks` | T-07, T-25 (Fee), T-27 (UID) |
| FW-21 | Signed images, rollback, no-torque update state | implemented as FW-38 (round 23, contract §10f; round-23 fix 12): see the FW-38 row — `boot/verify.c`, `boot/update.c`, `boot/uds_update.c`, `boot/boot.c`, `app/app.c: update_conditions, update_enter` | `update:` (FW-38's row) | T-44…T-50 |
| FW-31 | Current-loop liveness (round 16, named in round 17): the 1 ms task sees a current-loop ISR older than `cal_isns_stale_us` ⇒ currents lost, resolver aged; (round 18) a signed age — an ISR that preempted the task after its time read is alive | `app/app.c: app_task_1ms` | `scenarios: a_stopped_current_loop_is_caught_by_the_task, a_current_loop_preempting_the_task_is_not_a_dead_loop` | T-22 (ISR rate) |
| FW-32 | Service-lock clear (round 17): UDS SecurityAccess 0x27 (seed/key, build-time key hook — none by default ⇒ refused; one key per seed; three invalid keys lock out until the MCU restarts) and RoutineControl 0x31 start 0xF010; refused with HV present/unknown or the bridge armed; NVM record rewritten CLEARED + DTC; effective at the next power-up | `comms/uds.c: uds_handle`; `app/app.c: service_clear, diag, diag_seed`; `nvm/nvlog.h: NV_SERVICE_CLEARED` | `uds: security_access_refused_without_a_key_function, seed_key_unlocks_one_routine_run, wrong_keys_lock_out_and_malformed_requests_are_refused`; `scenarios: service_lock_clear_is_refused_without_a_key, service_lock_clear_is_refused_with_hv_present_or_armed, service_lock_clear_with_the_key_takes_effect_at_the_next_power_up` | T-26 (diagnostic RX), T-35 (the product key) |
| FW-33 | LV supply supervision (round 17, the let-through LV entry): VSUP through the FS26 AMUX (VSUP / 14, set at every boot); an overvoltage (> 20 V, the FS26's VSUPOV) is information — RUN, the torque unchanged, `DTC_LV_OVERVOLTAGE` stamped over the event — for `cal_vsup_ld_ms` above `cal_vsup_jump_max_v` (IR-03 test B) and `cal_vsup_jump_ms` at or below it (IR-02 jump start); longer ⇒ sustained: `DTC_LV_OV_SUSTAINED` and the §6 command-lost ramp until VSUP is back; no reading ⇒ `DTC_LV_VSUP_UNKNOWN` | `sense/vsup.c: vsup_update`; `safety/fs26.c: fs26_init` (M_AMUX_CTRL); `app/app.c: sense_slow, detect` (the command-lost row, the DTCs) | `scenarios: lv_load_dump_35v_for_400ms_is_information_not_a_fault, lv_overvoltage_beyond_its_band_takes_the_orderly_ramp, lv_24v_jump_start_is_information_for_its_60s` | T-39 (the AMUX reading, the profiles on the bench), T-37 (the bands vs the OEM's profiles) |
| FW-34 | Sample freshness judged at or after the acquisition (round 18, A16-R01): each check uses a time read after its reads; the age is signed (a stamp after the check by less than the hold is fresh, the hold or more before or after it stale); the ISR's entry time stays its own (liveness, WCET, the FOC angle, the bridge); the task's liveness check signed too; the FW-15 recovery timer on the bridge's own clock (a fault stamp can postdate the task's time) | `include/ti_types.h: ti_stale`; `app/app.c: sense_fast` (check times, V_DC read last), `app_isr_current` (the two times), `app_task_1ms` (liveness), `recovery`; `sense/current.c: isns_update`; `sense/vdc.c: vdc_update`; `control/resolver.c: rslv_age`; `safety/bridge.c: br_rec_step` (own time); `platform/host/sim_hal.c: sim_adc_read_delay_ns`, `sim_fs26_xfer_hook` | `time: sensor_stamps_are_judged_with_a_signed_age`; `current: a_triplet_stamped_after_the_check_time_is_fresh`; `vdc: a_channel_stamped_after_the_check_time_is_fresh`; `resolver: a_frame_newer_than_the_check_time_is_not_aged_out`; `bridge: fw15_low_wait_runs_on_the_bridges_own_clock` (also across the µs wrap); `scenarios: samples_stamped_after_the_isr_entry_stay_fresh` (1 / 5 / 50 µs per read, across the µs wrap), `a_run_at_speed_with_the_adc_reads_taking_time`, `a_current_loop_preempting_the_task_is_not_a_dead_loop`, `fw15_low_wait_counts_from_a_fault_that_preempted_the_task` | T-36 (the ISR's reads within its WCET) |
| FW-35 | Resolver time base independent of interrupt latency (round 18, A16-R02) and its origin independent of every completion (round 19, A17-R01): block k starts at t_org + (k − k_org)·T on the SWG-start cadence, t_org the later of two 64-bit time reads around the SWG enable (PRIMASK), uncertain by ± u = the bracket + 1 µs + `cal_swg_start_lat_us`; no completion sets or moves it, an unanchored ring counts nothing; every completion judged within [−u, `cal_sd_irq_lat_max_us` + u] (first) / [−u, T/2 + u] (later) of its block's end, else the ring breaks (early included); the reader refuses a slot the cadence says may be rewritten; a broken ring re-syncs from the clock with the DMA positions only confirming it — a DMA out of phase is `lost` (also the platform's flags), down until the synchronized producer restart (≤ `cal_rslv_restart_max` per key cycle, retained across an MCU reset); each restart and re-acquisition one occurrence of `DTC_RSLV_REACQUIRED`, no §6 row | `hal/sdadc_ring.c: hal_sd_ring_anchor, hal_sd_ring_complete` (the absolute windows, `resync`, `ring_break`, `ring_lost`, the re-base), `hal_sd_ring_read` (the cadence check); `hal/sdadc.h`; `platform/s32k396/s32k396_resolver.c: hal_swg_start` (the bracket and the anchor), `hal_sdadc_init, hal_sdadc_restart, hal_sdadc_lost, s32k_sdadc_dma_irq` (64-bit time, the lost flag); `platform/host/sim_hal.c` (the same, the SWG-triggered DMA model, `sim_swg_start_latency_ns`); `app/app.c: sense_slow` (the restart and the DTC), `key_cycle_init` (the budget); `app/app.h: app_session_t`; `comms/dtc.h: DTC_RSLV_REACQUIRED`; `tools/gen-params.mjs: cal_sd_irq_lat_max_us, cal_swg_start_lat_us, cal_rslv_restart_max` | `sdadc:` the 10 round-19 tests (`the_first_completion_is_judged_against_the_swg_start, the_swg_start_latency_stays_inside_the_declared_uncertainty, a_constant_completion_delay_is_never_published, a_rejected_delay_is_never_absorbed_after_a_break, a_channel_paused_one_period_is_lost_not_taken_for_a_late_one, a_late_interrupt_inside_the_origins_uncertainty_is_no_phase_loss, an_early_completion_breaks_the_ring, the_origin_and_the_clock_index_hold_across_the_32bit_wrap, the_anchor_refuses_an_origin_too_uncertain, nothing_counts_before_the_swg_start`), the 7 round-18 tests (`interrupts_held_off_within_the_deadline_keep_every_stamp_exact, an_interrupt_held_off_past_the_deadline_is_rejected_then_reacquired, interrupts_held_off_for_a_lap_are_detected_never_fresh, one_channel_lapping_while_the_others_do_not, all_three_stalled_together_are_detected_and_reacquired, the_reader_preempted_across_a_lap_never_returns_the_frame, stamps_are_exact_across_the_microsecond_and_epoch_wraps`), `lost_samples_keep_the_ring_down_until_a_restart` and the 6 round-16 tests (every stamp within ± u); `params: round18_cal_defaults_and_ranges, round19_cal_defaults_and_ranges`; `scenarios: a_late_resolver_interrupt_at_speed_is_counted_and_reacquired` (SiC, IGBT), `a_late_completion_after_a_break_never_dates_the_angle, resolver_producer_restarts_are_bounded_per_key_cycle, a_frozen_resolver_channel_is_never_read_as_fresh, resolver_frames_stopping_withdraws_the_angle_at_the_hold` | T-40 (the latency from the carrier boundary, the cadence vs the STM), T-41 (the SDADC/eDMA flags wired to `lost`), T-42 (the SWG start latency, the first block's phase), T-30 (the period a whole 100 µs), T-28 (the lost flag) |
| FW-36 | Resolver chain latency added with its physical sign (round 18, A16-R03): θ(now) = θ_block + ω·((now − t_ref) − t_mid + L), L > 0 = the reported angle lags | `control/resolver.c: rslv_theta_e_at`; `tools/gen-params.mjs: cal_rslv_latency_us` (description) | `resolver: latency_compensation_matches_the_true_angle_at_now` (an independent rotor oracle: ± direction, 3000 / 10 000 rpm, 25 / 50 µs), `acquires_at_speed_after_a_reset` (corrected) | T-37 (the HIL measurement's sign convention) |
| FW-37 | Torque solved jointly with the voltage, current-circle and demagnetisation constraints (round 23, A20-F03/F04 of the A.21 rechecks): the returned current vector lies on the torque curve of the torque it reports; MTPA by bisection on i_d (convex current on the curve); field weakening along the curve to the voltage boundary nearest MTPA; torque reduced by bisection only when nothing fits; a postcondition (torque sign/band, voltage, current, demagnetisation) before every return, TQ_POSTCOND → DTC_TORQUE_POSTCOND; INV_STATUS 20 B with the applied torque in b4–5 | `src/control/torque.c` (`torque_to_current`, `torque_from_current`), `src/app/app.c` (TQ_POSTCOND handling), `src/comms/can_cmd.c` | `tests/test_torque.c` (10 new tests: the reviewer's eight four-quadrant cases, MTPA vs a bracketed optimum, monotonicity, sign/zero, LUT consistency, non-finite, postcondition), `tests/test_scenarios.c` (salient motor end to end, current-limited case, postcondition fault at 1000 / 10 000 rpm), `tests/test_can_cmd.c` (20-byte frame) — 8 mutations caught; 300 tests / 2802 checks / 0 failed in host, −O2, ASan/UBSan | T-05 / T-06 (the image identity the record binds to), T-40 (the 1 ms task budget with the solver: ≈ 65 µs worst case) |
| FW-38 | Firmware update (round 23, gap 1; implements FW-21): a signed container (Ed25519 over the header + SHA-256 of the payload, target = SKU, security version); one check for the application and the bootloader (signature, target, security version >= the counter, hash; distinct refusals); the update state entered by UDS only disarmed, discharged and at standstill, withdrawing the validated arming evidence (no arming, no torque); 0x10/0x34/0x36/0x37/0x31/0x11 with block-sequence and length checks; a write-ahead boot record (anti-rollback counter, last known good) in NVM; install, trial, self-confirmation after 5 s, commit + counter; rollback on an interrupted swap, an unconfirmed first boot or an invalid EXEC | `src/boot/image.h`, `verify.c: img_parse, img_verify, img_pubkey`, `ed25519_tweetnacl.c: ed25519_verify` (vendored TweetNaCl + S < L), `sha256.c`, `update.c: upd_enter … upd_service`, `uds_update.c: upd_uds_claims, upd_uds`, `boot.c: boot_decide, boot_rec_read, boot_rec_queue`; `src/hal/flash.h`, `platform/host/sim_flash.c`, `platform/s32k396/s32k396_flash.c` (stub); `src/comms/uds.c` (one dispatch), `src/app/app.c: update_conditions, update_enter` (+ `upd_init`, `upd_service`); `nvm/nvlog.h: NV_REC_BOOT`; `tools/sign-image.mjs` | `tests/test_update.c` (16 tests: RFC 8032 / FIPS 180-4 vectors, the tool's container byte for byte, every refusal, a fuzzed parser and every header bit flip, install/trial/commit, a power cut at every write of the swap and of the commit, torn record writes, an unconfirmed first boot, EXEC corrupted, the UDS protocol, refused while armed / HV present / moving, the state cannot arm, end to end over UDS) — 14 mutations caught | T-44…T-50; second pass: DID 0xFD23 (root of trust kind + key id, `img_root()`), tests/test_uds_diag.c the_root_of_trust_is_reported_with_the_test_key_id (the TEST key id pinned) |
| FW-39 | Motor self-commissioning in an interlocked service mode (round 23, gap 2; contract §10g): entered only through the FW-32 SecurityAccess (one start per unlock) with the tool's rig attestation (`LK` / `DF` / `DR`), armed through the normal path (ARMED_ZERO_TORQUE, the FW-24 evidence and the FW-20 record behind it), no §6 row, no active DTC, a fresh VCU command without enable, the VCU's vehicle speed valid and zero, V_DC in the SKU window with the battery proven, the routine's motor speed, the service CALs in range; the same every 1 ms plus the tool's heartbeat, the locked rotor's motion, the dyno's steadiness, a current bound, no voltage saturation, the schedule — any loss aborts to the normal safe state (`DTC_MC_ABORTED`); no torque command accepted (the enable hidden from the state machine until withdrawn); R_s by two DC levels along phase U's axis (ΔV/ΔI), L_d/L_q by a sinusoidal current reference on a DC bias (2 × 2 impedance, delay and hold undone, eigenvalues in the zero's frame, AXES), ψ, the electrical zero and the resolver direction from the back-EMF at i_d = i_q = 0 on the dyno (DIR_PHASES, DIR_DYNO); value + uncertainty + verdict; staged only inside the FW-20 class and its band of the record or after a confirming run; written only by RoutineControl 0xF021 through `calib_seal`/`calib_check` as a new NV_REC_CALIB version, effective at the next key cycle | `src/app/commission.c` (`start, preconditions, watch, mc_abort, plan, mc_isr_refs, mc_isr_sample, est_rs, est_ldq, ldq, est_psi, emf, judge, class_ok, commit, mc_uds_handle, mc_torque_barred, mc_cal_validate`); `src/app/app.c` (`app_init, diag, control_fast, app_task_1ms, gather, torque_path`: the hooks); `src/comms/can_cmd.c` (VCU_CMD vehicle speed); `src/comms/dtc.h`; `src/platform/host/sim_pmsm.c` (the virtual PMSM); `tests/harness.c` (`H.plant`, `H.veh_speed_*`) | `commission:` `service_cals_are_range_checked, the_routines_recover_the_plant_on_sic_and_igbt` (R_s, L_d, L_q ≤ 0.17 %, ψ ≤ 0.15 %, the zero ≤ 0.75 mrad on 8XX SiC and 4XX IGBT; stated 1 / 1 / 1 / 0.5 % / 3.5 mrad), `ld_lq_axes_come_from_the_zero_and_a_wrong_one_is_reported, a_change_beyond_its_band_needs_a_confirming_run, the_record_is_written_only_through_fw20_after_confirmation, every_precondition_refuses_the_start` (18 refusals), `a_running_routine_aborts_on_every_loss` (10 losses), `no_torque_command_is_accepted_in_service_mode, the_resolver_direction_is_checked_against_the_phases_and_the_dyno, a_value_outside_the_fw20_class_is_never_staged` (also the CURRENT abort at the default margin) — 10 tests, 266 checks; 21 mutations caught (below) | T-51 (the CALs on the real machine and rig), T-52 (L_d/L_q on the real inverter), T-53 (the zero against an encoder, ψ and R_s against terminal measurements), T-54 (the DBC signal, the tool's procedure), T-35 (the product key) |
| FW-40 | Diagnostic services (round 23, gaps 3 and 6; contract §10h): UDS 0x19 — 01/02 by status mask (AND 0x7F), 0A every DTC, 04 snapshot records = the NVM fault ring newest first with the operating context FW-40 appended to `nv_fault_t`, two ring records per task, restarted once if the ring moves; 0x22 — 12 DIDs of telemetry and identity (round 23: + FW-46's 0xFD46), each copied between two current-loop ISRs; 0x2A — the telemetry DIDs at 100/10/1 ms, at most one frame per task and none beside a response frame, a busy mailbox drops it; 0x14 — gated as FW-32 (SecurityAccess, one clear per unlock, HV absent, disarmed), the DESAT class, the service lock and the key cycle's no-arming failures kept; ISO 15765-2 on CAN-FD with segmented responses under the tester's flow control; DTC number 0xD10000 \| id, the number/description table generated from `dtc.h` | `comms/uds_diag.c` (`uds_diag_rx`, `uds_diag_tick`, `read_dtc_info`, `scan_step`, `read_dids`, `DIDS`, `did_copy`, `periodic_rq`, `periodic_step`, `clear_dtcs`, `kept`, `tp_step`, `send_usdt`, `flow_control`); `comms/uds.c: uds_handle` (the dispatch line); `app/app.c: diag` (the tick), `ctx_now` (the operating context); `app/app.h: app_t.udsd`; `comms/dtc.c: dtc_clear`; `nvm/nvlog.h: nv_fault_ctx_t`; `safety/fault_mgr.c: fm_desat`; `tools/dtc-table.mjs`, `comms/dtc_table.h` (generated); `platform/host/sim_hal.c: sim_can_tx_busy` | `uds_diag:` `every_dtc_has_a_number_and_a_description`, `dtc_count_and_lists_by_status_mask_follow_injected_faults`, `snapshot_records_are_the_stored_fault_records`, `a_record_written_during_the_scan_is_never_answered_twice`, `dids_read_the_live_state`, `clear_is_gated_like_the_service_routine`, `permanent_latches_are_not_cleared`, `malformed_requests_are_refused_and_a_bounded_fuzz_changes_nothing`, `segmented_responses_follow_the_testers_flow_control`, `periodic_dids_are_rate_limited_to_one_frame_per_task`, `the_stream_changes_no_control_output_and_no_task_timing` — 11 tests, 649 checks; 21 mutations caught ("Round 23 — FW-40" below) | T-26 (the flow control on the request ID, the periodic ID, the one TX mailbox), T-36 (the diagnostic path's WCET) |
| FW-41 | Sampled-waveform capture, read-only (round 23, gap 4; contract §10i): a static ring of `CAP_N` records (2048 × 32 B) written last in the current-loop ISR from values it already holds (no conversion, no wait, no loop); triggers: a new DTC occurrence or a new §6 row, the command, a level on one channel with hysteresis, a sources mask; the pre-/post-trigger split set with every arm; frozen until re-armed; armed at every start-up; read-out over UDS (`22 FD 40`, `22 FD 41`, `2E FD 41`, `31 01 F0 41`, `31 01 F0 42`) in blocks carrying their number and the capture id; the host decoder to CSV | `diag/capture.c: cap_init, cap_isr` (`q16`, `qang`, `check`, `level`), `cap_arm, cap_trigger, cap_status, cap_image_read`; `diag/uds_capture.c: uds_capture_handle`; `comms/uds.c: uds_handle` (the dispatcher line); `comms/dtc.c: dtc_events`; `app/app.c: app_init, app_isr_current` (the two calls); `tools/capture-decode.mjs` | `capture:` all 15 tests — unit (every channel's scale and corners, the split at 0 / 1 / 777 / 2047, an early trigger, the command, the level's hysteresis, the sources mask, 60 random captures, the host cost), UDS (framing, sequence, seek, re-arm, refusals), the Node decoder round trip and its refusals, end to end (a DESAT, an over-current, an over-voltage, a resolver loss at the split with the 160 ISRs around each, the read-out over the bus under torque); 12 mutations caught; 15 tests / 180 checks in the host, −O2 and ASan/UBSan builds | T-43 (the copy's cost on silicon, the ring in the linker map, the CAN-FD transmit buffer of 0x7E9), T-36 |
| FW-42 | Overspeed protection (round 23, gap 5): the resolver-valid speed (never a held one), both directions, against the calibration record's `n_max_rpm` every 1 ms; warning band (`cal_ovs_warn_frac`) ⇒ the §6 command-lost row (torque ramped to zero; SPO below n_x, current control above it), the speed-limit request (b14.3), no arming; trip band (`cal_ovs_trip_frac`) ⇒ the §6 "Resolver invalid, or control lost" row, latched (SPO below n_x under the energy rule, LS-ASC at or above it; the VCU reset below n_x); `cal_ovs_debounce_ms` in and out, `cal_ovs_hyst_frac` hysteresis; `DTC_OVERSPEED` one occurrence per event | `safety/overspeed.c: ovs_step, wanted`; `app/app.c: overspeed` (before `detect`), `detect` (cmd_lost), `arming` (the refusal), `torque_path` (speed_limit_req); `comms/dtc.h: DTC_OVERSPEED`; `tools/gen-params.mjs: cal_ovs_*` | `fw42_44: overspeed_warning_trip_recovery_with_hysteresis, overspeed_debounce_in_ms_and_no_evidence_without_a_valid_resolver, overspeed_warning_zeroes_the_torque_and_requests_a_speed_limit, overspeed_trip_takes_the_control_lost_row_at_high_and_low_speed, a_single_bad_resolver_sample_never_trips_overspeed, overspeed_refuses_arming_while_active, round23_cal_defaults_and_ranges` — 9 mutations caught (README, FW-42 / FW-43 / FW-44) | T-37 (the bands against the motor's n_max and the driveline) |
| FW-43 | Run-time statistics (round 23, gap 7; no safety relevance): energy drawn / returned (V_DC·I_DC, I_DC from the loop's v_dq·i_dq, while modulating), key-on and RUN/DERATE time, the highest module / coolant / motor temperatures, DTC occurrences per class, key cycles (the existing counter); no distance (no vehicle-speed or wheel signal in the contract); the run-time record every `cal_rs_save_s` and once on entering SAFE_POWERDOWN — an unclean shutdown loses the interval since the last record (documented caveat); read-only DID 0xFE43 | `nvm/runstats.c: rs_step, rs_count_dtcs, rs_persist, rs_did_read, rs_uds_handle`; `nvm/nvlog.[ch]: NV_REC_RUNTIME` (post-ring A/B pairs); `app/app.c: run_stats, init_runtime, diag` (the dispatcher line); `tools/gen-params.mjs: cal_rs_save_s` | `fw42_44: runstats_energy_time_and_maxima_integrate_exactly, runstats_dtc_occurrences_count_per_class, runstats_record_is_saved_at_a_bounded_rate_and_once_at_shutdown, runstats_accumulate_a_scripted_drive, runstats_persist_across_a_key_cycle_and_lose_the_last_interval_on_power_loss, runstats_did_answers_only_its_own_read` — 3 mutations caught | T-25 (the record's Fee blocks) |
| FW-44 | Key-on current-offset refresh (round 23, gap 9): the key-on check against the EOL record unchanged (refusal beyond `cal_isns_offset_tol_v`); inside it the working offset (conversion and FW-05 compare) steps toward the fresh mean by ≤ `cal_isns_ofs_step_v`, once per key cycle, disarmed, PWM off, at standstill (< n_ss); persisted, bound to the calibration record's CRC; the EOL record never modified | `sense/offtrack.c: ofs_init, ofs_decide`; `app/app.c: offset_refresh, init_runtime, sense_fast` (the working offsets), `set_watchdogs` (the FW-05 compare) | `fw42_44: offset_tracker_adopts_a_bounded_step_inside_the_tolerance_only, offset_refresh_at_key_on_rate_limited_per_key_cycle, offset_outside_the_tolerance_still_refuses_and_adopts_nothing, no_offset_adoption_while_moving_or_armed` — 7 mutations caught | T-37 (`cal_isns_ofs_step_v` against the sensors' drift data) |
| FW-45 | Saturation-dependent inductance (round 23, gap 8; contract §10l): the record's L_d / L_q maps (the apparent inductance at 0, 0.2 … 1.0 × the SKU's current limit; layout 4; the shape at the scalars' level; `calib_check`: range, non-increasing, last ≥ 0.3 × first, the full scale the SKU's; round 24: the model's flux rising, (k + 1)·L_k − k·L_(k−1) ≥ 0.25·L_0 at every breakpoint — refused at the power-up and the FW-39 commit); λ_d = ψ + L_d(\|i_d\|)·i_d, λ_q = L_q(\|i_q\|)·i_q in the torque, the ellipse, the circle, the demagnetisation limit and the postcondition; the solve on the maps' contour (exact contour, slope from the differential inductances, a five-point sign scan per argmin, the ψ_e guard from the maps' extremes; bounded; a flat map is FW-37 bit for bit); the current loop's k_p scheduled by the differential inductance (floor 0.3), the speed voltages and FW-10's speed model from the maps; FW-39's L_d/L_q routine at a bias index (0x40+k / 0x50+k, 0x02, the flags' bias bit), a whole map committed through FW-20 | `control/motor.h: motor_sat`; `control/torque.c: torque_from_current, torque_v_required, hyp_iq_sat, hyp_slope_sat, hyp_cost, hyp_argmin, torque_to_current`; `control/foc.c: foc_step, foc_omega_model`; `nvm/calib.c: calib_nominal, map_ok`; `app/commission.c: map_bias, plan, est_ldq, judge_point, results, commit, mc_uds_handle`; `platform/host/sim_pmsm.c` (saturation) | `fw45_46: a_flat_map_is_the_scalar_solve_bit_for_bit, the_saturated_solve_meets_the_references_on_random_saturating_maps, the_psi_e_guard_keeps_the_contour_defined_on_weak_magnets, the_record_is_layout_4_and_a_bad_map_is_refused, the_map_delivers_the_torque_of_a_saturating_machine_at_80_percent_current, gain_scheduling_keeps_the_current_loops_margin_on_a_saturating_axis, the_routine_at_six_biases_measures_and_commits_the_map, the_bias_byte_is_checked_and_a_map_is_committed_whole, the_bias_runs_on_the_true_axes`; round 24: `the_model_flux_must_rise_with_the_current, an_unsupported_map_is_refused_at_the_commit_and_at_power_up`; every earlier suite unchanged | T-57 |
| FW-46 | Torque-ripple feed-forward (round 23, gap 10; contract §10m): the record's 36-point i_q table (10° el, 0.01 A, ±30 A; layout 4); applied in the current-loop ISR at the FOC's angle less the table's mean, clamped to `cal_ripple_ff_max_a` (0 = off, the default), only for a solved vector the bridge modulates for torque (no §6 decision, zeroing or commissioning), the resolver valid and 6·f_e < `cal_ripple_ff_fmax_hz`, scaled down (never the vector) to keep the circle and the ellipse (round 24: for every i_q between the table's extremes — the interval cut at the L_q breakpoints and 0, each piece bounded in closed form); DID 0xFD46 write (one ISO 15765-2 request) staged under FW-39's interlocks, committed by RID 0xF021, read back by 0x22 | `control/torque.c: torque_ripple_at, torque_ripple_scale, ff_span_fits, ff_piece_max`; `app/app.c: init_calibration, torque_path, control_fast`; `app/commission.c: mc_ripple_write, mc_ripple_read, commit`; `comms/uds_diag.c: ripple_rq, ripple_write, rx_dispatch, did_ripple`; `tools/gen-params.mjs: cal_ripple_ff_max_a, cal_ripple_ff_fmax_hz`; `platform/host/sim_pmsm.c` (cogging) | `fw45_46: the_ripple_feed_forward_cancels_the_cogging_at_100_rpm, the_ripple_table_write_is_interlocked_and_range_checked, the_feed_forward_never_leaves_the_solved_margin, with_the_default_cal_a_table_changes_nothing`; round 24: `the_scale_holds_every_i_q_between_the_extremes, the_scale_holds_every_i_q_on_20000_physical_maps` | T-58 |
| R23-1 | FW-13 rate check over a window (round-23 fix 1, §10k): the mean over `cal_temp_rate_win_ms` against the last accepted mean, judged once it moved more than `cal_temp_rate_db_codes`; open/short per sample, the latch after three implausible windows unchanged | `sense/temp.c: update_one, accept`; `include/ti_params.h`, `tools/gen-params.mjs: cal_temp_rate_win_ms, cal_temp_rate_db_codes` | `temp: one_code_steps_and_a_slow_rise_never_trip_the_rate_check, a_fast_ramp_a_step_open_and_short_still_trip, the_deadband_holds_a_slow_swing_of_a_few_codes, open_short_rate_plausibility`; `r23_fixes: module_temperatures_rising_through_the_derating_band_keep_their_channels` | T-37 (the window and deadband against the sensors' noise) |
| R23-2 | Field weakening from the measured link (fix 2): current control at zero torque when ω_e·ψ ≥ (1 − `cal_fw_emf_margin_frac`)·v_available(V_DC), with the unknown speed and the bound ≥ n_x as before; the §6 rows on n_x | `app/app.c: emf_needs_control, torque_path` | `r23_fixes: zero_torque_below_n_x_takes_the_back_emf_off_the_diodes, the_take_over_follows_the_measured_link`; `tool/bridge` check (7 500 rpm, 750 V, 0 N·m: pack current within 5 A) | T-37 (the margin on the dyno) |
| R23-3 | Torque-command slew (fix 3): a fresh command at `cal_torque_slew_nm_s` both ways; §6 zeroing and the solver's refusals immediate; the FW-11 ramp unchanged | `app/app.c: torque_path` | `r23_fixes: a_torque_reversal_at_10000_rpm_is_slewed_and_never_trips, the_fault_paths_still_zero_the_torque_at_once` | T-37 (the slew against the driveline) |
| R23-4 | Every declared DTC set where its condition is detected (fix 4): DESAT pending at boot, the sensor self-test and FS0B-release timeouts, the key-on offset, the module/board/motor sensor groups (with passes), the over-temperature; a DESAT record that blocks arming at a power-up names itself | `app/app.c: selftest_dtcs, temp_dtcs, dtc_report, sense_fast` (the offset), `app_init` (the block); `safety/fault_mgr.[ch]: fm_boot, fm_t.desat_bank`; `platform/host/sim_fs26.c: flt_err_stuck` | `r23_fixes: a_desat_pending_at_boot_is_recorded, a_desat_recorded_before_the_power_up_names_the_fault_it_blocks, a_sensor_self_test_timeout_is_recorded, a_current_offset_beyond_its_tolerance_is_recorded, an_fs0b_release_timeout_is_recorded, board_motor_and_over_temperatures_are_recorded_and_pass`; `commission: a_running_routine_aborts_on_every_loss` (a real board NTC open) | — |
| R23-5 | The LS-ASC entry transient (fix 5): inside `cal_asc_oc_window_ms` of an entry an over-current is `DTC_ASC_OC_TRANSIENT`, no row; the compare re-armed after the window (FAULT1 only without an OV flag); outside it the latched fault; the low sides proven to hold (FAULT1 → high sides only) | `app/app.c: asc_transient, over_current, asc_oc_rearm`; `safety/bridge.[ch]: br_enter_pwm_asc` (`t_asc_us`); `comms/dtc.h: DTC_ASC_OC_TRANSIENT` | `r23_fixes: an_asc_entry_transient_is_information_and_the_low_sides_hold, an_over_current_outside_the_asc_window_is_still_the_fault, an_over_voltage_asc_keeps_its_fault_flag_through_the_transients_rearm` | T-08…T-10 (FAULT1 mapped to the high sides on silicon), T-37 (the window against the ASC entry peak, R8X-13) |
| R23-6 | The speed after a resolver fault (fix 6, §6): the bound \|n_last\| + `cal_speed_accel_max_rpm_s`·t in every n < n_x decision, unknown past n_max (replaces `cal_speed_hold_ms`) | `app/app.c: app_speed_hi_rpm, sense_slow, ctx_now, gather, execute, arming, emf_needs_control, torque_path` | `r23_fixes: a_resolver_loss_is_decided_on_the_speed_bound, the_speed_is_unknown_once_the_bound_passes_n_max`; `scenarios: battery_path_loss_while_armed_at_every_speed` (the resolver-lost case) | T-37 (a_max on the vehicle) |
| R23-7 | The resolver block dated at the demodulator's own centroid (fix 7): T(N−1)/(2N) − T·sin(2ref − 2π/N)/(2N·sin(2π/N)) — 7.7 µs after the samples' mean at −24° | `control/resolver.c: centroid_us, rslv_update` | `resolver: the_block_angle_is_referred_to_the_demodulators_own_centroid, latency_compensation_matches_the_true_angle_at_now`; `r23_fixes: the_angle_the_current_loop_uses_is_the_rotors_at_speed` | T-37 (`cal_rslv_latency_us`: the chain beyond the centroid) |
| R23-8 | FW-10's debounce holds (fix 8): one out-of-window frame is one count; a frame beyond the tracking limit or `cal_rslv_debounce` × the acceleration threshold is kept out of the observer (one count, the hold not renewed); the latches after `cal_rslv_debounce` consecutive frames | `control/resolver.c: observer, rslv_update`; `app/app.c: detect` (the DTC named) | `resolver: one_frame_outside_the_amplitude_window_is_one_count, one_frame_degrees_off_is_kept_out_of_the_observer, persistent_offsets_and_implausible_accelerations_still_latch, frames_kept_out_do_not_renew_the_hold`; `r23_fixes: a_single_corrupt_resolver_frame_does_not_withdraw_the_resolver`; `fw42_44: a_single_bad_resolver_sample_never_trips_overspeed` | — |
| R23-9 | NVM slot check a CRC-32C over header + payload (fix 9); the legacy CRC-32 slots readable, replaced by the next write | `nvm/nvlog.c: rec_crc, slot_valid`; `util/ti_crc.c: ti_crc32c`; `platform/host/sim_hal.c: sim_nvm_power_loss_bytes` | `nvlog: a_torn_calib_write_never_resurrects_the_record_two_writes_back, a_torn_write_of_a_record_ending_in_its_own_crc_never_reads_back_mixed, slot_check_is_a_crc32c, legacy_slots_stay_readable_and_are_replaced_in_the_new_format` | T-25 |
| R23-10 | 64 NVM slots (fix 10), the existing slots unmoved | `hal/nvm.h: HAL_NVM_SLOTS`; `nvm/nvlog.h: NV_SLOTS_USED` | `nvlog: every_record_has_its_own_slots_and_the_map_has_room` | T-25 (64 Fee blocks), T-50 |
| R23-11 | FW-38 TransferData over ISO 15765-2 (fix 11): the receiving side (flow control BS 4, STmin 0, N_Cr 1 s, programming services only), `UPD_BLOCK_MAX` 4095 | `comms/uds_diag.c: uds_diag_rx, rx_first, rx_consecutive, rx_flow_control, rx_dispatch, uds_diag_tick`; `boot/update.h`; `boot/uds_update.[ch]`; `app/app.c` (the static assert) | `update: a_1_mib_image_downloads_over_iso_tp_in_seconds, segmented_requests_are_received_under_iso_15765_2, programming_services_follow_the_protocol, an_update_over_uds_installs_confirms_itself_and_commits`; `uds_diag: malformed_requests_are_refused_and_a_bounded_fuzz_changes_nothing` | T-26 (the RX FIFO takes a block of 4), T-45 |
| R23-12 | FW-21's rows point to FW-38 (fix 12) | contract §10; `README.md`; this matrix | — | — |
| R23-13 | The tool's bench (fix 13): an ISO 15765-2 tester, the periodic frames, `dtc_clear` through 0x14 with the bench key, the VCU vehicle speed | `tool/bridge/bridge.c`, `check.c`; `tool/protocol/generate.mjs`; `tool/PROTOCOL.md` | `make -C tool/bridge check`; `node tool/protocol/generate.mjs --check` | — |
| R23-14 | Counts (fix 14): 188 fields, 89 CAL rows, 80 DTCs, 402 tests | `include/`, `README.md`, `docs/timing.md`, `docs/target-bringup.md` (T-37) | — | — |
| F241 | Acquisition freshness of the slow-list inputs (round 24, §10n): each `hal_adc_read()` judged against the stamp its consumer last took — NEW taken; HELD (the same conversion, younger than `cal_temp_hold_ms`) taken nothing, the verdict kept; EXPIRED (older, or never converted) withdrawn until a new conversion; no "must change" rule. Temperatures invalid (their group DTC, FW-04's invalid-reading derating); V5GD/VOFS: V_DC invalid, the §6 V_DC row (not the V5GD row); KL15, INTRLOK_N, VSUP read as never converted; HW_ID needs eight new conversions; `DTC_ADC_SLOW_STALE` | `include/ti_types.h: ti_acq`; `sense/temp.[ch]: update_one, temp_update`; `sense/vdc.[ch]: vdc_update` (`ref_stale`); `app/app.[ch]: init_identity, sense_fast, sense_slow, detect` (`acq`, `acq_expired`); `comms/dtc.h: DTC_ADC_SLOW_STALE`; `tools/gen-params.mjs: cal_temp_hold_ms`; `platform/host/sim_hal.c: sim_adc_freeze, sim_adc_never` | `acq_fresh`: all 12 tests; `temp: a_held_conversion_is_not_taken_again_and_an_expired_one_withdraws_the_channel`; every earlier test unchanged in logic (`temp`, `vdc` call the new signatures with NEW) | T-60 |
| F243 | FW-13's rate latch holds (round 24 follow-up): a latched `TEMP_RATE` is never replaced by a later sample — the channel stays invalid and RATE for the key cycle; every new sample's open/short check in `wire`, `fault` = `wire` only without the latch; the open or short surfaced as `DTC_TEMP_OPEN_SHORT` (82, 0xD10052; set while any channel's `wire` reads open or short, passed when none does); open/short without a latch per sample as before | `sense/temp.[ch]: update_one` (`wire`); `app/app.c: temp_dtcs`; `comms/dtc.h`, `comms/dtc_table.h`: `DTC_TEMP_OPEN_SHORT`; `nvm/runstats.c` (class sensor) | `temp: a_latched_rate_fault_is_never_replaced_by_an_open_or_short_sample, open_and_short_without_a_rate_latch_still_act_per_sample`; `acq_fresh: a_rate_latched_module_channel_that_reads_open_or_short_stays_latched` | — |
| F244 | V_DC stamps under the acquisition contract (round 24 follow-up, §10n): each channel's stamp judged by `ti_acq` with the hold `cal_vdc_stale_us` — new or held judged as before, expired stale until a new stamp (round 18's `ti_stale` alone read a stopped converter's stamp as fresh for 199 µs around every 2^32 µs of its age); the §6 V_DC-invalid row (`DTC_VDC_STALE`), HV unknown; never converted: code 0, fail-safe | `sense/vdc.[ch]: vdc_update` (`acq_ch`) | `vdc: a_stopped_channel_stays_stale_across_2e32_us_of_its_age`; `acq_fresh: a_stopped_v_dc_converter_is_stale_until_a_new_conversion`; the round-16/18 `vdc` tests unchanged | — |

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
| Resolver frame protocol (A14-R02): per-channel DMA completion, epoch published when every channel has it, seqlock + DMA positions + copy-time bound; (round 18, FW-35) the cadence stamp, the servicing deadlines, the re-acquisition; (round 19) the SWG-start origin, the clock re-sync, the restart | `hal/sdadc.h, hal/sdadc_ring.c`; `platform/s32k396/s32k396_resolver.c, s32k396_main.c` (three interrupts); `platform/host/sim_hal.c` (per-channel eDMA model, triggered by the SWG) | `sdadc:` all 24 tests; `scenarios: a_frozen_resolver_channel_is_never_read_as_fresh, temporary_empty_reads_never_fault, a_late_resolver_interrupt_at_speed_is_counted_and_reacquired` |
| One time domain (A12-R06): 64-bit µs, ms = us64/1000; (round 18) sensor stamps against a check time with a signed age (`ti_stale`) | `hal/timer.h`; `include/ti_types.h: ti_stale`; `platform/host/sim_hal.c, platform/s32k396/s32k396_io.c: hal_time_us64`; `app/app.c`; `nvm/nvlog.c`; `comms/dtc.c: dtc_set, dtc_times` | `time:` 5 tests; `scenarios: running_across_the_microsecond_wrap_keeps_fresh_frames_fresh, boot_across_the_microsecond_wrap_reaches_armed, desat_retry_waits_1s_across_the_microsecond_wrap, dtc_time_stamps_across_the_microsecond_wrap_in_the_application, samples_stamped_after_the_isr_entry_stay_fresh` |
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
| Every ADC read taking 1, 5 or 50 µs before it stamps (the target's order), also with one tick's reads straddling the 32-bit µs wrap; the current-loop ISR preempting the 1 ms task after it read its time; a DESAT that preempts the task, the hold over before its recovery, also across the wrap (FW-34) | `scenarios: samples_stamped_after_the_isr_entry_stay_fresh, a_run_at_speed_with_the_adc_reads_taking_time, a_current_loop_preempting_the_task_is_not_a_dead_loop, fw15_low_wait_counts_from_a_fault_that_preempted_the_task`; `bridge: fw15_low_wait_runs_on_the_bridges_own_clock`; `current:`, `vdc:`, `resolver:`, `time:` the round-18 unit tests |
| SDADC completion interrupts held off by 5–29 µs (accepted, exact), 40 and 60 µs (rejected, re-acquired), exactly a lap (never fresh), one channel lapping, all three stalled (interrupts or DMAs), the reader preempted across a lap, an anchor served late, both wraps; lost samples (FW-35) | `sdadc:` the round-18 tests; `scenarios: a_late_resolver_interrupt_at_speed_is_counted_and_reacquired` |
| Resolver chain latency of 25 and 50 µs, both directions, 3000 and 10 000 rpm, against the true rotor angle at `now` (FW-36) | `resolver: latency_compensation_matches_the_true_angle_at_now` |
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

## Round 18: before/after on the pre-fix source

Three confirmed defects of commit 4425af9 (reviews A16-R01, A16-R02, A16-R03), each mechanism verified against the
source before the change:
- **A16-R01** (FW-34). `app_isr_current()` read `now_us` at entry; the target's `hal_adc_read_phase()`
  (`s32k396_adc.c`, the triplet) and `refresh()` (every `hal_adc_read()`, V_DC included) stamp with a LATER
  `hal_time_us()`; `isns_update()`/`vdc_update()` then took `ti_elapsed(now_us, stamp, hold)` — unsigned, so a stamp
  one tick newer read 4 294 967 295 µs old. `rslv_age()` had the same shape. The host's reads stamped with the frozen
  simulation clock (the ISR's entry), which hid it. Grepping every caller of the comparison found it once more, in
  the 1 ms task: the FW-31 liveness check `ti_elapsed(now_us, a->t_isr_us, …)` — `now_us` is read at the task's start,
  before the FS26 transfers, and the current-loop ISR (priority 2) rewrites `t_isr_us` whenever it preempts the task
  there: "loop dead", currents lost, resolver aged, the control-lost row — on the target whenever a current-loop
  trigger falls between the task's time read and that check (the FS26 answer's three SPI frames, ≈ 30 µs, every second
  tick). *Fix:* each check reads its own time after its acquisition reads (`sense_fast`; V_DC read after VOFS/V5GD);
  `ti_stale()` — a signed age, stale at `hold` or more before the check or after it — in `isns_update`, `vdc_update`,
  `rslv_age` and the liveness check; the ISR's entry time kept for `t_isr_us`, the WCET, the FOC angle and the bridge.
  **The same class at the FW-15 recovery timer** (closed after the first report of this round): `br_rec_step()`
  compared the caller's `now_us` — the 1 ms task's, read at its start — with `rec_t_fault_us`, which the fault ISR
  stamps (`app.c`, `t_fault_us`). A fault that preempts the task after its time read stamps later; if the DESAT hold
  (60 µs) has run out before the task reaches `recovery()` in that same tick (no EN drop pending), the unsigned age
  wrapped and the ≥ 1.5 ms low wait was skipped: EN back high and the FLT_CLR pulse ≈ 0.1 ms after the fault, the
  driver's 1 ms FLT mute not over — no reset, `DTC_FLT_RECOVERY_FAIL` (fail-safe, but a wrong recovery). *Fix:* the
  round-17 rule of its sibling (§10a FW-22: the bridge reads its own time, never a caller's stamp) — `br_rec_step()`
  takes no time argument and times the low wait with `hal_time_us()`, read after its `br_service()`; `recovery()` and
  `execute()` lost the parameter with it.
- **A16-R02** (FW-35). `hal_sd_ring_complete()` stamped `t_start = now_us − period_us` from the interrupt's time and
  counted `(hw − done) % 4` blocks; `dma_within()` was modulo 4 too. Reproduced on the pre-fix source: a 20 µs latency
  stamps every frame 20 µs too new; one burst served 60 µs late at 6000 rpm on the SiC SKU latched the observer's
  acceleration check (FAULT under torque); interrupts held off exactly four periods let the reader return the next
  lap's block under the old epoch's stamp; and every break left the ring down until reset. *Fix:* the cadence stamp
  (`t_org + (k − k_org)·100 µs`, anchored at the first completion after (re)acquisition, moved back by an earlier
  one); a block's first completion within `cal_sd_irq_lat_max_us` (30 µs), a later channel's within half a period,
  else the ring breaks; the reader's cadence check (3 periods); re-acquisition from the DMA positions with a fresh
  origin and `DTC_RSLV_REACQUIRED`; lost samples stay down until re-init.
- **A16-R03** (FW-36). `rslv_theta_e_at()` computed `(now − t_ref) − t_mid − cal_rslv_latency_us`; the parameter is a
  delay beyond the mid-block reference, so the extrapolation must add it; `resolver: acquires_at_speed_after_a_reset`
  expected θ(t_ref + 50 µs) with 50 µs of latency to equal θ(t_ref) — the wrong sign, encoded. *Fix:* `+ L`; the
  header, the generator's description, `docs/timing.md` and the T-37 row state the sign; the test corrected.

  **And at the PWM-ASC entry's dead-time reference** (the last instance, closed after the second report): `br_enter_pwm_asc()`
  took the caller's time as the moment the high sides went off whenever the PWM was already inhibited — a 1 ms task whose
  time predates a hardware fault that preempted it read that stamp as "long ago" and skipped the dead-time wait (the
  fault ISR's own run time made it moot in practice; the rule is now structural). The bridge stamps every turn-off it makes
  on its own clock (`pwm_off()`), the fault ISR notes the hardware inhibit on entry (`br_note_pwm_off()`), and the entry
  counts the dead time from the newer of that stamp and the caller's. Test `bridge: pwm_asc_dead_time_counts_from_the_bridges_own_turn_off`
  (2.5 µs IGBT dead time, before and across the wrap). Mutation: the newer-stamp rule removed → 2 checks fail. Totals with
  it: 278 tests, 2534 checks, 0 failed in the three flavours; the pre-fix tree's 98 failing checks become 100.

Method, as in rounds 14–17: the pre-fix `firmware/` (4425af9) was extracted into a scratch directory and the round-18
`tests/` copied over it, with the round-18 host simulation (`sim.h`, `sim_hal.c`: test infrastructure — the ADC read
time, the per-channel interrupt hold, the overrun flag, the FS26-transfer hook) re-expressed on the pre-fix ring API:
`hal_sd_ring_init()` with its old three arguments, the overrun flag setting the old `broken` (the pre-fix driver's
`TI_SD_LOST` path), `hal_sdadc_reacquired()` = 0 (the pre-fix ring never re-acquired). The old tree also received the
round-18 CAL row (`include/ti_params.h` + `make params` with the round-18 generator: data it never reads). A
compatibility header force-included into the test files only mapped the new interfaces onto what the pre-fix code
did: `ti_stale` = `ti_elapsed` (unsigned), `hal_sdadc_init(c, l)` = the one-argument call (no deadline),
`DTC_RSLV_REACQUIRED` = `DTC_COUNT` (no such record: 0 occurrences), and `br_rec_step(b, p)` = the pre-fix
three-argument call with the time its caller passed — the task's start time in the one test that models the task
(`s_task_us`), the current time elsewhere. Every test compiled there. Pre-fix (the set before the PWM-ASC dead-time test was added; that test fails 2 more checks on the tree without the newer-stamp rule): 277 tests, 2519 checks, 98 failed — all
in the tests below; every other test passed. This tree: 278 tests, 2534 checks, 0 failed, also at `-O2` and under
ASan/UBSan (a check inside a frame loop runs once per frame, hence the one check of difference).

| Suite: test | Item | Pre-fix (4425af9) | This tree |
|---|---|---|---|
| time: sensor_stamps_are_judged_with_a_signed_age | A16-R01 | FAIL (4): a stamp 1–199 µs after the check read ≈ 2^32 µs old, before and across the wrap | pass |
| current: a_triplet_stamped_after_the_check_time_is_fresh | A16-R01 | FAIL (6): triplets stamped 1, 5, 50 µs after the check stale and invalid, with and without the wrap | pass |
| vdc: a_channel_stamped_after_the_check_time_is_fresh | A16-R01 | FAIL (6): both channels stale — V_DC invalid, HV unknown | pass |
| resolver: a_frame_newer_than_the_check_time_is_not_aged_out | A16-R01 | FAIL (6): a frame 30 µs to hold − 1 µs newer than the check withdrew the angle | pass |
| scenarios: samples_stamped_after_the_isr_entry_stay_fresh | A16-R01 | FAIL (12): at 1, 5 and 50 µs per read, with and without the µs wrap, the first delayed tick lost the currents (stale), the control-lost row, FAULT, DTC_ISNS_STALE | pass: every tick fresh, RUN |
| scenarios: a_run_at_speed_with_the_adc_reads_taking_time | A16-R01 | FAIL (2): FAULT at the first tick, at 1 µs and at 5 µs | pass: 2 × 500 ms of RUN at 100 Nm |
| scenarios: a_current_loop_preempting_the_task_is_not_a_dead_loop | A16-R01 (task) | FAIL (1): the first FS26 answer with the ISR inside it declared the loop dead: currents lost, the row, FAULT | pass: 300 ms of RUN, ≥ 300 preemptions; a real stop still caught |
| bridge: fw15_low_wait_runs_on_the_bridges_own_clock | A16-R01 class (FW-15) | FAIL (8): a fault stamped 20 µs after the task's time, the hold over, nothing pending — WAIT_LOW done at the first step: EN high and FLT_CLR ≈ 0.08 ms after the fault, the driver not reset (REC_FAIL); the same with the task's time 10 µs before the 32-bit wrap and the fault after it | pass: the pulse and the release ≥ 1.5 ms after the fault, then DONE, both cases |
| bridge: pwm_asc_dead_time_counts_from_the_bridges_own_turn_off | A16-R01 class (PWM-ASC entry, self-found) | FAIL (2): with the caller's 20 µs-old time taken as the turn-off, PWM-ASC was set inside the 2.5 µs dead time of the real hardware inhibit, before and across the wrap | pass |
| scenarios: fw15_low_wait_counts_from_a_fault_that_preempted_the_task | A16-R01 class (FW-15) | FAIL (4): FLT_HS inside the task's FS26 transfer at 10 000 rpm, the hold over before `recovery()`: EN back high in that same task, FLT_CLR ≈ 0.1 ms after the fault, `DTC_FLT_RECOVERY_FAIL`, no PWM-ASC | pass: ≥ 1.5 ms, reset, PWM-ASC |
| sdadc: interrupts_held_off_within_the_deadline_keep_every_stamp_exact | A16-R02 | FAIL (1): every stamp 5–29 µs too new | pass: exact |
| sdadc: a_late_anchor_is_moved_back_by_the_first_prompt_completion | A16-R02 | pass (the pre-fix stamp followed each interrupt; this proves the round-18 origin correction) | pass |
| sdadc: an_interrupt_held_off_past_the_deadline_is_rejected_then_reacquired | A16-R02 | FAIL (6): the 40 / 60 µs-late completion accepted — its block published, stamped 40 / 60 µs too new | pass: never published; frames again within 3 periods |
| sdadc: interrupts_held_off_for_a_lap_are_detected_never_fresh | A16-R02 | FAIL (3): the reader returned the next lap's block under the old stamp; the ring never noticed | pass |
| sdadc: one_channel_lapping_while_the_others_do_not | A16-R02 | FAIL (6): caught by the skew, then down for good (each channel) | pass: re-acquired |
| sdadc: all_three_stalled_together_are_detected_and_reacquired | A16-R02 | FAIL (5): interrupts stalled 5 periods left the ring down for good (1); no re-acquisition recorded (4) — the 8-period and DMA stalls resumed on the interrupt-time stamps | pass |
| sdadc: the_reader_preempted_across_a_lap_never_returns_the_frame | A16-R02 guard | FAIL (1): the read refused (copy-time bound), but with the interrupts held the ring then stayed down | pass |
| sdadc: stamps_are_exact_across_the_microsecond_and_epoch_wraps | A16-R02 | FAIL (1): stamps 20 / 29 µs too new two milliseconds in three | pass |
| sdadc: lost_samples_keep_the_ring_down_until_init | A16-R02 guard | pass (the pre-fix ring stayed down after any break) | pass |
| sdadc: a_frozen_channel_yields_no_frame (changed: re-acquired in step, never out of step) | A16-R02 | FAIL (3): down for good after the in-step resume (EXC, SIN, COS) | pass |
| sdadc: held_off_completion_interrupts_never_yield_a_fresh_frame (changed: re-acquired) | A16-R02 | FAIL (2): down for good | pass |
| sdadc: the_epoch_counter_wraps (37 frames) | — | pass | pass |
| params: round18_cal_defaults_and_ranges | A16-R02 CAL | pass (row added to the old tree) | pass |
| scenarios: a_late_resolver_interrupt_at_speed_is_counted_and_reacquired | A16-R02 | FAIL (6): SiC — the frame stamped 60 µs too new latched the observer's acceleration check: a resolver fault, FAULT under torque; IGBT ran on the mis-stamped frame; neither event recorded | pass: RUN throughout, 2 occurrences of DTC_RSLV_REACQUIRED, SiC and IGBT |
| scenarios: a_frozen_resolver_channel_is_never_read_as_fresh (changed: re-acquired) | A16-R02 | FAIL (6): the ring and the resolver down for good after the in-step resume | pass: re-acquired, re-primed, the row still latched |
| resolver: latency_compensation_matches_the_true_angle_at_now | A16-R03 | FAIL (8): off by 2ωL — 3.6 / 7.2° el at 3000 rpm and 12.0 / 24.0° at 10 000 rpm for 25 / 50 µs, both directions | pass: ≤ 0.3° el |
| resolver: acquires_at_speed_after_a_reset (corrected) | A16-R03 | FAIL (1): 0 rad where 4·ω·100 µs = 0.503 rad | pass |

Mutations of this tree, each fix undone once (failed checks, the tests that caught it): isns freshness unsigned again
(6: `current: a_triplet_…`); V_DC freshness unsigned (6: `vdc: a_channel_…`); resolver age unsigned (6: `resolver:
a_frame_newer_…`); V_DC judged at the ISR entry with the signed helper kept (4: `scenarios: samples_stamped_…`, 50 µs
per read); the task's liveness check unsigned (1: `scenarios: a_current_loop_preempting_…`); `ti_stale` without its
future bound (5: `time:`, `resolver:`); the stamp from the interrupt's time (5: three `sdadc` tests); no servicing
deadline for a block's first completion (3: the 40 µs case — a later channel's half-period check catches 60 µs); no
re-acquisition after a break (48, ten tests); no half-period check of a later channel (2: `sdadc: a_frozen_channel_…`,
a channel that resumed out of phase under the others' count — mixed frames); the reader without the cadence check
(3: the lap); no origin correction (1: `sdadc: a_late_anchor_…`); lost samples re-acquired (1: `sdadc: lost_samples_…`);
no re-acquisition DTC (7: two scenarios); the latency subtracted again (9: `resolver:` two tests); the FW-15 low wait
timed with a clock 100 µs older than the bridge's own read — the task's start, in effect (8: `bridge:
fw15_low_wait_runs_on_the_bridges_own_clock`).

Test infrastructure changed with it (host simulation only): `sim_adc_read_delay_ns()` (a read takes simulated time,
events included, before it stamps), `sim_sdadc_irq_hold_ns()` (one channel's next completion interrupt held off),
`sim_sdadc_overrun()` (the target's DMA/FIFO error flag), `sim_fs26_xfer_hook()` (an interrupt inside the task's FS26
transfer); `hal_sdadc_init()` takes the servicing deadline and refuses a carrier whose period is not a whole number of
microseconds, as the target's does. Existing tests changed: see README "Round 18".

## Round 19: before/after on the pre-fix source

One confirmed defect of commit e315bf1 (A17-R01: all three rechecks — md, html, csv — reproduced it; register F201),
the mechanism verified against the source before the change: `hal_sd_ring_complete()` (`sdadc_ring.c` lines 147–150)
set the cadence origin from a completion's own execution time — `t_org = now_us − period_us` at the first completion
after (re)acquisition (at init the first completion took the DMA positions and the next block's anchored), with
`t_org += late` moving it back when an earlier one came. So (a) a late anchoring completion became the reference: its
block and every later one were stamped late by it with `broken` false — reproduced: 29–80 µs (and 800 µs) of latency
on the first two blocks published block 1 stamped exactly that much too new; (b) a CONSTANT delay passed the 30 µs
deadline forever, the deadline being measured from the displaced origin — reproduced: 40 / 60 µs from init published
50 of 50 frames 40 / 60 µs too new (9.6° / 14.4° el at 10 000 rpm, 4 pole pairs); (c) after a break the re-anchor
took the next completion again, so a delay rejected once was absorbed — reproduced: 40 / 60 µs from mid-run broke the
ring once, then published 49 frames 40 / 60 µs too new; at the application, a 60 µs burst followed by 25 µs (inside the
deadline) left the FOC angle 5.3° el behind the rotor model at 10 000 rpm; (d) `resync()` inferred the counts from equal
DMA slots modulo 4 and never the acquisition phase — reproduced: all three DMAs stalled 5 periods, or silent for
2^32 µs + one period, re-acquired under a fresh origin with their counts off the clock; a channel paused one period and
resumed stayed down without any flag (no restart path existed). The same shape once more (found with the tests): an
early completion moved the origin back to itself — SIN's DMA forced 20 µs into a period moved it 80 µs (the next
completion then happened to break the ring); one early by less than the deadline would have re-dated every later frame
by as much, never broken.

*Fix (FW-35 rewritten, contract §10d):* the SDADCs are triggered by the SWG period start, so the origin is the **SWG
start** — `hal_swg_start()` (both platforms) brackets the generator enable with two `hal_time_us64()` reads inside
PRIMASK and calls the new `hal_sd_ring_anchor(r, t_org, k_org, unc)`: t_org the later read, k_org the first carrier
period's block (count 1: `hal_sdadc_init()` arms the DMAs at count 0 and stops the SWG first; the task's k_org = 0 is the
same block in the ring's count-after-completion convention), unc = the bracket + 1 µs of timer resolution + the new CAL
`cal_swg_start_lat_us` (2 µs, range 0–20, T-42): 3 µs by default; unc ≥ a quarter period is refused (`lost`). Running,
`hal_swg_start()` only updates the amplitude (the trim; LDOS, T-42). The ring never anchors itself (`anchored` only by
the anchor; an unanchored ring counts nothing) and judges EVERY completion — the first, and every one after a re-sync —
against block k's end t_org + (k − k_org + 1)·T with unc added to the allowance on both sides (64-bit, no wrap): a
block's first completion within [−unc, `cal_sd_irq_lat_max_us` + unc], a later channel's within [−unc, T/2 + unc];
early or late breaks the ring. `t_org += late` and the anchor-from-callback are gone. The origin is re-based by whole
periods at every publication and re-sync (the same cadence: block distances stay small across the 2^32 epoch wrap).
**Re-sync from the clock** (`resync`): the block j whose end lies within [−unc, T/2 + unc] of the completion (none: wait
for the next), the counts set to j, the DMA write positions only confirming it: every DMA past j ⇒ in step (j itself not
published); a later channel still on j ⇒ its own completion decides; the completing DMA still on j while another is past
it, a DMA anywhere else, or 12 completions without agreement ⇒ `lost`. Deviation from the brief, stated: the brief's
k_now = k_org + (now − t_org)/T with every slot required to equal k_now mod 4 would call a legitimately later channel
(30 µs, the round-16 model) or an interrupt served late inside the origin's ± 3 µs lost; the window, the wait for the
later channel's own completion, the witness rule (the completing DMA behind one that is past the block) and the wait
limit keep those in, and still catch a channel one period behind within two periods and all three behind within four.
**Synchronized producer restart:** `hal_sdadc_restart()` (both platforms: `hal_sdadc_init()` + `hal_swg_start()` at the
present code, the re-acquisition count kept), `hal_sdadc_lost()`; the 1 ms task (`sense_slow`) restarts a lost ring at
most `cal_rslv_restart_max` (3, range 0–10) times per key cycle — the count in the retained `app_session_t`, zeroed at a
cold start, kept across an MCU reset inside the key cycle — each one occurrence of `DTC_RSLV_REACQUIRED`; beyond it the
resolver stays invalid (FW-28, the §6 row). The host simulation now triggers the DMA blocks from the SWG start (t0 + k·T,
t0 = the enable + `sim_swg_start_latency_ns()`), anchors as the target does, and `hal_sdadc_init()` stops the SWG and
re-arms the DMAs. `TI_FW_ID` 0x0A0F0013. The platform contract (T-41): the eDMA error, the SDADC FIFO overrun and a missed
trigger set `lost`; the start latency and the first block's phase are measured (T-42); the completion latency is now
measured from the carrier boundary (T-40), the SDADC's output latency included.

Method, as in rounds 14–18: the pre-fix `firmware/` (e315bf1) was extracted into a scratch directory and the round-19
`tests/` copied over it, with the round-19 host simulation (`sim.h`, `sim_hal.c`: the SWG-triggered DMA model, the start
latency knob, `sim_sdadc_block_start_ns`) re-expressed on the pre-fix ring API — `hal_sdadc_init()` with its two
arguments, the 32-bit completion time, no anchor call (the pre-fix ring anchors itself), `hal_sdadc_restart()` = the
pre-fix re-init (`hal_sdadc_init` + `hal_swg_start`), `hal_sdadc_lost()` = the pre-fix ring's `lost` flag. Data the
pre-fix code never reads or writes was added so everything compiled: the ring's `unc_us` (0), `app_session_t`'s
`rslv_restarts` (0), the two CAL rows (`include/ti_params.h` + `make params` with the round-19 generator). A compatibility
header force-included into the test files only maps `hal_sdadc_init(c, l, s)` onto the two-argument call. Pre-fix:
290 tests, 2705 checks, **108 failed — all in the 17 tests below**; every other test passed. This tree: 290 tests, 2705
checks, 0 failed, also at `-O2` and under ASan/UBSan.

| Suite: test | Item | Pre-fix (e315bf1) | This tree |
|---|---|---|---|
| sdadc: the_first_completion_is_judged_against_the_swg_start | A17-R01 (a) | FAIL (42): no uncertainty declared; block 0 never published; block 1 published and stamped 29 / 30 / 31 / 33 / 34 / 40 / 60 / 80 / 800 µs too new with `broken` false — beyond the deadline included | pass: 0–33 µs accepted, blocks 0 and 1 stamped at their true starts exactly; 34–800 µs broken at once, never published; one re-acquisition, then honest |
| sdadc: the_swg_start_latency_stays_inside_the_declared_uncertainty | A17-R01 (budget) | FAIL (6): stamps from the anchoring completion (+1 µs, +30 µs) instead of the start | pass: 1 / 2 / 3 µs start latency, prompt and at the 30 µs deadline — every stamp exactly the latency early, within u = 3 µs |
| sdadc: a_constant_completion_delay_is_never_published | A17-R01 (b) | FAIL (8): 50 of 50 frames published 40 / 60 µs too new, the ring never broken | pass: broken at the first completion, nothing published for 5 ms, never `lost`; honest again once the delay ends |
| sdadc: a_rejected_delay_is_never_absorbed_after_a_break | A17-R01 (c) | FAIL (4): after the first break, 49 frames 40 / 60 µs too new | pass: every delayed completion breaks the ring, nothing published; honest once it ends |
| scenarios: a_late_completion_after_a_break_never_dates_the_angle | A17-R01 (c), application | FAIL (1): the FOC angle up to 5.3° el behind the rotor model at 10 000 rpm, the resolver invalid on a tick | pass: within 3° el (the simulation's own error ≈ 1.9°), valid throughout, one re-acquisition, no restart |
| sdadc: a_channel_paused_one_period_is_lost_not_taken_for_a_late_one | A17-R01 (d) | FAIL (6): never `lost` — the ring down with no flag, nothing to restart it | pass: `lost` within two periods of the resume (EXC, SIN, COS), frames after the restart, honest |
| sdadc: a_frozen_channel_yields_no_frame (changed) | A17-R01 (d) | FAIL (15): the in-step resume (20 periods) re-acquired from equal positions; `lost` never set (6 cases) | pass: `lost` during the freeze; nothing after either resume; the restart brings frames back |
| sdadc: all_three_stalled_together_are_detected_and_reacquired (changed) | A17-R01 (d) | FAIL (3): the 5-period DMA stall re-acquired under a fresh origin | pass: held interrupts (5, 8 periods) and the 8-period DMA stall re-sync (in phase with the clock); the 5-period one is `lost` (wait limit), restart |
| sdadc: the_origin_and_the_clock_index_hold_across_the_32bit_wrap | A17-R01 (d), wraps | FAIL (2): 2^32 µs + one period of silence re-acquired (counts off the clock) | pass: anchored 150 µs before the µs wrap with the epoch 3 before its own, a break after both — honest; 2^32 µs + a multiple of 4 periods silent — re-synced; one more period — `lost` |
| sdadc: an_early_completion_breaks_the_ring | self-found (the same class) | FAIL (1): not broken — the origin moved 80 µs back to the early completion | pass: broken at once, the early block never published, re-synced (SIN's DMA is back on the clock), honest |
| sdadc: a_late_interrupt_inside_the_origins_uncertainty_is_no_phase_loss | re-sync rule (no false loss) | FAIL (1): re-synced at once from the equal positions (then re-anchored at the next completion) | pass: broken, waits (every DMA still on the block the clock may count as ended), re-syncs — never `lost` |
| sdadc: nothing_counts_before_the_swg_start | unanchored ring | FAIL (2): stray completions before the start counted — the positions taken (count 4), the ring synced | pass: nothing counted, nothing published |
| sdadc: the_anchor_refuses_an_origin_too_uncertain | anchor guard | FAIL (1): no such refusal (frames flow) | pass: u = 25 µs `lost`, 24 µs runs |
| sdadc: the_epoch_counter_wraps (+1 check) | re-base | FAIL (1): the origin not re-based to the newest epoch | pass |
| sdadc: a_late_channel_holds_the_frame_back_and_the_stamp_is_the_first (changed) | A17-R01 | FAIL (1): the stamp 1 µs late (the anchoring completion's latency) | pass: exactly the block start |
| scenarios: a_frozen_resolver_channel_is_never_read_as_fresh (changed) | restart | FAIL (3): re-acquired by the ring (count 1), no restart | pass: `lost`, one restart, one DTC occurrence, re-validated, the row latched |
| scenarios: resolver_producer_restarts_are_bounded_per_key_cycle | restart limit | FAIL (11): no restart path — the ring down for good at the first loss | pass: three restarts (three DTC occurrences, the SWG at the trim's code, re-validated), the fourth refused, the resolver invalid with the row; an MCU reset inside the key cycle keeps the budget spent |
| params: round19_cal_defaults_and_ranges | CALs | pass (rows added to the old tree) | pass |
| sdadc: lost_samples_keep_the_ring_down_until_a_restart (renamed) | restart | pass (the pre-fix re-init) | pass: the restart keeps the re-acquisition count |

Mutations of this tree, each mechanism undone once (failed checks, the tests that caught it): the origin re-dated by the
first completion (57: `the_first_completion_…` 42, `…start_latency…` 6, `a_constant_…` 8, `a_late_channel_…` 1); no
uncertainty in the windows (12: `the_first_completion_…` 6 — 31 and 33 µs rejected — and `…start_latency…` 6); an early
completion accepted (3: `an_early_completion_…`); the re-sync re-dating the origin from the completion, the old absorption
(21, seven tests including `a_rejected_delay_…` and the application's angle); the re-sync ignoring the DMA positions (33:
`a_frozen_channel_…` 15, `a_channel_paused_…` 6, the 32-bit-wrap test, two scenarios); no witness rule (3: `a_channel_paused_…`
— caught only by the wait limit, five periods late); no wait limit (6: the 5-period stall, the 2^32 µs silence,
`resolver_frames_stopping_…`); a 32-bit clock index (3: `the_origin_and_the_clock_index_…`); no uncertainty guard at the
anchor (1); an unanchored ring counting (1: `nothing_counts_…`); no restart in the task (24: three scenarios); no restart
limit (6) and the budget reset at an MCU reset (2) (`resolver_producer_restarts_…`); the restart dropping the re-acquisition
count (1: `lost_samples_…`); the strict re-sync — the completing DMA required past the block (3: `a_late_interrupt_inside_…`);
no re-base at publication (1: `the_epoch_counter_wraps`).

Test infrastructure changed with it (host simulation only): the DMA blocks start at the SWG start (`hal_swg_start()`), not
at sim time 0 — `sim_swg_start_latency_ns()` (the enable to the first block's phase 0, default 0), `sim_sdadc_block_start_ns()`
and `sim_sdadc_period_index()` counted from it (every stamp's reference); `hal_sdadc_init()` stops the SWG and re-arms the
DMAs (a frozen one runs again); `h_rotor_theta_e()` (the harness rotor's electrical angle). Existing tests changed: see
README "Round 19".

## Round 23 — FW-39 (motor self-commissioning)

Method: the suite on the round's starting tree (300 tests, 2802 checks, 0 failed) with FW-39 added gives 310 tests, 3068
checks, 0 failed — also with `-fsanitize=address,undefined` and at `-O2`; `make target-check` 52 → 55 markers, each with its
row (this item's three `TODO(HW)`: T-51…T-53). Each mutation below ran on that tree. The estimator was developed against the virtual PMSM, which exposed two
effects the tests now pin: the FOC's dead-time compensation, applied to a current sampled 1.5 periods earlier, read as
+45 µH on both axes at 500 Hz when the voltage was taken from the loop's own v_d/v_q (hence the duties), and the inverter
error, a different real matrix in the d and the q run, cost 9 % of the saliency until both runs shared one DC operating
point; the hold correction had been applied as sinc instead of 1/sinc (−0.2 %).

Mutations of this tree, each check undone once (failed checks, the tests that caught it): the vehicle-speed precondition
(10: `every_precondition_…`, `a_running_routine_aborts_…`); the active-DTC precondition (12: both); the heartbeat watch
(5: the abort test); the HV window (8: both); the attestation (17: the refusals); the VCU enable check (12: both and
`no_torque_command_…`); the state check — armed through ARMED_ZERO_TORQUE (1: the refusal in RUN); the evidence / record /
gains check reduced to `no_arm` (13); the §6-row precondition (13: both); the locked routines' speed precondition (4); the
locked rotor's angle watch (3: the heavy free rotor, caught only by its excursion); the dyno speed window (5); the
confirmation bypassed (12: `a_change_beyond_its_band_…`, `the_record_is_written_…`); the FW-20 class check (2:
`a_value_outside_the_fw20_class_…`); the torque bar (3: the abort test, `no_torque_command_…`); one start per unlock (4:
`the_record_is_written_…`); the unlock not required (1); the service CALs not validated (14); the direction against the
phase sequence (1) and against the attested dyno (2: `the_resolver_direction_…`); the commit allowed while the bridge
switches (2: `the_record_is_written_…`).

## Round 23 — FW-40 (diagnostic services)

Method: the tree as it stood when FW-40 landed (358 tests, 3792 checks, 0 failed — other round-23 items already in) with
FW-40 added gives 369 tests, 4441 checks, 0 failed, also with `-fsanitize=address,undefined` and at `-O2`; the FW-40 suite
alone is 11 tests, 649 checks, and no other suite's count moved. `make target-check`: FW-40 adds no marker and moves none
(its `app.h` include sits after the `TI_FW_ID` marker). The suite was developed in a private copy of the tree and landed
only green. Tests put requests straight into `uds_handle()` between two 1 ms tasks and take the frames `uds_diag_tick()`
sends, so a DID or a status is compared with the live state at the same instant; three go through the 1 ms task on the
bus. The host has no preemption inside the task, so the copy's re-take when a current-loop ISR intervenes is target-only
(T-36).

Mutations, each built alone into a private tree with its object forced to rebuild, the suite run, the source restored
(failed checks, the tests that caught it): M1 the HV-absent gate of 0x14 removed (6: `clear_is_gated_…`); M2 the armed
gate removed (1: the same — armed with the link reading 20 V); M3 the SecurityAccess gate removed (7: the same and the
fuzz); M4 `DTC_DESAT_REPEAT` clearable (4: `permanent_latches_…`); M5 the unlock not consumed (4: `clear_is_gated_…`,
`permanent_latches_…`); M6 the status mask ignored (18: `dtc_count_and_lists_…`); M7 i_d and i_q swapped in the snapshot
(4: `snapshot_records_…`); M8 every DTC's records answered (1: the same); M9 no restart when the ring moves (2:
`a_record_written_…`); M10 the V_DC channels swapped in 0xF204 (1: `dids_read_…`); M11 every due periodic DID in one task
(302: `periodic_dids_…`, `the_stream_…`); M12 a periodic frame beside a response frame (5: `periodic_dids_…`); M13 the
block size ignored (9: `segmented_…`); M14 STmin ignored (7: the same); M15 a busy mailbox losing the response frame (1:
the same); M16 the layer waiting 5 µs (11: `dids_read_…`, `the_stream_…`); M17 the torque command not recorded in the fault
context (2: `snapshot_records_…`); M18 a malformed 0x22 length accepted (1: the malformed-requests test); M19 N_Bs never
abandoning (2: `segmented_…`); M20 the service lock clearable (1: `permanent_latches_…`); M21 a WAIT taken as
ContinueToSend (4: `segmented_…`). The first run of M2, M3 and M10 showed the suite's gaps (M3 a stale object: make 3.81
compares whole seconds): the armed-with-HV-absent case and V_DC channels 1 % apart were added, and every mutation rerun
with its object deleted first.

## Round 23 — Fixes (defects found by the closed-loop simulator and the update work)

Method: the live tree at the start (369 tests, 4447 checks, 0 failed in the host, ASan/UBSan and −O2 flavours;
`make target-check` 61 markers) → 402 tests, 4707 checks, 0 failed in all three; 61 markers (no marker added or moved).
Each fix's "before" run restores the pre-fix code in the live tree with the new tests present, runs the suite and
restores; each mutation is applied alone to the live tree, its objects deleted (make 3.81 compares whole seconds), the
suite run, the source restored. Counts are failed checks; the tests named caught it. Rows R23-1…R23-14 above.

| Fix | Before (the old code, the new tests) | Mutations (failed checks: the catching tests) |
|---|---|---|
| 1 FW-13 rate | 13 checks in 4 tests (one code at 50/100/125 °C invalid; TEMP_RATE latched at 70.6 °C on a 5 °C/s rise; the derating fallback 0.544 at 100 °C) | no deadband (1: `the_deadband_holds_a_slow_swing_of_a_few_codes` — survived its first run, the test was added for it); the rate over 1 ms (11: `module_temperatures_…`, `one_code_steps_…`); no latch (5: `a_fast_ramp_…`, `open_short_rate_plausibility`); no rate check (7: the same) |
| 2 field weakening from V_DC | 3 checks (`zero_torque_below_n_x_…`, `the_take_over_…`) | the take-over at the diodes' point V_DC/√3 (2); no margin (2); the OV trip for the measured link (2) — each `the_take_over_follows_the_measured_link` |
| 3 torque slew | 3 checks (`a_torque_reversal_at_10000_rpm_…`: DTC_OVERCURRENT, LS-ASC) | §6 zeroing slewed (33: battery-path, stale-CAN scenarios, `the_fault_paths_still_zero_…`); the slew at the FW-11 constant (1); decreases unslewed (3: `a_torque_reversal_…`); the HVIL ramp at the slew (1: `the_fault_paths_…`) |
| 4 DTCs set | 9 checks in 5 tests; the addendum 4 checks (`a_desat_recorded_before_the_power_up_…`) | M4a DESAT pending not recorded (1); M4b the two timeouts swapped (3); M4c the offset DTC unset (1); M4d the board DTC on one NTC (4: + `a_running_routine_aborts_on_every_loss`); M4e over-temperature never passes (1); M4f no temperature passes (2); M4g over-temperature at the derating start (2); M4h the block names HS always (2); M4i the bank from retained RAM only (4) |
| 5 ASC transient | 9 checks in 3 tests | the window ×10 (8); never re-armed (5); FAULT1 cleared beside an OV flag (1: `an_over_voltage_asc_keeps_…`); no window, any time in ASC (8) |
| 6 speed bound | 4 checks in 2 tests (LS-ASC 200 ms after a 2 000 rpm resolver loss) | the bound constant (4); n_max ignored (1: `the_speed_is_unknown_…`); the §6 context on the last speed (2: `a_resolver_loss_is_decided_…`) |
| 7 resolver centroid | 19 checks in 3 tests (1.848° el at 10 000 rpm, 0.553° at 3 000) | the shift's sign (20); the nominal phase for the block's (4: `the_block_angle_…`) |
| 8 FW-10 debounce | 71 checks in 4 tests | every frame taken (67); one frame invalidates (4); the gate at the tracking limit only (33); kept-out frames renew the hold (1: `frames_kept_out_…`); stale named before the latched fault (3) |
| 9 NVM CRC-32C | the torn calib write read back the record from two writes back; the random tears mixed | CRC-32 again (3); new slots in the old format (3); legacy slots refused (1: `legacy_slots_…`) |
| 10 64 slots | `every_record_has_its_own_slots_…` (capacity) | 32 slots (1); A/B pairs overlapping after the ring (28 in 8 tests: FW-38, FW-44) |
| 11 ISO-TP TransferData | 6 checks in 3 tests | out-of-sequence frames taken (2 — survived its first run: `segmented_requests_…` now checks that a wrong sequence number abandons the reception); no flow control after a block (12); no N_Cr (1); an oversized first frame ignored (1); another service's segmented request dispatched (1) |
| 13 the bench | on the old bridge: no `msg`, no `periodic` line, `dtc_clear` a hook, no vehicle speed | `make -C tool/bridge check`, 33 expectations: no tester flow control (2: 0x19 04, 0x19 0A); periodic frames paired as responses (1: 0x2A); no vehicle speed (3: service mode); `dtc_clear` the old hook (2: the 0x14 gate, the kept latches); no bench key (6); the consecutive-frame sequence check removed survives (the image never sends one) |

Found on the way: the host PMSM re-anchored a dyno-driven rotor at the last trigger's angle (a step back each call) —
fixed in `platform/host/sim_pmsm.c`; its explicit integration loses damping at 16 000 rpm (the ASC tests run at 8 500 /
10 000 rpm). Open (host card model, reported): the FS1B-preset ASC latch applied without the gate supply at a power-up
while the rotor turns (`platform/host/sim_chain.c: sim_chain_ls_on`).

## Round 23 — FW-45/FW-46

Method: the live tree at the start (402 tests, 4707 checks, 0 failed; `make target-check` 61 markers) → **415 tests, 4875
checks, 0 failed** in the host, −O2 and ASan/UBSan flavours (and at −O2 with `-ffp-contract=off`, and on the Cortex-M7 under
QEMU with GCC 14.3 and newlib: `make qemu-test`, 1397 s); 63
markers (T-57, T-58; T-52/T-53's citations moved). The new suite `fw45_46` is 13 tests; every earlier test file is
unchanged and passes. The new tests cannot compile against the old sources (they have neither the maps nor the table), so
each "before" restores the old behaviour in the new tree — the mutation that is that behaviour — and counts the failed
checks. Each mutation was applied alone to the live tree, its objects deleted (make 3.81 compares whole seconds), the
suite run, the source restored and the tree compared with its copy (`diff -r`: identical). Bit-identity beyond the suite:
a differential run of the baseline `torque.c` (its public names renamed) against the new one over the same 20 000 cases,
each motor with no map and with a flat map — 0 differ in the result, i_d, i_q, torque or voltage, at −O1 and −O2, with and
without floating-point contraction.

| Item | Before (the old behaviour restored: failed checks) | After |
|---|---|---|
| FW-45 record | layout 3 kept (M15): 2 checks, `the_record_is_layout_4_…` | pass |
| FW-45 torque model and solve | the maps ignored — FW-37 as it was (M1): 12 checks in 5 tests — the saturated reference (the scalar solve > 0.1 % off in 1 271 of 4 733 TQ_OK, worst 217 N·m), the 80 % torque (−9.8 %), the sweep's committed torque, the guard test, the speed model; the scalar contour in the solve (M2): 8 checks in 4 tests | pass (the map: −0.1 % at 80 %) |
| FW-45 current loop | no scheduling (M7): 3 checks, `gain_scheduling_…` (93 % overshoot, 87 % ring-back) | pass (6 %, none) |
| FW-45 commissioning | FW-39's 7-byte request parser (M23): 56 checks in 2 tests | pass |
| FW-46 | never applied (N1): 1 check (the −80 %); the DID not dispatched (N13): 16 checks in 2 tests | pass (−83 %) |

Mutations — 41, each caught (failed checks: the tests that caught it):

| # | Mutation | Caught by |
|---|---|---|
| M1 | the maps ignored (`motor_sat` 1 always): the scalar model | 12: `the_saturated_solve_…`, `the_psi_e_guard_…`, `the_map_delivers_…`, `the_routine_at_six_biases_…`, `gain_scheduling_…` |
| M2 | the solve on the scalar hyperbola (the contour without the maps) | 8: `the_saturated_solve_…`, `the_psi_e_guard_…`, `the_map_delivers_…`, `the_routine_at_six_biases_…` |
| M3 | no sign scan: one bisection over [d_lo, 0] | 1: `the_saturated_solve_…` (a TQ_LIMITED where the request fits) |
| M4 | the ψ_e guard from the scalars | 1: `the_psi_e_guard_…` (25 TQ_POSTCOND in 20 000) |
| M5 | flat maps on the saturated path | 2: the FW-37 suite itself (`torque: mtpa_spm_and_ipm, non_finite_inputs_…`) |
| M6 | F23's least-voltage i_d from the scalar closed form | 1: `the_saturated_solve_…` |
| M7 | no gain scheduling | 3: `gain_scheduling_…` |
| M8 | no 0.3 floor | 1: `gain_scheduling_…` |
| M9 | the d decoupling from the scalar L_q | 1: `gain_scheduling_…` |
| M10 | FW-10's speed model from the scalar L_d | 1: `gain_scheduling_…` |
| M11–M14 | a rising map accepted; no last ≥ 0.3 × first; the full scale not bound to the SKU; the ripple table unbounded | 1, 1, 1, 2: `the_record_is_layout_4_…` |
| M15 | layout 3 kept | 2: `the_record_is_layout_4_…` |
| M16 | the q run biased along d | 7: `the_routine_at_six_biases_…` |
| M17 | every bias the routine's 50 A | 17: `the_routine_at_six_biases_…` |
| M18 | the differential points committed as the apparent map | 1: `the_routine_at_six_biases_…` (the map 2.7 % → beyond 3 %) |
| M19 | a partial map ignored, the rest committed | 2: `the_bias_byte_…` — survived its first run (nothing else staged: refused anyway); R_s is now staged beside the partial map |
| M20 | the scalar not set from the committed map | 1: `the_routine_at_six_biases_…` |
| M21, M22 | bias index 6 accepted; the bias byte for any routine | 7, 6: `the_bias_byte_…` |
| M23 | the 8-byte (escape) start not taken | 56: `the_bias_byte_…`, `the_routine_at_six_biases_…` |
| M24 | a biased run also judged and staged as the scalar | 2: `the_routine_at_six_biases_…` |
| M25, M26 | the bias in the record's frame (the zero ignored); the d run's bias rotated the wrong way | 1, 1: `the_bias_runs_on_the_true_axes` |
| N1 | the feed-forward never applied | 1: `the_ripple_feed_forward_…` |
| N2 | no frequency gate | 1: `the_ripple_feed_forward_…` (600 rpm) |
| N3 | no scaling to the solved margin | 2: `the_feed_forward_never_leaves_…` |
| N4 | the table's mean applied | 2: `the_feed_forward_never_leaves_…` |
| N5 | the extremes not clamped to the CAL | 1: `the_feed_forward_never_leaves_…` |
| N6 | the feed-forward under a zeroing (`zero_now`) | 1: `the_feed_forward_never_leaves_…` |
| N7–N10 | the write's range check, unlock, running / in-torque check, exact length removed | 10, 1, 4, 1: `the_ripple_table_write_…` |
| N11 | the commit without the table | 5: `the_ripple_feed_forward_…` |
| N12 | the read-back ignores the staged table | 2: `the_ripple_feed_forward_…`, `the_ripple_table_write_…` |
| N13 | the segmented write not dispatched | 16: `the_ripple_feed_forward_…`, `the_ripple_table_write_…` |
| N14 | the low extreme not checked in the scale | 1: `the_feed_forward_never_leaves_…` (the regenerating side) |
| N15 | the table not interpolated (the point below) | 1: `the_ripple_feed_forward_…` |

M5, M9, M14 and N14 needed a second form (the first was a compile error: an unused function, variable or parameter); M4 was
not caught on the first run — the 20 000-case reference holds few weak-magnet machines whose saturated L_q falls below L_d, so
`the_psi_e_guard_…` was added for them. Not host-testable: the ordering of the scale around the references in
`torque_path` (a scale that fits both vectors until the new references are out) guards a current-loop ISR preempting the
task between the writes — the host runs the ISR between tasks, never inside one (T-58).

## Round 24 — FW-45 flux slope, FW-46 whole interval

An external review of round 23 (with an independent double-precision oracle; contract §10l "Flux slope (round 24)", §10m
"The whole interval (round 24)"). Method: each "before" restores the round-23 source in a private copy of the live tree
with the new tests present, runs the suite and restores (the copy compared with the live sources afterwards: identical);
each mutation is applied alone in that copy, its objects deleted (make 3.81 compares whole seconds), the suite run, the
source restored. The copy keeps a parallel agent's builds in the live tree undisturbed. Counts are failed checks; the tests
named caught it. The suite: 434 tests / 5108 checks in the live tree before the round-24 tests (the parallel round-24
acquisition work included) → 438 / 5139 (+4 tests, +31 checks), 0 failed.

| Item | Before (the round-23 source, the new tests) | After |
|---|---|---|
| FW-45 map check | 8 checks: `the_model_flux_must_rise_with_the_current` 6 (the reviewer's map on each axis, round 23's own positive control, the margin at each breakpoint, the margin over point 0), `an_unsupported_map_is_refused_at_the_commit_and_at_power_up` 2 (the knee committed, the reviewer's map armed) | pass |
| FW-46 scale | 5 checks: `the_scale_holds_every_i_q_between_the_extremes` (the reviewer's case: k = 1 with 315.08 V of 312.25 V at breakpoint 3; K and B in both mirrors) | pass |
| The proof | `the_scale_holds_every_i_q_on_20000_physical_maps` passes on round 23 too (at its seed round 23's scale stays inside; a 200 000-case run outside the suite, maps and cases drawn alike without the half near the least \|v\|: 6 outside, by up to 0.59 %, all regenerating) | 0 of 20 000 beyond 1e-6 (worst +5.8e-7) |

Mutations — 14, each caught:

| # | Mutation | Caught by |
|---|---|---|
| C1 | the slope rule removed (round 23's `map_ok`) | 8: `the_model_flux_…` 6, `an_unsupported_map_…` 2 |
| C2 | positivity only (margin 0) | 3: `the_model_flux_…` 2, `an_unsupported_map_…` 1 |
| C3 | margin 0.30 (the gain floor's) | 7: `the_map_delivers_the_torque_…` 3 (the reference plant refused at the power-up), `the_model_flux_…` 3, `the_scale_holds_every_i_q_between_…` 1 |
| C4 | the margin over the neighbouring point, not point 0 | 1: `the_model_flux_…` (1, 1, 0.8, 0.655…: 0.22·L_0 below breakpoint 3) |
| C5 | the wrong end of the segment (k + 1) | 16: `the_routine_at_six_biases_…` 8, `the_map_delivers_…` 3, `the_model_flux_…` 3, `an_unsupported_map_…` 1, `the_scale_holds_every_i_q_between_…` 1 |
| C6 | the last breakpoint skipped | 3: `the_model_flux_…` |
| T1 | no breakpoints (the extremes and one bound over the interval) | 3: `the_scale_holds_every_i_q_between_…` |
| T2 | the negative breakpoints skipped | 1: the same (K regenerating at −96.17 A) |
| T3 | no bound on the pieces (C = 0: the breakpoints alone) | 2: the same (B in both mirrors) — second form: the first left a parameter unused (a compile error under −Werror) |
| T4 | the cruder bound, max(A, B) + C/4 | 2: the same (the steep but monotone case: the scale must be the extremes' exactly) |
| T5 | C a quarter of itself | 2: the same (B) |
| T6 | the extremes unordered | 1: the same (the extremes swapped) |
| T7 | a NaN extreme dropped (min/max of the two) | 1: the same |
| T8 | the circle not checked at the interval's ends | 6: `the_feed_forward_never_leaves_the_solved_margin` 5, `the_scale_holds_every_i_q_on_20000_…` 1 |

Found on the way: checking the map's breakpoints is not enough (§10m) — the reviewer's premise that |v| is convex in i_q
between them holds for linear pieces of λ_q, but the model's pieces are quadratic (L linear in i), and a steep segment bends
|v| outward: an exploration of 200 000 intervals found interior peaks up to 2.4·10⁻⁴ above every extreme, base and
breakpoint at margins 0.25 and 0.30 (none at 0.5). Instead of the task's fallback, a fine scan (still a sample, ≈ 830
voltage checks against ≤ 40), each piece is bounded in closed form. Not changed: `tool/qt` explains a refused map commit as
"a map must not rise with the current" (`CommissioningSequencer.cpp`) — it does not name the flux-slope rule.

## Round 24 — acquisition freshness (F241)

An external review (MAJOR; contract §10n, README "Round 24 — acquisition freshness of the slow channels"). Method: the
live tree at the start (421 tests, 4937 checks, 0 failed; 63 markers). The work ran in a private copy of the live tree
refreshed before every run, with the parallel FW-45/FW-46 agent's files (`nvm/calib.c`, `control/torque.[ch]`,
`tests/test_fw45_46.c`) at HEAD there. "Before" restores the old behaviour in the new tree — the new tests cannot compile
against the old sources (no `sim_adc_never`, no DTC): every read taken as a new sample (`ti_acq` returning NEW), which is
exactly what the task and the current loop did — and for the module tests `temp.c` or `vdc.c` ignoring the acquisition;
each mutation is applied alone, every object rebuilt (`make -B`: `rsync` keeps the live files' times), the suite run.
Counts are failed checks; the tests named caught it. Result: **434 tests, 5108 checks, 0 failed** in the default, −O2
(`-ffp-contract=off`) and ASan/UBSan flavours; the live tree with both round-24 changes 438 / 5139, 0 failed; 63 markers.

| Item | Before (the old behaviour restored: failed checks) | After |
|---|---|---|
| Every read taken as new (task and current loop) | 76 checks in 11 tests: `an_acquisition_is_new_held_or_expired` 10, `one_conversion_then_none_for_60_s_is_withdrawn` 4 (valid, accepted at 60 001 ms, the derating on 70 °C), `each_stopped_temperature_channel_is_held_then_withdrawn` 32 (all seven channels: re-accumulated, never withdrawn, no DTC), `three_stopped_module_channels_take_the_invalid_reading_derating` 3 (derating 1.0), `a_temperature_input_that_never_converts_is_invalid_from_the_start` 7 (read as a short), `the_hold_is_counted_across_the_microsecond_wrap` 1, `a_stopped_v5gd_or_vofs_reading_leaves_v_dc_invalid_not_a_v5gd_loss` 12 (V_DC valid on the frozen code), `a_stopped_kl15_reading_is_held_then_read_as_absent` 2 (KL15 on), `a_stopped_interlock_reading_loses_the_hvil_signature_within_100_ms` 2 (status CLOSED, the open loop unseen), `a_stopped_vsup_reading_is_no_reading` 1, `the_identity_needs_eight_new_conversions` 2 (never converted: `DTC_HWID_SHORT`; stopped: identity accepted) | pass |
| `temp.c` ignoring the acquisition | 19 checks in 4 tests: `a_held_conversion_…` 4, `each_stopped_…` 7, `a_temperature_input_…` 7, `one_conversion_…` 1 | pass |
| `vdc.c` ignoring the acquisition | 12 checks: `a_stopped_v5gd_or_vofs_…` | pass |
| A constant temperature converted on schedule, 3 min | passes before and after (the guard against a "must change" rule) | pass |

Mutations — 18; 17 caught, one equivalent:

| # | Mutation | Caught by |
|---|---|---|
| M1 | the age check removed (`ti_acq` without `ti_stale`) | 60: `each_stopped_…` 32, `a_stopped_v5gd_or_vofs_…` 10, `an_acquisition_…` 5, `one_conversion_…` 4, `three_stopped_…` 3, `a_stopped_kl15_…` 2, `a_stopped_interlock_…` 2, `a_stopped_vsup_…` 1, `the_hold_…_wrap` 1 — second form: the first left two parameters unused (a compile error under −Werror) |
| M2a | the wrong timestamp: the task's time as the slow samples' stamp (the old defect) | 45: `each_stopped_…` 32, `one_conversion_…` 4, `three_stopped_…` 3, `a_stopped_kl15_…` 2, `a_stopped_interlock_…` 2, `a_stopped_vsup_…` 1, `the_hold_…_wrap` 1 |
| M2b | the wrong timestamp: the ISR's time as VOFS/V5GD's stamp | 10: `a_stopped_v5gd_or_vofs_…` |
| M2c | the wrong timestamp: the age of the stamp last taken, not of the read | 1160 in 142 tests (nothing is ever taken) |
| M3a | off by one: the hold + 1 µs | 13: `an_acquisition_…` 5, `each_stopped_…` 7, `the_hold_…_wrap` 1 |
| M3b | off by one: the hold − 1 µs | 2: `an_acquisition_…` (the host's tick grid cannot see 1 µs below the hold) |
| M3c | off by one period: `cal_temp_hold_ms` + 1 in the task | 8: `each_stopped_…` 7, `the_hold_…_wrap` 1 |
| M4 | EXPIRED not kept (the same stamp revives it, as ti_stale's age does near 2^32 µs) | 2: `an_acquisition_…` |
| M5 | `temp.c` takes a held sample again | 9: `each_stopped_…` 7, `a_held_conversion_…` 2 |
| M6 | `temp.c`: an expired channel stays valid | 35: `each_stopped_…` 25, `one_conversion_…` 4, `three_stopped_…` 3, `a_held_conversion_…` 2, `the_hold_…_wrap` 1 |
| M6b | `temp.c`: an expired channel keeps its open window | 16: `each_stopped_…` 14, `a_held_conversion_…` 2 |
| M7 | `vdc.c`: an expired reference leaves V_DC valid | 8: `a_stopped_v5gd_or_vofs_…` |
| M7b | `vdc.c`: an expired V5GD judged a loss (`v5gd_ok` false: the V5GD row) | 4: the same |
| M8 | KL15 / INTRLOK_N / VSUP: an expired sample passed on as its frozen code | 4: `a_stopped_interlock_…` 2, `a_stopped_kl15_…` 1, `a_stopped_vsup_…` 1 |
| M9 | the HVIL judges a held conversion again | **not caught — equivalent at the default**: the hold (10 ms) is not above the HVIL period (10 ms), so a held conversion at an evaluation is of the present drive level and never judged twice; it would count one conversion more than once toward FW-09's debounce only with a hold above the period |
| M10 | no `DTC_ADC_SLOW_STALE` | 38 in 9 tests |
| M11 | HW_ID: repeated conversions accepted | 2: `the_identity_needs_eight_new_conversions` |
| M12 | the V_DC row named `DTC_VDC_STALE` for an expired reference | 2: `a_stopped_v5gd_or_vofs_…` — survived its first run: the test now checks that the channels' own stale DTC is not set |

The host's 1 ms task spends simulated time only in its FS26 frames, at its start, so a test reads the task's check time as
the tick's end: the stopped channel is held at every tick that ends less than 10 ms after the freeze (the last at 8.97 or
9.03 ms) and withdrawn at the one that ends at 10 ms, exactly (M3c's extra period is seen). Found on the way: `sim_adc_freeze` froze only the stamp, so a stopped
channel's code followed the input — it had no user; it now keeps the code of the freeze. Not host-testable: the silicon
period of the slow list and the HW_ID wait (T-60).

### Follow-ups F243, F244

Two defects in the round-24 sources, each reproduced by a probe on them before the fix (contract FW-13 and §10n, README
"Round 24 — acquisition freshness of the slow channels", rows 6 and 7). Method: the live tree 438 tests / 5139 checks,
0 failed. "Before" restores, in a private copy of the live tree with the new tests, the round-24 `temp.c` fault chain
(`wire` never written — the new field did not exist) or `vdc.c`'s `ti_stale` line; each mutation is applied alone in that
copy, every object rebuilt (`make -B`), the suite run. Counts are failed checks; the tests named caught it. Result:
**443 tests, 5195 checks, 0 failed** in the default, −O2 (`-ffp-contract=off`) and ASan/UBSan flavours; 63 markers.

| Item | Before (failed checks) | After |
|---|---|---|
| F243 — a latched `TEMP_RATE` through an open or short sample | 12 in 3 tests: `a_latched_rate_fault_is_never_replaced_…` 4 (the latch replaced at the sample; valid again at the 200th in-range sample, the module maximum back on it), `a_rate_latched_module_channel_…` 4 (TMOD_U valid 200 ms after, the derating 0.544 → 1.000, 340 A rms; no DTC), `open_and_short_without_a_rate_latch_…` 4 (its `wire` checks only — its fault and validity checks pass: the rules without a latch are unchanged) | pass |
| F244 — a stopped V_DC channel across 2^32 µs of its stamp's age | 4: `a_stopped_channel_stays_stale_across_2e32_us_of_its_age` (valid 199 µs of the ±150 µs window, each channel, both epochs); `a_stopped_v_dc_converter_is_stale_…` passes before (the hold at every tick, the §6 row named `DTC_VDC_STALE`, the recovery: unchanged behaviour away from the wrap) | pass |

Mutations — 13, each caught:

| # | Mutation | Caught by |
|---|---|---|
| G1 | the latch guard removed (`fault` = `wire` at every sample) | 14: `a_rate_latched_module_channel_…` 6, `a_latched_rate_fault_…` 4, `open_short_rate_plausibility` 2, `a_held_conversion_…` 1, `a_fast_ramp_a_step_open_and_short_still_trip` 1 |
| G2 | wrong priority: an open or short outranks the latch (the round-24 order) | 8: `a_rate_latched_module_channel_…` 4, `a_latched_rate_fault_…` 4 |
| G3 | wrong priority the other way: open and short latched like RATE | 8: `open_and_short_without_a_rate_latch_…` 6, `board_motor_and_over_temperatures_are_recorded_and_pass` 2 |
| G4 | the invalid verdict from the sample, not the latch | 7: `a_rate_latched_module_channel_…` 2, `a_latched_rate_fault_…` 2, `open_short_rate_plausibility` 1, `a_held_conversion_…` 1, `a_fast_ramp_…` 1 |
| G5 | `wire` not recorded while the latch holds | 6: `a_rate_latched_module_channel_…` 4, `a_latched_rate_fault_…` 2 |
| G6 | `wire` never cleared by an in-range sample | 12: `open_and_short_without_a_rate_latch_…` 6, `board_motor_…` 2, `a_rate_latched_module_channel_…` 2, `a_latched_rate_fault_…` 2 |
| G7 | `DTC_TEMP_OPEN_SHORT` from `fault`, not `wire` | 4: `a_rate_latched_module_channel_…` |
| G8 | `DTC_TEMP_OPEN_SHORT` not reported | 4: the same |
| V1 | `ti_stale` alone (the round-18 line: "before") | 4: `a_stopped_channel_stays_stale_…` |
| V2 | `ti_acq` not sticky (EXPIRED revived by the same stamp) | 6: `a_stopped_channel_stays_stale_…` 4, `an_acquisition_is_new_held_or_expired` 2 |
| V3 | the slow list's hold (`cal_temp_hold_ms`) for V_DC | 13: `a_stopped_channel_stays_stale_…` 8, `a_stopped_v_dc_converter_…` 2, `a_channel_stamped_after_the_check_time_is_fresh` 2, `stale_channel_invalid` 1 |
| V4 | a held stamp read as stale (NEW only) | 12: `a_stopped_channel_stays_stale_…` 4, `disagreement_over_5_percent_invalidates_both` 2, `a_stopped_v_dc_converter_…` 2, `nominal_pair_valid` 1, `vofs_out_of_window_…` 1, `v5gd_out_of_window_…` 1, `the_feed_forward_never_leaves_the_solved_margin` 1 |
| V5 | one acquisition state for both channels | 4: `a_stopped_channel_stays_stale_…` |

The open or short under a latch is surfaced by a DTC, not by DID 0xF205: its two bits per channel keep the latched
verdict (3, rate), and a DID layout change would reach the tool. `DTC_TEMP_OPEN_SHORT` is set for any channel reading open
or short, latched or not, so its meaning does not depend on the latch; the channel's group DTC and the derating are
unchanged. Not changed: the latch lives in RAM, so an MCU reset inside the key cycle (`app_init`, `temp_init`) clears it
like every temperature state — as before these fixes.
