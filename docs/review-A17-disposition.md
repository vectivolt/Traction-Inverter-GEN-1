# Review round 18 — three rechecks of `4425af9` (rev A.17)

**Inputs.** Three independent delta rechecks of the A.16 push `4425af90…` (25 September 2026): (1) an HTML recheck that
ran 15 C cases / 143 assertions on the acquisition code under −O2 and ASan/UBSan, 13 reduced-order electrical fault
simulations of the exciter protection and 128 ideal-MFB corner evaluations (A16-R01…R04 html); (2) a Markdown recheck
with a C excerpt harness (8 cases / 128 assertions), 12 local graph assertions and a manufacturer-pin-number comparison
with negative controls (A16-R01…R04 md); (3) a CSV register of three findings with executed counterexamples against the
SDADC ring and the resolver angle extrapolation (A16-R01…R03 csv). The instruction: fix and align everything end to
end; verify every claim independently before acting; classify each finding as Confirmed / Already Fixed / Firmware
Handled / Not Applicable / False Finding / Improvement Recommended; implement only what is genuinely required, no
over-engineering, firmware where it is reliable, hardware where software cannot act in time.

**Method.** Every claim was taken to its source before anything was edited: the card net list and the built
`circuit.json` (pin numbers, DEX nets), `packages/cells.tsx`, the legacy and modern KiCad sets, the verifier
expressions, the firmware (`app.c`, `current.c`, `vdc.c`, `s32k396_adc.c`, `sdadc_ring.c`, `s32k396_resolver.c`,
`resolver.c`, the host simulation), and the archived datasheets read with `pdftotext` (PMEG4050EP-Q Table 2 and
Table 7, NCV4276C maximum ratings, MF-MSMF /33X row, derating table and the p.7/p.8 trip curves rendered, ALM2402
§8.3.6 and Fig. 7, SMDJ-HRA ratings, ROHM ESR characteristics). Three agents then worked in parallel: an **Opus
adversarial cross-check** of every number behind the exciter rows (`xcheck18`: it confirmed the diversion figures after
the move to within 3 % and found four further errors of ours — §"Cross-check findings"), a **Sonnet datasheet audit**
of the 15 bound diode part numbers and the KiCad/IPC pin conventions (two sheets archived), and an **Opus firmware
agent** implementing the three timing defects with fail-before/pass-after tests. Every hardware change is one line
of the card or one shared cell, mutation-tested in the ERC; every closed number is recomputed by `design-verify.mjs`.

## Verdict

All three rechecks are right on what they found, and two of them under-stated it. The pin-number defect is not one
diode but the shared cell behind all 35 two-pin diodes; the diversion diode really did bypass the resistor its own
model divided by; the two firmware time-ordering defects are target-only (the host model hid them); the latency sign
is inverted and its test encoded the inversion; the "any source impedance" PASS rested on a trip-time bound that
exists only at ≥ 8 A, and the LDO's 40 V output rating was credited as reverse-current evidence it does not give.
The cross-check then found the largest gap of the round, which no reviewer had: with the ECU asleep, a sustained short
of an excitation line to the *normal* 12.6–16 V battery leaves 1.5–3.8 W in the TVS indefinitely once the PTC has
tripped, because the tripped PTC keeps passing P_d/(V_S − V_BR). No affordable part closes that window (a lower
TVS voltage moves it to the direct short, a higher one lets a normal-voltage short charge VEXD past the amplifier's
18 V through the diversion), so it is closed the way the rest of this design closes such things: a computed WARN row,
a layout rule that makes the TVS's own heat trip the PTC, a bench sweep as a release gate, an interface requirement and
a vendor question — with the fail-safe end state written down. **Hardware changes: two, both zero-count** — the diode
anodes move one node (to the amplifier output) and RSX becomes an anti-surge 0.5 W part on the same pad. Nothing
else in the schematic changes; the pin-number correction changes numbers, not nets. **Firmware: three fixes**
(FW-34…36). **Every reviewer "NOT READY" item is either fixed here or is a numbered gate with a procedure.**

**"Already Fixed" for a gate means the gate exists and is open** — never that the physical qualification is complete.

## Review 1 — HTML recheck (A16-R01…R04 html)

