# Review round 9 — disposition of every finding (rev A.9)

**Inputs.** Two independent rechecks of `main` at `0da9014` (rev A.8):

- **Review 1:** *A.8 independent recheck* (`A8_independent_recheck.html`): A8-01…A8-03, plus the open
  gates A8-G01 and A8-G02.
- **Review 2:** `review_0da9014.md`: A8-N01…A8-N03, closure notes on A7-N01…N05, and the open gates.

Marine is out of scope. Both reviewers accepted the round-8 fixes at source level and asked for them
to stay:
- the ASC fault mask;
- the second Schmitt buffer;
- the buffered discharge drive and the 261 Ω LEDs;
- the sensitized boot test;
- the ground-referenced FAULT_OUT clamp;
- the atomic BOM generator.

Neither reviewer recommends a power-stage change, and none is made.

**Method.**

- Every claim was checked against the manufacturer's own document:
  - Mornsun **QA01C-18** (rev 2018.12.11-A/0, fetched and read, including its Fig. 1 envelope);
  - NSI6611 DS 1.2 absolute-maximum table;
  - FS26 fault-reaction table;
  - HIITIO HCM75S12T4K3;
  - Toshiba TLP152.
- Two Sonnet agents looked up exact parts and extracted datasheet data (resistor pulse ratings, a
  P-channel switch).
- Every number that decides a disposition was recomputed in `design-verify.mjs`.
- Every fix has a lock-in in `erc-audit.mjs` (**883 checks, 0 fail**), mutation-tested below.
- `design-verify.mjs` **116 PASS · 14 WARN · 0 FAIL**; `sim-verify.mjs` **23 PASS · 6 WARN · 0 FAIL**;
  pin-verify **1804/1804**.
- An independent Opus reviewer then attacked the implemented changes. Its 15 findings are
  dispositioned at the end, and their fixes are in every number above.

## Verdict in one paragraph

The reviews found three real things and one proof gap:
- **The ASC/discharge bias modules were modelled from the wrong datasheet.** The QA01C-18 is
  +18/−3 V; F61 in rev A.5 had read the base QA01C's +20/−4 V sheet.
- **The driver FLT/RDY lines were pulled up to the card rail.** Their absolute maximum is the
  driver's own VCC1, a separate regulator.
- **The self-test had a voltage threshold where it needed an energy limit.**
- **The proof gap:** the RFS4 stress had been justified at 16 V only.

All are fixed at almost no cost. Along the way this round also found a larger problem nobody had
reported: the power board's LV side ran on unswitched KL30, about **150 mA while the vehicle
sleeps**. One P-FET on the card now switches it with the FS26. **+₹29 per unit**, ₹28 of it that
switch and its gate network.

## Classification legend

| Class | Meaning |
|---|---|
| **Confirmed** | Real defect; fixed in rev A.9 (file and lock-in named) |
| **Already Fixed** | Already handled, or already tracked as an explicit gate; no change needed |
| **Firmware Handled** | Correctly owned by firmware; requirement in `firmware-contract.md` |
| **Not Applicable** | Does not apply to this design's envelope |
| **False Finding** | The claim, or its proposed fix, is wrong for this design |
| **Improvement Recommended** | Valid, not a defect; implemented where it cost nothing, otherwise listed |

## Review 1 — A8-01…A8-03, A8-G01…G02

