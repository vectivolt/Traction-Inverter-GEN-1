# Review round 12 — system review of `4544715` (rev A.11)

**Inputs.** Two independent whole-system reviews of `main` at `4544715` (rev A.10), both with
executable checks, test plans and readiness matrices:

- **Review 1:** `engineering_review(3).html` + `finding_register(1).csv`, 28 findings (R1-F01…F28).
- **Review 2:** `engineering_review(2).pdf` + `findings(3).csv`, 35 findings (R2-F01…F35).

Both verdicts: the topology and the earlier corrections are valid; the design is **not** ready for HV
or motor operation until the listed gates close; no wholesale redesign, no brake chopper, no second
MCU, no capacitor-bank change. Both name the same next milestone: the MCU ball map and connector
binding (pin freeze).

**Method.**
- Every claim was checked against the source and the manufacturer data on disk or on-line
  (LEM HC5FW-900-S drawing p.2; TI ALM2402-Q1 pin table; TI UCC12050 SNVSB38D; Murata MGJ2 selection
  guide; Bourns MF-LSMF; S32K39 DS Rev. 3 Tables 38/40 and the injection limits; FS26 COUT_VREF).
- Three Sonnet agents did the part look-ups (isolated 5 V supplies with a stated reinforced working
  voltage, ≥ 33 V polyfuses, flameproof/fusible 10 W resistors); an Opus agent re-derived the five
  calculations that decide a hardware change (exciter transfer function, SPO freewheel energy, VREF
  corners, resolver-pin injection, PTC/TVS coordination).
- Every number that decides a disposition is a computed row in `design-verify.mjs`
  (**122 PASS · 17 WARN · 0 FAIL**); every fix has an ERC lock-in (mutation-tested below).

## Verdict

Of 63 findings, **12 are real and fixed** (F123–F134), five of them hardware or contract-level:

- **The SPO release rule was incomplete** (R1-F01/R2-F08). Rule (a) accepted three-phase-open on
  back-EMF alone. The winding energy that the module diodes rectify into an isolated link — 1.5·L·I²,
  61 J for the 0.35 mH screening motor at 340 A rms against 40 J of link headroom — was missing. The
  rule is now an energy inequality the commissioning engineer can evaluate from L_d/L_q, ψ_f and n_max.
- **The resolver exciter could never have worked** (R2-F04). The MFB feedback pair was drawn swapped:
  |H(10 kHz)| = 0.18, and the S32K39 SWG (≤ 2.30 V pp, Table 40) would have driven ≈ 0.7 V pp into the
  resolver, not 8 V pp. A textbook MFB (1.5 nF / 220 pF) gives 1.85 → 7.7 V pp.
- **The V_DC bias part never existed** (R1-F12, R2-F02/F03). "MGJ2D150505SC" is not a Murata order
  code and no MGJ2 is reinforced above 150 Vrms. Each channel now has a TI UCC12050 (V_IOWM 1200 Vrms /
  1697 VDC, the AMC1311's own class) behind its own 5 V LDO.
- **The Hall interface was under-drawn** (R1-F03/F04): the claimed open-wire pull-down was never in
  the schematic, and the HC5FW symbol had VCC on terminal 1, which is V_ref.
- **A resolver wire shorted to KL30 injected 23 mA into an SDADC pin** (R2-F13) against a 3 mA limit
  that is also the absolute maximum; 10 k series/bias resistors and rescaled filter caps fix it at zero
  cost.
- **The LV-entry TVS was the wrong class** (found while checking R1-F14/R2-F10): the notes described
  the classic TPSMC24CA, which conducts at a 24 V jump start and takes 3× its 400 ms capability in an
  ISO 16750-2 test B — after which the polyfuse trips, sees ≈ 30 V against its 24 V rating and latches.
  The 24 V-stand-off TPSMC24CA-VR (AEC-Q101, same pad and price — the class the archived sheet and the
  LCSC code already were) is dark at 24 V and clamps under the boost's 34 V limit; because no SMC
  datasheet rates a pulse beyond 1 ms, the OEM's test-B source resistance is a new gate.

The other 51 are already tracked gates (SC envelope, DPT, thermal, flyback, 4XX cans, motor data, pin
freeze), firmware-owned behaviour, wording, or out of the schematic's scope (application firmware,
the ISO 26262 lifecycle). Nothing in either review justifies a power-stage, capacitor or topology change.

