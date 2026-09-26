# Qualification plan — rev A.17: the round-17 closure and the round-18 rechecks (2026-09-25)

Every item the design leaves open becomes an executable procedure here. Each one states what it closes, the
unit it runs on, the fixture and instruments, the steps, what is measured, the numeric pass criteria, what is
recorded, and the design action if it fails. No hardware is built yet.

**Baseline.** The plan is written against the rev A.15 schematic set plus the round-17 gap closure that is in the
working tree ([`review-A16-disposition.md`](review-A16-disposition.md), gap register G-01…G-14). At that state:
- the verification report stands at 143 PASS · 15 WARN · 0 FAIL;
- the firmware image is `TI_FW_ID` 0x0A0F0011, and the calibration record stays layout 2.

Round 17 is still being completed: its "LV input" and "Firmware" sections are placeholders. If the LV-input study
(G-11) changes the LV-entry parts, QP-LV-03 follows it.

**Sources.** Every source is read as data:
- the README VERIFY list ①…㉘ and the target evidence for the arming record;
- the 🟡 WARN rows of [`verification-report.md`](verification-report.md): 27 at rev A.15, of which 15 remain after
  round 17. The 12 closed in round 17 keep a characterisation here wherever the README or the row says so;
- the 6 WARN rows of [`simulation-report.md`](simulation-report.md) (23 PASS · 6 WARN · 0 FAIL);
- "What stays open" in [`review-A13-disposition.md`](review-A13-disposition.md),
  [`review-A14-disposition.md`](review-A14-disposition.md), [`review-A15-disposition.md`](review-A15-disposition.md)
  and [`review-A16-disposition.md`](review-A16-disposition.md);
- [`firmware-contract.md`](firmware-contract.md) §10c and §11;
- the silicon, HIL and EOL checklist [`../firmware/docs/target-bringup.md`](../firmware/docs/target-bringup.md)
  (T-01…T-38) and the target items of [`../firmware/README.md`](../firmware/README.md) and
  [`../firmware/docs/traceability.md`](../firmware/docs/traceability.md);
- [`dfm.md`](dfm.md) and [`../marine/design-basis.md`](../marine/design-basis.md) §12.

**Two companion documents** supply what this plan does not own. Both are cited by name and topic:
- [`vendor-requests.md`](vendor-requests.md) holds the supplier statements that a test cannot replace.
- [`interface-requirements.md`](interface-requirements.md) holds the vehicle, motor, harness, coolant and EOL
  allocations.

**Contents.**
- §0 rules and set-ups
- §1 power stage
- §2 gate drive
- §3 DESAT and short circuit
- §4 thermal and environment
- §5 LV supply, transients and EMC
- §6 isolation and HV
- §7 resolver and exciter fault bench
- §8 discharge
- §9 safety chain and ASC
- §10 firmware HIL and dyno
- §11 first article and EOL production
- §12 Marine deltas
- §13 traceability
- §14 open items that are not test procedures, and the OEM inputs still missing
- §15 inconsistencies found while setting the criteria

---

## 0. Rules and set-ups

### 0.1 How the pass criteria were set

1. **Numbers come from their sources.** Each number is copied from the row that owns it. That means a
   verification-report row title in quotes with its section in *italics*, a simulation row by S-number, a
   contract requirement by FW-number, a checklist row by T-number, or a finding or gap ID. Where a source gives a
   range, the worst end is used.
2. **Three verdicts.**
   - **PASS** — the measurement is inside the verified prediction, the number the row or simulation claims.
   - **CONDITIONAL** — the measurement is outside the prediction but inside the rating. The model is
     corrected, the owning row is re-run with the measured value, and release waits for that row.
   - **FAIL** — the measurement is beyond the limit: the row's limit column or a datasheet absolute
     maximum. The "On fail" action applies. A test with a hard limit aborts there.

   A row that has only a limit gets PASS or FAIL only.