| ID | Review said | Class | Verification | Action in rev A.9 |
|---|---|---|---|---|
| A8-01 | MAJOR — FLT/RDY pull-ups on V5A exceed the NSI6611 rating (≤ VCC1) by 0.2 V at the rail corners; the V5GD-absent state is undefined | **Confirmed** (round 8 had listed it as P-03, a bench item — wrong: a bench pass cannot waive an absolute maximum) | NSI6611 DS 1.2: RDY, FLT ≤ **VCC1** with no +0.3 V; IN±/RST ≤ VCC1 + 0.3 V. V5A (FS26 LDO2) and V5GD (NCV4276C) can sit 0.2 V apart. **Found while fixing it:** moving only the four pull-ups leaves RFLTC (V5A) forward-biasing the BAT46 diode-OR into the FLT lines, up to ≈ 68 mV above V5GD. | RFLTP1/2, RRDYP1/2 **and RFLTC** now pull to **V5GD**, which reaches the card on harness **pin 1**. This is the final map after R9X-02. The first draft used pin 39, whose neighbours were DGND and HW_ID only in odd/even numbering. V5GD's neighbours are now HW_ID, AGND and DGND in both numberings, so a short is detected or collapses V5GD; it can never stick a safety line. RV5GP 100 k reads an open pin as V5GD absent. A lost V5GD now reads FLT/RDY low: the latch sets, ASC is masked, the eFlexPWM fault forces PWM low and DRV_EN falls. That is SPO, and every card output into the dead domain is low. At the rail corners the IN/EN inputs sit ≤ VCC1 + 0.2 V, inside their +0.3 V rating. §6 gains a V5GD row, and §9 tells a V5GD loss from a pending DESAT. ERC by net and pin. +₹0.3. |
| A8-02 | MODERATE — the 10–15 mA LED window rests on a typical V_F tempco and an inferred LVC output resistance | **Improvement Recommended** (proof basis); no hardware change | Correct. Toshiba gives V_F limits at 25 °C and the −1.8 mV/°C tempco as typical. The guaranteed quantities hold with margin even for a pessimistic V_F: I_FLH ≤ 7.5 mA over −40…100 °C, and the 20 mA absolute maximum. | 261 Ω kept, as both reviewers advise. The model now applies a **±28 % tempco band** at every corner: 10.30 cold / 10.67 hot / 14.99 max mA. The row judges the guaranteed margins first (1.37× over I_FLH, 1.33× under the absolute maximum). The recommended window remains a T7-04 bench item. The LVC load line is labelled as a MOS-triode bound, not a supplier curve. |
| A8-03 | MAJOR — the 0.23 W RFS4 justification covers 16 V only; "a generic 0603 takes it" is not evidence | **Confirmed** | Reproduced with FS1B asserted at its 22 mA limit: **0.234 / 0.484 / 0.651 W** at 16 / 24 / 35 V. If FS1B is never released (MCU never boots, or deep fail-safe) the 16 V case continues. BACKUP_SAFETY_PATH_FS1B = 1 would repeat the pulses through RSTB. A 0.1 W 0603 is 0.08 W at 85 °C. | RFS4 = **ROHM ESR03EZPF1001** (alternative: Panasonic ERJ-PA3F1001V), selected and checked by a Sonnet agent against the manufacturer data:<br>• anti-surge, AEC-Q200 (per ROHM's product page);<br>• 0.33 W at 70 °C, so **0.27 W at 85 °C**;<br>• short-time overload 2 × U_R for 5 s, **≈ 1.3 W**.<br>**BACKUP_SAFETY_PATH_FS1B = 0** (FW-12): a short-to-high becomes a DTC, not an MCU-reset loop, and the MCU's §6 control is not interrupted. Computed design-verify row; MPN locked. +₹0.7. |
| A8-G01 | CRITICAL gate — short-circuit survival (IGBT 10.1 µs at 100 mA soft-off vs the 6 µs point; SiC unpublished) | **Already Fixed** (tracked release gate) | Reproduced; S9 reports WARN. | Unchanged. The no-HV driver fixture comes first, then contained SC tests. Blanking is not reduced to turn the row green. |
| A8-G02 | CRITICAL gate — "keep connected" is a request, not independence; a BMS opening can be dependent; a cold/full pack is a normal condition | **Confirmed** (the wording) → **Firmware Handled** (release rule) | Correct: the contract called an early BMS opening a "double fault". | New §6 **motor/vehicle release rule**. A motor is released only if one of these holds: (a) E_LL,pk(n_max) < U_N (SPO energy-safe without the battery); (b) HIL/dyno proves the contactors stay closed through inverter fault recovery for every opening cause, the inverter's own faults included, with charge acceptance down to n_x; (c) for the LV-loss rows, the HV backup-bias option. Zero BOM; no brake chopper by default. |
| Closure register (R7-01…R7-07) | accepted at source level; fixture/vendor gates remain | **Already Fixed** | — | Kept as they are. |

## Review 2 — A8-N01…A8-N03 and closure notes

| ID | Review said | Class | Verification | Action in rev A.9 |
|---|---|---|---|---|
| A8-N01 | MAJOR — the BOM orders QA01C-18 (+18/−3 V) but describes and models the QA01C (+20/−4 V) | **Confirmed** (datasheet binding; F61 reopened) | Mornsun QA01C-18 DS 2018.12.11-A/0, p.1: **+18/−3 V**, ±100 mA. Fig. 1 +18 V envelope: max +9 → +2 %, min 0 → −7 % over 10–100 % load. Line ±1.3 %/%; tempco ±0.03 %/°C. The archived `QA01C.pdf` is the base part. | Envelope modelled at our 2–8 % loads with V15 ±3 %: **16.9–20.9 V** (the max line is extrapolated below 10 %). ASC entry **7.07 → 7.52 µs**, and the FW-06 end-point moves 905 → **906 V** (8XX; 4XX 542 V), still ≤ 91 % of U_N. Descriptions, EXTRACTED-PARAMS §13, design-basis and contract corrected; MPN locked. Zero BOM; the part is kept. |
| A8-N02 | MAJOR — the QDIS gate has no demonstrated upper bound (a 24 V supply gives 23.6 V > +22 V) | **Confirmed** (margin). The 24 V case itself came from A8-N01's wrong sheet. | With the real envelope the gate saw up to 20.9 V through 47 Ω. That is inside the +22 V absolute maximum but above the +18 V recommended level, and relies on extrapolation below 10 % load. | **RQDG 47 Ω → 1.5 kΩ** against RQDPD 10 k: **V_GS 13.4–18.1 V**, 3.9 V under the absolute maximum. 1.5 k × 1.2 nF C_iss ≈ 2 µs, which is irrelevant for a discharge switch. No zener behind 47 Ω (the reviewer's warning). Zero BOM. Bench: V18Q, QDVO and V_GS at start-up, no load and ON. |
| A8-N03 | MAJOR — "below 60 V" is not an energy limit for ASC-actuating self-tests | **Confirmed** → **Firmware Handled** | Reproduced: 0.62 J (8XX) / 1.54 J (4XX) at 59 V and C_max. | FW-16 eligibility:<br>• both V_DC channels < **12 V** read (≤ 13 V true): **≤ 30 mJ / ≤ 75 mJ**, inside a 0.1 J design limit;<br>• a QDIS top-up from ≤ 60 V first (≤ 1.4 s);<br>• n_ss chosen so that E_LL,pk(n_ss) ≤ 13 V;<br>• the precharge handshake kept.<br>Computed rows per bus class. Zero BOM. |
| A7-N01…N05 closure | closed at source/requirement level | **Already Fixed** | — | Kept. |
| A7-N05 note | per-file rename is not a whole-set transaction | **Improvement Recommended** → implemented | Correct. | Two-phase publish: every temp file is written first, then all are renamed. A true multi-file transaction is not needed for a repository generator. |
| Stored-pass note | a stored pass does not establish latent-fault coverage after every reset | **Firmware Handled** | With the QDIS top-up, FW-16 now runs at every key-on with the contactors open. The stored pass covers only mid-drive MCU resets, inside the key-cycle diagnostic interval. | Stated in FW-16. |
| V5A/V5GD gate | a bench result cannot waive an absolute maximum | = **A8-01** | — | Resolved in hardware, not by bench. |
| Battery-opening gate | not automatically a double fault | = **A8-G02** | — | Release rule. |
| QDIS failed short | resistor-string containment | **Already Fixed** (tracked gate) | — | Fusible/flameproof qualification stays a gate. |
| Physical design / firmware | symbolic MCU binding, executable firmware | **Already Fixed** (tracked: A6-R12/R13) | — | Unchanged. |
| OVP arithmetic | coherent under its assumptions | Agreed | — | A8-N01's entry change is carried in (906 V). No comparator or chopper added. |