**BOM ≈ −₹165/unit**: two ₹480 modules replaced by two ₹360 parts and two ₹32 LDOs; eight small
capacitors and three resistors added.

## Classification legend

| Class | Meaning |
|---|---|
| **Confirmed** | Real defect; fixed in rev A.11 (file and lock-in named) |
| **Already Fixed** | Already handled, or already an explicit tracked gate; no change |
| **Firmware Handled** | Correctly owned by firmware; requirement in `firmware-contract.md` |
| **Not Applicable** | Outside the schematic/BOM release, or does not apply to this envelope |
| **False Finding** | The claim, or its proposed fix, is wrong for this design |
| **Improvement Recommended** | Valid, not a defect; done where it cost nothing, otherwise listed |

## Review 1 (R1-F01…F28)

| ID | Review said | Class | Verification | Action in rev A.11 |
|---|---|---|---|---|
| R1-F01 | CRITICAL — the SPO release rule (back-EMF below the cap rating) omits the energy stored in the motor inductances; their 300 µH / 20 mΩ ODE ends at 1038 V (8XX) / 667 V (4XX) | **Confirmed** (contract) | Reproduced: 1.5·L·I² with the S6 screening motor (0.35 mH) is 61 J at 340 A rms; the 8XX headroom ½·C_min·(1000² − 850²) is 40.3 J. The back-EMF work during the decay adds to it. The A.9 rule (a) was written for the rectified-voltage question only. | §6 rule (a) now requires ½·C_min·(U_N² − V_max²) ≥ W_mag + W_emf from the dq model at the worst angle, with W_mag = ¾(L_d i_d² + L_q i_q²). Computed row (WARN): the 8XX bank covers the screening motor to 277 A rms; above that the motor is released under rule (b). No chopper, no capacitor. **F123** |
| R1-F02 | MAJOR — "BMS charge limit 0" is conflated with "contactor open" in §6; a connected pack with zero regen permission needs zero-torque field weakening, not ASC | **Confirmed** (documentation) | The row read "Battery contactor opens / BMS limit 0 → LS-ASC". With the pack connected it is a voltage source; ASC would brake for nothing. | Row split: battery path lost → LS-ASC (unchanged); BMS limit 0 with the pack connected → the healthy-row behaviour (zero torque, current control kept), FW-06 armed as the backstop. FW-08 reworded. **F134** |
| R1-F03 | MAJOR — the claimed Hall open-wire pull-down is absent from HallChain | **Confirmed** | README and design-basis L4 claimed "pull-downs give implausible zero" since A.4; the cell had none. An open OUT wire left the 3.3 nF input holding its last value. | **R⟨ph⟩B0 100 k** to AGND at each card input: an open wire or dead sensor reads 0 V, outside the HC5FW 0.2–4.8 V output range, now the FW-05 validity window. RL ≥ 10 k per DS. ERC lock. +₹0.9. **F126** |
| R1-F04 | MAJOR — the HC5FW is not a 3-pin device: 1 V_ref, 2 V_out, 3 Gnd, 4 U_C + grounded E terminals; the carrier drawing is not released | **Confirmed** (schematic) | LEM DS p.2 drawing and schematic: terminals 1 V_ref (output, "not connected"), 2 V_out, 3 Gnd, 4 U_C, E1–E4 mass pins; "connector type: none" — a PCB-mount THT device. Our symbol had VCC on pin 1. | 8-terminal symbol by DS number (V_ref open, E1–E4 to AGND); the off-board carrier drawing now derives from it. ERC by terminal number. Zero BOM. **F127** |
| R1-F05 | MODERATE — ALM2402QPWPRQ1 is 14-pin PWP; the BOM says HTSSOP16 | **Confirmed** (documentation) | TI pin table: PWP = 14-pin HTSSOP PowerPAD; the symbol was already 14 pins. | Footprint HTSSOP14-PWP; ERC asserts the BOM package against the symbol. **F131** |
| R1-F06 | CRITICAL — IGBT SC clearing has no closed bound; BOM text says "inside 6 µs" while variants.md says 10.1 µs; the 6 µs rating is at 800 V not 850 V | **Already Fixed** (tracked gate RR04/③) + **Confirmed** (stale text) | The blanking-cap description still carried the A.6 "3.1 µs … inside the 6 µs SC rating" claim that round 7 had superseded (4.8 / 10.1 µs). | Text corrected; the gate now also asks hiitio for the SC statement at 850 V and the 16.7 V gate-rail corner (computed WARN row). **F134** |
| R1-F07 | CRITICAL — the SiC module has no published SC withstand envelope | **Already Fixed** (tracked gate ③: hiitio SC letter + contained tests) | Correct; the S9 row already reports WARN for it. | Unchanged. |
| R1-F08 | CRITICAL — ASC is unavailable for several LV/driver faults; SPO must then be energy-safe; FS1B-ASC with EN low must not be credited with DESAT | **Already Fixed** | §6 already routes V5GD loss, feed loss and FLT_LS to SPO under the release rule; FW-12 already says EN-low ASC is not DESAT-covered. The energy side is R1-F01. | Unchanged (the release rule fix closes the energy side). |
| R1-F09 | CRITICAL — the fast OVP/PWM-fault route is a contract, not a bound implementation | **Already Fixed** (tracked ①, ⑫, ⑯) | Correct: pin freeze + HIL measurement are the standing gates. | Unchanged. |
| R1-F10 | MAJOR — MCU balls and SBC OTP are not bound | **Already Fixed** (tracked ①, ⑦) | Correct. | Unchanged. |
| R1-F11 | MODERATE — the ALT field mixes drop-ins with redesign candidates (M7 for a fast diode; a 3-lead TO-247 for the 4L QDIS) | **Confirmed** (documentation) | The header said "footprint-compatible second source"; US1M listed M7 (1N4007 class) for the DESAT chain and the 250 kHz aux rectifier; QDIS listed "any TO-247". | ALT redefined as "candidate — qualify before use"; M7 removed, ES1M/US1MDFQ named; QDIS alternate restricted to TO-247-4L with the same pin order. **F134** |
| R1-F12 | CRITICAL — isolation working-voltage approvals are unbound (MGJ, TLP152, transformers, Y-caps) | **Confirmed** for PS5B/PS5C; **Already Fixed** (tracked) for the rest | Murata MGJ2 guide: no 15→±5 V code exists; reinforced approval 150 Vrms. TLP152 (⑪), transformers (⑤), LEM (⑩) were already gates. QA01C-18 and the Y-caps quote a hipot only. | **UCC12050** per channel (V_IOWM 1200 Vrms / 1697 VDC) behind its own NCV4276C from V15. Barrier register added (design-basis §6a); new gate ⑳ for QA01C-18 and the Y-caps. **F125** |
| R1-F13 | MAJOR — gate-bias start-up and six-output regulation need measurement | **Already Fixed** (tracked ⑬) | Correct. | Unchanged. |
| R1-F14 | MAJOR — the 24 V PTC's interruption voltage vs jump start / clamped load dump | **Confirmed** (the coordination, not the PTC) | The Opus check went further than the finding: the notes described the classic TPSMC24CA (V_BR 22.8–25.2 V), which conducts at the 24 V/60 s jump start and, in an ISO 16750-2 test B (35 V, 400 ms) at Ri 0.5–2 Ω, absorbs 130–470 W against ≈ 110 W it can take for 400 ms; at 0.5 Ω the polyfuse trips inside the pulse (0.1–0.25 s hot), sees ≈ 30 V against V_max 24 V and stays latched at 13.5 V until a KL30 cycle. The archived datasheet was the -VR series, a different class. | **TPSMC24CA-VR** bound explicitly on all three LV entries (AEC-Q101, 24 V stand-off, V_BR 26.7–29.5 V, same pad and price — the archived sheet and the LCSC code were this class already): dark at 24 V, clamps ≤ 33 V at 10–15 A under the TPS55340's 34 V abs max. Test B: 55 W at Ri 4 Ω (covered), ≈ 100 W at 2 Ω (at the extrapolated 400 ms limit — no SMC datasheet rates a pulse beyond 1 ms), 330 W at 0.5 Ω (the polyfuse trips and sees ≈ 30 V). The OEM's Ri is gate ㉓: at ≤ 2 Ω a TVS pulse test or 35 V-rated rails (a ≥ 42 V boost input) decide. The 5 kW 5.0SMDJ24CA was evaluated and not taken (not AEC-Q101, same 1 ms rating limit). The polyfuse is kept: the same-footprint 33 V part holds 1.17 A hot against the 1.19 A worst chain. Computed rows; MPN lock. Zero cost. **F130** |
| R1-F15 | MAJOR — the 4XX capacitor is a class, not a part | **Already Fixed** (tracked ⑧) | Correct. | Unchanged. |
| R1-F16 | MAJOR — capacitor ripple/lifetime: 221 A / 260 A ideal, 11 % sharing margin | **Already Fixed** (tracked ④) | Their 0.6498·I matches our 0.65·I row; the per-can rating is continuous at 70 °C, the 340 A point is a 30 s event (185 A continuous = 49 %). Sharing is the thermal first-article item. | Unchanged. |
| R1-F17 | MAJOR — discharge resistor fail-open / post-failure withstand not proven by a class | **Already Fixed** (tracked ⑮) + **Improvement Recommended** | A Sonnet search found no catalogue 470 Ω / 10 W part with a published fail-open curve (the fusible FW / AC-CS / CWFS families stop at 100 Ω / 7 W). | Candidate bound: TE/CGS SQP10 ("will not burn or emit incandescent particles under any overload", 700 V LEV); the stuck-ON test stays the gate (㉒). |
| R1-F18 | MAJOR — discharge witnessing depends on surviving LV/reference domains | **Firmware Handled** | FW-07 already invalidates both channels on VOFS/V5GD; FW-18 lacked the consequence. | FW-18: with either witness invalid the HV state is reported unknown, never safe. **F134** |
| R1-F19 | MAJOR — 850 V max is a restricted envelope, not universal 800 V-pack support; the divider is linear to 912 V, not "hard-clipped" | **Already Fixed** + **Improvement Recommended** (documentation) | Our text says "2 V FS = 911 V, the witness never saturates" — no clipping claim. FW-03/§2 already state 500–850 V. | variants.md now says a > 850 V pack is a separate configuration (5.1 k bottoms, cans, thresholds, DPT together). |
| R1-F20 | MAJOR — resolver amplitude, load, fault current and angle integrity are not bound to a target resolver | **Confirmed** (two parts) + tracked | The amplitude finding is R2-F04 (F124); the fault current is R2-F13 (F128). The resolver itself is a commissioning input (㉑). | See those rows. |
| R1-F21 | MAJOR — loss/thermal ratings not validated at the implemented bias and cold-plate boundary | **Already Fixed** (tracked ④) | Correct; the 0.045 K/W is per position in the model. | Unchanged. |
| R1-F22 | MODERATE — generated-release equivalence is not independently established | **Improvement Recommended** (release process) | The manufacturer pin manifest is the pin-freeze gate (①). | Verifier guard added (R2-F29); otherwise unchanged. |
| R1-F23 | MINOR — variants.md says HW_ID is pin 40 | **Confirmed** (documentation) | Stale since the A.9 harness re-lay. | Pin 2. **F134** |
| R1-F24 | MODERATE — the FW-02 τ bands (0.60 / 0.71 s ± 20 %) overlap | **Confirmed** (documentation) | A 4XX bank fitted with its own discharge board reads 0.71 s, inside the 8XX band. | FW-02 demoted to a plausibility check; the kit's voltage class is proven by traceability and an EOL C/R measurement (dfm.md §4). **F134** |
| R1-F25 | MAJOR — FW-05 mixes rms and instantaneous; 1.25 × 400 = 500 A is below the 566 A crest | **Confirmed** (documentation) | Correct. | FW-05: 1.25 × √2 × I_pk,rms = ±601 A (8XX) / ±707 A (4XX) instantaneous, both polarities, in calibrated ADC codes. **F134** |
| R1-F26 | MAJOR — the traction application is not present | **Not Applicable** (separate deliverable) | The contract is the hardware-facing interface; the reviewers say the same. | Unchanged. |
| R1-F27 | MODERATE — fixed split termination is an endpoint assumption | **Improvement Recommended** | Correct. | Endpoint/DNP stuffing option noted on the part rule and the build label. **F134** |
| R1-F28 | MAJOR — automotive safety and environmental evidence is not established by component labels | **Not Applicable** to the schematic release (program work products) | Correct and already stated in README/§11. | Unchanged. |

