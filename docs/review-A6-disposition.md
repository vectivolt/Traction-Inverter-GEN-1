# Review round 6 — disposition of every finding (rev A.6)

**Inputs.** Review 1: *Independent engineering review* of commit `0e8ebbe` (55-page report +
`findings_register.csv`, 46 findings R-F01…R-F46). Review 2: a conditional assessment without
repository access (validation gates RV-01…RV-09). Review IDs are prefixed **R-** here to keep
them apart from this repository's own finding log (F1…F76 in
[`verification-report.md`](verification-report.md)).

**Method.** Every claim was checked against the primary datasheets in `docs/datasheets/`
(re-extracted for this round: NSI6611A-Q1 DS 1.2, SN74LVC1G74, UCC28C4x SLUS458I, AMC1311, C3D,
HC5FW, HCS600, HCG600) and against the netlists; the hiitio catalogue was scanned for every
part in the same D3 outline. Every
contested number was then recomputed by a **second, independent reviewer** in a separate
script (losses, overshoot, DESAT timing, flyback start-up, hold-up, discharge, ripple, sensing);
the two agree within stated assumptions. Fixes are locked into `erc-audit.mjs` and re-verified
in `design-verify.mjs` / `sim-verify.mjs` for **every SKU**.

## Verdict in one paragraph

Review 1 was substantially right about the **calculation layer**: the overshoot unit error, the
SiC conduction formula, the IGBT short-circuit time, the power envelope, the S5/S10 errors and
the start-up margin were real, and they are fixed. Two of its "confirmed defects" needed a
different answer than it proposed: making the fault latch **fault-dominant would deadlock the
gate drivers** (the NSI6611 releases FLT only on an EN rising edge), and the start-up problem was
**worse than a margin issue** (the rev A.5 values could never start at 9 V). The critical
*open validation items* (regeneration with the contactor open, ASC, motor data, firmware) are
real release gates, but they are firmware and motor-integration work: the hardware already has
the actuators and sensing they need, and they are now written down as testable requirements in
[`firmware-contract.md`](firmware-contract.md). The round found **six problems neither review
reported** (N1–N6 below), including BOM lines that would have re-created two earlier fixed
defects.

Hardware changes: **eleven**, all low-cost — **+₹31/unit** on the SiC build (₹70,934 → ₹70,965 at
1k; table at the end). No power-stage redesign, no added supplies, contactors, chopper or filters.

## Classification legend

| Class | Meaning |
|---|---|
| **Confirmed** | Real defect; fixed in rev A.6 (file and lock-in named) |
| **Already Fixed** | The repository already handled it, or already tracked it as an explicit gate; no change needed |
| **Firmware Handled** | Correctly owned by firmware; requirement now in `firmware-contract.md` (FW-xx) |
| **Not Applicable** | Does not apply to this design's envelope, or outside a hardware repository |
| **False Finding** | The claim, or its proposed fix, is wrong for this design |
| **Improvement Recommended** | Valid, not a defect; implemented where it cost nothing, otherwise listed |

## Review 1 — the 46 register findings