## Found during this round

| # | Finding | Fix |
|---|---|---|
| N17 | **Parking drain, ≈ 150 mA.** VBAT_H/L came straight from the unswitched KL30 node, and the power board's whole LV side ran whenever KL30 was present, including while the vehicle slept: the V15 boost (EN tied to V12L), ULDO15, 2 × QA01C-18 (16–30 mA no-load input each), 2 × MGJ2, the V5GD LDO with six NSI6611 VCC1 and two AMC1311 LV sides, and both flyback controllers on their start feed. That drain would flatten a 12 V battery in about a week, and the FS26's own LPOFF achieved nothing. | Card-side switch on the feed: **QLVS = Diodes DMP6023LEQ-13**, found by a Sonnet agent and checked against Diodes DS39935 (P-ch −60 V, 28 mΩ at −10 V, SOT-223, AEC-Q101/PPAP; "Advance Information", so confirm production status at PO; alternative DMP6023LSS-13). It is driven by a 2N7002 from V5A: on while the FS26 is awake, MCU resets included, and ≤ 1 µA asleep. ZLVS (C15) clamps V_GS up to the 39 V TVS clamp; 47 k/100 k give −8.2 V at 12 V and −6.1 V at 9 V, with a soft ≈ 0.1 ms turn-on. The S1 start-up already begins at VDD = 0 V, so no start time is lost. +₹27.6. |
| N18 | After A8-01, a V5GD loss and a pending DESAT both read FLT low at boot. | §9: FLT and RDY low on both banks, with both V_DC channels invalid (AMC1311 LV sides on V5GD), is a supply DTC, not a DESAT. |

