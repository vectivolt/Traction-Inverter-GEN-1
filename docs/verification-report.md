# Design Verification Report — rev A.3 (2026-09-09)

End-to-end verification of the 220 kW / 800 V traction inverter at actual operating corners
(V_bus 500–850 V · KL30 9–16 V · 10 kHz · 65 °C coldplate), worst-case component tolerances.
Three independent layers:

1. **Geometric pin-verify** (sheets vs netlist): **1523/1523 pins, 100 %**
2. **Structural ERC audit** (netlist vs design intent, `erc-audit.mjs`): **654 checks, 0 fail, 0 warn**
3. **Numeric verification** (this report, `design-verify.mjs`): **51 PASS · 3 WARN · 0 FAIL** (+7 info)

## Findings log (defects found by this campaign — all fixed in rev A.3)

| # | Severity | Finding | Fix |
|---|---|---|---|
| F1 | **HIGH** | Flyback CS resistor 0.033 Ω vs UCC28C43's 1 V threshold ⇒ 30 A "limit" = no overcurrent protection (value was scaled for the NJW4140's low CS threshold) | 0.22 Ω/1210 ⇒ 4.5 A limit vs 2.4 A worst-case operating peak |
| F7 | **HIGH** | FB divider (18k/15k/1.3k, GEN3 values for the NJW ref) regulates VCC at **5.26 V** with the 2.5 V UCC28C43 reference ⇒ UVLO lockout, gate supply never starts | 75k/15k ⇒ VCC 15.0 V |
| F21 | **HIGH** | Flyback VCC had **no start path** (aux-winding-only feed cannot bootstrap) | 4.7 k trickle-start from the 12 V rail (425 µA @9 V vs 100 µA start spec) |
| F20 | **HIGH** | Five net→net alias `<trace>`s left pins floating on the drawing: gate-bias winding returns (6×), module-NTC returns (3×), MCU temp inputs (5×), card-side TMOD_RTN unterminated | all aliases removed — direct binding; TMOD_RTN star-tied to AGND via RTMR 0 Ω |
| F2 | MED | Bus-to-chassis Y caps specced as **Y2** (250 Vac line class) at an 850 V DC bus | Y1-class 4.7 nF (500 Vac / 8 kV impulse); alt 2×Y2 series / CeraLink |
| F4 | MED | The two "independent" V_DC senses shared **one** bias module (common-cause vs the stated safety mechanism #7) | second reinforced module (PS5C) — channel 2 fully independent |
| F25 | MED | 12 V-node MLCCs were 25 V-rated under a 24 V-standoff TVS that clamps ≈ 39 V in load dump | all KL30-node caps ⇒ 50 V rating |
| F26 | MED | Active discharge (4×560 Ω) = 2.19 s at the R+5 %/C+10 % corner — over the 2 s crash target | 4×470 Ω ⇒ 1.60 s nom / 1.84 s worst |
| F27 | LOW | V_DC divider hit exactly 2.0 V full-scale at 850 V — the OV witness saturated right where it matters | bottom 6.65 k ⇒ 6.2 k (full-scale = 911 V) |
| F28 | **HIGH** | ASC pin driven from an 18 V rail vs **abs max GND2+6 V** (NSI6611 DS 1.2) | 2.2 k series + 5.1 V zener clamp at the ganged pins |
| F29 | MED | Module symbol used symbolic aux pins; HS DESAT sensed the DC+ power terminal | REAL HCS600 pin map (1=G_L…9=HS drain-sense, 10/11=AC); HS DESAT moved to the dedicated aux sense pin |
| F30 | LOW | Gate off-bias −4.3 V is not a HCS600-recommended combo (+15 pairs with −5) | zener split ⇒ +15/−5.1 (also consolidates to the C5V1 already on the BOM) |
| F31 | **HIGH** | UCC28C43's real UVLO is 8.4/7.6 V (SLUS458I) — the gate-power flyback cannot start at 9 V cold-crank | UCC28C40 grade (7.0/6.6 V) |
| F32 | **HIGH** | VGT12EEM's real Lp is 10 µH (not a 200 µH-class part) — at 52 kHz the peak current would hit ~10 A every cycle | oscillator retimed to ~250 kHz (10 k/680 pF); DCM Ipk ≈ 1.2 A |
| F33 | **HIGH** | FB divider targeted 15 V on the NF winding; with NP:NF:NS = 1:1.6:2.9 that drives the secondaries to ~27 V (zener overstress) | 56k/15k ⇒ VCC_reg 11.8 V ⇒ V_sec 21.4 V ⇒ +15.6/−5.1 V rails |
| F34 | **HIGH** | FS26 VMONEXT is a fixed 0.8 V reference — the 10k/18.7k divider fed it 3.26 V = permanent overvoltage fault | 52.3k/10k ⇒ 0.794 V at 5 V nominal |
| F35 | MED | FS0B/FS1B low-side outputs clamp at 4–22 mA; 120 Ω pull-ups forced 42 mA | 1 k pull-ups (4.6 mA) |
| F36 | MED | S32K39 core is 1.14 V via an external NMOS ballast from a 1.5 V rail (DS Table 11) — direct FS26-VCORE→V11 is not a supported topology | VCORE→V15S 1.5 V + SQ2310ES ballast (GEN3-exact) regulated by the MCU's BCTRL loop |
| — | LOW | Hall ratiometric reference (V5A) ≠ ADC reference (VREF5): ±1–2 % gain drift between two 5 V rails | accepted (GEN3-identical); EOL calibration note |
| — | LOW | ALM2402 resolver swing at 9 V cold-crank ≈ 7 Vpp vs 8 Vpp target | accepted — amplitude-invariant demodulation |

## Margin tables


### Power stage

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| SVPWM power capability @700 V | 247.8 kW avail | 220 kW target | ✅ PASS | 89% of limit |
| Per-switch loss (220 kW pk / 120 kW cont) | 257.8 W / 106.6 W | - | ℹ️ |  |
| Tj at 220 kW peak (65 °C plate) | 85.9 °C | 175 °C | ✅ PASS | 49% of limit · steady-state bound; 30 s Zth is lower |
| Tj continuous 120 kW | 73.6 °C | 175 °C | ✅ PASS | 42% of limit |
| Module switch RMS current | 154.4 A | 600 A rating (620 A @Tc60) | ✅ PASS | 26% of limit |
| Vds peak (850 V + 20 nH · 5 kA/µs) | 950 V | 1200 V | ✅ PASS | 79% of limit |

### DC link

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| Ripple per can, continuous | 7.2 A | 15.4 A | ✅ PASS | 47% of limit |
| Ripple per can, 30 s peak | 13.2 A | 15.4 A DS rating @10 kHz/70 °C | ✅ PASS | 86% of limit · inside the CONTINUOUS rating — no transient allowance needed |
| Voltage margin | 850 V | 1100 V | ✅ PASS | 77% of limit |
| Bus ripple voltage (capacitive term) | 5.24 V pk | - | ℹ️ | ESL/busbar dominates in layout |
| ESR heating per can, continuous | 0.4 W | ~2 W film-can class | ✅ PASS | 20% of limit |
| Stored energy @850 V | 115.6 J | - | ℹ️ |  |

### Discharge

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| Passive 850→60 V, worst (R+5 %, C+10 %) | 66.1 s | 120 s service rule | ✅ PASS | 55% of limit · nominal 57.3 s |
| Bleeder W/resistor @850 V (R−5 %) | 1.13 W | 2 W | ✅ PASS | 56% of limit |
| Bleeder V/resistor @850 V | 170 V | 200 V working (std 2512) | ✅ PASS | 85% of limit · 700 V nom ⇒ 140 V (70 %) |
| Active 850→60 V, worst (R+5 %, C+10 %) | 1.84 s | 2 s crash target | ✅ PASS | 92% of limit · nominal 1.59 s · 5 s R100 ⇒ 36.8 % |
| Energy per 10 W wirewound (C+10 %) | 31.8 J | 100 J single-pulse | ✅ PASS | 32% of limit |
| V per wirewound @850 V | 212.5 V | ≥350 V axial class | ✅ PASS | 61% of limit |
| QDIS stress | 0.45 A pk · 0.015 W | 1200 V / 42 A part | ✅ PASS | fully-enhanced switch, no linear region |
| Both paths together 850→60 V | 1.55 s | - | ℹ️ |  |

### Gate drive

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| VCC2 (+15) vs UVLO-rising MAX | 15.6 V | 12.8 V | ✅ PASS | 82% of limit · flyback ±5 % ⇒ 14.25 V worst, still above 12.8 V |
| VCC2 worst vs recommended-min | 14.25 V (−5 %) | 13 V rec-min | ✅ PASS | 91% of limit · trim flyback to 15.2 V nom if bench shows droop |
| VCC2−VEE2 span | 20.7 V | 32 V recommended (35 abs) | ✅ PASS | 65% of limit |
| Peak gate current demand | 8.3 A | 10 A driver | ✅ PASS | 83% of limit |
| Gate power per switch @10 kHz | 0.25 W | - | ℹ️ |  |
| Per-domain bias load | 0.36 W | ~1.5 W per secondary budget | ✅ PASS | 24% of limit |
| Positive gate clamp (18 V zener + Vf) | 18.8 V | +22 V abs Vgs | ✅ PASS | 85% of limit |
| Negative gate clamp (5.1 V zener + Vf) | −5.9 V | −10 V abs Vgs | ✅ PASS | 59% of limit |
| HS DESAT sense point | module aux drain pin 9 (DSH) | - | ✅ PASS | F29 — real HCS600 pin map; kelvin sensing, no busbar drop in the trip level |
| ASC drive level | 5.1 V clamp at ganged pins | GND2+6 V abs | ✅ PASS | F28 — 2.2 k + zener from the 18 V opto rail |
| DESAT trip at switch | 6.8 V ≈ 1.2 kA | - | ℹ️ | detects hard faults, not overload — halls cover overload |
| DESAT blanking (47 pF) | 1.15 µs | ≤3 µs SiC SCWT | ✅ PASS | 38% of limit · ≥0.5 µs needed to ride through turn-on |
| Shoot-through lockout | IN+/IN− complementary pairing | - | ✅ PASS | verified structurally in erc-audit (12 checks) |

### Flyback

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| VCC regulation point on NF (56k/15k) | 11.83 V | 20 V abs | ✅ PASS | 59% of limit · F33 — a 15 V target through NF would push the secondaries to ~27 V |
| Derived gate rail VCC2 | 15.6 V (+/−5.1) | 13–32 V driver window | ✅ PASS | 83% of limit · Vsec 21.4 V; UVLO-max 12.8 V cleared |
| Switching frequency (10k/680p) | 252.9 kHz | - | ℹ️ | F32 — Lp 10 µH demands small per-cycle energy; osc anchors per SLUS458I curves |
| DCM peak current vs CS limit | 1.18 A op | 3.03 A limit (0.33 Ω) | ✅ PASS | 39% of limit · F1+F32 — real Lp: energy/cycle ½·10µ·Ipk² |
| CS limit as the saturation guard | 3.03 A | 4.5 A (Isat unpublished — guard band) | ✅ PASS | 67% of limit · bench-verify core at current limit |
| Trickle-start current @9 V KL30 | 255.3 µA | 100 µA start | ✅ PASS | 39% of limit · F31 — UCC28C40's 7.8 V max UVLO-on leaves 255 µA; the C43 grade left NOTHING |
| CS resistor power | 0.153 W | 0.75 W (1210) | ✅ PASS | 20% of limit |

### LV

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| V15 boost setpoint (110k/9.53k) | 15.41 V | 13.5–16.5 V module window | ✅ PASS | 28% of limit · QA01C/ISO5V input range |
| Boost switch current @9 V | 0.72 A avg | 5.25 A limit | ✅ PASS | 14% of limit |
| NCV4276 5 V load (6 driver VCC1 + optos) | 50 mA | 400 mA | ✅ PASS | 13% of limit |
| FS26 VMONEXT divider (52.3k/10k @5 V) | 0.794 V | 0.8 V fixed reference ±window | ✅ PASS | 6% of limit · F34 — old 10k/18.7k fed 3.26 V = permanent OV; OTP window set around 100 % |
| FS0B/FS1B pull-up sink current (1 k) | 4.6 mA | 22 mA low-side clamp | ✅ PASS | 21% of limit · F35 — 120 Ω would have forced 42 mA |
| S32K39 core topology | FS26 VCORE→V15S 1.5 V → QBAL ballast → V11 1.14 V | - | ✅ PASS | F36 — per DS Table 11; direct VCORE→V11 is not a supported topology |
| NCV4276 dissipation @12 V | 0.35 W | ~1.5 W DPAK on copper | ✅ PASS | 23% of limit |
| Polyfuse hold (worst chain @9 V) | 1.19 A | 3 A hold | ✅ PASS | 40% of limit |
| Load-dump path | TVS 24 V standoff, clamp ~39 V | - | ✅ PASS | F25 — all 12 V-node MLCCs raised to 50 V rating; TPS55340 Vin abs 45 V rides the clamped pulse |

### Sensing

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| VDC divider @850 V (6.2 k bottom) | 1.865 V | 2 V AMC FS (= 911 V readable) | ✅ PASS | 93% of limit · OV witness keeps headroom above V_bus,max — F27 |
| VDC chain accuracy (RSS, uncal) | ±1.01 % | 5 % cross-check window | ✅ PASS |  |
| Divider dissipation @850 V | 255.6 mW total | 6× 1206 (250 mW ea) | ✅ PASS | 17% of limit · 141.7 V per 200 V-rated 1206 — 71 % |
| Hall output at 480 A pk | 3.57 V | 0.3–4.7 V buffer swing | ✅ PASS | 49% of limit |
| Hall ratiometric ref vs ADC ref | V5S(V5A) vs VREF5 | - | 🟡 WARN | two 5 V sources — ~±1–2 % gain drift between them; calibrate at EOL or move VREFH to V5A (GEN3 ships the same topology) |
| HVIL signatures (drive hi/lo/open) | 3.0 / 2.0 / 2.5 V | - | ✅ PASS | distinct at ±5 % R tolerance (worst separation 0.38 V) |
| Resolver monitor dividers @4 V pk | 2.83 / 3.31 V | 5 V SDADC input | ✅ PASS | 66% of limit |
| Resolver drive @9 V KL30 | ≈7 V pp available vs 8 V pp target | - | 🟡 WARN | ALM2402 swing limit at cold-crank — angle still tracks (amplitude-invariant demod); GEN3 identical |

### Safety

| Check | Value | Limit | Verdict | Margin note |
|---|---|---|---|---|
| FS0B → driver EN path | 2 gate delays (~20 ns) + driver td | - | ✅ PASS | no software; erc-verified topology |
| ASC latch power | V5A + RASCP default-low | - | ✅ PASS | survives MCU reset; FS1B can SET via RFS1 |
| ASC drive path | TLP152 + QA01C + 5.1 V clamp, DCN-referenced | - | 🟡 WARN | DS 1.2 confirms ASC forces OUTH high at a GND2-referenced 0-5 V pin (F28 level fix applied); behaviour DURING VCC2-UVLO is unspecified — bench-verify that gate power (SBC-held flybacks) is sufficient for ASC hold |
| Default-OFF discipline | 11 pulldowns power + 4 card | - | ✅ PASS | erc-verified |

## Assumptions pending datasheet extraction

Constants tagged *assumed* above (module Rth/Esw class values, NSI6611 UVLO/DESAT/RDY drive
type, C3D ripple rating, VGT12EEM Lp/Isat/turns, LEM sensitivity, UCC28C43 exact thresholds)
are being replaced by extracted datasheet values in `docs/datasheets/EXTRACTED-PARAMS.md`;
any check whose verdict changes will be re-flagged. The two genuinely open items remain the
**NSI6611 ASC-vs-EN behaviour** and the **hiitio module aux-pin drawing** (VERIFY list).

## Method

Closed-form worst-case analysis (tolerance corners: R ±5 %, precision ±1 %, C +10 %,
KL30 9–16 V, V_bus to 850 V) — the correct tool at schematic phase; SPICE adds nothing
without vendor switch models, and the double-pulse/discharge/thermal items are already
flagged for bench verification at EVT in `docs/design-basis.md`.