## Review 2 (R2-F01…F35)

| ID | Review said | Class | Verification | Action in rev A.11 |
|---|---|---|---|---|
| R2-F01 | CRITICAL — the MCU is a logical signal list, not a package-bound symbol; design-basis says LQFP-176 | **Already Fixed** (tracked ①) + **Confirmed** (stale text) | Correct; the pin freeze is the standing fabrication blocker. Design-basis §8 still said LQFP-176. | Text: 289-MAPBGA, bound at the pin freeze. **F134** |
| R2-F02 | MAJOR — "MGJ2D150505SC" is not in the Murata catalogue | **Confirmed** | Murata KDC_MGJ2: inputs 5/12/15/24 V, outputs +15/−5 … +20/−5 V only. | UCC12050 (see R1-F12). **F125** |
| R2-F03 | CRITICAL — the MGJ2's reinforced approval is 150 Vrms; 2.4 kV continuous is functional | **Confirmed** | As stated in the Murata safety table. | UCC12050: V_IOWM 1200 Vrms / 1697 VDC, VDE 0884-11 reinforced. **F125** |
| R2-F04 | MAJOR — the drawn exciter filter gives \|H(10 kHz)\| = 0.178: 1.78 V pp from a 5 V pp SWG, not 8 V pp | **Confirmed** | Independent nodal solve: 0.178 exactly. Worse than stated: the S32K39 SWG is 0.39–2.30 V pp (Table 40), so the drawn network gave ≈ 0.7 V pp. Cause: the MFB feedback pair was drawn swapped (C to the summing node, R to the inverting input) — a first-order 2 kHz roll-off. Their 560 pF screen keeps the wrong topology and reaches only 0.82. | Textbook MFB: REXA4 24 k output→summing node, CEXA1 220 pF output→inverting input, CEXA2 1.5 nF to ground: f0 17.9 kHz, Q 0.70, \|H\| 1.85 (±5 % over ±10 % caps) → 7.7 V pp; the SWG register is the firmware knob; excitation monitor unchanged. ERC by topology and value. Zero BOM. **F124** |
| R2-F05 | CRITICAL — the IGBT SC point (800 V / 15 V / 175 °C) is not a guarantee at 850 V / 16.9 V | **Already Fixed** (tracked RR04) + gate wording | Correct. | Gate wording now names the 850 V / 16.7 V corner (R1-F06). |
| R2-F06 | CRITICAL — DESAT-to-current-extinction margin unproven | **Already Fixed** (tracked RR04/③) | Correct. | Unchanged. |
| R2-F07 | CRITICAL — the OV/driver-fault hardware routes are requirements, not demonstrated | **Already Fixed** (tracked ⑫/⑯) | Correct. | Unchanged. |
| R2-F08 | CRITICAL — the SPO release criterion omits the stored winding energy (60.7 J vs 40.3 J) | **Confirmed** | Same as R1-F01; their 0.35 mH figure is our S6 screening motor. | See R1-F01. **F123** |
| R2-F09 | CRITICAL — motor-dependent ASC and dead-LV safety are not closed | **Already Fixed** (tracked ⑥; §6 rules (a)–(c)) | Correct; rule (a) is now complete. | Unchanged beyond R1-F01. |
| R2-F10 | MAJOR — the 24 V PTC is not coordinated with the 39 V-clamp environment | **Confirmed** (the TVS class) | See R1-F14. | TPSMC24CA-VR bound explicitly; gate ㉓. **F130** |
| R2-F11 | MAJOR — VREF5 sits at the FS26 3.3 µF upper limit before tolerance | **Confirmed** (margin) | CSB5 2.2 + CMA1 1 + CMA2 0.1 = 3.3 µF nominal; +10 % exceeds the window. | CSB5 → 1 µF (0805 X7R 16 V, pinned in the BOM): 2.1 µF nominal, 1.2 µF at the worst minimum (−10 % tolerance, −15 %/−30 % DC bias for the 0805/0603 parts, −15 % cold, −5 % aging) and 2.8 µF at the maximum (+10 %, +5 % after reflow, +15 % over temperature). CMA1 stays 1 µF as the reservoir at the MCU VREFH pins. Computed row. Zero cost. **F129** |
| R2-F12 | MAJOR — six-domain gate-power magnetics and cross-regulation need binding | **Already Fixed** (tracked ⑤/⑬) | Correct. | Unchanged. |
| R2-F13 | MAJOR — an ESD suppressor does not clamp a continuous short-to-KL30 within the SDADC pin rating | **Confirmed** | (16 − 5.7 V) / 450 Ω = 23 mA into the pin against I_INJ 3 mA continuous (S32K39); 20 mA into the VMID buffer through 680 Ω. The resolver shares the vehicle connector with KL30. | Opus check: the 3 mA is also the absolute maximum, both legs pull up through the winding, and 4.7 k with the 2.2 nF pin cap would have made a 7 kHz corner. So RSINF/RCOSF and RSIN/RCOS → **10 k** (1.0 / 1.8 / 2.9 mA at 16 / 24 / 35 V; the VMID buffer sinks 2.7 mA), the caps rescaled to 47 pF + 100 pF differential with 22 pF common-mode on both legs (the P-only caps had converted common mode to differential): corner 47 kHz, −12° on both channels; source 20 k against Z_DIFF ≥ 215 k is a ratio-cancelled 9 %. Computed row, ERC values. Zero cost. **F128** |
| R2-F14 | MAJOR — peak capacitor ripple has modest sharing margin | **Already Fixed** (tracked ④) | See R1-F16. | Unchanged. |
| R2-F15 | MAJOR — the 4XX bank is a class specification | **Already Fixed** (tracked ⑧) | Correct. | Unchanged. |
| R2-F16 | CRITICAL — a shorted QDIS with the battery connected is 384 W / 284 W into four 10 W resistors | **Already Fixed** (tracked ⑮) + candidate | FW-19 detects it at the next precharge; containment is the fail-open resistor — the qualification gate. | TE SQP10 candidate bound (R1-F17). |
| R2-F17 | MAJOR — the discharge pulse rating must be an exact part | **Already Fixed** (tracked ⑮) + candidate | See R1-F17. | — |
| R2-F18 | MODERATE — FW-05 mixes rms and instantaneous | **Confirmed** (documentation) | See R1-F25. | **F134** |
| R2-F19 | MAJOR — FW-15 puts an NVM write ahead of the safe action | **Confirmed** (documentation) | The step read "log which bank to NVM first". The hardware chain has already forced the safe state, but the wording could be implemented as a blocking write. | Latch in retained RAM, queue the NVM write, never ahead of the §6 action. **F134** |
| R2-F20 | MAJOR — the divider is not a 920 V-normal design | **Already Fixed** + note | Correct; see R1-F19. | variants.md note. |
| R2-F21 | MAJOR — the two V_DC channels share references and supplies | **Already Fixed** | Design-basis §6 states it; FW-07 reads VOFS and V5GD and cross-checks the BMS. | Unchanged. |
| R2-F22 | MAJOR — the Hall sensor insulation needs a qualified assembly | **Already Fixed** (tracked ⑩) | Correct. | Unchanged. |
| R2-F23 | CRITICAL — no application firmware to verify | **Not Applicable** (separate deliverable) | — | Unchanged. |
| R2-F24 | MAJOR — the S6 margin assumes double update (51° → 19° at 1.5 T) | **Improvement Recommended** (documentation) | Correct; the contract did not state the assumption. | §2 now states the 0.75 T_sw assumption and the 19° single-update figure. **F134** |
| R2-F25 | MAJOR — the thermal rating is conditional on an assumed cold plate | **Already Fixed** (tracked ④) | Correct. | Unchanged. |
| R2-F26 | MODERATE — UEXD package metadata | **Confirmed** (documentation) | See R1-F05. | **F131** |
| R2-F27 | MODERATE — board NTCs drawn as headers, bought as 0603 NTCs | **Confirmed** | FW-13 calls them board NTCs; the NtcIn cell drew a 2-pin header. | On-board 0603 NTCs RTAMB/RTHS; symbol = BOM; ERC. **F132** |
| R2-F28 | MAJOR — power-board identity does not identify the capacitor assembly | **Confirmed** (process) | See R1-F24. | EOL C/R measurement + kit traceability (dfm.md §4). **F134** |
| R2-F29 | MAJOR — kicad5-verify can exit 0 on an empty page set | **Confirmed** (tooling) | Static reading confirmed: all counters start at zero. | Fails unless 4 sheets and ≥ 1500 pins were compared. **F133** |
| R2-F30 | MAJOR — generated-netlist parity is not component correctness | **Improvement Recommended** (tracked ①) | The manufacturer pin manifest is the pin-freeze deliverable. | Unchanged. |
| R2-F31 | MAJOR — CLASS parts and generic alternates are not production specifications | **Already Fixed** (tracked: CLASS = buy to the printed rating; AVL at RFQ) | The Y-cap working-voltage point is real → gate ⑳. | Gate ⑳ added. |
| R2-F32 | MAJOR — the safety lifecycle is not established by labels | **Not Applicable** (program) | See R1-F28. | Unchanged. |
| R2-F33 | MODERATE — CAN termination must be position-configurable | **Improvement Recommended** | See R1-F27. | DNP option. **F134** |
| R2-F34 | MINOR — stale HW_ID pin 40 and LQFP-176 notes | **Confirmed** (documentation) | Both found. | Both fixed. **F134** |
| R2-F35 | MAJOR — zero regen permission is not an open contactor | **Confirmed** (documentation) | See R1-F02. | **F134** |

