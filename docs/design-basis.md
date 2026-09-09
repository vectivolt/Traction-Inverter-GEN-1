# Traction Inverter — Design Basis (rev A.3, 2026-09-09)

A standalone product: an economical, ASIL-D-capable **220 kW-class, 800 V** SiC traction
inverter (passenger EV / commercial traction, NOT an LEV drive). Two boards: **Power (HV)** +
**Control card (LV)**, one 40-way harness. Schematic-complete deliverable; PCB layout is a
later phase.

Class alignment: Wolfspeed CRD300DA12E-XM3 (300 kW/800 V) and TI TIDM-02014 (300 kW/800 V) are
the direct references; NXP EV-INVERTERGEN3 (S32K396) is the control architecture reference.

## 1. Ratings

| Parameter | Value | Basis |
|---|---|---|
| DC link voltage | 500–850 V (700 V nom) | 800 V-class pack (charging excursion to 850 V) |
| Peak power | 220 kW / 30 s | 2XX kW class target |
| Continuous power | 120 kW | thermal design point |
| Peak phase current | 340 Arms (480 A pk) | 220 kW at 700 V, PF 0.85, SVPWM linear |
| Continuous phase current | 185 Arms | 120 kW same point |
| DC feed current | 314 A pk / 171 A cont | P/V_dc |
| Switching frequency | 8–10 kHz | SiC module loss budget, traction standard |
| Motor | 3-phase PMSM, resolver feedback | |
| Ambient / coolant | −40…+85 °C board, 65 °C coldplate | liquid-cooled |

Max SVPWM line-line voltage at 700 V: V_ll = V_dc/√2 = 495 Vrms.
P = √3 · 495 · 340 · 0.85 ≈ 248 kW available; 220 kW rating leaves modulation/PF margin.

## 2. Power stage — SiC module selection (hiitio primary, standard EconoDUAL 3 footprint)

At 340 Arms per phase, discrete TO-247 paralleling (6–8 per switch) loses to a module on
economy and on the 12 nH-class commutation loop the references achieve. hiitio's module line
copies industry-standard footprints outright, which satisfies the second-source rule at module
level: **EconoDUAL 3** is the most second-sourced power-module outline in the industry.

**Choice: 3 × hiitio HCS600FH120D3C1 — 1200 V / 600 A SiC MOSFET half-bridge, EconoDUAL 3.**

- Footprint-compatible alternates (same outline, same bolt pattern, aux-pin layout to VERIFY
  per vendor): Infineon **FF6MR12W2M1H_B11** (SiC), Starpower **GD600HTT120C6S**-class SiC
  EconoDUAL clones, hiitio **HCS800FH120D4B3** (ED3H, 800 A) for a 300 kW stretch.
- Per-switch loading at peak (340 Arms phase): conduction I_rms ≈ 240 A vs 600 A rating;
  ≈ 175 W conduction (3 mΩ-class hot) + ≈ 80 W switching @700 V/10 kHz → ≈ 255 W/switch for
  30 s; ≈ 95 W/switch continuous. Coldplate at 65 °C, module RthJC ≈ 0.04–0.06 K/W-class ✓.
  (Verify against the HCS600 datasheet thermal impedance at DVT — module datasheet is
  quote-gated; hiitio publishes IEC 60747 test data.)
- 1200 V at 850 V max bus = 71 % utilization — the same margin the XM3/TIDM run.
- Per-module DC-side film snubber 1 µF/1200 V across DC+/DC− at the terminals; laminated
  busbar keeps the loop ≤ 15 nH (reference: XM3 busbar 5.3 nH + module 6.7 nH).
- Module NTC (2 pins) → conditioned on the power board, read by MCU.

Why not discrete TO-247 (the 30–60 kW answer): 6-parallel × 6 switches of 13 mΩ discretes
matches neither the loop inductance nor the assembly economics of one module per phase at
this current.

## 3. DC link (film) + ripple

Capacitor RMS current (2-level SVPWM): I_c,rms ≈ 0.62 · I_ph,rms → **211 Arms peak-30 s /
115 Arms continuous**.

