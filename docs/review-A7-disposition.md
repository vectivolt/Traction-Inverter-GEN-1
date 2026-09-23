# Review round 7 — disposition of every finding (rev A.7)

**Inputs.** Both reviews checked the Road inverter at commit `c963794`.

- **Review 1:** an *independent A.6 recheck* with ten findings, RR01…RR10. It came with
  `A6_independent_recheck.html` and `independent_checks.py`.
- **Review 2:** `review_c963794.md`, with fourteen classified findings and open items
  (A6-R01…A6-R14) and a README note.

Both reviews excluded the Marine Series. Review IDs are kept as the reviewers wrote them.

**Method.** Each claim was checked in three ways:

- **Datasheets.** Checked against the primary datasheets in `docs/datasheets/`, re-extracted for
  this round: SN74LVC1G74, Nexperia 74LVC1G11/1G32, FS26 Rev 3, NSI6611A-Q1 DS 1.2, TLP152,
  UCC28C4x, TPS55340, HCG600, AMC1311 and QA01C. Parts that would be added were first looked up
  in the manufacturers' current datasheets: 74LVC3G17-Q100, 74LVC2G17-Q100, 74LVC1G74-Q100,
  TI SN74LVC1G17/2G17-Q1 and onsemi NLV37WZ17.
- **Netlists.** Each claim was traced in the netlists.
- **Independent numbers.** Every number that decides a disposition was recomputed on its own.

Every fix is:

- locked into `erc-audit.mjs` (**845 checks, 0 fail**). The new lock-ins were
  mutation-tested: each fails when its fix is reverted.
- re-verified in `design-verify.mjs` (**105 PASS · 14 WARN · 0 FAIL**) and `sim-verify.mjs`
  (**23 PASS · 5 WARN · 0 FAIL**), for every SKU.

An independent second reviewer then cross-checked the implemented fixes adversarially. It found
one critical problem in the first draft. The draft dropped EN during ASC, and in that state the
NSI6611 does not give DESAT priority over ASC. The design was corrected before release; see
**Cross-check of the implemented fixes** at the end.

## Verdict in one paragraph

Both reviews were right about the **safety-logic interfaces** added in A.6:

- The two RC timing nodes drove non-Schmitt LVC inputs at 14,000–63,500 ns/V; those inputs are
  specified to 5–10 ns/V.
- FS1B was loaded past its guaranteed V_OL point.
- The claim that the clear one-shot also stops **runaway code** was false.
- **ASC entry had no guaranteed break-before-make.**
- The gate rail was really ≈16.9 V, not 15.6 V, because the model left out the aux diode.
- The start feed could hold the flyback's feedback node up and **lock gate power out** at a
  high KL30.
- Two 25 V capacitors sat on a node that reaches ≈33 V.

All of these are fixed at the root, and each fix is a small part or a value change:

- a Schmitt buffer;
- a delay on the low-side ASC line, with the MCU-path ordering in the PWM peripheral;
- a separate feedback-sense rectifier;
- corrected pull-ups, a sink-only FAULT_OUT and capacitor ratings.

Items that software can own reliably are specified in
[`firmware-contract.md`](firmware-contract.md), not added as hardware:

- the retrigger case: PWM fault inputs locked in the MCU, plus the FS26 watchdog;
- the over-voltage reaction budget.

The short-circuit closure at the slow corner, the discharge-resistor qualification, the MCU pin
binding and executable firmware stay **release gates**. Both reviewers accepted that they cannot
be closed on paper. This round also found **two defects neither review reported** (N10, N11).

**Cost: +₹57 per unit** (₹70,965 → ₹71,022 SiC, ₹45,465 → ₹45,522 IGBT, at 1k):

- +₹27 for the safety and gate-supply fixes;
- +₹30 for replacing the unbuildable 20-pin debug header with the real 10-pin part.

There is no power-stage change, no added supply and no comparator.

## Classification legend

| Class | Meaning |
|---|---|
| **Confirmed** | Real defect; fixed in rev A.7 (file and lock-in named) |
| **Already Fixed** | The repository already handled it, or already tracked it as an explicit gate; no change needed |
| **Firmware Handled** | Correctly owned by firmware; requirement now in `firmware-contract.md` (FW-xx) |
| **Not Applicable** | Does not apply to this design's envelope, or outside a hardware repository |
| **False Finding** | The claim, or its proposed fix, is wrong for this design |
| **Improvement Recommended** | Valid, not a defect; implemented where it cost nothing, otherwise listed |