## Hardware changes in rev A.11 (all SKUs)

| Change | Parts | ₹/unit | Finding |
|---|---|---|---|
| V_DC bias: UCC12050 ×2 (SOIC-16W) replace the two MGJ2 modules; NCV4276C ×2 from V15 with 1 µF + 10 µF each; 10 µF on each VISO | +6, −2, 2 values | −960 + 720 + 64 + 9 = **−167** | R1-F12, R2-F02/F03 |
| Resolver exciter: MFB rewired; CEXA2 300 pF → 1.5 nF, CEXA1 4.7 nF → 220 pF | values | 0 | R2-F04 |
| Hall pull-downs R⟨U,V,W⟩B0 100 k | +3 | +0.9 | R1-F03 |
| LEM HC5FW drawn with its 8 terminals | symbol | 0 | R1-F04 |
| Resolver inputs: RSINF/RCOSF and RSIN/RCOS → 10 k; CSIND 47 pF, CSINA2 100 pF, 22 pF common-mode pairs on both legs (+4) | values, +4 | +1.2 | R2-F13 |
| SWG load capacitor CEXA4 47 pF | +1 | +0.3 | R2-F04 (Opus check: Table 40 load 25–100 pF) |
| LV-entry TVS ×3: bound explicitly as TPSMC24CA-VR (24 V stand-off, AEC-Q101) — the classic part the notes described conducts at 24 V | MPN | 0 | R1-F14, R2-F10 |
| CSB5 2.2 µF → 1 µF (0805 X7R 16 V) | value | 0 | R2-F11 |
| Board NTCs on-board (RTAMB/RTHS) | symbol | 0 | R2-F27 |
| ALM2402 footprint HTSSOP14-PWP | BOM | 0 | R1-F05 |
| **Total** | | **≈ −₹165** | |

