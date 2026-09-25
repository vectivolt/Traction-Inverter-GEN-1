# Review round 16 — three rechecks of `32214be` (rev A.15)

**Inputs.** Three independent delta rechecks of the A.14 push: (1) a recheck with a five-item register that
ran four reduced-order exciter transients and an eight-case phase-ADC excerpt harness; (2) a recheck that
executed acquisition fixtures against the resolver and phase-current paths (12 cases / 174 checks of the earlier
harness re-passed); (3) a recheck that re-ran the prior regression harness and the ADC excerpt. The instruction:
finish everything end to end; the schematic fully finished, furnished and polished (not the PCB).

**Method.** Every claim verified against the source first (the Bourns derating row for the /33X, the VEXD rail
capacitors, the back-drive expressions, the phase-ADC caller, the resolver update path and the SIN-DMA
sequence). Hardware/BOM/verifier/docs by this session; the three acquisition contracts and the amplitude planes by an Opus
agent with fail-before/pass-after tests (`make test` 223/1882 → **243 tests / 2192 checks / 0 failed**, ASan/UBSan and
−O2 clean, target-check OK; 242 tests run on the pre-fix tree with 122 failing checks, all in the new rows —
`firmware/docs/traceability.md`, "Round 16"). Contract §10b FW-27…FW-30.

## Verdict

All three rechecks are right. None of the round-15 corrections is reversed; what they found is one real corner
that the round-15 rows did not carry (the SWG low corner lands the winding under the 6.5 V pp floor), two
interpretation errors in the round-15 fault rows (one RC time constant read as the end of a pulse; a 20 ms TVS
allowance extrapolated beyond the datasheet curve), one part-data error (the /33X hot hold current) and three
acquisition contracts in the firmware. The fixes are one resistor value (24 k → 28 k), one part change at zero
cost (the exciter TVS becomes unidirectional, which also removes the negative-fault current from the amplifier),
conditional/open rows where the evidence is a measurement, and the firmware contracts. **The rows that could not
be closed on paper are now marked OPEN and named in gate ㉘ — no PASS rests on an extrapolation.**

**"Already Fixed" for a gate means the gate exists and is open** — never that the physical qualification is
complete.

## Review 1 — five-item register (A14-R01…R05)

| ID | Review said | Class | Verification | Action in rev A.15 |
|---|---|---|---|---|
| A14-R01 (R03 in the html) | MODERATE — the /33X holds 0.07 A at 85 °C, not the unsuffixed part's 0.09 A the BOM carried | **Confirmed** | Bourns derating table p.9 | corrected in the BOM and EXTRACTED §31 (the full /33X row); judged against 35 mA nominal / 60 mA assumed (WARN: measured with the selected resolver). **F170** |
| A14-R02 | MAJOR — the back-drive row's 48 µs is one RC time constant, not the end of the pulse; CEXD adds 4.7 µF (26.7 µF); a legal rail voltage is not a diode pulse envelope; the negative fault puts 43 W / 0.9 J into a generic 1206 | **Confirmed** (model interpretation) | expressions reduce to R·C; ALM2402 §8.3.6 | exponential model (4.9 A peak, τ 59 µs, 0.19 mJ in the diode, 99 % by 270 µs) **marked OPEN** pending the measured envelope; **TVSEP/TVSEN → SMCJ8.5A (unidirectional)** so a negative fault flows through the TVS forward diode (I_FSM 200 A) and the amplifier's lower diode sees < 0.3 A; RSX bound to ERJ-8ENF2R20V with its stress stated (1.5 mJ positive, 0.1 W negative). **F168** |
| A14-R03 | MAJOR — the PTC/TVS PASS uses an unallocated 0.5 Ω source, a single 8 A / 20 ms trip point and an 8 J allowance; at 0.1 Ω the current exceeds the PTC's 40 A | **Confirmed** (unsupported PASS) | SMCJ curve ends at 10 ms | TVS-energy row conditional on the measured clearing time (5.5 J supported at 10 ms), a source-impedance allocation row (≥ 0.27 Ω at 35 V) — both WARN; gate ㉘ text names them. **F169** |
| A14-R04 | MODERATE — sense_fast() ignores hal_adc_read_phase()'s return and passes an uninitialised timestamp with zero-filled channels to isns_update() | **Confirmed** | source | deterministic HAL contract; the caller consumes only complete triplets, otherwise marks the current invalid through the sensor-failure path while V_DC and resolver acquisition keep running; every missing-channel combination tested. **F171** |
| A14-R05 (html) | MODERATE — the TVS/PTC survival rests on extrapolated clearing and energy allowances | **Confirmed** | as A14-R03 | as above. **F169** |

