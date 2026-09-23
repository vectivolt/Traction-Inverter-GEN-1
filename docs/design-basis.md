# Traction Inverter — Design Basis (rev A.6)

A standalone product: an economical, ASIL-D-capable **220 kW-class, 800 V** SiC traction
inverter (passenger EV / commercial traction, NOT an LEV drive). Two boards: **Power (HV)** +
**Control card (LV)**, one 40-way harness. Schematic-complete deliverable; PCB layout is a
later phase.

Class alignment: Wolfspeed CRD300DA12E-XM3 (300 kW/800 V) and TI TIDM-02014 (300 kW/800 V) are
the direct references; NXP EV-INVERTERGEN3 (S32K396) is the control architecture reference.

## 1. Ratings

| Parameter | Value | Basis |
|---|---|---|
| DC link voltage | 500–850 V (700 V nom) — 8XX SKUs; 250–500 V (400 V nom) — 4XX SKUs | 800 V-class pack (charging excursion to 850 V) |
| Peak power | 220 kW / 30 s **from 654 V up** (168 kW at 500 V) | 2XX kW class target; current-limited below — firmware P(V_dc) (A.6, R-F09) |
| Continuous power | 120 kW **from 656 V up** (91 kW at 500 V) | thermal design point |
| Peak phase current | 340 Arms (480 A pk) | 220 kW at 700 V, PF 0.85, SVPWM linear |
| Continuous phase current | 185 Arms | 120 kW same point |
| DC feed current | 314 A pk / 171 A cont | P/V_dc |
| Switching frequency | 8–10 kHz | SiC module loss budget, traction standard |
| Motor | 3-phase PMSM, resolver feedback | |
| Ambient / coolant | −40…+85 °C board, 65 °C coldplate | liquid-cooled |

Max SVPWM line-line voltage at 700 V: V_ll = V_dc/√2 = 495 Vrms.
P = √3 · 495 · 340 · 0.85 ≈ 248 kW available; 220 kW rating leaves modulation/PF margin.
**Rev A.6 (review R-F09):** the ratings are current-limited below ≈655 V — with a 5 %
modulation reserve, P = √(3/2)·0.95·V·I·0.85 gives 220 kW at 654 V and 168 kW at 500 V. The
full per-SKU envelope and the firmware derating rule are in [`firmware-contract.md`](firmware-contract.md) §3.

### One platform, four SKUs (rev A.6)

The same power PCB, control card, discharge PCB and cap-bank busbar build every SKU; the
difference is the module (SiC HCS600 / IGBT HCG600 — same D3 outline and pin map), the 16
film cans (1100 V/20 µF for 8XX, 600 V/50 µF for 4XX), a handful of resistor/capacitor values,
an identity resistor and the firmware parameter set. End of the 30 s peak at V_max with the
corrected loss model (S4, static junction path since round 8; with no plate mass at all 135 / 142 / 133 °C): 8XX SiC 125 °C · 8XX IGBT 132 °C (5 kHz) · 4XX IGBT 125 °C; the rev
A.5 "89 °C / 110 °C" figures used the wrong conduction formula (R-F02/F26). Matrix, costs and
the launch recommendation: [`variants.md`](variants.md).

## 2. Power stage — SiC module selection (hiitio primary, standard EconoDUAL 3 footprint)

At 340 Arms per phase, discrete TO-247 paralleling (6–8 per switch) loses to a module on
economy and on the 12 nH-class commutation loop the references achieve. hiitio's module line
copies industry-standard footprints outright, which satisfies the second-source rule at module
level: **EconoDUAL 3** is the most second-sourced power-module outline in the industry.

**Choice: 3 × hiitio HCS600FH120D3C1 — 1200 V / 600 A SiC MOSFET half-bridge, EconoDUAL 3.**

- Drop-in alternates with the **pin map verified against their datasheets** (rev A.6 catalogue
  scan): hiitio **HCG600FH120D3E1EA** (1200 V/600 A IGBT — the IGBT SKUs), **HCS900FH120D3C1**
  (1200 V/900 A SiC, 2.8 mΩ hot) and **HCG900FH120D3RC** (1200 V/900 A IGBT, SC 8 µs at
  600 V/150 °C) — the higher-current upgrade path. **Not** drop-ins, despite earlier listing:
  HCS800FH120D4B3 ("ED3H": lettered press-fit pins with split Kelvin/power sources) and
  Infineon FF6MR12W2M1H (not an EconoDUAL-3 package code). No 650/750 V part exists in this
  outline, so 4XX SKUs use the same 1200 V silicon. Third-party EconoDUAL-3 parts need their
  own pin-map check before listing.
- Per-switch loading at peak (340 A rms, 850 V, 10 kHz — rev A.6): conduction **330 W**
  (½·I²·R, 5.7 mΩ hot — synchronous rectification carries the current in both directions) +
  switching ≈200 W (DS energies at RG_ON 3.3 / RG_OFF 6.8 Ω) + Qrr + dead-time diode ≈ **554 W
  per switch** for 30 s; ≈200 W continuous. Tj at the end of the 30 s peak ≤ 125 °C at 65 °C
  coolant (coldplate 0.045 K/W assumed — thermal test). DS Rth(j-c) 0.066 + 0.015 grease.
- 1200 V at 850 V max bus = 71 % utilization — the same margin the XM3/TIDM run.
- Per-module DC-side film snubber 1 µF/1200 V across DC+/DC− at the terminals; laminated
  busbar target ≤ 5 nH external. The **module's own stray inductance is not published** (the
  XM3 figures do not transfer to a D3 package — R-F29). With the DS fall time (13 ns cold at
  3.3 Ω) the 850 V turn-off overshoot is the binding constraint: RG_OFF starts at 6.8 Ω and the
  double-pulse test at 850 V/481 A, cold and hot, sets it within 3.3–10 Ω (R-F01, S8).