## Hardware changes in rev A.9 (all SKUs)

| Change | Parts | ₹/unit | Finding |
|---|---|---|---|
| FLT/RDY pull-ups and RFLTC to V5GD; one harness DGND pin → V5GD (pin 1 after the R9X-02 re-map) | 0 | 0 | A8-01 |
| V5GD pull-down/monitor RV5GP + RV5GS (2 × 47 k) into PTB5 | +2 | +0.6 | A8-01, R9X-01/06 |
| RQDG 47 Ω → 1.5 kΩ (discharge gate divider) | value | 0 | A8-N02 |
| RFS4 → ROHM ESR03EZPF1001 (anti-surge) | part swap | +0.7 | A8-03 |
| LV feed switch QLVS + QLVN + ZLVS + RLVSG (10 k) + RLVSD (4.7 k) | +5 | +27.6 | N17, R9X-10 |
| LV feed switch slew network RLVSM 1 k + CLVSM 100 nF (drain-gate) | +2 | +0.6 | R9X-04 |
| ULDOEX INH from VBATC to V5A | net | 0 | R9X-03 |
| HARNESS40 re-laid for both dual-row numberings; class MPN | map | 0 | R9X-02 |
| CASC, CQD 25 V → 50 V | rating | 0 | R9X-11 |
| **Total** | | **+29** (₹71,040 → ₹71,069 SiC; ₹45,540 → ₹45,569 IGBT) | |

**Rejected as over-engineering:**
- **A common V5A/V5GD logic rail.** It would carry a power-board fault, such as a driver isolation
  breakdown, into the card's safety logic.
- **A "V5GD good" qualifier on the ASC mask** to keep ASC through a V5GD loss. The release rule
  covers SPO energy instead.
- **A zener clamp behind 47 Ω on the QDIS gate** (continuous clamp current).
- **A 1206 RFS4.** The anti-surge 0603 covers the envelope.
- **Another resistor change on the LEDs.**
- **A separate OV comparator or a brake chopper.**

## Firmware contract changes (rev A.9)

