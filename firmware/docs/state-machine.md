# Operating state machine

`src/safety/state_machine.c: sm_step` is a pure step function. `app_task_1ms` calls it every
1 ms with the sampled inputs (`sm_in_t`), and `app.c` executes the requests it returns
(`sm_out_t`).

The states are the ones the task listed. The transitions follow contract §9, with one ordering
change: the FW-16 gate self-test runs **before** precharge, because the VCU precharges only after
"self-test done". The task's list had PRECHARGE_WAIT before GATE_SELFTEST.

The state machine only **gates** arming and torque. The bridge action for a fault (SPO, LS-ASC,
PWM-ASC re-entry) is not a state. The fault manager decides it from the §6 matrix
(`fault_mgr.c`, `safe_state.c`), and the fault ISR and the 1 ms task apply it in every state.
The hardware got there first: the eFlexPWM FAULT inputs and the DRV_EN chain act without code.

```mermaid
stateDiagram-v2
    [*] --> OFF
    OFF --> INIT: KL15 on
    INIT --> SAFE_POWERDOWN: KL15 off
    INIT --> FAULT: init FAIL (FS26 readback/OTP, HW_ID, SKU, calibration, gains, arming evidence,<br/>service lock)
    INIT --> SENSOR_SELFTEST: init OK, then the §9 step 2 fault-latch clear (one-shot)

    SENSOR_SELFTEST --> SAFE_POWERDOWN: KL15 off
    SENSOR_SELFTEST --> FAULT: V5GD out of window, FLT low at boot, RDY up before step 6,<br/>gate power failed, sensors not OK in time, FS0B not released in time
    SENSOR_SELFTEST --> Operating: sensors OK, FS0B/FS1B released (step 4),<br/>ASC decision from speed (step 5), gate power up (step 6)

    state Operating {
        VEHICLE_HANDSHAKE --> GATE_SELFTEST: fresh VCU command, no self-test yet
        VEHICLE_HANDSHAKE --> PRECHARGE_WAIT: fresh VCU command, self-test done
        GATE_SELFTEST --> PRECHARGE_WAIT: FW-16 pass or stored pass
        PRECHARGE_WAIT --> ARMED_ZERO_TORQUE: contactors closed, link at pack, FW-19 OK
        ARMED_ZERO_TORQUE --> RUN: fresh command, enable, torque request, contactors closed,<br/>no battery-path row
        ARMED_ZERO_TORQUE --> PRECHARGE_WAIT: contactors open below n_x
        RUN --> DERATE: derate active
        DERATE --> RUN: derate cleared (hysteresis)
        RUN --> ARMED_ZERO_TORQUE: stale or disabled command, after the ramp to zero (FW-11)
        DERATE --> ARMED_ZERO_TORQUE: stale or disabled command, after the ramp to zero
        RUN --> ARMED_ZERO_TORQUE: contactors not closed, or the battery-path row<br/>(stale report, V_DC off the pack): no torque in that invocation
    }

    Operating --> FAULT: a §6 row or a no-arm failure (FW-16 fail, FW-19 refusal, ...);<br/>a battery-path loss only while its §6 response holds energy (current control, ASC)
    Operating --> DISCHARGE: VCU discharge request (edge), contactors open
    Operating --> SAFE_POWERDOWN: KL15 off or shutdown request

    FAULT --> VEHICLE_HANDSHAKE: rows cleared (non-latched, or VCU reset below n_x)
    FAULT --> VEHICLE_HANDSHAKE: FW-15 recovery done for the one authorised retry (reduced torque)
    FAULT --> DISCHARGE: VCU discharge request (edge), contactors open
    FAULT --> SAFE_POWERDOWN: KL15 off or shutdown request

    DISCHARGE --> VEHICLE_HANDSHAKE: done or contactors not open (entered from Operating)
    DISCHARGE --> FAULT: done or contactors not open (entered from FAULT)
    DISCHARGE --> SAFE_POWERDOWN: done with KL15 off

    SAFE_POWERDOWN --> INIT: KL15 on again
    SAFE_POWERDOWN --> OFF: link discharged (or contactors not open) and NVM idle, then FS26 LPOFF
```

## What each state permits

| State | MCU_GATE_EN (arm) | Torque | Notes |
|---|---|---|---|
| OFF, INIT, SENSOR_SELFTEST | no | no | FS0B still holds DRV_EN low until step 4. §9 step 5 keeps the ASC latch when n ≥ n_x at boot. An incomplete arming evidence (route, image, REG_PROT lock, EOL/HIL record) or a service lock fails INIT: FAULT for the key cycle, so FS0B is never released and FW-16 never runs (round 14). |
| VEHICLE_HANDSHAKE | no | no | Waits for a fresh E2E-valid VCU command. |
| GATE_SELFTEST | per FW-16 step | no | Runs only under its measured no-HV, standstill conditions (≤ 0.1 J). Otherwise it uses the stored pass or refuses. |
| PRECHARGE_WAIT | no | no | FW-19 watches the precharge curve. A refusal forbids arming for the key cycle. |
| ARMED_ZERO_TORQUE | yes | zero | |
| RUN / DERATE | yes | yes | DERATE is FW-04 with hysteresis. The retry after a DESAT runs at reduced torque. The outputs describe the state an invocation ends in (round 15): the step that leaves RUN/DERATE grants no torque, and on the way to FAULT, DISCHARGE or SAFE_POWERDOWN no arm either. |
| FAULT | no | no | The §6 action is applied by the fault manager; the only modulation is its own (zero-torque current control after a battery-path loss below n_x while winding current remains, round 15). Exit is only through the rows clearing or the FW-15 retry path. A failure that forbids arming (evidence, service lock, FW-16/FW-19 refusal) keeps it here for the key cycle. |
| DISCHARGE | no | no | QDIS is fired only while the contactors are reported open (FW-17/18). |
| SAFE_POWERDOWN | no | no | Discharges if the contactors are open, flushes NVM, then LPOFF. |

## Tests

- `state_machine`: `boot_order_follows_section_9`, `init_and_sensor_failures_never_arm`,
  `selftest_and_precharge_refusals`, `stale_command_ramps_then_zero_torque`,
  `derate_hysteresis_states`, `faults_and_contactor_opening`, `leaving_run_grants_no_torque_in_the_same_invocation`,
  `battery_path_row_leaves_run_and_blocks_reentry`, `discharge_only_with_contactors_open`,
  `key_off_powerdown_to_lpoff`, `desat_retry_runs_at_reduced_torque`.
- `scenarios: boot_to_run_follows_section_9` runs the same path on the simulated card.