- Module NTC (2 pins) → conditioned on the power board, read by MCU.

Why not discrete TO-247 (the 30–60 kW answer): 6-parallel × 6 switches of 13 mΩ discretes
matches neither the loop inductance nor the assembly economics of one module per phase at
this current.

## 2b. IGBT SKUs (same boards, same layout — values per SKU BOM)

**HCG600FH120D3E1EA** (hiitio, 1200 V/600 A IGBT half-bridge) shares the **D3 EconoDUAL-3
outline and the exact 11-pin map** of the SiC module (its DS p.8 circuit diagram: 1=G_L
2=E_L 3=DC− 4=DC+ 5/6=NTC 7=G_H 8=E_H 9=HS C-sense 10/11=AC) — a true drop-in, zero layout
change. Facts: VCEsat 1.50/1.82 V (25/175 °C, 600 A, VGE 15 V) · Eon+Eoff 143.7 mJ hot ·
Qg 4.36 µC · Isc 1800 A · Tvjop 150 °C · NTC B25/50 3375.

**The SKU is value swaps + firmware** (`npm run bom:igbt` → `docs/bom-igbt.md`; 4XX:
`npm run bom:igbt4`):
- R⟨ph⟩⟨HL⟩DS 100 Ω → **4.7 kΩ** (DESAT trip 4.2–7.2 V at the collector vs VCEsat 1.8 V hot)
- C⟨ph⟩⟨HL⟩BL 47 pF → **82 pF** C0G — rev A.6 (R-F03): the DS short-circuit rating is **tP ≤ 6 µs**
  at 800 V/175 °C/15 V, not the 10 µs class used before; 150 pF alone took 4.5 µs to detect.
  82 pF: 2.98 µs worst detection + ≈1.5 µs soft-off < 5 µs derated; contained SC test gates it
- R⟨ph⟩⟨HL⟩ON/OFF → **1.0/1.0 Ω** (IGBT Eon rises steeply with Rg — DS Fig.5)
- RF⟨HL⟩RT 10 k → **8.2 k** (≈308 kHz: the IGBT-class gate charge needs +22 % bias capacity)
- firmware: f_sw **5 kHz**, dead-time 2.5 µs, same NTC curve
- gate rails **the same as SiC** (+15.4 V nominal, 13.6–16.9 V corners / −5.1 V — rounds 7/8): the DS characterizes at ±15 V, VGE(th) is a high
  5.0–6.2 V, and the NSI6611 Miller clamp holds the off-state — dv/dt shoot-through stays a
  bench row.

Rev A.6 re-verification with the diode die, m·cosφ and energies at our gate network (R-F26):
IGBT Tj 127 °C at the end of the 30 s peak at 850 V/5 kHz (was quoted 110 °C), 98 °C
continuous; semiconductor efficiency 98.5 % vs 99.0 % SiC at the continuous point. 4XX is a
separate SKU of the same boards with its own cap bank (see §3 and `variants.md`).

## 3. DC link (film) + ripple

Capacitor RMS current (2-level SVPWM): worst case **0.65 · I_ph,rms** (Kolar, M ≈ 0.6, cosφ = 1;
rev A.6 — 0.62 was the rated point) → **221 A rms peak-30 s (13.8 A/can) / 120 A continuous**.
4XX SKUs: 16 × 50 µF/600 V (≥18 A/can) in the same positions — 260 A worst case at 400 A rms
(16.2 A/can).

**Choice: 16 × 20 µF 1100 V DC film (Faratronic C3D1M206KFSA382, LCSC C2840809) = 320 µF.**
- DFM rev A.2: no 40 µF/1100 V can is LCSC-stocked in any brand; 16 × Faratronic 20 µF is
  cheaper (~$3.4/can), in stock, and spreads ripple. Alt: TDK B32778G0406K000 40 µF 4-lead,
  one per two positions (DigiKey). References use 300 µF (3 × 100 µF FTCAP custom).
- Ripple capability ≥ 13 A each @10 kHz → 200 A continuous class ✓ (211 A is a 30 s peak).
- Energy at 850 V: E = ½·320 µF·850² = **115.6 J** (discharge design input).
- Voltage ripple: ΔV ≈ I_c/(8·f_sw·C_tot)-class ≈ negligible; ESL dominated — busbar item.
- No series strings → no balance dividers; the passive bleeder below is the fixed load.
- **Packaging: the 16 cans are a separate CAP BANK assembly** (sheet 2 of 4) — a
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