| Requirement | Change |
|---|---|
| §1, FW-06, §4b, §4c | ASC entry ≤ 7.5 µs, end-point 906 V (QA01C-18 low end) |
| §4c | Gate-logic supply loss: pull-ups on V5GD, SPO with every input to the dead domain low (A8-01, closes R8X-10/P-03); a hovering V5GD is caught on PTB5 and forced to SPO; MCU-pad injection at power-down documented (R9X-06/13) |
| §6 | V5GD/feed-loss row; **motor/vehicle release rule** for SPO energy (A8-G02), with (c) limited to total KL30 loss (R9X-09); FW-08b wording |
| FW-07 | Both V_DC channels invalid while V5GD is outside 4.75–5.25 V: a dead V5GD reads as a false 0 V bus (R9X-01) |
| FW-15 | The DESAT goes to NVM first; an FS26 restart now clears the drivers' own latch (R9X-08) |
| FW-12 | BACKUP_SAFETY_PATH_FS1B = 0 (A8-03) |
| FW-16 | Energy-based eligibility: read < 3 V, or QDIS for 2 τ first (≤ 64 mJ worst), n_ss from E_LL,pk (A8-N03, R9X-07) |
| §9 | Step 1 powers the power board through QLVS, LPOFF only (N17); step 7 QDIS top-up and the NVM DESAT gate; V5GD loss told apart on PTB5 (N18, R9X-01); FAULT_OUT outcomes with the anti-surge RFS4, backup path 0 and the held-FS1B jump start (R9X-14) |

## Mutation test of the new lock-ins

Each mutation was rebuilt with tsci in a scratch copy, in three sets (7/7, 7/7 and 4/4 expected failures); the ERC must fail.

| Mutation | Caught by |
|---|---|
| RFLTP1 back to V5A | pull-up check and the "no FLT/RDY node on V5A" check |
| RFLTC back to V5A; RV5GP removed | pull-up check, no-V5A check and the round-8 diode-OR check |
| harness pin 39 back to DGND | V5GD-reference check. The harness-equality checks still pass, because both boards are rebuilt from the same map, which is why this dedicated check exists. |
| RQDG back to 47 Ω | discharge-divider check |
| PSQD back to "QA01C"; RFS4 back to a generic 0603 | round-9 MPN check, all four SKUs |
| FVBL fed from NRC (switch bypassed); ZLVS reversed | LV-feed-switch check, the switched-VBAT check and the "nothing but QLVS bridges NRC" check |
| a diode from V5A to V5GD on the card (cross-check R9X-05) | "no part ties V5GD to V5A" check |
| V5GD moved beside PWM_UL/PWM_VH/FLT_LS_N in the harness map (R9X-02) | both-numbering supply-pin check (names each violating neighbour) |
| QLVS gate and source labels swapped (R9X-05) | QLVS/QLVN pin-number check |
| ULDOEX INH back on VBATC (R9X-03) | ULDOEX-enable check |

## Cross-check of the implemented changes

An independent Opus reviewer re-traced the uncommitted A.9 changes against the datasheets and the
compiled netlists, rebuilt a scratch copy and mutation-tested the ERC. It found **no CRITICAL**
defect. It confirmed:
- QLVS orientation and pinout, V_GS clamping and reverse battery;
- the LPOFF behaviour of LDO2;
- the power-up order;
- the QA01C-18 envelope and the discharge divider;
- the ASC timing (break-before-make 3.94 µs against 2.71 µs);
- the RFS4 arithmetic and the FS26 backup-path behaviour;
- the FW-16 energy arithmetic.

Every finding was verified again here before acting.