| ID | Review said | Class | Verification | Action in rev A.17 |
|---|---|---|---|---|
| A16-R01 (html) | MAJOR — the current ISR saves now_us before sense_fast(); the HAL stamps completed reads with a later timer value; isns_update() subtracts unsigned → fresh samples declared stale (age 4 294 967 295 µs); same pattern in the VDC path; test phase and VDC together across timer rollover | **Confirmed** (target only) | `app.c` 423/427, `s32k396_adc.c` 171/90 (hal_time_us() after the ISR entry), `ti_types.h` 53 (unsigned); the host stamps with the frozen sim clock (`sim_hal.c` 452/464), so the suite could not see it; `rslv_age()` has the same shape for a completion that preempts the ISR | **FW-34**: check-time read after the acquisition reads (ISR-entry time kept for WCET/liveness), a signed-safe freshness helper for sensor stamps, a host knob that advances the sim clock inside each HAL read; phase + VDC + resolver tests across the 32-bit wrap. **F196** |
| A16-R02 (html) | MAJOR — the new diversion bypasses its assumed limiter: connector → PTC → VREX_PX → DEXP → VEXD; RSXP is on the other branch; the 2.2 Ω × 26.7 µF model is not this loop; 62 A at zero external resistance (57.8 / 50.4 / 45.5 A with 0.1 / 0.5 / 1 µH); the PTC's I_max is 40 A; the PMEG 70 A is an 8.3 ms half-sine at 25 °C; candidate: move each anode to the amplifier side of RSX | **Confirmed** | `control-card.tsx` 605–608 (DEX anode on VREX_PX, FEXP on the same node), `design-verify.mjs` 689 (`iRev0 = vFin / 2.2`); the cross-check reproduced 27–67 A at 24 V and 39–99 A at 35 V as drawn, TVS dark at t = 0⁺, and 4.6–5.0 A / 60 µs after the move | anodes moved to VREX_P/VREX_N (in parallel with the ALM2402 upper diode; zero parts); ERC lock + graph cut (no DEX on the protected node; mutation-tested); the back-drive row is the coupled TVS + diversion solve (PTC 37 A at 24 V/0 Ω), judged by I²t; card comments, parts-db, QP-RX-05, README ㉘ aligned. **F191** |
| A16-R03 (html) | The NCV4276C 40 V output limit is not a reverse-power specification; establish output/input/ground currents, inhibit behaviour, loads; keep a supplier/bench gate; a blocking path only if the results justify one | **Confirmed** (evidence gap, no hardware change) | `NCV4276C.pdf` maximum ratings: V_Q −1…40 V beside V_I ≥ V_Q + 0.5 V; the "−42 V reverse" feature is reverse-input protection with the output near 0 V (Figs 15/16/24); nothing on OUT > IN or a back-driven inhibited output | the claim withdrawn from the row, the card and the plan; QP-RX-05 a RELEASE GATE again (reverse current into OUT in the three input states, SMU sweep, 100 pulses + parametrics); **VR-33** to onsemi; the consequence if it conducts (1.3–1.6 A DC through RSX) stated; a blocking element only on a failed gate. **F192** |
| A16-R04 (html) | A larger TVS does not establish every clearing corner: the 20 ms maximum is at 8 A / 23 °C; at 5 Ω the current is ≈ 2.7 A; qualify cold, hot, post-trip and source-limited cases against temperature- and waveform-appropriate limits; do not insist on a larger TVS | **Confirmed** (over-claimed PASS) | MF-MSMF sheet: the only maximum is 0.02 s at 8 A; below it the trip time is a typical curve (p.8 curve C for the /33X — p.7 is the unsuffixed 30 V part); SMDJ Fig. 2 is an exponential pulse (≈ 5.3 J rectangular-equivalent at 85 °C, not 9 J); T_J max 150 °C; R_thJL 15 / R_thJA 75 K/W | rows recomputed at their worst corners: the bounded region PASS (1.7 J at the 8 A point; the ISO 16750-2 direct short inside it), the sub-8 A window and the ECU-asleep trickle as computed WARN rows, the 35 V TVS energy back to the double event, IR-16 → 0.05 / 0.29 / 0.22 Ω; closure: dfm.md layout rule (TVS island + PTC coupling), QP-RX-04 step 2b sweep (release gate), IR-42, VR-17 needed again; no part change. **F193** |
| html — amplitude/hold remarks | 60 Ω → 42.43 mA rms vs the 41/42 mA rounding; \|H(10 kHz)\| 2.076 with the coupling capacitor; 1.917–2.243 over 1 % R / 10 % C; 6.47 V pp at the low corner and 60 Ω; do not automatically raise the feedback resistor | **Improvement Recommended** (documentation; no hardware change) | cross-check §4: nominal 2.0761 confirmed; the bound parts are ±5 % C0G (parts-db), so the design band is 1.98–2.18 and QP-RX-01's "1.94–2.2 (±10 %)" was wrong both ways; the round-17 hold expression added 2.6 Ω of PTC not on the sheet; the five-corner stack at 60 Ω is 6.48 V pp — the trim saturates, DTC, no arm (an EOL rejection) | gain row over the bound tolerances + the ±10 % build band; QP-RX-01 band 1.98–2.18; hold current 41.9 mA at the setpoint, 48.3 mA at the CAL ceiling (69 % of the hot hold); planes at the sheet's 0.35 / 5.0 Ω; the stack recorded as INFO with REXA4 30.1 k named as the free margin lever (not applied: a 0.3 % shortfall at the coincidence of five tolerances, caught at EOL). **F195** |
| html — LV architecture / broader claims | The pin-side anti-series pair, CLVC3, fuse strategy and central-suppression assumption are implemented; the full transient matrix, waveforms, corners and consumer-KiCad connectivity were not rerun by the review | **Already Fixed** (round 17) / gates exist | `control-card.tsx` 235–237, `erc-audit.mjs` 620–667, QP-LV procedures, IR-02/03/34…41; the KiCad-10 netlist proof runs in `npm run sheets` (PASS again this round: 686 components / 434 nets) | none |
| html — readiness | Power-off bench READY; motor operation, freeze, HV dyno, DVT NOT READY until the diversion path and the gates | **Confirmed** as of A.16; see "Readiness" below for A.17 | — | — |