**Choice: 16 × 20 µF 1100 V DC film (Faratronic C3D1M206KFSA382, LCSC C2840809) = 320 µF.**
- DFM rev A.2: no 40 µF/1100 V can is LCSC-stocked in any brand; 16 × Faratronic 20 µF is
  cheaper (~$3.4/can), in stock, and spreads ripple. Alt: TDK B32778G0406K000 40 µF 4-lead,
  one per two positions (DigiKey). References use 300 µF (3 × 100 µF FTCAP custom).
- Ripple capability ≥ 13 A each @10 kHz → 200 A continuous class ✓ (211 A is a 30 s peak).
- Energy at 850 V: E = ½·320 µF·850² = **115.6 J** (discharge design input).
- Voltage ripple: ΔV ≈ I_c/(8·f_sw·C_tot)-class ≈ negligible; ESL dominated — busbar item.
- No series strings → no balance dividers; the passive bleeder below is the fixed load.
- **Packaging (rev A.3): the 16 cans are a separate CAP BANK assembly** (sheet 2 of 4) — a
  laminated busbar (two copper plates + insulation film) the cans solder/bolt onto, because
  FR4 cannot carry the 340 A-class bus current. The sheet is the busbar vendor's electrical
  drawing: entry lugs, 3× module DC tab pairs, discharge-board studs, 16 can positions. The
  power PCB touches DCP/DCN only through its entry taps (Y-caps, HVIL, V_DC sense dividers).

## 4. DC-link discharge — checked (the review item)

Reference behaviour: XM3/TIDM ship a **purely passive** bleeder (9 × 68 kΩ 2512, 800→50 V in
< 60 s, 9.4 W at 800 V) and verify discharge via the bus-voltage sense. This design keeps that
network **verbatim** and adds an **active** path for the ≤2 s / ≤5 s automatic-discharge case.

Rev A.3: the whole network — bleeder + active path — lives on a **separate bolt-on discharge
board** (sheet 3 of 4), mounted directly across the cap-bank busbar studs, the XM3 pattern:
the stored energy and its bleeder are one assembly and can never be separated. Bias + command
arrive from the power board over a 4-way header (V15 / QDIS_CMD / 2× GND); the command line
keeps its default-OFF pulldown on the power board, and the sheet's title block carries the
rule "NEVER energize without this board fitted".