| ID | Cross-check said | Class | Verification | Action |
|---|---|---|---|---|
| R9X-01 | MAJOR — a dead V5GD leaves the V_DC receivers at their +0.5 V offset: a valid "0 V bus", not "invalid" as §9 claimed | **Confirmed** | The AMC1311 LV sides run on V5GD. Unpowered, they drive nothing, and each receiver reads its healthy-zero offset. 850 V could be reported as discharged. | V5GD is read on **PTB5**: RV5GP split into 2 × 47 k. FW-07 marks both channels invalid, and the firmware forces SPO, outside 4.75–5.25 V. §9 tells a V5GD loss apart by this reading. +₹0.3. |
| R9X-02 | MAJOR — the drawn harness numbers row by row: V5GD (pin 39) faced VDC1_P, VBAT_H faced ASC_CMD/QDIS_CMD, VBAT_L faced FLT_LS_N/RDY_HS; "MICROFIT3-40" does not exist | **Confirmed** | `Header(n, 2)` puts pin k across from k+20, as Molex dual-row parts do; box/IDC headers use odd/even. My round-9 pin-39 reasoning assumed odd/even. Micro-Fit 3.0 dual row stops at 24 circuits. | HARNESS40 is re-laid so VBAT_H (19/20), VBAT_L (39/40) and V5GD (1) touch only ground or their own rail **in both numberings**. HW_ID moves to 2. The ERC checks both, and the layout picks the real family. JIC/JICC become a class MPN. Zero cost. |
| R9X-03 | MAJOR — ULDOEX had IN and INH on VBATC: ≈ 0.9 mA in LPOFF; the parking row covered only the power board | **Confirmed** | NCV4276C: ≤ 10 µA with INH low; on above 2.8 V. | **ULDOEX INH → V5A** (zero cost). The parking row now sums every unswitched load: ≤ 43 µA at 25 °C, ≤ 126 µA at 85 °C. |
| R9X-04 | MINOR — QLVS turn-on is limited only by its gate-drain charge: 9–31 A into ≈ 45 µF at every wake, VBATC dips ≈ 2 V | **Confirmed** | The 47 k drive left the slew to the Miller plateau. | **1 k + 100 nF drain-gate**: ≈ 15 V/ms, ≤ 1.1 A. Computed row. +₹0.6. |
| R9X-05 | MINOR — the ERC passed a QLVS/QLVN gate/source label swap and a V5A–V5GD diode | **Confirmed** | Reproduced by the reviewer's mutations. | QLVS/QLVN locked by pin number; no part may bridge V5GD to V5A; ULDOEX INH locked. Mutation-tested below. |
| R9X-06 | MINOR — a dead V5GD can hover at 1–4 V (module-NTC clamp diodes fed by 5.1 k pull-ups; DRV_EN/PWM through the input clamps), with FLT/RDY between thresholds | **Confirmed** (claim) → **Firmware Handled** + bench | Plausible: a dead V5GD carries little load. | The PTB5 reading catches it. The firmware forces SPO, which also removes the EN/PWM path. The "deterministic" wording is qualified. Bench: V5GD off with both paths; if the NTC path alone holds V5GD up, re-reference the NTC clamps to a zener. |
| R9X-07 | MINOR — "< 12 V read ⇒ ≤ 13 V" assumed a 1 V error; the chain is ≈ ±9 V uncalibrated | **Confirmed** → **Firmware Handled** | The MCU ADC alone is ±3.3 V of bus per reading. | FW-16 runs only if both channels read < 3 V (≤ 12 V true: ≤ 64 mJ 4XX), or after QDIS for 2 τ from any < 60 V reading (≤ 9.3 V: ≤ 38 mJ). Computed rows. |
| R9X-08 | MINOR — V5GD now switches with V5A, so an FS26 restart erases a latched DESAT; the next boot could re-arm into the same short | **Confirmed** → **Firmware Handled** | A direct consequence of N17. | FW-15 writes the DESAT to NVM first; §9 blocks automatic arming on a record from this or the previous key cycle. |
| R9X-09 | MINOR — release rule (c) cannot restore ASC for the V5GD row or a lost feed | **Confirmed** (text) | ASC is masked for V5GD; a lost feed also kills V15 and the ASC opto. | (c) limited to total KL30 loss; the V5GD/feed row needs (a) or (b). |
| R9X-10 | NOTE — the V_GS row used KL30, not NRC; "≤ 1 µA" relied on 2N7002 leakage × 100 k | **Confirmed** | NRC ≈ KL30 − 0.5 V. 2N7002 leakage has no hot limit (vendors quote up to 500 µA at 125 °C). | Row from NRC (−5.8 V at 9 V). Gate pull-up **100 k → 10 k** with 4.7 k drive: a 50 µA hot leakage makes 0.5 V, under the 1 V V_GS(th) minimum. |
| R9X-11 | NOTE — CASC/CQD are 25 V parts at up to 20.9 V; the datasheet circuit uses 100 µF and a loaded −Vo | **Improvement Recommended** → implemented | — | 50 V parts at the same price; the bias bench item covers output capacitance and −Vo loading. |
| R9X-12 | NOTE — stale texts; the fetched datasheets are not archived | **Confirmed** (text) | — | Fixed: the power.tsx CASCD comment, the UGDL description, the FW-12 wording. §11g is the A.7 history and stays as written. The QA01C-18, DMP6023LEQ and ESR03 PDFs are cited in EXTRACTED-PARAMS but not added to the repository without the owner's approval. |
| R9X-13 | NOTE — MCU pads want their pull-ups on the pad rail; V5GD outlives V5A by 1–3 ms at power-down | **Confirmed** (documented) | ≤ 0.2 V over the pad rail in normal running; ≤ 0.9 mA per line at power-down. | Documented in §4c; inside the S32K39 injection rating. |
| R9X-14 | NOTE — an FS1B held through a jump start runs RFS4 at 0.48 W for 60 s | **Confirmed** | A 22 mA FS1B limits at 24 V, its pin reads high, and ABIST holds it. | Computed row: 18 V/60 min at 65 °C fits. 24 V/60 s is a **WARN** (1.47× nameplate, element ≈ 149 °C), accepted for this triple condition: the only effect is a disconnected FAULT_OUT on an already-shorted wire. |
| R9X-15 | NOTE — a hard short on the power-board feed will likely destroy QLVS before a polyfuse trips | **Not Applicable** (accepted, documented) | Damage after a primary fault; the feed is lost either way (SPO). | Documented; no DPAK part. |