## Review 2 — acquisition fixtures (A14-R01…R03, N01)

| ID | Review said | Class | Verification | Action |
|---|---|---|---|---|
| A14-R01 | CRITICAL — resolver validity does not expire when new blocks stop (valid after 1 s of silence); the gap check runs only when a later block is consumed | **Confirmed** | app.c sense_fast(), resolver.c observer() | per-tick age check of the last accepted coherent frame with a bounded hold (CAL from the angle-error envelope), then angle invalidated and the resolver-invalid safe state dispatched; an empty read alone never faults. **F172** |
| A14-R02 | CRITICAL — one SIN-DMA heartbeat tags all three SDADC channels fresh; a completion between the reads lets a mixed-generation tuple through | **Confirmed** | s32k396_resolver.c, sense_fast() | per-channel completion handshake, one coherent frame (EXC + SIN + COS + epoch) copied atomically with the generation checked across the copy, partial/overrun detection; incoherent frames feed the age policy; host DMA modelled per channel. **F173** |
| A14-R03 | MAJOR — the phase-current timestamp contract | **Confirmed** | as review 1 A14-R04 | **F171** |
| A14-N01 | MODERATE — the monitor taps the protected node before the PTC; "sees what the resolver gets" is too strong (7.26 vs 7.00 V pp) | **Confirmed** (wording) | — | planes documented: winding = monitor × 0.964 cold, × 0.875 post-trip; the FW-10 setpoint is defined at the monitor plane; EOL characterises monitor-to-terminal transfer. **F167** |
| previous A13-R01/R02 | corrected (topology; contactor loss at any speed, 66 cases pass) | **Confirmed closure** | — | none |

## Review 3 — regression re-run (A14-R01…R05)

| ID | Review said | Class | Verification | Action |
|---|---|---|---|---|
| A14-R01 | MAJOR — the phase-current timestamp contract | **Confirmed** | — | **F171** |
| A14-R02 | MAJOR — the reverse-drive PASS is not a survival result (τ = 59.7 µs with 26.7 µF, 2.14 A still flowing at 48 µs, 0.2 mJ); RSX generic; a −24 V fault = 46.5 W in RSX | **Confirmed** | — | **F168** (OPEN row; unidirectional TVS removes the negative-fault current from the amplifier and RSX) |
| A14-R03 | MODERATE — the /33X holds 70 mA at 85 °C | **Confirmed** | — | **F170** |
| A14-R04 | MODERATE — the resolver-terminal minimum is checked only at nominal amplitude: source 6.98 V pp → 6.35 V pp at the winding; the monitor does not see the PTC drop | **Confirmed** | 1.884 × 2 × 1.85 × 0.909 = 6.34 V pp | **MFB feedback 24 k → 28 k** (|H| ≈ 2.09): the SWG low corner gives 7.1 V pp at the winding; FW-10 setpoint 7.2 V pp at the monitor needs 1.84 V pp of SWG (≤ 1.884, 3 % headroom) and 1.91 V pk per output (under the −40 °C slew ceiling); the trim ramps up from ≈ 1.5 V pp so the untrimmed maximum never slew-limits; post-trip 6.3 V pp is flagged by FW-10 as intended. **F167** |
| A14-R05 | MODERATE — the TVS/PTC pulse PASS is conditional | **Confirmed** | — | **F169** |
| previous findings | corrected; 47 Ω ballast analytic maximum useful | **Confirmed closure** | — | none |

## What this round changes in hardware

| Item | Was | Now |
|---|---|---|
| REXA4 (exciter MFB feedback) | 24 k (|H| 1.85) | **28 k** (|H| ≈ 2.09; f0 16.6 kHz) |
| TVSEP/TVSEN | SMCJ8.5CA (bidirectional) | **SMCJ8.5A (unidirectional)**, cathode on the protected node |
| RSXP/RSXN | generic 1206 2.2 Ω | **Panasonic ERJ-8ENF2R20V** (1206, AEC-Q200), stress stated |
| FEXP/FEXN text | 0.09 A at 85 °C | **0.07 A at 85 °C** (the /33X row) |
| CEXD | generic 4.7 µF class row (0603) | bound 4.7 µF 25 V X7R 1206 |