## Review 1 — RR01…RR10

| ID | Review said | Class | Verification | Action in rev A.7 |
|---|---|---|---|---|
| RR01 | MAJOR — the clear one-shot (RLAT2 10 k / CCLR 15 nF) drives `ULAT2 /CLR` at ≈63,500 ns/V; TI allows 5 ns/V at 5 V | **Confirmed** | τ = 150 µs; the 30–70 % traversal takes 127 µs. SN74LVC1G74 §6.3: Δt/Δv ≤ 5 ns/V at 5 V, and the part has no Schmitt input (its full text never mentions one). Logically the edge is mostly benign, because a clear with PRE high cannot change /Q. The ROC violation is still four orders of magnitude, which leaves the recovery handshake undefined. | **USCH = 74LVC3G17-Q100** (Nexperia; no Δt/ΔV row in its ROC table, Grade 1). Channel 1: FLT_CLR_N → ULAT2 /CLR. With the buffer's V_T+ window (0.40–0.73 V_CC) and CCLR now C0G, the one-shot is **72–210 µs** at the buffer output. It needs ≥ 49 µs to deliver the drivers' reset edge (verification report, *Gate drive*). Lock-in: no RC node lands on an LVC input. |
| RR02 | MAJOR — the 10 k / 3.3 nF delay feeds the 74LVC1G11-Q100 at ≈14,000 ns/V; the datasheet allows 10 ns/V despite its "Schmitt action" wording | **Confirmed** | τ = 33 µs. Nexperia's Table 6 says 10 ns/V at 2.7–5.5 V, and the prose about "Schmitt action" does not waive it. The same holds for 74LVC1G74-Q100 and 74LVC1G08-Q100 (checked). | USCH channel 2: FLT_OKD → UAND2.C. The global drop is now **22–53 µs** (V_T− window 0.22–0.49 V_CC, R ±1 %, C0G ±5 %). That exceeds the faulted driver's 11.8 µs soft-off at the DS-minimum 100 mA, taken from the new 16.7 V high-corner rail. |
| RR03 | CRITICAL — the one-shot does not stop repeated clears; 40 µs low / 4 µs high keeps FLT_OKD at 4.2–4.8 V | **Confirmed** (the claim) → **Firmware Handled** | The counterexample is right, and the A.6 comment overclaimed "a stuck pin **or runaway code**". What the circuit actually does: a faulted NSI6611 keeps itself off until EN has been low ≥ 0.55–1.3 ms and rises again. Re-pulsing the clear holds DRV_EN **high**, so it never gives the driver that reset edge. What is left is the healthy switches following runaway PWM, which is the runaway-code hazard itself. | Three layers, zero BOM cost:<br>• **FW-15:** FLT_HS_N/FLT_LS_N (already MCU inputs) routed to eFlexPWM FAULT inputs in fail-safe, manual-clear mode and write-protected, so PWM is forced low in the MCU hardware while any FLT is asserted.<br>• The drivers' own latch.<br>• **FW-12:** FS26 Q&A watchdog with WD_ERR_LIMIT = 2 → FS0B. Its release needs a token-derived SPI write that runaway code cannot produce.<br>Comment and design-basis claims corrected. The reviewer's "separate driver reset from PWM permission" is met inside the MCU; no latch redesign needed. |
| RR04 | CRITICAL — S9 printed PASS on the 400 mA soft-off; at 100 mA the IGBT needs 8.9 µs vs tP ≤ 6 µs | **Confirmed** (checker criterion) | The reviewer's arithmetic reproduces exactly: detection 2.98 µs, total 4.46 / 8.92 µs. With the corrected gate rail (A6-R06), the soft-off now starts from the 16.7 V high corner: **4.77 µs at 400 mA, 10.1 µs at 100 mA**. | Both rows (design-verify, S9) are now **WARN = release gate**, showing both corners. The gate closes with the NOVOSENSE I_STO distribution, the hiitio SC envelope at 850 V and the actual gate bias, and a contained SC test with integrated energy. 82 pF is kept; blanking is not shrunk blindly. |
| RR05 | CRITICAL — ASC overrides only the low sides; DRV_EN=H + PWM_UH=H + ASC=H shoots through; the FS1B/MCU-dead path is not covered | **Confirmed** | Two gaps. (1) The MCU path had no defined entry order. (2) With FS1B_TDELAY = 0 (default), FS0B and FS1B assert together, and the HS turn-off (EN deglitch ≤ 60 ns + IGBT turn-off) raced the LS turn-on (TLP152 ≤ 170 ns + tASC_r ≥ 390 ns). **Datasheet constraint** (DS 1.2 §8.12): DESAT is honoured over ASC only with EN high and IN+ high/IN− low; with EN low the table marks it "irrelevant". | **Break-before-make without touching EN** (firmware-contract §4c):<br>• **CASCD 12 nF + DASCR** (power board): the LS ASC starts ≥ 3.4 µs after the latch sets; entry ≤ 7.0 µs, release ≤ 0.75 µs.<br>• **FS1B path:** FS0B drops DRV_EN, so the HS are off ≈0.2 µs after it; that is hardware.<br>• **MCU path:** eFlexPWM fault on the high-side outputs → `ASC_REQ` → PWM-ASC after the dead time (LS on via IN+, EN high), so LS DESAT stays active. A shorted HS then gives one bounded SC event.<br>• Exit is MCU-sequenced (FW-06a).<br>• Residual: FS1B-ASC runs with EN low. A new HS short during it is a double fault; a NOVOSENSE statement is a release gate.<br>A first draft also dropped DRV_EN from the latch (DASC). It was **removed** after the cross-check, because it switched the LS DESAT off. ₹0.7. |
| RR06 | CRITICAL — the 20 µs OVP requirement is not an implemented guarantee; filter τ 6.19 µs, 100 kS/s adds ≈10 µs | **Firmware Handled** (open validation) | The 897 / 538 V endpoints reproduce. The budget was a written number, and ASC entry was not in it. | **FW-06 rewritten as a measured budget:**<br>• filter lag 6.2 + AMC1311B 2.1 + receiver 0.3 + sample wait ≤ 5.0 (**≥ 200 kS/s** per channel, free-running) + conversion 1.0 + ADC watchdog → eFlexPWM fault → ASC_REQ 1.0 = **15.6 µs**;<br>• plus **≤ 7.0 µs** hardware ASC entry with all six switches off (phase current up to 481 A into the link);<br>• → **905 V (8XX) / 542 V (4XX)**, 90 % of the cans' 85 °C U_N.<br>Route specified; HIL event-to-ASC measurement is the gate. No comparator: the reviewer agrees it is needed only if the route misses the budget. |
| RR07 | Thermal co-heating not closed — S4 injects only the switch die | **Confirmed** (model gap) | The shared coldplate carries IGBT + diode loss. `tjPos` puts both into the plate term (loss-model.mjs), in the steady-state bounds and in the S4 transient. | S4 30 s peak: **8XX IGBT 129 °C** (was 127), **4XX IGBT 123 °C** (was 121). Steady-state bounds: 142 °C (WARN, 95 % of Tvjop, conservative for a 30 s peak); diode regen 121 °C. The coldplate Rth stays the thermal release gate. |
| RR08 | The 8 kHz SiC mode's 1.2 kHz current-loop ceiling misses 45° PM (43.3°) | **Confirmed** (contract) | S6 reproduces 51.4° / 43.3° / 47.2°. | Firmware-contract SKU table: **≤ 1.2 kHz at 10 kHz, ≤ 1.1 kHz at 8 kHz**. The ceiling is per switching mode; the parameter set carries one gain set per f_sw. |
| RR09 | The 4XX capacitor is still a class specification | **Already Fixed** (tracked procurement gate) | Already listed as the 4XX can RFQ gate. | RFQ requirement added to the part spec: the ripple rating must be stated **at 5 kHz** for the IGBT build, not inherited from 10 kHz. |
| RR10 | MAJOR — CB15O1/2 (22 µF/25 V) on V15B see the pass-through (≈38.5 V at 39 V) | **Confirmed** | V15B follows V12L − V_f in pass-through. At the design's own ≈33 V clamped load dump that is 132 % of 25 V. All other 12 V-node capacitors were already 50 V. | **22 µF/50 V 1210** (the same part as CLVC2: one line item, same pads). The boost loop was re-checked over the realistic 20–35 µF effective range: PM 72–84° (S2 keeps the PM-worst 35 µF). New regression row: CB15O at the clamped load dump, 66 %. |