## Release gates still open (bench / vendor — not closable on paper)

1. **Short-circuit** (A8-G01/R7-05): the IGBT at the 100 mA soft-off corner; a SiC letter from
   hiitio; the NOVOSENSE I_STO distribution; contained SC tests including the ASC-held case.
2. **No-HV driver fixture:** DESAT during PWM-ASC, the V5GD-off state, and the FW-16 FLT injection
   with the pad rule.
3. **Bias and gate levels** (A8-N01/N02): PSASC/PSQD output at no load, start-up and ON; QDVO and
   QDIS V_GS; ASC entry timing at the QA01C-18 low end.
4. **Opto drive and timing** (A8-02, T7-04), at supply and temperature corners.
5. **FAULT_OUT wire faults** (A8-03) at 16/24/35 V with FS1B released and asserted, with RFS4
   temperature and drift.
6. **Self-test energy fixture** (A8-N03): a latent high-side short at ≤ 13 V in an energy-limited
   fixture.
7. **Parking drain** (N17, R9X-03): whole-inverter current with the FS26 in LPOFF, hot and cold, and
   the QLVS wake inrush (R9X-04).
7a. **V5GD-off hover** (R9X-06): V5GD with the drivers unpowered, the NTC pull-ups live and EN/PWM
    driven; confirm the PTB5 detection and forced SPO.
7b. **Harness connector** (R9X-02): choose the real 40-way dual-row family at layout, draw its true
    numbering, and keep the supply-pin neighbour check green.
8. **Motor/vehicle release** (A8-G02): the release rule's (a), (b) or (c) per motor, and the
   ASC-entry current vs the DESAT minimum.
9. **Thermal** (T7-10); **MCU and footprint binding** (A6-R12); **firmware deliverable** (A6-R13);
   DPT, EMC/LV transients, insulation, mechanical DV.