| ID | Review said | Class | Verification | Action in rev A.6 |
|---|---|---|---|---|
| R-F01 | CRITICAL — S8 overshoot 1000× too small | **Confirmed** (F63) | `Ln·20e12·1e-6` = 0.3 V for 15 nH·20 kA/µs; correct is 300 V. Worse: the HCS600 DS fall time (13 ns cold at 600 A, 3.3 Ω) is ≈30 kA/µs at 481 A, so **no EconoDUAL-class loop holds 1080 V at 850 V at 3.3 Ω** | S8 rebuilt on the DS fall time; RG_OFF start value 6.8 Ω, RG_ON 3.3 Ω (was 1.5/1.0 — faster than any published point); Eoff at 6.8 Ω booked in the loss model; DPT at 850 V cold/hot sets the final value (3.3–10 Ω). Module Ls requested from hiitio |
| R-F02 | MAJOR — SiC conduction understated | **Confirmed** (F64) | Exact: ½·I²·R per switch (verified numerically for several m, cosφ); old formula 2.43× low | Loss model rebuilt (`loss-model.mjs`); peak 30 s Tj 122 °C at 850 V (was "89 °C"); continuous 90 °C; 99.0 % semiconductor efficiency at the continuous point. **No hardware change** — margin still 53 K to 175 °C |
| R-F03 | CRITICAL — IGBT SC 6 µs not 10 µs | **Confirmed** (F65) | HCG600 DS Table 5: Isc 1800 A for tP ≤ 6 µs at 800 V/175 °C/15 V. 150 pF gave 4.5 µs worst detection alone | IGBT blanking **82 pF C0G**: 2.98 µs worst detection + ≈1.5 µs soft-off < 5 µs derated to 850 V/15.6 V; min blank 1.22 µs. Contained SC test is the release gate |
| R-F04 | CRITICAL — SiC SC envelope unknown | **Already Fixed** (tracked) | Rev A.5 already carried "SiC tSC unpublished — vendor letter is the gate" | Kept as a release gate; S9 now reports the worst-case reaction (3.1 µs, 6.5 µs at the DS-minimum soft-off current) |
| R-F05 | MAJOR — S9 is a timer, not a current model | **Confirmed** (F69, F74) | True. And the review's side question is real: DRV_EN drops 0.5–0.9 µs after DESAT, inside the faulted driver's soft turn-off (1–11 µs), and the NSI6611 DS does not say whether RST/EN aborts it | **10 k/3.3 nF** between latch and the AND's Schmitt input delays the global drop 12–40 µs (FS0B/MCU paths undelayed). S9 relabelled "reaction timeline", worst-case corners |
| R-F06 | CRITICAL — fault latch not fault-dominant | **Confirmed (residual), proposed fix False** (F70) | Truth table correct (PRE=CLR=L ⇒ Q̄=H). But that window is the **only** way to hand the six NSI6611s their RST/EN rising edge while FLT is still asserted — a fault-dominant latch, or raw FLT ANDed into DRV_EN, deadlocks recovery until KL30 is removed. The real weakness: a **stuck-low clear pin** held the window open indefinitely | Clear made a **hardware one-shot** (15 nF into the existing 10 k pull-up + BAT46 clamp): ≥54 µs per falling edge, re-arms itself. Recovery sequence FW-15/FW-16 |
| R-F07 | CRITICAL — MCU package not bound | **Already Fixed** (tracked) | Symbolic MCU was a declared pre-layout blocker since A.4. Also found: `parts-db` says 289-MAPBGA while `design-basis` says LQFP-176 (N6) | Stays the fabrication blocker; package choice itself added to it |
| R-F08 | CRITICAL — no traction firmware | **Not Applicable** (repo scope) | This repository is the hardware design package | Every firmware obligation written as FW-01…FW-21 in `firmware-contract.md` |
| R-F09 | MAJOR — power range vs current limits | **Confirmed** (F67) | 220/120 kW need ≥654/656 V at 340/185 A (PF 0.85, 5 % reserve); 168/91 kW at 500 V | Published P(V_dc) per SKU; firmware derates by V_dc (FW-03) |
| R-F10 | MAJOR — V_DC divider headroom at 920 V | **Not Applicable** | Product max is 850 V: worst-corner linear FS 902 V vs the 880 V OV trip (AMC1311B) | Documented: a > 880 V pack needs the 5.11 k bottom resistor (value swap) |
| R-F11 | MAJOR — V_DC channels not independent | **Confirmed** (F72) | Both receivers share the UVOF offset buffer: its failure shifts both by up to 228 V and passes the 5 % cross-check | `VOFS` routed to an MCU ADC (zero parts), FW-07; BMS pack voltage is the third witness; wording corrected |
| R-F12 | MAJOR — HVIL "comparator" not present | **Confirmed** (doc, F76) + **Firmware Handled** | The HVIL is an ADC signature read by the MCU | Wording corrected; reaction FW-09 (≤100 ms, no blind contactor opening at speed) |
| R-F13 | CRITICAL — ASC not speed/health qualified | **Firmware Handled** | Hardware has both actuators (SPO via DRV_EN, LS-ASC latch) and separate FLT_HS/FLT_LS; the choice is motor- and fault-dependent | Decision matrix §6 incl. "FLT_LS ⇒ SPO only" and the FS1B policy (FW-12); motor validation gate |
| R-F14 | MAJOR — ASC hold-up basis wrong | **Confirmed** (F74) | The VEE cap was added in parallel with VCC2; bleeder and gate loads ignored: 1.1–3.2 ms, not 15 ms; the command path dies in ≈1 ms | S10 rebuilt. Conclusion unchanged (ASC needs KL30) plus a hard motor rule: SPO at dead LV is energy-safe only if E_LL,pk(n_max) < 1000/600 V — otherwise the HV-fed backup-bias option |
| R-F15 | CRITICAL — contactor open during regen | **Firmware Handled** | Real hazard (100 kW charges 290 µF by 100 V in ≈0.26 ms). Hardware has fast V_DC sensing (AMC1311B, ≈2 µs) and ASC | FW-06 ADC hardware compare → ASC ≤100 µs, FW-08 DC-link control; no brake chopper (review V10 agrees) |
| R-F16 | MAJOR — motor parameters unbound | **Not Applicable** (motor not selected) | — | Commissioning input; §6 uses n_x from it |
| R-F17 | MAJOR — S1 is not a start-up model | **Confirmed** (F66, F74) | True; physically the rail *is* Vin-independent once running (DCM), but start-up was never modelled | S1 now has a cycle-by-cycle start-up model (ported from the independent check) |
| R-F18 | MAJOR — 9 V start margin | **Confirmed — worse than reported** (F66) | 4.7 k needs 8.47 V at the 12 V node (≈8.0–8.3 V at KL30 9 V) **and** the UCC28C40's 0.4 V UVLO hysteresis gives ≈0.15 ms bursts on 4.7 µF: **never starts at 9 V, not even typical**; 22 µF still fails the all-worst corner | **2.2 k** start, **47 µF** VDD (one burst, 75–295 ms, every corner), **18 V VDD zener** (the lower resistor would otherwise lift VDD past 18 V at a 24 V jump start with the flyback disabled — no internal clamp) |
| R-F19 | MAJOR — transformer saturation/insulation | **Already Fixed** (tracked) | Isat unpublished was already a bench item | TDK working-insulation rating added to the vendor gates |
| R-F20 | MAJOR — 800 V bank not a 4XX bank | **Not Applicable** to the 8XX product; **addressed** for the new 4XX SKUs | Correct for a 4XX product | 4XX bank: 16 × 50 µF/600 V (≥22 A) in the same can positions of the same busbar |
| R-F21 | MAJOR — hot-frequency/life evidence | **Improvement Recommended** | Worst ripple factor is 0.65 (was 0.62): 13.8 A/can vs 15.4 A at the 30 s peak | Factor corrected; hot/life + sharing = thermal test |
| R-F22 | MAJOR — discharge resistor pulse class | **Improvement Recommended** (spec) | 32 J/resistor (8XX), 28 J (4XX) vs ≥100 J class — fine, but the part must be bound | Spec: fail-open flameproof wirewound, ≥3 pulses/5 min; FW-17 repetition limit |
| R-F23 | CRITICAL — QDIS stuck ON | **Firmware Handled** + spec | 384 W (8XX) / 284 W (4XX) with the battery connected; the old "detected as a 2.2 kΩ load" claim was wrong | FW-17 contactor-state interlock + timeout, FW-19 precharge plausibility, fail-open resistor class; no added hardware |
| R-F24 | MODERATE — bleeder voltage at 920 V | **Confirmed at 850 V** (F68) | Even at 850 V the low-tolerance part sees 184 V = 92 % of 200 V | 2 × 6 × 22 k: 154 V (77 %), same discharge times |
| R-F25 | MODERATE — S5 double-counts the bleeder | **Confirmed** (F74) | 1829 Ω was already the parallel value | S5 rebuilt: 1.81 s worst (8XX), 1.70 s (4XX) |
| R-F26 | MAJOR — IGBT diode loss missing | **Confirmed** (F71) | Also m·cosφ = 0 and energies at the DS 0.51 Ω | Separate IGBT/diode dies, motoring + regen; 8XX IGBT rated at 5 kHz: 127 °C end of 30 s |
| R-F27 | MODERATE — IGBT Qg scaled linearly | **Confirmed** (minor, F71) | 3.0 µC scaled vs ≈3.4 µC real | Full 4.36 µC used; bias budget still closes (72 % of the worst-part capacity with RT 8.2 k) |
| R-F28 | MAJOR — S4 not a validated model | **Confirmed** (model, F74) + **Already Fixed** (coldplate tracked) | S4 started cold and used the wrong losses | Initialized at continuous steady state, shared losses, every SKU; coldplate Rth stays the thermal-test gate |
| R-F29 | MAJOR — geometry not verified | **Already Fixed** (layout phase) | Layout is an explicit later phase. The XM3 inductance was reused for a D3 module | Wording corrected (F76) |
| R-F30 | MAJOR — LV robustness | **Improvement Recommended** | TVS/reverse/post-regulators exist; the new VDD clamp closes one more pin | DV plan per OEM ISO 16750-2/7637-2 contract |
| R-F31 | MAJOR — resolver validation | **Firmware Handled** | GEN3-derived AFE; diagnostics are firmware | FW-10 |
| R-F32 | MAJOR — loop margin at 4–6 kHz | **Confirmed** (F74) | 10 kHz bandwidth was applied to the IGBT build | S6 per f_sw: ceilings 1.39 / 1.15 / 0.76 kHz at 10 / 8 / 5 kHz; per-SKU gains (§2) |
| R-F33 | MAJOR — ±900 A tight at 600 A rms | **Not Applicable** as stated | Our 4XX ceiling is 400 A rms: 566 A peak + ripple ≈ 620 A = 69 % of the range | A future 4XX-HP (≈560 A rms, 900 A module) needs a larger sensor — noted in `variants.md` |
| R-F34 | MODERATE — CAN termination fixed | **Improvement Recommended** | Correct for a mid-bus installation | Install note: termination DNP when not an end node |
| R-F35 | MAJOR — resolver shields to AGND | **Improvement Recommended — implemented** (F75) | Textbook: shield current does not belong in the analog reference | Shields on the connector ground (DGND), zero cost; EMC test confirms |
| R-F36 | MAJOR — checker scope overstated | **Confirmed** (doc) | The geometric check proves conversion, not datasheet truth | Wording corrected; the new value/MPN gate adds the missing layer (N1) |
| R-F37 | MODERATE — RSS labelled worst case | **Confirmed** (F76) | Correlated top string; AMC1311**B** gain ±0.2 % | Worst ±2.1 % / RSS / calibrated ±0.3 % reported separately |
| R-F38 | MAJOR — SBC OTP not released | **Already Fixed** | OTP manifest in design-basis §8a since A.4.4 | Boot readback FW-12 |
| R-F39 | MAJOR — automotive ordering codes | **Improvement Recommended** | Procurement work | PPAP/traceability at RFQ |
| R-F40 | MAJOR — RG/dead time need DPT | **Confirmed** (with R-F01) | Start values were faster than any characterized point | 3.3/6.8 Ω SiC, 1.0/1.0 Ω IGBT as SKU values; DPT matrix |
| R-F41 | MAJOR — mechanical/coolant evidence | **Already Fixed** (later phase) | — | DV plan |
| R-F42 | MAJOR — calibration/NVM | **Firmware Handled** | — | FW-20 |
| R-F43 | MAJOR — boot/update safety | **Already Fixed** (hardware) + **Firmware Handled** | DRV_EN is inhibited through reset/boot/debug by pulldowns + FS0B (structurally verified) | FW-21 |
| R-F44 | MAJOR — ASIL-capable ≠ compliant | **Already Fixed** | design-basis §10 already says "architecture capable of ASIL D; formal claim needs ISO 26262 work products" | — |
| R-F45 | MODERATE — BOM is planning data | **Already Fixed** | Labelled "planning ±25 %" | — |
| R-F46 | MAJOR — OC/OV reaction allocation | **Firmware Handled** | DESAT is SC protection (≈2.8× rated), not an operating limit | Allocation table + response times FW-05/FW-06 |