**Review 1, other items**

| Item | Class | Disposition |
|---|---|---|
| SiC turn-off still predicts ≈1104 V at 850 V / 481 A with an assumed 15 nH; 13.6 nH fits the 1080 V guard | **Already Fixed** (tracked) | Already an S8 WARN and the first DPT release gate: loop inductance and RG_OFF are set from the double-pulse test. |
| MCU package/pin binding is a fabrication blocker | **Already Fixed** (tracked) | Same as A6-R12. |
| "Firmware Handled means specified, not executed" | Agreed | A6-R13. The contract now carries measurable budgets (FW-06, §4c, FW-12, FW-15), so HIL can close them. |

## Review 2 — A6-R01…A6-R14

| ID | Review said | Class | Verification | Action in rev A.7 |
|---|---|---|---|---|
| A6-R01 | CRITICAL — FS1B loading vs the 4 mA current-limit corner; ASC_SET_N 1.667 V > VIL; the checker used 22 mA and divided by 1000 twice | **Confirmed** (circuit + checker) | FS26 Rev 3 Tables 196/197: I_LIM 4–22 mA; V_OL ≤ 0.4 V **only up to 2 mA**; the SBC's own read-back calls the pin low only **< 0.7 V**. The 1 k pull-ups took 5.45 mA. At a 4 mA part, FS1B sat at 1.33 V and FS0B at 1.0 V, which would also trip the SBC's own short-to-high diagnosis. | **RENP1/2 1 k → 5.1 k** (NXP's recommended value for a VDDIO pull-up; already a BOM value). FS1B at V_OL, with the strap and a specified FAULT_OUT load: 1.79 mA ≤ 2 mA; ASC_SET_N ≤ 0.84 V vs 1.35 V; FS0B 0.93 mA. **FAULT_OUT made sink-only** (DFO, a BAT46 in series with RFS4; cross-check). A wire shorted to ground, a dead VCU input or a negative spike cannot preset ASC. A KL30 short is clamped at the latch (DSET). Interface specified (contract §9): the VCU pulls up ≥ 10 k to ≤ 5 V, reads ≤ 1.2 V; a 12 V pull-up is not allowed. Checker rewritten with the min-current corner. The slower 5.1 k FS0B edge goes through USCH channel 3. |
| A6-R02 | = RR01 | **Confirmed** | — | See RR01. |
| A6-R03 | = RR02 (buffer, or obtain a vendor letter) | **Confirmed** | — | See RR02. The buffer was chosen over a vendor letter: it closes the exception for ₹10 and needs no vendor action. |
| A6-R04 | = RR04 | **Confirmed** (checker) | — | See RR04: WARN release gate at both soft-off corners. |
| A6-R05 | CRITICAL — "HS DESAT ⇒ LS-ASC permitted" is an unproven inference; three auto-retries | **Confirmed** (requirement) | FLT_HS/FLT_LS are bank reports, not a bridge-health diagnosis. | §6 FLT_HS row rewritten. The fault latch holds EN low, and the DS documents LS DESAT over ASC only with EN high. So the response is **SPO first**; after the FW-15 reset (≥ 1.5 ms), LS-ASC is permitted **only as PWM-ASC** (EN high, IN+ high). A failed-short HS then ends in LS DESAT: one bounded SC event, then SPO. The ASC latch alone is not used for this row. **FW-15: no automatic retry after DESAT** (one VCU-authorised retry per key cycle, ≥ 1 s, reduced torque; a second DESAT latches the DTC). |
| A6-R06 | MAJOR — the VCC2 equation omits the aux rectifier drop | **Confirmed** | A.6: FB on VDD, which sits behind the US1M aux diode, so the real rail was **16.9 V**, not the 15.6 V claimed. | Fixed together with A6-R07. The FB divider senses a **separate aux rectifier** (1N4148WS + 100 Ω + 100 nF per chain) with **52.3k/15k**. The equation now carries both diodes, V_FB 2.45–2.55 V, 1 % divider and zener 4.8–5.4 V. **VCC2 = 15.4 V nominal, 13.54–16.7 V corners**, inside a stated 13.5–17.0 V bias window (NSI6611 recommended min 13 V + margin; ≤ 17 V keeps the SC current near the 15 V DS data). The low corner includes the US1M drop at peak current and the FB bias (cross-check). It sits at the window edge, so the row is a **WARN until the LV bench**. The sense capacitor is a soft-termination MLCC, so a crack cannot short FB. S9/S10 and the soft-off rows use the corners. |
| A6-R07 | MAJOR — the start resistor can back-feed the regulated VDD node at high KL30 | **Confirmed** (by calculation) | With the converter stopped, VDD settles where the start feed equals I_q plus the divider current. For a 1.5 mA controller (DS: 2.3 typ, no min) on a 16 V rail that is **12.3 V**, above the 11.83 V target. At a 24 V jump start it reaches the 18 V clamp. FB then stays above 2.5 V, the controller never restarts and gate power is lost. | Separate FB-sense node (A6-R06): FFS is fed **only** by the winding and decays through the 67 k divider (τ 6.7 ms), so a stopped converter always restarts. Start threshold improves to 7.72 V (8.05 V available at 9 V). S1 is re-run without the divider on VDD: 176 / 240 ms typ/worst at 9 V, one burst. ₹1.1 per chain. |
| A6-R08 | = RR06 | **Firmware Handled** | The reviewer's own RK4 run (10.3–20.2 µs at 100 kS/s) motivated the **≥ 200 kS/s** requirement. | See RR06. |
| A6-R09 | bom-gen exits 0 with missing inputs | **Confirmed** | Code inspection agreed. | Preflight of all four inputs **before any write**: missing, empty, malformed or older than its TSX source ⇒ abort with exit 1, no file written. Mutation-tested (missing, empty). Wrong-SKU input is **not applicable**: the netlists are SKU-independent and the variant is applied inside bom-gen. Input hashes / cardinality tags: Improvement, not needed. |
| A6-R10 | JSWD source 10-pin vs BOM 20-pin | **Confirmed** | Confirmed. | **Samtec FTSH-105-01-L-DV-K** (2×5, 1.27 mm, keyed; Harwin/CNC alternates); pin map printed in the part spec. bom-gen now checks the connector contact count against the netlist. |
| A6-R11 | Stuck-on QDIS with contactors closed: 96 W/resistor (8XX); firmware cannot bound it | **Confirmed** (specification gap) | Arithmetic reproduced: 384 / 284 W total. A shorted FET ignores FW-17, and FW-19 sees it only at precharge. | No added hardware; the resistor becomes the qualified bound. The RDIS part spec now carries testable criteria: fusible/flameproof, **opens at 70–100 W within 60 s**, holds **≥ 1.2 kV DC** after opening, contained HV fault test, hot and after repeated normal discharges. An HV fuse was rejected, as in A.6: it cannot tell the 0.45 A discharge pulse from the fault. |
| A6-R12 | MCU package binding | **Already Fixed** (tracked pre-layout gate) | Acknowledged in the repository since A.6 (R-F07/N6). | Unchanged: the fabrication blocker. |
| A6-R13 | Full-system firmware evidence absent | **Already Fixed** (tracked; separate deliverable) | The repository states it. | The contract now gives HIL-closable numbers (FW-06 budget, §4c timing, FW-12 watchdog settings, FW-15 routing). |
| A6-R14 | The flyback peak check used one transformer's Lp for the bank current | **Confirmed** (checker) | Bank Lp = 10 µH/3. | 2.05 A vs the 3.03 A limit (68 %). CS-resistor power recomputed with the DCM conduction duty: 0.099 W. |
| README note | README retains older loss assumptions | **Confirmed** | Confirmed. | README sizing block regenerated from the shared loss model: 329 W conduction, 225 W switching, 554/200 W → 122/90 °C, and so on. |

## New findings from this round (neither review reported them)

| # | Finding | Fix |
|---|---|---|
| N10 | **ASC opto under-driven.** RASCL 470 Ω gives the TLP152 LED 5.4–7.0 mA at the V5A/V_F corners. The TLP152's guaranteed turn-on current I_FLH is up to **7.5 mA**, and its recommended I_F is 10–15 mA. A worst-case unit might never assert ASC. | **270 Ω**: 10.6–13.5 mA, 71 % of the turn-on margin used. Zero cost. |
| N11 | **Latches not automotive-qualified.** ULAT/ULAT2 were TI SN74LVC1G74DCUR, a catalog part with no AEC-Q100 variant. The rest of the safety logic is Q100. | **Nexperia 74LVC1G74DC-Q100**: pin-identical in VSSOP-8, Grade 1, same price. The TI part stays as the catalog alternate. |

Checked system-wide and left as is (documented in the verification report, *Gate drive*):

- **Open-drain release edges** (ASC_SET_N ≈32 ns/V, FLT_CMB_N ≈64 ns/V, RDY ≈0.2 µs/V) still
  exceed the LVC Δt/ΔV. Each only returns an input to its idle level, with no output change:
  - a preset released while clear is high holds the latch;
  - RDY and FS0B release while MCU_GATE_EN is low (§9 arming order, FW-14).
- **FS1B_TDELAY.** The FS26 could separate FS0B and FS1B, but only in ≥ 5 ms steps. The hardware
  break-before-make lets it stay 0.

## Hardware changes in rev A.7 (all SKUs)

| Change | Parts | ₹/unit | Finding |
|---|---|---|---|
| Schmitt buffer 74LVC3G17-Q100 (+100 nF) on the clear one-shot, the soft-off delay and FS0B | +2 | +10.2 | RR01/RR02, A6-R02/R03/R01 |
| 100 k dead-buffer pull-down on FS0B_B | +1 | +0.3 | cross-check 4 |
| DRV_EN and ASC_CMD read-backs (10 k each to MCU inputs) | +2 | +0.6 | cross-check 4, pre-existing 3 |
| FS0B/FS1B pull-ups 1 k → 5.1 k | value swaps | 0 | A6-R01 |
| FAULT_OUT sink-only (DFO) + latch-preset clamp (DSET), both BAT46 | +2 | +3.0 | A6-R01, cross-check 2 |
| CASCD 12 nF C0G + DASCR 1N4148WS on the LS ASC line | +2 | +0.7 | RR05 |
| 1 k series resistor at each LS ASC pin | +3 | +0.9 | cross-check 5 |
| RASCL 470 → 270 Ω | value swap | 0 | N10 |
| ULAT/ULAT2 → Nexperia 74LVC1G74DC-Q100 | part swap | 0 | N11 |
| CCLR X7R → C0G | spec | +0.2 | cross-check 10 |
| Flyback FB-sense rectifier (1N4148WS + 100 Ω + soft-termination 100 nF) and RF⟨HL⟩FB1 56 k → 52.3 k, ×2 chains | +6 | +3.2 | A6-R06/R07, cross-check 6c |
| CB15O1/2 25 V → 50 V | value swaps | +8 | RR10 |
| JSWD → Samtec FTSH-105-01-L-DV-K (10-pin 1.27 mm) | part correction | +30 | A6-R10 |
| **Total** | | **+57** (₹70,965 → ₹71,022 SiC; ₹45,465 → ₹45,522 IGBT) | |

**Rejected as over-engineering:**

- The cross-check's alternative to DASC: a FET that splits the high-side EN. It only adds cover
  for firmware driving a high side during ASC, and there DESAT (with EN high) and the drivers'
  IN− interlock already bound the event.
- A fault-dominant or dual-path latch redesign for RR03.
- Buffering all eight slow open-drain nets: they are state-benign.
- A hardware OV comparator (RR06), pending HIL.
- High-side ASC drivers.
- A gate-rail OVP clamp: the flyback's full power would need a 3 W part per chain. The
  single-point faults go to the FMEDA instead.
- An HV fuse in the discharge string.
- A larger coldplate on the RR07 numbers.

## Firmware contract changes (rev A.7)

| Requirement | Change |
|---|---|
| FW-06 | Measured budget, ≥ 200 kS/s per channel, ADC watchdog → eFlexPWM fault → ASC_REQ route; link peak 905 / 542 V including the all-off window |
| §4c / FW-06a (new) | ASC entry without touching EN. MCU path: high sides off (eFlexPWM fault, HS outputs only) → ASC_REQ → **PWM-ASC with EN high** after the dead time (LS DESAT stays documented). FS1B path: FS0B + CASCD. Exit: ASC_CLR while PWM-ASC holds, then the PWM transition. |
| FW-12 | WD_ERR_LIMIT = 2; keep BACKUP_SAFETY_PATH_FS0B = 1; FS_GPIO1 low until §9 step 6; FS1B-ASC only where the motor needs it |
| FW-15 | FLT on eFlexPWM fault inputs (fail-safe, locked); **no automatic retry after DESAT** (A6-R05); what the one-shot does and does not bound (RR03) |
| FW-16 | Boot self-test through the DRV_EN / ASC_CMD read-backs, with gate power off |
| §6 | FLT_HS row: SPO first, then PWM-ASC only after the FW-15 reset. MCU-hang row: FS1B-ASC residual stated. |
| §9 | Boot order: FS1B is asserted at every POR, so first make resolver and V_DC valid, then release FS0B/FS1B, then decide on the ASC latch from speed, then gate power, then arm. FAULT_OUT is sink-only with the VCU pull-up. |
| SKU table | Current-loop ceiling ≤ 1.1 kHz at 8 kHz (RR08) |

## Cross-check of the implemented fixes (independent reviewer, adversarial)

The second reviewer traced every changed net across both boards and the harness. It re-derived
all numbers from the datasheets without running the repository scripts. It confirmed the pinouts,
the timing numbers and ASC entry itself, and reported the following. Every item was verified
against the datasheets before action.

| # | Finding | Class | Action |
|---|---|---|---|
| 1 | CRITICAL: the draft DASC held EN low during every ASC. NSI6611 DS §8.12: with EN low and ASC high, DESAT is "X" (irrelevant). So a high side failing short during a commanded ASC would not be ended by the LS DESAT. | **Confirmed** (verified in the DS table) | DASC **removed**. MCU-path ASC is PWM-ASC with EN high; FS1B path is FS0B + CASCD. §6 FLT_HS and MCU-hang rows re-dispositioned; the NOVOSENSE statement is a release gate. |
| 2 | MAJOR: with 5.1 k pull-ups, a FAULT_OUT short to ground (or a dead VCU input, or a negative spike) preset the ASC latch (1.47 V ≤ VIL). | **Confirmed** | DFO makes FAULT_OUT sink-only and DSET clamps a KL30 short; lock-in and design-verify rows added. |
| 3 | MAJOR: the boot rules cleared a reset-surviving ASC blindly. Also, FS1B is asserted at every POR (FS26 §22.11.3, verified), so the boot clear did not stick and ASC engaged when gate power came up. | **Confirmed** → **Firmware Handled** | §9 re-ordered (speed known → release FS0B/FS1B → latch decision → gate power → arm); FS_GPIO1 low until then (FW-12). |
| 4 | MINOR: an unpowered USCH has Hi-Z outputs, and FS0B-path coverage was EOL-only. | **Confirmed** | 100 k pull-down on FS0B_B; DRV_EN and ASC_CMD read-backs; FW-16 boot test. |
| 5 | MINOR: CASCD makes ASC_DRV stiff, while each LS ASC pin sits on its own Kelvin source (L·di/dt against the pin's −0.3/+6 V abs max). | **Confirmed** | 1 k at each LS ASC pin (GateDrive cell). Adds "ASC pin vs GND2 during the 850 V DPT" to the DPT gate. |
| 6 | MINOR: (a) the gate-rail low corner omitted the US1M drop at peak current and the FB bias; (b) at KL30 ≳ 23 V the start feed covers I_DD and FFS becomes a light peak detector; (c) new FB-sense single-point faults. | **Confirmed** (model) / **Improvement Recommended** | (a) Low corner 13.54 V; the window row is WARN until the bench. (b) 24 V and 33 V at full gate load added to the bench gate. (c) Soft-termination MLCC for CF⟨HL⟩FS; resistor/diode opens go to the FMEDA, the same class as A.6's "divider open". |
| 7 | MINOR: ASC exit with DASC | **Not Applicable** after item 1 | FW-06a rewritten anyway (clear while PWM-ASC holds). |
| 8 | MINOR: lock-ins that would miss a regression (FFD node, RASCG/RASCPD values, label-keyed pins, an over-broad message). | **Confirmed** | Lock-ins keyed by pin number; FFD/FVCC separation; RASCG/RASCPD values; message corrected. Mutation-tested. |
| 9 | NOTE: the FW-06 row booked the ASC entry at the regen slope. | **Confirmed** (model) | All-off window at I_pk/C: 905 / 542 V (still 90 % of U_N). |
| 10 | NOTE: CCLR X7R margin, the Nexperia flip-flop DS not archived, doc drift. | **Confirmed** | CCLR C0G (one-shot 72–210 µs). The Nexperia 74LVC1G74-Q100 Rev 7 Table 4 was read (SD=RD=L ⇒ Q = Q̅ = H, the same as TI), and its URL is recorded in EXTRACTED-PARAMS §25. Doc numbers corrected. |
| P1 | Pre-existing: FAULT_OUT had no protection; a 12 V pull-up or a KL30 short overdrove the latch input. | **Confirmed** | Covered by item 2 (DFO + DSET). NXP's 10 nF/22 nF EMC network is an EMC-phase item. |
| P2 | Pre-existing: FS1B-ASC and the fault-latch path run with EN low (LS DESAT undocumented). | **Confirmed** | §4c/§6 rewritten; FW-12 limits FS1B-ASC; NOVOSENSE statement is a release gate. |
| P3 | Pre-existing: no MCU read-back of DRV_EN or ASC_CMD. | **Confirmed** | Read-backs added (item 4). |
| P4 | Pre-existing: MCU pins that drive 5 V logic must sit on the 5 V I/O domain (the one-shot needs a ≥ 4 V step). | **Improvement Recommended** | Added to the A6-R12 pin-binding gate. |
| P5 | Pre-existing: TLP152 operating ambient is 100 °C max. | **Improvement Recommended** | Added to the N8 opto gate: confirm the power-board ambient, or use the SO6L reinforced alternative rated 125 °C. |
| P6 | Pre-existing: the flyback control loop was never analysed (S1 is start-up only). | **Improvement Recommended** | Load steps added to the gate-supply bench. |

## Release gates still open (bench / vendor — not closable on paper)

1. **Short-circuit.** IGBT at the 100 mA soft-off corner; SiC letter from hiitio; NOVOSENSE I_STO
   distribution; contained SC tests with integrated energy at the actual gate bias (RR04/A6-R04).
2. **NOVOSENSE statement on DESAT during ASC** with EN low, or with IN+ low (§8.12 marks it
   irrelevant). This decides the FS1B-ASC residual (§4c).
3. **DPT at 850 V.** Sets SiC RG_OFF and the loop inductance (≤ 13.6 nH at 3.3/6.8 Ω, the 1080 V
   guard). Also measure the ASC pin vs GND2 at each LS driver.
4. **LV-only gate-supply bench** (A6-R06/R07). Six-domain VCC2 at start, full gate activity, ASC,
   no-load and load steps, across KL30 9–16 V, 24 V and 33 V, hot and cold. This checks the
   13.5–17.0 V window (low corner 13.54 V) and the FB-sense filter (100 Ω/100 nF is the
   calibration knob).
5. **ASC entry measurement** (RR05). All six V_GS at no HV, on both entry paths (MCU HS-off →
   ASC_REQ; FS1B with the MCU halted). HS off before LS on; ≥ 3.4 µs; ≤ 7.0 µs.
6. **FW-06 on HIL** (RR06/A6-R08). Event → high sides off + ASC request ≤ 15.6 µs, low sides on ≤ 7.0 µs later. Then a dyno
   contactor opening under full regen.
7. **Discharge resistor qualification** (A6-R11) to the stated opening criteria.
8. **Thermal.** Coldplate Rth (0.045 K/W assumed), can current sharing, 30 s repeat (RR07); TLP152
   ambient (P5).
9. **MCU package binding** (A6-R12), with the 5 V domain for the safety-logic pins (P4), and the
   firmware deliverable (A6-R13), including confirmation that PTC25/PTC26 reach an eFlexPWM
   FAULT input (FW-15).
10. **4XX can RFQ** (RR09), with the ripple rating stated at 5 kHz.
11. **FMEDA** entries for the FB-sense chain (cross-check 6c); EMC, LV transients, insulation
    coordination, mechanical DV.

**Marine Series.** M8 is a frozen fork of the Road 8XX IGBT boards. It forks at **rev A.7**, so
it inherits every fix above. M10's new power stage uses the Road control card and flyback cell,
so it inherits them too.
