# Traction Tool — simulator bridge protocol and vehicle CAN-FD codec

This is the interface specification for the Qt application (or any other client). It has two parts:

- **Part A — the simulator bridge** (`tool/bridge/sim_bridge`): a child process that runs the traction-inverter
  firmware on its host simulation with a physical plant, and speaks newline-delimited JSON on stdin/stdout.
- **Part B — the vehicle CAN-FD and UDS codec**: the frames the firmware exchanges with the VCU/BMS and the
  diagnostic tester, as defined by `firmware/src/comms/can_cmd.h` / `uds.h` / `uds_diag.h` and
  `docs/firmware-contract.md` (FW-11, FW-08b, FW-24, FW-32, FW-38 to FW-41). The same information is machine-readable
  in `tool/protocol/can-frames.json`, with test vectors produced by the firmware's own encoders.

Firmware basis of this document: `TI_FW_ID` 0x0A0F0015 (the round-23 working tree of 2026-09-26: INV_STATUS is
20 bytes; the round-23 fixes: the bridge's ISO 15765-2 tester, the periodic stream, the VCU vehicle speed, 0x14 and
the bench key). The machine-readable exports record the exact firmware tree they were generated from; when the firmware
changes, regenerate them (section C) — the generator fails loudly if this document's tables no longer match the
firmware's encoders.

Conventions: all JSON is UTF-8 (the bridge emits ASCII only). Numbers are JSON numbers (IEEE doubles; the firmware's
values are single precision, printed with 7 significant digits). A non-finite value is `null`. Booleans are
`true`/`false`. "sim" = the simulated world; "firmware" = values the firmware itself computed.

---

## Part A — the simulator bridge

### A.1 What it runs

The firmware's host build has no free-running scheduler: its tests call the public entry points on the host
simulation's clock (`firmware/tests/harness.c`). The bridge does the same with the **unmodified** firmware sources
(`firmware/src/**` and `firmware/src/platform/host/*`), compiled with the firmware Makefile's host flags:

| Entry point | When |
|---|---|
| `app_init()` | every power-up (start, `reboot`, `provision`, key-on after LPOFF) |
| `app_isr_current()` | every current-loop trigger: 2 × f_sw (20 kHz for the 8XX SiC SKU at 10 kHz), on its own exact grid |
| `app_isr_fault()` | from the host simulation's fault hook, when an eFlexPWM fault flag sets (300 ns latency) |
| `app_task_1ms()` + `app_idle()` | every 1 ms of simulated time, on a second exact grid |

A trigger that falls inside a long task is dropped (on the target it would preempt the task) — the harness rule.
Behind the HAL sits the firmware repository's own card model (FS26, safety chain, eFlexPWM with REG_PROT, ADC
watchdogs, SDADC + eDMA resolver chain, CAN, NVM) and, in the bridge, a physical plant:

- **PMSM on a dyno.** The calibration record's motor (the S6 screening motor: Ld = Lq = 0.35 mH, Rs = 25 mΩ,
  ψ = 0.15 Wb, 4 pole pairs). The dyno holds the shaft speed (setpoint + ramp); the machine's torque never changes the
  speed. Electrical model driven by what the switches actually do (read from the card model): modulating = average
  phase voltages from the duty the firmware wrote **one current-loop period earlier** (double-update PWM; the output
  stays off until the first written duty loads) minus the dead-time voltage loss; ASC = phases shorted; all switches
  off = three-phase diode bridge (back-EMF rectifies into the link above V_DC). Implicit dq integration (10 µs
  steps; 1 µs through the diodes).
- **DC link.** C_nom of the SKU; pack (open-circuit 750 V 8XX / 400 V 4XX, 30 mΩ) behind the main contactor or a
  precharge resistor (τ 100 ms); passive bleeder; active discharge string when the firmware drives QDIS. The
  inverter's losses are drawn from the link while modulating.
- **Thermal.** Three module NTCs (slightly different paths), two board NTCs, the motor sensors, first-order against
  the coolant temperature; losses from the phase current, V_DC and f_sw.