## Furnishing (the instruction of this round: "fully finished and furnished")

Not a review finding — the user asked for the schematic to be finished and furnished, so the BOM side was closed
in the same round:

| Item | Was | Now |
|---|---|---|
| Timing / filter / rail capacitors (≈ 40 lines) | generic "MLCC-CLASS" rows, some with the wrong size | value / voltage / dielectric / size classes: C0G for the MFB, RT/CT, feed-forward and resolver filters; X7R at the voltages the F52/F54 rows assume (VPRE 16 V, VEXD/V15 25 V 1210, boost input 50 V 1206) |
| BOM ⇄ sheet package | the BOM printed the rule's footprint | `bom-gen` reads the drawn chip size from the built footprint and refuses a rule that orders another size; the first run caught the MCU decoupling rows (0402 vs the DFM's 0603), the boost input cap, one FS26 output cap and the AGND–DGND tie |
| FVS1 (KL15 sense polyfuse) | class row | **Bourns MF-MSMF010/60X** (0.10 A hold, 60 V, AEC-Q200) — the lowest hold current the family offers |
| LFH1 / LFL1 / LFC (12 V beads) | class row | **Murata BLM31PG121SH1L** (120 Ω, 3.5 A, 1206, automotive twin of the SN1L) |
| LUB / LVB / LWB (hall 5 V beads) | class row | **Murata BLM21PG221SH1D** (220 Ω, 1.25 A at 125 °C, 0805, powertrain grade) |
| LVS1–4 (CAN beads) | class row | **TDK MMZ1608B471CTDH5** (470 Ω, 0.5 A, 0603, automotive series) — the guessed "CTAH0" / "TN1D" codes do not exist / are commercial |
| JDIS / JCTL / JHVIL | generic 2.54 mm headers | **Samtec IPL1-104-01-L-S-K / IPL1-102-01-L-S-K** with IPD1 housings and CC79L crimps (the JIC family: one tool set) |
| CPET (DGND–chassis 4.7 nF) | class row | **Vishay VY2472M49Y5US6TV0** (Y2, 10 mm disc; alternate TDK B32021A3472 film box) — the guessed "…M63…" body code was the 10 nF part |
| RPET / RAGT | 0603 class text on 1206 / 0805 drawings | bound at the drawn size with the reason (200 V working for the chassis bleed; ≥ 2 A star tie) |
| Datasheets | 74 PDFs | **83 PDFs**, EXTRACTED §33–§36 (beads, polyfuse, IPL1/IPD1, VY2/B32021) |

BOM 8XX SiC ₹72,865 @1k (+₹370: the Samtec headers and the automotive beads). Every board's BOM lines are now
either an exact MPN or a value/rating class with its family named; no "buy something that fits" rows remain.

## Self-found in this round

| Item | Fix |
|---|---|
| Firmware: a never-acquired resolver raised the latched "control lost" row straight from INIT, and the unknown-speed §6 decision then raised MCU_GATE_EN for PWM-ASC in a no-arm state (exposed by the new SWG-ramp acquisition timing; five existing no-arm scenarios caught it) | the row requires a resolver that was valid once |
| Firmware: FW-15's 1 s retry gate compared floored millisecond stamps, so a shifted sub-ms phase allowed a retry at 999.8 ms | microsecond comparison; boundary test |
| Firmware image identity: the calibration record is layout 2 (monitor gain), `TI_FW_ID` 0x0A0F0010 | a new EOL/HIL validation record and a new calibration record are required before the image arms |

## What stays open

Unchanged gates plus ㉘ as re-scoped in the README: the ALM2402 reverse-diode envelope (the back-drive row is
OPEN), the PTC clearing waveform at the allocated source impedance (the TVS-energy row is conditional), the
measured excitation current against the 70 mA hot hold, the amplitude at the winding with the selected
resolver; the firmware's target evidence as before. Nothing in this round touches the power stage.

## Register

F167–F173 in `calculations/design-verify.mjs`; the CSV beside this file lists every review ID with its class.