**Count (primary class):** Confirmed **20** (R-F06 with a different fix than proposed — its
proposed fix is the one **False** item) · Already Fixed **9** · Firmware Handled **6** · Not
Applicable **5** · Improvement Recommended **6** = 46. R-F12, R-F14, R-F28 and R-F43 also carry a
firmware rule or tracked gate as a secondary class.

## Review 1 — summary-level items outside the register

| Item | Class | Disposition |
|---|---|---|
| "One control platform, separate 4XX/8XX power stages" | **Improvement — implemented differently** | One power PCB *can* carry both: the cap bank and discharge are already bolt-on assemblies and the SiC and IGBT modules share the D3 footprint and pin map (no 650/750 V part exists in that outline, so 4XX runs 1200 V silicon). See [`variants.md`](variants.md). A 400 V × 220 kW product needs ≈560 A rms — a 900 A module (same pin map) plus larger current sensors and cap bank |
| Keep the S32K396; complete its binding | Agree | R-F07 |
| Start DPT at the characterized 3.3/3.3 Ω | Agree for RG_ON; RG_OFF starts at 6.8 Ω | R-F01/F40 |
| No brake chopper / contactors / sine filter by default | Agree | R-F15 |

## Review 2 — validation gates RV-01…RV-09

Review 2 had no repository access and states it found no defect; its gates map onto the above.

