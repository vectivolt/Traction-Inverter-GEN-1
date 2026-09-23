# Review round 10 — schematic-only rechecks of `7235337` (rev A.10)

**Inputs.** Two schematic-only rechecks of `main` at `7235337` (rev A.9). Both covered connections,
component selection, interfaces and values, not firmware or vehicle readiness.

- **Review 1:** `A9_schematic_recheck.html`. It raised S9-01 (the RASCG power rating) and S9-02 (a
  stale sheet note), plus notes.
- **Review 2:** A9-S01 (the RDY input edge rate), plus confirmations and notes.

Both reviewers verified the A.9 corrections at source level and asked for them to stay:
- the V5GD reference for FLT/RDY, including RFLTC;
- the V5GD monitor divider;
- the LV feed switch;
- the ULDOEX enable;
- the QA01C-18 binding and the discharge-gate divider;
- the harness map;
- the anti-surge RFS4.

Neither asks for a topology, power-stage or sensor change, and none is made.

**Method.**
- Each claim was checked against the manufacturer data:
  - Nexperia 74LVC1G11 rev 13.1: Δt/ΔV ≤ 10 ns/V at 2.7–5.5 V;
  - ROHM ESR03 rev ESR03-IA-013E: 0.33 W at 70 °C in both resistance rows.
- The numbers were recomputed in `design-verify.mjs`.
- The ERC now has **891 checks, 0 fail**; the new locks are mutation-tested.
- design-verify **118 PASS · 14 WARN · 0 FAIL**; sim-verify **23 · 6 · 0**; pin-verify **1815/1815**.

## Verdict

Both rechecks are right on everything they raise:
- **A9-S01:** the RDY edge-rate finding reverses a round-7/8 decision. Buffering RDY had been
  rejected as over-engineering because the slow edge is state-benign. That argument still holds
  functionally, but it left the primary shutdown chain running a gate outside its datasheet
  conditions. A third 74LVC3G17-Q100 closes it for about ₹11.
- **S9-01:** RASCG gets a part that is rated for the power it carries.
- **S9-02 and the notes:** text only.

**+₹11 per unit** (SiC ₹71,080, IGBT ₹45,580).

## Review 1

| ID | Review said | Class | Verification | Action in rev A.10 |
|---|---|---|---|---|
| A.9 corrections (FLT/RDY on V5GD with RFLTC, V5GD monitor, QLVS topology and clamp, ULDOEX INH, QA01C-18, discharge divider, RFS4, ASC logic, JDIS/JCTL map) | present and correct; keep them | **Already Fixed** | Agreed. | Kept. |
| S9-01 | MODERATE — RASCG (2.2 k) has no power specification; it dissipates 76–119 mW continuously while ASC is held | **Confirmed** | (V18A − ZASC)²/R: 20.9 V into 4.8 V at −1 % gives **0.119 W**, for as long as ASC is held (minutes at speed). A 0.1 W 0603 allows 82 mW at 85 °C. | RASCG = **ROHM ESR03EZPF2201**: 0.33 W at 70 °C → **0.27 W at 85 °C**, 2.3× margin. The 2.2 k is kept because it sets the ASC break-before-make RC with CASCD. Computed row; MPN locked. +₹0.7. |
| S9-02 | MINOR — the discharge sheet still says "passive 67.5k bleeder (58 s)" | **Confirmed** (documentation) | The network is 2 × 6 × 22 k = 66 k. | The sheet heading now reads "passive 66k bleeder (56 s nom / 65 s worst)". |
| Note | the divider does not guarantee a hard 18.0 V ceiling (18.17 V ideal, 18.22 V at 1 %) | **Confirmed** (wording) | Agreed. | "13.3–18.2 V with 1 % resistors — a divider, not a clamp", in the model, board comment, parts-db and notes. |
| Note | use the final harness map, not the interim pin-39 wording | **Confirmed** (documentation) | The A9 disposition's A8-01 row still quoted pin 39. | Row corrected to the final map (V5GD on pin 1, HW_ID 2, VBAT_H 19/20, VBAT_L 39/40). |
| Note | row-by-row numbering puts VBAT_H across from VBAT_L | **Improvement Recommended** (documentation) | Both come from VBSW; a bridge only parallels the two polyfuses. | Noted in the HARNESS40 comment; no remap. |
| UMCU symbolic, JIC/JICC class | still to bind before a manufacturing netlist | **Already Fixed** (tracked gates A6-R12, R9X-02) | — | Unchanged. |