- **Vehicle.** A VCU + BMS relay sending VCU_CMD and VCU_BMS every 10 ms through the firmware's own encoders
  (E2E CRC, alive counters), with bench arm/disarm sequences (A.6); VCU_CMD carries the vehicle speed (valid, 0 km/h:
  the dyno bench's vehicle at rest). The diagnostic bus for UDS, through an ISO 15765-2 tester (A.4.5): segmented
  responses under its flow control, the periodic frames (A.5.8).
- **Provisioning.** What the EOL/HIL rig does: the FLT → PWM fault route bound, a sealed validation record for this
  image/SKU/card in NVM (FW-24), the FS26 PROG_ID bound (FW-12), a calibration record with a motor ID (FW-20) — and
  the bench SecurityAccess key, as a product build's `TI_UDS_KEY_FN` (the default firmware build has none).
  Each can be withdrawn to show the fail-closed behaviour (`provision`). And, at the first power-up, the FW-38 boot
  record (`firmware/src/boot/boot.h`): IDLE, target = the SKU, anti-rollback counter 1 — what the firmware's image
  verification checks the target and the security version against (without it every image fails: TARGET). There is
  no bootloader: when the firmware resets itself (ECUReset `11 01`, `hal_sys_reset`) the bridge restarts the card as
  at `reboot` (a `log` "ECUReset (FW-38): …", then `hello`), and the record stays as the application left it
  (ACTIVATE after `31 01 F0 38`: a staged image is verified and activated, never installed or run; one activation per
  bridge process, a second one answers NRC 0x22).

### A.2 Process model

```
sim_bridge [--sku 8xx_sic|8xx_igbt|4xx_igbt|4xx_sic] [--rate HZ] [--time FACTOR] [--paused] [--script FILE]
sim_bridge --golden
```

| Option | Default | Meaning |
|---|---|---|
| `--sku` | `8xx_sic` | the parameter set (`firmware/include/params_<sku>.h`) and identity resistor |
| `--rate HZ` | 100 | telemetry frames per second of **simulated** time (1–1000; rounded to a whole ms period) |
| `--time FACTOR` | 1 | simulated seconds per wall second (0–100); **0 = as fast as possible** |
| `--paused` | off | start paused (advance with `step`) |
| `--script FILE` | — | commands to run, one JSON object per line (`#` lines are comments); see A.8 |
| `--golden` | — | print the codec test vectors (A.5.7) and exit |

- **stdin**: one command per line (A.4). Lines longer than 4095 bytes are dropped. Blank lines are ignored.
- **stdout**: one JSON object per line, flushed per line (A.5). Nothing else is written to stdout.
- **stderr**: only the usage text on a bad option.
- **Exit**: status 0 when stdin reaches end of file (close the pipe to stop the bridge); 2 on a bad option or an
  unreadable script. The bridge never exits on its own otherwise.
- **Start-up**: a `log` line ("power-up 1 …") and a `hello` line, then telemetry. The firmware boots through
  section 9 (INIT → SENSOR_SELFTEST → GATE_SELFTEST → VEHICLE_HANDSHAKE/PRECHARGE_WAIT) in about 0.5 s of simulated
  time; from then on `arm` works.
- **Speed**: ≈ 12–18 × real time on an Apple M-series host while modulating at 20 kHz (the `sim.rt` field reports the
  achieved factor; if the host cannot keep up with `--time`, the bridge re-anchors and logs a warning).

### A.3 Time

| Quantity | Where | Meaning |
|---|---|---|
| `t_ms` | every line | **session time**, ms (fractional). Simulated time since the bridge started; monotonic across power cycles (a reboot does not rewind it). Starts at ≈ 1000. |
| `state.fw_ms` | `tel` | the firmware's own `hal_time_ms()`. Restarts at 1000 on every power-up. DTC first/last stamps are in this time base. |
| `state.boot` | `tel`, `hello` | power-up counter (1 = the first). |
| telemetry period | `--rate`, `rate` | in simulated time: at `--time 0` frames come as fast as the simulation runs. |
| `in_ms` | commands | a delay in simulated time (A.4.1). |
| `hold_ms` | `torque` | a deadman in **wall-clock** time (A.4.3). |

A DTC stamp converts to session time with `t_ms − state.fw_ms + dtc_ms` (valid within the same `boot`).

### A.4 Commands (stdin)

A command is a **flat** JSON object (string / number / boolean / null values only; no nested objects or arrays),
at most 24 keys. `cmd` names the command. Optional on every command:

- `"id"`: a number, echoed in the command's `ack` (use it to correlate).
- `"in_ms"`: > 0 schedules the command that many ms of **simulated** time after it is read; it is acknowledged when
  it runs (not when it is received). Up to 128 commands may be waiting. Commands read in the same batch (e.g. a
  script, or several lines written at once) share the same reference time.

Every command produces exactly one `ack` line (A.5.3) — `uds` when its response is complete (a segmented one with
its last consecutive frame) or 50 ms after the last frame; `info`,
`reboot`, `provision` and `params` print their extra line first. An unknown command, a missing or out-of-range field
gives `"ok": false` with `err`; nothing is changed then.

#### A.4.1 Session

| cmd | Fields | Effect |
|---|---|---|
| `ping` | — | ack only. |
| `info` | — | prints `hello` again (current values), then the ack. |
| `rate` | `hz` > 0 | telemetry rate (simulated time), 1–1000 Hz, rounded to a whole ms period. |
| `time` | `factor` 0–100 | time factor (0 = as fast as possible). Re-anchors the wall clock. |
| `pause` | `on` bool (omitted = toggle) | stops/resumes the simulation (telemetry stops too). |
| `step` | `ms` 1–10000 (default 1) | runs that many 1 ms ticks now (meant for pause; telemetry is emitted per the rate). |
| `reboot` | — | power cycle: the whole card model resets (NVM kept: key cycle counter, validation record, DESAT and self-test records, service lock, and a calibration record committed by FW-39 — which the firmware then loads instead of the bench's sealed default, so the committed values are in force from this key cycle and `hello.calib` shows them), retained RAM lost, `app_init()`; prints `log` + `hello`. Plant speed and coolant are kept, the VCU returns to idle with the contactors open. |
| `key` | `on` bool (default true) | KL15. Off: the firmware goes to SAFE_POWERDOWN (discharge if the contactors are open, then FS26 LPOFF → OFF). On: after LPOFF or in OFF this is a power-up (as `reboot`); otherwise KL15 returns. |
| `provision` | any of `val`, `route`, `otp`, `cal`, `sa_key` (bool) | changes the EOL/HIL provisioning, then power-cycles (prints `hello`). `val`: the FW-24 validation record present; `route`: FLT → PWM fault route bound; `otp`: `cal_fs26_prog_id` bound (else 0xFFFF, FW-12); `cal`: the calibration record bound to a motor ID (FW-20). Any of these false ⇒ the firmware refuses to arm (fail closed) and the `arm` checklist shows which item. `sa_key` (round 23, default true): the bench SecurityAccess key (not KL15: that is the `key` command), `key[i] = seed[(i + 1) mod 4] XOR (0xA5 + i)`, as a product build's `TI_UDS_KEY_FN`; false = the default firmware build: every `27 01` answers NRC 0x22, so `dtc_clear`, the 0xF010 routine and FW-39's service mode are refused (fail closed). |

#### A.4.2 Vehicle (the bench VCU + BMS relay)

| cmd | Fields | Effect |
|---|---|---|
| `arm` | — | starts the arm sequence (A.6): enable request on, wait for PRECHARGE_WAIT (self-test done), precharge, close the contactors. The firmware then arms (ARMED_ZERO_TORQUE, MCU_GATE_EN). |
| `disarm` | — | torque request 0, enable off; once the applied torque is below 0.5 N·m and \|n\| < n_x, the contactors open (the FW-08 zero-torque opening). While \|n\| ≥ n_x it waits (`vcu.note` says why): opening at speed would be a battery-path loss. |
| `torque` | `nm` (\|nm\| ≤ 3000), optional `slew_nm_s` ≥ 0, optional `hold_ms` > 0 | the VCU torque request (sign: + accelerates in + speed; the firmware's direction interlock applies per gear). The VCU slews the value in its frames at `slew_nm_s` (default 3000 N·m/s; 0 = steps — round 23: the firmware slews a fresh command itself at `cal_torque_slew_nm_s`, 2000 N·m/s). `hold_ms`: see A.4.3. Ignored (0) while disarming. |
| `gear` | `gear` "N" "D" "R" "P" | the gear in VCU_CMD (default D). D never drives backwards, R never forwards, N/P = zero torque; changes only below `cal_dir_change_rpm`. |
| `enable` | `on` bool | the enable request bit (arm sets it; disarm clears it). |
| `fault_reset` | — | the VCU fault-reset bit for 100 ms. The firmware resets latched §6 rows only below n_x and under its own conditions. |
| `retry_auth` | — | the DESAT-retry authorisation bit for 3 s (FW-15: one retry per key cycle, ≥ 1 s after the DESAT, below n_x, at reduced torque). |
| `discharge` | — | the discharge-request bit for 6 s (FW-17: QDIS fires only with the contactors reported open; the ack's `info` says so if they are not). |
| `coolant` | `c` −40…120 | the coolant temperature: the plant's coolant and the VCU_CMD coolant field. |
| `vspeed` | `kmh` 0…655.35, optional `valid` bool (default true) | round 23: the vehicle speed in VCU_CMD (b4 bit 5, b6–7; B.3). FW-39's service mode needs it valid and ≤ 0.5 km/h. Default: valid, 0 km/h (the dyno bench's vehicle at rest). |
| `bms` | any of `chg_kw` ≥ 0, `dis_kw` ≥ 0, `pack_v` (0, 1000) | BMS charge (regen) and discharge power limits relayed in VCU_BMS; `pack_v` = the pack's open-circuit voltage in the plant. Default 100 / 250 kW, 750 V (8XX). |
| `vcu_model` | `cmd_frames` bool, `bms_frames` bool | false = the built-in VCU stops sending that frame, so an **external VCU** (your CAN codec, through `can_rx`) drives the firmware. With `cmd_frames` false the plant's contactors follow the contactor field of the VCU_CMD frames you inject, and the arm/disarm sequences are off. |
| `can_rx` | `can_id` (11-bit), `hex` (payload bytes, e.g. "7050E803035A0000"; spaces/colons allowed), optional `bus` (0 vehicle, 1 diagnostic) | injects one raw frame into the firmware's receive queue (it runs the E2E and counter checks). |
| `can_tap` | `on` bool (default true) | every frame the firmware sends (INV_STATUS every 10 ms, UDS responses and flow controls, periodic frames) is also printed as a `can` line (A.5.5). |

#### A.4.3 Torque deadman (`hold_ms`)

`{"cmd":"torque","nm":120,"hold_ms":200}` applies 120 N·m and arms a **wall-clock** deadman: unless another
`torque` command arrives within 200 ms, the request returns to 0 N·m (slewed). A client implements hold-to-apply by
re-sending the command every ≤ 100 ms while the operator holds the control; if the client freezes or dies, the
torque drops by itself. A `torque` command without `hold_ms` cancels the deadman (the value is held).

#### A.4.4 Plant

| cmd | Fields | Effect |
|---|---|---|
| `speed` | `rpm` (\|rpm\| ≤ the motor's n_max, 16 000), optional `ramp_rpm_s` (default 2000, ≤ 20 000) | the dyno speed setpoint; the shaft ramps to it. The firmware has no speed command (VCU_CMD carries torque only): speed is the load machine's. |

#### A.4.5 Service and parameters

| cmd | Fields | Effect / ack extras |
|---|---|---|
| `param_get` | `name` (a `cal_*` row) | ack: `name`, `ptype` ("f32" "u32" "u16" "u8"), `value`, `default`, `min`, `max`. |
| `param_set` | `name`, `value` | writes one `cal_*` value into a copy of the live set, runs the firmware's own `ti_params_validate()` on the copy and applies it only if it passes. Integer types take whole numbers only. Ack: `name`, `ok`, `violations` (the count `ti_params_validate()` returned), `value` (the value in force after the command), `err` when refused. The change is live for everything the firmware reads at run time; values used only at initialisation (e.g. the current-loop gains from `cal_fc_fraction`) take effect at the next `reboot`. Only the rows of `firmware/include/cal_ranges.h` are writable. |
| `param_reset` | — | all parameters back to the build's defaults (with the provisioned PROG_ID). |
| `params` | — | prints a `params` line (A.5.6), then the ack. |
| `asc` | `on` bool (default true) | **simulator test hook.** On: PWM-ASC entered through the firmware's own §4c sequence (`br_enter_pwm_asc`) — only with the bridge armed (idle or modulating). Off: the firmware's MCU-commanded exit (`br_exit_asc`), allowed only below n_x (FW-06a). The vehicle interface has no ASC command: on the real inverter ASC is only a §6 decision. |
| `dtc_clear` | — | round 23: the tester's sequence through the image's own services (no longer a hook) — SecurityAccess with the bench key (`27 01`, `27 02`), then `14 FF FF FF` (FW-40). The image's gates apply: the key provisioned, HV absent and the bridge disarmed — else `ok` false, `err` naming the step and its NRC (0x22). The DTCs a clear keeps stay: the DESAT class, the service lock and every DTC that forbids arming for the key cycle (a clear releases no latch). Refused while `uds` requests are in flight. The sequence runs the simulation for the few ms it takes (≤ 100 ms per request), also while paused. |
| `uds` | `hex`: the request's single frame, PCI first — classic ("02 27 01") or the CAN-FD escape format ("00 0A 22 F2 00 …", up to 64 bytes: one frame; bytes beyond 64 are ignored). A longer request is segmented by the client: its first frame as a `uds` (the ack carries the ECU's flow control), its consecutive frames as `can_rx` with `bus` 1 — each block's last one as a `uds`, whose ack is the next flow control or the response (the Qt tool's `BridgeTransport`) | sent to 0x7E1 on the diagnostic bus, padded with 00 to the CAN-FD length (8, 12, 16, 20, 24, 32, 48, 64). Round 23: the bridge is an ISO 15765-2 tester — a first frame on 0x7E9 is answered with flow control ContinueToSend (BS 0, STmin 0) and the consecutive frames are reassembled, their sequence numbers checked. Ack when the response is complete: `msg` (the UDS payload, hex spaced, e.g. "7F 27 22"), `frames` (1 = a single frame), `rsp` (the first frame as received, hex spaced, e.g. "03 7F 27 22 AA AA AA AA"), `rsp_id` (0x7E9 = 2025); `ok` false with `err` for a broken segmented response (a sequence error, or abandoned by the ECU); `rsp` "" with `info` if nothing came within 50 ms of the request or of the last frame. The ECU's flow control for a first frame the client sent itself is reported as that request's response (`msg` empty). Up to 8 in flight; paused, the command runs the simulation until the response (at most 200 ms). |

#### A.4.6 Fault injection (`inject` / `clear`)

`{"cmd":"inject","fault":"<name>"}` — `{"cmd":"clear","fault":"<name>"}` or `{"cmd":"clear","fault":"all"}`.
Persistent injections stay until cleared and are listed in `plant.inject`; events happen once.

| fault | Kind | What the simulation does | What the firmware should do (the contract) |
|---|---|---|---|
| `desat_hs` | event | a high-side driver latches DESAT (FLT_HS) in the card model | hardware SPO; FW-15; FAULT; PWM-ASC after the ≥ 1.5 ms reset at n ≥ n_x (§6) |
| `desat_ls` | event | a low-side driver latches DESAT (FLT_LS) | SPO at every speed; FAULT |
| `overcurrent` | event | two phase-U samples beyond 1.3 × the FW-05 trip (a sensed spike) | the ADC compare trips: high sides off, the latched "control lost" row |
| `overtemp` | persistent, optional `dt_c` 1–120 (default 60) | coolant-flow loss: the module temperatures rise by `dt_c` at 2 K/s | FW-04 derating (90 → 115 °C on the module NTC); DTC_OVERTEMP at the band's end (round 23). The FW-13 rate check (round 23: over a window, with a deadband) passes a 2 K/s rise |
| `vdc_sense_loss` | persistent | V_DC channel 1 reads the AMC1311 fail-safe level (0.1 V at the pin) | FW-07: V_DC invalid, HV state UNKNOWN, the "V_DC invalid" row |
| `battery_loss` | event | the main contactor opens under load; the VCU reports it open | FW-08: zero current below n_x, then SPO; LS-ASC at speed; FW-06 if the link reaches the OV trip |
| `resolver_loss` | persistent | resolver output at 20 % amplitude (a broken sin/cos wire) | FW-10: resolver invalid — round 23: §6 decides on the speed bound (`motion.speed_bound_rpm`, growing at `cal_speed_accel_max_rpm_s`): SPO while it stays below n_x, LS-ASC once it reaches n_x |
| `can_loss` | persistent | the VCU and BMS frames stop | FW-11: command-lost ramp; armed ⇒ also the battery-path row |
| `hvil_open` | persistent | the HVIL signature reads open | FW-09: ramp to zero within 100 ms |
| `v5gd_loss` | persistent | V5GD hovering at 4.5 V (only its sense pin sees it) | §4c: SPO, ASC_CLR, V_DC invalid, no arming |
| `lv_overvoltage` | persistent | KL30 at the FS26 VSUP = 35 V | FW-33: information for 500 ms, then the command-lost ramp |

The BMS limit change is the `bms` command (FW-11: zero regen on `chg_kw` 0 or on a BMS timeout).

### A.5 Output lines (stdout)

Every line has `"type"`. Unknown types or fields must be ignored by the client (the protocol grows by addition).

#### A.5.1 `hello`

Printed at start-up, on `info`, `reboot` and `provision`.

| Field | Type | Meaning |
|---|---|---|
| `bridge` | string | "traction-tool-bridge 1" (protocol generation) |
| `fw_id` | string | `TI_FW_ID` of the compiled image, "0x0A0F0015" |
- `root` (round 23): `{kind, key_id}` — the image verifier's root of trust (`kind` "test" on the host build — the tool shows a banner, an EOL station refuses it; "otp"/"build" on a target; `key_id` = SHA-256(public key)[0..3]); DID 0xFD23 carries the same; DID 0xFD24 carries the FW-38 boot record (`hello.boot_rec` over the bridge); DID 0xFD25 the record's motor data (ψ, pole pairs, the scalars — `hello.motor`/`hello.calib` over the bridge); DID 0xFD26 the active record's FW-45 inductance maps (the read-back of a committed map after the key cycle).
| `sku`, `sku_name` | int, string | 1 = 8XX SiC, 2 = 8XX IGBT, 3 = 4XX IGBT, 4 = 4XX SiC |
| `dtc_count` | int | `DTC_COUNT` (ids 1 … dtc_count−1) |
| `dtcs` | string[] | DTC names in id order (index = id, `dtcs[0]` = "DTC_NONE"), from the compiled `dtc.h` |
| `fsw_hz`, `isr_hz`, `fc_hz` | numbers | switching frequency, current-loop rate (2 f_sw), current-loop crossover |
| `n_x_rpm` | number | the §6 crossover speed (line-line back-EMF peak = OV trip) |
| `carrier_hz` | int | resolver excitation carrier (10 kHz) |
| `states` | string[] | state-machine names by value (A.7) |
| `ss_rows`, `ss_actions` | string[] | §6 row and action names by value (A.7) |
| `limits` | object | `vdc_min_v`, `vdc_max_v`, `ov_trip_v`, `i_pk_rms_a`, `i_cont_rms_a`, `i_oc_trip_a`, `p_peak_w`, `p_cont_w`, `dead_time_ns` |
| `motor` | object | `ld_h`, `lq_h`, `rs_ohm`, `psi_wb`, `pp`, `id_demag_a`, `n_max_rpm` (the calibration record's motor) |
| `calib` | object | the FW-20 record: `layout_version`, `size` (bytes), `err` (CAL_ERR_* bits, A.7), `motor_id`, `fsw_hz`, `sku`, `serial`, `crc32`, `fields` [{`name`, `offset`, `size`}] (the byte layout of `calib_t`), `isns` [3 × {`offset_v`, `gain_v_per_a`, `sign`}], `vdc` [2 × {`gain`, `offset_v`}], `rslv` {`ratio_nom`, `exc_code_per_vpp`, `sin_gain`, `cos_gain`, `phase_trim_deg`, `zero_rad`, `motor_pp`, `resolver_pp`}, `mt_type` (0 PT1000, 1 NTC), `mtpa_n` |
| `validation` | object | the FW-24 EOL/HIL record in NVM: `present`; if present `layout`, `sku`, `flags`, `fw_id`, `ovp_chain_ns`, `serial`; always `valid_flags` (what the firmware accepts from it for this image/SKU/card) and `budget_us` (FW-06, 15.6) |
| `selftest_record` | object | the FW-16 NVM record: `present`; `key_cycle`, `passed`, `failed_step` |
| `provision` | object | `val`, `route`, `otp`, `cal`, `sa_key` (A.4.1) |
| `boot_rec` | object | the FW-38 boot record in NVM (A.1; no DID carries it): `state` (0 IDLE, 1 ACTIVATE, 2 INSTALL, 3 TRIAL, 4 CONFIRMED, 5 COMMIT, 6 RESTORE), `last` (the last activation's outcome, `boot_last_t`), `last_err` (`img_result_t`), `target`, `sec_counter` (anti-rollback), `lkg_valid` |
| `evidence` | int | arming evidence present (ARM_EV_* bits, A.7) |
| `key_cycle`, `boot` | ints | |
| `params` | array | the writable parameters as compiled: [{`name`, `type`, `min`, `max`, `value`, `default`}] (see `tool/protocol/params.json` for units, groups and descriptions) |

#### A.5.2 `tel` — one telemetry frame

Frames come every `1000/rate` ms of simulated time. Everything outside `plant`, `vcu` and `sim` is what the
**firmware** computed (the same information a HIL rig would read out); `plant` is the simulator's truth, which the
firmware never sees directly.

Top level: `v` (1), `seq` (frame counter), `t_ms` (A.3), `src` ("sim"), `can` (the last INV_STATUS frame the
firmware sent, hex, 20 bytes = 40 characters; decode with Part B).

**`state`**

| Field | Type | Meaning |
|---|---|---|
| `sm` | int | operating state (A.7) |
| `bridge` | int | bridge mode: 0 disarmed (SPO), 1 idle (EN high, PWM off), 2 modulating, 3 PWM-ASC |
| `hv` | int | HV state reported to the vehicle (FW-18): 0 unknown, 1 safe (< 60 V), 2 present |
| `self_test_done`, `fault`, `derate`, `zero_torque`, `discharging`, `precharge_refused`, `keep_hv`, `no_safe_state`, `service_required`, `speed_limit_req` | bool | the INV_STATUS flags (Part B) |
| `no_arm` | bool | a failure forbids arming for this key cycle |
| `evidence` | int | arming evidence present (ARM_EV_* bits) |
| `key_cycle`, `boot`, `fw_ms` | int | A.3 |
| `gate_en` | bool | MCU_GATE_EN output |
| `drv_en` | bool | the DRV_EN chain (card model: FS0B · MCU_GATE_EN · RDY · fault latch) |
| `asc_latch` | bool | the hardware ASC latch |
| `pwm` | int | eFlexPWM mode: 0 off, 1 ASC (low sides on), 2 modulating |

**`motion`**

| Field | Unit | Meaning |
|---|---|---|
| `speed_rpm` | rpm | the firmware's speed (resolver; held at its last value when invalid, see `speed_known`) |
| `speed_valid` | bool | resolver valid now |
| `speed_known` | bool | speed valid, or (round 23) the resolver lost with `speed_bound_rpm` still below n_max; false ⇒ §6 uses the n ≥ n_x column |
| `speed_bound_rpm` | rpm | round 23: the upper bound of \|n\| the §6 decisions use — \|speed\| + `cal_speed_accel_max_rpm_s` × the time since the resolver was last valid (\|speed\| while it is valid) |
| `n_x_rpm` | rpm | §6 crossover speed |
| `torque_req_nm` | N·m | the VCU request as received (before the direction interlock and the limits) |
| `torque_cmd_nm` | N·m | the firmware's torque command after the interlock, limits, ramps and the DC-link trim |
| `torque_est_nm` | N·m | 1.5 · pp · (ψ · iq + (Ld − Lq) · id · iq) from the measured currents |
| `gear` | string | the gear as received: "N" "D" "R" "P" |

**`foc`** (amplitude-invariant dq: dq amperes = phase peak amperes)

| Field | Unit | Meaning |
|---|---|---|
| `id_a`, `iq_a` | A | measured dq currents (phase currents parked with the resolver angle at their sample time) |
| `id_ref_a`, `iq_ref_a` | A | the current loop's references (hold their last value when not modulating) |
| `vd_v`, `vq_v` | V | the loop's voltage references (last step) |
| `vmax_v` | V | voltage-circle limit `cal_mod_index_max · V_DC / √3` |
| `sat` | bool | the voltage limit clipped the last step |
| `ia_a`, `ib_a`, `ic_a` | A | calibrated phase currents (instantaneous) |
| `i_rms_a` | A rms | √((id² + iq²)/2) |
| `duty_a`, `duty_b`, `duty_c` | 0–1 | the last duties the loop computed |
| `p_elec_w` | W | 1.5 · (vd · id + vq · iq) from the loop (0 when not modulating) |
| `isns_valid` | bool | phase-current sensing valid (all three channels, fresh, Σi plausible) |

**`link`**

| Field | Unit | Meaning |
|---|---|---|
| `vdc_v`, `vdc_valid` | V, bool | the firmware's V_DC and validity (FW-07) |
| `v_ch1`, `v_ch2` | V | the two V_DC channels |
| `vofs_v`, `v5gd_v` | V | receiver offset (0.475–0.525 V window) and gate-logic supply (4.75–5.25 V) |
| `v_pack_v` | V | pack voltage received in VCU_BMS |
| `contactors` | int | contactor state received in VCU_CMD: 0 invalid, 1 open, 2 precharge, 3 closed |
| `vsup_v` | V | KL30 at the FS26 VSUP (FW-33) |
| `ign_on` | bool | KL15 (debounced) |

**`temps`** (°C): `mod_u_c`, `mod_v_c`, `mod_w_c` (module NTCs), `mod_max_c` (hottest valid; −273 when none),
`mod_valid` (all three valid), `board_h_c`, `board_a_c`, `motor1_c`, `motor2_c`, `coolant_c` (as received; `null`
when the VCU reports 0xFF), `derate_start_c`, `derate_end_c` (the FW-04 band in force).

**`limits`**

| Field | Unit | Meaning |
|---|---|---|
| `derate` | 0–1 | thermal derating factor (FW-04) |
| `derate_active` | bool | derating in force (the DERATE state) |
| `coolant_factor` | 0–1 | peak allowance from the coolant temperature |
| `i_limit_rms_a` | A rms | current allowance now |
| `t_motor_nm`, `t_regen_nm` | N·m | torque magnitudes allowed now (FW-03 envelope, BMS power limits, derating) |
| `peak_used_s`, `peak_exhausted` | s, bool | the 30 s peak budget (FW-04) |
| `p_chg_w`, `p_dis_w` | W | BMS limits as received |
| `torque_max_nm` | N·m | `cal_torque_max_nm` |

**`safety`**

| Field | Type | Meaning |
|---|---|---|
| `rows` | int | active §6 rows, bit = row value (A.7) |
| `latched` | int | rows that need a VCU fault reset |
| `action` | int | the combined §6 action (A.7; 0 when no row is active) |
| `row` | int | the row that decided (10 = none) |
| `asc_permitted` | bool | LS-ASC may be (re-)entered |
| `hvil` | int | HVIL status: 0 unknown, 1 closed, 2 open, 3 short to ground, 4 short to supply, 5 implausible |
| `rule_a`, `rule_b` | bool | §6 energy rules for the deciding row |

**`wd`**

| Field | Meaning |
|---|---|
| `fs26_state` | FS26 FS_STATES: 9 INIT_FS, 10 SAFETY_OUT_NOT_RELEASED, 11 NORMAL |
| `fs26_wd_err` | FS26 watchdog error counter (FS0B at WD_ERR_LIMIT 2) |
| `fs26_refresh` | good watchdog answers so far |
| `fs0b`, `fs1b` | FS26 safety outputs asserted |
| `isr_age_us` | µs since the current-loop ISR last ran (FW-31 compares it with `cal_isns_stale_us`) |
| `n_isr`, `wdog_kicks` | counters |
| `cmd_fresh`, `bms_fresh` | VCU_CMD / VCU_BMS within their timeouts |
| `can_crc`, `can_frozen`, `can_jump` | rejected vehicle frames by cause (counters) |
| `rslv_valid`, `rslv_amp`, `rslv_mon_vpp`, `rslv_err` | resolver validity, normalised amplitude (1 nominal), excitation at the monitor (V pp), tracking error |

**`arm`** — the arming checklist, computed by the bridge from the firmware's state (all must be true to arm):

| Field | True when | Requirement |
|---|---|---|
| `cal` | the calibration record passes `calib_check()` (`hello.calib.err` = 0) | FW-20 |
| `val` | the EOL/HIL validation record proves FAULT_ROUTE and OVP_ROUTE for this image/SKU/card | FW-24 |
| `otp` | `cal_fs26_prog_id` bound and no FS26 PROG_ID / OTP / debug / INIT read-back / SPI / FS1B-short DTC | FW-12 |
| `platform` | ROUTE_BOUND, CONFIG_MATCHES, PROTECTION_LOCKED read back | FW-24 |
| `identity` | no HW_ID / SKU-mismatch DTC | FW-01, FW-02 |
| `params` | no DTC_PARAMS_INVALID and a current-loop gain set exists | FW-20, §2 |
| `hvil` | HVIL closed | FW-09 |
| `no_fault` | no §6 row active, not in FAULT, no key-cycle arming ban | §6 |
| `self_test` | FW-16 passed (or its stored pass accepted) | FW-16 |
| `precharge` | FW-19 verdict OK | FW-19 |
| `gate_power` | RDY_HS and RDY_LS up | FW-14 |
| `sensors` | resolver, V_DC and currents valid, current offsets checked, a module NTC valid | §9 step 3 |
| `vcu` | VCU_CMD fresh | FW-11 |
| `contactors` | contactors reported closed | §9 |
| `armed` | MCU_GATE_EN high (the result) | §9 step 8 |

**`dtc`** — every DTC that has occurred since the last clear: an array of `[id, status, occurrences, first_ms,
last_ms]`. `id` indexes `hello.dtcs`; `status` is the ISO 14229-1 status byte (bit 0 testFailed = active; A.7);
times are firmware ms (`state.fw_ms` base). The UDS code is `0xD10000 | id`. Descriptions and the firmware's
response per DTC: `tool/protocol/dtcs.json`.

**`plant`** (simulator truth)

| Field | Unit | Meaning |
|---|---|---|
| `speed_rpm`, `target_rpm` | rpm | shaft speed and dyno setpoint |
| `torque_nm` | N·m | electromagnetic torque |
| `id_a`, `iq_a` | A | true dq currents (true rotor frame) |
| `v_link_v` | V | DC-link voltage |
| `v_ocv_v` | V | pack open-circuit voltage |
| `i_dc_a`, `p_dc_w` | A, W | bridge DC current and power (1 ms average; + = from the link), inverter losses included while modulating |
| `p_mech_w` | W | shaft power (+ = motoring, delivered to the dyno) |
| `p_loss_w` | W | inverter + copper losses (thermal model) |
| `t_coolant_c` | °C | coolant |
| `theta_err_deg` | ° el | the firmware's electrical angle minus the true angle (null when the resolver is invalid) |
| `bridge` | string | what the switches do: "mod", "asc", "off" (diode bridge) |
| `contactor` | string | "open", "precharge", "closed" (physical) |
| `inject` | string[] | active persistent injections |

**`vcu`** (the bench VCU model): `seq` ("idle" "arming" "precharge" "closed" "disarming"), `note` (why a sequence
waits; absent when it does not), `enable`, `torque_nm` (request), `torque_tx_nm` (in the frames, after the slew),
`slew_nm_s`, `gear`, `vspeed_valid`, `vspeed_kmh` (round 23: the vehicle speed sent, A.4.2 `vspeed`), `send` (false
during `can_loss`), `external_cmd`, `external_bms` (A.4.2 `vcu_model`).

**`sim`**: `time_factor`, `rt` (achieved simulated/wall ratio over the last second), `paused`, `rate_hz`.

#### A.5.3 `ack`

`{"type":"ack","id":7,"cmd":"param_set","ok":true,"t_ms":6821.02, ...}` — `id` only if the command had one;
`ok`; `err` (when `ok` is false) or `info` (a remark); `t_ms`; plus the command's extras (A.4.5; `uds`: `msg`,
`frames`, `rsp`, `rsp_id`). A line that is not a flat JSON object is answered with
`{"type":"ack","cmd":"","ok":false,"err":"not a flat JSON object",...}`.

#### A.5.4 `log`

`{"type":"log","level":"info"|"warn","t_ms":…,"msg":"…"}` — power-ups, "the host cannot keep up".

#### A.5.5 `can` (with `can_tap`)

`{"type":"can","bus":0,"id":513,"len":20,"hex":"0F17…","t_ms":…}` — a frame the firmware transmitted
(bus 0 vehicle, 1 diagnostic).

#### A.5.6 `params`

`{"type":"params","params":[{"name","type","min","max","value","default"},…]}` — as `hello.params`, current values.

#### A.5.7 `golden` (`--golden` only)

`{"type":"golden","in":{…},"frame":{"msg","id","len","hex"},"decoded":{…}}` — for each input set, the frame the
firmware's encoder produced (`can_encode_vcu_cmd`, `can_encode_vcu_bms`, `can_status_encode`, the UDS request) and,
for the VCU frames, what the firmware's receiver (`can_cmd_rx`) decodes from it; for UDS what the default build
answers. These lines are collected into `tool/protocol/can-frames.json` `vectors`.

#### A.5.8 `periodic`

`{"type":"periodic","pdid":0,"did":61952,"hex":"09 00 02 41 04 04 00 10 00 10 AA","t_ms":…}` — round 23: a frame
the firmware sent on 0x6E9 for a `2A` request (FW-40 ReadDataByPeriodicIdentifier): `pdid`, `did` = 0xF200 | pdid,
`hex` = the data as sent, padded with 0xAA to the CAN-FD length (the DID's own length and layout: contract §10h).
Not acknowledged, not paired with a request; stop the stream with `2A 04 <pdid>`.

### A.6 Sequences

**Boot** (≈ 0.5 s simulated): INIT → SENSOR_SELFTEST (resolver ramp, V_DC, current offsets, FS0B/FS1B release,
gate power) → GATE_SELFTEST (FW-16; runs because the link is at 0 V and the contactors are open) →
PRECHARGE_WAIT (self-test done). Enum values are not in sequence order (PRECHARGE_WAIT = 4, GATE_SELFTEST = 5).

**arm** (built-in VCU): enable on → when the firmware reports PRECHARGE_WAIT: contactors "precharge" → when the
link reaches 98 % of the pack after ≥ 300 ms (or after 2.5 s — FW-19 then judges it): "closed" → the firmware
enters ARMED_ZERO_TORQUE and raises MCU_GATE_EN (≈ 0.45 s). With enable on and |torque| > 0.5 N·m it enters RUN.
If the firmware is in FAULT, `vcu.note` says so: a `fault_reset` (below n_x) comes first.

**disarm**: torque 0, enable off → RUN → ARMED_ZERO_TORQUE when the torque has ramped out → contactors open
(only below n_x) → PRECHARGE_WAIT (a normal disarm, no fault).

**External VCU** (testing your codec against the firmware): `{"cmd":"vcu_model","cmd_frames":false}` (and
`bms_frames` false if you relay the BMS too), `{"cmd":"can_tap","on":true}`; then send VCU_CMD every 10 ms with
`can_rx` (the contactor field of your frames drives the plant's contactors) and decode the `can` lines. A wrong
CRC is counted in `wd.can_crc` and raises DTC_CAN_E2E; a wrong counter in `wd.can_frozen` / `wd.can_jump`.

### A.7 Enumerations

| Name | Values |
|---|---|
| state (`state.sm`, INV_STATUS b2) | 0 OFF, 1 INIT, 2 SENSOR_SELFTEST, 3 VEHICLE_HANDSHAKE, 4 PRECHARGE_WAIT, 5 GATE_SELFTEST, 6 ARMED_ZERO_TORQUE, 7 RUN, 8 DERATE, 9 FAULT, 10 DISCHARGE, 11 SAFE_POWERDOWN |
| §6 rows (`safety.rows` bit n, `safety.row`) | 0 CMD_LOST, 1 BATTERY_LOST, 2 BMS_LIMIT_ZERO, 3 RESOLVER_INVALID (also "control lost"), 4 FLT_HS, 5 FLT_LS, 6 V5GD_LOSS, 7 OVERVOLTAGE, 8 OVERCURRENT, 9 VDC_INVALID |
| §6 actions (`safety.action`) | 0 NONE, 1 RAMP_KEEP_CC, 2 RAMP_THEN_SPO, 3 ZERO_CURRENT, 4 SPO, 5 LS_ASC, 6 SPO_THEN_PWM_ASC |
| arming evidence bits (`state.evidence` = present; INV_STATUS b15 = missing) | 0x01 ROUTE_BOUND, 0x02 CONFIG_MATCHES, 0x04 PROTECTION_LOCKED, 0x08 FAULT_ROUTE_VALIDATED, 0x10 OVP_ROUTE_VALIDATED |
| calibration errors (`hello.calib.err`) | 0x001 MISSING, 0x002 VERSION, 0x004 CRC, 0x008 RANGE, 0x010 SKU, 0x020 SERIAL, 0x040 MOTOR_ID, 0x080 FSW |
| DTC status byte | 0x01 testFailed, 0x02 testFailedThisOperationCycle, 0x04 pendingDTC, 0x08 confirmedDTC, 0x10 testNotCompletedSinceLastClear, 0x20 testFailedSinceLastClear, 0x40 testNotCompletedThisOperationCycle |
| gear | 0 N, 1 D, 2 R, 3 P |

The names also come in `hello` (`states`, `ss_rows`, `ss_actions`, `dtcs`) — prefer those at run time.

### A.8 Scenario scripts

A script is a text file of commands, one per line, normally each with `in_ms` (the time after the bridge starts,
simulated). `sim_bridge --script FILE` loads it after `hello`; stdin stays open for more commands. Examples in
`tool/bridge/scenarios/`: `drive-cycle.jsonl`, `desat-at-speed.jsonl`, `battery-loss-regen.jsonl`,
`thermal-derate.jsonl`. A client can send the same lines itself (all in one write, so they share the reference time).

### A.9 Example session

```
$ ./sim_bridge --rate 200
{"type":"log","level":"info","t_ms":1001.02,"msg":"power-up 1: 8XX SiC, key cycle 1, init ok"}
{"type":"hello","bridge":"traction-tool-bridge 1","fw_id":"0x0A0F0015","sku":1,"sku_name":"8XX SiC",...}
{"type":"tel","v":1,"seq":0,"t_ms":1002.02,"src":"sim","state":{"sm":1,...},...}
> {"cmd":"arm","id":1}
{"type":"ack","id":1,"cmd":"arm","ok":true,"t_ms":2410.02}
> {"cmd":"speed","rpm":3000,"id":2}
> {"cmd":"torque","nm":150,"hold_ms":250,"id":3}
... "state":{"sm":7,"bridge":2,...},"motion":{"speed_rpm":2999.98,...,"torque_cmd_nm":150,"torque_est_nm":149.79,...}
> {"cmd":"inject","fault":"desat_hs","id":4}
... "state":{"sm":9,...},"dtc":[[17,47,1,4431,4431]],...
> {"cmd":"param_set","name":"cal_ign_off_v","value":6,"id":5}
{"type":"ack","id":5,"cmd":"param_set","name":"cal_ign_off_v","ok":false,"violations":1,"err":"ti_params_validate() refused the set: 1 violation(s); nothing written","value":4,"t_ms":…}
```

---

## Part B — the vehicle CAN-FD and UDS codec

Source: `firmware/src/comms/can_cmd.h` (layouts), `can_cmd.c` (the encoders and the receiver), `uds.h`,
`uds_diag.h`, `firmware/src/boot/uds_update.h`, `firmware/src/app/commission.h`, `firmware/src/diag/uds_capture.h`.
"Frame layouts are this repository's definition until the OEM DBC binds them" (can_cmd.h). Machine-readable:
`tool/protocol/can-frames.json` (`messages`, `e2e`, `diagnostics`, `vectors`).

### B.1 Conventions

- CAN FD, 11-bit identifiers. VCU_CMD and VCU_BMS are 8 bytes; INV_STATUS is 20 bytes (a valid CAN FD length,
  DLC 11); a vehicle frame shorter than 8 bytes is rejected. Bit rates are not fixed by the contract.
- Multi-byte fields are **little-endian**. Bit n of a byte is its 2ⁿ bit.
- Encoding (as `can_cmd.c` does it, IEEE float32): `raw = (int) clamp(x OP k, lo, hi)` — the conversion truncates
  toward zero; OP/k/lo/hi per field below. Decoding: `x = raw × scale + offset`.

### B.2 End-to-end protection (FW-11)

- **CRC**: CRC-8/SAE-J1850 — polynomial 0x1D, init 0xFF, final XOR 0xFF, no reflection (check value of
  "123456789" = 0x4B). It is computed over `len` bytes: the **DataID** = the low byte of the CAN ID (0x01, 0x02,
  0x01 for 0x101/0x102/0x201) in place of byte 0, followed by bytes 1 … len−1; the result is byte 0.
  ```c
  uint8_t crc = 0xFF;
  for (i = 0; i < len; i++) { crc ^= (i == 0) ? (id & 0xFF) : data[i];
      for (b = 0; b < 8; b++) crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x1D) : (uint8_t)(crc << 1); }
  data[0] = crc ^ 0xFF;
  ```
- **Alive counter**: byte 1 bits 0–3, +1 per frame (mod 16), one counter per message. The receiver
  (`counter_ok`): the first frame is accepted; `delta = (ctr − last) mod 16`; delta 0 = frozen sender → rejected;
  1 ≤ delta ≤ `cal_can_ctr_max_jump` (2) → accepted; a larger jump → rejected, and `last` resynchronises to it.
- **Staleness**: VCU_CMD older than `can_stale_ms` (20 ms) ⇒ stale — torque ramps to zero (never held); while armed
  the contactor state is then unknown ⇒ the §6 battery-path row. VCU_BMS older than `cal_bms_timeout_ms` (100 ms)
  ⇒ zero regen. A VCU sends both every 10 ms.

### B.3 VCU_CMD — 0x101, 8 bytes, VCU → inverter, every 10 ms

| Byte | Bits | Field | Encoding |
|---|---|---|---|
| 0 | 0–7 | CRC | B.2 |
| 1 | 0–3 | alive counter | B.2 |
| 1 | 4–5 | gear | 0 N, 1 D, 2 R, 3 P |
| 1 | 6 | enable | 1 = enable request |
| 1 | 7 | fault reset | 1 = reset request |
| 2–3 | 16 | torque request | int16, 0.1 N·m: `(int16) clamp(T × 10, −32767, 32767)` |
| 4 | 0–1 | contactors | 0 invalid, 1 open, 2 precharge, 3 closed |
| 4 | 2 | DESAT retry authorisation | 1 = authorised (FW-15) |
| 4 | 3 | discharge request | 1 = request (FW-17) |
| 4 | 4 | shutdown request | 1 = request |
| 4 | 5 | vehicle speed valid | round 23 (FW-39): 1 = bytes 6–7 carry the vehicle speed |
| 4 | 6–7 | reserved | 0 |
| 5 | 0–7 | coolant temperature | uint8, °C + 40: `(uint8) clamp(T + 40, 0, 254)`; 0xFF = not available |
| 6–7 | 16 | vehicle speed | round 23 (FW-39): uint16, 0.01 km/h: `(uint16) clamp(v × 100, 0, 65535)`; read only by FW-39's service mode (valid and ≤ `vspeed_max_kmh` 0.5 km/h) |

Receiver semantics (the inverter): torque passes the direction interlock (D: drive +, brake − only while
n > `cal_dir_change_rpm`; R mirrored; N/P: 0; gear changes only below `cal_dir_change_rpm`), then the limits.

### B.4 VCU_BMS — 0x102, 8 bytes, VCU (BMS relay) → inverter, every 10 ms

| Byte | Bits | Field | Encoding |
|---|---|---|---|
| 0 | | CRC | B.2 |
| 1 | 0–3 | alive counter | B.2 (bits 4–7 reserved 0) |
| 2–3 | 16 | pack voltage | uint16, 0.1 V: `(uint16) clamp(V × 10, 0, 65535)` |
| 4–5 | 16 | charge (regen) power limit | uint16, 0.1 kW: `(uint16) clamp(P_W / 100, 0, 65535)` |
| 6–7 | 16 | discharge (motoring) power limit | uint16, 0.1 kW: `(uint16) clamp(P_W / 100, 0, 65535)` |

### B.5 INV_STATUS — 0x201, 20 bytes, inverter → VCU, every 10 ms

| Byte | Bits | Field | Encoding |
|---|---|---|---|
| 0 | | CRC | B.2 |
| 1 | 0–3 | alive counter | B.2 |
| 1 | 4 | self-test done | |
| 1 | 5 | keep HV connected (FW-08b) | |
| 1 | 6 | derate | |
| 1 | 7 | fault | |
| 2 | 0–7 | state | A.7 |
| 3 | 0–1 | bridge | 0 disarmed (SPO), 1 idle, 2 modulating, 3 PWM-ASC |
| 3 | 2–3 | HV state | 0 unknown, 1 safe, 2 present |
| 3 | 4 | zero torque | \|command\| < 0.5 N·m: a non-emergency opening may proceed (FW-08) |
| 3 | 5 | discharging | |
| 3 | 6 | precharge refused | FW-19 |
| 3 | 7 | speed valid | |
| 4–5 | 16 | torque applied | int16, 0.1 N·m (round 23: what the issued current references represent; 0 without modulation) |
| 6–7 | 16 | speed | int16, 1 rpm: `(int16) clamp(n, −32767, 32767)` |
| 8–9 | 16 | V_DC | uint16, 0.1 V: `(uint16) clamp(V × 10, 0, 65534)`; **0xFFFF = invalid** |
| 10 | 0–7 | module temperature | uint8, °C + 40: `(uint8) clamp(T + 40, 0, 254)` (hottest valid NTC; raw 0 = −40 °C when none is valid) |
| 11 | 0–7 | confirmed DTCs | uint8, count saturating at 255 |
| 12–13 | 16 | first active DTC | uint16 DTC id (0 = none); UDS code 0xD10000 \| id; names in `tool/protocol/dtcs.json` |
| 14 | 0 | no safe state proven | an SPO held with neither rule (a) nor (b) |
| 14 | 1 | service required: do not re-energise | stuck-on QDIS (FW-26) |
| 14 | 2 | open the contactors | |
| 14 | 3 | speed limit requested | no voltage-feasible current (FW-25) |
| 14 | 4–7 | reserved | 0 |
| 15 | 0–4 | arming evidence missing | ARM_EV_* bits (A.7) |
| 15 | 5–7 | reserved | 0 |
| 16–17 | 16 | torque command | int16, 0.1 N·m (round 23: after the limits, ramps and trims — what bytes 4–5 are asked to be) |
| 18–19 | | reserved | 0 |

A decoder should accept the pre-round-23 16-byte frame too (bytes 16–19 absent) when talking to older images.

### B.6 Diagnostics — UDS on the diagnostic bus (FW-32, FW-38 to FW-41)

ISO 14229-1 over ISO 15765-2 on the CAN-FD diagnostic bus (TX_DL 64). Request 0x7E1, response 0x7E9, periodic
frames 0x6E9 (until the OEM diagnostic specification binds them). The payloads below are the UDS messages (PCI
removed); in the frames:

- **Requests**: a single frame — classic (PCI `0x0L`, L = 1…7, e.g. `02 27 01`) or the CAN-FD escape format
  (`00`, SF_DL ≤ 62, in a frame longer than 8 bytes). Round 23: the FW-38 programming services also take a first
  frame (12-bit length ≤ 4095) and consecutive frames under the ECU's flow control (`30 04 00`: block size 4,
  STmin 0; N_Cr 1 s; a longer first frame gets overflow `32`); another service's segmented request is NRC 0x13.
  A wrongly addressed request gets no response.
- **Responses**: ≤ 7 bytes a classic 8-byte single frame, ≤ 62 an escape single frame in the smallest CAN-FD length
  that holds it (both padded with 0xAA), longer (FW-40, ≤ 1024) a first frame (`1L LL`, 62 data bytes) and
  consecutive frames (`2N`, 63) under the tester's flow control (block size, STmin, WAIT ≤ 16, overflow;
  N_As = N_Bs = 1 s). A new request aborts a pending response. A negative response is `7F <SID> <NRC>`.

| Request | Response | Notes |
|---|---|---|
| `27 01` SecurityAccess requestSeed | `67 01 s0 s1 s2 s3` | the key is a build-time hook; the default build has none ⇒ `7F 27 22` (conditionsNotCorrect), fail closed. A zero seed: already unlocked. The bridge's bench key: A.4.1 `provision` |
| `27 02 k0 k1 k2 k3` sendKey | `67 02` | one key per seed; 3 invalid keys ⇒ NRC 0x36 until the MCU restarts. One unlock serves one 0xF010 run, one `14` clear, one FW-39 start or commit, one FW-46 table write |
| `31 01 F0 10` start 0xF010 (clear the stuck-on QDIS lock, FW-32) | `71 01 F0 10` | only unlocked (else NRC 0x33); refused with HV present or unknown or the bridge armed (0x22); 0x72 if the NVM write cannot be queued; takes effect at the next power-up |
| `31 01 F0 20 rt aa aa` start a commissioning routine (FW-39) | `71 01 F0 20 rt` | rt 1 Rs, 2 Ld/Lq, 3 ψ/zero; the attestation `4C 4B` (locked rotor) for 1–2, `44 46` / `44 52` (dyno forward / reverse) for 3. Unlocked; the preconditions (ARMED_ZERO_TORQUE through the normal path, no row, no DTC, a fresh VCU command without enable, the vehicle speed valid and zero, HV in the SKU window, the rotor's speed) else NRC 0x22 with the reason in results index 0 |
| `31 01 F0 20 02 aa aa k` the Ld/Lq routine at a bias index (FW-45, round 23) | `71 01 F0 20 02` | 8 bytes: the CAN-FD escape single frame (`uds` hex "00 08 31 01 F0 20 02 4C 4B k"); k 0…5 = the saturation map's breakpoint (0, 0.2 … 1.0 × the SKU's current limit; at least 50 A, at most the limit less 20 A) on the d and q axes; k > 5 or another routine ⇒ NRC 0x31. Its results are that map point's (below); the commit writes a whole map (all six points of an axis) or refuses (0x22) |
| `31 03 F0 20 ix` results (FW-39) | `71 03 F0 20 ix b1 b2` | ix 0x00 state (0 idle, 1 running, 2 done, 3 aborted), reason; 0x01 routine, staged mask (round 23: bit 5 a FW-45 map point, bit 6 the FW-46 table); 0x02 (FW-45) the staged points of the Ld map, of the Lq map (bit k); 0x10+q verdict, flags (FW-45: bit 3 a biased run, k in bits 4–6: the point's flags); 0x20+q value, 0x30+q uncertainty (BE16; q 0 Rs 10 µΩ, 1 Ld / 2 Lq 0.1 µH, 3 ψ 10 µWb, 4 zero 0.1 mrad); 0x40+k / 0x50+k (FW-45) the Ld / Lq differential inductance the last run at bias k measured (0.1 µH). Any request of RID 0xF020 is the tool's heartbeat (`hb_timeout_ms` 200) |
| `31 02 F0 20` stop · `31 01 F0 21` commit (FW-39) | `71 02 F0 20` · `71 01 F0 21` | stop: NRC 0x24 when none runs; commit: a fresh unlock, the staged values sealed into a new FW-20 record version (DTC_MC_CAL_WRITTEN), used from the next key cycle |
| `19 01 mm` / `19 02 mm` / `19 0A` ReadDTCInformation (FW-40) | `59 01 7F 01 nn nn` / `59 02 7F (d d d st)*` / `59 0A 7F (d d d st)*` | DTC number = 0xD10000 \| id; status availability mask 0x7F |
| `19 04 d d d rr` snapshot records (FW-40) | `59 04 d d d st (rr 01 FD 2F [49 bytes])*` | rr 1…16 = the n-th newest event of that DTC, 0xFF all; the 49 bytes are the fault ring's `nv_fault_t` (contract §10h) |
| `22 did did …` ReadDataByIdentifier (FW-40, FW-41) | `62 (did data)*` | up to 8 DIDs: telemetry 0xF200–0xF208, identity 0xFD20–0xFD22, the capture's status 0xFD40 and block 0xFD41, round 23 the ripple table 0xFD46 (FW-46: 36 × int16 BE in 0.01 A — the staged table, else the active record's; a segmented response); unsupported ones left out, none ⇒ NRC 0x31; layouts in contract §10h / §10i / §10m |
| `2E FD 46 [72 bytes]` the torque-ripple table (FW-46, round 23) | `6E FD 46` | 75 bytes: a segmented request (the client's first frame and consecutive frames, as TransferData); 36 × int16 BE, 0.01 A, i_q over one electrical period at 10° el. The unlock (NRC 0x33; one write per unlock), no routine running and not in torque (0x22), exactly 75 bytes (0x13: a single frame never holds it), each value within `cal_ripple_ff_max_a` (0x31; the default 0 A: only a zero table). Staged for the FW-39 commit (`31 01 F0 21`), used from the next key cycle |
| `2A mode pdid…` ReadDataByPeriodicIdentifier (FW-40) | `6A` | mode 01 every 100 ms, 02 every 10 ms, 03 every 1 ms, 04 stop; pdid = the low byte of 0xF2xx, up to 4; frames `[pdid, data]` on 0x6E9 (A.5.8) |
| `14 FF FF FF` ClearDiagnosticInformation (FW-40) | `54` | or one DTC number; gated as 0xF010 (unlocked — one clear per unlock —, the bridge disarmed, HV absent); the DESAT class, the service lock and every arming-forbidding DTC are kept |
| `2E FD 41 bb bb` · `31 01 F0 41 [cfg]` · `31 01 F0 42` (FW-41) | `6E FD 41` · `71 01 F0 41` · `71 01 F0 42` | the waveform capture: seek, arm, trigger (not gated: they change only the capture's RAM) |
| `10 02` / `34` / `36` / `37` / `31 01·03 FF 01` / `31 01 F0 38` / `11 01` (FW-38) | `50 02 …` / `74 20 0F FF` / `76 cc` / `77` / … | the programming session (arming forbidden until the next power-up: DTC_FW_UPDATE), RequestDownload (unlocked; maxNumberOfBlockLength 4095), TransferData (a segmented request, ≤ 4093 data bytes), RequestTransferExit, verify, activate, reset (contract §10f) |
| any other service | NRC 0x11 / 0x12 / 0x13 / 0x31 / 0x7F | |

NRC values: 0x11 serviceNotSupported, 0x12 subFunctionNotSupported, 0x13 incorrectMessageLength,
0x21 busyRepeatRequest, 0x22 conditionsNotCorrect, 0x24 requestSequenceError, 0x31 requestOutOfRange,
0x33 securityAccessDenied, 0x35 invalidKey, 0x36 exceededNumberOfAttempts, 0x71 transferDataSuspended,
0x72 generalProgrammingFailure, 0x73 wrongBlockSequenceCounter, 0x7F serviceNotSupportedInActiveSession.

### B.7 Test vectors (from the firmware's encoders)

| Message | Input | Frame (hex) |
|---|---|---|
| VCU_CMD | ctr 0, D, enable, 100.0 N·m, closed, coolant 50 °C | `7050E803035A0000` |
| VCU_CMD | ctr 5, R, enable, −123.4 N·m, precharge, retry auth, 25 °C | `44652EFB06410000` |
| VCU_CMD | ctr 15, N, fault reset, 0 N·m, open, discharge, shutdown, −40 °C | `6F8F000019000000` |
| VCU_CMD | ctr 2, D, enable, 80.0 N·m, closed, 50 °C, vehicle speed valid 123.45 km/h (round 23) | `E7522003235A3930` |
| VCU_BMS | ctr 0, 750.0 V, 100 kW, 250 kW | `4E004C1DE803C409` |
| VCU_BMS | ctr 9, 401.3 V, 0 W, 12 345 W (→ 12.3 kW) | `C009AD0F00007B00` |
| UDS | `02 27 01` → | `037F2722AAAAAAAA` |

The full set (21 vectors including four INV_STATUS frames, saturation and rounding cases, and the decoded values the
firmware's receiver reports) is `vectors` in `tool/protocol/can-frames.json`; each is re-encoded from the tables of
this part by `tool/protocol/generate.mjs`, byte for byte.

---

## Part C — the machine-readable exports

| File | Content |
|---|---|
| `tool/protocol/params.json` | `parameters`: the 89 writable `cal_*` rows — `name`, `type` (f32/u32/u16/u8), `unit` (from the name suffix), `min`, `max`, `default`, `group`, `description` (the generator's basis text), `refs` (FW-xx / § cited), `writable`; `constants`: the contract constants (per SKU where they differ); `cross_field_rules`: the rules `ti_params_validate()` applies, extracted from `params.c`; `skus`; `write_rules`; `firmware` (the tree it came from). |
| `tool/protocol/dtcs.json` | `dtcs`: `id`, `code` (0xD1xxxx), `name`, `class` (no_arm / fault / degrade / info / service), `description`, `response` (what the firmware does, from the contract and the code), `requirement`, `header_comment` (dtc.h), `set_in` (the sources that raise it), `never_set`; `status_bits`; `never_set` (the declared-but-unused list; empty since round 23). |
| `tool/protocol/can-frames.json` | `messages` (per signal: `byte`, `bit`, `bits`, `kind`, `signed`, `unit`, `scale`, `offset`, `invalid_raw`, `encode` {op, k, clamp}), `e2e`, `conventions`, `diagnostics` (transport, identifiers, the services of B.6, NRCs), `vectors`. |

Regenerate and verify (Node ≥ 18 or Bun; builds the bridge first):

```
node tool/protocol/generate.mjs           # write the three files; exit 1 on any mismatch
node tool/protocol/generate.mjs --check   # verify the files are current (CI); writes nothing
```

The generator reads `firmware/tools/gen-params.mjs` by running it with its file writes disabled (it never touches
`firmware/`), and fails if: a `cal_*` row differs between gen-params.mjs, `include/cal_ranges.h` and the compiled
bridge; the DTC list of `dtc.h` differs from the compiled one, or a DTC has no description in the generator; a frame
length or any byte of a firmware test vector differs from what these tables encode; or the CRC check value is wrong.