| Gate | Class | Where |
|---|---|---|
| RV-01 pinned implementation / BOM agreement | **Already Fixed** + strengthened | Three verification layers + the new value/MPN gate (N1); MCU package remains (R-F07) |
| RV-02 regeneration with contactor open | Firmware Handled | R-F15, FW-06/FW-08 |
| RV-03 back-EMF / ASC / demagnetization | Firmware Handled + motor gate | R-F13/F14/F16, §6 |
| RV-04 SC/DESAT coordination | Confirmed/fixed + vendor gates | R-F03/F04/F05 |
| RV-05 gate inhibition in reset/boot/brown-out | **Already Fixed** | Pulldowns, FS0B, RDY, UVLO (erc-audit) |
| RV-06 polarity / torque sign / FOC | Firmware Handled | commissioning |
| RV-07 thermal validation | Bench gate | R-F21/F28 |
| RV-08 LV / isolation / EMC / mechanical | Bench gate | R-F29/F30/F35/F41 |
| RV-09 calibration / CAN / diagnostics / update / EOL | Firmware Handled | FW-11, FW-20, FW-21 |

## New findings from this round (neither review reported them)

| # | Finding | Fix |
|---|---|---|
| N1 (F73) | BOM class MPNs had drifted from the netlist on 12 lines. RFS1–4 printed **R0603-120R**, which would re-create F40 (ASC latch at an indeterminate 2.5 V). CLVC2 printed **4.7 µF**, re-creating F52. The IGBT BOM printed the SiC value next to the IGBT MPN | `parts-db` corrected; SKU rows carry their value; `bom-gen` now **fails** on any value/MPN disagreement |
| N2 | The UCC28C40 has 0.4 V of UVLO hysteresis and no VDD clamp — the true root of R-F18, and why the obvious fix (lower start resistor) needed a clamp | 47 µF + 18 V zener (F66) |
| N3 | Bias-bank capacity was quoted as 3.87 W; that is 100 % of the typical current limit **before losses**. At worst-case parts it is 2.3 W — 87 % loaded by the IGBT build's gate charge | IGBT SKUs fit RT 8.2 k (≈308 kHz, +22 %, 72 %); SiC unchanged at 58 % |
| N4 | The recommended "fault-dominant" latch fix would deadlock recovery (NSI6611 FLT reset needs an EN rising edge) | See R-F06 |
| N5 | At the DS-minimum soft-off current (100 mA), the IGBT gate needs ≈6 µs to turn off a short circuit — the IGBT's whole 6 µs rating | Release gate: I_STO distribution from NOVOSENSE + contained SC test per silicon |
| N6 | S32K396 package: `parts-db` 289-MAPBGA vs `design-basis` LQFP-176 | Part of the R-F07 binding task |
| N7 | Found during the marine isolation audit: the HC5FW 900-S/SP1 is LEM's *reduced-insulation* (no sleeve) variant for low-voltage use — 2.5 kV/1 min, 3.6 mm creepage, 2.7 mm clearance. A bare 850 V busbar through it is short of basic insulation (≈6 mm creepage needed) | Insulating sleeve on each phase busbar through the aperture (mechanical note, ≈₹20/phase, design-basis §5); LEM confirmation is a release gate |
| N8 | Also found in that audit: both TLP152s (UQD discharge driver, UASC ASC buffer) bridge the LV side to DC−, but the base part is UL1577-only — 3750 Vrms/1 min, 5.0 mm creepage, no V_IORM/V_IOWM. The Road barrier rule (reinforced at 850 V DC working) is unproven for them, while RECOM was rejected under the same rule | Procurement gate on both BOM lines: TLP152(V4) with V_IORM ≥ 850 Vpk reinforced, else an SO6L-class reinforced opto (same function, larger footprint) |
| N9 | Found while answering "who owns regenerative braking": FW-06's ≤ 100 µs overvoltage budget was sized at 100 kW. At the SKU's full 220 kW regen with the battery path lost the link charges at ≈0.9 V/µs — a 100 µs response ends at 962 V (96 % of the cans' 85 °C U_N), and a once-per-PWM-period V_DC sample at 5 kHz (200 µs) at 1038 V; 4XX: 568 / 603 V of 600 V | FW-06 tightened to ≤ 20 µs on a free-running V_DC conversion slot (≥ 100 kHz) → 897 V / 538 V; design-verify locks it ("Regeneration, battery path lost"); dyno gate: contactor opening under full regen. Firmware only, ₹0 |

## Hardware changes in rev A.6 (all SKUs unless noted)

| Change | Parts | ₹/unit | Finding |
|---|---|---|---|
| SiC RG_ON/RG_OFF 1.5/1.0 → 3.3/6.8 Ω (IGBT SKUs 1.0/1.0 Ω) | value swaps | 0 | R-F01/F40 |
| IGBT DESAT blanking 150 → 82 pF (SKU value) | value swap | 0 | R-F03 |
| Flyback start 4.7 k → 2.2 k 1206, VDD 4.7 → 47 µF 1210 (×2) | value swaps | +17 | R-F18 |
| VDD 18 V clamp zener (×2) | +2 × BZT52-C18 | +1 | N2 |
| IGBT flyback RT 10 k → 8.2 k (SKU value) | value swap | 0 | N3 |
| Fault latch → AND delay 10 k/3.3 nF | +2 | +0.6 | R-F05 |
| Fault-latch clear one-shot 15 nF + BAT46 clamp | +2 | +1.8 | R-F06 |
| Bleeder 2 × 5 × 27 k → 2 × 6 × 22 k | +2 × 2512 | +6 | R-F24 |
| VOFS → MCU ADC | net only | 0 | R-F11 |
| SKU identity: RHWID (power) + RHWP (card), harness pin 40 | +2 | +0.6 | platform |
| Resolver shields → connector ground | net only | 0 | R-F35 |

Rejected as over-engineering: high-side ASC drivers, an HV-fed backup bias supply (motor-
dependent option, §6 rule decides), a brake chopper, duplicated V_DC receiver chains, a
hardware OV comparator (the MCU ADC compare is fast enough: FW-06), fuses in the discharge
string (a 0.45 A HV-DC fuse cannot tell the discharge pulse from a stuck-on fault).

## Release gates still open (bench / vendor — not closable on paper)

1. **DPT at 850 V/481 A, −20 °C and hot** — sets SiC RG_OFF (overshoot ≤ 1080 V); hiitio module Ls.
2. **Short-circuit** — hiitio letter for SiC; contained SC test per silicon; NOVOSENSE
   RST/EN-during-soft-off statement and I_STO distribution.
3. **Thermal** — coldplate Rth (0.045 K/W assumed), can current sharing, 30 s repeat.
4. **Gate power** — bias-bank bench at worst parts (IGBT SKU at 72 %), transformer Isat.
5. **Motor data** — n_x and E_LL at n_max decide §6 and whether the HV backup bias is needed.
6. **MCU package binding** (R-F07, N6) — still the fabrication blocker.
7. **4XX can MPN** — Faratronic RFQ (≥18 A rms, 600 V at 85 °C, same 37.5 mm can).
8. EMC, LV transients, insulation coordination, mechanical DV.