## Review 2

| ID | Review said | Class | Verification | Action in rev A.10 |
|---|---|---|---|---|
| A9-S01 | MAJOR (margin) — RDY_HS/RDY_LS still drive the 74LVC1G11-Q100 inputs directly through 5.1 k pull-ups; the gate allows 10 ns/V | **Confirmed** — reverses the round-8 "rejected as over-engineering" entry | The Nexperia 74LVC1G11 table gives Δt/ΔV ≤ 10 ns/V at 2.7–5.5 V; the "Schmitt action" wording does not waive it. 5.1 k into ≥ 10 pF of harness and driver capacitance gives ≥ 21 ns/V. The earlier argument still holds functionally: the edge only returns an AND input to its permissive level, at boot while MCU_GATE_EN is low, and in running behind the EN deglitch. But it left the primary shutdown chain outside its datasheet. | **USCH3** (74LVC3G17-Q100): 1A = RDY_HS → UAND1.C, 2A = RDY_LS → UAND2.B, channel 3 tied low, 100 nF local. **RRDB** 100 k: a dead USCH3 reads RDY_HS low, so DRV_EN goes low, as RSCH and RFCB do for their buffers. The MCU keeps reading the raw RDY lines. FW-16 steps d/e now cover both USCH3 channels. ERC by pin number, plus "no raw RDY line on an AND input". ≈ ₹10.6. |
| V5GD pull-up correction (with RFLTC) | right; keep it | **Already Fixed** | — | Kept. |
| V5GD-off hover via the module-NTC clamps | not new; already a tracked item | **Already Fixed** (tracked: R9X-06 bench item, PTB5 detection) | — | Unchanged. |
| LV feed switch, CLVSM 50 V | correctly connected | **Already Fixed** | — | Kept; inrush stays a measurement. |
| QA01C-18 and the discharge divider | keep; a divider, not a clamp | **Already Fixed** / wording | — | See review 1, note 1. |
| Harness: VBAT_H across from VBAT_L | not a signal hazard, but not proof of independent fusing | **Improvement Recommended** (documentation) | — | See review 1. |
| RFS4 ESR03 | valid; no need for 1206 | **Already Fixed** | — | Kept. |

## Hardware changes in rev A.10 (all SKUs)

| Change | Parts | ₹/unit | Finding |
|---|---|---|---|
| RDY Schmitt buffer USCH3 74LVC3G17-Q100 + CSCH3 100 nF + RRDB 100 k | +3 | +10.6 | A9-S01 |
| RASCG → ROHM ESR03EZPF2201 (2.2 k, 0.33 W) | part swap | +0.7 | S9-01 |
| **Total** | | **+11** (₹71,069 → ₹71,080 SiC; ₹45,569 → ₹45,580 IGBT) | |

**Not changed:**
- **The RDY pull-ups** stay at 5.1 k on V5GD. Stronger pull-ups would not reach 10 ns/V either.
- **UAND1/UAND2** are not replaced; no single 3-input AND gate with Schmitt inputs is available.
- **The harness is not re-mapped** for the VBAT_H/VBAT_L facing.

## Mutation test of the new lock-ins

Rebuilt with tsci in a scratch copy; the ERC must fail.

| Mutation | Caught by |
|---|---|
| UAND1.C back on the raw RDY_HS (USCH3 bypassed) | the AND1-inputs check, the USCH3 pin-number check and "no raw RDY line on an AND input" |
| RRDB removed | USCH3 check (dead-state pull-down) |
| RASCG back to a generic R0603-2k2 | round-10 MPN check, all four SKUs |

## Release gates

These are unchanged from A.9 (see `review-A9-disposition.md`), plus one addition: the no-HV
fixture also checks the RDY release waveform at USCH3 input and output, and DRV_EN, with
representative harness capacitance (A9-S01 verification).