**Passive (always on):** 12 × standard **22 kΩ 2512 2 W** thick-film as 6-series ×
2-parallel = **66 kΩ net** (rev A.6, R-F24: with ±5 % parts the low-tolerance resistor of the
old 5 × 27 k string carried 184 V at 850 V — 92 % of a plain 2512's 200 V working rating).
- 850→60 V in **56.5 s nominal / 65.3 s worst** (R +5 %, C +10 % incl. the 3 µF local caps) ✓
- At 850 V: 0.96 W per resistor worst (48 % of 2 W); worst-tolerance resistor **154 V (77 %)**
- 4XX SKUs: 12 × 15 kΩ = 45 kΩ → 500→60 V in 76.6 s / 88.5 s worst, 91 V per resistor
- Rule printed on the discharge sheet: never energize without this board fitted.

**Active (commanded):** 1200 V SiC FET + 4 × series **470 Ω 10 W ceramic-cased wirewound
(SQP/RX27-class, axial)** = 1.88 kΩ (rev A.3 / F26: the 560 Ω string missed the 2 s crash
target at the R+5 %/C+10 % worst case — 2.19 s).
- 850→60 V in **1.57 s nominal / 1.81 s worst-case** with the bleeder counted once (rev A.6
  S5 fix) ✓ (≤ 2 s OEM crash target; ECE R100 5 s). 4XX: 4 × 220 Ω → 1.47 / 1.70 s
- Peak 0.45 A / 384 W total decaying; energy 115.6 J → **28.9 J nominal / 31.8 J worst per
  resistor** (10 W ceramic wirewound single-pulse capability ≥ 100 J) ✓; 213 V per resistor ✓
- Switch: hiitio **HCM75S12T4K3** (1200 V, 75 mΩ, TO-247-4L — vendor consolidation; any
  1200 V ≥ 5 A FET fits). Peak switch dissipation 0.38²·75 mΩ ≈ 11 mW — trivial.
- Control: default-OFF — TLP152-class opto driver on a DCN-referenced isolated bias (QA01C),
  gate pulldown to DCN so a floating control line means OFF.
- Verification: MCU commands discharge and witnesses V_DC fall on **both** isolated senses
  (§6): stuck-off is detected within 200 ms (no ΔV). **Stuck-on with the battery connected is
  NOT visible as a bus load** (rev A.6, R-F23 — the battery holds the bus; 384 W into four
  10 W parts): QDIS fires only with the contactors reported open + 5 s timeout (FW-17), a
  shorted QDIS is caught at the next precharge (FW-19), and the wirewounds are specified
  fail-open/flameproof. Resistor spec: ≥ 3 × 32 J pulses per 5 min.

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
- **Insulation is completed by the busbar (N7).** The HC5FW 900-S/SP1 is LEM's low-voltage-
  application, *reduced-insulation* variant ("cover without sleeve"): 2.5 kV/1 min per IEC 60664-1,
  creepage 3.6 mm, clearance 2.7 mm, CTI 550 (DS p.1, p.3). On its own that is short of basic
  insulation at 850 V (IEC 60664-1 Table F.5, PD2, material group II: ≈6 mm creepage). Each phase
  busbar therefore carries an insulating sleeve through the aperture (≥1 kV DC-rated heat-shrink
  or powder coat, ≥10 mm past both faces) — a mechanical-drawing note, ≈₹20/phase, no PCB change.
  Release gate: LEM confirms the sleeve spec for 850 V DC working.

## 6. DC-link voltage sensing — two independent channels

2 × (6 × 470 kΩ series top + 6.2 kΩ bottom → 850 V ≈ 1.87 V; full-scale 2 V = **911 V**, so
the OV witness never saturates — rev A.3 / F27) into **AMC1311B** (0–2 V input) isolated
amps, **each with its own reinforced bias module** (F4), read on different ADCs.
- 142 V and 43 mW per top resistor (1206 thin-film, 200 V rated) ✓; uncalibrated error
  **±2.1 % worst case** (correlated top string — rev A.6, R-F37; the old ±0.7 % was an RSS),
  ≈±0.3 % after EOL calibration; worst-corner linear FS 902 V vs the 880 V OV trip
- Channel 2 is the discharge witness and OV cross-check (disagreement > 5 % ⇒ fault).
- **Not fully independent (R-F11):** the receivers share VREF5/V5A and the +0.5 V offset
  buffer UVOF, whose failure moves both channels together. Rev A.6 reads `VOFS` on its own ADC
  pin (FW-07) and cross-checks the BMS pack voltage with contactors closed.
- (References use AMC0386-Q1 integrated-divider parts — the divider+AMC1311 chain is the
  economical equivalent with the same reinforced barrier.)

## 7. Gate drive (per switch × 6)

**NSI6611A-Q1** (AEC-Q100, 10 A source/sink, SOIC-16W, reinforced, CMTI ≥150 V/ns): DESAT
(2 × US1M 1 kV series sense diodes + 47 pF blanking: 0.9 µs typ / 1.9 µs worst to detection),
active Miller clamp (CLAMP wired to gate), UVLO, RDY/FLT# feedback, separate RG_ON/RG_OFF
(**3.3 Ω / 6.8 Ω** — rev A.6: the DS-characterized turn-on point and an overshoot-limited
turn-off start value; the old 1.5/1.0 Ω was faster than any published point), gate-source 10 k.
10 A peak drive on a ~2.5–4 µC module gate ⇒ no booster stage needed (the XM3 driver does the
same class of module directly).

Bias: the **GEN3 dual-flyback pattern, copied from EV-POWEREVBHD2**: two **UCC28C40DR**
current-mode flybacks (HS chain / LS chain; the C43 grade's 8.4 V UVLO cannot start at 9 V
crank — F31; original NJW4140 dropped for distribution at DFM; 2-transistor default-OFF
enable clamp and a 12 V trickle-start feed; switch **BUK7Y14-80E** — standard-level ±20 V
gate, F56 — with a US1M + SMAJ13A primary clamp, F38), each driving **three VGT12EEM
transformers with primaries paralleled — one floating secondary per phase → six independent
domains**, secondary rectifier on the **dot end (pin 8, F37)**;
per-secondary rectifier + **BZT52-C5V1 zener splitting the winding into +15.4 V / −5.1 V about
each Kelvin source** (the HCS600 datasheet's recommended +15/−5 combo — rev A.3/F30); primary-side
regulated from the aux winding (F33). Round 7 (A6-R06/R07): the 52.3k/15k divider senses its
**own** small rectifier on the aux winding (1N4148WS + 100 Ω + 100 nF), not VDD. With VDD sensing,
the 2.2 k start feed could hold FB above the reference while the converter was stopped (from a
≈16 V rail for a low-current controller, certainly at a 24 V jump start), so it never restarted.
The old model also left out the aux diode drop: the real rail was ≈16.9 V, not 15.6 V. Counted
properly (V_FB, divider, both rectifiers at peak current, FB bias, split zener, the FB-sense 100 Ω at
100/50/20 % conduction per corner), VCC2 = 15.4 V nominal, 13.57–16.89 V corners, inside a 13.5–17.0 V bias window.
**Start-up (rev A.6, R-F18):** the UCC28C40 has only 0.4 V of UVLO hysteresis and no VDD clamp.
2.2 kΩ trickle start + **47 µF** VDD reservoir make one start burst reach aux-winding takeover in
every corner (73–240 ms at KL30 9–14 V, S1 — rev A.7 with the divider off VDD); an **18 V zener** protects VDD when the flyback is
held off during a 24 V jump start. The rev A.5 values (4.7 k / 4.7 µF) could not start at 9 V.
Gate power at 10 kHz: Qg·ΔV·f ≈ 3 µC·19 V·10 kHz ≈ 0.6 W per switch — well inside a small
EE core. −5.1 V off-bias + Miller clamp holds the SiC off; +15 V matches the
Infineon/Starpower EconoDUAL alternates (hiitio dies accept +15…18/−5, abs −10).
This replaces six packaged DC-DC modules with ~₹2k of magnetics + jellybeans and is
reference-proven. FLT# wired-OR per side, RDY wired-AND into the enable chain, EN gated by
the hardware safety chain; **flyback enables are OR-gated (MCU ∨ FS26 GPIO1) so the SBC alone
keeps gate power alive for ASC** — GEN3's exact 74LVC1G32 arrangement.

ASC: the three **low-side** ASC pins (driver secondary side; all LS aux-sources share the
DCN-referenced domain) are driven together from a DCN-referenced buffer on its own QA01C
bias — commanded by the latched ASC_CMD line, through 1 k at each driver pin. Entry is
**break-before-make** (round 7, RR05): a 12 nF delay holds the low sides off ≥ 3.4 µs after the
latch sets. By then the high sides are off: FS0B has dropped DRV_EN (MCU dead), or the eFlexPWM
fault has forced PWM low (MCU alive). ASC deliberately does **not** drop EN. The NSI6611 honours
DESAT over ASC only with EN high and IN+ high (DS §8.12), so a commanded ASC is held as
**PWM-ASC** with EN high, and a high side that fails short still ends in the low-side DESAT
(`firmware-contract.md` §4c). A latched driver fault masks the ASC command in hardware (UASCG,
round 8), so the healthy low sides release and the bridge reaches SPO; the faulted driver holds
itself off until the FW-15 reset (DS Fig. 8.11). Which safe state applies is a motor- and
fault-dependent **firmware decision** (`firmware-contract.md` §6: e.g. a DESAT on a low-side
switch permits SPO only). *Limitation: ASC cannot be held through total KL30 loss (S10: 0.5–
3.1 ms of gate reservoir, ≈1 ms of command path). SPO at dead LV is energy-safe only if the
motor's E_LL,pk at n_max is below the cap rating (1000 V 8XX / 600 V 4XX); otherwise the
HV-fed backup-bias option (TI TIDM-02014 pattern) is required for that motor.*

## 8. Control card (ASIL D brain)

- **S32K396** (lockstep M7, ASIL-D capable, on-chip resolver interface + SDADC — the
  EV-INVERTERGEN3 architecture), LQFP-176.
- **FS26 SBC** (ASIL-D): VPRE/VCORE/VDDIO/VREF rails from KL30, challenge-response watchdog,
  FCCU error inputs, **FS0B** safe output.
- Safety chain (hardware): DRV_EN = FS0B ∧ MCU_GATE_EN ∧ RDY_HS ∧ RDY_LS ∧ FLT_OK (two
  74LVC1G11); FLT# wired-OR → MCU + fault latch. Rev A.6: the latch output reaches the AND
  through 10 k/3.3 nF (22–53 µs to the Schmitt threshold, so a DESAT-ing driver finishes its
  soft turn-off first), and the MCU clear is a hardware one-shot (a stuck pin cannot hold the
  chain permissive). The latch cannot be made fault-dominant: the NSI6611 releases FLT only on
  an EN rising edge (recovery sequence: `firmware-contract.md` §7). Watchdog escalation
  deasserts FS0B.
  Round 7 changes:
  - Both RC nodes and the FS0B line reach the logic through a **74LVC3G17-Q100 Schmitt
    buffer**. The LVC flip-flop/AND inputs allow 5–10 ns/V; the RC nodes were 14,000–63,500 ns/V.
  - FS0B/FS1B pull-ups are **5.1 k** (NXP's value), so FS1B stays inside its 2 mA V_OL point.
  - A re-pulsing clear pin is covered by the locked eFlexPWM fault inputs and the FS26 watchdog
    (RR03).
  - Both latches are the AEC-Q100 Nexperia part.
  Round 8 changes:
  - Both latch presets (the FLT diode-OR, now BAT46, and the FS1B strap) go through a second
    Schmitt buffer.
  - ASC_CMD = latch AND no-FLT (UASCG).
  - FS1B is pulled up through its strap only, and its battery-short clamp goes to ground.
- Resolver AFE: exciter op-amp + push-pull buffer (10 kHz carrier, from V15 via card),
  sin/cos dividers + filters into the S32K396 RDC pins (per SPF-91122).
- 2 × CAN-FD (**TCAN1042HGV-Q1** class), vehicle + diagnostic.
- HVIL: card sources a resistor ladder through the HV connector chain; the MCU reads the
  signature on an ADC pin (3.0 / 2.0 / 2.5 V) — **there is no hardware comparator** (rev A.6,
  R-F12). Loop-open ⇒ torque ramp to zero and the §6 safe state; the discharge command follows
  only once the contactors are reported open.
- Motor temp (2 × PT1000/NTC), SWD debug, boot straps, 40-way harness with default-OFF
  pulldowns on every enable/PWM line the harness can float.

## 8a. FS26 OTP configuration (what the FS2633D programmed variant must contain)

The schematic assumes this exact OTP set — the ordering step must procure a variant (or
NXP OTP-programming service) matching it; the abbreviated "FS2633D" label alone does not
guarantee it (review round 3/5 item):

| OTP field | Required setting | Anchored by |
|---|---|---|
| VPRE_V | 6.0 V | LDOIN & TRKIN headroom ≥ V5A+0.35 V, ≤6.35 V max |
| FPRE | 450 kHz | LSBC = 10 µH (DS pairing: L_VPRE 10 µH ↔ 450 kHz) |
| CORE_LSEL_OTP[1:0] | 10 (2.2 µH) | LCOR = XAL4020-222 (eff window 1.5–2.9 µH) |
| VCORE | 1.5 V | V15S rail → QBAL ballast → V11 (S32K39 Table 11) |
| LDO1 / LDO2 | 3.3 V / 5.0 V, slotted ON | V3B / V5A rails |
| VMONEXT | enabled (0.8 V ref) | 52.3 k/10 k divider on V5A |
| VMONCORE / VMONPRE | enabled | V15S / VPRE monitors |
| TRK1/TRK2 slots | OFF (111) | trackers unused, outputs open |
| WK2PD_OTP / IO2PD_OTP | 1 / 1 | WAKE2, GPIO2 left open |
| VBST (boost front-end) | disabled | pins terminated per DS unused table |
| FS0B + FS1B, FCCU1/2 | enabled | safety chain + ASC strap |
| FS1B_FS0B_EN_OTP; FS1B_TDELAY / TDUR (FS_SAFE_IOS_2, init) | 0 (delayed-assertion mode, OTP default); TDELAY **00000** (with FS0B), TDUR 100 ms; BACKUP_SAFETY_PATH_FS0B = 1, **BACKUP_SAFETY_PATH_FS1B = 0** (round 9, A8-03: an FS1B short-to-high is a DTC, not an MCU-reset loop) — all read back by FW-12 | FW-16 step a reads the ASC preset at once; the FAULT_OUT pulse and a wire-short overload are bounded (round-8 cross-check R8X-03, P-01) |
| GPIO1STAGE_OTP / GPIO1 power-up slot | push-pull (or high-side driver), **not slotted** — stays low until the MCU sets it over SPI | flyback-enable OR input: a slotted GPIO1 would raise gate power at every POR while FS1B still presets the ASC latch, engaging LS-ASC at boot (round 8, N15; firmware-contract §9) |

## 9. LV power

- Card: KL30 (9–16 V) → polyfuse + STPS5L60SY reverse Schottky + TPSMC24CA TVS (GEN3 exact) →
  **FS2633D**: VPRE 6.0 V buck (§8a) → VCORE 1.5 V (MCU V11), VREF 5 V, LDO1 3.3 V, LDO2 5 V
  (VDD_HV_A); VMONEXT/VMONCORE rail monitors wired per GEN3.
- Power board: **two separately fused KL30 feeds** (HS / LS chains, GEN3 pattern — polyfuse +
  NRVBAF360T3G + TVS + bead each) feed the two gate flybacks directly (they regulate
  primary-side over the full 9–16 V window — no pre-regulator needed);
  **NCV4276C 5 V LDO** (INH tied on via 100 k since A.4 — VCC1 and the AMC1311 LV sides
  stay alive whenever the card is awake) for driver VCC1 logic. Round 9 (N17): both feeds come
  from a card-side P-FET switch (QLVS) that follows V5A, so the power board is unpowered while
  the vehicle sleeps. It had drawn ≈ 150 mA from unswitched KL30;
  **TPS55340-Q1 boost → 15.4 V (V15B) → NCV4276C-ADJ post-regulator → V15 = 15.0 V**
  (F51 — a boost passes its input through above the setpoint; the LDO clamps jump-start/
  load-dump pass-through away from the bias modules) feeding the four isolated bias
  modules (Murata MGJ2 ×2 for the V_DC senses + Mornsun **QA01C-18** ×2 for ASC/discharge —
  13.5–16.5 V windows; QA01C-18 outputs **+18/−3 V**, 16.9–20.9 V at these light loads. Round 9,
  A8-N01: F61 had read the base QA01C's +20/−4 V sheet).
- Budget ≈ 20 W total LV on the power board at full gate load.

## 10. ASIL D concept (decomposition summary — printed on sheet 1)

| # | Mechanism | Independence |
|---|---|---|
| 1 | Lockstep S32K396 (ASIL-D core) runs the torque path | — |
| 2 | FS26 Q&A watchdog + supply monitors → FS0B | independent silicon, no SW |
| 3 | FS0B ∧ MCU_EN ∧ RDY hardware AND → all 6 driver enables | discrete gate, no SW |
| 4 | 3 × phase current + ΣI = 0 plausibility | 3rd channel = redundancy |
| 5 | 2 × isolated V_DC senses (Δ > 5 % ⇒ fault) + shared-offset monitor + BMS pack cross-check | separate dividers, amps + bias; receivers share VREF5/offset (monitored — R-F11) |
| 6 | Per-switch DESAT OC, driver-local, latching | different technology vs halls |
| 7 | ASC via LS drivers, latched, overspeed-gated | separate command + bias path |
| 8 | HVIL loop monitor → torque off + safe state | MCU-read ADC signature (no hardware comparator — R-F12) |
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

### ASC hold-up operating limit (S10, corrected in rev A.6)

After a TOTAL 12 V loss the LS drivers' VCC2 reservoirs hold ASC for **0.5–3.1 ms** (two series
reservoirs, DC-bias-derated MLCC, bleeder + gate loads — the old "≈15 ms" put the VEE cap in
parallel with VCC2), and the ASC command path (boost → V15 → QA01C → TLP152) collapses within
≈1 ms. Sustained ASC therefore **requires KL30 present** — the FS26 (GPIO1) holds the flybacks
up through faults, but not through a dead 12 V system. A dead-LV coast-down is three-phase-
open; it is energy-safe only for motors whose E_LL,pk at n_max stays below the cap rating
(`firmware-contract.md` §6) — otherwise the HV-fed backup-bias option is required.

## 11. Verification status (current release: rev A.10)

Three independent verification layers gate every release (see
[`verification-report.md`](verification-report.md)):

- geometric pin-verify **1815/1815 (100 %)**;
- structural ERC **891 checks, 0 fail**, with a lock-in for every fixed finding (by net, pin number and
  first-match MPN per SKU; mutation-tested);
- numeric worst-case verification **118 PASS / 14 WARN / 0 FAIL** across all four SKUs;
- operating-point simulation (`sim-verify.mjs`, S1–S10 on the shared `loss-model.mjs`)
  **23 PASS / 6 WARN / 0 FAIL**.

Every WARN names the bench or vendor gate that closes it. The BOM generator fails on any
value/MPN disagreement and on missing, empty or stale inputs. Rev A.7 answered the seventh
review round (RR01–RR10, A6-R01–R14) line by line in
[`review-A7-disposition.md`](review-A7-disposition.md). The implemented fixes were then
cross-checked adversarially by an independent reviewer, and that caught one critical draft
defect before release. Firmware obligations are in [`firmware-contract.md`](firmware-contract.md).

Earlier rounds: the rev A.3 campaign found and fixed 18 defects (F1–F36); the external
reviews then confirmed and fixed F37–F46 (A.4), F47–F51 (A.4.1), F52–F57 (A.4.2), F58–F59
(A.4.3), F60–F62 (A.5 docs audit), F63–F76 (A.6), F77–F89 (A.7), F90–F97 (A.8), F98–F105
(the A.8 cross-check), F106–F113 (A.9), F114–F119 (the A.9 cross-check) and F120–F122 (A.10).

## 11j. Rev A.10 — schematic rechecks of A.9 (summary)

Two schematic-only rechecks of `7235337` kept every A.9 correction and raised three items, answered
in [`review-A10-disposition.md`](review-A10-disposition.md):

- **RDY input edge (A9-S01).** The open-drain RDY lines drove the 74LVC1G11-Q100 AND gates at
  ≈20–100 ns/V against a 10 ns/V limit. A third 74LVC3G17-Q100 (USCH3, with a dead-state pull-down)
  now conditions both. This reverses the round-7/8 "state-benign" rejection: no LVC input on the
  shutdown chain runs outside its datasheet.
- **RASCG power (S9-01).** 2.2 k carries up to 0.12 W for as long as ASC is held. It is now a ROHM
  ESR03 (0.27 W at 85 °C).
- **Text.** The discharge sheet now reads 66 k; the gate divider is stated as 13.3–18.2 V (a
  divider, not a clamp); the A9 disposition quotes the final harness map.
- **Cost:** +₹11/unit.

## 11i. Rev A.9 — review round nine (summary)

Two rechecks of `0da9014` (A8-01…03, A8-N01…N03, gates A8-G01/G02) are answered in
[`review-A9-disposition.md`](review-A9-disposition.md). Both reviewers kept the round-8 fixes and
recommended no power-stage change.

- **Bias module binding (A8-N01).** PSASC/PSQD are QA01C-18 (+18/−3 V). F61 had read the base
  QA01C's +20/−4 V sheet. The real envelope at these loads is 16.9–20.9 V, so ASC entry is 7.52 µs
  and the FW-06 end-point 906 V. The discharge gate is now a 1.5 k/10 k divider at 13.4–18.1 V
  (A8-N02).
- **FLT/RDY rating (A8-01).** The NSI6611 rates FLT/RDY to VCC1 with no +0.3 V. Their pull-ups, and
  the diode-OR pull-up, now sit on V5GD (harness pin 1; the harness map keeps every supply pin beside ground in both dual-row numberings). A lost V5GD ends
  deterministically in SPO.
- **Proofs.** RFS4 is an anti-surge ROHM ESR03 covering 0.23 / 0.48 / 0.65 W at 16 / 24 / 35 V;
  BACKUP_SAFETY_PATH_FS1B = 0 (A8-03). The LED window uses a ±28 % tempco band and is judged on the
  guaranteed margins (A8-02). FW-16 has an energy limit, < 12 V read, ≤ 75 mJ (A8-N03). The §6
  motor/vehicle release rule replaces the "double fault" shortcut (A8-G02).
- **Self-found (N17).** The power board's LV side ran on unswitched KL30, ≈ 150 mA while parked.
  A card-side P-FET on V5A (DMP6023LEQ) removes it.
- **Cross-check of the fixes** (independent Opus reviewer, R9X-01…15, F114–F119). No CRITICAL
  finding:
  - A dead V5GD made the V_DC receivers read a false 0 V. V5GD is now read on an ADC pin; outside
    4.75–5.25 V the firmware marks V_DC invalid and forces SPO. The same reading catches a
    back-powered, hovering V5GD.
  - The drawn harness numbers row by row, so the map is re-laid: VBAT and V5GD touch only ground in
    both dual-row numberings.
  - The resolver-exciter LDO now sleeps with the FS26: whole-inverter parking drain ≤ 43 µA at
    25 °C.
  - QLVS has a slew network (≤ 1.1 A inrush) and a 10 k gate pull-up that holds off a hot 2N7002's
    leakage.
  - FW-16 no longer trusts low-voltage readings (read < 3 V, or QDIS for 2 τ).
  - DESATs go to NVM, because an FS26 restart now clears the drivers' own latch.
- **Cost:** +₹29/unit (₹28 of it the switch and its gate network).

## 11h. Rev A.8 — review round eight (summary)

Two rechecks of `2ac42ed` (R7-01…R7-07, A7-N01…N05) are answered in
[`review-A8-disposition.md`](review-A8-disposition.md).

- **DESAT during latched ASC.** NSI6611 Fig. 8.11 shows a faulted driver holding its own gate off
  through IN-low, EN-low and ASC until a reset edge after its mute time. The healthy low sides,
  however, stayed on through ASC. A ₹5 AND gate (UASCG) now makes any latched FLT mask ASC on every
  path, so a DESAT always ends in SPO. FW-15 always clears the ASC latch before a recovery.
- **Interfaces.**
  - A second 74LVC3G17-Q100 buffers the FLT diode-OR and FS1B strap presets (no slow edge left on
    a flip-flop input). The FLT combining diodes are now BAT46, for the Schmitt threshold.
  - The same buffer drives the discharge opto. Both TLP152 LEDs run through 261 Ω at
    10.3–14.8 mA, inside the recommended 10–15 mA.
  - A FAULT_OUT battery short is clamped to ground (ZSET, BZT52-B5V6: ≤ 6.33 V at a 35 V load
    dump), not into V5A.
- **Firmware / models / tooling.**
  - FW-16 re-specified with every term sensitized.
  - FW-08b keeps HV connected during a DESAT recovery at speed.
  - S4 junction path is static (+3 °C).
  - bom-gen publishes atomically.
- **Cross-check of the fixes** (independent reviewer, R8X-01…17, P-01…06; F98–F105). No CRITICAL
  finding; the three MAJOR ones were contract text:
  - FW-15 always clears the ASC latch.
  - FW-16 runs only on measured no-HV/standstill conditions (otherwise a stored pass arms). It
    handles the FS_GPIO1 flyback OR, and step h injects FLT from the MCU pins, so the latch and mask
    paths are covered in the field.
  - FS1B_TDELAY = 0 and TDUR = 100 ms are required.
  - Hardware, ≈ ₹1 in total:
    - RFCB 100 k: a dead USCH2 reads as FLT.
    - RASCP moved to the latch output: a dead ULAT reads no-ASC.
    - CFLTF2 100 pF on FLT_LS_N.
    - RASCL/RQDL 261 Ω.
    - ZSET changed to B5V6.
    - The discharge header reordered V15-GND-CMD-GND.
  - ERC now locks the diode-OR and the FS1B back-feed by net, and the safety MPNs per SKU.
  - S4 also reports the static-plate bound (8XX IGBT 142 °C, WARN until the thermal test).
- **Cost:** +₹18/unit.

## 11g. Rev A.7 — review round seven (summary)

Two independent rechecks of `c963794` (RR01–RR10, A6-R01–R14) are answered line by line in
[`review-A7-disposition.md`](review-A7-disposition.md).

- **Safety-logic interfaces.**
  - The A.6 RC timing nodes drove LVC inputs at 14,000–63,500 ns/V; the limit is 5–10 ns/V.
    They now pass through a 74LVC3G17-Q100 Schmitt buffer, as does the FS0B line.
  - FS0B/FS1B pull-ups are 5.1 k, so FS1B stays inside its 2 mA V_OL point.
  - The FAULT_OUT load is now specified.
  - Both latches are AEC-Q100 parts.
- **ASC break-before-make.** A 12 nF delay holds the low sides off ≥ 3.4 µs after the latch sets;
  the high sides are already off (FS0B with the MCU dead, the eFlexPWM fault with it alive).
  A commanded ASC is held as PWM-ASC with EN high, so the low-side DESAT stays active. A draft
  that dropped EN from the latch was removed after the cross-check (DS §8.12). The ASC opto LED
  is driven ≥ 10 mA (guaranteed turn-on 7.5 mA), and each LS ASC pin has its own 1 k.
- **FAULT_OUT is sink-only.** A grounded vehicle wire can no longer preset ASC; a KL30 short is
  clamped at the latch.
- **Boot order.** FS1B is asserted at every POR, so the ASC latch is decided from speed before
  gate power comes up.
- **Read-backs.** DRV_EN and ASC_CMD are read by the MCU for a boot self-test.
- **Gate supply.**
  - The flyback senses its own aux rectifier, so the start feed can no longer lock it out at a
    high KL30.
  - The rail equation now counts both diodes, the peak-current drop and the FB bias: 15.4 V
    nominal, 13.54–16.7 V. It is a bench gate, because the low corner sits at the window edge.
  - The boost output capacitors are 50 V.
- **Firmware-owned, specified:**
  - FW-06 as a measured 15.6 + 7.0 µs budget;
  - locked eFlexPWM fault inputs plus WD_ERR_LIMIT = 2 against re-pulsed clears;
  - no automatic retry after DESAT;
  - the 1.1 kHz loop ceiling at 8 kHz.
- **Models.** Shared-coldplate thermal coupling; the flyback bank inductance; the SC soft-off
  from the high-corner rail, now a release gate at both I_STO corners.
- **Cost:** +₹57/unit, of which +₹30 corrects the debug header.

## 11f. Rev A.6 — review round six (summary)

- **Calculation layer rebuilt** (R-F01/F02/F09/F14/F25/F26/F32): overshoot in SI units on the
  DS fall time, exact SiC conduction (½·I²·R), IGBT diode die, published P(V_dc) envelope,
  corrected S5/S10, per-SKU current-loop ceilings — one shared loss model.
- **Hardware (eleven low-cost changes, +₹31/unit):** SiC gate 3.3/6.8 Ω; IGBT blanking 82 pF
  (6 µs SC rating); flyback start 2.2 k/47 µF + 18 V VDD clamp (the A.5 values never started
  at 9 V); fault-latch → AND delay 10 k/3.3 nF; hardware one-shot clear; bleeder 2 × 6 × 22 k;
  VOFS and HW_ID on MCU ADC pins; resolver shields to the connector ground; IGBT RT 8.2 k.
- **Rejected with evidence:** the "fault-dominant latch" fix (deadlocks the NSI6611 FLT reset);
  a brake chopper, contactors, duplicate V_DC chains, a hardware OV comparator, high-side ASC,
  an HV backup bias by default (motor-dependent option in the contract).
- **Platform:** one set of boards builds 8XX SiC, 8XX IGBT, 4XX IGBT (and 4XX SiC on request);
  see [`variants.md`](variants.md).

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

**Regeneration / battery-disconnect strategy (review §5.4; sharpened in rev A.6, R-F15)** —
the discharge resistors are *shutdown bleeders*, not a regen dump, by design. The reaction to
contactor opening under regeneration is **ASC** above the motor's crossover speed and
three-phase-open below it — but the time budget is short: at the SKU's full 220 kW the link
charges at ≈0.9 V/µs and reaches the 880 V trip 34 µs after the battery path opens. So it runs
on the S32K396 ADC **hardware compare** into the ASC request on a free-running V_DC conversion
slot, **≤ 20 µs** (FW-06; rev A.6 N9 — the earlier 100 µs budget, sized at 100 kW, would end at
962 V, 96 % of the cans' 85 °C rating), not in a scheduled task. The discharge path only ever
handles the stored 116 J.

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

## 11c. Rev A.4.2 — review round three

Six further defects confirmed against primary datasheets and fixed (F52–F57): the FS26
VCORE buck network (inductor to a listed OTP selection, effective output capacitance on
V15S/VPRE/input closed with derating math in the report), the ACT45B CAN-choke winding
mapping (real windings 1-4 / 2-3 — both ports had CANH crossed onto CANL), the ALM2402
resolver amplifier moved behind ULDOEX (12.1 V; the part is 18 V abs and VBATC can see
24 V), completion of the ULDO15 feed-forward compensation (onsemi Cb requirement for the
ADJ version with ceramic output), the flyback switch corrected to BUK7Y14-80E
(standard-level ±20 V gate — the BUK9Y logic-level part was outside abs-max at the 11.8 V
drive, and the 5.6 V zener that masked it conducted ~0.29 A through every ON interval),
and the LDO ordering code (NCV4276CDTADJRKG).

Accepted qualifications from the same round, on record:
- **VREF5 rail capacitance** is judged as a rail (CSB5 + CMA1 + CMA2 ≈ 2.2 µF effective,
  inside the 1.1–3.3 µF window) — not one capacitor at a time.
- **ASC with VCC1 lost**: per the NSI6611 DS, DESAT protection is unavailable when VCC1 is
  open while ASC still operates — so "a faulted switch is never forced on" holds only with
  VCC1 present. Classification corrected in round 4: a UGDL open-output or a V5GD
  interconnect break is a SINGLE initiating failure that removes VCC1 (the KL30-held
  enable is availability, not redundancy). VCC1-UVLO parks the driver outputs low —
  three-phase open — and ASC remains commandable per the DS, so the safe states stay
  reachable; the
  case is added to the ASC bench matrix (entry/exit, VCC1 brown-out, VCC2 UVLO ride).
  *Superseded in round 9 (A8-01):* FLT/RDY now pull up to V5GD itself, so a V5GD loss reads as FLT
  and ends deterministically in SPO with ASC masked (contract §6 V5GD row, release rule).
- **ULDO15/ULDOEX in TSD** (sustained >20 V LV) suspends active discharge, ASC bias and
  resolver excitation — availability is not credited in that stationary service state.
- **IGN_SNS disposition**: ADC input (PTA25 as ADC channel), firmware thresholds; the
  47 k/10 k scaling makes 9–16 V read 1.46–2.68 V.

## 11e. Rev A.4.4 — round five: procurement binding

No electrical change. The generic class identifiers flagged in review round 5 are bound to
real orderable parts, each verified against its manufacturer datasheet before binding:
LCOR → Coilcraft XAL4020-222 (2.2 µH, AEC-Q200; values read from the XAL4000 datasheet),
LB15/LSBC → XAL4040-103 (one line item for both 10 µH positions), PS5B/PS5C →
Murata MGJ2D150505SC with an explicit procurement gate (verify the reinforced
characterization at 850 VDC working and the SIP-7 pin map before PO). One candidate was
evaluated and **rejected during this binding**: RECOM R15P05S/R6.4 — its 6.4 kV rating is a
1-second test voltage; the insulation grade per IEC 62368-1 is *basic* at 250 VACrms
working, which fails the barrier rule for the 850 V bus. PSASC/PSQD stay Mornsun QA01C-18
(6 kVDC, 60950-family approvals; CB-cert verification at PO stays in the BOM note).
The S32K396 package binding remains the standing fabrication blocker.

## 12. References

- Wolfspeed CRD300DA12E-XM3 user guide + discharge PCB (passive 9× CRGP2512F68K network —
  copied verbatim here) + controller schematics
- TI TIDM-02014 (TIDUF23A): safety concept (PMIC SAFE_OUT OR-gate, LVSS straps, dual-MCU ASC
  decomposition), UCC5880/UCC14240 gate-drive pattern
- NXP EV-INVERTERGEN3: S32K396-HPWR-MC control card (SPF-91122), EV-POWEREVBHD2 power board
- hiitio catalog (hiitio.com, Sep-2025): HCS600FH120D3C1 EconoDUAL 3 SiC module line,
  HCM75S12T4K3, module packaging in-house (IATF 16949)
- DC-Modules power-module-platform: NSI6611 verified pin map, drawing pipeline, barrier rules