## Review 2 — Markdown recheck (A16-R01…R04 md)

| ID | Review said | Class | Verification | Action in rev A.17 |
|---|---|---|---|---|
| A16-R01 (md) | MAJOR — `Smd2FP()` assigns pin1 = anode; Nexperia PMEG4050EP-Q Table 2 says 1 = K, 2 = A; the legacy D symbol puts pin 1 on the left with VREX_PX; a manufacturer-numbered footprint reverses DEXP/DEXN; fix locally, do not reverse the global convention without auditing every diode | **Confirmed — and systemic** | `cells.tsx` 20; `circuit.json`: every `<diode>` carries pin 1 = anode (35 instances on four boards); PMEG4050EP-Q.pdf Table 2 "1 K cathode, 2 A anode"; the Sonnet audit of all 15 bound MPNs: every manufacturer that numbers its terminals (Nexperia, Vishay 1N4148WS) puts the cathode on pin 1, the others mark a band only; KiCad `Device:D` ("Sim.Pins 1=K 2=A") and every `Diode_SMD` footprint pad 1 = K; IPC-7351C pin 1 = cathode | the audit the review asked for was done and the GLOBAL fix taken (a per-part symbol would have left 33 diodes reversed against KiCad's footprints): the shared `Diode` cell numbers pin 1 = cathode, pin 2 = anode with the port names unchanged; ERC lock on every board (mutation: a swapped DEXP fails); the legacy `D_21` variant (already used by the TVS chips) now carries every diode; KiCad-10 netlist proof PASS; two datasheets archived (TPSMC-VR, Nexperia BZT52). **F190** |
| A16-R02 (md) | MAJOR — the new charging diode bypasses RSX but its proof uses RSX; 44–67 A initial-current screens; the PMEG 70 A is conditional; candidate: DEX anode to VREX_P/N | **Confirmed** | as html R02 | as html R02. **F191** |
| A16-R03 (md) | MAJOR — 20 ms clearing applied beyond its supporting conditions (15.65 A at 0.5 Ω, 5.74 A at 2 Ω, 1.31 A at 10 Ω, 2.46 A post-trip); restore a release qualification condition; a larger TVS is not a substitute | **Confirmed** | as html R04 | as html R04. **F193** |
| A16-R04 (md) | MAJOR — the LDO output absolute voltage credited as reverse-current qualification; restore it as a qualification dependency; no automatic replacement | **Confirmed** | as html R03 | as html R03. **F192** |
| md — LV input | 37.505 V peak reproduced for the coded pulse model; centralised suppression remains an interface requirement | **Already Fixed** / arithmetic confirmed | `design-verify.mjs` LV A.16 rows; IR-03 | none |
| md — retain | monitor resistors, /33X 1812, acquisition failure path, CAD variants, fail-closed target guards (the unbound IMCR definitions and the compile-time error are deliberate) | **Firmware Handled** / retained | `s32k396_board_cfg.h` — the placeholders are a build error by design (FW-24) | none |

## Review 3 — CSV register (A16-R01…R03 csv)

| ID | Review said | Class | Verification | Action in rev A.17 |
|---|---|---|---|---|
| A16-R01 (csv) | MAJOR — the actual charging branch excludes RSX while the proof uses I = V/2.2 and τ = 2.2·C; put limiting impedance in the actual branch or qualify it; firmware cannot limit capacitor charging current | **Confirmed** | as html R02 | as html R02 (the limiting impedance IS RSX once the anode is on the amplifier node). **F191** |
| A16-R02 (csv) | MAJOR — modulo-four count inference; timestamp from callback execution; late completion callbacks re-timestamp old data (a 900 µs-old frame stamped 800); a full-ring wrap disguises unequal generations (5/1/5 accepted); independent acquisition epoch/time, detect the servicing deadline before publish, re-acquire on ambiguity | **Confirmed** | `sdadc_ring.c`: `t_start = now_us − period_us` from the callback time; `adv = (hw − done) % 4` (a lap → adv 0 → "repeated interrupt"); `dma_within()` modulo 4; the frame stamp feeds `rslv_theta_e_at()` — a 100 µs stamp error is 24° at 10 000 rpm / 4 pp | **FW-35**: cadence-locked stamps (block k at t_origin + (k − k₀)·period; the SDADC data rate and the STM timer share one PLL), a servicing-deadline bound `cal_sd_irq_lat_max_us` before publish, self re-acquisition with an information DTC; tests for held-off interrupts, one lap, one channel lapping, all stalled, the reader preempted across a lap, wrap. **F197** |
| A16-R03 (csv) | MAJOR — rslv_theta_e_at() subtracts the positive SDADC/filter chain delay; a documented positive delay makes the represented instant earlier, requiring more extrapolation; −12°/−24° at 10 000 rpm for 25/50 µs; static zero calibration is insufficient | **Confirmed** | `resolver.c` 168 (`− p->cal_rslv_latency_us`); `gen-params.mjs` 160 and `timing.md` define it as a positive delay [0, 200] µs; `test_resolver.c` 143–144 asserts the inverted behaviour | **FW-36**: sign corrected; header, parameter description, timing.md and the T-37 HIL row made unambiguous; the test replaced by a physical-time oracle (both directions, two speeds, two delays). **F198** |

## Cross-check findings (this round's own, Opus `xcheck18`)

| Finding | Class | Action |
|---|---|---|
| The PTC current at a true 0 Ω external is 39.4–41.8 A at V_BR,min / cold (round 17 used V_BR,max: 37 A); IR-16's 35 V figure is 0.29 Ω, not 0.27 Ω; the negative fault at 0 Ω is 65 A and was never checked | **Confirmed** | rows recomputed at the corner; IR-16 → 0.05 Ω at ≤ 26 V, 0.29 Ω at 35 V, 0.22 Ω negative (where IR-41 allocates one). F193 |
| The 9 J "sheet point" was the exponential-pulse rating read as rectangular and un-derated: ≈ 6.5 J at 25 °C, 5.3 J at 85 °C — the 35 V double-event TVS row (6.2 J) no longer passes on the 20 ms bound | **Confirmed** | the single-fault bounded region still passes (1.7 J); the 35 V row back to the double-event INFO it already was for the PTC side. F193 |
| NEW: with the ECU asleep, a sustained short to 12.6–16 V leaves 1.5–3.8 W in the TVS after the PTC trips (P_TVS = V_BR·P_d/(V_S − V_BR)); the never-trip region (< 0.4 A) up to 4.2 W; T_J 137–307 °C on the sheet's pads | **Confirmed** (no reviewer had it) | computed WARN row; dfm.md island + thermal-coupling rule; QP-RX-04 step 2b release gate (12.6/14.4/16/24 V × 0–100 Ω, asleep and awake, −40/85 °C points); IR-42; VR-17; fail-safe end state documented. F193 |
| RSX (0.25 W) carries the ALM2402's 0.93 A source limit during a powered negative fault until OTF: 1.9 W = 122 % of its 5 s overload rating; no row checked RSX | **Confirmed** | RSXP/RSXN → ROHM ESR18EZPF2R20 (0.5 W anti-surge, AEC-Q200, same pad; overload 2.0 W for 5 s → 95 %); RSX row; ERC MPN lock. F194 |
| The Schottky was judged peak-vs-peak against a 140× wider pulse; Fig. 7 of the ALM2402 ends at 0.4 A, so "0.8 V at 5 A" was not a sheet value | **Confirmed** (metric) | I²t judge (7·10⁻⁴ vs 20 A²s, T_j(init) 25 °C only); text corrected. F191 |
| Gain band with the bound tolerances 1.98–2.18; the hold-current expression used 2.6 Ω of PTC not on the sheet; "1.3 Ω cold" and "7.2 V pp at the winding" had no sheet basis; the five-corner winding stack at 60 Ω is 6.48 V pp | **Confirmed** (documentation) | as html amplitude remarks. F195 |
| Stale card comments (SMCJ parts, 200 A, "clamps below 12.8 V up to 40 A") | **Confirmed** | comments rewritten with the round-18 figures |
| Firmware, self-found while fixing A16-R01 (no reviewer had them): the 1 ms task's current-loop liveness check compared the task's time with the ISR's newer entry stamp (a false "control lost"); the FW-15 recovery timer took the task's time against the fault ISR's stamp (the ≥ 1.5 ms low could be skipped — the driver would not reset); the PWM-ASC entry took a caller's time as the high-side turn-off when the PWM was already inhibited (the dead-time wait could be skipped) | **Confirmed** (same class) | all three closed on the FW-22 rule — the bridge/task read their own clock, the bridge stamps every turn-off and the fault ISR notes the hardware inhibit — each with a fail-before test and a caught mutation; FW-34 lists them. F196 |

## What stays open (numbered)

- **QP-RX-05** (release gate): the reverse rail current into ULDOEX in the three input states; **VR-33** (onsemi) may close it in writing. On fail: a blocking element in the regulator's output path — the only case in which a part is added.
- **QP-RX-04 step 2b** (release gate): the sustained-short sweep with the ECU asleep on the real layout (the island and the PTC coupling of dfm.md decide the window); **IR-42** (the OEM's short-circuit test condition and harness routing); **VR-17** (pulse data beyond 10 ms, needed again).
- **IR-16** at its new values (0.05 / 0.29 / 0.22 Ω) — trivially met by any harness at 24 V; the 35 V and negative values are allocations.
- **Datasheet archive:** the archived `SS34.pdf` is Vishay's SMC-family sheet while DB15 is drawn as SMA (the polarity convention is the same; an SMA-specific sheet for the sourced part goes in at PO); the ST BAT46ZFILM sheet could not be fetched (site blocks); its polarity was read from ST's page text (no numbered pins; band = cathode).
- **Winding amplitude at the full five-corner stack** (6.48 V pp at the IR-13 minimum): an EOL rejection, not a field failure; REXA4 30.1 k is the free margin if EOL yield ever shows it.
- **Firmware:** the completion-interrupt latency distribution on the target (T-40) confirms `cal_sd_irq_lat_max_us`; the HIL latency measurement (T-37) now carries the sign convention.

## Readiness (after this round)

| Area | Level |
|---|---|
| Power-off inspection / continuity / schematic-to-component reconciliation | READY (pin numbers follow the parts and KiCad; the KiCad-10 netlist proof passes) |
| Schematic / BOM production freeze | READY subject to the two restored release gates (QP-RX-04 2b, QP-RX-05) and VR-33; no further hardware change is expected from them unless VR-33 says the regulator conducts from OUT |
| Target-controlled motor operation | firmware FW-34…36 in the image (TI_FW_ID 0x0A0F0012 — a new EOL/HIL validation record before arming, as always); the target checklist (T-01…T-40) unchanged in nature |
| HV dyno / full-power prototype / DVT / production | per `qualification-plan.md` — unchanged by this round |

## Counts and deliverables

ERC 969 checks / 0 fail (two new locks, both mutation-tested) · verify 157 PASS / 17 WARN / 0 FAIL / 25 info (the two new WARN rows are numbered gates; one round-17 PASS reverted to the double-event INFO) · sim 23/6/0 · pin-verify 2057/2057 in the two legacy variants, KiCad 10.0.6 netlist proof PASS (686 components / 434 nets) · BOM 686 components / 236 lines, ₹72,983 / 47,483 / 45,883 / 71,383 @1k (+₹2/unit: the anti-surge RSX pair; the diode move and the pin numbers are free) · datasheets 107 · register F190–F198 · firmware 278 tests / 2534 checks / 0 failed in all three build flavours (host, −O2, ASan/UBSan), target-check 48 markers; on the pre-fix tree 98 failing checks (+2 for the PWM-ASC dead-time item), 16 mutations each caught; TI_FW_ID 0x0A0F0012; new CAL cal_sd_irq_lat_max_us 30 µs (5–45); DTC_RSLV_REACQUIRED (information) · `review-A17-disposition.csv` alongside.