**Not changed, and why:**
- **No brake chopper, backup bias, bigger cap bank or third V_DC channel** — both reviews say the same; the SPO rule is a release condition on the motor, not a hardware addition.
- **The 24 V polyfuses stay**: with the stand-off TVS the fuse does not trip in a test B at Ri ≥ 2 Ω and sees ≤ 8 V; the 24 V exposure needs a downstream short during a jump start (V_max, not over it); the 33 V same-footprint part would sit below the 1.19 A worst-chain draw at 85 °C.
- **The discharge resistors keep their class rating** with a named candidate: no catalogue part documents fail-open at 470 Ω / 10 W, so the test remains the proof.
- **The MCU, SBC, modules, gate values, DESAT networks and current sensors** are unchanged; every review item about them is a measurement gate already on the list.

## Mutation test of the new lock-ins

Run twice: four net-level mutations in a scratch copy (rebuilt with tsci, ERC must fail) and two
part-rule mutations against the live database (restored afterwards).

| Mutation | Result | Caught by |
|---|---|---|
| Exciter back to the A.10 topology (CEXA1 to the summing node, REXA4 to the inverting input) | ERC fail | "exciter MFB closes" and the round-12 topology/value check |
| LEM drawn the old way (VCC on terminal 1 = V_ref) | ERC fail | LEM terminal-number check + the A.4 "LEM supply through JLEM" / "hall sensor wiring" checks |
| R⟨ph⟩B0 removed | ERC fail | hall pull-down check |
| CSB5 back to 2.2 µF | ERC fail | VREF5 rail value check |
| DTVSC back to the classic TPSMC24CA | ERC fail ×4 (per SKU) | round-12 parts-resolve assert |
| UEXD footprint back to HTSSOP16 | ERC fail | UEXD package assert |

Scratch build: 899 pass · 15 fail (the four net mutations trip 13 checks between them); live database
with the two rule mutations: 909 pass · 5 fail; restored: **914 pass · 0 fail**.

## Release gates

Unchanged from A.10 (see `review-A10-disposition.md`), plus: ⑳ barrier working-voltage statements for
QA01C-18 and the Y-caps; ㉑ resolver bench (8 V pp from the SWG register, harness short-to-KL30
injection at the SDADC pins); ㉒ the SQP10 candidate through the stuck-ON test; ㉓ the OEM's ISO
16750-2 test-B source resistance (below 1 Ω the rails need a 35 V rating); and the RR04 SC gate now
names the 850 V / 16.7 V corner.