3. **Characterisation QPs.** Gates ㉖ and ㉘ since round 17, and the rows round 17 closed on paper, are
   characterisations. They use the same three verdicts: a PASS confirms the paper closure, a CONDITIONAL reopens
   the row, and a FAIL is a design defect. **Round 18 exception:** two ㉘ measurements are release gates again —
   QP-RX-05's reverse rail current into ULDOEX (F192, VR-33) and QP-RX-04 step 2b's sustained-short sweep with
   the ECU asleep (F193, IR-42) — because their rows could not be closed on paper (verification-report "Sensing
   A.17" WARN rows).
4. **Labels.** "(derived)" marks arithmetic on source numbers, and the arithmetic is shown. "(plan)" marks a
   plan choice, such as a sample size, a step, a dwell or a repetition count. A plan choice is not a design
   number.
5. **OEM inputs.** Some levels are the OEM's to set: the test-B R_i, harness fault impedance, resolver impedance,
   motor data, coolant flow and others. Each comes from [`interface-requirements.md`](interface-requirements.md)
   under the topic named in the procedure. That document has no topic yet for EMC classes, insulation test
   levels, environmental profiles, the sleep-current budget at temperature, the CAN DBC, or the allocation of
   negative exciter faults. For those, the procedure is written in full and its level stays an open OEM input
   (§14). The record always cites the revision of the input it used.
6. **Vendor statements.** Where a supplier statement closes a gate, a sample test only characterises the part.
   The statement is requested in [`vendor-requests.md`](vendor-requests.md).
7. **Closing a row.** A row closes, or moves, only when the QP record is filed and `design-verify.mjs` or
   `sim-verify.mjs` is re-run with the measured value and the record ID.

### 0.2 Units under test (DUT) and firmware state

| Tag | Meaning |
|---|---|
| **8S / 8I / 4I / 4S** | 8XX SiC ([`bom.md`](bom.md), RHWID 10 k) · 8XX IGBT ([`bom-igbt.md`](bom-igbt.md), 4.7 k) · 4XX IGBT ([`bom-igbt4.md`](bom-igbt4.md), 2.2 k) · 4XX SiC on request ([`bom-sic4.md`](bom-sic4.md), 22 k). Marine builds are in §12. |
| **HW A.15+** | Hardware built from the rev A.15 schematic set with the round-17 part changes: DEXP/DEXN PMEG4050EP-Q, TVSEP/TVSEN SMDJ8.5A-HRA, RFS4 ESR18EZPF1001. The record carries the PCB fabrication revision, the assembly serial, and the date code of every barrier and safety part in design-basis §6a. |
| **FW** | The production image `TI_FW_ID` 0x0A0F0011, built for the DUT's SKU. **No FW** means the card is not in the loop: F-JICB drives the harness. |
| **CAL** | The FW-20 calibration record, layout 2, sealed to the card's device UID, the SKU, the f_sw and a non-zero motor ID. On benches without a motor, the ID is the test motor or the S6 screening motor (0.35 mH / 25 mΩ). The nominal record carries motor_id 0 and never arms (T-07). |
| **VAL** | `NV_REC_VALIDATION` (`arm_validation_t`, layout 1, magic "EVID") for 0x0A0F0011 and this card, written by QP-EOL-05 (T-05). Any change of `TI_FW_ID` invalidates it (T-06). |
| **OTP** | An FS2633D variant programmed per design-basis §8a, with `cal_fs26_prog_id` set to its M_PROGID. The default 0xFFFF never arms (T-34). |

Firmware in the loop energises a gate only with **CAL + VAL + OTP** present (FW-24; target-bringup "What always
holds"). QP-EOL-05 therefore runs before any QP that arms. FW-16 needs the same evidence.

### 0.3 Fixtures

| Tag | Fixture |
|---|---|
| **F-JICB** | A 40-way harness breakout in place of the card at JICC. A pattern generator (10 ns resolution, plan) drives the six PWM lines, DRV_EN, ASC_CMD, QDIS_CMD and both flyback enables; its outputs are high-impedance at power-up, so the default-OFF pull-downs keep acting. The fixture supplies VBAT_H/VBAT_L and reads FLT/RDY, V_DC, NTC and V5GD. |
| **F-NOHV** | The complete inverter (power board, card, modules, cap bank, discharge board) with the DC link at 0 V (bleeder fitted) and the HV input open. DESAT is injected by opening a test link in series with R⟨ph⟩⟨HL⟩DS: the driver's I_CHG (350–650 µA) then charges C⟨ph⟩⟨HL⟩BL to V_DESAT_TH (8.5–10 V) after the real blanking time. FLT is injected at the power-board side. |
| **F-DPT** | The product's power stage on its coldplate: a module, the power board, the cap-bank busbar with its 16 cans and the discharge board. A chiller/heater sets the baseplate from −20 °C to 150 °C. An HV source (0–1000 V) charges an external reservoir of ≥ 10 × C_link (plan) through a series resistor. An air-core load inductor is sized so the first pulse reaches the test current (L = V·t₁/I). An HV crowbar protects the set-up, and gate patterns come from F-JICB. |
| **F-SC** | F-DPT plus a low-inductance short: type I across the load, type II triggered during conduction. A backup series interrupter (a fast fuse or backup IGBT) opens ≤ 10 µs after the expected extinction (plan). The whole fixture sits in a blast enclosure. |
| **F-LVB** | LV-only bench: the complete inverter, or the power board on F-JICB. No HV; modules fitted. Programmable KL30, a chamber (−40…+85 °C) and a thermal-stream head for local +125 °C. |
| **F-RX** | Resolver fault bench: the card on a harness replica to the selected resolver, or a 70 Ω primary emulation (the verifier's screening load). The replica is built to the resolver-harness topic of interface-requirements (cable length and capacitance). A programmable fault source (±40 V, ≥ 50 A) feeds the line through a settable series resistor, from 0 Ω external upwards. An energy-limited mode (2 A current limit, plan) runs first. Separate current probes sit on the PTC lead, the TVS lead and the DEXP/DEXN Schottky lead. A 0.1 Ω shunt between ULDOEX and CEXD measures the reverse rail current. |
| **F-HIL** | Card-in-the-loop HIL. It emulates the power board at the 40-way harness (AMC1311B outputs into the V_DC receivers, LEM outputs, FLT/RDY, NTCs, V5GD), the resolver (SIN/COS synthesised from the card's own excitation) and the vehicle (CAN-FD bus 0 with VCU 0x101/0x102, bus 1 with UDS 0x7E1/0x7E9, KL15, HVIL ladder, FAULT_OUT with a VCU pull-up of 10 k to 5 V, contactor states). It runs a motor model at a step of ≤ 1 µs with ≤ 1 µs I/O latency (plan), and a debugger can halt the core. |
| **F-DYNO** | The target motor on a dyno with a battery emulator and a controllable contactor. An external HV clamp at the can U_N (1000 V 8XX / 600 V 4XX) is the fixture's last-resort protection. Coolant at 65 °C. |
| **F-B2B** | The back-to-back EOL rig (cost-rollup: 220 kW, ₹15–25 L). |
| **F-EOL** | The card-level and box-level EOL stations of §11. Neither is in this repository (target-bringup T-05, T-07). |

### 0.4 Instrument classes

| Tag | Instrument | Minimum (plan) | Why |
|---|---|---|---|
| I-HVD | HV differential probe | ≥ 200 MHz, ≥ 1.5 kV differential | SiC t_f 13 ns cold at 3.3 Ω (HCS600 DS) needs ≥ 27 MHz for the edge alone (0.35/13 ns, derived); the overshoot ring is faster |
| I-HVP | Passive 100:1 HV probe | ≥ 400 MHz, ≥ 2.5 kV | Low-side V_DS, referenced to DC−, on an isolated-channel scope only |
| I-ISO | Optically isolated probe | ≥ 500 MHz, common-mode rating ≥ 150 V/ns (the NSI6611 CMTI class) | Every V_GS/V_GE, the ASC pin vs GND2, high-side nodes |
| I-ROG | Rogowski / CWT | ≥ 30 MHz; ≥ 1.5 kA pk (DPT), ≥ 6 kA pk (SC) | Device current at the module tab |
| I-CP | DC current probe | DC–50 MHz, ≥ 30 A | LV rails, TVS/PTC/Schottky currents, LED currents |
| I-SCO | Oscilloscope | ≥ 500 MHz, ≥ 5 GS/s, 12-bit, isolated channels for HV work | — |
| I-LVS | Logic-level scope + 10:1 probes | ≥ 350 MHz | Safety-logic edges (the UASCG mask is ≤ 11 ns) |
| I-PS | Programmable LV supply + transient generator | 0–40 V, ≥ 30 A; ISO 16750-2 / ISO 7637-2 pulses; programmable R_i 0.5–4 Ω; reverse polarity | LV tests |
| I-HVS | HV DC source + reference meter | 0–1000 V (1300 V for M10); meter ≤ 0.05 % | V_DC calibration, discharge |
| I-LCR | 4-wire LCR meter | C at 1 kHz, R 4-wire, L with DC bias, Z at 10 kHz | Kits, transformers, resolver impedance |
| I-TC | Temperature | type-K thermocouples, fibre-optic probes on HV-floating points, calibrated IR camera | Thermal |
| I-PA | Power analyser + DC-accurate current transducers | — | Losses, efficiency |
| I-HIP | Hi-pot / IR tester | AC/DC, ramp control, µA leakage resolution | Insulation |
| I-PD | Partial-discharge system per IEC 60270 | — | Insulation |
| I-NA | Network analyser + injection transformer | — | Loop gains |
| I-DBG | JTAG/SWD debugger with SWO/ETM trace | — | Core halt, WCET |
| I-CAN | CAN-FD interface with the vehicle DBC and a UDS client | — | Vehicle interface, FW-32 |

### 0.5 What every record carries

- the DUT serial and configuration: SKU, HW, FW, and the CAL/VAL/OTP identity;
- fixture and instrument IDs with their calibration dates;
- ambient and coolant temperature;
- the raw waveforms and logs (a screenshot alone is not a record);
- each measured value against its criterion, and the verdict;
- the gate, row, gap or checklist item that the result closes.

### 0.6 Order of execution

1. Incoming and document checks: QP-PS-04, QP-HV-01, QP-FAI-01.
2. First-article bring-up without HV: QP-FAI-02/03, QP-EOL-01/02/04, QP-GD-01…05, QP-LV-01/06.
3. Evidence records: QP-FW-01 and QP-FW-02, then QP-EOL-05. Nothing arms before this step.
4. The no-HV safety chain: QP-SF-01…09 and QP-SC-01.
5. Energy-limited faults before full energy: the energy-limited mode of QP-RX-04 first, and QP-SF-09 before
   FW-16 runs at key-on.
6. The HV power stage (QP-PS-01/02, QP-DS-01/02) before the destructive tests (QP-SC-02…04, QP-DS-03).
7. Thermal and dyno work: QP-TH, QP-FW-08, QP-SF-10.
8. EMC, insulation and environment on DV units: QP-LV-07/08, QP-HV-04/05, QP-TH-05.
9. The EOL rig proven on DV units before PV: QP-EOL-xx.

### 0.7 QP index

| Group | QP-IDs |
|---|---|
| §1 Power stage | PS-01 SiC DPT · PS-02 IGBT DPT · PS-03 loss-model check · PS-04 DC-link can lots |
| §2 Gate drive | GD-01 six-domain VCC2 bench · GD-02 flyback start/restart/drain · GD-03 bias-bank capacity and transformer saturation · GD-04 UCC14141-Q1 and ASC/QDIS gate levels · GD-05 VOW3120 drive, timing and ambient |
| §3 DESAT / SC | SC-01 I_STO spread and DESAT timeline · SC-02 contained SC IGBT · SC-03 contained SC SiC · SC-04 SC during PWM-ASC |
| §4 Thermal / environment | TH-01 coldplate Rth and plate τ · TH-02 30 s peak, repeat and derating · TH-03 can sharing · TH-04 hot first article · TH-05 environmental and mechanical DV · TH-06 sensor-chain drift |
| §5 LV / EMC | LV-01 regulator loops · LV-02 jump start · LV-03 load dump B at the interface R_i · LV-04 remaining ISO 16750-2 / 7637-2 · LV-05 FAULT_OUT wire faults · LV-06 parking drain and wake inrush · LV-07 EMC emissions · LV-08 EMC immunity |
| §6 Isolation / HV | HV-01 barrier certificates · HV-02 VGT12EEM insulation · HV-03 busbar sleeve through the HC5FW · HV-04 hi-pot and IR (DV) · HV-05 partial discharge · HV-06 creepage/clearance audit |
| §7 Resolver / exciter | RX-01 amplitude planes and SWG trim · RX-02 excitation at low KL30 · RX-03 KL30 short into the SDADC/monitor pads · RX-04 positive terminal fault, VEXD on · RX-05 back-drive, VEXD off/cranking · RX-06 negative terminal fault · RX-07 PTC hold and post-trip · RX-08 motor-temperature line fault |
| §8 Discharge | DS-01 timing with both witnesses · DS-02 repeated pulses · DS-03 stuck-ON resistor opening (contained) · DS-04 stuck-ON/off detection and the FW-32 service lock |
| §9 Safety chain / ASC | SF-01 ASC entry, six V_GS · SF-02 ASC exit/release · SF-03 DESAT during PWM-ASC · SF-04 DESAT with EN low · SF-05 fault latch and FW-15 timing · SF-06 FS26 on silicon · SF-07 S10 hold-up and VCC2-UVLO · SF-08 V5GD loss and hover · SF-09 FW-16 and the energy fixture · SF-10 motor data and the §6 release rule · SF-11 HVIL and harness default-OFF |
| §10 Firmware HIL / dyno | FW-01 PWM-fault route, CPU halted · FW-02 FW-06 chain ≤ 15.6 µs · FW-03 REG_PROT lock · FW-04 WCET · FW-05 acquisition on silicon · FW-06 FW-05 OC route · FW-07 §6 matrix on HIL · FW-08 dyno: contactor opening under regen + loop margins · FW-09 long run and vehicle CAN |
| §11 First article / EOL | FAI-01 design-data release · FAI-02 first-card bring-up · FAI-03 harness numbering · EOL-01 identity · EOL-02 FS26 OTP fields · EOL-03 kit LCR · EOL-04 LV functional · EOL-05 validation record · EOL-06 current calibration · EOL-07 V_DC calibration · EOL-08 resolver calibration · EOL-09 safety-chain functional · EOL-10 hi-pot/IR · EOL-11 HV functional · EOL-12 B2B spin · EOL-13 record seal · EOL-14 vehicle commissioning |
| §12 Marine | MA-01 identity · MA-02 M10 DPT and DESAT string · MA-03 marine SC · MA-04 45 °C thermal and standstill · MA-05 marine discharge, 103 W stuck-ON · MA-06 24 V control power and sustained ASC · MA-07 multi-cell bench · MA-08 DC-grid joining and FW-06 peaks · MA-09 M10 insulation and creepage · MA-10 IACS UR E10 set |

---

## 1. Power stage

### QP-PS-01 · SiC double-pulse test — sets RG_OFF (8S at 850 V / 481 A; 4S at 500 V / 566 A)

**Closes** —
- gate ② ("SiC double-pulse at 850 V/481 A, cold and hot sets RG_OFF (3.3–10 Ω; module Ls unpublished)");
- *Power stage — all SKUs* "Turn-off overshoot, SiC 850 V / 481 A, cold (RG_OFF 6.8 Ω)" (WARN);
- simulation S8 "Loop-L budget @850 V/481 A for 1080 V — 3.3 Ω cold" and "— 6.8 Ω cold" (WARN), and
  "— 6.8 Ω hot" (PASS, confirmed here);
- the SiC "min blank 0.78 µs" of *Gate drive* "DESAT worst detection + soft-off, SiC 47 pF";
- F63;
- A7 cross-check item 5 (ASC pin vs GND2 during the 850 V DPT);
- A13 "the 4XX/8XX power-stage populations are qualified separately (DPT, SC, thermal)".

**DUT** —
- 8S (4S on request), HW A.15+ power stage on F-DPT. No FW (F-JICB).
- All three module positions are tested. The low-side device is measured at every point and the high side once
  per position (plan).
- RG_ON is 3.3 Ω. RG_OFF is stepped through 3.3 / 4.7 / 6.8 / 10 Ω (plan steps inside the 3.3–10 Ω window).
- Three modules from ≥ 2 lots (plan).

**Fixture and instruments** —
- F-DPT.
- V_DS at the module terminals: I-HVP (DC− reference) or I-HVD.
- I-ISO on V_GS at the Kelvin pins and on each low-side ASC pin vs GND2.
- I-ROG on the DC− tab; I-SCO; baseplate temperature by I-TC.
- The thermal points are −20 °C and 150 °C. 150 °C is the HCS600 t_f datum: 13 ns at 25 °C, 22 ns at 150 °C.

**Pre-conditions** —
- The passive bleeder is fitted (the sheet-1 rule).
- VCC2 of the domain under test is recorded (QP-GD-01). The test runs at 15.4 V nominal and at the 16.9 V high
  corner (plan: both).
- The V and I probes are deskewed to ≤ 1 ns (plan), and the probe grounds are checked for ringing on a dead bus.

**Steps**
1. Start energy-limited: 425 V / 240 A (half of 850 V / 481 A, plan) at RG_OFF 6.8 Ω and 25 °C.
2. Step up to 850 V / 481 A. **Abort rule:** do not fire a pulse whose extrapolated V_DS,pk (overshoot ∝ di/dt)
   exceeds 1080 V.
3. Repeat at −20 °C and 150 °C, after the baseplate has settled within 2 K (plan).
4. Repeat steps 1–3 for each RG_OFF value. Select the lowest RG_OFF that passes at −20 °C.
5. At the selected RG_OFF (850 V, 481 A, both temperatures), record the second-pulse turn-on: the V_DS tail
   against time from the gate-on edge.
6. For 4S, repeat steps 1–5 at 500 V / 566 A (the 4XX peak switch current, *Power stage — all SKUs* "Peak switch
   current vs module rating").

**Measure** —
- V_DS,pk at turn-off, di/dt (10–90 %), t_f;
- the loop inductance L_σ = ΔV_overshoot / (di/dt) (derived, per pulse);
- E_on and E_off (∫v·i);
- the V_GS extremes;
- the ASC pin vs GND2 extremes on both edges;
- V_DS at t_on + 0.78 µs.

**Pass** —
- V_DS,pk ≤ 1080 V (repetitive guard) at 850 V / 481 A, at −20 °C **and** 150 °C. The limit is 1200 V abs.
  The predictions are 1104 V cold at 15 nH / 6.8 Ω (the WARN) and 1000 V hot.
- L_σ ≤ 13.6 nH, the S8 budget for 6.8 Ω cold. RG_OFF 3.3 Ω is allowed only if L_σ ≤ 7.8 nH. At 6.8 Ω hot the
  budget is 22.9 nH.
- V_GS stays inside +22 / −10 V abs. The clamps predict +18.8 / −5.9 V.
- Each low-side ASC pin stays inside GND2 −0.3 … +6 V (NSI6611 abs max) on every edge.
- At t_on + 0.78 µs (the SiC minimum blanking), V_DS < 7.2 V (the minimum DESAT trip at the switch, *Gate drive*
  "DESAT trip at the switch (corners)"). No FLT occurs in any sequence.
- E_off at the selected RG_OFF is ≤ the value `loss-model.mjs` books for that resistor. The model's basis is the
  HCS600 Fig. 14 read at 150 °C / 600 A: ≈ 38.5 mJ at 6.8 Ω and ≈ 55 mJ at 10 Ω, before the model's V·I scaling.
  CONDITIONAL if higher: QP-TH-02 and the Tj rows are re-run with the measured energy.

**Record** — waveforms for every (RG_OFF, T, V, I) point, L_σ per position, the selected RG_OFF, and the
E_on/E_off table for the loss model.

**On fail** — use the verifier's levers in this order:
1. RG_OFF → 10 Ω (≈ +30 mJ E_off, +11 °C at peak; re-run QP-TH-02).
2. A firmware current limit I_pk(V_dc) above 800 V (an FW-03 envelope change).
3. A busbar or snubber change if L_σ is the cause (the 1 µF per-module film snubber; a busbar target of
   ≤ 5 nH external).

vendor-requests.md (hiitio — module stray inductance) splits L_σ between the module and the busbar.

### QP-PS-02 · IGBT double-pulse test (8I at 850 V / 481 A; 4I at 500 V / 566 A)

**Closes** —
- A13 "populations qualified separately";
- *IGBT SKUs* "Gate rails legality (+15.6/−5.1)" (note "dv/dt shoot-through is a DPT row") and design-basis §2b
  ("dv/dt shoot-through stays a bench row");
- the "Min blank 1.22 µs vs the turn-on tail (DPT)" note of *Gate drive* "DESAT worst detection + soft-off, IGBT
  82 pF";
- the IGBT note of "Turn-off overshoot, SiC 850 V / 481 A, cold" ("IGBT SKUs: tf 200–385 ns ⇒ < 40 V").

**DUT** — 8I and 4I, HW A.15+, on F-DPT. RG 1.0/1.0 Ω; No FW. Three modules from ≥ 2 lots (plan).

**Fixture and instruments** — as QP-PS-01. Add I-ISO on the complementary (off-state) device's V_GE.

**Pre-conditions** — as QP-PS-01. Gate rails at nominal 15.4 V and at the 16.9 V high corner.

**Steps**
1. Energy-limited step first (half voltage and current, plan).
2. 850 V / 481 A for 8I and 500 V / 566 A for 4I, at −20 °C and 150 °C (Tvjop).
3. Capture the turn-off, the second turn-on, and the complementary device's V_GE during the turn-on dv/dt.

**Measure** —
- V_CE overshoot above the bus, t_f;
- the off-state V_GE excursion of the complementary device, and any shoot-through current at the DC− tab;
- V_CE at t_on + 1.22 µs;
- E_on, E_off and E_rec at 1.0 Ω.

**Pass** —
- The V_CE overshoot is < 40 V above the bus (t_f 200–385 ns), and V_CE,pk ≤ 1080 V.
- The off-state V_GE stays < 5.0 V (V_GE(th) min) at the maximum dv/dt, with the −5.1 V off-bias and the Miller
  clamp. No current beyond the diode-recovery current appears at the DC− tab.
- V_CE at t_on + 1.22 µs is < 4.2 V (the minimum IGBT DESAT trip). No FLT occurs.
- V_GE stays inside ±20 V abs (the clamps predict +18.8 / −5.9 V).
- E_on ≤ the booked value (DS ≈ 60 mJ at 1 Ω, 175 °C / 600 A; E_rec ≈ 40 mJ at 1 Ω). CONDITIONAL if higher: the
  thermal rows are re-run.

**Record** — waveforms and the energy table.

**On fail** — the Miller clamp and −5.1 V are already fitted. The next lever is RG_ON: a higher value raises
E_on, so QP-TH-02 is re-run. The blanking (82 pF) may move only inside the SC budget of QP-SC-02.

### QP-PS-03 · Loss-model check at the continuous and peak points

**Closes** — the loss input to gate ④ and to S4 ("losses from loss-model.mjs at V_max"); the "Semiconductor
efficiency @ continuous" rows of *Power stage — 8XX SiC / 8XX IGBT / 4XX IGBT / 4XX SiC*.

**DUT** — each SKU, HW A.15+, complete inverter on F-DYNO or F-B2B. FW with CAL, VAL and OTP; RG as selected in
QP-PS-01/02.

**Fixture and instruments** — I-PA on the DC input and the three phases; coolant at 65 °C, flow per
interface-requirements (coolant).

**Steps**
1. Run the continuous point to thermal steady state: 8S and 8I at 120 kW, 700 V, 185 A rms (10 kHz and 5 kHz);
   4I and 4S at 90 kW, 400 V, 250 A rms.
2. Record DC power in, AC power out and the module NTCs.
3. Run the 30 s peak at V_max: 340 A rms at 850 V (8XX), 400 A rms at 500 V (4XX). This data feeds QP-TH-02.

**Measure** — total inverter loss and efficiency; the per-module loss split (from the NTC rise × the measured
Rth, QP-TH-01).

**Pass** —
- The measured inverter efficiency is ≥ the row value minus 0.2 pt. The rows are 99.01 % (1197 W) for 8S,
  98.56 % (1751 W) for 8I, 98.07 % (1775 W) for 4I and 98.29 % (1567 W) for 4S. The 0.2 pt comes from the row
  notes: the model uses a conservative 175 °C R_DS(on), and caps, busbar and LV add ≈ 0.1–0.2 pt.
- CONDITIONAL if lower: the Tj rows and S4 are re-run with the measured loss.

**Record** — loss tables per SKU and operating point.

**On fail** — RG retune within QP-PS-01/02, or the FW-04 derating re-tuned from the measured loss.

### QP-PS-04 · DC-link capacitor lots (8XX and 4XX cans)

**Closes** —
- *DC link — 4XX bank (4XX IGBT)* "4XX can binding" (WARN);
- gate ⑧, its residual ("ripple rating checked at PO");
- RR09 (the 4XX ripple rating stated at 5 kHz);
- F20/F21.

**DUT** — incoming lots:
- 8XX: Faratronic C3D1M206KFSA382, 20 µF / 1100 V (U_N 1000 V at 85 °C);
- 4XX: C3D1U506KFAA382, 50 µF / 600 V.

Five cans per lot (plan). No FW.

**Fixture and instruments** — I-LCR (C at 1 kHz, ESR at 10 kHz and 5 kHz); the Faratronic data package
(vendor-requests.md, Faratronic — 4XX can ripple, ESR and life).

**Steps**
1. Measure C and ESR per can.
2. Compare the vendor ripple and life data with the requirement below.

**Pass** —
- C inside its tolerance class.
- 8XX: ESR at 10 kHz ≤ 7.8 mΩ, and the rated ripple is 15.4 A rms at 10 kHz / 70 °C / ΔΘ_case 15 K.
- 4XX: the vendor states ≥ 18 A rms at 10 kHz / 70 °C (the row's limit), and states a **5 kHz** rating ≥ 16.2 A
  rms (derived: S3's worst per-can current at 400 A, and the IGBT 4XX switches at 5 kHz, RR09).
- A life statement at the S3 continuous current is present (7.5 A per can for 8XX, 10.2 A per can for 4XX).

**Record** — per-lot table and the vendor data.

**On fail** — the TDK B32778 drop-in alternative for 8XX; for 4XX, another can in the same 37.5 mm positions. In
both cases QP-TH-03 is re-run.

---

## 2. Gate drive

### QP-GD-01 · LV-only gate-supply bench — six-domain VCC2 (gate ⑬)

**Closes** —
- gate ⑬;
- *Gate drive* "VCC2 low corner vs recommended-min" (WARN, 13.57 V vs 13 V);
- *Flyback* "Derived gate rail VCC2 (both diodes, all corners)" (WARN, 13.57–16.9 V in the 13.5–17.0 V window;
  "BENCH GATE: six-domain VCC2 at start, full gate load, ASC and no-load, KL30 9–16 V, 24 V and 33 V");
- A6-R06/R07; contract §11 ("the LV-only gate-supply bench");
- A7 gate 4, with the load steps (P6) and the FB-sense filter knob;
- FW-14 and target-bringup T-37 (RDY timings).

**DUT** —
- 8S and 8I, HW A.15+ power board with its three modules fitted (the real gate charge), on F-LVB via F-JICB.
  4I shares the 8I gate network and 4S the 8S one. No FW.
- Three boards per build, with transformers from ≥ 2 VGT12EEM lots (plan).

**Fixture and instruments** —
- F-LVB, with I-PS on KL30.
- The chamber runs −40 / +25 / +85 °C for the whole board.
- A thermal-stream head then takes each flyback chain and its three driver domains to +125 °C (the component
  corner the verifier uses), with the VOW3120s (100 °C ambient maximum) shielded (plan).
- I-ISO on VCC2–Kelvin and VEE–Kelvin of all six domains (six isolated channels, or one bank at a time).
- I-CP on each flyback's KL30 feed; I-LVS on VDD, FFS, RDY_HS and RDY_LS.

**Pre-conditions** — HV absent, discharge board fitted. F-JICB runs complementary PWM at the SKU's f_sw: 8S at
10 kHz and at the 20 kHz option, 8I at 5 kHz. Dead time is 1.0 µs (SiC) or 2.5 µs (IGBT).

**Steps**
1. For KL30 = 9, 12, 14 and 16 V, plus 24 V for 60 s and 33 V for 400 ms (plan durations), at each temperature:
   start the flybacks from cold and wait for RDY_HS ∧ RDY_LS.
2. Hold each state:
   - no load (PWM low);
   - full gate activity (all six switching at f_sw);
   - ASC (low sides on through ASC_CMD, high sides off);
   - load steps between no load and full activity at 1 Hz (plan).
3. Record VCC2/VEE of all six domains, VDD, FFS and RDY. The flyback drain is captured on this set-up by
   QP-GD-02.

**Measure** — VCC2 and VEE per domain (min/max per state, ripple pk-pk); the VCC2−VEE span; RDY rise/fall after
enable and disable.

**Pass** —
- 13.5 V ≤ VCC2 ≤ 17.0 V in every domain, state, KL30 point and temperature. The prediction is 13.57–16.9 V.
- VCC2 never drops below 13 V (the NSI6611 recommended minimum → FAIL) or below 12.8 V (the UVLO-rising maximum:
  RDY must stay high).
- VEE is −4.8 … −5.4 V (the predicted corners; CONDITIONAL outside). VCC2−VEE ≤ 32 V recommended (21.7 V
  predicted at the high corner).
- At 24 V and 33 V the rails stay in the window, and VDD ≤ 18 V (the zener) and ≤ 20 V abs.

**Record** — the six-domain table per (KL30, T, state), RDY timings, and the FB-sense node waveform.

**On fail** — the knob named in A7 gate 4: the FB-sense filter (100 Ω / 100 nF) and the FB divider
(52.3 k / 15 k). Re-run the `design-verify.mjs` §5 corners after a change. A high corner above 17.0 V also
raises the SC current: re-run QP-SC-02 at the new corner, or tighten the gate rail's upper corner (*Power stage —
all SKUs* "IGBT short-circuit rating condition vs the design corner").

### QP-GD-02 · Flyback start-up, restart and drain stress (S1)

**Closes** —
- S1 "Flyback drain peak @9 V in" and "@16 V in" ("leakage 2 % assumed — bench-confirm the ring");
- S1 "Gate-power STARTUP, A.7: 2.2 k / 47 µF";
- *Flyback* "Restart after a stopped interval", "Start threshold at the 12 V node, worst (2.2 k)", "Start
  resistor dissipation @24 V jump start", "VDD in regulation";
- *Flyback A.4* "Reflected voltage vs clamp-TVS standoff" and "Drain worst case (clamped load dump)";
- F66, F80; interface-requirements (LV supply — KL30 range 9–16 V).

**DUT** — as QP-GD-01, on the same set-up.

**Fixture and instruments** — F-LVB; I-LVS on the BUK7Y14-80E drain (10:1 probe, spring ground), VDD and FFS;
I-CP on the SMAJ13A clamp lead.

**Steps**
1. Start from VDD = 0 V at KL30 9, 12 and 14 V, at −40 / 25 / 85 °C. Record the time from enable to all six VCC2
   ≥ 13.5 V, and count VDD UVLO bursts.
2. Step KL30 down from 9 V in 0.1 V steps (plan) to find the lowest KL30 that starts.
3. Stop the flyback for 1 s and restart it, at 16 V and at 24 V, 10 times each (plan).
4. Record the drain peak at 9 V and 16 V with full gate load, at 25 °C and 85 °C, and during the clamped load
   dump of QP-LV-03.
5. At 24 V with the flyback held off for 60 s, record VDD and the temperature of the 2.2 k start resistor.

**Pass** —
- The rails come up in one burst within ≤ 240 ms at 9 V (the S1 worst case). The PASS predictions are ≤ 176 ms
  at 9 V typical and ≤ 73 ms at 14 V worst.
- The flyback starts at KL30 ≤ 9 V (7.72 V is needed at the 12 V node; 8.05 V is available at 9 V).
- Every restart succeeds (FB from the aux-only FFS node, τ 6.7 ms).
- The drain stays ≤ 80 V abs. The predictions are 36.2 V (9 V), 43.2 V (16 V) and 61.2 V (clamped load dump).
- The SMAJ13A carries no current in normal off-intervals (the reflected 7.4 V is below its 13 V stand-off).
- At 24 V with the flyback held off, VDD ≤ 18 V (≤ 20 V abs) and the start resistor dissipates ≤ 0.25 W (0.078 W
  predicted).

**Record** — start-time table, burst count, drain waveforms and the restart log.

**On fail** —
- Start: the 47 µF VDD reservoir and the 2.2 k start feed, with S1 re-fitted to the measured I_START and UVLO.
- Drain ring: a primary snubber, or the clamp path.
- Restart: the FB-sense network.

### QP-GD-03 · Bias-bank capacity at worst parts and VGT12EEM saturation (gate ⑤, gate-power part)

**Closes** —
- gate ⑤ ("bias bank at worst parts + VGT12EEM Isat");
- *Gate drive* "Gate-power demand per bank, SiC @20 kHz option" (WARN, 1.99 W vs 2.31 W), and the PASS rows at
  10 kHz SiC and 5 kHz IGBT (confirmed here);
- *Flyback* "DCM peak current vs CS limit (bank)" and "CS limit as the saturation guard" ("bench-verify core at
  current limit");
- A6 gate 4; R-F19; simulation "What simulation cannot close" (transformer core saturation at the CS limit).

**DUT** —
- Power boards 8S (RT 10 k, ≈ 253 kHz) and 8I (RT 8.2 k, ≈ 308 kHz), with dummy gate loads per domain.
- Worst-part set per bank:
  - three VGT12EEM with the lowest measured L_p in the lot (DS 10 µH ± 20 %, measured at 100 kHz / 1 V);
  - a UCC28C40 whose measured CS threshold is at the low end (DS 0.9 V minimum);
  - both selected from 20 transformers and 10 controllers (plan).
- No FW.

**Fixture and instruments** — F-LVB; programmable R–C dummy loads; I-CP on the bank's 0.33 Ω CS shunt current;
I-LCR with DC bias; thermal-stream to 125 °C.

**Steps**
1. For each transformer, measure L_p against DC bias current, 0 → 1.5 A (plan: a third of the 4.5 A guard), at
   25 °C and 125 °C.
2. Increase the load on all three domains of a bank equally, until any VCC2 reaches 13.5 V or the CS limit
   engages. Record the bank power.
3. Hold each demand at KL30 9 V and 16 V, 85 °C, and record VCC2 and the primary current:
   - 1.32 W (SiC, 10 kHz);
   - 1.99 W (SiC, the 20 kHz option);
   - 1.99 W (IGBT, 5 kHz).

**Pass** —
- The worst-part capacity is ≥ 2.31 W (SiC, 253 kHz) and ≥ 2.81 W (IGBT, 308 kHz).
- At each demand, VCC2 stays in 13.5–17.0 V, and the peak bank current stays ≤ 3.03 A (the CS limit at 0.33 Ω;
  predicted 2.04 A).
- No saturation inside the CS-limited envelope: L_p ≥ 8 µH (10 µH − 20 %) up to 1.30 A per transformer at
  125 °C. The 1.30 A is derived as the largest share of the 3.03 A bank limit with one transformer at −20 % and
  two at +20 %: (1/8) / (1/8 + 2/12) = 0.43, and 0.43 × 3.03 A = 1.30 A.
- The knee (L_p down to 80 % of its zero-bias value) is recorded against the 4.5 A guard.

**Record** — L(I) curves, capacity per bank, the CS threshold per controller.

**On fail** —
- Capacity: the IGBT SKUs already fit RT 8.2 k. The 20 kHz SiC option is dropped from the parameter sets; the
  10 kHz mode stays.
- Saturation: a lower CS limit (the shunt value), and the TDK statement (vendor-requests.md, TDK — saturation
  current at operating temperature).

### QP-GD-04 · UCC14141-Q1 bias and ASC / QDIS gate levels (gate ㉓)

**Closes** —
- gate ㉓;
- *Discharge* "QDIS gate V_GS (UCC14141-Q1 envelope through the 1.5 k/10 k divider)" ("BENCH: V18Q, QDVO and V_GS
  at start-up, no load and ON");
- *Gate drive* "ASC drive level"; *Safety A.9* "RASCG continuous dissipation while ASC is held";
- A8-N01/N02, R9X-11, F106, F107, F122.

**DUT** — power board + discharge board, 8S and 4I, both UCC14141-Q1 positions (PSASC, PSQD), HW A.15+, on
F-JICB. Three boards (plan). No FW.

**Fixture and instruments** — F-LVB; I-ISO on the bias outputs (VDD–VEE), on QDVO (the VOW3120 output), on QDIS
V_GS (DCN-referenced), and on the three low-side ASC pins vs GND2; I-LVS on V15 and ENA.

**Steps**
1. Set KL30 to 9, 13.5 and 16 V (plan) at −40 / 25 / 85 °C.
2. Power V15 up and down with ASC_CMD and QDIS_CMD low. Capture the bias outputs, QDVO, the ASC pins and QDIS
   V_GS.
3. Hold no load, then QDIS ON (link at 0 V) and ASC ON (latched) for 60 s each (plan), then release.

**Pass** —
- The bias outputs stay in 17.4–18.6 V in every state (62 k / 10 k with 1 % parts, UCC14141-Q1 §7.5).
- QDIS V_GS is 11.6–16.3 V when ON (≤ 18 V recommended, ≤ 22 V abs for the HCM75S12T4K3). When OFF it stays
  below V_GS(th) min 2.0 V throughout power-up and power-down (no glitch).
- The low-side ASC pins stay ≤ the 5.1 V clamp (≤ GND2 + 6 V abs) and ≥ 3.2 V (V_ASCH max) when ON. They stay
  ≤ 1.3 V (V_ASCL min) whenever ASC_CMD is low, including power-up and power-down.
- The RASCG (2.2 k) dissipation is ≤ 272 mW at 85 °C (88 mW predicted).

**Record** — waveforms, steady values, and the RASCG temperature (to QP-TH-04).

**On fail** —
- Output: the FB divider (62 k / 10 k).
- QDIS V_GS: the 1.5 k / 10 k divider ("a divider, not a clamp").
- An ASC or QDIS glitch: the VOW3120 UVLO (11–13.5 V rising) against the bias ramp, and the ENA divider
  (10 k / 4.7 k).

### QP-GD-05 · VOW3120 LED drive, timing and ambient (gate ⑱, T7-04)

**Closes** —
- gate ⑱;
- *Safety A.8* "VOW3120 LED current — ASC opto (RASCL, from UASCG)" and "… — discharge opto (RQDL, from USCH2
  ch3)". These are PASS rows whose 10–16 mA window "closes only with the ±28 % V_F tempco band
  (typical-derived — T7-04 bench)";
- A8-02; A7 P5 (opto ambient).

**DUT** — card + power board, 8S, HW A.15+. Commands come from F-JICB, or from the card with FW, CAL, VAL and
OTP. Three boards, both optos (plan).

**Fixture and instruments** — chamber at −40 / 25 / 100 °C (the VOW3120 T_amb maximum); V5A forced to 4.9 V and
5.1 V (the row's corners); the LED current read across RASCL/RQDL (270 Ω, 1 %); I-LVS on the command and I-ISO
on the opto output.

**Steps** — at each (T, V5A) point, assert and release ASC_CMD and QDIS_CMD 100 times (plan).

**Pass** —
- I_F stays in 10–16 mA (the recommended window) at every corner. The predictions are 10.77 mA cold, 10.93 mA
  hot and 15.84 mA maximum.
- The guaranteed margins hold at every corner: I_F ≥ 10 mA (1.25 × the 8 mA I_FLH maximum) and ≤ 25 mA abs. FAIL
  outside.
- t_PLH and t_PHL are ≤ 0.5 µs at every corner.
- The local ambient of each opto in the product is ≤ 100 °C (measured in QP-TH-04; FAIL above).

**Record** — I_F and delay tables.

**On fail** —
- Window: the RASCL/RQDL value. The window moves with V_F; the verifier notes 261 Ω would reach 16.4 mA.
- Ambient > 100 °C: relocate the opto, or use the SO6L reinforced alternative rated 125 °C (A7 P5) after a
  QP-HV-01 barrier check.

---

## 3. DESAT and short circuit

### QP-SC-01 · NSI6611 soft-off current spread and the DESAT timeline (low voltage)

**Closes** —
- gate ③, the "I_STO spread" part: a measured complement to the NOVOSENSE statement;
- the timeline terms of *Gate drive* "DESAT worst detection + soft-off, SiC 47 pF" and "…, IGBT 82 pF" (WARN);
- "Global DRV_EN drop after a DESAT vs the faulted driver's soft turn-off" (bench confirmation);
- *Safety A.13* "Firmware DESAT hold before MCU_GATE_EN drops";
- S9 SiC and IGBT ("timeline only");
- A6 N5; F69, F84, F146; FW-22; target-bringup T-37 (`cal_desat_en_hold_us` against the measured soft turn-off).

**DUT** — two parts:
- **(a)** ≥ 30 NSI6611ASC-Q1SWR from ≥ 2 date codes on a socketed board with a fixed capacitive load (100 nF +
  1 Ω, plan). No FW.
- **(b)** A HW A.15+ inverter on F-NOHV, in 8S (47 pF blanking) and 8I (82 pF) builds, with FW, CAL, VAL and OTP.

**Fixture and instruments** —
- (a) Chamber at −40 / 25 / 125 °C; VCC2 set to 16.9 V (the high corner the soft-off starts from); I-CP on OUT.
- (b) F-NOHV with the DESAT test link; I-ISO on V_GS (Kelvin), the DESAT pin and FLT; I-LVS on DRV_EN,
  MCU_GATE_EN, the latch /Q and the Schmitt-buffer output.

**Steps**
1. (a) For each part and temperature, trigger DESAT and take I_STO from the discharge slope of the load.
2. (b) At 25 / −40 / 85 °C (plan), with modulation running at no HV, open the DESAT link on one channel. From the
   gate-on edge capture:
   - the end of blanking;
   - DESAT → OUT, and FLT low;
   - the end of soft-off (V_GS below V_GS(th) min);
   - DRV_EN falling, and MCU_GATE_EN falling.
3. Repeat on all six channels: one phase's high and low side at every temperature, the rest at 25 °C (plan).

**Pass** —
- t_detect is ≤ 1.93 µs (SiC, 47 pF) or ≤ 2.98 µs (IGBT, 82 pF) worst, and ≥ 0.78 µs or 1.22 µs (the minimum
  blanking).
- DESAT → FLT low takes 0.4–0.8 µs.
- Soft-off ends before DRV_EN falls. DRV_EN falls 22–53 µs after FLT (+ 0.4–0.8 µs FLT), and always ≥ 12 µs
  after it (the IGBT soft-off at the 100 mA DS minimum).
- MCU_GATE_EN stays up for ≥ 54.1 µs after the first FLT sighting (`cal_desat_en_hold_us` 60 µs). It drops no
  later than 160 µs (timing.md: the hold plus one current-loop period at 10 kHz).
- **I_STO.** The sample minimum at every temperature is recorded; it feeds QP-SC-02/03.
  - PASS for the typical-based rows if I_STO,min ≥ 400 mA (the rows' 4.81 / 3.08 µs).
  - CONDITIONAL if it lies between 100 mA and 400 mA: the budgets are recomputed with the measured minimum.
  - Either way, the gate closes only with the guaranteed minimum from NOVOSENSE (vendor-requests.md, NOVOSENSE —
    guaranteed minimum I_STO across −40…150 °C and 13.5–16.9 V VCC2).

**Record** — I_STO histogram per temperature; the timeline table per channel.

**On fail** —
- Detection: the blanking capacitors, only inside the false-trip limits measured in QP-PS-01/02 ("82 pF is kept;
  blanking is not shrunk blindly", RR04).
- DRV_EN before soft-off ends: the 10 k / 3.3 nF delay. vendor-requests.md (NOVOSENSE — RST/EN during a started
  soft turn-off) decides whether the delay is needed at all.
- The MCU_GATE_EN hold: `cal_desat_en_hold_us`, inside its CAL range of 55–250 µs.

### QP-SC-02 · Contained short circuit — IGBT (8I, 4I)

**Closes** —
- gate ③ ("contained SC tests per silicon");
- *Gate drive* "DESAT worst detection + soft-off, IGBT 82 pF" (WARN) and "SC current-extinction budget, IGBT
  (F07)" (WARN);
- *Power stage — all SKUs* "IGBT short-circuit rating condition vs the design corner" (WARN);
- S9 "DESAT reaction, IGBT 82 pF" (WARN);
- RR04, A6-R04, F65, F84, R1-F06/R2-F05, A8-G01/R7-05;
- contract §11 ("contained short-circuit tests per silicon");
- the Marine M8 DESAT row (same hardware).

**DUT** —
- 8I and 4I power stages, HW A.15+, on F-SC. The drivers are from the QP-SC-01 sample, including the part with
  the lowest I_STO (plan).
- No FW. F-JICB drops DRV_EN 22 µs after FLT: the low end of the card's delay, the worst case for an
  interrupted soft-off.
- Three devices per condition, ≥ 10 events each, ≥ 1 s apart (plan; the FW-15 retry gate is ≥ 1 s).

**Fixture and instruments** — F-SC; I-ROG ≥ 6 kA; I-HVP/I-HVD on V_CE; I-ISO on V_GE; I-LVS on FLT and DRV_EN;
baseplate heater and chiller.

**Conditions** (the grid comes from the rows):
- **A** — the DS point: 800 V, VCC2 15.0 V, 175 °C (HCG600 Table 5).
- **B** — the design corner: 850 V, VCC2 16.9 V (high corner), 150 °C (Tvjop).
- **C** — the design corner, cold: 850 V, 16.9 V, −40 °C.
- **D** — 4I: 500 V, 16.9 V, 150 °C.
- Each condition runs type I (turn-on into the short) and type II (the short applied while conducting 481 A for
  8I, 566 A for 4I).

**Steps**
1. Run an energy-limited ladder at condition B, type I: 200 → 400 → 600 → 850 V (plan). Inspect after each step.
2. Run conditions A, B, C and D, each with types I and II.
3. After the last event on each device, measure its static parameters: V_CE(sat) at 600 A and 25 °C, V_GE(th),
   I_GES and I_CES.

**Measure** —
- t0: I_C crossing 600 A;
- t_FLT;
- t_Vth: V_GE falling below 5.0 V (the V_GE(th) minimum);
- t_ext: I_C falling below 10 % of its peak;
- the I_SC peak, E_SC = ∫V_CE·I_C dt, and the V_CE peak at turn-off;
- the fall/tail time, t_ext − t_Vth.

**Pass** —
- **A:** t_P = t_Vth − t0 ≤ 6 µs, and t_ext − t0 ≤ 6 µs (t_P ≤ 6 µs at 800 V / 15 V / 175 °C, DS Table 5; the
  extinction row compares against the same 6 µs).
- **B, C:** t_P and t_ext ≤ 5 µs. At 850 V / 15.6 V the rating is treated as ≈ 5 µs (*IGBT SKUs* "Short-circuit
  rating used for DESAT timing"). The predictions are 4.81 µs (detect + soft-off at 400 mA) and 6.61 µs (with an
  assumed fall/tail of 1.8 µs), so the measured fall/tail decides this condition.
- **D:** t_ext ≤ 6 µs (the DS rating, read at the lower voltage — plan reading).
- V_CE peak ≤ 1200 V (V_CES) at every SC turn-off.
- Survival: the static parameters stay inside the HCG600 DS limits and within 5 % of their pre-test values
  (plan), with no visible damage.
- A measured fall/tail > 1.8 µs (the extinction row's assumption) is CONDITIONAL: the row is recomputed.

**Record** — all waveforms, the E_SC table, static parameters before and after, and the I_STO of the driver
used.

**On fail** — in the order the sources allow:
1. Tune the DESAT and blanking values, only inside the false-trip and overshoot limits (the QP-PS-02 turn-on
   tail).
2. Tighten the gate rail's upper corner (lower I_SC; re-run QP-GD-01).
3. Fit a stronger soft-off driver, only if the budget cannot close.

vendor-requests.md (hiitio — IGBT short-circuit rating restated at 850 V / VCC2 16.7–16.9 V) may move the limit.
If B fails while A passes, the 850 V / 16.9 V corner is the open issue. Restricting the SKU's voltage would be an
interface-requirements decision (HV side — 850 V maximum), not a hardware change.

### QP-SC-03 · Contained short circuit — SiC (8S, 4S)

**Closes** —
- gate ③ (the SiC letter and the contained test);
- *Gate drive* "DESAT worst detection + soft-off, SiC 47 pF" (WARN) and "SC current-extinction budget, SiC
  (F07)" (WARN);
- S9 "DESAT reaction, SiC 47 pF" (WARN);
- R-F04, F04;
- the Marine M8-SiC "Short-circuit withstand" row (WARN).

**DUT** — 8S and 4S power stages on F-SC, set up as in QP-SC-02: lowest-I_STO driver, DRV_EN dropped 22 µs after
FLT, three devices × 10 events per condition (plan).

**Conditions**
- **B** — 850 V, VCC2 16.9 V, 150 °C (the verifier's release envelope, "850 V/150 °C/+16.9 V").
- **B′** — 850 V, 16.9 V, 175 °C (the condition vendor-requests.md asks hiitio to state).
- **C** — 850 V, 16.9 V, −40 °C (SiC SC current is highest cold).
- **D** — 4S: 500 V, 16.9 V, 150 °C.
- Each condition runs types I and II.

**Steps** — as QP-SC-02, starting with the energy-limited ladder. The static checks after the test are V_GS(th),
R_DS(on) at 600 A and 25 °C, I_GSS and I_DSS.

**Measure** — as QP-SC-02, with V_GS(th) min taken from the HCS600 datasheet.

**Pass** —
- t_ext ≤ the hiitio t_SC for the tested condition (vendor-requests.md, hiitio — SiC short-circuit withstand
  t_SC at 850 V / 16.9 V). The letter is the release limit, and the SiC gate cannot close without it. The
  acceptance stated there is t_SC ≥ 6.83 µs.
- The predictions are 3.38 µs at the typical 400 mA I_STO and 6.83 µs at the 100 mA DS minimum, both with an
  assumed fall/tail of 0.3 µs. A measured t_ext above 3.38 µs is CONDITIONAL (the row is recomputed); above t_SC
  it is FAIL.
- The fall/tail is ≤ 0.3 µs (the row's assumption).
- V_DS peak ≤ 1200 V, and survival as in QP-SC-02.

**Record and On fail** — as QP-SC-02. A SiC fail blocks the SiC SKUs and M8-SiC, not the IGBT SKUs.

### QP-SC-04 · Short circuit during PWM-ASC at HV (the ASC-held case)

**Closes** —
- A8 gate 1 and A9 gate 1 ("contained SC tests … including the ASC-held case");
- the HV confirmation of contract §4c "A DESAT while ASC is active" (the no-HV version is QP-SF-03);
- F90.

**DUT** — 8I and 8S inverters on F-SC with the card in the loop: FW, CAL, VAL and OTP; link at 850 V; motor
leads to a low-inductance star short, so PWM-ASC has a circulating path. Three events per build (plan).

**Fixture and instruments** — F-SC; a triggered crowbar across one high-side device (DC+ to phase) that emulates
a high side failing short during PWM-ASC; I-ROG on the affected low-side tab; I-ISO on all three low-side V_GS;
I-LVS on ASC_CMD, FLT_LS_N, DRV_EN and the ASC pins.

**Steps**
1. Enter PWM-ASC by the MCU path (§4c).
2. Fire the crowbar. The affected low side sees the SC and its DESAT trips.
3. Capture until the FW-15 recovery completes.

**Pass** —
- One bounded SC event, with t_ext inside the silicon's QP-SC-02/03 limit.
- UASCG takes ASC_CMD low ≤ 11 ns after FLT, and the healthy low-side ASC pins release ≤ 1.07 µs after FLT.
- DRV_EN drops ≥ 22 µs after FLT.
- The bridge ends in SPO. ASC does not come back until the FW-15 reset and the §4c re-entry, and never
  automatically after an FLT_LS (§6: SPO only).
- The devices survive, as in QP-SC-02.

**Record** — waveforms, event log, and the §6 decision on CAN.

**On fail** — UASCG wiring and timing (a hardware change), or the FW-15 sequencing (firmware); nothing is
released until this passes.

---

## 4. Thermal and environment

### QP-TH-01 · Coldplate Rth and plate time constant per switch position

**Closes** —
- gate ④ (coldplate Rth, 0.045 K/W assumed);
- simulation S4 "Tj static-plate bound (30 s peak held to steady state, no plate mass) — all SKUs" (WARN,
  "the thermal test (T7-10) measures the plate pole");
- *Power stage — 8XX IGBT* "Tj steady-state bound, peak 30 s (340 A, 850 V, 5 kHz)" (WARN, 142 °C vs 150 °C);
- R8X-11, RR07, T7-10 (vendor Zth overlay), F96, F105;
- contract §11 "thermal/coldplate";
- F15 (A13).

**DUT** — each SKU's module set on the production coldplate, with the production TIM and torque (dfm §3
"torque + TIM verify"), HW A.15+. No FW (DC heating).

**Fixture and instruments** — a DC heating supply of several hundred A (plan); a sense-current source of 1 A
(plan); an oven for TSEP calibration; a chiller at 65 °C with flow and pressure per interface-requirements
(coolant — the document records that the flow is not yet specified); I-TC on the baseplate and the coolant
in/out.

**Steps**
1. Calibrate the temperature-sensitive parameter (TSEP) of every die at a 1 A sense current, in an oven from 25 to
   150 °C with the module unpowered:
   - IGBT: V_CE at V_GE 15 V;
   - diode: V_F;
   - SiC: V_DS(on) at V_GS 15.4 V, or the body-diode V_SD at −5 V.
2. On the coldplate, heat one switch position with DC. For IGBT positions, load the switch and its diode together
   (RR07: the plate carries both). Use two power levels: the continuous loss (8S 200 W; 8I 250 W + 42 W diode)
   and the peak (8S 554 W; 8I 560 W + 92 W).
3. Record the heating curve from cold to steady state. Then cut to the sense current and read the TSEP within
   100 µs (plan, √t extrapolation); record the cooling curve.
4. Measure all six positions, at nominal and at minimum flow.

**Measure** —
- Rth(j–coolant) per position;
- the coldplate Rth (derived) = the measured value − the DS Rth(j-c) (SiC 0.066 K/W; IGBT 0.07 K/W, diode
  0.10 K/W) − Rth(c-s) 0.015 K/W;
- the plate τ from a curve fit, and Zth(t).

**Pass** —
- Coldplate ≤ 0.045 K/W per switch position with the position's dies loaded together, and plate τ ≥ 60 s (both
  S4 assumptions). With those values, the static-plate bounds hold: 135 / 142 / 133 / 143 °C (8S / 8I / 4I / 4S)
  against 175 / 150 / 150 / 175 °C.
- CONDITIONAL: Rth > 0.045 K/W, but the static bound recomputed with it is still ≤ the limits. Re-run S4 and the
  *Power stage* Tj rows.
- FAIL: the recomputed bound exceeds the limit. 8I is the tight one, at 142 °C = 95 % of 150 °C.
- The measured Zth(j-c) agrees with hiitio's curve within ±10 % (plan).

The measurements are compared with two statements: vendor-requests.md (coldplate supplier — measured R_th and
plate τ at a stated flow) and vendor-requests.md (hiitio — Zth figures as guaranteed limits).

**Record** — Rth and τ per position, Zth curves, flow and ΔT.

**On fail** — re-tune the FW-04 derating (peak duration and current per coolant temperature) from the measured
Zth; the 8I peak is the first to be reduced. Otherwise change the coldplate or the flow (interface-requirements,
coolant). An RG change is allowed only inside the QP-PS-01/02 limits.

### QP-TH-02 · 30 s peak, repeat and derating on the dyno — the Tj bound

**Closes** —
- gate ④ ("30 s repeat", RR07);
- S4 "Tj end of 30 s peak" for 8XX SiC / 8XX IGBT / 4XX IGBT / 4XX SiC (PASS rows, measured here);
- the *Power stage* diode rows;
- FW-04 and FW-13 targets; target-bringup T-37 (thermal derating, `cal_peak_recovery_s`);
- firmware README item 9.

**DUT** — each SKU complete inverter on F-DYNO or F-B2B, with FW, CAL, VAL and OTP; coolant at 65 °C
(interface-requirements, coolant — 65 °C inlet).

**Fixture and instruments** — I-PA; the module NTCs through FW; the rig stops switching at the end of the peak and
reads the TSEP of QP-TH-01 within 1 ms (plan).

**Steps**
1. Reach continuous steady state: 185 A rms at 700 V (8XX), 250 A rms at 400 V (4XX), at the SKU's f_sw.
2. Run a 30 s peak at V_max: 340 A rms at 850 V (8XX), 400 A rms at 500 V (4XX). Read Tj at the end.
3. Repeat the peak after the `cal_peak_recovery_s` 180 s recovery, five times (plan).
4. Run regeneration at cosφ −0.85, peak and continuous, and read the diode Tj.
5. Raise the coolant to 70 °C (plan: 5 K over the assumption) and request a peak.

**Pass** —
- End-of-peak Tj:
  - PASS ≤ 125 / 132 / 125 / 133 °C (S4, for 8S / 8I / 4I / 4S);
  - CONDITIONAL ≤ 135 / 142 / 133 / 143 °C (the static bound);
  - FAIL > 175 / 150 / 150 / 175 °C.
- Diode Tj ≤ 121 °C at peak regeneration, ≤ 90 °C (8I) and ≤ 94 °C (4I) in continuous regeneration.
- The fifth peak ends ≤ 2 K above the first (plan): no ratcheting.
- With coolant above the 65 °C assumption, the peak is refused (FW-04).
- NTC-based Tj matches the TSEP within the model error that FW-04 derates for. The error is recorded and fed into
  the FW-04 CAL.

**Record** — Tj traces, NTC vs TSEP, and the derate events.

**On fail** — FW-04 derating parameters, or peak duration and current in the envelope (an FW-03 change,
interface-requirements).

### QP-TH-03 · DC-link can current sharing and can temperature

**Closes** —
- gate ④ ("can sharing");
- the S3 note on both S3 rows ("equal sharing assumed — 15–20 % busbar imbalance is a thermal-test item");
- *DC link — 8XX bank* "Ripple per can, 30 s peak" and *DC link — 4XX bank* likewise (PASS rows at equal
  sharing);
- RR09, F14 (A13).

**DUT** — 8S and 4I complete inverters on F-DYNO, with FW, CAL, VAL and OTP.

**Fixture and instruments** — I-TC on the case of all 16 cans at the maker's hot-spot position; I-ROG on four
reachable can terminals (the two bank ends and two central cans, plan); I-PA.

**Steps**
1. Run continuous at the S3 worst point (M 0.6, cosφ 1): 185 A rms for 8XX, 250 A rms for 4XX. Hold until each
   can's case rise is steady.
2. Derive each can's share: k_i = I_i / I_mean. Use √(ΔT_i / ΔT_mean) at equal ESR, calibrated against the
   Rogowski cans.
3. From continuous steady state, run a 30 s peak at the worst point (340 A / 400 A) and record the hottest can.

**Pass** —
- k_max ≤ 1.116 (8XX) or ≤ 1.111 (4XX). These are derived as rating / worst per-can current: 15.4 / 13.8 A and
  18 / 16.2 A. Then no can exceeds its ripple rating at the S3 worst point.
- If k_max is higher, CONDITIONAL: pass only if the hottest can's case stays ≤ 85 °C at the end of the 30 s peak.
  85 °C is 70 °C + 15 K (the rating's ΔΘ_case at a 70 °C ambient) and also the U_N 1000 V (8XX) / 600 V (4XX)
  basis of the OV trip. Otherwise FAIL.
- Each can's continuous ESR heating stays ≤ 1.85 W (0.44 W predicted for 8XX).

**Record** — per-can ΔT, k_i and the Rogowski readings.

**On fail** — a busbar layout change (can positions and current paths), or an FW-04 peak derate.

### QP-TH-04 · Hot first article — board hot spots

**Closes** —
- *Sensing A.13* "VDC bias LDO (NCV4276C DPAK) behind the 47 Ω ballast" and "LDO pass dissipation, analytic
  maximum over load" (INFO: "the hot first article measures the loaded input current");
- F139 ("measured at the hot first article"), F149;
- the dfm §4 rules (U5LB/U5LC copper; resistor terminal temperatures; the PTCs need free air; keep the fuse away
  from LDO heat);
- gate ⑱ (the VOW3120 ambient part);
- F179: RFS4, now the 1206 ESR18EZPF1001, as installed.

**DUT** — complete inverter 8S (and 8I), HW A.15+, first article, with FW, CAL, VAL and OTP.

**Fixture and instruments** — chamber at 85 °C with the power board interior instrumented (I-TC, IR through a
window); a current shunt in each UCC12051-Q1 input.

**Steps**
1. Run at full gate activity with both V_DC channels live, at 85 °C ambient (and 100 °C for the ⑱ check, plan),
   to steady state.
2. Hold ASC for 10 min with FS1B held and FAULT_OUT shorted to an 18 V source (plan), then release.

**Measure** —
- the U5LB/U5LC tab temperatures → Tj via the DS ψ;
- the UCC12051-Q1 input currents;
- the temperatures of R5LB/R5LC, RASCG and RFS4 at their terminations;
- the local ambient of UASC/UQD (VOW3120), FEXP/FEXN and DEXP/DEXN;
- the FMT1/FMT2 temperatures.

**Pass** —
- LDO Tj ≤ 150 °C (FAIL above). The predictions are ≈ 119 °C at 85 °C and 134 °C at 100 °C; the analytic worst
  case over load is 0.6 W at 113 mA, 120 / 135 °C.
- UCC12051-Q1 input ≤ 96 mA (the budget). Above it is CONDITIONAL: re-run the analytic-maximum row.
- R5L ≤ 455 mW (≤ ≈ 820 mW at 85 °C, the limit).
- RASCG termination ≤ 138 °C at its 0.12 W (ESR03 fillet rule, dfm §4).
- RFS4 dissipation ≤ 0.5 W at ≤ 70 °C (ESR18, 0.3 W predicted at 18 V held), with its element ≤ 155 °C. The dfm
  §4 fillet figure still describes the ESR03 (§15).
- VOW3120 local ambient ≤ 100 °C.
- PTC local ambient ≤ 85 °C: the MF-MSMF020/33X range, and the 0.07 A hold point used in QP-RX-07.
- FMT1/FMT2 stay below the temperature at which the 438A rating is derated (−55…150 °C part; record for the
  fuse-derating curve).

**Record** — thermal map and the table above.

**On fail** —
- LDO: more copper (≥ 1.2 in² of 2 oz is the rule), or the D2PAK variant (43.3 K/W on 1.222 in²).
- Resistors: move them off the hot spots.
- Opto and PTC ambient: relocate, or improve airflow and enclosure.

### QP-TH-05 · Environmental and mechanical DV (ISO 16750-3 / -4 per OEM)

**Closes** —
- contract §11 "mechanical DV";
- README roadmap "ISO 16750 environmental" and "thermal endurance";
- A12 "What is still deliberately NOT claimed": AEC-Q qualification of the VOW3120 and of the harness connector.
  vendor-requests.md (Samtec — automotive A-Series MPN) is the supplier side of the connector gap;
- dfm §3 VERIFY (module aux-pin variant; vendor-requests.md, hiitio — solder-pin vs press-fit).

**DUT** — three complete inverters per SKU population (8S, 8I, 4I; plan), with FW, CAL, VAL and OTP.

**Fixture and instruments** — vibration and shock tables, a thermal-shock chamber and a humidity chamber. The
profiles are an OEM input with no topic in interface-requirements.md yet (§14). The EOL subset is the functional
check.

**Steps**
1. Before the stresses, run QP-EOL-04, EOL-09 and EOL-10 plus the QP-SF-01 entry times as a baseline.
2. Apply the OEM sequence: vibration and shock (ISO 16750-3), thermal cycling or shock and damp heat (ISO
   16750-4).
3. Repeat the functional set after each leg.
4. After the last leg, inspect and cross-section the harness connector (Samtec IPL1), the VOW3120 solder joints,
   the SQP10 leads, the film-can terminations, the module auxiliary-pin interface and the conformal coat.

**Pass** —
- Each functional result stays inside its own QP criterion after every leg.
- No drift beyond the record's baseline by more than the QP tolerance.
- Contact resistance of the IPL1 contacts is within Samtec's specification.
- The VOW3120 LED current window of QP-GD-05 still holds.
- No cracked joints, and no coat voids over HV-spacing areas.

**Record** — the per-leg functional record and the inspection report.

**On fail** — part or process change per the failure. The AEC-Q gap is then closed by this board-level evidence,
or by a different connector or opto family.

### QP-TH-06 · Sensor-chain drift over temperature — hall reference vs VREF5, V_DC chain

**Closes** —
- *Sensing* "Hall ratiometric ref vs ADC ref". It was a WARN at A.15; PASS since round 17 (G-07, F180) "after
  the EOL gain calibration (FW-20)", within the ±5 % torque-accuracy allocation the row cites from
  interface-requirements (EOL/calibration — hall gain vs VREF5). This QP measures the residual;
- the "≈ ±0.3 % after EOL calibration" claim of *Sensing* "VDC chain error after EOL gain/offset calibration";
- the FW-05 addendum ("equal gain error … an EOL calibration item").

**DUT** — the control card with its hall AFE and V_DC receivers, plus one power board for the V_DC chain;
8S parameter set; FW, CAL (from QP-EOL-06/07), VAL and OTP.

**Fixture and instruments** — card chamber at −40 / 25 / 85 °C (the product ambient); a reference current through
each LEM via a multi-turn loop (N turns × I, plan); I-HVS with a reference meter at 850 V; I-LVS on V5A and
VREF5.

**Steps**
1. At each temperature, record V5A, VREF5 and their ratio.
2. Read each phase current at ±200 A and ±481 A equivalent. Compute the gain relative to the 25 °C calibration.
3. Apply 850 V and 880 V. Read both V_DC channels.

**Pass** —
- The V5A / VREF5 ratio drift after the EOL calibration is ≤ ±2 % (the worst end of the row's ±1–2 %), and the
  residual current-gain error stays inside the ±5 % torque-accuracy allocation. That allocation is the one the
  row cites from interface-requirements (EOL/calibration — hall gain vs VREF5).
- At every temperature the FW-05 decision holds: 481 A true current does not trip, and a true current of ≤ 620 A
  trips (the host test "overcurrent_crest_does_not_trip_620_does"). That bounds the low-side gain error at
  ≤ 3.1 % (derived: 601/620 = 0.969).
- The calibrated V_DC error stays within ±0.3 % across temperature, and |VDC1 − VDC2| < 5 % (FW-07).

**Record** — drift table per channel.

**On fail** — moving VREFH to V5A (the row: "not taken" today), or a temperature-compensated FW-20 record.

---

## 5. LV supply, transients and EMC

### QP-LV-01 · Regulator loops and load steps

**Closes** —
- S2 (both "UB15 phase margin" rows note "measured Bode still a bench gate");
- *LV A.4* "UB15 comp zero (2k·100nF) vs output pole" ("bench Bode gates it");
- F59;
- simulation "What simulation cannot close" (measured regulator Bode and load steps, the FS26 VCORE loop).

**DUT** — card + power board, 8S, HW A.15+, on F-LVB. FW with CAL (arming is not needed).

**Fixture and instruments** — I-NA with injection at the UB15 feedback divider; an electronic load on V15; I-LVS
on V15B, V15, V15S, V11, VPRE and VEXD.

**Steps**
1. Measure the UB15 loop gain at KL30 9 V and 12 V, at nominal and maximum V15 load.
2. Load-step V15 by enabling and disabling the bias modules (UCC14141-Q1 / UCC12051-Q1).
3. Step the MCU load: run-mode change, CAN burst, NVM write.
4. Step the ULDO15 and ULDOEX loads.

**Pass** —
- UB15 phase margin ≥ 45° (the S2 limit) at 9 V and 12 V, with the crossover ≪ f_sw/10 (58 kHz) and ≪ the RHPZ.
  The prediction is 72–75° at 1.9–2.5 kHz.
- V15 stays inside 13.5–16.5 V (the design band) during the bias steps.
- No FS26 VMONCORE / VMONPRE flag. V15S stays inside 1.425–1.65 V (the S32K39 externally-sourced V15 range) and
  V11 ≤ 1.26 V abs.
- ULDO15 and ULDOEX settle without sustained ringing (the feed-forward zeros are 14.5 / 15.4 kHz).

**Record** — Bode plots and step waveforms.

**On fail** — the UB15 compensation (2 k / 100 nF / 470 pF), or the LDO feed-forward capacitors.

### QP-LV-02 · Jump start 24 V / 60 s and the 33 V point

**Closes** —
- *LV A.4* "V15 behind ULDO15 @24 V jump start" and "ULDO15 dissipation @24 V sustained";
- *LV A.4* "VEXD target for ALM2402";
- *Flyback* "Start resistor dissipation @24 V jump start";
- *Safety A.9* "RFS4 with FS1B held a whole key-on …" (WARN at A.15; PASS since round 17 with the ESR18 — G-06,
  F179);
- R9X-14, F51, F55; contract §11 (LV transients); interface-requirements (LV supply — 24 V jump start ≤ 60 s).

**DUT** — complete inverter 8S (and 4I), HW A.15+, with FW, CAL, VAL and OTP; no HV (plus one run with HV at
700 V, plan).

**Fixture and instruments** — I-PS; I-CP on the three TPSMC24CA-VR (DTVSC, DTVH, DTVL); I-TC on RFS4, ULDO15 and
the start resistor; I-LVS on V15, VEXD, VDD, IGN_SNS and QLVS V_GS.

**Steps**
1. KL30 at 24 V for 60 s at 25 °C and 85 °C (plan), running armed at zero torque.
2. Repeat with FAULT_OUT shorted to KL30 and FS1B held (the triple condition), at 25 °C.
3. Repeat with the flyback held off.
4. Apply 33 V for 400 ms (the clamped load-dump level, plan).

**Pass** —
- All three TVSs stay dark at 24 V: V_BR ≥ 26.7 V, no current beyond datasheet leakage.
- V15 stays at 15.0 V (≤ 18 V, the UCC14141-Q1 recommended maximum). ULDO15 thermal-shutdown cycling is
  allowed ("stationary service"); record it.
- VEXD stays ≤ 16 V (12.07 V target).
- VDD ≤ 18 V with the flyback held off, and the start resistor dissipates ≤ 0.25 W (0.078 W predicted).
- RFS4 (ESR18) dissipates ≤ 0.5 W (0.48 W predicted, 0.96–0.97× nameplate) with its element ≤ 155 °C (≈ 107 °C
  predicted). After the run it is within its tolerance and the ESR series overload drift, or it has opened; an
  open only disconnects an already-shorted FAULT_OUT (accepted).
- KL15 sense-pin injection stays ≤ 3 mA (0.72 mA at 40 V predicted). The QLVS gate stays inside ±20 V.

**Record** — waveforms, temperatures and the TSD log.

**On fail** — TVS or LDO choice; for RFS4, the ESR25 (0.75 W) in the same series.

### QP-LV-03 · ISO 16750-2 load dump test B at the interface R_i (gate ㉗)

**Closes** —
- gate ㉗;
- *LV A.11* "Load dump test B (35 V, 400 ms) into the TPSMC24CA-VR + MF-LSMF300/24X polyfuse" (WARN);
- *LV A.11* "… into the classic TPSMC24CA + MF-LSMF300/24X polyfuse" (now INFO, historical — the incoming check
  below proves the fitted part is the -VR);
- G-11 (round 17, LV input — the study is pending; this QP is written part-agnostic at the rails);
- F130, F138, R1-F14/R2-F10, A11-R03, F11 (A13), N03.

**DUT** — complete inverter 8S (and 4I), HW A.15+, with FW, CAL, VAL and OTP; three units (plan).

**Fixture and instruments** — I-PS with a test-B generator at programmable R_i; I-CP on the LV-entry TVSs (DTVSC,
DTVH, DTVL — TPSMC24CA-VR at the time of writing) and the polyfuses (FLVC, FVBH, FVBL, FH1, FL1); I-LVS on V12L
(boost input), the ULDO15 input, V15B, the flyback drains and ASC_SET_N; I-TC on the TVS cases.

**Steps**
1. Incoming check of every fitted DTV*: marking, and V_BR at 1 mA ≥ 26.7 V. The classic TPSMC24CA (V_BR
   22.8–25.2 V) must not appear.
2. Test B, 35 V / 400 ms. Energy-limited first at R_i = 4 Ω, then 2 Ω. Then the OEM minimum R_i from
   interface-requirements (LV supply — load dump test B source resistance; it records that no OEM has stated it
   yet). Go below that only for characterisation, down to 0.5 Ω (plan).
3. Apply the OEM pulse count and interval, running armed at zero torque.

**Measure** — TVS current and energy per pulse; the peak rail voltages; polyfuse behaviour; the functional status.

**Pass at the OEM R_i** —
- The boost input stays ≤ 38 V recommended (40 V abs) for the TPS55340-Q1; the predicted clamp is ≤ 33 V.
- The ULDO15 input stays ≤ 40 V (operating). CB15O1/2 see ≤ 50 V (≈ 33 V predicted).
- The flyback drain stays ≤ 80 V (61.2 V predicted).
- **TVS current.** Measured ≤ the row: 2 A / 53 W at 4 Ω, 3.7 A / 102 W at 2 Ω, 11.2 A / 328 W at 0.5 Ω.
  CONDITIONAL if higher.
- **TVS survival.** After the pulses, V_BR stays in 26.7–29.5 V and the leakage at 24 V stays within the -VR
  datasheet I_R.
- **Polyfuses.** None trips at R_i ≥ 1 Ω. Below ≈ 1 Ω a trip is expected ("sees ≈ 30 V … latches until a KL30
  cycle"): if the OEM R_i is below 1 Ω, that behaviour is FAIL.
- **Function.** The OEM functional-status class for test B. No FLT, no false DRV_EN, and the §9 restart
  behaviour is correct after any reset.

**Record** — per-R_i waveforms and TVS parameters before and after.

**On fail** — the gate ㉗ ladder:
- at 2 Ω, the TVS pulse test decides the part;
- below 1 Ω, 35 V-rated rails (the boost's own derating, not a new part);
- a larger SMC-pad TVS (5.0SMDJ class — not AEC-Q101), or the 3 kW AEC-Q101 family the round-17 datasheet round
  names (TPSMD-FL, search evidence only);
- (withdrawn in round 17, VR-18): the LV entry is let-through since rev A.16 — no clamp conducts at the 35 V plateau, so no long-pulse TVS rating is needed; QP-LV-03 judges the plateau at the rails (FS26 VSUP, boost input, LDO junctions) and the fuse current
  R_i).

### QP-LV-04 · Remaining ISO 16750-2 / ISO 7637-2 supply tests (levels per OEM)

**Closes** — contract §11 "LV transients (ISO 16750-2/7637-2 per the OEM contract)"; A6 gate 8 and A7 gate 11
("LV transients"); interface-requirements (LV supply — reverse KL30 polarity).

**DUT** — complete inverter per SKU population, with FW, CAL, VAL and OTP.

**Fixture and instruments** — I-PS; I-CAN logging INV_STATUS; I-LVS on DRV_EN, FS0B, RDY and QLVS.

**Steps** — run the OEM set: the supply-voltage range, slow decrease/increase, momentary drops, reset behaviour,
the starting profile, reverse voltage, superimposed AC, open circuit, and ISO 7637-2 pulses 1, 2a, 2b, 3a, 3b on
KL30 and KL15. The levels are an OEM input; interface-requirements names only the 9–16 V range, the jump start,
test B and reverse polarity so far (§14).

**Pass** — the OEM functional-status class for each test, plus these design checks:
- no DRV_EN pulse while FS0B or the fault latch should hold it low;
- after any reset, §9 order: FW-16 is skipped or the stored pass is used, and the ASC latch is decided from speed;
- the flyback restarts after every drop (QP-GD-02 behaviour);
- each QLVS wake inrush is ≤ 1.1 A;
- reverse polarity is blocked by the STPS5L60S / NRVBAF360T3G paths with no damage.

**Record** — per-test log.

**On fail** — per test; typical levers are the TVSs, the input capacitance, and the FS26 wake and reset
configuration.

### QP-LV-05 · FAULT_OUT wire faults (gate ⑳)

**Closes** —
- gate ⑳;
- *Safety A.8* "FAULT_OUT asserted level at the VCU (10 k to 5 V)" and "FAULT_OUT shorted to KL30 (FS1B released):
  ASC_SET_N clamp vs USCH2 V_I abs max";
- *Safety A.9* "RFS4 with FAULT_OUT shorted to KL30 and FS1B asserted (22 mA limit end)";
- A7-N04, P-01, A8-03, F94, F102, F111.

**DUT** — card + power board, 8S, with FW, CAL, VAL and OTP; the VCU emulation pulls up with 10 k to 5 V and
≤ 1 nF.

**Fixture and instruments** — a current-limited fault source (plan: 1 A limit) on the FAULT_OUT wire; I-LVS on
ASC_SET_N (at the USCH2 pin), V5A, ASC_CMD_RB and FS1B; I-CP into V5A; I-TC on RFS4.

**Steps**
1. Short FAULT_OUT to ground: with V5A up, with the card sleeping (FS26 LPOFF), and with V5A off.
2. Short FAULT_OUT to KL30 at 16 V (continuous), 24 V (60 s) and a 35 V / 400 ms pulse, in each V5A state, with
   FS1B released and then asserted into the short (FS0B event).
3. Record which of the two P-01 outcomes the fitted FS1B shows (4–22 mA current-limit spread).

**Pass** —
- **Assertion level.** With the normal VCU load, FAULT_OUT asserted reads ≤ 1.2 V (1.11 V predicted; the 5 V CMOS
  V_IL is 1.5 V).
- **Short to ground.** It never presets ASC (ASC_CMD_RB stays 0).
- **Short to KL30, clamp.** ASC_SET_N stays ≤ 5.96 / 6.12 / 6.33 V at 16 / 24 / 35 V (the 125 °C values), and
  ≤ 6.5 V abs.
- **Short to KL30, V5A.** The current into V5A is ≤ 0.14 mA live and ≤ 0.63 mA with V5A off; a V5A that is off
  rises ≤ 40 mV.
- **FS1B asserted into the short.** Either the preset works, or the FS26 counts an FS1B short-to-high and the
  firmware sets a DTC with **no MCU reset** (BACKUP_SAFETY_PATH_FS1B = 0) and no arming until the wire is
  repaired.
- **RFS4.** Dissipation ≤ 0.234 / 0.484 / 0.651 W at 16 / 24 / 35 V, for pulses of ≤ 0.1 s (FS1B_TDUR) or ≤ 0.3 s
  (boot). The held 24 V / 60 s case is judged as in QP-LV-02.

**Record** — waveforms and the FS26 diagnostic registers.

**On fail** — the ZSET class (BZT52-B5V6 ± 2 %), RFS4, or the DFO steering; a V5A back-feed points to a layout
leak.

### QP-LV-06 · Parking drain and wake inrush (gate ㉙, parking part — renumbered from the second ㉔)

**Closes** —
- gate ㉙ ("parking drain with the card in LPOFF (N17)"; renumbered in round 17 from the second use of ㉔ in the README — see §15);
- *LV A.9* "Parking drain, whole inverter", "LV feed switch inrush into the power board at each wake", "LV feed
  switch held off by a hot 2N7002", "LV feed switch V_GS from NRC";
- N17, R9X-03/04/10, F110, F116, F117.

**DUT** — complete inverter per SKU population, with FW, CAL, VAL and OTP.

**Fixture and instruments** — µA meter in KL30 (burden ≤ 1 mV at 1 mA, plan); I-CP on the QLVS drain current;
chamber at −40 / 25 / 85 °C, plus a local hot air stream on the 2N7002 to 125 °C (plan).

**Steps**
1. Park the unit (FS26 LPOFF). Measure KL30 current at 9, 13.5 and 16 V, at each temperature.
2. Wake it through KL15 20 times (plan) and capture the QLVS inrush and the VBATC dip.
3. Heat the 2N7002 while parked and check that QLVS stays off.

**Pass** —
- Parking current ≤ 0.1 mA at 25 °C (≤ 43 µA predicted). At 85 °C it stays ≤ 126 µA (the predicted bound); the
  OEM sleep budget at temperature is an open OEM input (§14).
- Inrush ≤ 1.1 A per wake, and ≤ 3 A (the limit); 0.68 A at 12 V and 1.06 A at 16 V predicted.
- QLVS V_GS stays ≤ 0.5 V while parked hot (below the 1.0 V V_GS(th) minimum). When on, it is −5.8 V at KL30 9 V
  (≤ 35 mΩ).

**Record** — current table and inrush waveforms.

**On fail** — find the unswitched load (the N17 / R9X-03 method); the QLVS gate network (10 k / 4.7 k, 1 k /
100 nF). vendor-requests.md (Diodes — DMP6023LEQ production status) names the fallback part if QLVS changes.

### QP-LV-07 · EMC emissions (CISPR 25)

**Closes** — contract §11 "EMC"; README roadmap "CISPR 25 EMC"; simulation "What simulation cannot close"
("EMI/CISPR"); A6 gate 8.

**DUT** — one complete inverter per SKU population, 8S, 8I and 4I (plan), with FW, CAL, VAL and OTP, harnesses to
the OEM layout.

**Fixture and instruments** — a CISPR 25 chamber with LV and HV artificial networks and an absorber-lined shielded
enclosure; a load (the motor on a through-wall shaft, or an inductive load bank). The edition, classes and
frequency ranges are an OEM input with no topic in interface-requirements.md yet (§14).

**Steps** — measure conducted emissions (voltage and current methods, LV and HV lines) and radiated emissions in
these modes:
- LV only;
- HV energised with the gates off;
- switching at each f_sw at a representative load;
- active discharge.

**Pass** — below the OEM class limits in every mode. The record marks the design's own sources:
- the flyback at ≈ 253 kHz (SiC) and ≈ 308 kHz (IGBT) and their harmonics;
- FS26 VPRE at 450 kHz;
- PWM at 5 / 8 / 10 kHz;
- the SWG at 10 kHz;
- CAN.

**Record** — spectra per mode.

**On fail** — Y-caps (4.7 nF Y1 per rail), the chassis RC (RPET 1 M / CPET 4.7 nF), CAN chokes, shields, or layout
changes.

### QP-LV-08 · EMC immunity with the safety functions monitored

**Closes** — contract §11 "EMC"; A6 gate 8; A7 gate 11.

**DUT** — as QP-LV-07.

**Fixture and instruments** — BCI (ISO 11452-4), ALSE (ISO 11452-2), ESD (ISO 10605) and ISO 7637-3 coupling, at
the OEM levels (an open OEM input, §14); F-HIL-style monitoring over optical links of CAN, FLT, DRV_EN and FS0B.

**Steps** — for each test and level, run at a fixed torque (plan: 30 % of rated) and log the monitored signals.

**Pass** — at each level, the functional status the OEM specifies, and for the safety functions **no event**:
- no false FLT or DESAT;
- no DRV_EN or FS0B drop;
- no FW-05 or FW-06 trip;
- |VDC1 − VDC2| < 5 %;
- |Σi| < 45 A (`cal_isum_tol_a`);
- the resolver amplitude window and tracking hold, and the frame age stays < 500 µs (`cal_rslv_hold_us`);
- no CAN E2E error or DTC;
- a stable HVIL signature.

**Record** — event log per level.

**On fail** — filtering (e.g. the CFLTF/CFLTF2 100 pF FLT filters), shield termination (resolver shields are on
DGND at the connector, F75), or layout.

---

## 6. Isolation and HV

### QP-HV-01 · Barrier parts: certificates and the PO-time check (gate ㉔, barrier part)

**Closes** —
- gate ㉔ (barrier: "the PO-time check that TI's VDE/UL certificates … have issued");
- gate ⑪ (closed; only this check remains);
- design-basis §6a;
- dfm §1 T3 PO gate;
- A12 "What stays open" (certificate status);
- interface-requirements (HV side — isolation/Y-cap class).

**DUT** — the purchase documents for each barrier element. No test unit.

**Steps** — for each element, obtain the certificate that lists the exact ordering code, and compare its certified
values with the §6a row:
- **AMC1311B** — V_IOWM 1.2 kVrms (VDE 0884-11).
- **UCC12051QDVERQ1** (production) and **UCC12050** (proto) — V_IOWM 1200 Vrms / 1697 VDC, reinforced.
- **NSI6611ASC-Q1** — reinforced, SOIC-16W.
- **VOW3120-X017T** — V_IORM 1414 Vpk, DIN EN 60747-5-5 option 1, CPG ≥ 10 mm (not AEC-Q101).
- **UCC14141QDWNRQ1** — VDE 40058888 (issued, archived); UL 1577 still "planned".
- **VY1472M63Y5UQ6TV0** — Y1 500 VAC / 1500 VDC.
- **VY2472M49Y5US6TV0** (CPET) — Y2.
- The VGT12EEM and HC5FW go to QP-HV-02 and QP-HV-03. The MGJ2 is not fitted (§15).

**Pass** — a valid certificate that names the ordering code, with certified values ≥ the §6a requirement. A
"planned" status is FAIL for release; the only exception is where an issued certificate from another body covers
the same insulation claim, as the UCC14141-Q1 VDE certificate does.

**Record** — certificate register with file numbers.

**On fail** — hold the PO; use the listed alternates only after the same check (dfm: basic-insulation substitutes
are banned by name).

### QP-HV-02 · VGT12EEM transformer insulation (gate ⑤, insulation part)

**Closes** — gate ⑤ ("VGT12EEM … working insulation"); design-basis §6a row "VGT12EEM-200S1A4 ×6" (gate ⑤);
R-F19; Marine M10 "VGT12EEM flyback transformer" (WARN, handled in QP-MA-09).

**DUT** — 10 transformers from ≥ 2 lots (plan), fresh parts not used elsewhere.

**Fixture and instruments** — I-HIP (AC, 1 mA sense); I-PD; a humidity chamber for damp-heat pre-conditioning
(plan).

**Steps**
1. Withstand at the DS values: 2.6 kVrms for 1 min NP,NF → NS, and 1.3 kVrms for 1 min coil → core.
2. PD inception and extinction, primary ↔ secondary, as-received and after damp heat.

**Pass** —
- No breakdown and leakage ≤ 1 mA (the DS sense current).
- The PD extinction voltage is ≥ the level of the insulation-coordination study for 850 V DC working,
  reinforced. That level is an open OEM input (§14).
- Release also requires TDK's working-insulation rating (vendor-requests.md, TDK — working insulation ≥ 850 VDC
  Road / ≥ 1150 VDC Marine). The test is evidence; it is not a rating.

**Record** — hipot and PD data per part.

**On fail** — a transformer with a published working rating (a magnetics change: new footprint, QP-GD-01/03
re-run).

### QP-HV-03 · Phase-busbar sleeve through the HC5FW (gate ⑩)

**Closes** — gate ⑩; design-basis §5 (N7); design-basis §6a row "HC5FW ×3"; F13 (A13); interface-requirements
(harness — hall sensor sleeve).

**DUT** — each phase busbar with its sleeve per the mechanical note (≥ 1 kV DC-rated heat-shrink or powder coat,
≥ 10 mm past both faces), fitted through an HC5FW 900-S/SP1 on its carrier. Three assemblies (plan).

**Fixture and instruments** — I-HIP; I-PD; creepage and clearance gauges.

**Steps**
1. Measure creepage and clearance from the busbar to the sensor pins and the ground plane.
2. Hipot from the busbar to the shorted sensor terminals and ground, at the level LEM signs off.
3. Measure PD at the coordination level.
4. Repeat after QP-TH-05 thermal cycling.

**Pass** —
- Creepage ≥ ≈ 6 mm (IEC 60664-1 Table F.5, PD2, material group II, at 850 V — design-basis §5). The bare sensor
  gives 3.6 mm creepage and 2.7 mm clearance.
- No breakdown; PD inside the coordination limits.
- LEM's written sign-off of the sleeve for 850 V DC working (vendor-requests.md, LEM — sleeve sign-off).

**Record** — measurements and the LEM letter.

**On fail** — sleeve specification (thickness, overlap), or an HC5FW variant with a sleeve.

### QP-HV-04 · Assembly withstand (hi-pot) and insulation resistance — DV

**Closes** — contract §11 "insulation coordination"; dfm §3 EOL hi-pot (the DV type level); A6 gate 8.

**DUT** — three complete inverters (plan), one per SKU population, before and after QP-TH-05.

**Grouping** —
- The **HV group** is DC+, DC−, U, V and W shorted together. The gate domains, the AMC1311 HV sides and the
  discharge board are part of it.
- The **LV group** is every vehicle-connector pin plus KL30 / GND, shorted to the chassis. DGND is tied to PE
  through RPET 1 M / CPET 4.7 nF, so LV and chassis form one group here.

**Fixture and instruments** — I-HIP; ramp 500 V/s (plan).

**Steps**
1. DC withstand, HV group ↔ LV group + chassis, at the OEM type-test level and dwell. That level is an open OEM
   input: interface-requirements covers the barrier class, not the test levels (§14).
2. Insulation resistance at the OEM test voltage.

**Pass** —
- No breakdown or flashover.
- Leakage after the Y-cap charging transient (4.7 nF per rail) is ≤ the OEM limit, and IR is ≥ the OEM limit.
- The applied level never exceeds the lowest published 1-min withstand on the barrier list: 2.5 kVrms (HC5FW
  U_d — with the sleeve per QP-HV-03) and 2.6 kVrms (VGT12EEM NP,NF–NS). As DC that is ≈ 3.54 kV (derived: × √2).
  The VGT12EEM coil–core figure of 1.3 kVrms also bounds the level if the layout ties the core to either side;
  confirm the core potential in the layout.

**Record** — level, dwell, leakage and IR.

**On fail** — find the path (creepage, coat, a part), then QP-HV-06.

### QP-HV-05 · Partial discharge of the reinforced barriers — DV

**Closes** — design-basis §6a ("A one-minute hipot figure is not a working-voltage rating"); contract §11
"insulation coordination".

**DUT** — the same units and grouping as QP-HV-04, before and after QP-TH-05.

**Fixture and instruments** — I-PD per IEC 60270, at the levels of the insulation-coordination study (an open OEM
input, §14).

**Steps** — raise the voltage to inception, hold, and lower to extinction, following the study's profile.

**Pass** — the PD extinction voltage is ≥ the study's value for 850 V DC reinforced, and the PD magnitude is ≤ the
agreed pC limit.

**Record** — the PD profile per unit.

**On fail** — locate the site (PCB slot or coat, transformer, opto, connector) and change it.

### QP-HV-06 · Creepage / clearance audit — layout and first article

**Closes** —
- design-basis §6a row "Busbar / HV connector spacing" ("layout");
- dfm §4 barrier rules (UCC14141-Q1 split ≥ 10 mm, matching the VOW3120's ≥ 10 mm);
- interface-requirements (HV side — isolation class: "creepage/clearance per IEC 60664-1 at the agreed pollution
  degree");
- Marine §2 creepage (PD2 sealed vs PD3 — the decision is due before the Road layout; QP-MA-09).

**DUT** — the layout database and three first-article boards of each type.

**Steps** — run a CAD clearance/creepage report on every HV-to-LV and HV-to-HV net pair. Measure the first
articles optically at each barrier part (NSI6611, AMC1311B, UCC12051-Q1, UCC14141-Q1, VOW3120, VGT12EEM), the HV
connector and the busbar.

**Pass** — every distance is ≥ the value of the pollution degree and OVC class agreed with the OEM. The
UCC14141-Q1 and VOW3120 barriers are ≥ 10 mm (dfm §4).

**Record** — distance table.

**On fail** — layout change before release.

---

## 7. Resolver and exciter fault bench

All of §7 runs on F-RX with the selected resolver, the harness replica, and FW with CAL, VAL and OTP.

**The exciter path since round 18** runs from the amplifier output node (where the Schottky DEXP/DEXN PMEG4050EP-Q
to VEXD sits, in parallel with the ALM2402's own upper diode — round 18 moved it off the protected node so the
harness charging loop passes RSX) through RSX 2.2 Ω (ROHM ESR18EZPF2R20 anti-surge since round 18) to the protected
node, then through the PTC MF-MSMF020/33X to the connector. Two parts hang on the protected node:
- the TVS SMDJ7.0A-HRA to AGND (round 19, F203: the 8.5A of rounds 17–18 left the parked-trickle case unclosable);
- the monitor tap.

**Gate ㉘ since round 18** (README ㉘): the sheet-bounded single fault (fault current ≥ 8 A — the ISO 16750-2 direct
short), the PTC hold and the 35 V double event stay characterisations (§0.1 item 3). Two measurements are RELEASE
GATES again (F192, F193): the reverse rail current into ULDOEX (QP-RX-05 — the NCV4276C's 40 V output rating covers
the node voltage only, VR-33) and the sustained-short impedance sweep with the ECU asleep (QP-RX-04 step 2b — below
8 A the PTC's trip time is a typical curve and the tripped PTC's trickle leaves 1.5–4 W in the TVS; dfm.md layout
rule, IR-42, VR-17).

### QP-RX-01 · Excitation amplitude planes and SWG trim with the selected resolver (gate ㉕, amplitude)

**Closes** —
- gate ㉕ ("8 V pp at 10 kHz … against the excitation monitor"), re-scoped in round 16 to the planes;
- *Sensing A.15* "Excitation at the WINDING with the SWG at its LOW corner" and "FW-10 trim setpoint 7.2 V pp at
  the monitor";
- *Sensing A.15* "Amplitude planes: monitor (protected node) vs winding" (INFO: "EOL characterises
  monitor-to-terminal transfer");
- *Sensing A.11* "Resolver excitation at 10 kHz";
- A15 "What stays open" ("the amplitude at the winding with the selected resolver");
- F167, A14-N01, FW-30; target-bringup T-29, T-30, T-31;
- interface-requirements (motor — excitation amplitude targets, resolver transformation ratio, resolver/exciter
  harness capacitance).

**DUT** — card, 8S parameters, HW A.15+. Three cards (plan), including the lowest-MAXAPP S32K396 of those screened
(plan: screen 10 MCUs for the SWG maximum at code 15).

**Fixture and instruments** — F-RX; I-ISO differential at the protected node (the monitor plane) and at the
resolver terminals (the winding plane); SWG output at the MCU pin; chamber at −40 / 25 / 85 °C, plus 125 °C local
on DEXP/DEXN.

**Steps**
1. At KL30 13.5 V (plan), power up and log the SWG ramp: code, time to "ready", and the monitor reading in codes.
2. Measure amplitudes at the amplifier output, the monitor plane and the winding, cold and hot.
3. Measure the SWG output at codes 0, 8, 12 and 15 (T-31), and the excitation frequency (T-30).
4. Heat DEXP/DEXN to 125 °C and repeat step 2. A reverse-biased Schottky leaks up to 300 µA at 40 V / 25 °C, and
   more hot: a DC offset the amplifier must sink.
5. Trip one PTC (energy-limited fault, QP-RX-04 step 1), restart warm within 1 h, then restart after cooling.
6. Swap in a 25 Ω resolver emulation (the host test "a_low_impedance_resolver_saturates_the_trim_with_a_dtc").

**Measure** — MAXAPP of each sample; the SWG amplitude at the setpoint; per-output amplitude and offset;
|H(10 kHz)|; winding/monitor ratio (cold and post-trip); the time to ready; the SDADC input range at the maximum
corner (T-29); DTCs.

**Pass** —
- **Trim.** It settles to 7.2 V pp ± 5 % at the monitor plane (the trim band), ready within 20 / 25 / 35 ms at the
  high / typical / low MAXAPP corner (host model). The needed SWG amplitude is ≤ the sample's MAXAPP: 1.843 V pp
  needed against a 1.884 V pp low corner, 2.2 % headroom (derived).
- **Slew.** The per-output amplitude is ≤ 4.14 V pp, the −40 °C slew ceiling; the design needs 3.82 V pp per
  output. The waveform shows no slew-limiting at −40 °C. Code 8 starts below the slew-safe level at the maximum
  corner (T-31), and the excitation is at 10.000 kHz (T-30).
- **Gain.** |H(10 kHz)| lies in **1.98–2.18** for the bound parts (R ±1 %, CEXA1/CEXA2 ±5 % C0G; nominal 2.076 with the
  coupling capacitor) — a board reading 1.94–1.98 is out of tolerance; a ±10 % build spans 1.92–2.24 (round 18, F195).
- **Winding.** The winding amplitude is ≥ 6.5 V pp cold (6.94 V pp predicted with the PTCs at 1.3 Ω; 7.13 V pp at
  R_min, 6.30 V pp at R_1max — the sheet bounds the PTC at 0.35–5.0 Ω, so this step SETS the FW-10 window from the
  measured monitor-to-terminal transfer). At the SWG low corner with the trim saturated it is 7.1 V pp predicted at
  nominal gain; at the full tolerance stack (SWG low × |H| 1.98 × REXB −1 % corner) it is 6.78 V pp at 70 Ω and
  6.48 V pp at the IR-13 60 Ω minimum — there the trim reports saturation (DTC_RSLV_SWG_SAT) and the unit does not
  arm: an EOL rejection of that board/MCU pairing, not a CONDITIONAL (round 18, F195).
- **Schottky leakage.** With DEXP/DEXN at 125 °C, the monitor stays inside the trim band and the output offset
  stays inside the swing that the "Resolver drive @9 V KL30" row allows (outputs 0.59–4.41 V).
- **Planes.** Winding/monitor is 0.964 cold (70/72.6) and 0.875 after a trip (70/80). After a trip the winding
  reads 6.3 V pp and FW-10 sets DTC_RSLV_EXCITATION; a cool restart recovers.
- **Low-impedance resolver.** With 25 Ω, DTC_RSLV_SWG_SAT is set and the unit does not arm.
- **SDADC range.** SIN/COS at the maximum excitation corner stay inside the SDADC input range with margin (T-29).

**Record** — amplitude table per plane, MAXAPP per MCU, ramp logs. The measured transfer feeds
`cal_rslv_wind_per_mon` (QP-EOL-08/14).

**On fail** —
- Setpoint and planes: `cal_rslv_exc_target_vpp` (CAL 6.5–8.3 V pp) inside the headroom checks.
- Gain: the MFB network (28 k feedback).
- Slew: `cal_swg_code_init`.
- Leakage: a new search for a lower-leakage Schottky. Of the three candidates in the round-17 datasheet round, the
  fitted PMEG4050EP-Q already leaks least (the SBR3U40P1-7 leaks 40 mA at 125 °C).

### QP-RX-02 · Resolver excitation at low KL30

**Closes** — *Sensing* "Resolver drive @9 V KL30 (round 17 recheck of the A.4.3 row)" (WARN at A.15; PASS since
round 17 — G-04, F177). This QP confirms the recomputed row.

**DUT** — as QP-RX-01.

**Fixture and instruments** — F-RX; I-PS; I-LVS on VBATC, VEXD, both ALM2402 outputs and the monitor plane.

**Steps**
1. Starting from 13.5 V, step KL30 down to 5.0 V in 0.5 V steps (plan), at −40 / 25 °C. At each step record
   VEXD, both output swings, the monitor amplitude, the SWG code and the DTCs.
2. Repeat as a dip from 13.5 V to 9 V while running at zero torque.

**Pass** —
- VEXD ≥ 4.6 V (the upper swing + 0.2 V rail margin); ≈ 8.3 V is predicted at KL30 9 V.
- The outputs stay centred on VMID_REX 2.5 V, at 0.59–4.41 V with the 7.64 V pp setpoint.
- The full amplitude, with the trim inside ± 5 % and no DTC_RSLV_SWG_SAT, holds down to KL30 ≤ 5.3 V (the row:
  "any KL30 ≥ 5.3 V keeps the full amplitude"). At 9 V this must hold with margin.
- At −40 °C, the binding limit is the slew ceiling (2.07 V pk), not the rail.

**Record** — the amplitude-vs-KL30 table.

**On fail** — reopen the row (the recomputation or the ULDOEX dropout model).

### QP-RX-03 · Harness short to KL30 into the SDADC and monitor pads (gate ㉕, injection)

**Closes** —
- gate ㉕ ("the harness short-to-KL30 injection measured at the SDADC pins");
- *Sensing A.11* "Resolver wire shorted to KL30: SDADC pin injection";
- *Sensing A.13* "Excitation-monitor pad injection";
- R2-F04/F13, R1-F20, F128, F136, F152.

**DUT** — card with the MCU powered and unpowered (V5A 0 V); 8S parameters; three cards (plan).

**Fixture and instruments** — fault source to each of SIN+, SIN−, COS+ and COS− at 16 / 24 / 35 V (60 s at
16 / 24 V, 400 ms at 35 V, plan); 35 V and 50 V at the excitation line for the monitor taps; the pad current read
across RSINR/RCOSR (120 Ω) with I-ISO; I-CP on the VMID buffer supply; I-LVS on V5A.

**Steps** — apply each fault in each power state. Afterwards, re-measure the channel gain and phase (QP-EOL-08
method).

**Pass** —
- **SIN/COS pads.** Pad current ≤ 3 mA (the S32K39 operating and absolute limit) in every power state; predicted
  1.33 / 2 / 2.92 mA at 16 / 24 / 35 V into 0 V.
- **Monitor pads.** ≤ 3 mA; predicted 1.96 mA at 35 V and 2.81 mA at 50 V.
- **VMID buffer.** It sinks ≤ 5.4 mA (the OPA348 limits at ≈ 7 mA at 125 °C). If it saturates toward V5A, FW-10
  must report the resolver invalid — never a false angle.
- **Back-powering.** An unpowered V5A is not powered up by the fault.
- **After the fault.** Channel gain and phase are unchanged within the 1.3° matching bound.

**Record** — current table and the before/after channel data.

**On fail** — the series resistors (12 k), or the clamp structure.

### QP-RX-04 · Exciter terminal fault, positive, VEXD and MCU on — and the sustained-short impedance sweep with the ECU asleep (gate ㉘; step 2b a RELEASE GATE since round 18)

**Closes** —
- gate ㉘ as re-scoped in round 17: the PTC clearing waveform (cold, hot, post-trip), the PTC, TVS and Schottky
  currents, TVS temperature, buffer unharmed;
- *Sensing A.13* "Exciter terminal fault — clamp at the protected node during the PTC trip window" (BENCH);
- *Sensing A.15* "Exciter TVS energy — single fault …" and "… — load-dump-coincident fault …" (PASS since round
  17);
- *Sensing A.15* "Exciter PTC current vs I_max 40 A — single fault at 24 V with the IR-16 minimum harness (0.05 Ω)"
  (PASS) and "… — load-dump-coincident fault (35 V)" (INFO, double event);
- *Sensing A.17* "Exciter TVS/PTC coordination BELOW the 8 A bound — a sustained short through external resistance"
  and "Sustained exciter short to the NORMAL battery (12.6–16 V) with the ECU asleep" (both WARN — step 2b closes them);
- *Sensing A.17* "RSX power during a terminal fault" (PASS with the round-18 anti-surge part; measured here);
- G-02, F153, F159, F169, F175, F193, F194; IR-42; VR-17;
- A15 "What stays open" ("the PTC clearing waveform at the allocated source impedance").

**DUT** — card, three cards × both lines (plan). The PTCs are tested at room temperature ("cold"), pre-heated to
85 °C ("hot"), and within 1 h of a previous trip ("post-trip").

**Fixture and instruments** — F-RX; I-CP separately on the PTC lead, the TVS lead and the RSX lead (the Schottky
now sits on the amplifier node: its current is the RSX current less the ALM2402 diode share); I-TC on the TVS lead,
the RSX body and the PTC; I-LVS at the protected node, the amplifier output node and on VEXD; a settable series
resistor 0–100 Ω (≥ 50 W) for step 2b; the card's sleep control (V5A off / LPOFF) for step 2b.

**Steps**
1. Energy-limited first: 24 V at a 2 A limit. Check the clamp and the monitoring.
2. **Single fault.** Apply 16 V, 24 V and 26 V (the ISO 16750-2:2023 jump start, IR-02) for 60 s to EXC+ and EXC− in
   turn, in each PTC state, with the source directly at the connector (0 Ω external — expected to exceed I_max at 24
   and 26 V, recorded), at the IR-16 minima (0.08 / 0.14 Ω) and with the harness replica. Record I_PTC(t), V_TVS(t)
   and ∫v·i (the ≥ 8 A release criterion below).
2b. **Sustained-short impedance sweep, ECU asleep (RELEASE GATE, round 18, F193).** With the card in LPOFF (V5A = 0,
   so the ALM2402 SDN is low and ULDOEX is inhibited — nothing sinks the fault current), apply 12.6, 14.4, 16 and
   24 V to EXC+ and EXC− in turn through external series resistances 0, 0.5, 1, 1.35, 2, 3, 5, 10, 20, 35, 70 and
   100 Ω, each for 10 min or until the TVS lead temperature has settled, whichever is later; abort a point at a
   TVS lead temperature of 150 °C and record the time. Repeat the 12.6 V / 20 Ω and 16 V / 5 Ω points at −40 °C
   and 85 °C chamber. Then repeat the 12.6 V and 24 V sweeps with the card awake (ALM2402 driving) and record the
   RSX body temperature. **Marine (QP-MA-11):** the same sweep at the kit rail 11.4 / 12.0 / 13.2 V and the ship
   bus 18 / 24 / 31.2 V (marine/verification-report.md §7: 5.2 W at 12.0 V, 8.3 W at 11.4 V — the platform's
   worst case).
3. **Double event, characterisation.** Apply 35 V for 400 ms with source + harness at the interface-requirements
   allocation (IR-16: exciter-line fault source + harness impedance ≥ 0.29 Ω at 35 V since round 18).
4. Repeat each 10 times per line (plan), cooling to 25 °C between repetitions.
5. After the tests, run the QP-RX-01 amplitude check.

**Measure** — I_PTC(t), I_TVS(t), I_RSX(t), the protected-node and amplifier-node voltages, the clearing time (fault
start → I_PTC below the 0.4 A trip current), the TVS energy ∫v·i, the TVS peak lead temperature, the ALM2402 and
buffer status; for step 2b at every (voltage, resistance) point: the settled TVS power and lead temperature, whether
and when the PTC tripped (by current or by the TVS's heat), and the RSX temperature awake.

**Pass** —
- **Single fault, 16 / 24 / 26 V.** I_PTC peak ≤ 40 A at the IR-16 minima (0.08 Ω at ≤ 24 V, 0.14 Ω at 26 V: 37.9 A
  and 37.4 A predicted cold — round 19, F199: the round-18 row was evaluated at 24 V only); at a true 0 Ω the prediction
  is 46 A at 24 V and 52 A at 26 V with the 7.0 V clamp — recorded, not judged (IR-16).
- **Clearing time and TVS energy**, at 16 / 24 / 26 V and 35 V, against the SMDJ7.0A-HRA's **5.3 J** at 10 ms (the point
  converted from the exponential test pulse to a rectangular one and derated to 85 °C; 6.5 J at 25 °C — round 18, F193)
  and E_cap(t) beyond it (≈ 7.1 J at 20 ms on the plain SMDJ sheet's Z_th Fig. 6 — round 19):
  - **fault current ≥ 8 A (RELEASE CRITERION since round 19, F200)** — with a stiff supply (≤ 10 mΩ) plus the harness
    replica at R_ext = 0 / 0.14 / 0.5 / 1.33 Ω, at 16 / 24 / 26 V (+31.2 V for Marine, QP-MA-11), at −40 / 23 / 85 °C and
    post-trip (re-fault within 1 h, R up to R_1max), on ≥ 5 parts from each of 2 lots: record i(t) and v_TVS(t), from
    them E_TVS = ∫v·i, q = ∫i, t_trip (i below 50 % of I0) and I_peak. Accept if E_TVS ≤ 0.5·E_cap(t), t_trip ≤ 20 ms for
    I0 ≥ 8 A, I_peak ≤ 40 A at the IR-16 allocation, and R stays within R_min–R_1max after 100 trips at the worst point.
    The sheet bounds only the 8 A point (20 ms); above it the verifier's figure (≤ 1.4 J at the 7.0 V clamp, 0.13 J at
    37 A) rests on the ASSUMED transfer of the maximum curve to a stiff source — this measurement closes the row
    ("Sensing A.15" WARN), together with VR-16.
  - **fault current ≥ 8 A and > 20 ms** — FAIL (the sheet's only maximum trip time is exceeded).
  - **fault current < 8 A** (step 2b) — judged by the sweep criteria below, not by a clearing time.
  - **TVS energy > 5.3 J** — FAIL.
- **Sweep, ECU asleep (step 2b, RELEASE GATE).** At every (voltage, resistance) point the TVS lead temperature stays
  ≤ 125 °C (T_J ≤ 150 °C at 15 K/W junction-to-lead) and no part is damaged. Where the PTC never trips, the settled
  TVS power is ≤ the steady-state figure for the measured lead temperature. A point where the TVS lead exceeds 125 °C
  but the PTC then trips on the TVS's heat within 60 s (the dfm.md island coupling), TVS undamaged, is CONDITIONAL:
  the rows are re-run with the measured coupling. A destroyed TVS is FAIL unless the end state is the fail-safe one
  (TVS short, PTC tripped and holding, FW-10 fault at key-on, nothing else damaged) — then CONDITIONAL with the
  island/coupling as the corrective action and IR-42 raised with the OEM.
- **Sweep, awake.** The RSX body stays ≤ 155 °C (ROHM ESR operating limit) and the amplifier reaches OTF, recovers,
  and is parametrically unchanged (QP-RX-01 afterwards).
- **Clamp.** The protected node stays ≤ 12.8 V while VEXD is up (8.8 V predicted at 24 V / 17.8 A with the 7.0 V
  clamp; 10.7 V / 15.6 A with the 8.5 V class of rounds 16–18), and never
  above the 18 V output abs max.
- **Double event, 35 V at ≥ 0.37 Ω.** The node stays ≤ 12.8 V (9.1 V / 37 A predicted with the 7.0 V clamp), and the
  TVS energy is ≤ 5.3 J (≈ 0.08 J predicted on the typical trip curve; 7.6 J if the PTC took the full 20 ms at 40 A —
  the double event is bounded only by the typical curve, F193). The outcome must be fail-safe: the PTC may fail open (above I_max its survival
  is not warranted), and FW-10 must then report a resolver fault. The ALM2402, the buffer and the MCU pads must
  be undamaged.
- **After the trip.** The TVS holds V_BR·P_d/(V_S − V_BR): ≈ 0.6 W at 24 V, 0.3 W at 35 V — 1.5–3.8 W at 12.6–16 V
  (step 2b) — with its lead temperature inside the SMDJ steady-state rating for the measured value.
- **TVS after the test.** V_BR stays in 7.78–8.60 V, with leakage within the SMDJ-HRA I_R (200 µA at 7.0 V).
- **Survival.** The ALM2402 and the buffer are unharmed: QP-RX-01 amplitudes unchanged, no OT flag.

**Record** — all waveforms; clearing time per (state, voltage); TVS energy and temperature. The PTC data answers
the questions in vendor-requests.md (Bourns — clearing time across 15–40 A; behaviour above the 33 V V_max).

**On fail** —
- Single-fault clamp or energy: the TVS package (the 5.0SMDJ series on the DO-218 pad is the fallback).
- A PTC current above 40 A at the IR-16 harness minimum: a series element or a PTC with a higher I_max.
- Sweep (2b): first the layout — island area, via count, PTC placement on the island (the coupling is the lever; a
  PTC with a lower trip current does not exist at 33 V, and no TVS voltage removes the trickle window — verification-report
  "Sensing A.17"); then IR-42 with the OEM.
- A non-fail-safe double event: raise it with interface-requirements (harness allocation).

### QP-RX-05 · Exciter back-drive with VEXD off and cranking (gate ㉘; the reverse rail current into ULDOEX a RELEASE GATE since round 18)

**Closes** —
- gate ㉘ ("the reverse rail current into ULDOEX") — a RELEASE GATE since round 18 (F192, VR-33);
- *Sensing A.15* "Exciter back-drive with VEXD absent — rated diversion THROUGH RSX (DEXP/DEXN on the amplifier node,
  round 18)" (PASS by I²t; the reverse current into the regulator is what this procedure decides);
- G-01, F160, F168, F174, F191, F192;
- A15 "What stays open" ("the ALM2402 reverse-diode envelope").

**DUT** — as QP-RX-04. The VEXD states are:
- off: KL30 off, and separately the card in LPOFF with ULDOEX inhibited;
- cranking: KL30 at 6, 8 and 9 V (plan).

**Fixture and instruments** — F-RX; the 0.1 Ω shunt between ULDOEX OUT and CEXD; I-CP on the RSX lead (since round 18
the whole diversion passes it) and on the DEXP/DEXN lead (the ALM2402 diode share is the difference); I-LVS on VEXD,
VBATC, the ULDOEX input and output, and both ALM2402 outputs; an SMU (12 V / 1 A compliance) on VEXD for step 4.

**Steps**
1. Energy-limited first.
2. Apply 24 V for 60 s and 35 V for 400 ms per line, in each VEXD state.
3. Run 100 pulses per line and state (plan), then retest the ALM2402 (output swing, current limit, OTF) and the
   ULDOEX regulation.
4. **Regulator reverse current (round 18).** With the fault source removed, sweep VEXD with the SMU from 0 to 12 V
   in each input state — IN at 0 V (KL30 off), IN at 4.5 and 6 V (cranking), IN at 13.5 V with INH low (inhibited)
   — logging I_OUT (into the regulator), I_IN and I_GND; then hold 11 V for 60 s per state.

**Measure** — the total back-drive current: peak, time constant, charge. The split between the Schottky and the
RSX / ALM2402-diode path. The reverse rail current into ULDOEX, and the VEXD peak.

**Pass** —
- The total envelope is ≤ the row's (7.0 V clamp, round 19/20): peak ≤ 4.0 A, τ ≈ 60 µs, ≈ 0.23 mC (99 % by 280 µs),
  the protected node held at ≈ 9.1 V by the TVS meanwhile. That puts ≈ 0.12 mJ in the Schottky and ≈ 1.0 mJ in RSX
  (the 8.5 V class: 5.0 A, 0.28–0.30 mC, 11.0–11.6 V, 0.15 / 1.5 mJ). A larger envelope is CONDITIONAL.
- The Schottky I²t is ≤ 1 mA²s (0.7 mA²s predicted against the 20 A²s of its 70 A / 8.3 ms I_FSM point — the
  criterion is I²t, not peak against a 140× wider pulse; round 18).
- The RSX / ALM2402-diode share is recorded. The row's premise is that the internal diodes (≈ 0.8 V at 5 A)
  carry only the residual above the Schottky's 0.49 V; a share that contradicts this is CONDITIONAL.
- VEXD stays ≤ 18 V abs (≈ 10.5 V predicted).
- **Reverse rail current into ULDOEX (RELEASE GATE, F192 / VR-33).** In every input state of step 4 the current
  into the regulator's OUT pin is ≤ 100 mA at any VEXD up to 12 V and returns to the leakage floor once the
  diversion pulse has ended (steps 2–3: the shunt current after 300 µs); VBATC does not rise (no back-powering that
  could wake the card — INH follows V5A); after 100 pulses per state V_Q, dropout, I_q and the current limit are
  unchanged. A sustained reverse path (the 60 µs pulse turning into ≈ 1.3–1.6 A DC through RSX, 3–5 W) is FAIL:
  the on-fail action is a blocking element in the regulator's output path — prescribed only then. The 40 V V_Q
  maximum rating is a node-voltage rating and closes nothing here.
- The ALM2402 is parametrically unchanged after the 100 pulses (swing, current limits ≈ 750 / 550 mA, OTF).

**Record** — envelope and split table per state.

**On fail** — a Schottky with lower V_F or higher I_FSM on the same diversion, or a series element in the RSX path;
a reverse current into ULDOEX beyond the criterion: a blocking element in the regulator's output path (VR-33's
answer may close it without one). vendor-requests.md has no ALM2402 envelope question; the rated diversion replaced it.

### QP-RX-06 · Exciter negative terminal fault (gate ㉘, where allocated)

**Closes** — gate ㉘ ("positive and (where the OEM allocates it) negative"); *Sensing A.15* "Exciter negative
terminal fault — UNIDIRECTIONAL SMDJ8.5A-HRA forward-conducts" (PASS, confirmed here); F168 (negative part).

**DUT** — as QP-RX-04. It runs only if the OEM allocates negative faults. interface-requirements has no such
allocation yet (§14); without it the record reads "not allocated".

**Fixture and instruments** — F-RX with −24 V (and −35 V where allocated); the current probes as in QP-RX-04.

**Steps** — energy-limited first; then −24 V for 60 s per line with VEXD on and off.

**Pass** —
- The protected node stays at −0.7 … −1.2 V. The SMDJ7.0A-HRA forward current is ≤ its I_FSM of 300 A; ≈ 27 A is
  predicted until the PTC trips.
- The amplifier's lower diode carries ≤ 0.3 A through RSX (≈ 0.23 A / 114 mW predicted).
- The PTC clears, as in QP-RX-04, and the ALM2402 is unharmed.

**Record** — waveforms.

**On fail** — TVS package or rating; RSX value (the ERJ-8ENF2R20V stress is stated).

### QP-RX-07 · PTC hold current against the excitation current, and post-trip recovery (gate ㉘)

**Closes** —
- gate ㉘ ("the PTC's 70 mA hot hold against the measured excitation current"; round 17: "against the selected
  resolver's measured impedance");
- *Sensing A.15* "Exciter PTC hold current (MF-MSMF020/33X, 0.07 A at 85 °C) vs the excitation current at the
  interface minimum resolver impedance" (PASS since round 17 — G-03, F176);
- A15 "What stays open" ("the measured excitation current against the 70 mA hot hold");
- F161, F170, A13-R06, A14-R03;
- interface-requirements (motor — resolver primary impedance ≥ 60 Ω at 10 kHz).

**DUT** — card with the selected resolver on the replica harness, three cards (plan).

**Fixture and instruments** — F-RX; I-LCR for the resolver primary impedance at 10 kHz; true-rms current in each
exciter line (I-CP); chamber, with the PTC local ambient known from QP-TH-04.

**Steps**
1. Measure the resolver primary impedance at 10 kHz, cold and hot.
2. Run at the trim setpoint for 8 h (plan), at the PTC's local ambient for an 85 °C product ambient, and at
   −40 °C.
3. Record the line current and PTC resistance, and any trip.
4. Trip a PTC and log the monitor and winding amplitudes over 1 h, plus the FW-10 flags.

**Pass** —
- The resolver primary impedance is ≥ 60 Ω at 10 kHz.
- The excitation current is ≤ 42.4 mA rms: the interface value, 7.2 / (2√2 × 60); the verifier's row gives 41 mA,
  and ≈ 34 mA with the 70 Ω screening resolver. It is also ≤ the PTC hold at the measured ambient: 70 mA at
  85 °C, 0.12 A at 70 °C (Bourns derating table).
- No nuisance trip in 8 h.
- After a trip the resistance falls to ≤ 5.0 Ω within 1 h (R_1max), the winding reads ≈ 6.3 V pp, FW-10 flags it,
  and the flag clears once the PTC cools.

**Record** — impedance, current, resistance, trip and flag logs.

**On fail** — a resolver below 60 Ω is an interface non-conformance for the OEM. Otherwise a PTC with a higher
hold current (the MF-MSMF family), or placement with more free air (dfm).

### QP-RX-08 · Motor-temperature line terminal fault (gate ㉘)

**Closes** — gate ㉘ ("each motor-temperature line to 24 V/60 s and 35 V/400 ms"); *Sensing A.13* "Motor-temp line
KL30 short: SMAJ5.0A carries the fault until FMT opens"; F154, F18 (A13); interface-requirements (motor —
motor-temperature sensor type).

**DUT** — card, both motor-temperature lines, with the MCU on, off and in LPOFF standby; three cards (plan).

**Fixture and instruments** — fault source at 16 V and 24 V (60 s) and 35 V (400 ms) through the harness replica
(≈ 1 Ω fuse + harness); I-CP on TVSM; I-LVS on the OPA333 input and on VREF5.

**Steps** — apply each fault per line and state; replace the fuse between tests; check the line function after
replacing FMT.

**Pass** —
- FMT (0438.375WRA, I²t 0.0041 A²s) opens: ≈ 16 µs at ≈ 16 A predicted.
- The TVSM clamp stays ≤ 9.2 V, with the current ≤ its 43.5 A I_PP.
- The OPA333 input current is ≤ 3.9 mA (the 10 mA abs limit).
- VREF5 stays in regulation, with injection < 0.5 mA and no FS26 VREF fault.
- After the fault the open line is reported as a DTC (FW-13), and the line works again after the fuse is replaced.

**Record** — waveforms.

**On fail** — the fuse or TVS rating; the 1 k series resistor.

---

## 8. Discharge

### QP-DS-01 · Active and passive discharge timing with both witnesses

**Closes** —
- S5 (four rows, measured here);
- *Discharge — 8XX values* and *— 4XX values* rows: "Passive 850→60 V, worst", "Active + passive 850→60 V, worst
  corner", "Bleeder V/W", "V per wirewound";
- README "DC-link discharge — checked"; FW-17 and FW-18;
- FW-02 and target-bringup T-37 ("the discharge τ per bank").

**DUT** — 8S and 4I complete inverters with FW, CAL, VAL and OTP; LCR values from QP-EOL-03 recorded.

**Fixture and instruments** — I-HVS with a series-diode-isolated charge path, so the source cannot hold the link;
the contactor state emulated on CAN (interface-requirements, HV side — contactor/precharge feedback contract); an
I-HVD reference on the link; both V_DC channels logged by FW.

**Steps**
1. Charge to 850 V (8XX) or 500 V (4XX). Report the contactors open. Command a discharge. Log to 60 V.
2. Repeat with QDIS disabled (JDIS unplugged): passive only.
3. Force one witness invalid (VOFS outside 0.475–0.525 V, via fault injection on the card) and repeat step 1.

**Pass** —
- **Active + passive, 850 → 60 V.** PASS ≤ 1.57 s nominal, scaled by the measured R and C (derived); FAIL above
  1.81 s (8XX, the worst corner) or 1.70 s (4XX, 500 → 60 V; 1.47 s nominal). The OEM crash target is ≤ 2 s.
- **Passive only.** ≤ 65.3 s (8XX) or ≤ 88.5 s (4XX), within the 120 s service rule.
- **Witness.** Both channels see the decay within 200 ms, and τ = R·C lies within ±20 % of 0.60 s (8XX) or 0.71 s
  (4XX) (FW-02).
- **Invalid witness.** With a witness invalid, the HV state on CAN reads "unknown", never "safe" (FW-18).

**Record** — V(t) on both channels and the reference; τ; the CAN states.

**On fail** — the resistor values (RDIS 470 / 220 Ω, RBLD 22 k / 15 k), or the witness software; a bank
capacitance out of range goes back to QP-EOL-03.

### QP-DS-02 · Repeated-discharge pulse capability (3 × 32 J per 5 min)

**Closes** — *Discharge* "Energy per 10 W wirewound (C+10 %)" (32.1 J / 27.6 J against the 100 J single-pulse
class; "firmware ≤ 3 discharges/5 min"); the R-F22 specification ("≥ 3 × 32 J pulses per 5 min"); FW-17 target
"Discharge resistor thermal".

**DUT** — the discharge board on 8S and on 4I, in the product enclosure.

**Fixture and instruments** — as QP-DS-01; I-TC on each SQP10 body; 4-wire R before and after.

**Steps**
1. Run three discharges from 850 V (500 V for 4I) inside 5 min, at 25 °C and at the enclosure's 85 °C.
2. Request a fourth inside the same 5 min.
3. Repeat the whole sequence 20 times (plan).

**Pass** —
- The energy per resistor, ∫v·i, is ≤ 32.1 J (8XX) or ≤ 27.6 J (4XX).
- The resistor voltage is ≤ 212.5 V (8XX) or ≤ 125 V (4XX), against the ≥ 350 V axial class.
- FW-17 refuses the fourth discharge.
- After the sequence each SQP10 is inside 470 Ω or 220 Ω ± 5 % (J tolerance), undamaged, and back to ambient
  temperature before the next window.
- The bleeder resistors show ≤ 0.96 W (8XX) or ≤ 0.49 W (4XX) each; their temperature is recorded.

**Record** — energies, temperatures and resistances.

**On fail** — the resistor class, or the FW-17 window.

### QP-DS-03 · Stuck-ON QDIS: resistor fail-open characterisation, contained (gates ⑮, ㉖)

**Closes** —
- gate ⑮ (A6-R11) and gate ㉖, which is a characterisation since round 17: "the ⑮ stuck-ON test now only
  characterises the fail-open time";
- *Discharge — 8XX values* and *— 4XX values* "QDIS stuck ON with the battery connected", and *Discharge*
  "Stuck-ON QDIS with the battery connected, 8XX" / "…, 4XX". All four were WARN at A.15 and are PASS since round
  17 on the TT/Welwyn statement (G-05, F178);
- F23, F157, A6-R11 (the criteria in review-A7-disposition), R1-F17, R2-F16.

**DUT** — discharge boards with SQP10-470RJB15 (8XX) and SQP10-220RJB15 (4XX). Five boards per SKU per condition
(plan). QDIS is emulated shorted with a wired link across the FET: "a pre-existing FET short". An alternate
resistor is tested only if it carries the same no-flame statement (BOM note).

**Fixture and instruments** — a battery emulator (850 V or 500 V, ≥ 1 A, low impedance, with a fast series
breaker for safety); a fire-safe enclosure; video and I-TC/IR; I-HIP for the post-opening withstand.

**Steps**
1. Connect the source to the link through the discharge board, with QDIS shorted. The string carries 0.45 A
   (8XX, 96 W per resistor) or 0.57 A (4XX, 71 W per resistor).
2. Record until the string opens, or 60 s plus a margin (plan: 120 s).
3. Run at 25 °C, hot (85 °C board), and after the QP-DS-02 repeat sequence.
4. After opening, apply ≥ 1.2 kV DC across the opened resistor for 60 s (plan) and measure the leakage.
5. Check the passive bleeder and the rest of the discharge board.

**Pass** —
- **No flame, no incandescent particles, no ejected parts.** This is the TT/Welwyn statement the rows now rest on
  ("will not burn or emit incandescent particles under any condition of applied temperature or overload"). A
  violation is FAIL: the statement is contradicted.
- **Fail-open characterisation** against the A6-R11 specification: the resistor opens at 70–100 W within 60 s,
  and the opened resistor holds ≥ 1.2 kV DC. An opening slower than 60 s, but without flame, is CONDITIONAL. It
  is recorded, the rows stay PASS on the no-flame statement, and the functional bound is FW-26 detection plus the
  service lock (QP-DS-04).
- The string current goes to zero and stays there once open.
- The passive bleeder still discharges within ≤ 65.3 s (8XX) or ≤ 88.5 s (4XX), and the PCB shows no tracking.

**Record** — video, time to open, temperatures, and the leakage after opening. The record answers the question in
vendor-requests.md (TT Electronics / Yageo — SQP10 fail-open data under sustained overload).

**On fail** —
- A flame: change the part. No 10 W family states an engineered fail-open; the Bourns FW and TE FWFU families do,
  at ≤ 7 W. A string of more, smaller fusible parts is a design review (an HV fuse was rejected: it "cannot tell
  the discharge pulse from the fault").
- A slow opening: record it; the service lock (QP-DS-04) bounds the stuck-ON condition.

### QP-DS-04 · Stuck-ON / stuck-off detection and the FW-32 service lock

**Closes** —
- FW-26 (F157; "service required" latched);
- FW-18 stuck-off;
- FW-32 (contract §10c: the service-lock clear);
- target-bringup T-26 (UDS 0x7E1 on bus 1) and T-35 (the product key function);
- FW-19, and T-37 (precharge τ).

**DUT** — 8S complete inverter with FW, CAL, VAL and OTP, on F-HIL for the vehicle and a real HV link. Two builds:
the default build, which has no key function (`TI_UDS_KEY_FN` absent), and the product build with the OEM key
(T-35).

**Fixture and instruments** — an external relay across QDIS (emulates the short without damaging parts); the
battery emulator with a contactor; I-CAN with a UDS client on bus 1.

**Steps**
1. With the contactors closed, close the QDIS relay. Open the contactors (VCU emulation) with no discharge
   command.
2. Power-cycle KL30 three times (plan).
3. Precharge with the relay still closed.
4. **Default build.** Request a seed (0x27), then start routine 0xF010.
5. **Product build.**
   - With HV present and with the bridge armed: unlock, then start the routine.
   - With HV absent: unlock, start the routine, then power-cycle.
   - Send three invalid keys.
6. With the relay open and JDIS unplugged, command a discharge.

**Pass** —
- **Step 1.** The uncommanded decay at the active rate is detected. The firmware latches the DTC and sets
  INV_STATUS b14.1 ("do not re-energise") and b14.2 ("open the contactors"). The record is kept in NVM, and the
  unit does not arm.
- **Step 2.** The lock is re-applied at every boot.
- **Step 3.** FW-19 refuses arming: the plateau is ≈ 5 % below the pack (97.5 % threshold), or the charge time
  constant is short.
- **Step 4.** The seed request returns NRC 0x22 and the routine NRC 0x33. The lock stays: fail closed.
- **Step 5.**
  - With HV present or unknown, or armed, the routine returns NRC 0x22 and nothing is written.
  - With HV absent, the routine rewrites the NVM record as CLEARED with the key cycle and sets
    DTC_SERVICE_LOCK_CLEARED. The running key cycle keeps the lock, and the next power-up arms.
  - Three invalid keys lock SecurityAccess out until the MCU restarts.
- **Step 6.** No decay within 200 ms raises the stuck-off DTC, and the passive bleeder completes within
  ≤ 65.3 s.

**Record** — CAN/UDS log, DTC and NVM dumps.

**On fail** — firmware (the discharge and UDS modules).

---

## 9. Safety chain and ASC

### QP-SF-01 · ASC entry on all six V_GS, every entry path (gate ⑭)

**Closes** —
- gate ⑭;
- RR05, A7 gate 5, contract §11 ("the ASC entry measurement");
- *Safety A.8* "ASC break-before-make: HS off before LS on" and "ASC entry, latch set → LS gates on (worst)";
- "ASC latch preset low level";
- F79.

**DUT** — 8S and 8I on F-NOHV, with FW, CAL, VAL and OTP. The UCC14141-Q1 output value is recorded (QP-GD-04), so
that the unit with the lowest output is used for the low-end check.

**Fixture and instruments** — I-ISO on all six V_GS at the Kelvin pins; I-LVS on ASC_REQ (U4 / PTD7), ASC_Q,
ASC_CMD, DRV_EN, FS0B, FS1B, the ASC pins and the six PWM pins.

**Entry paths**
- **(a) MCU path.** An injected over-voltage at the card receiver (QP-FW-02 method) → eFlexPWM FAULT1 takes the
  high sides off → ASC_REQ → PWM-ASC after the dead time.
- **(b) FS1B path.** The MCU is halted by I-DBG → FS26 watchdog → FS0B drops DRV_EN, FS1B sets the latch → CASCD
  delay → the low sides enter ASC with EN low.
- **(c) FS0B + FS1B together.** FW-16 step a, at standstill.

**Steps** — run each path 20 times (plan) at KL30 9 / 13.5 / 16 V and −40 / 25 / 85 °C.

**Pass** —
- On every path, every high-side V_GS falls below V_GS(th) min before any low-side V_GS rises above it
  (break-before-make).
- The low-side ASC starts ≥ 3.4 µs after the latch sets (contract §4c). The FS1B-path prediction is ≥ 4.42 µs.
- Entry completes — all three low sides at ≥ 90 % of VCC2 (plan) — within ≤ 7.56 µs of the latch setting. The
  7.56 µs is the verifier's worst case at the UCC14141-Q1 low end; the FW-06 906 V end-point assumes it.
- **MCU path.** The high sides begin to turn off ≤ 0.13 µs after the fault. PWM-ASC starts no sooner than the dead
  time (1.0 µs SiC, 2.5 µs IGBT). EN stays high.
- **FS1B path.** ASC_SET_N ≤ 0.84 V while asserted (the USCH2 V_T− minimum is 1.07 V). The latch survives the MCU
  reset.

**Record** — timing table per path and corner.

**On fail** — CASCD / DASCR (the break-before-make RC); the UCC14141-Q1 output (QP-GD-04); the firmware dead-time
wait.

### QP-SF-02 · ASC exit and release timing (FW-06a)

**Closes** — FW-06a target "Scope capture of the exit edge"; target-bringup T-36 (the ASC exit edge and the
ASC_REQ re-edge pulse width); the A.12 re-verification "release 1.06 µs" (review-A12 F141 row; *Safety A.8* "ASC
entry, latch set → LS gates on (worst)" note "release ≤ 1.06 µs"); §15 item 1.

**DUT** — as QP-SF-01.

**Fixture and instruments** — as QP-SF-01, plus the ASC_CLR line.

**Steps** — from PWM-ASC, command the FW-06a exit to SPO and to modulation, 50 times each (plan), at −40 / 25 /
85 °C, for SiC (1.0 µs dead time) and IGBT (2.5 µs).

**Measure** — from the ASC_CLR edge: the ASC pins falling below V_ASCL 1.3 V; the low-side V_GS falling below
V_GS(th) min; the first high-side V_GS rising above V_GS(th) min. Also the width of the ASC_REQ re-edge pulse.

**Pass** —
- The ASC pins release ≤ 1.06 µs after ASC_CLR (the A.12 value; the contract still says ≤ 0.75 µs, see §15).
- There is no overlap: every low-side V_GS is below V_GS(th) min before any high-side V_GS rises above it. This
  must hold for SiC and IGBT, with `asc_exit_hs_delay_ns` at 1000 ns. T-36 checks only the ≥ 1 µs gap; this QP
  adds non-overlap.
- The ASC_REQ re-edge pulse is ≥ the latch's minimum clock pulse width (74LVC1G74-Q100 datasheet).

**Record** — timing distribution.

**On fail** — raise `asc_exit_hs_delay_ns` to cover the measured release plus the low-side turn-off. This is a
parameter change with no hardware change. Update contract §4c.

### QP-SF-03 · DESAT during PWM-ASC on the no-HV fixture (gate ⑲)

**Closes** —
- gate ⑲;
- R7-01, A7-N01, T7-01, F90, F99;
- A8 gate 2 ("Repeat with MCU reset and FS1B");
- *Safety A.8* "Latched driver FLT masks ASC on every path (UASCG) before DRV_EN drops";
- contract §4c DESAT-during-ASC table.

**DUT** — 8S and 8I on F-NOHV, with FW, CAL, VAL and OTP.

**Fixture and instruments** — the F-NOHV DESAT link on one low side; I-ISO on all six V_GS; I-LVS on ASC_CMD,
FLT_LS_N, the latch, DRV_EN, the ASC pins and FFLAG (through a debug GPIO, plan).

**Steps**
1. Enter PWM-ASC by the MCU path, and open the DESAT link on one low side.
2. Repeat with an MCU reset issued 100 µs after the FLT (plan).
3. Repeat in FS1B-ASC with the MCU halted (EN low). The driver's DESAT behaviour with EN low is the QP-SF-04
   question.
4. Then run the authorised FW-15 recovery.

**Pass** —
- The faulted gate stays off through IN low, EN low and ASC (DS Fig. 8.11).
- UASCG takes ASC_CMD low ≤ 11 ns after FLT, and the healthy low-side ASC pins release ≤ 1.07 µs after FLT.
- DRV_EN drops ≥ 22 µs after FLT, and the eFlexPWM fault forces PWM low.
- The bridge ends in SPO.
- Nothing turns back on until the FW-15 reset. FW-15 clears the ASC latch before the reset edge, so ASC_CMD_RB
  reads 0 before FFLAG is cleared.
- After an MCU reset, ASC does not return while the FLT is latched.

**Record** — waveforms per path.

**On fail** — the UASCG wiring or FW-15 sequence; the release is blocked.

### QP-SF-04 · DESAT with ASC high and EN low — characterisation (gate ⑰)

**Closes** — gate ⑰ (the measured complement); contract §4c FS1B path ("A NOVOSENSE statement … is a release
gate"); RR05 residual; A7 P2; interface-requirements (safety-concept assumptions — the one named double fault).

**DUT** — ten drivers across three boards (plan), on F-NOHV.

**Fixture and instruments** — as QP-SF-03.

**Steps** — in FS1B-ASC (EN low, ASC high), open the DESAT link on a low side; record OUT and FLT. Repeat with IN+
low and EN high (the other "irrelevant" row of DS §8.12). Run at −40 / 25 / 125 °C.

**Pass / decision rule** —
- If OUT goes low and FLT asserts in every sample and state, the result *supports* the vendor statement. The gate
  itself closes only with that statement (vendor-requests.md, NOVOSENSE — RST/EN during soft turn-off and the
  EN-low + ASC-high row).
- If OUT stays high in any case, the residual is confirmed. FW-12 keeps FS1B-ASC only for motors that need it
  (contract §6), and the FMEDA carries the double fault.

**Record** — per-sample behaviour table.

**On fail** — no hardware change is planned. The residual stays a documented double fault and a motor-release
input to QP-SF-10.

### QP-SF-05 · Fault latch, one-shot clear and FW-15 recovery timing

**Closes** —
- FW-15 target ("one-shot timing") and target-bringup T-37 (the one-shot);
- *Gate drive* "Fault-latch CLEAR one-shot (15 nF into 10 k, at the USCH output)" (72–210 µs ≥ 49 µs);
- *Safety A.8* "FLT diode-OR low level" and "FS0B load at its V_OL point";
- *Safety* "FS0B → driver EN path";
- F70, F77, F91, RR03, R8X-09;
- contract §7 steps 1–4; the round-16 retry-gate incidental.

**DUT** — 8S on F-NOHV, with FW, CAL, VAL and OTP.

**Fixture and instruments** — I-LVS on PTD9 (FLT_CLR), the Schmitt-buffer output, ULAT2 /Q, DRV_EN, FS0B, the FLT
lines and the six PWM pins; the DESAT link.

**Steps**
1. Trigger a DESAT. Let FW-15 run the recovery and capture it.
2. Re-pulse the clear pin from the debugger: fast (< 72 µs spacing) and slow (> 1.3 ms) (plan).
3. Request a VCU-authorised retry at 999 ms and at 1001 ms after the event.
4. Trigger a second DESAT after the retry.
5. Reboot with a DESAT record in NVM.

**Pass** —
- The one-shot holds CLR low for 72–210 µs at the buffer output (≥ 49 µs).
- DRV_EN rises 16–46 µs after /Q.
- The FLT → DRV_EN delay is 22–53 µs, and FS0B → EN takes two gate delays (≈ 20 ns).
- DRV_EN stays low ≥ 1.5 ms before the reset edge, so t_FLT_MUTE (0.55–1.3 ms) is honoured.
- **Re-pulsing.** Fast re-pulsing gives no RST/EN edge. Slow re-pulsing leaves PWM held low by the eFlexPWM lock,
  and FFLAG is cleared only in step 4.
- **Retry gate.** The retry at 999 ms is refused and the one at 1001 ms is allowed (microsecond comparison). The
  second DESAT latches the DTC. The NVM record blocks automatic arming at the next boot.
- **Static levels.** The FLT diode-OR low is ≤ 0.75 V and FS0B ≤ 0.4 V (V_T− minimum 1.07 V).

**Record** — timing table.

**On fail** — CCLR / RLAT2 (15 nF / 10 k), the delay RC (10 k / 3.3 nF), or firmware (FW-15).

### QP-SF-06 · FS26 on silicon: watchdog cadence, FS0B/FS1B, GPIO1, reset re-entry (gate ⑦, runtime part)

**Closes** —
- gate ⑦ (runtime part; the per-field OTP check is QP-EOL-02);
- FW-12;
- target-bringup T-21 (an injected lockstep fault reaches the FS26), T-24 (SWT), T-32 (the answer arithmetic and
  the round-17 cadence finding), T-33 (MCU-only reset, release after RSTB);
- firmware README item 11; R-F38, RR03;
- vendor-requests.md (NXP — FS26 across an MCU-only reset for the ordered OTP variant).

**DUT** — card, 8S, with FW, CAL, VAL and OTP; F-HIL.

**Fixture and instruments** — I-DBG; SPI bus analyser; I-LVS on the SPI chip select, FS0B, FS1B, RSTB, GPIO1,
DRV_EN and FAULT_OUT.

**Steps**
1. Run normally for 10 000 watchdog refreshes. Capture every answer's time within its window.
2. Starve the watchdog, then send wrong answers.
3. Force an MCU-only reset (SWT, and separately a debug reset) with the FS26 in NORMAL. Then force an RSTB.
4. Inject a lockstep fault (T-21).
5. Hold FS1B shorted high (QP-LV-05) at boot.
6. Park in LPOFF and wake.

**Pass** —
- **Cadence (T-32).** The watchdog error counter stays 0 over 10 000 refreshes. Every answer falls inside the open
  window (open 1.5–3.0 ms after the previous answer) with ≥ 0.25 ms margin on both sides.
  - With the old ≥ 2000 µs due rule, the round-17 finding predicts answers every ≈ 3.0 ms, at the end of the
    window: FAIL.
  - With the ≥ 1500 µs rule (`WD_DUE_US` in `fs26.c` at the time of writing), the prediction is ≈ 2.0 ms.
  - The record states which rule the tested image carries.
- **Starvation.** FS0B asserts within about two windows. WD_ERR_LIMIT is 2 and the window is ≤ 3 ms, so ≤ 6 ms
  (derived). DRV_EN falls with it.
- **FS1B.** It asserts with FS0B (FS1B_TDELAY = 0): ASC_CMD_RB reads 1 within 20 µs (FW-16 step a). FAULT_OUT gives
  a 100 ms pulse per event (TDUR).
- **Release.** FS0B is released only after FLT_ERR_CNT is back to 0 and a token-derived SPI write. After an
  MCU-only reset the INIT values read back, and FS0B is released within `cal_sensor_selftest_ms` +
  `cal_fs0b_release_ms` (T-33).
- **Lockstep.** An injected lockstep fault reaches the FS26 through FCCU (T-21). A stopped task resets the MCU
  after 50 ms (SWT, T-24).
- **FS1B short-to-high.** It gives a DTC without an MCU reset (BACKUP_SAFETY_PATH_FS1B = 0).
- **GPIO1.** It stays low from POR until §9 step 6 (not slotted).
- **LPOFF.** LPOFF → wake follows §9.

**Record** — SPI traces and the timing histogram.

**On fail** — the FW-12 refresh scheduling (the T-32 proposal), or the FS26 OTP variant (QP-EOL-02).

### QP-SF-07 · ASC hold-up through total LV loss, and ASC during VCC2 UVLO (S10)

**Closes** —
- S10 "ASC hold-up after TOTAL LV loss (gate reservoirs)" (WARN: 3.1 ms typical, 0.5 ms worst);
- *Safety* "ASC drive path" (WARN: "behaviour DURING VCC2-UVLO is unspecified — NOVOSENSE statement requested …
  and the ASC-hold measurement with SBC-held gate power is a procedure in docs/qualification-plan.md");
- design-basis §10a "ASC hold-up operating limit";
- interface-requirements (LV supply — total LV loss); §15 item 3.

**DUT** — 8S and 8I on F-NOHV, with FW, CAL, VAL and OTP.

**Fixture and instruments** — a KL30 disconnect switch (< 1 µs bounce-free, plan); I-ISO on the three low-side
VCC2 and V_GS; I-LVS on V15, the UCC14141-Q1 output, the VOW3120 output (the ASC command path) and GPIO1.

**Steps**
1. Hold ASC (MCU path), then open KL30. Measure until each low-side driver parks its gate at UVLO. Run at the
   nominal rail and at the lowest VCC2 found in QP-GD-01, at 25 °C and −40 °C (MLCC bias worst).
2. Measure the collapse of the command path.
3. With KL30 present and the MCU halted, hold FS1B-ASC with GPIO1 holding the flybacks for 1 h (plan).
4. Lower VCC2 through UVLO while ASC is held.

**Pass** —
- The hold-up (KL30 off → first low-side gate parked by UVLO, falling 9.0–11.8 V) is ≥ 3.1 ms from the nominal
  rail at 25 °C (S10 typical) and ≥ 0.5 ms from the low-corner rail at −40 °C (S10 worst). Shorter is CONDITIONAL:
  S10 is re-fitted and the §6 total-LV-loss row is re-judged.
- The command path collapses in ≈ 1 ms. That figure is recorded as measured; see §15 item 3.
- With KL30 present, VCC2 stays ≥ 13.5 V throughout the 1 h hold.
- ASC releases cleanly when VCC2 falls through UVLO (VCC2 UVLO outranks ASC), with no chatter. The observed floor
  is compared with vendor-requests.md (NOVOSENSE — ASC during a VCC2-UVLO transition).

**Record** — decay curves and the command-path timing.

**On fail** — hold-up is not credited for safety (contract §6). A shorter hold-up tightens the motor-release rule
(QP-SF-10, rule (c)). Chatter at UVLO calls for a hysteresis review.

### QP-SF-08 · V5GD loss, hover and the supply-domain matrix (gate ㉑)

**Closes** —
- gate ㉑;
- A9 gate 7a (hover, R9X-06) and A8 gate 6a (the supply-domain matrix, R8X-10, P-03);
- A8-01, R9X-01, R9X-13, F108, F114;
- contract §4c "Gate-logic supply loss" / "A dead V5GD can hover".

**DUT** — 8S complete inverter on F-NOHV, with FW, CAL, VAL and OTP.

**Fixture and instruments** — a switch in the power-board V5GD LDO output (UGDL), plan; I-LVS on V5GD, the FLT/RDY
lines, DRV_EN, PWM, ASC_CMD and the V5GD_SNS pin (PTD27); I-CP on V5GD.

**Steps**
1. Open V5GD with V5A up, both at standstill and in PWM-ASC.
2. Hold V5A up while forcing V5GD low.
3. Hover: with the drivers unpowered, keep the NTC pull-ups live and drive EN/PWM from the card. Read V5GD.
4. Power down normally and measure the per-line current into the MCU pads while V5GD outlives V5A.

**Pass** —
- FLT and RDY read low. The fault latch sets and UASCG masks ASC; the eFlexPWM forces PWM low. DRV_EN falls after
  the FW-22 hold (60–160 µs after the ASC_CLR), and the bridge reaches SPO.
- No card output drives the dead domain.
- The firmware sees V5GD outside 4.75–5.25 V, forces SPO, marks V_DC invalid (no false "0 V bus") and sets the
  supply DTC. The unit does not arm.
- During hover, the V5GD voltage is recorded, and detection plus the forced SPO still act.
- At power-down, ≤ 0.9 mA per FLT/RDY line flows into the unpowered pads.

**Record** — hover voltage and currents.

**On fail** — per contract §4c: "If the NTC path alone can hold V5GD up, the NTC clamps move to a zener to
ground."

### QP-SF-09 · FW-16 self-test and the energy-limited latent-short fixture (gates ㉒, ㉔ self-test part)

**Closes** —
- gate ㉒, FW-16 part ("skip/stored-pass rule, the FS_GPIO1 steps, FLT injection with the pad rule");
- gate ㉙, self-test part ("the self-test energy fixture at ≤ 13 V");
- A8 gate 6b; A9 gate 6;
- *Safety A.9* "FW-16 self-test residual energy, 8XX bank" and "…, 4XX bank" ("Fixture-qualified");
- R8X-02/03/17, A8-N03, R9X-07, F93, F98, F109, F118;
- target-bringup T-15 (no false FLT trip with the fault-input glitch filter off), T-16 and T-17 (the FLT-pad
  OBE and the re-lock after step h), and T-37 (FW-16 chain timings).

**DUT** — 8S and 4I complete inverters on F-NOHV plus the HIL vehicle, with FW, CAL, VAL and OTP. A modified card
set carries stuck-term injection points (plan: jumpers that tie one chain term permissive at a time).

**Fixture and instruments** — the link charged to a set voltage from I-HVS; the latent high-side short emulated by
a link across one HS device; I-LVS on DRV_EN_RB, ASC_CMD_RB, FS_GPIO1 and FFLAG.

**Steps**
1. **Healthy pass.** Run FW-16 at key-on with its conditions met, and time steps a–h.
2. **Stuck terms.** Tie each term permissive in turn: FS0B path, UAND1.B, RDY_HS, RDY_LS, the ASC clock, the ASC
   clear, the FS_GPIO1 OR inputs, the FLT diode-OR, USCH2 (dead), ULAT (dead). Run FW-16 each time.
3. **Skip / stored pass.** Reset the MCU with the contactors closed. Reboot at speed.
4. **Energy.** Charge the link to a reading below 3 V on both channels, and separately to 60 V. Let the firmware
   do the QDIS 2τ top-up.
5. **Latent-short fixture.** Charge the link to ≤ 13 V with the HS link fitted. Run steps a, f and h.

**Pass** —
- **Healthy.** All steps pass. In step h, ASC_CMD_RB reads 0 at once, DRV_EN_RB reads 0 within 60 µs and FFLAG is
  set; after the clear, DRV_EN_RB and ASC_CMD_RB read 1, and finally ASC_CMD_RB reads 0. The pad lock reads back
  after step h (T-17), and there is no false FLT trip with FFILT = 0 (T-15).
- **Stuck terms.** Every stuck term is detected at its step (the table in contract §7): no arming and a DTC.
- **Skip.** FW-16 is skipped rather than run on assumption. The stored pass is used only if it comes from this or
  the previous key cycle; otherwise there is no arming.
- **Energy.** The residual energy is ≤ 26 mJ (8XX) or ≤ 64 mJ (4XX) when both channels read < 3 V. After the 2τ
  top-up (1.35 s 8XX, 1.64 s 4XX) the link is ≤ 9.3 V (≤ 38 mJ).
- **Latent short.** The fixture stays harmless at ≤ 0.1 J: module parameters and the short link are unchanged.

**Record** — step timing table, detection matrix, energies.

**On fail** — FW-16 conditions or sequencing (firmware), or the hardware read-back points.

### QP-SF-10 · Motor data, n_x and the §6 release rule; ASC-entry current vs the DESAT minimum (gates ⑥, ㉒ commissioning)

**Closes** —
- gate ⑥ ("motor data for the safe-state matrix (and whether the HV backup bias is needed)");
- gate ㉒, commissioning part ("ASC-entry current vs the DESAT minimum");
- *System A.11* "SPO freewheel energy at 340 A rms into the isolated link (8XX SiC …)" and "… 400 A rms (4XX IGBT
  …)" (WARN ×2);
- the motor part of S10;
- F123, F135, F148, R8X-13, A8-G02, A9 gate 8, F08 (A13);
- contract §6 rules (a)–(c), and FW-08b;
- target-bringup T-38 (the rule (b) evidence and `rule_b_released`);
- interface-requirements (motor — ψ_f, L_d / L_q / R_s, n_max, pole pairs, screening-motor coverage; and the
  safety-concept assumptions).

**DUT** — each motor to be released with each SKU, on F-DYNO, with FW, CAL (with the real motor ID), VAL and OTP.

**Fixture and instruments** — F-DYNO; 4-wire R_s; an isolated-link set-up (the battery emulator disconnected by a
series diode and contactor, with the external clamp at U_N); motor soak to its lowest temperature ("cold magnets").

**Steps**
1. Measure R_s. Measure L_d(i) and L_q(i) up to the SKU's I_pk with the inverter's standstill pulse method.
   Measure ψ_f from the open-circuit back-EMF at speed, cold and hot.
2. Compute E_LL,pk(n_max), and n_x where √3·ω_e·ψ_f equals 880 V (8XX) or 530 V (4XX).
3. At every speed up to n_max and the largest calibrated current, compute the rule (a) screen, valid only while
   E < V₀:
   V_pk ≤ E + √((V₀ − E)² + 1.5·(L_d·î_d² + L_q·î_q²)/C_min), with E = √3·ω_e·(ψ_f + |L_q − L_d|·Î/2),
   V₀ = 880 / 530 V and C_min = 291 / 723 µF. Where the screen fails, or E ≥ V₀, run the diode-bridge simulation.
4. Validate the screen on the dyno. Command SPO from 25 / 50 / 75 % of the rule-(a) current limit (plan steps)
   with the link isolated. Measure V_link,pk and extrapolate before any next step.
5. Where (a) fails, obtain the rule (b) evidence: the VCU/BMS keeps the contactors closed through inverter fault
   recovery at every such point, for every opening cause, with keep_hv asserted (FW-08b). Only then is
   `rule_b_released` written into the calibration record; it defaults to false (T-38).
6. At n_max with cold magnets, enter ASC 10 times (plan) and measure the peak phase current.
7. Measure the ASC braking torque at low speed, as input to the FS1B policy (FW-12).
8. Determine n_ss for FW-16 (E_LL,pk(n_ss) ≤ 12 V).

**Pass** —
- For each operating point, the release is proven by (a), by (b), or — for total KL30 loss only — by (c), the
  HV backup-bias option. Otherwise the motor is **not released** (contract §6).
- The measured SPO peaks are ≤ the screen's prediction at each step, and ≤ U_N (1000 / 600 V). Screening-motor
  references: 61 J → 1092 V (8XX) and 84 J → 716 V (4XX); rule (a) covers it only to 250 / 233 A rms at zero
  back-EMF.
- The ASC-entry peak current is below the low-side DESAT minimum — SiC 7.2 V at the switch ≈ 1.3 kA hot; IGBT the
  current where V_CE(sat) reaches the 4.2 V minimum on the HCG600 curve — with a margin ≥ 1.25× (plan: the
  FW-05 crest-to-trip factor). No FLT occurs in the 10 entries.
- The FS1B policy, n_ss and `rule_b_released` are written into the calibration record.

**Record** — motor dataset, n_x, the (a)/(b)/(c) decision per row, ASC currents.

**On fail** — the HV backup-bias option (motor-dependent); a lower calibrated current limit at speed; or the
motor is not released with this inverter.

### QP-SF-11 · HVIL, harness default-OFF and link-loss behaviour

**Closes** —
- FW-09 target "Harness signatures", and target-bringup T-38 (HVIL harness signatures);
- README interboard-links table ("If it fails" for L1–L5);
- *Sensing* "HVIL signatures (drive hi/lo/open)";
- *Sensing A.11* "Hall signal open wire (R⟨ph⟩B0 100 k)";
- *Safety* "Default-OFF discipline" (11 power + 4 card pull-downs);
- F100 (discharge header), F126;
- interface-requirements (harness — HVIL loop).

**DUT** — 8S complete inverter, with FW, CAL, VAL and OTP.

**Fixture and instruments** — break-out adapters that can open or short single lines on L1, L2, L4 and L5;
I-LVS on DRV_EN and the gate lines; I-CAN.

**Steps**
1. HVIL: drive high, drive low, and open.
2. Unplug or open each L1 line in RUN at zero torque (plan) and at standstill.
3. Unplug L2, and short QDIS_CMD to V15 on the header.
4. Open one hall signal wire.

**Pass** —
- HVIL signatures read 3.0 / 2.0 / 2.5 V (worst separation 0.38 V). An open loop ramps torque to zero within
  ≤ 100 ms, and the safe state follows §6.
- Every harness-floatable line defaults OFF: gates low, FS26 sees the loss.
- With L2 unplugged, QDIS stays off. The header pin-order short (V15-GND-CMD-GND) cannot put 15 V on QDIS_CMD.
- An open hall wire falls 2.5 → 0.2 V in ≤ 0.83 ms. It reads outside the 0.2–4.8 V window, the channel goes
  invalid, and modulation stops.

**Record** — per-line table.

**On fail** — the pull-down or layout.

---

## 10. Firmware HIL and dyno

### QP-FW-01 · PWM-fault route with the CPU halted; route binding and configuration read-back

**Closes** —
- README target evidence ("PWM-fault injection on PTC26/PTC25 with the CPU halted");
- gate ⑯ (closed in A.12; the physical route remains FW-24 evidence);
- F01, F150 (A13);
- target-bringup T-01, T-02 (IMCR values and SSS width), T-05 (fault-route part) and T-15 (register-image fields);
- the arming-evidence items ROUTE_BOUND, CONFIG_MATCHES and FAULT_ROUTE_VALIDATED;
- vendor-requests.md (NXP — reference-manual IMCR/SSS values and the BCTU list layout).

**DUT** — card, target build of FW with the RM-derived IMCR/SSS values filled in (a target build stops with
`#error` until they are); F-HIL.

**Fixture and instruments** — I-DBG (halt); drive of FLT_HS_N (PTC26) and FLT_LS_N (PTC25) at the harness;
I-LVS on the six PWM pins.

**Steps**
1. Boot and read back the IMCR binding and the fault lock-down image, including CTRL2.INDEP.
2. Start modulation (HIL load).
3. Halt the core. Drive PTC26 low, release it, and check the outputs and FFLAG. Repeat with PTC25.
4. Repeat with the core running.

**Pass** —
- The IMCRs read back non-zero, the two IMCR indices differ, and the SSS values match under the confirmed field
  width (T-02).
- The lock-down image matches.
- With the core halted, driving either FLT pad low forces all six PWM outputs low within ≤ 1.0 µs. That bound is
  the FW-06 action allocation, which includes the fault path.
- The outputs stay low until manual clear, and FFLAG is set.

**Record** — timing and read-back data. These results feed FAULT_ROUTE_VALIDATED (QP-EOL-05).

**On fail** — the IMCR values. If the mux cannot reach FAULT0/2 from these pads, a card pin swap (T-01, FW-15),
not another firmware route.

### QP-FW-02 · FW-06 over-voltage chain ≤ 15.6 µs, and event to ASC

**Closes** —
- README target evidence ("the ADC-watchdog → PWM-fault chain ≤ 15.6 µs");
- gate ⑫, HIL part;
- *Regeneration, battery path lost — budget* "FW-06 latency to the ASC request" (the route "measured on HIL");
- RR06, A6-R08, N9, F06 (A13), F165;
- A7 gate 6 and A8 gate 7 ("event → safe current, not just ASC_REQ");
- contract §11 ("the HIL measurement of the FW-06 chain");
- target-bringup T-05 (OVP part), T-08, T-09, T-10, T-11; timing.md FW-06 table;
- OVP_ROUTE_VALIDATED; interface-requirements (EOL/calibration — the EOL/HIL arming-evidence record).

**DUT** — **(a)** analog segment (DV): three power boards (plan), 8S, No FW, measured at the receiver output.
**(b)** card segment: every card, with FW, CAL and OTP, on F-HIL — the EOL form is QP-EOL-05.

**Fixture and instruments** — (a) I-HVS stepped or ramped at 0.89 V/µs (the 220 kW regen rate) through 880 V,
I-HVD reference, I-LVS at the receiver output. (b) The HIL drives the receiver inputs with a ramp crossing the
calibrated OV code; I-LVS on ASC_REQ (U4 / PTD7) and the PWM pins; I-DBG trace.

**Steps**
1. (a) Measure the divider + AMC1311B + receiver lag: the step-response τ plus the delays.
2. (b) Per channel, over 20 randomised sampling phases (plan), with the slow list running (ADC1's three injected
   conversions active):
   - measure receiver crossing → ASC_REQ edge;
   - check the trip code (±1 LSB, T-09) and the reported channel (T-10);
   - measure the V_DC ch2 sample gap (T-11);
   - capture the ASC_REQ re-edge.
3. Event → safe current: on F-NOHV with the power board, ASC_REQ → all three low sides on (the QP-SF-01 method).

**Pass** —
- (a) The analog segment is ≤ 8.6 µs (divider 6.2 + AMC1311B 2.1 + receiver 0.3).
- (b) The card segment is ≤ 7.0 µs (derived: sample wait 5.0 + conversion 1.0 + action 1.0). The trip lands at the
  programmed code ± 1 LSB on each channel, and the OV row and DTC are raised. The sample gap is ≤ 5.0 µs (4 µs
  allocated: 1 + 3 conversions). The total, (a) max + (b) max, is ≤ 15.6 µs.
- ASC_REQ → the low sides fully on takes ≥ 3.4 µs and ≤ 7.56 µs. Crossing → safe current is therefore ≤ 23.2 µs
  (derived: 15.6 + 7.56), which underlies the 906 V (8XX) / 542 V (4XX) end-points.

**Record** — distributions per channel. `ovp_chain_ns` = the (a) max + the (b) max, written by QP-EOL-05.

**On fail** — "A separate comparator is added only if this route misses the budget on HIL" (contract FW-06). A
missed sample gap is solved by moving MT2_SIG off ADC1 (T-11).

### QP-FW-03 · REG_PROT lock witness and re-lock after a reset

**Closes** — README target evidence ("REG_PROT lock readback"); F02, F151 (A13); target-bringup T-03 (the REG_PROT
layout, coverage of eFlexPWM_1, XRDC fallback) and T-04 (the IMCR protection and CTRL2 byte lock);
PROTECTION_LOCKED.

**DUT** — card, target FW with the REG_PROT offsets filled from the S32K39 RM; F-HIL.

**Fixture and instruments** — I-DBG; a test image hook that attempts writes from the CPU and from a DMA channel
(plan: a debug build of the same sources, never shipped).

**Steps**
1. After init, read the SLBR and GCR.HLB lock bits.
2. Attempt writes to the locked eFlexPWM fault registers and the IMCRs, from the CPU and from DMA.
3. Force a watchdog reset and check the re-lock before re-arming.

**Pass** —
- `hal_pwm_protection_locked()` is true only when every lock bit reads set.
- No write lands: each read-back is unchanged and a bus error is flagged. This includes the IMCRs and, if byte
  locking exists, CTRL2's upper byte (T-04).
- After a reset, the unit re-locks before any arming. A lock lost while armed goes through the §6 "control lost"
  row.

**Record** — register dumps.

**On fail** — XRDC as the fallback; `hal_pwm_protection_locked()` then reads the XRDC state (T-03).

### QP-FW-04 · WCET and ISR latency on the target

**Closes** — README target evidence ("WCET"); F20 (A13, real-time timing); target-bringup T-36; timing.md (the
"WCET measured" columns).

**DUT** — card, FW, CAL, VAL and OTP, on F-HIL at the worst operating point from timing.md: RUN at the
field-weakening limit, CAN at full load, an NVM job in flight. UDS traffic on bus 1 is added at its maximum of 4
requests per tick (FW-32).

**Fixture and instruments** — I-DBG with SWO/ETM trace, or GPIO toggles with a scope.

**Steps** — capture ≥ 10⁶ ISR entries per context (plan); inject the fault ISR during the longest PRIMASK section
(the `nv_queue` copy).

**Pass** (timing.md budgets) —
- fault ISR ≤ 10 µs, and FW-06 action ≤ 1.0 µs to the ASC_REQ edge, including the worst PRIMASK section;
- each SDADC interrupt ≤ 1 µs;
- the current loop < 0.5·T_sw minus the conversion time (≈ 23 µs at 20 kHz);
- the 1 ms task ≤ 400 µs;
- FlexCAN RX ≤ 2 µs.

**Record** — WCET table, filling timing.md.

**On fail** — code or priority changes; if the FW-06 action misses, see QP-FW-02.

### QP-FW-05 · Acquisition on silicon: ADC chains, phase triplets, SDADC frames, resolver age, SWG law

**Closes** —
- target-bringup T-12 (BCTU list read-back), T-13, T-14, T-22, T-28 (SDADC eDMA), T-29, T-31;
- FW-27…FW-31 (FW-31 current-loop liveness, named in round 17);
- traceability FW-05 (VALID-bit behaviour on silicon) and FW-10;
- F162, F165, F171–F173; firmware README items 28 and 33–35.

**DUT** — card, FW, CAL, VAL and OTP, on F-HIL (resolver synthesised from the real excitation).

**Fixture and instruments** — I-DBG (to freeze a DMA channel, stop the BCTU or force a FIFO overrun); the HIL
resolver with a programmable delay; I-LVS on the SWG pin.

**Steps**
1. Boot. Check that the chain read-back matches the ball map, then corrupt one chain mask via the debugger and
   reboot.
2. Read the BCTU list back against the PHASE rows (T-12).
3. Stop the BCTU, then withhold one phase channel's VALID bit.
4. Freeze each SDADC DMA channel in turn, delay one, force a FIFO overrun, and hold off the completion interrupts.
   Stop resolver delivery at 0, 1000 and 10 000 rpm, including across the µs wrap.
5. Measure the resolver chain latency (HIL angle vs the firmware angle).
6. Measure the SWG amplitude per IOAMPL code (0, 8, 12, 15) and the ramp timing.

**Pass** —
- A chain mismatch refuses the configuration. A BCTU list that does not convert ADC3_P1, ADC4_P5 and ADC0_S19 is
  refused (T-12).
- **FW-31.** A stopped BCTU is caught by the 1 ms task (last ISR older than `cal_isns_stale_us` 200 µs): currents
  lost, the §6 "control lost" row. The current-loop ISR otherwise runs at exactly 2·f_sw (T-22).
- **FW-27.** A missing channel writes nothing. No partial triplet is consumed, and the last good stamp is kept.
- **FW-29.** A frozen, late or overwritten SDADC channel, or a FIFO overrun, yields no frame.
- **FW-28.** The angle is withdrawn within 500 µs of the newest frame (`cal_rslv_hold_us`), at most one tick
  late: ≤ 550 µs at 20 kHz and ≤ 600 µs at 10 kHz. The resolver-invalid §6 row follows, and re-acquisition starts
  from scratch.
- The measured latency is written into `cal_rslv_latency_us` (CAL 0–200 µs).
- **SWG law.** `cal_swg_code_init` = 8 gives ≈ 1.45 V pp at the maximum corner, and the untrimmed maximum does not
  slew-limit. The ramp reaches the setpoint in 20 / 25 / 35 ms.

**Record** — per-case logs.

**On fail** — firmware; RTD configuration.

### QP-FW-06 · FW-05 overcurrent compare route and threshold

**Closes** —
- FW-05 target ("ADC watchdog → TRGMUX/LCU → FAULT1 route and its latency; activity CALs");
- target-bringup T-08, T-09 and T-10 (the phase-current part);
- firmware README items 2 and 7 (the route and the safe state after the trip);
- FW-05 addendum (F24, F156).

**DUT** — card, FW, CAL and OTP (8XX and 4XX parameter sets), on F-HIL.

**Fixture and instruments** — the HIL current outputs at the hall-equivalent voltage, using the channel's
calibrated gain and offset; I-LVS on PWM.

**Steps**
1. Ramp each phase to the crest-equivalent of 481 A, then to 620 A (8XX). Repeat both polarities.
2. For 4XX, ramp to 566 A and then to 729 A (derived: 707 A × 620/601).
3. Stick one channel at zero while |i*| ≥ 20 A.
4. Stick all three channels.

**Pass** —
- 481 A (8XX) or 566 A (4XX) crest does not trip. The trip occurs at ±601 A (8XX) or ±707 A (4XX), at the
  programmed code ± 1 LSB, within ≤ 2 PWM periods, and only the high sides are forced off.
- The event is latched as "control lost", with the low sides per §6, and the overcurrent row is the one raised
  (T-10).
- A stuck channel is caught by the activity check within `cal_isns_act_debounce` 20 samples: the reference reaches
  20 A and the reading is < 0.2 of it.
- |Σi| > 45 A over 3 samples is caught.

**Record** — trip table.

**On fail** — the TRGMUX/LCU route; the CAL values.

### QP-FW-07 · §6 matrix, battery-path loss and keep_hv on HIL

**Closes** —
- contract §6 (every row × column);
- FW-08 / FW-08b (vehicle integration), including the round-17 decision: zero current under the battery-lost
  row, and the RUN-only DC-link trim;
- F148, F164; firmware README items 8, 15, 24, 25 and 26;
- target-bringup T-38;
- interface-requirements (HV side — contactor/precharge feedback contract; safety-concept assumptions).

**DUT** — card, FW, CAL (screening motor, then the real motor from QP-SF-10), VAL and OTP, on F-HIL.

**Steps** —
1. For each §6 row, inject the fault at n = 0, at n < n_x, at n ≥ n_x and at unknown speed, while motoring and
   regenerating at 340 A rms (8XX) or 400 A rms (4XX).
2. Inject the battery-path loss while armed at zero, low, high and unknown speed, with the contactor reported
   OPEN, INVALID or stale.
3. In normal RUN with the battery present, drive V_DC above the range maximum while regenerating.

**Pass** —
- The action matches the §6 cell: SPO, LS-ASC or current-controlled ramp.
- Under the battery-lost row below n_x, id = iq = 0 at the current-loop rate and INV_STATUS reports the 0 Nm
  applied (round 17).
- Ordinary torque permission is withdrawn in the same invocation.
- keep_hv is set exactly when an SPO relies on rule (b), at any speed, and is held until rule (a) holds or ASC is
  active.
- With the battery absent, the status reads "no safe state proven" (b14.0) together with DTC_SPO_ENERGY.
- A zero-torque opening at standstill with no winding current is a normal disarm, not a FAULT.
- **Step 3.** The DC-link trim takes regeneration back only while V_DC is above `vdc_max_v` (850 V 8XX), by at
  most `cal_dcl_tmax_nm` (50 Nm). It never adds motoring torque, and it is reset outside RUN.

**Record** — the matrix result table (the traceability scenario names as the reference).

**On fail** — firmware.

### QP-FW-08 · Dyno: contactor opening under full regeneration, and current-loop margins (gate ⑫, dyno part)

**Closes** —
- gate ⑫ ("HIL then dyno: contactor opening under full regen");
- *Regeneration, battery path lost — 8XX bus* and *— 4XX bus* "Link peak with the FW-06 response" (the "dyno
  contactor opening under full regen" gate);
- A7 gate 6;
- FW-03 target (dyno); FW-08 and T-37 (DC-link trim gains on the real bank, `cal_vdyn_reserve_frac`);
- S6 (the three rows; "motor constants assumed") and contract §2 (the ceiling "re-derived from the measured delay
  and the real L_d/L_q").

**DUT** — 8S, 8I and 4I on F-DYNO, with FW, CAL (the real motor), VAL and OTP; QP-FW-02 and QP-SF-10 passed.

**Fixture and instruments** — the battery emulator with an uncoordinated contactor opening (no "zero torque
before opening"); the external clamp at U_N; I-HVD on the link; I-LVS on ASC_REQ and the low-side V_GS.

**Steps**
1. Open the contactor at regen of 25, 50, 75 and 100 % of 220 kW (8XX) or 150 kW (4XX), at n ≥ n_x (the ASC
   path) and at n < n_x (the zero-current path).
2. Extrapolate the link peak before each next step, and stop if it would exceed the pass value.
3. Measure the sample-to-actuation delay (BCTU trigger → PWM update).
4. Run current steps from 10 to 50 % of the rating at each f_sw.

**Pass** —
- V_link,pk ≤ 906 V (8XX) or ≤ 542 V (4XX). Above that, up to U_N (1000 / 600 V), is CONDITIONAL: the model and
  budget are re-run. Above U_N is FAIL, and the clamp protects the rig.
- The response follows §6: LS-ASC above n_x; zero current, then SPO, below it.
- The measured delay plus the real L_d/L_q gives a phase margin ≥ 45° at a crossover ≤ 1.2 kHz (10 kHz),
  ≤ 1.1 kHz (8 kHz) or ≤ 0.7 kHz (IGBT, 5 kHz). The step overshoot is consistent with that margin.
- No voltage-infeasible request reaches the PWM (the FW-25 witness, with `cal_vdyn_reserve_frac` 5 %).

**Record** — peak table, loop data.

**On fail** — FW-06 route or comparator (QP-FW-02); per-SKU current-loop gains.

### QP-FW-09 · Long run across the time-base wrap; vehicle CAN interface

**Closes** —
- F147 / FW-23 on the target;
- FW-11 target ("Vehicle DBC / DataID agreement") and the contract's vehicle-interface paragraph (status bytes
  14–15 to the DBC);
- target-bringup T-23 (µs base), T-25 (NVM, SPI, CAN on silicon), T-26 (the three CAN IDs) and T-38 (DBC and
  DataIDs);
- the FW-15 retry gate across the wrap.

**DUT** — card, FW, CAL, VAL and OTP, on F-HIL, running for 4 h (plan) — more than three 71.6 min wraps.

**Fixture and instruments** — I-CAN with the vehicle DBC. The DBC is an OEM input: interface-requirements lists
the contactor-feedback content, not the frame layout (§14).

**Steps**
1. Run continuously with CAN at full load across the wraps: VCU 0x101/0x102 on bus 0, UDS 0x7E1 on bus 1.
2. Inject frozen alive counters, bad CRCs, a 20 ms stale command and a BMS timeout.
3. Straddle a wrap with a DESAT retry.
4. Power-cycle with every NVM record type present.

**Pass** —
- No false staleness or DTC across the wraps, and the DTC time stamps stay monotonic. The 1 ms task period and
  the µs base are correct on a GPIO (T-23).
- A command stale for 20 ms ramps torque to zero; while armed, the stale report is also the battery-path row
  (firmware README item 24).
- A BMS timeout (100 ms) zeroes regeneration.
- The CRC-8 (0x1D) and the DataIDs match the DBC.
- INV_STATUS b14/b15 decode per the DBC.
- The retry is not allowed before 1 s across the wrap.
- Every NVM record survives a power cycle, and the FS26 SPI frames are CRC-clean (T-25).

**Record** — run log.

**On fail** — firmware or DBC.

---

## 11. First article and EOL production

### QP-FAI-01 · Design-data release check (KiCad sets, ball map)

**Closes** —
- the A14 "What stays open" item: an export from a running KiCad instance. Round 17 (G-08, F181) closed it with
  the KiCad 9/10 set proved by `kicad-cli`; this QP repeats the proof at every release;
- F183 (the harness pin numbering in the legacy sets) and F184 (the SMDJ/PMEG symbol class);
- gate ① residual.

**DUT** — the three shipped sets: `kicad5/traction` (EasyEDA import), `kicad5/traction-native` (KiCad 5–8) and
`kicad/traction` (KiCad 9/10, `Traction-Inverter-KiCad-modern.zip`).

**Steps**
1. Run `npm run sheets`. This runs `kicad-sch-verify.mjs` with the installed KiCad CLI and its mutation tests.
2. Open the native set in KiCad 5–8 and the modern set in KiCad 9/10.
3. Check that every MCU pin number equals its ball ID in `docs/mcu-pin-manifest.md`.
4. Check that JIC/JICC pins are numbered 1–40.

**Pass** —
- The KiCad netlist equals the built netlist on every board: 324 / 26 / 35 / 299 components and 194 / 2 / 25 / 201
  nets for power / capbank / discharge / card.
- The mutation tests are detected (mirrored MCU, ball swap, legacy harness numbering, duplicate reference).
- ERC shows no failing types.
- H5 is on V15S and J7 on V25, and the harness pins read 1–40.
- Annotate is not run on the modern set (its references do not end in digits).

**Record** — the verifier output.

**On fail** — the generators; no fabrication until the check passes.

### QP-FAI-02 · First-card bring-up: supply balls, pin map on silicon, clocks, ADC triples

**Closes** — gate ① residual (F143/F144 on real silicon); F162, F165, F166 on the target; target-bringup T-13,
T-14, T-18, T-19, T-20, T-21, T-23, T-24, T-25 (the bring-up acceptance items).

**DUT** — the first three cards (plan), HW A.15+.

**Fixture and instruments** — current-limited supplies; flying probe; a board-test image that toggles and reads
each MCU signal net (plan: a test build of the same sources); precision voltage injection on every ADC input;
scope on PWM and GPIO.

**Steps**
1. Power up with current limits. Measure H5 / V15S, V11, J7 / V25 and the VREF rails. Watch the GPIO outputs
   through power-up.
2. Toggle and read every signal net at its far-end test point.
3. Inject a known voltage on each ADC input and read it through the instance/subtype/channel triple of the
   generated map.
4. Measure the clocks, f_sw, the dead time and the trigger position.

**Pass** —
- V15S is in 1.425–1.65 V, V11 ≈ 1.14 V (≤ 1.26 V abs), V25 is present with CV25, and nothing is at a 5 V level on
  a core ball.
- No output glitches high at power-up, and `ASC_CLR_N` is never low (T-18).
- Every signal net toggles at the ball the manifest names.
- Every ADC input reads its injected value on the right triple (NTC_A on ADC5_S11, HW_ID on ADC3_P0, MT2_SIG,
  INTRLOK_N and TMOD_W on ADC1). HW_ID, VOFS and V5GD read their nominal values (T-14).
- **PWM (T-19, T-20).** f_sw and the dead time equal the SKU parameters (10 kHz / 1.0 µs SiC, 5 kHz / 2.5 µs
  IGBT). The PWM is complementary and centred, with the trigger at both zero-vector centres.
- **Clocks and tasks (T-21, T-23).** The clocks are measured, and the 1 ms task and µs base are correct.
- **SWT (T-24).** A stopped task resets the MCU after 50 ms.

**Record** — the bring-up checklist.

**On fail** — stop; the ball map (`mcu-ballmap.json`), the RTD configuration, or the layout.

### QP-FAI-03 · Harness numbering and connector orientation

**Closes** — A9 gate 7b (R9X-02); dfm §4 ("confirm the header's pin-1 corner and the odd/even row numbering
against the Samtec print"); F115, F100, F182; interface-requirements (harness — 40-way card harness pinout);
vendor-requests.md (Samtec — pin-1 corner and row numbering).

**DUT** — the first-article JIC/JICC headers, the 40-way cable, and JDIS/JCTL/JHVIL.

**Steps** — continuity-map the cable and headers against the HARNESS40 map (the 80 assertions of `erc-audit.mjs`),
using the confirmed numbering: sequential per row, row A 1–20 and row B 21–40 (F182). Check the supply-pin
neighbours.

**Pass** —
- The map matches pin by pin: V5GD on pin 1, HW_ID on pin 2, VBAT_H on 19/20, VBAT_L on 39/40.
- VBAT and V5GD sit next to ground or their own rail only.
- The discharge header reads V15-GND-CMD-GND on both boards.

**Record** — the continuity table.

**On fail** — correct the cable drawing or silkscreen before any power-up.

### QP-EOL-01 · Identity: HW_ID, image, parameter set, FS26 variant (per unit)

**Closes** — FW-01 / FW-02 and target-bringup T-37 ("the HW_ID divider on real cards"); T-06 (the released
`TI_FW_ID`); dfm §2 ("EOL reads HW_ID and rejects a unit whose parameter set disagrees"); F166.

**DUT** — every inverter at box EOL.

**Steps**
1. Read V_ID from a started slow conversion.
2. Read `TI_FW_ID`, the SKU of the parameter set, and M_PROGID.

**Pass** —
- V_ID lies within ±4 % of its nominal: 0.90 V (4I), 1.60 V (8I), 2.50 V (8S), 3.44 V (4S). Open (> 4.6 V) and
  short (< 0.2 V) are rejected.
- The SKU matches the kit record, `TI_FW_ID` is 0x0A0F0011, and M_PROGID equals `cal_fs26_prog_id`.

**Record** — V_ID per unit. The distribution against the window edges is reviewed per lot.

**On fail** — reject the unit.

### QP-EOL-02 · FS26 OTP per-field check (gate ⑦)

**Closes** — gate ⑦ ("FS26 OTP readback"); target-bringup T-34; firmware README item 6 ("the per-field OTP
comparison is EOL"); dfm §5 (the OTP PO gate).

**DUT** — every card at card EOL, in FS26 debug/OTP mode (DEBUG strap).

**Steps** — read the OTP bank and the INIT registers and compare each against design-basis §8a.

**Pass** — every field matches:

| Field | Required |
|---|---|
| VPRE_V | 6.0 V |
| FPRE | 450 kHz |
| CORE_LSEL_OTP[1:0] | 10 (2.2 µH) |
| VCORE | 1.5 V |
| LDO1 / LDO2 | 3.3 V / 5.0 V, slotted ON |
| VMONEXT | enabled |
| VMONCORE / VMONPRE | enabled |
| TRK1 / TRK2 | OFF (111) |
| WK2PD_OTP / IO2PD_OTP | 1 / 1 |
| VBST | disabled |
| FS0B, FS1B, FCCU1/2 | enabled |
| FS1B_FS0B_EN_OTP | 0 |
| FS1B_TDELAY | 00000 |
| FS1B_TDUR | 100 ms |
| BACKUP_SAFETY_PATH_FS0B | 1 |
| BACKUP_SAFETY_PATH_FS1B | 0 |
| GPIO1STAGE_OTP | push-pull (or high-side driver), GPIO1 **not slotted** |

`cal_fs26_prog_id` is set to the procured variant's M_PROGID.

**Record** — the field dump per card.

**On fail** — reject the card. The OTP variant is fixed at procurement (vendor-requests.md, NXP — FS26 behaviour
for the exact ordered OTP variant).

### QP-EOL-03 · Cap-bank / discharge kit LCR against the kit record (per unit)

**Closes** — dfm §4 ("The cap-bank / discharge kit's voltage class is proven at EOL by measurement"); FW-02 note
("the EOL capacitance/resistance measurement"); R1-F24, R2-F28; interface-requirements (EOL/calibration —
cap-bank / discharge-kit LCR traceability).

**DUT** — every cap-bank and discharge kit, before the first HV energisation.

**Steps** — measure C of the fitted bank and R of the active string and of the bleeder with I-LCR. Compare with the
serial-linked kit record.

**Pass** —
- Bank: 320 µF ± 10 % (8XX) or 800 µF ± 10 % (4XX).
- Active string: 1.88 kΩ (8XX) or 880 Ω (4XX), ± 5 %.
- Bleeder: 66 kΩ (8XX) or 45 kΩ (4XX), ± 5 %.
- The kit's voltage class matches the unit's SKU.

**Record** — kit record with serials.

**On fail** — reject the kit.

### QP-EOL-04 · LV functional and rails (per unit)

**Closes** —
- dfm §3 "LV functional";
- the standing values of *LV* (V15 boost setpoint, polyfuse hold), *LV A.4* (IGN_SNS), *Sensing* (HVIL signatures,
  VOFS monitored);
- FW-07 windows;
- F17 / F158 (CAN termination population);
- interface-requirements (harness — CAN termination as a vehicle-position population; the HVIL loop).

**DUT** — every inverter at box EOL, KL30 at 9 / 13.5 / 16 V.

**Steps** — measure at test points and through FW read-back.

**Pass** —
- **Rails and windows.**
  - V15B 15.41 V ± the setpoint tolerance, V15 = 15.0 V (inside the 13.5–16.5 V band);
  - V5GD 4.75–5.25 V; VOFS 0.475–0.525 V;
  - VEXD ≈ 12.07 V (≤ 16 V);
  - UCC14141-Q1 outputs 17.4–18.6 V;
  - all six VCC2 in 13.5–17.0 V at 13.5 V KL30, with VEE −4.8 … −5.4 V.
- **Inputs.** IGN_SNS reads 1.46–2.68 V over 9–16 V. HVIL reads 3.0 / 2.0 / 2.5 V.
- **FAULT_OUT.** It asserts ≤ 1.2 V with the 10 k / 5 V load.
- **Parking.** Current ≤ 0.1 mA at 25 °C.
- **CAN termination.** Fitted as the vehicle position requires.
- No FS26 diagnostic flags.

**Record** — values per unit.

**On fail** — reject and route to repair.

### QP-EOL-05 · Arming evidence: fault-route injection and OVP chain → the validation record (per card)

**Closes** —
- README target evidence;
- FW-24 (FAULT_ROUTE_VALIDATED, OVP_ROUTE_VALIDATED);
- target-bringup T-05, T-06 and T-27 (the device UID as the serial);
- firmware README item 22;
- the round-17 image `TI_FW_ID` 0x0A0F0011 ("needs a new EOL/HIL validation record before it arms");
- F166; interface-requirements (EOL/calibration — the EOL/HIL arming-evidence record, the OEM's rig).

**DUT** — every card at card EOL, FW and OTP. Any later image change repeats this QP.

**Fixture and instruments** — the card-EOL station (F-HIL functions and I-DBG).

**Steps**
1. Read the device UID (T-27).
2. Run the QP-FW-01 steps: PTC26 and PTC25 injected with the core halted.
3. Run the QP-FW-02 (b) steps: the card segment on both channels.
4. Write `arm_validation_t`:
   - magic "EVID", layout 1;
   - the SKU;
   - flags FAULT_ROUTE_VALIDATED | OVP_ROUTE_VALIDATED;
   - hw_serial = the device UID;
   - fw_id = 0x0A0F0011;
   - ovp_chain_ns = the card segment measured + the QP-FW-02 (a) DV maximum;
   - CRC-32.
5. Reboot, and read INV_STATUS byte 15.

**Pass** —
- The UID is unique per device, stable across resets, and equal to the station's reading.
- Route: PWM low ≤ 1.0 µs with the core halted, on both pads.
- ovp_chain_ns ≤ 15 600.
- After the reboot, byte 15 reports no missing evidence. A record for another image or card, or a corrupted one,
  is refused (spot-check 1 in 100, plan).

**Record** — the record image and its CRC per card.

**On fail** — reject the card.

### QP-EOL-06 · Phase-current calibration and the hall-reference check (per unit)

**Closes** —
- FW-20 (current offset/gain/sign; T-07);
- *Sensing* "Hall ratiometric ref vs ADC ref", the "EOL gain calibration" its round-17 PASS rests on (G-07,
  F180);
- the FW-05 addendum and the traceability coverage table (an equal gain error is an EOL item);
- interface-requirements (EOL/calibration — hall gain vs VREF5; the FW-20 calibration record).

**DUT** — every inverter at box EOL on F-B2B (or the card with a multi-turn loop through each LEM).

**Steps**
1. Apply a reference current: ±200 A and ±400 A equivalent, measured by a DC-accurate reference transducer.
2. Compute offset, gain and sign per phase.
3. Verify the compare threshold: inject the hall-equivalent voltage of 481 A and of 620 A (8XX) — or 566 A and
   729 A (4XX), derived as in QP-FW-06 — with each channel's own calibrated gain.

**Pass** —
- Offset 2.2–2.8 V and gain 1.9–2.6 mV/A per phase (the FW-20 ranges), sign ±1. The datasheet nominal is 2.5 V
  and 2.22 mV/A.
- The three gains agree with the reference within ±1 % (plan; the equal-gain error is only observable here).
- 481 A (566 A) does not trip, and 620 A (729 A) trips.
- V5A / VREF5 is recorded.

**Record** — goes into CAL.

**On fail** — sensor or AFE repair.

### QP-EOL-07 · V_DC calibration and the OV compare (per unit)

**Closes** — FW-07 target ("Receiver offsets and gains at EOL"); *Sensing* "VDC chain error after EOL gain/offset
calibration" (≈ ±0.3 %) and "VDC chain error, WORST CASE uncalibrated" (±2.08 %); T-07.

**DUT** — every inverter at box EOL, I-HVS with the reference meter.

**Steps**
1. Apply 100, 500 and 850 V (8XX), or 100, 300 and 500 V (4XX).
2. Compute gain and offset for both channels.
3. Ramp to the OV trip: 880 V or 530 V.

**Pass** —
- Before calibration, the error is ≤ ±2.08 %. Gains fall within ±15 % of the divider ratio 455.84 and offsets
  within ±20 V (the FW-20 ranges).
- After calibration, the residual is ≤ ±0.3 % of reading at ≥ 250 V, and |VDC1 − VDC2| < 5 % above the 18 V floor.
- Both channels' OV compares fire at 880 V (530 V) within ±0.3 % (derived: ±2.6 V at 880 V).

**Record** — goes into CAL.

**On fail** — divider or receiver repair.

### QP-EOL-08 · Resolver-chain calibration (per unit)

**Closes** —
- FW-20 layout 2 (the resolver record with the monitor gain `exc_code_per_vpp`); target-bringup T-07 and T-31;
- FW-10 target ("EOL phase trim, EOL monitor gain …");
- *Sensing A.11* "Resolver wire shorted to KL30" note ("worst independent corners 1.29° electrical, an EOL
  calibration item (FW-20)");
- *Sensing A.13* "Excitation-monitor anti-alias filter" (the 3° fixed offset "absorbed by the FW-20 phase
  calibration");
- interface-requirements (EOL/calibration — resolver monitor-to-winding ratio; the FW-20 record; motor —
  resolver/exciter harness capacitance).

**DUT** — every card, on the EOL resolver emulator (known angle, amplitude and phase) with a harness replica.

**Steps**
1. Measure the monitor-plane amplitude with a reference instrument against the firmware codes.
2. Sweep the angle and measure SIN/COS gain, offset and phase against the emulator.
3. Measure the monitor-to-signal phase offset.
4. Run the SWG trim to the setpoint.
5. With the harness replica, measure winding/monitor at the emulator terminals.

**Pass** —
- `exc_code_per_vpp` lies in 1500–3500 codes per V pp.
- SIN/COS gains 0.8–1.25, offsets ±0.2, phase trim within ±30°, ratio_nom 0.2–2.0 (the FW-20 ranges).
- The SIN/COS mismatch before calibration is ≤ 1.3°. The monitor-to-signal offset is ≈ 3.0°; a deviation larger
  than the 1.3° matching bound means the CEXM / C_AAF parts are investigated.
- The trim reaches 7.2 V pp ± 5 % with no DTC_RSLV_SWG_SAT.
- Winding/monitor r ≥ 0.903 (derived: the 6.5 V pp floor ÷ the 7.2 V pp setpoint — the FW-30 load check). With
  r ≥ 0.950 there is no nuisance trip anywhere in the ±5 % trim band (derived: 6.5 / (0.95 × 7.2); T-07
  acceptance: "at the trim band's low edge … no nuisance trip"). Between the two values is CONDITIONAL: narrow
  the band or raise the setpoint within the headroom checks.

**Record** — goes into CAL (layout 2).

**On fail** — card repair; a CONDITIONAL result goes to QP-RX-01.

### QP-EOL-09 · Safety-chain functional (per unit)

**Closes** — contract §7 FW-16 "Field coverage" ("The EOL rig still drives the FLT lines from the power-board side,
which step h cannot reach: the harness and the drivers' own FLT outputs"); gate ⑲ and gate ㉑ (the production
subset).

**DUT** — every inverter at box EOL, no HV, with FW, CAL, VAL and OTP.

**Steps**
1. Run FW-16 (all steps must pass).
2. Force each driver's own FLT output low from the power-board side (six channels), and check the latch, DRV_EN and
   the UASCG mask.
3. Force each RDY low.
4. Run one DESAT injection per bank (the F-NOHV link, fitted on the EOL adapter).
5. Run the QP-SF-01 path (a) entry once.

**Pass** —
- FW-16 passes.
- Each forced FLT sets the latch, drops DRV_EN 22–53 µs later, and masks ASC ≤ 11 ns after it.
- RDY low drops DRV_EN.
- The ASC entry is 3.4–7.56 µs, break-before-make.

**Record** — per unit.

**On fail** — reject.

### QP-EOL-10 · Hi-pot and insulation resistance (per unit)

**Closes** — dfm §3 EOL "hi-pot"; cost-rollup item 9.

**DUT** — every inverter at box EOL; grouping as QP-HV-04.

**Steps** — apply the production level and duration (an open OEM input, §14); it must not exceed the QP-HV-04 DV
level. Then measure IR.

**Pass** — no breakdown; leakage and IR within the OEM production limits.

**Record** — per unit.

**On fail** — reject.

### QP-EOL-11 · HV functional: precharge, witnesses, discharge time (per unit)

**Closes** — dfm §3 "discharge-time check"; FW-19 plausibility; FW-18 witness; FW-02 τ.

**DUT** — every inverter at box EOL, with FW, CAL, VAL and OTP. The EOL HV source precharges through a resistor
that gives τ ≥ `cal_precharge_tau_min_s`.

**Steps**
1. Precharge to 700 V (8XX) or 400 V (4XX).
2. Compare both channels with the reference.
3. Report the contactors open and discharge.

**Pass** —
- The FW-19 plausibility passes: no plateau below 97.5 % of the source, no short τ.
- Both channels agree with the reference within the EOL-07 residual.
- 700 → 60 V is reached within the time for the measured R·C, derived with the S5 method. The 850 V figure is
  1.57 s nominal.
- τ is within ±20 % of 0.60 s (8XX) or 0.71 s (4XX).

**Record** — per unit.

**On fail** — reject.

### QP-EOL-12 · Back-to-back spin and power test (per unit)

**Closes** — dfm §3 "spin test on back-to-back rig"; cost-rollup item 9.

**DUT** — every inverter on F-B2B, with FW, CAL, VAL and OTP.

**Steps** — run 10 min at the continuous point, then one 10 s step at peak current (plan), at each SKU's f_sw.

**Pass** —
- No DTC, FLT or FW-05 / FW-06 trip.
- |Σi| < 45 A throughout.
- The resolver stays valid.
- Module NTC rises are consistent with the QP-TH-01 Rth: ≤ the steady-state prediction for the rig coolant (plan
  tolerance ±5 K).

**Record** — per unit.

**On fail** — reject.

### QP-EOL-13 · Record sealing and final read-back (per unit)

**Closes** — FW-20 target ("Fee configuration, device UID read"); target-bringup T-07; the round-17 identity
("the calibration record stays layout 2").

**DUT** — every inverter, last station.

**Steps**
1. Seal the calibration record: layout 2, CRC-32, the device UID, the SKU, f_sw, and the motor ID if the motor is
   known at EOL. With motor_id 0 the unit cannot arm until QP-EOL-14 runs.
2. Read back the CAL and VAL records and `TI_FW_ID`.
3. Clear the EOL DTCs and confirm the service lock is absent.
4. Park in LPOFF.

**Pass** — every read-back matches and every CRC passes (`calib_check` = 0 once the motor is bound); no service
lock; INV_STATUS byte 15 lists only "motor not bound" where that is expected.

**Record** — the final configuration record.

**On fail** — re-seal, or reject.

### QP-EOL-14 · Vehicle commissioning (per vehicle; motor-bound items)

**Closes** —
- the FW-20 motor fields (electrical zero, pole pairs, motor data, sensor type, MTPA, f_sw, `rule_b_released`);
- target-bringup T-07, T-31 and T-37 (`cal_rslv_wind_per_mon`, DC-link trim gains on the real bank, precharge τ);
- *Sensing A.15* "Amplitude planes" ("EOL characterises monitor-to-terminal transfer with the real harness");
- F17 (termination);
- interface-requirements (EOL/calibration — resolver monitor-to-winding ratio; motor — pole pairs,
  motor-temperature sensor type, resolver transformation ratio).

**DUT** — the inverter in the vehicle, or on a dyno with the vehicle harness, with the motor from QP-SF-10.

**Steps**
1. Measure winding/monitor with the real harness and the actual resolver, PTCs cold. Write `cal_rslv_wind_per_mon`
   (CAL 0.8–1). Never credit a post-trip PTC.
2. Find the electrical zero.
3. Load the motor dataset and the QP-SF-10 decisions (FS1B policy, n_ss, `rule_b_released`).
4. Set the precharge τ.
5. Seal the record with the motor ID.

**Pass** —
- The FW-20 ranges hold: L_d, L_q 20 µH–5 mH; R_s 1 mΩ–0.5 Ω; ψ 0.01–0.5 Wb; pp 1–12; id_demag 10–3000 A;
  n_max 1000–30000 rpm; a PT1000, or an NTC with R25 1–100 kΩ and B 2000–5000 K; an MTPA table that increases
  monotonically.
- r ≥ 0.903, and ≥ 0.950 for no nuisance trip (QP-EOL-08).
- The unit arms and reaches the first torque step.

**Record** — the commissioning record.

**On fail** — per item.

---

## 12. Marine deltas

The Road QPs apply to the Marine forks unchanged, except where a delta below changes a number, adds a test or adds
a class requirement. M8 is the Road 8I build frozen at A.15. M8-SiC is the Road 8S build. M10 has a new 1100 V
power stage with the Road control card.

The round-17 card changes (DEXP/DEXN, SMDJ8.5A-HRA, RFS4 ESR18) reach the Marine forks only through a marine ECO
(marine §9). Until then, the marine §7 QPs use the part list of the marine fork being qualified.

### QP-MA-01 · Identity and parameter sets

**Closes** — MFW-01; marine §9 (identity).

**Delta to QP-EOL-01** — V_ID windows at ±4 %: 4.12 V (M8, RHWID 47 k), 3.75 V (M8-SiC, 30 k), 0.45 V (M10, 1 k).

**Pass** —
- Road firmware refuses every marine code, and marine firmware refuses every Road code (0.90 / 1.60 / 2.50 /
  3.44 V).
- The FW-05 trip is at ±583 A (M8) or ±525 A (M10), in instantaneous amperes (QP-FW-06 delta).
- The M10 kit is proven by LCR, not by τ: M10's nominal 0.71 s equals the Road 4XX value (QP-EOL-03 delta:
  20 × 15 µF, 5 × 470 Ω, 2 × 8 × 22 k).

### QP-MA-02 · M10 double-pulse test at 1100 V and the 3 × US1M DESAT string

**Closes** — marine §12 gate 7 ("M10 double-pulse test at 1100 V (sets RG)"); marine §9 DESAT-string row.

**DUT** — M10 power stage (HCG600FH170D3E1 and E1A, both sources), start RG 1.0 / 1.0 Ω, on F-DPT with a 1300 V
source.

**Pass** —
- V_CE,pk ≤ 1530 V (derived: the Road guard ratio 1080 / 1200 = 0.9 applied to 1700 V), unless hiitio or the
  class states a different repetitive limit. BV's reading of 1.5 / 1.8 × U_P is a class input (marine §2 and §12
  gate 4).
- V_CE at t_on + 1.22 µs is < 3.65 V (the minimum trip of the 3 × US1M string, against a V_CE(sat) of 1.92 V).
- The QP-PS-02 criteria apply otherwise.

### QP-MA-03 · Marine short-circuit and DESAT

**Closes** — marine verification rows "DESAT worst detection + soft-off (82 pF blank)" for M8 and M10 (WARN ×2) and
M8-SiC "Short-circuit withstand" (WARN); marine §12 gate 7 (the contained SC test on the 1700 V IGBT).

**Delta to QP-SC-02/03** —
- M8 is QP-SC-02 at 850 V: t ≤ 5 µs derated (4.81 / 10.29 µs predicted).
- M10 at 1100 V and 16.9 V: t_P and t_ext ≤ 4.8 µs (6 µs at 1000 V derated to 1100 V). The predictions are 4.35 µs
  at 400 mA and 8.44 µs at 100 mA.
- M8-SiC follows QP-SC-03. No marine SiC delivery before the hiitio letter.

### QP-MA-04 · Thermal at 45 °C coolant: continuous, overload and standstill

**Closes** — marine §3, §5 (cell ratings), §6b (standstill); marine §12 gate 1 (the terminal RMS rating — see
vendor-requests.md, hiitio) and gate 7 (coldplate Rth, shared with QP-TH-01).

**Delta to QP-TH-02** — coolant at 45 °C; continuous S1 duty.

**Pass** —
- **Continuous.** Tj ≤ 125 °C at the continuous current (M8 300 A rms, M10 270 A rms); IGBT 117 / 119 °C and
  diode 97 / 96 °C are predicted.
- **Overload 110 % / 60 s.** Tj ≤ 150 °C: M8 at 330 A (≤ 125 °C predicted), M10 at 297 A (≤ 127 °C predicted).
- **Standstill.** At 1 kHz below 2 Hz, rated torque holds indefinitely at 117 °C (M8) or 139 °C (M10). The MFW-06
  stall timer warns after 10 s at ≥ 50 % current and < 1 % speed.
- **Can ripple.** M8 12.2 / 13.4 A of 15.4 A; M10 8.8 / 9.6 A of 15 A.
- **Type approval.** Temperature rise at 45 °C air / 32 °C sea water (QP-MA-10).

### QP-MA-05 · Marine discharge: M10 values and the 103 W stuck-ON case

**Closes** — marine verification "QDIS stuck ON with the battery connected" rows (WARN ×3: M8 384 W, M10 515 W,
M8-SiC 384 W); marine §7; §15 item 5.

**Delta to QP-DS-01/03** —
- M10 active + passive 1100 → 60 V takes ≤ 2.33 s (≤ 5 s, IEC 61800-5-1); passive ≤ 89.6 s (≤ 120 s).
- ≤ 40.3 J per resistor. The switch is ≥ 1700 V class.
- The M10 stuck-ON case is 515 W / 5 = 103 W per resistor — above the 70–100 W window of the A6-R11 opening
  criterion. The QP-DS-03 characterisation therefore runs at 103 W too, and the no-flame statement is confirmed at
  that power before M10 release.

### QP-MA-06 · Control power (24 → 12 V) and sustained ASC

**Closes** — marine §2 (control power ±10 %, battery-fed +30 / −25 %), §6 ("Sustained ASC needs gate power"), §11
kit (the isolated 24 → 12 V converter, 60 W).

**DUT** — M8 with the marine kit, its converter fed from a class-grade 24 V, on F-NOHV.

**Pass** —
- The converter holds KL30 in 9–16 V over ship 24 V from −25 % to +30 % (18–31.2 V). The Road LV side never sees
  24 V sustained: the ULDO15 thermal-shutdown case of QP-LV-02 must not occur.
- ASC holds for the vessel's towing duration (plan: 8 h) with VCC2 ≥ 13.5 V. The FS26 stays out of LPOFF throughout
  (the marine firmware rule).
- Loss of one of two diode-OR feeds does not interrupt ASC.

### QP-MA-07 · Multi-cell bench: PWM sync, shaft-bus load, 6ω loop, N−1

**Closes** — marine §12 gate 7 ("CAN-FD PWM sync accuracy; 6ω loop on a DUAL motor; 1 kHz standstill ripple");
marine verification "PWM carrier sync between cells" (WARN) and "N = 8 … shaft bus 58 % at 1 kHz" (WARN).

**DUT** — two to six M8 cells on a DUAL motor, or an emulated multi-set load (plan), with the marine firmware.

**Pass** —
- Carrier sync is ≤ 1 µs (1.8° at 5 kHz). Loss of sync raises an alarm, not a trip.
- Shaft-bus load is ≤ 60 % (58 % at N = 8 and 1 kHz; drop to 500 Hz above that).
- The 6ω loop reduces the 5th / 7th from about 30 % / 10 % to ≤ 3 % / 1 %.
- One cell lost leaves the others at their N−1 limits without a trip; a follower holds its last reference for
  50 ms, then ramps.
- The 1 kHz standstill ripple is recorded against the QP-MA-04 standstill point.

**On fail** — route the spare JVEH SP1 / SP2 pins to a sync input (a card ECO; marine §6).

### QP-MA-08 · DC-grid joining (precharge) and the marine FW-06 peaks

**Closes** — MFW-12; marine §6b (per-cell precharge); marine §6c (FW-06 peak).

**Pass** —
- Precharge with 90 Ω (M8) or 110 Ω (M10) stays ≤ 10 A, 116 / 182 J, ≈ 150 ms. The cell closes only when
  |V_link − V_bus| < 5 %.
- Breaker opening under rated regen (the QP-FW-08 method): link peak ≤ 909 V (M8) or ≤ 1178 V (M10), 91 % of the
  cans' rating.

### QP-MA-09 · M10 insulation and creepage (1150 V DC class)

**Closes** — marine verification "VGT12EEM flyback transformer" and "HC5FW 900-S/SP1 hall sensor" rows (WARN ×2);
marine §12 gate 3 and gate 8 (creepage PD2 or PD3, before the Road layout).

**Delta to QP-HV-02/03/06** —
- The working voltage is ≥ 1150 V DC. The transformer needs a certified ≥ 1150 V DC working rating, and the
  sleeve needs LEM sign-off at 1150 V (vendor-requests.md, TDK and LEM — the Marine rows).
- The V_DC divider is 8 × 1206, 138 V each at 1100 V.
- Creepage for PD3 / OVC III (ABS): 12.5 mm at 800 V and 16 mm at 1000 V. If PD2 inside a sealed coated enclosure
  is argued instead, it must be documented.
- IT network: at a first earth fault the whole link sits across one Y-cap; the VY1 is rated 1500 V DC (≥ 1150 V).

### QP-MA-10 · IACS UR E10 type-approval set

**Closes** — marine §11 type-approval plan; §2 class requirements.

**DUT** — M8 (then the M10 extension) with the marine kit.

**Steps and pass** (E10 / DNV CG-0339) —
- dry heat 55 °C for 16 h operating (70 °C in a shared cubicle);
- damp heat 55 °C / 95 %, two cycles;
- vibration class A, 0.7 g (class B, 4 g, if mounted on machinery);
- insulation resistance and HV withstand at the levels agreed with the society (the E10 tables stop at 690 V);
- EMC to E10, including 24 dBµV/m at 156–165 MHz;
- a short-circuit type test;
- temperature rise at 45 °C air / 32 °C sea water;
- IP44 minimum; anti-condensation heater working.

Pass is the class surveyor's acceptance, with the Road functional set after each test.

---

### QP-MA-11 · Marine exciter terminal-fault sweep at the ship's voltages (gate ㉘ on the Marine kit; RELEASE GATE, round 19)

**Closes** — marine/verification-report.md §7 "Exciter terminal fault on the Marine LV" (PTC current PASS at the kit-loom
minimum, sustained-short WARN, sub-8 A window INFO); the Marine kit requirement "≥ 0.27 Ω on the exciter fault loop — a routing/segregation
rule for the kit harness" (marine/design-basis.md §12); Road F193/F199/F200 as they apply to the shared card.

**DUT** — the Road card in the Marine kit (the shared PCB, HW A.18), fed from the kit's isolated 24 → 12 V converter
(≈ 5 A limit) and, for the ship-bus cases, from a bench source at the bus voltage through the kit loom replica.

**Fixture and instruments** — as QP-RX-04 (F-RX; I-CP on the PTC, TVS and RSX leads; I-TC on the TVS lead and the PTC;
I-LVS at the protected node, VEXD and the kit rail); the kit loom replica (length and cross-section as built).

**Steps**
1. Energy-limited first (24 V at a 2 A limit).
2. **Direct short, ship bus.** 18, 24 and 31.2 V through the loom replica and through 0.27 Ω, for 60 s, EXC+ and EXC− in
   turn, PTC cold / hot / post-trip. Record I_PTC(t), V_TVS(t), ∫v·i and the trip time.
3. **Sustained short, card asleep (the platform's worst case).** With the card in LPOFF and the kit rail live, short each
   line to the kit rail at 11.4, 12.0 and 13.2 V (the low-V_P case that set the 7.0 V TVS class, F203: 2.0–2.5 W
   predicted uncapped, ≤ 132 °C junction coupled) through 0, 0.5, 2, 5, 10, 20 and 35 Ω, 10 min or until the TVS lead
   temperature settles (abort at 150 °C, record the time); then the ship bus at 18, 24 and 31.2 V through 0.5, 2, 5, 10,
   20, 35, 70 and 100 Ω. Repeat the 12.0 V / 0 Ω and 24 V / 20 Ω points at −25 °C and 55 °C (the marine cabinet
   range). Then repeat the 12.0 V and 24 V sweeps with the card awake.

**Measure** — as QP-RX-04, plus the kit converter's output during the fault (current, foldback) and the RSX temperature
awake.

**Pass**
- **Direct short.** I_PTC ≤ 40 A through the loom replica (≤ 38 A predicted at 0.27 Ω, 31.2 V cold corner, 7.0 V clamp) and the trip
  time ≤ 20 ms at ≥ 8 A with ∫v·i ≤ 5.3 J (the Road release criterion).
- **Sustained short, asleep.** As QP-RX-04 step 2b: TVS lead ≤ 125 °C at every settled point and nothing damaged; a point
  where the PTC trips on the TVS's heat within 60 s, TVS undamaged, is CONDITIONAL (the island coupling is re-run into
  the rows); a destroyed TVS is FAIL unless the end state is the fail-safe one (TVS short, PTC tripped and holding on the
  follow-on ≈ 14 A at 12 V / the converter's limit, FW-10 fault at key-on) — then CONDITIONAL with the layout island's
  pair transfers and the Marine operating concept (card awake while the rail is present) as corrective actions; the
  round-19 class (SMDJ7.0A-HRA) is the shared card's.
- **Awake.** RSX ≤ 155 °C, the amplifier reaches OTF and recovers, parametrically unchanged (QP-RX-01 afterwards).

**Record** — the sweep table per voltage; the kit converter's behaviour; the loom replica's measured resistance.

**On fail** — first the layout (the pair's transfers: island, vias, PTC placement) and the loom (segregation, or a series
element in the kit — ours to add); then the shared-card TVS class (the 6.5A buys 6–9 °C for +0.014 Ω; xcheck19).

## 13. Traceability

### 13.1 README gates → QP

| Gate | Short | QP |
|---|---|---|
| ① | MCU package and ball map (closed A.13) | residual: QP-FAI-01, QP-FAI-02 |
| ② | SiC DPT at 850 V / 481 A, cold and hot | QP-PS-01 (IGBT populations: QP-PS-02) |
| ③ | Short circuit: SiC letter, contained tests, NOVOSENSE soft-off and I_STO | QP-SC-01, QP-SC-02, QP-SC-03, QP-SC-04 + vendor-requests.md (hiitio, NOVOSENSE) |
| ④ | Coldplate Rth and can sharing | QP-TH-01, QP-TH-02, QP-TH-03 |
| ⑤ | Bias bank at worst parts, VGT12EEM Isat and working insulation | QP-GD-03, QP-HV-02 |
| ⑥ | Motor data for the safe-state matrix, HV backup bias | QP-SF-10 |
| ⑦ | FS26 OTP readback | QP-EOL-02, QP-SF-06, QP-EOL-01 (M_PROGID) |
| ⑧ | 4XX can (bound A.12; ripple checked at PO) | QP-PS-04 |
| ⑨ | LEM lead time | none — procurement (§14) |
| ⑩ | LEM sleeve sign-off | QP-HV-03 |
| ⑪ | Opto barrier (closed A.12) | residual: QP-HV-01 |
| ⑫ | HIL then dyno: contactor opening under full regen | QP-FW-02, QP-FW-08 |
| ⑬ | LV-only gate-supply bench | QP-GD-01, QP-GD-02, QP-LV-02 |
| ⑭ | ASC entry on all six V_GS, every path | QP-SF-01 |
| ⑮ | Discharge-resistor fault-opening | QP-DS-03 (characterisation since round 17) |
| ⑯ | PTC25/PTC26 to an eFlexPWM FAULT input (closed A.12) | residual: QP-FW-01 |
| ⑰ | NOVOSENSE: DESAT during ASC with EN low | QP-SF-04 + vendor-requests.md (NOVOSENSE) |
| ⑱ | VOW3120 100 °C ambient, LED drive and timing | QP-GD-05, QP-TH-04 |
| ⑲ | No-HV fixture: DESAT during PWM-ASC | QP-SF-03 (HV: QP-SC-04) |
| ⑳ | FAULT_OUT wire faults | QP-LV-05 |
| ㉑ | V5GD-off state | QP-SF-08 |
| ㉒ | FW-16 on HIL; ASC-entry current vs DESAT minimum | QP-SF-09, QP-SF-10 |
| ㉓ | Bias and gate levels | QP-GD-04 |
| ㉔ (barrier) | Barrier certificates at PO | QP-HV-01 |
| ㉔ (parking) | Parking drain; self-test energy fixture | QP-LV-06, QP-SF-09 |
| ㉕ | Resolver bench: amplitude; KL30 short at the SDADC pins | QP-RX-01, QP-RX-03 (and QP-RX-02) |
| ㉖ | Discharge resistor, no-flame closed by the TT statement; fail-open time characterised | QP-DS-03 |
| ㉗ | LV entry vs the OEM test-B R_i | QP-LV-03 |
| ㉘ | Terminal-fault bench (characterisation since round 17; QP-RX-04 step 2b and the QP-RX-05 reverse-current measurement release gates since round 18; the ≥ 8 A clearing measurement a release criterion and the Marine sweep since round 19) | QP-RX-04, QP-RX-05, QP-RX-06, QP-RX-07, QP-RX-08, QP-MA-11 |
| Target evidence | Fault-route injection with the CPU halted, the ≤ 15.6 µs chain, REG_PROT, WCET | QP-FW-01, QP-FW-02, QP-FW-03, QP-FW-04, QP-EOL-05 |

### 13.2 verification-report.md WARN rows → QP

**Still WARN after round 17 (15).**

| # | Section | Row | QP |
|---|---|---|---|
| 1 | Power stage — 8XX IGBT | Tj steady-state bound, peak 30 s (340 A, 850 V, 5 kHz) | QP-TH-01, QP-TH-02 |
| 2 | Power stage — all SKUs | Turn-off overshoot, SiC 850 V / 481 A, cold (RG_OFF 6.8 Ω) | QP-PS-01 |
| 3 | DC link — 4XX bank (4XX IGBT) | 4XX can binding | QP-PS-04, QP-TH-03 |
| 4 | Gate drive | VCC2 low corner vs recommended-min | QP-GD-01 |
| 5 | Gate drive | Gate-power demand per bank, SiC @20 kHz option | QP-GD-03 |
| 6 | Gate drive | DESAT worst detection + soft-off, SiC 47 pF | QP-SC-01, QP-SC-03 |
| 7 | Gate drive | DESAT worst detection + soft-off, IGBT 82 pF | QP-SC-01, QP-SC-02 |
| 8 | Flyback | Derived gate rail VCC2 (both diodes, all corners) | QP-GD-01 |
| 9 | System A.11 | SPO freewheel energy at 340 A rms … (8XX SiC …) | QP-SF-10 |
| 10 | System A.11 | SPO freewheel energy at 400 A rms … (4XX IGBT …) | QP-SF-10 |
| 11 | Gate drive | SC current-extinction budget, SiC (F07) | QP-SC-03 |
| 12 | Gate drive | SC current-extinction budget, IGBT (F07) | QP-SC-02 |
| 13 | LV A.11 | Load dump test B (35 V, 400 ms) into the TPSMC24CA-VR + MF-LSMF300/24X polyfuse | QP-LV-03 |
| 14 | Power stage — all SKUs | IGBT short-circuit rating condition vs the design corner | QP-SC-02 |
| 15 | Safety | ASC drive path | QP-SF-07 |

**WARN at A.15, closed in round 17, kept as characterisation (12).**

| # | Section | Row (round-17 closure) | QP |
|---|---|---|---|
| 16 | Discharge — 8XX values | QDIS stuck ON with the battery connected (G-05, F178) | QP-DS-03, QP-DS-04 |
| 17 | Discharge — 4XX values | QDIS stuck ON with the battery connected (G-05) | QP-DS-03, QP-DS-04 |
| 18 | Discharge | Stuck-ON QDIS with the battery connected, 8XX (G-05) | QP-DS-03, QP-DS-04 |
| 19 | Discharge | Stuck-ON QDIS with the battery connected, 4XX (G-05) | QP-DS-03, QP-DS-04 |
| 20 | Safety A.9 | RFS4 with FS1B held a whole key-on (G-06, F179) | QP-LV-02, QP-TH-04 |
| 21 | Sensing | Hall ratiometric ref vs ADC ref (G-07, F180) | QP-TH-06, QP-EOL-06 |
| 22 | Sensing | Resolver drive @9 V KL30 (G-04, F177) | QP-RX-02 |
| 23 | Sensing A.15 | Exciter TVS energy (split into single fault / load-dump-coincident; G-02, F175) | QP-RX-04 |
| 23b | Sensing A.15 | Exciter TVS energy at fault currents ≥ 8 A — WARN since round 19 (the I⁻² trip-time law is an assumption; F200) | QP-RX-04 step 2 (release criterion), VR-16 |
| 23c | Sensing A.17 / Marine §7 | Sub-8 A window and the sustained short with the card asleep (F193); the Marine kit rail (5.2 W at 12 V) | QP-RX-04 step 2b, QP-MA-11 |
| 24 | Sensing A.15 | Source-impedance allocation (now "Exciter PTC current vs I_max 40 A": single fault PASS, 35 V INFO; G-02) | QP-RX-04 |
| 25 | Sensing A.15 | Exciter back-drive with VEXD absent (now the rated diversion; G-01, F174) | QP-RX-05 |
| 26 | Sensing A.15 | Exciter PTC hold current (G-03, F176) | QP-RX-07 |
| 27 | LV A.11 | Load dump test B … classic TPSMC24CA … (now INFO, historical) | QP-LV-03 (incoming -VR check) |

Among the findings-log WARNs, F139 (LDO thermal, "measured at the hot first article") is covered by QP-TH-04.

### 13.3 simulation-report.md WARN rows (6) → QP

| Row | QP |
|---|---|
| S4 Tj static-plate bound — all SKUs | QP-TH-01, QP-TH-02 |
| S8 Loop-L budget — 3.3 Ω cold | QP-PS-01 |
| S8 Loop-L budget — 6.8 Ω cold | QP-PS-01 |
| S9 DESAT reaction, SiC 47 pF | QP-SC-01, QP-SC-03 |
| S9 DESAT reaction, IGBT 82 pF | QP-SC-01, QP-SC-02 |
| S10 ASC hold-up after TOTAL LV loss | QP-SF-07, QP-SF-10 |

Simulation PASS rows with a bench note are covered too: S1 → QP-GD-02, S2 → QP-LV-01, S3 → QP-TH-03, S5 → QP-DS-01,
S6 → QP-FW-08, S8 6.8 Ω hot → QP-PS-01.

### 13.4 Disposition "What stays open" and the round-17 gap register → QP

| Source | Item | QP |
|---|---|---|
| A13 | unchanged gates ②③④⑤⑥⑦⑨⑩⑫⑬⑭⑮⑰⑱⑲ ⑳㉑㉒㉓㉕㉖㉗ | §13.1 |
| A13 | ㉘ terminal-fault bench | QP-RX-04…08 |
| A13 | target evidence: fault-route injection with the CPU halted, OVP chain ≤ 15.6 µs, REG_PROT lock readback, WCET | QP-FW-01…04, QP-EOL-05 |
| A13 | 4XX/8XX power-stage populations qualified separately (DPT, SC, thermal) | QP-PS-01/02, QP-SC-02/03, QP-TH-01/02 |
| A14 | ㉘ widened (VEXD states, both polarities, PTC/TVS/reverse-rail currents) | QP-RX-04, QP-RX-05, QP-RX-06 |
| A14 | native-KiCad export from a running KiCad (closed in round 17 by the KiCad 9/10 proof) | QP-FAI-01 (release repeat) |
| A15 | ALM2402 reverse-diode envelope (replaced in round 17 by the rated diversion) | QP-RX-05 |
| A15 | PTC clearing waveform at the allocated source impedance | QP-RX-04 |
| A15 | measured excitation current against the 70 mA hot hold | QP-RX-07 |
| A15 | amplitude at the winding with the selected resolver | QP-RX-01, QP-EOL-14 |
| A15 | the firmware's target evidence and new records | QP-FW-01…05, QP-EOL-05, QP-EOL-13 |
| A16 | "What stays open": vendor answers, OEM/motor data and "the physical qualification tests (QP)" | this plan; §14 lists the inputs |
| A16 G-01 … G-07 | round-17 closures that keep a measurement | RX-05, RX-04, RX-07, RX-02, DS-03, LV-02, TH-06 |
| A16 G-08, G-13, G-14 | KiCad proof, harness numbering, SMDJ/PMEG symbol class | QP-FAI-01, QP-FAI-03 |
| A16 G-11 | LV input, load dump (study pending) | QP-LV-03 |

### 13.5 Contract §10c/§11 and the target-bringup checklist → QP

| Item | QP |
|---|---|
| Double-pulse (turn-off overshoot at 850 V, R_G_OFF) | QP-PS-01, QP-PS-02 |
| Contained short-circuit tests per silicon | QP-SC-02, QP-SC-03, QP-SC-04 |
| Thermal / coldplate | QP-TH-01…03 |
| EMC | QP-LV-07, QP-LV-08 |
| LV transients (ISO 16750-2 / 7637-2 per OEM) | QP-LV-02, QP-LV-03, QP-LV-04 |
| Insulation coordination | QP-HV-04, QP-HV-05, QP-HV-06 |
| Mechanical DV | QP-TH-05 |
| Round-7 gates: LV-only gate-supply bench · ASC entry measurement · FW-06 chain on HIL | QP-GD-01 · QP-SF-01 · QP-FW-02 |
| FW-31 current-loop liveness · FW-32 service-lock clear | QP-FW-05 · QP-DS-04 |
| FW-21 (bootloader + HSE) | none (§14) |

| Checklist | QP | Checklist | QP |
|---|---|---|---|
| T-01 | FW-01, EOL-05 | T-20 | FAI-02 |
| T-02 | FW-01 | T-21 | FAI-02, SF-06 |
| T-03 | FW-03 | T-22 | FW-05 |
| T-04 | FW-03 | T-23 | FAI-02, FW-09 |
| T-05 | EOL-05, FW-01, FW-02 | T-24 | FAI-02, SF-06 |
| T-06 | EOL-01, EOL-05 | T-25 | FAI-02, FW-09 |
| T-07 | EOL-06/07/08, EOL-13, EOL-14 | T-26 | FW-09, DS-04 |
| T-08 | FW-02, FW-06 | T-27 | EOL-05 |
| T-09 | FW-02, FW-06 | T-28 | FW-05 |
| T-10 | FW-02, FW-06 | T-29 | RX-01, FW-05 |
| T-11 | FW-02 | T-30 | RX-01 |
| T-12 | FW-05 | T-31 | RX-01, FW-05, EOL-08, EOL-14 |
| T-13 | FAI-02, FW-05 | T-32 | SF-06 |
| T-14 | FAI-02, FW-05 | T-33 | SF-06 |
| T-15 | FW-01, SF-09 | T-34 | EOL-02 |
| T-16 | SF-09 | T-35 | DS-04 |
| T-17 | SF-09 | T-36 | FW-04, SF-02 |
| T-18 | FAI-02 | T-37 | EOL-14 with SC-01, SF-05, SF-09, GD-01, FW-05, FW-08, DS-01, TH-02, EOL-01 |
| T-19 | FAI-02 | T-38 | FW-07, FW-09, SF-10, SF-11 |

CAL values that are measured, and where:
- `cal_desat_en_hold_us` → QP-SC-01
- `cal_oneshot_wait_us` → QP-SF-05
- `cal_rslv_hold_us`, `cal_isns_stale_us`, `cal_rslv_latency_us`, `cal_swg_code_init` → QP-FW-05
- `cal_isns_act_*`, `cal_isum_tol_a` → QP-FW-06
- `cal_rslv_exc_target_vpp`, `cal_rslv_wind_per_mon` → QP-RX-01, QP-EOL-08, QP-EOL-14
- `cal_peak_recovery_s` → QP-TH-02
- `cal_qdis_*` → QP-DS-01, QP-DS-04
- `cal_precharge_*` → QP-DS-04, QP-EOL-11, QP-EOL-14
- `cal_spo_release_a`, `cal_speed_hold_ms` → QP-FW-07
- `cal_dcl_tmax_nm` → QP-FW-07, QP-FW-08
- `cal_vdyn_reserve_frac` → QP-FW-08
- `cal_ntc_*` → QP-TH-02, QP-EOL-04
- `cal_fs26_prog_id` → QP-EOL-01/02
- `cal_sensor_selftest_ms`, `cal_fs0b_release_ms` → QP-SF-06

---

## 14. Open items that are not test procedures, and the OEM inputs still missing

| Item | Why no procedure | Where it is handled |
|---|---|---|
| ⑨ LEM lead time | A procurement action ("order T2 parts at kickoff") with no measurable acceptance. | vendor-requests.md (LEM — lead time) |
| HARA, FMEDA, DFA; the FMEDA entries for the FB-sense chain (A7 gate 11); the FS1B-ASC double fault | Analysis work products, not tests. QP-SF-04 feeds the FMEDA. | Safety plan |
| FW-21 (signed images, rollback, HSE bootloader) | Not implemented in the application image (contract §10 FW-21; firmware README item 14). | Bootloader deliverable |
| The product SecurityAccess key (`TI_UDS_KEY_FN`, HSE TRNG seed) | A release-build item (T-35). QP-DS-04 tests the product build once it exists; the default build is tested for fail-closed. | Release process |
| Supplier statements: hiitio module Ls, SiC t_SC, IGBT SC at 850 V / 16.9 V, Zth and FIT, aux-pin variant; NOVOSENSE guaranteed I_STO, RST/EN during soft-off with the EN-low + ASC-high row, ASC during VCC2 UVLO; TDK Isat and working insulation; LEM sleeve; Bourns PTC clearing and above V_max; Littelfuse long-pulse TVS data; TT SQP10 fail-open; NXP RM values and OTP variant; Samtec automotive MPN; Diodes production status; coldplate supplier Rth | A sample test characterises a part; it cannot supply a guaranteed spread or a rating. The linked QPs measure what is measurable and use the statement as the limit. | vendor-requests.md, by vendor and topic |
| **OEM inputs that the procedures need — carried as IR-34…IR-41 in interface-requirements.md since round 17 (to be filled by the OEM)** | The procedures are complete; only these levels are missing: the EMC standard edition, classes and levels (QP-LV-07/08); insulation test levels and PD limits, production and type (QP-HV-02/04/05, QP-EOL-10); the pollution degree and OVC decision (QP-HV-06, QP-MA-09); ISO 16750-3/-4 profiles (QP-TH-05); ISO 16750-2 / 7637-2 test levels beyond the four covered topics (QP-LV-04); the sleep-current budget at temperature (QP-LV-06); the CAN DBC and E2E DataIDs (QP-FW-09, T-38); whether negative exciter faults are allocated (QP-RX-06); the coolant flow and pressure (QP-TH-01; interface-requirements records it as unspecified). | To be added to interface-requirements.md |
| Marine class documents: BV's 1.5 / 1.8 × U_P reading; IRS/DNV HV test levels above 690 V | Class decisions; they enter QP-MA-02 and QP-MA-10 as inputs. | marine §12 gate 4 |
| Marine owner duty profiles (the tug rating) | They set a rating, not a test of the product. | marine §12 gate 6 |
| Gates ① ⑪ ⑯ (closed) | Only the residual checks named in §13.1 remain. | — |

---

## 15. Inconsistencies found while setting the criteria

These are observations. None of the source documents was changed; each one needs an owner decision.

1. **ASC exit: 1.0 µs vs 1.06 µs.** Contract FW-06a and the firmware (`asc_exit_hs_delay_ns` = 1000) keep the first
   high-side pulse ≥ 1 µs after ASC_CLR, "the ASC pins release in ≤ 0.75 µs". That figure is from the TLP152 era.
   The A.12 re-verification gives release ≤ 1.06 µs (VOW3120 t_PHL 0.5 µs max + DASCR discharge + t_ASC_f
   0.48 µs). For SiC the 1.0 µs dead time does not cover the difference. Target-bringup T-36 also checks only the
   ≥ 1 µs gap. QP-SF-02 makes non-overlap the criterion; the likely fix is a parameter change.
2. **S10 command-path collapse.** The "≈ 1 ms" collapse of the ASC command path is modelled in the `sim-verify.mjs`
   S10 comment for the QA01C/TLP152 path (UVLO 7.5–9.4 V). The fitted VOW3120 falls out at 9.5–12 V, and the figure
   was not re-derived. Interface-requirements (LV supply — total LV loss) repeats "≈ 1 ms". It is not credited for
   safety, but QP-SF-07 measures it.
3. **Duplicate gate number.** The README uses ㉔ twice: barrier statements, and parking drain + the self-test
   energy fixture. This plan calls them ㉔ (barrier) and ㉔ (parking).
4. **Marine M10 stuck-ON QDIS.** 515 W across 5 × 470 Ω is 103 W per resistor, above the 70–100 W window of the
   A6-R11 opening criterion. QP-MA-05 extends the characterisation.
5. **Contract numbers behind the verifier.**
   - Contract §1, §4b, §4c and the FW-06 table give "≤ 7.5 µs" for ASC entry; the verifier row gives 7.56 µs, and
     README gate ⑫ says 7.6 µs.
   - §4c gives "pins release ≤ 0.75 µs" in two places; the verifier gives 1.06 µs.
   - This plan uses the verifier values.
6. **Texts not yet updated for the round-17 part changes.**
   - [`dfm.md`](dfm.md) §4 still gives RFS4's terminal rule for the 0603 ESR03 (≤ 132 °C at 0.30 W); RFS4 is now
     the 1206 ESR18. QP-TH-04 records the ESR18's termination temperature.
   - The verification-report row "RFS4 with FAULT_OUT shorted to KL30 and FS1B asserted" still states the ESR03
     limit (0.27 W at 85 °C).
   - Interface-requirements (LV supply — 24 V jump start) still cites RFS4 at 1.47× nameplate.
   - Interface-requirements (harness — exciter-line fault impedance ≥ 0.27 Ω at 35 V) is written as a single-fault
     requirement, while the round-17 rows treat the 35 V case as a double event (INFO).
   - Vendor-requests (Bourns and Littelfuse, exciter rows) describe the round-16 SMCJ8.5A; its own round-17 note
     says so.
7. **Stale MGJ2 PO gate.** [`dfm.md`](dfm.md) §5 still names the MGJ2D150505SC certificate as a PO gate. The part
   was replaced by the UCC12050 / UCC12051-Q1 in A.11; the verification report now marks its MGJ2 line stale.
   QP-HV-01 checks the fitted parts.
8. **IGBT extinction budget.** The IGBT current-extinction budget at the *typical* 400 mA soft-off current (6.61 µs)
   already exceeds the 6 µs withstand. Only a measured fall/tail shorter than the assumed 1.8 µs can close gate ③
   for the IGBT without a design change. QP-SC-02 measures that term directly. This is a known WARN, restated here
   because it decides the test.
9. **FS26 watchdog cadence.** At the time of writing, target-bringup T-32 and timing.md still describe the
   ≥ 2000 µs due rule as "not changed": the FS26 would be answered every ≈ 3.0 ms, at the end of its open window.
   Meanwhile `fs26.c` already defines `WD_DUE_US` 1500, and review-A16 says the fix is "being fixed in the same
   round". The docs and the code disagree until round 17 finishes. QP-SF-06 settles it on silicon with the T-32
   acceptance.