**Passive (always on):** 10 × standard **27 kΩ 2512 2 W** thick-film as 5-series ×
2-parallel = **67.5 kΩ net** (DFM rev A.2 — electrically the XM3 68 k network, built from
any-vendor 200 V-working parts instead of the TTI-only TE CRGP 500 V line).
- τ = 67.5 k · 320 µ = 21.6 s → 850→60 V in τ·ln(14.2) = **57.3 s** ✓ (< 60 s, matches XM3)
- At 850 V: 10.7 W total, 1.07 W per resistor (54 % of 2 W) ✓; 170 V per resistor (85 % of
  a standard 2512's 200 V working) ✓; alt: TE CRGP2512F68K (LCSC C2073426) in 3s×3p
- Rule printed on the discharge sheet: never energize without this board fitted.

**Active (commanded):** 1200 V SiC FET + 4 × series **470 Ω 10 W ceramic-cased wirewound
(SQP/RX27-class, axial)** = 1.88 kΩ (rev A.3 / F26: the 560 Ω string missed the 2 s crash
target at the R+5 %/C+10 % worst case — 2.19 s).
- τ = 1.88 k · 320 µ = 0.60 s → 850→60 V in **1.60 s nominal / 1.84 s worst-case** ✓
  (≤ 2 s OEM crash target even at tolerance corners; ECE R100 5 s with 2.7× margin)
- Peak 0.45 A / 384 W total decaying; energy 115.6 J → **28.9 J nominal / 31.8 J worst per
  resistor** (10 W ceramic wirewound single-pulse capability ≥ 100 J) ✓; 213 V per resistor ✓
- Switch: hiitio **HCM75S12T4K3** (1200 V, 75 mΩ, TO-247-4L — vendor consolidation; any
  1200 V ≥ 5 A FET fits). Peak switch dissipation 0.38²·75 mΩ ≈ 11 mW — trivial.
- Control: default-OFF — TLP152-class opto driver on a DCN-referenced isolated bias (QA01C),
  gate pulldown to DCN so a floating control line means OFF.
- Verification: MCU commands discharge and witnesses V_DC fall on **both** isolated senses
  (§6): stuck-off is detected within 200 ms (no ΔV), stuck-on is detected as a 2.2 kΩ bus
  load. Discharge command is also fired by the HVIL-open path.

## 5. Phase current sensing (torque path, ASIL D)

3 × **LEM HC5FW 900-S** open-loop hall transducers on the output busbars (the GEN3 chain,
read verbatim from SPF-91122): single 5 V ratiometric supply, no floating bias, no modulator.
Card-side conditioning per phase: supply bead + 47 nF/4.7 nF, 100 Ω + 3.3 nF input filter,
2 × OPA376-Q1 buffers, 100 Ω into the S32K396 SAR ADC.
- Why not shunts + ΣΔ modulators: the S32K396's SDADC channels are consumed by the resolver
  sin/cos + excitation monitor (GEN3 allocation), and 3 × (shunt + NSI1306 + reinforced bias
  module) costs more than 3 halls once the barrier-grade bias is priced honestly.
- ±900 A range covers the 480 A peak with OC observability to full scale ✓
- Redundancy: Ia+Ib+Ic = 0 plausibility (any single-channel fault detectable); per-switch
  DESAT is the independent second technology.

## 6. DC-link voltage sensing — two independent channels

2 × (6 × 470 kΩ series top + 6.2 kΩ bottom → 850 V ≈ 1.87 V; full-scale 2 V = **911 V**, so
the OV witness never saturates — rev A.3 / F27) into **AMC1311-class** (0–2 V input) isolated
amps, **each with its own reinforced bias module** (F4), read on different ADCs.
- 142 V and 43 mW per top resistor (1206 thin-film, 200 V rated) ✓; ±0.7 % RSS uncalibrated
- Channel 2 is the discharge witness and OV cross-check (disagreement > 5 % ⇒ fault).
- (References use AMC0386-Q1 integrated-divider parts — the divider+AMC1311 chain is the
  economical equivalent with the same reinforced barrier.)

## 7. Gate drive (per switch × 6)

**NSI6611A-Q1** (AEC-Q100, 10 A source/sink, SOIC-16W, reinforced, CMTI ≥150 V/ns): DESAT
(2 × US1M 1 kV series sense diodes + blanking C ≈ 1.5 µs to match the reference SC window),
active Miller clamp (CLAMP wired to gate), UVLO, RDY/FLT# feedback, separate RG_ON/RG_OFF
(start 1.5 Ω / 1.0 Ω for the 600 A module — tune at double-pulse), gate-source 10 k pulldown.
10 A peak drive on a ~2.5–4 µC module gate ⇒ no booster stage needed (the XM3 driver does the
same class of module directly).

Bias: the **GEN3 dual-flyback pattern, copied from EV-POWEREVBHD2**: two **UCC28C43**
current-mode flybacks (HS chain / LS chain; DFM rev A.2 — multi-source LCSC/DigiKey part
replacing the thin-distribution NJW4140, with a 2-transistor default-OFF enable clamp and a
12 V trickle-start feed), each driving **three VGT12EEM transformers with
primaries paralleled — one floating secondary per phase → six independent domains**;
per-secondary rectifier + **BZT52-C5V1 zener splitting the winding into +15 V / −5.1 V about
each Kelvin source** (the HCS600 datasheet's recommended +15/−5 combo — rev A.3/F30); primary-side regulated (18k/15k/1.3k divider off the aux winding).
Gate power at 10 kHz: Qg·ΔV·f ≈ 3 µC·19 V·10 kHz ≈ 0.6 W per switch — well inside a small
EE core. −5.1 V off-bias + Miller clamp holds the SiC off; +15 V matches the
Infineon/Starpower EconoDUAL alternates (hiitio dies accept +15…18/−5, abs −10).
This replaces six packaged DC-DC modules with ~₹2k of magnetics + jellybeans and is
reference-proven. FLT# wired-OR per side, RDY wired-AND into the enable chain, EN gated by
the hardware safety chain; **flyback enables are OR-gated (MCU ∨ FS26 GPIO1) so the SBC alone
keeps gate power alive for ASC** — GEN3's exact 74LVC1G32 arrangement.

ASC: the three **low-side** ASC pins (driver secondary side; all LS aux-sources share the
DCN-referenced domain) are driven together from a DCN-referenced buffer on its own QA01C
bias — commanded by the latched ASC_CMD line. Above the overspeed threshold the safe state is
ASC (3-phase short via LS); below it, 3-phase open. *Limitation (documented, TI-pattern
upgrade path): ASC hold during total KL30 loss needs an HV-fed aux supply; this revision
documents SPO as the KL30-loss safe state.*

## 8. Control card (ASIL D brain)

- **S32K396** (lockstep M7, ASIL-D capable, on-chip resolver interface + SDADC — the
  EV-INVERTERGEN3 architecture), LQFP-176.
- **FS26 SBC** (ASIL-D): VPRE/VCORE/VDDIO/VREF rails from KL30, challenge-response watchdog,
  FCCU error inputs, **FS0B** safe output.
- Safety chain (hardware): GATE_EN = FS0B ∧ MCU_GATE_EN ∧ DRV_RDY (74HC11 wired-AND);
  FLT# wired-OR → FCCU + latch. Watchdog escalation deasserts FS0B with no software.
- Resolver AFE: exciter op-amp + push-pull buffer (10 kHz carrier, from V15 via card),
  sin/cos dividers + filters into the S32K396 RDC pins (per SPF-91122).
- 2 × CAN-FD (**TCAN1042HGV-Q1** class), vehicle + diagnostic.
- HVIL: card sources a current loop through the HV connector chain; window comparator on the
  return; loop-open ⇒ controlled shutdown + discharge command.
- Motor temp (2 × PT1000/NTC), SWD debug, boot straps, 40-way harness with default-OFF
  pulldowns on every enable/PWM line the harness can float.

## 9. LV power

- Card: KL30 (9–16 V) → polyfuse + STPS5L60SY reverse Schottky + TPSMC24CA TVS (GEN3 exact) →
  **FS2633D**: VPRE 5.4 V buck → VCORE 1.5 V (MCU V11), VREF 5 V, LDO1 3.3 V, LDO2 5 V
  (VDD_HV_A); VMONEXT/VMONCORE rail monitors wired per GEN3.
- Power board: **two separately fused KL30 feeds** (HS / LS chains, GEN3 pattern — polyfuse +
  NRVBAF360T3G + TVS + bead each) feed the two gate flybacks directly (they regulate
  primary-side over the full 9–16 V window — no pre-regulator needed);
  **NCV4276C 5 V LDO** (INH from EN_FLYBK_LS) for driver VCC1 logic;
  **TPS55340-Q1 boost → 15 V** only for the reinforced sense/discharge/ASC bias modules
  (ISO5V-RFC-6K ×1, QA01C ×2 — their 13.5–16.5 V input window).
- Budget ≈ 20 W total LV on the power board at full gate load.

## 10. ASIL D concept (decomposition summary — printed on sheet 1)

| # | Mechanism | Independence |
|---|---|---|
| 1 | Lockstep S32K396 (ASIL-D core) runs the torque path | — |
| 2 | FS26 Q&A watchdog + supply monitors → FS0B | independent silicon, no SW |
| 3 | FS0B ∧ MCU_EN ∧ RDY hardware AND → all 6 driver enables | discrete gate, no SW |
| 4 | 3 × phase current + ΣI = 0 plausibility | 3rd channel = redundancy |
| 5 | 2 × independent isolated V_DC senses (Δ > 5 % ⇒ fault) | separate dividers + bias |
| 6 | Per-switch DESAT OC, driver-local, latching | different technology vs halls |
| 7 | ASC via LS drivers, latched, overspeed-gated | separate command + bias path |
| 8 | HVIL loop monitor → discharge + shutdown | continuous hardware window |
| 9 | Active discharge commanded + witnessed on both V_DC channels | §4 |
| 10 | Default-OFF pulldowns on every harness-floatable control line | passive |

This is an *architecture capable of* ASIL D per the NXP GEN3 / TI TIDM-02014 pattern; the
formal claim needs the ISO 26262 work products (HARA, FMEDA, DFA), out of schematic scope.
Every hardware mechanism those analyses rely on is present above.

## 10a. Interboard links (how the four assemblies connect)

Every net that crosses assemblies passes through a **named connector/stud/tab drawn on both
sheets**, and `erc-audit.mjs` asserts both ends pin-by-pin. Same net name on two sheets =
one net, joined only at that named interface (rule printed in every NET NAMING panel).

| Link | Interface | Carries | Behaviour when lost |
|---|---|---|---|
| L1 power ⇄ card | `JIC` ⇄ `JICC`, 40-way harness | 6× gate PWM, FLT/RDY, EN/ASC, V_DC + NTC feedbacks, 15 V bias, grounds | every line default-OFF / pulled to safe → gates low, FS26 sees loss |
| L2 power ⇄ discharge | `JDIS` ⇄ `JCTL`, 4-way (V15, QDIS_CMD, 2× GND) | active-discharge bias + command | opto dark → active path OFF; passive bleeder alone still meets < 60 s |
| L3 busbar (cap bank) | `JCBE[PN]` lugs ⇄ HV entry cables · `JCB[UVW][PN]` tabs ⇄ EconoDUAL DC terminals · `JCBD[PN]` ⇄ `JDCP/JDCN` | the DC bus itself | bolted, torque-controlled joints; HVIL loop opens before any service parting |
| L4 card ⇄ LEM ×3 | `JLEM` 10-way | 5 V per sensor + 3 hall outputs | pull-downs give implausible zero → MCU plausibility trips |
| L5 HVIL | `JHVIL` (power) through the HV connector loop to the card monitor | interlock continuity | open = V_DDIO/2 signature → torque off + commanded discharge |

QDIS_CMD's default-OFF pulldown lives on the **power board** (not only the discharge board),
so an unplugged `JDIS`/`JCTL` cable cannot float the command in either direction.

## 11. Verification status (rev A.3)

Three independent verification layers gate every release (see
[`verification-report.md`](verification-report.md)):
geometric pin-verify **1674/1674 (100 %)** · structural ERC **769 checks, 0 fail** ·
numeric worst-case verification **64 PASS / 3 WARN / 0 FAIL**, every constant
datasheet-real. The rev A.3 campaign found and fixed 18 defects (F1–F36), and the rev A.4
external-review response confirmed and fixed 10 more (F37–F46), and the second round 5 more (F47–F51 in the report),
including four HIGH-severity ones only the real datasheets could reveal: the flyback
controller UVLO grade, the 10 µH/1:1.6:2.9 transformer reality (frequency + feedback
re-derived), the FS26 VMONEXT 0.8 V reference, and the ASC abs-max level — flyback CS scaling + FB reference + start
path, net-alias floats, Y-cap class, common-cause sense bias, load-dump cap ratings,
discharge worst-case, OV-witness headroom.

## 11a. Rev A.4 — external design review, answered line-by-line

An independent review of the rev A.3 PDFs was verified claim-by-claim against the primary
datasheets (all in `docs/datasheets/`). Outcome: **10 confirmed defects, fixed (F37–F46)**;
**3 claims rebutted with evidence**; the rest were layout/bench items already on the VERIFY
list. Every fix is locked into `erc-audit.mjs` so it cannot regress.

**Confirmed and fixed**
- **F37 transformer phasing** — TDK VGT12EEM winding diagram (DS p.3/9): NP dots on pin 2,
  NS dot on pin 8, NF dot on pin 3; pins 6/7 have no internal connection. The secondary
  rectifier now hangs on pin 8 (dot) so it conducts only in the OFF interval; the drawn
  version was forward-mode ≈2.9·V_in ≈ 35 V into gates rated +22 V abs.
- **F38 flyback drain clamp** — the SMBJ85A stood forward from drain to rail (conducting
  every OFF interval) and its 94.4 V minimum breakdown could never protect an 80 V FET.
  Replaced by US1M blocking diode → SMAJ13A TVS returned to the rail: reflected 7.4 V stays
  under the 13 V standoff, and the drain sees ≤60 V even at a clamped-load-dump input.
- **F39 DESAT clamp** — BAT64-04 is a series pair (A=1, K=2, junction=3); it now clamps
  DESAT→VCC2 instead of injecting VCC2 into the DESAT node.
- **F40 ASC latch** — SN74LVC1G74 rebound to the real DCU pins; the 120 Ω network (2.5 V
  "low", 42 mA clear current) replaced by 1 k series / 10 k pull-ups (asserted low 0.45 V).
- **F41 KL15 sense** — 47 k/10 k + 100 nF after the steering diode; the MCU pin reads
  2.7 V at 16 V and injection stays ≤0.7 mA at a 40 V load dump (was a raw 13.3 V path).
- **F42 V_DC receivers** — re-zeroed to +0.5 V (buffered VREF5 divider): the AMC1311
  fail-safe state (negative differential) rails to ~0 V and is now distinguishable from a
  genuinely dead bus.
- **F44 gate-power feeds** — VBAT_H/VBAT_L are now sourced on the card (per-bank polyfuses
  off the reverse-protected node) instead of existing only as harness pin names.
- **F45 package pin maps** — every symbol rebound to its real package: 74LVC1G11/32 (the
  AND gates previously had **no VCC pin at all**), SN74LVC1G74, NCV4276C (output is pin 5),
  TPS55340 RTE-16 (with the required SS capacitor and FREQ resistor, previously absent),
  BUK9Y14 LFPAK56 (S=1/2/3, G=4, D=tab), ALM2402 PWP-14 (output-stage supplies bound; SHDN
  pulled up through 10 k — it doubles as the open-drain OT flag, and grounded/floating
  means *shutdown*), QA01C SIP-7 (1/2/5/6/7), PESD parts as their real 2- or 3-terminal
  selves, FS26 as the full LQFP-48+EP with VDIG/VBOS decouplers, both buck bootstraps,
  DEBUG strap, and DS-specified unused-pin terminations. The **S32K396 remains symbolic**
  (its datasheet carries no package pin table — the IO-signal spreadsheet binds it at
  layout) and the sheet says so explicitly.
- **F46 global fault reaction** — FLT_HS/FLT_LS diode-OR into a second LVC1G74: any DESAT
  trip latches DRV_EN low on all six channels in hardware; the driver's own soft-shutdown
  handles the faulted switch in ~1 µs; the MCU must clear deliberately after diagnosis.
- Driver ordering code pinned: **NSI6611ASC-Q1SWR** (the ASC-capable variant), not the
  family label.

**Rebutted with evidence**
- *"Divider too close to 2 V at 900 V with 1 % resistors"* — the system maximum is 850 V
  (spec §1) and the bottom leg is 0.1 %: worst-corner linear FS ≈ 902 V. Margin row added.
- *"Resolver excitation monitors are asymmetric"* — deliberately GEN3-exact (12 k / 24 k
  arms); the SDADC pair and NXP's angle diagnostics account for it. Documented, not a bug.
- *"Discharge calculations"* — the reviewer's own numbers (67.5 kΩ, 1 880 Ω, 1.52 s @800 V)
  match this design basis §4/§5; pulse ratings were already dimensioned (24.8 J vs 100 J
  single-pulse wirewound; 0.95 W vs 2 W per bleeder resistor).

**Regeneration / battery-disconnect strategy (review §5.4)** — the discharge resistors are
*shutdown bleeders*, not a regen dump, by design: the reaction to load-dump or contactor
opening under regeneration is **ASC** (three-phase short via the low side, FS26-backed gate
power hold-up), which circulates the machine current in the motor and cannot pump the bus.
Below the ASC/open crossover speed the reaction is three-phase-open (body diodes see less
than the bus). This is the printed safe-state concept on sheet 1; the discharge path only
ever handles the stored 116 J.

## 11b. Rev A.4.1 — review round two

All three "definite connection errors" were verified against the primary datasheets and
confirmed; two more items surfaced in the same audit. Fixed as F47–F51:

- **F47 AMC1311**: real pins are 2=IN, 3=SHTDN (active-high, internal 100 k pull-up). Both
  channels had them exchanged — the analog inputs were grounded. Symbol rebound; ERC now
  asserts the tap lands on IN. (Channel agreement genuinely could not have caught this —
  a common-mode symbol error defeats redundancy; the fix layer is the package audit.)
- **F48 TRKIN**: it is the VREF regulator's input supply (abs-max group with LDOIN/CORE_IN),
  not a ground-able unused input. Now fed from VPRE — same rail as LDOIN, which already has
  to satisfy VREF+350 mV headroom under the 6.35 V max. CIN_TRK ≥0.5 µF effective is a
  layout placement note (VPRE bank).
- **F49 TPS55340 pin 5 = SYNC** (7 V abs), grounded per the DS "if not used" instruction.
- **F50 capacitors**: CVBOS 1→4.7 µF, CSB6 (LDO1/V3B) 1→4.7 µF, and CSB5 (VREF) 1→2.2 µF —
  the third found by re-reading the same table (COUT_VREF 1.1–3.3 µF effective).
- **F51 V15 pass-through**: a boost cannot regulate below its input; with V12L at 24 V
  (jump start) or ~33 V (clamped load dump) the LB15/DB15 path feeds V15 directly, and the
  QA01C window is 13.5–16.5 V. Fix: NCV4276C-ADJ (40 V/0.4 A) post-regulator — in mild
  dropout normally (V15 ≈ 15.0–15.2 V), clamping at 15.0 V during pass-through. Sustained
  24 V service dissipates ~2.8 W → thermal shutdown may cycle V15: acceptable, because
  jump start is a stationary service case and the passive bleeder needs no LV at all.
- **Release hygiene**: the earlier standalone discharge/cap-bank PDFs the reviewer held
  were a prior send (rev A.3) — every PDF now regenerates from one source in one run, and
  all five carry the same revision string. Lesson recorded: a matching pin *count* is not a
  matching pin *map*.
- **ASC ↔ fault-latch interplay** (review §4): per the NSI6611 DS, ASC forces the output
  high regardless of the input side — so DRV_EN low does not block ASC (they are correctly
  independent), while DESAT and VCC2-UVLO outrank ASC (a faulted switch is not forced on).
  Startup/entry/exit sequencing stays on the bench list.

## 12. References

- Wolfspeed CRD300DA12E-XM3 user guide + discharge PCB (passive 9× CRGP2512F68K network —
  copied verbatim here) + controller schematics
- TI TIDM-02014 (TIDUF23A): safety concept (PMIC SAFE_OUT OR-gate, LVSS straps, dual-MCU ASC
  decomposition), UCC5880/UCC14240 gate-drive pattern
- NXP EV-INVERTERGEN3: S32K396-HPWR-MC control card (SPF-91122), EV-POWEREVBHD2 power board
- hiitio catalog (hiitio.com, Sep-2025): HCS600FH120D3C1 EconoDUAL 3 SiC module line,
  HCM75S12T4K3, module packaging in-house (IATF 16949)
- DC-Modules power-module-platform: NSI6611 verified pin map, drawing pipeline, barrier rules
