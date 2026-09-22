# Extracted datasheet parameters — Traction Inverter

Source PDFs live in this directory. Citations reference the section/table of each datasheet.
Rev A.5 refresh: facts updated to the released part set (F1–F61 closed); superseded parts'
sections are replaced, and their PDFs are retained only where they serve as review evidence.
Rev A.6: rows marked (A.6) were re-extracted for the round-6 review (NSI6611 DESAT/soft-off/FLT
reset, UCC28C40 UVLO corners, AMC1311B, HCS600 switching table, HCG600 SC 6 µs, HCH900 hybrid).

## 1. NSI6611A-Q1 (NSI66x1A-Q1 datasheet EN 1.2, `NSI6611A-Q1.pdf`)

| Parameter | Value | Citation |
|---|---|---|
| RDY pin output type | **Open-drain**, active low (reports UVLO). "Open drain low output voltage" V_RDY_L ≤ 0.3 V @ I_SINK = 5 mA | §6.1 DC Char., "RDY Reporting"; pin table §1 |
| FLT pin type | **Open-drain**, active low (reports overcurrent/short). V_FLT_L ≤ 0.3 V @ I_SINK = 5 mA | §6.1 DC Char., "FLT Reporting" |
| VCC2 UVLO rising (VCC2_ON) | 9.8 / 11.2 / 12.8 V (min/typ/max) | §6.1, Driver Side Supply |
| VCC2 UVLO falling (VCC2_OFF) | 9.0 / 10.4 / 11.8 V; hysteresis 0.8 V typ | §6.1, Driver Side Supply |
| DESAT detection threshold V_DESAT_TH | 8.5 / 9.3 / 10 V | §6.1, Desaturation |
| DESAT internal blanking | Leading-edge blank time 200 ns typ + deglitch filter 100/200/320 ns | §6.1, Desaturation |
| DESAT charge current I_CHG | 350 / 500 / 650 µA (blanking-cap charge, V_DESAT = 2 V); discharge 10/15 mA | §6.1, Desaturation |
| ASC pin function | Driver-side input (pin 1, NSI6611ASC only). ASC = high forces OUTH high regardless of input side. Priority: above VCC1 UVLO, **below** VCC2 UVLO and DESAT. V_ASCH 2.7–3.2 V, V_ASCL 1.3–1.7 V; abs max GND2−0.3 to GND2+6 V | §8.11; §6.1 ASC Pin Characteristic; §2 abs max |
| Recommended VCC2 range | VCC2−GND2 = **13 to 32 V**; VCC2−VEE2 ≤ 32 V; VEE2−GND2 abs −17.5 to +0.3 V | §4 Recommended Operating Conditions |
| Peak source / sink current | I_OUTH 11 A typ source; I_OUTL 12 A typ sink (8 A at V_OUT = VEE2+2.5 V); pulse < 10 µs | §6.1 Output Pin Characteristic |
| Abs max VCC2−VEE2 | **35 V** (−0.3 min) | §2 Absolute Maximum Ratings |
| DESAT timing (A.6) | LEB 200 ns typ · deglitch 100/200/320 ns · DESAT→OUT 90 % low 150/250/360 ns (0.1 nF load) · DESAT→FLT low 400/650/800 ns | §6.1 Desaturation |
| Soft turn-off (A.6) | I_STO **100/400/570 mA** (one level; duration not specified) | §6.1; §8.9 |
| FLT latch / reset (A.6) | FLT held low until an **RST/EN rising edge** after RST/EN low ≥ t_FLT_MUTE **0.55–1.3 ms**; resets ignored during the mute time; no auto-clear. RST/EN low during an ongoing soft turn-off: **NOT STATED** | §8.10; §9.4; §6.1 |
| RST/EN pin (A.6) | active-high enable, internal ≈50 k pull-down (Fig 8.1); V_INH 2.5/2.9/3.5 V, V_INL 1.5/2.1/2.5 V; t_RST_FIL 480/600/800 ns | §6.1; §8.x |
| ICC2 / R_OH / R_OL (A.6) | ICC2 1/3.3/7 mA (OUT high); R_OH 2.2 Ω, R_OL 0.3 Ω typ @0.1 A; peak sink 8 A at V_OUT = VEE2+2.5 V | §6.1 |
| ASC priority (A.6) | VCC2 UVLO > DESAT > ASC > VCC1 UVLO; ASC overrides RST/EN and IN; with VCC1 open DESAT is not available | §8.11; §8.12 |

## 2. UCC28C4x family — **design uses UCC28C40DR** (SLUS458I covers C40–C45, `UCC28C43.pdf`)

| Parameter | Value | Citation |
|---|---|---|
| FB (error-amp) reference | 2.5 V ±1 %: V_FB 2.475/2.5/2.525 V @25 °C; 2.45–2.55 V over temp | §6.5 Electrical Char. (Error Amplifier) |
| CS threshold V_CS | 0.9 / 1.0 / 1.1 V (max input signal, V_FB < 2.4 V) | §6.5, Current Sense |
| VCC UVLO on/off | **UCC28C40/41 grade (used): VDD_ON 7.0 V typ (7.5 V max), VDD_OFF 6.6 V typ** — the C43/45 grade (8.4 V start) cannot start at 9 V crank, F31; C42/C44 start at 14.5 V. (A.6 re-read: the earlier "7.8 V max" was not the table value) | §6.5, Undervoltage Lockout |
| Abs max VCC | VDD = 20 V abs max (I_VDD 30 mA); recommended operating VVDD ≤ 18 V | §6.1 Abs Max; §6.3 Rec. Operating |
| Oscillator formula | **No closed-form equation given.** CCT charged from VREF (5 V) through RRT between 0.7 V and 3.0 V thresholds; trimmed 8.4 mA discharge sink. Frequency set from curves "Oscillator Frequency vs Timing Resistance and Capacitance" (Fig. 6-13). Anchors: RRT=10 kΩ + CCT=3.3 nF → f_OSC 50.5/53/55 kHz (spec); RRT=15.4 kΩ + CCT=1 nF → 110 kHz (design example). (These fit f ≈ 1.72/(R_RT·C_CT) within a few %.) | §7.3.5 Oscillator, Fig 7-4; §6.5 Oscillator; §7.3.1.4 |
| Max duty cycle | D_MAX 94–96 % (UCC28C42/43/40); D_MIN 0 % | §6.5, PWM |
| C40 UVLO corners (A.6) | VDD_ON **6.5/7.0/7.5 V**, VDD_OFF **6.1/6.6/7.1 V** (≈0.4 V hysteresis — one start burst lasts C_VDD·0.4 V / I_run) | §6.5 Undervoltage Lockout |
| Start / run current (A.6) | I_START 50/100 µA at VDD_ON−0.5 V; IVDD 2.3/3.0 mA no load (+ Qg·f of the FET) | §6.5 Current Supply |
| VDD clamp (A.6) | **none internal** — "the VDD pin must be protected from external sources which could exceed the 20 V level" | §7.3.1.7 |

## 3. AMC1311 / AMC1311B (`AMC1311.pdf`)

| Parameter | Value | Citation |
|---|---|---|
| Pin map (DWV) | 1 VDD1 · **2 IN · 3 SHTDN (active-high, internal ~100 kΩ pull-up — tie to GND1)** · 4 GND1 · 5 GND2 · 6 OUTN · 7 OUTP · 8 VDD2 — F47 | Table 6-1 Pin Functions |
| VDD1 range | AMC1311: 4.5–5.5 V; AMC1311B: 3.0–5.5 V (abs max 6.5 V) | §7.3 Rec. Operating (Table 7-3); §7.1 |
| Input range | V_FSR specified linear full-scale = **−0.1 to +2 V** (IN to GND1); high-impedance input R_IN 1 GΩ | §7.3; §7.5 |
| Gain error | AMC1311: ±1 % max (0.4 % typ); AMC1311B: ±0.2 % max (±0.05 % typ), @25 °C | §7.5 Electrical Char., E_G |
| Output common-mode | V_CMout 1.39 / 1.44 / 1.49 V | §7.5, Analog Output |
| Fail-safe output on VDD1 loss | V_FAILSAFE = −2.6 to −2.5 V differential (active when SHTDN high, VDD1 undervoltage, or VDD1 missing). VDD1UV rising 2.5/2.7/2.9 V | §7.5; §8.3.2 Fail-Safe Output |
| Beyond the linear range (A.6) | V_Clipping 2.516 V typ: between 2.0 and ≈2.5 V the output keeps rising with reduced linearity, then clips (V_CLIPout 2.49 V) | §7.3; §8.3.3 |
| AMC1311**B** (the fitted DWVR grade) | gain error ±0.2 % max, offset ±1.5 mV max, BW ≥220 kHz | §7.9 |

## 4. Faratronic C3D 20 µF / 1100 V (C3D1M206KFSA382, `C3D1M206KFSA382.pdf`)

| Parameter | Value | Citation |
|---|---|---|
| Rated ripple current @10 kHz | I_max = **15.4 A rms** (f = 10 kHz, Θ_amb = 70 °C, ΔΘ_case = 15 °C) | 技术参数 (Technical Parameters) table + note 1 |
| ESR @10 kHz | **7.8 mΩ** (tanδ 15×10⁻⁴ @1 kHz, 140×10⁻⁴ @10 kHz) | Technical Parameters table |
| dV/dt | **37 V/µs** | Technical Parameters table |
| (Ratings context) | U_N,85°C = 1000 Vdc; U_N,70°C = 1100 Vdc; 20 µF; 37.5 mm pitch | Technical Parameters table header |

## 5. BUK7Y14-80E (Nexperia, `BUK7Y14-80E.pdf`) — flyback switch since rev A.4.2 (F56)

| Parameter | Value | Citation |
|---|---|---|
| V_GS | **−20 to +20 V DC** (standard-level — legal at the 11.8 V VDD drive) | §6 Limiting values |
| V_DS / I_D | 80 V; 65 A @ V_GS = 10 V, T_mb = 25 °C | §6 Limiting values |
| R_DS(on) | 9.2 mΩ typ / 14 mΩ max @ V_GS = 10 V, I_D = 15 A, 25 °C | §8 Characteristics |
| Q_G(tot) | 44.8 nC typ (Q_GD 12.9 nC) @ I_D = 15 A, V_DS = 64 V, V_GS = 10 V | §8 Dynamic characteristics |
| Superseded part | BUK9Y14-80E (`BUK9Y14-80E.pdf`, retained as F56 evidence): **V_GS ±10 V DC only** — outside the drive; its 5.6 V gate "clamp" conducted every ON interval | its §6 Limiting values |

## 6. TDK VGT12EEM-200S1A4 (`VGT12EEM-200S1A4.pdf`, VGT series catalog p.3/9)

| Parameter | Value | Citation |
|---|---|---|
| Turns ratio | NP : NF : NS = **1 : 1.6 : 2.9** | Characteristics Specification Table |
| Isolation rating | Withstanding voltage NP,NF–NS = **2.6 kVrms/1 min** (sense 1 mA); coil–core 1.3 kVrms/1 min | Characteristics Specification Table |
| Primary inductance | NP = **10 µH ±20 %** (100 kHz/1 V); leakage ≤ 0.2 µH (NS shorted) | Characteristics Specification Table |
| Power/VA class | **Not specified in datasheet** — described only as "power transformer for IPM drive of motor inverter" (flyback use). No VA/W rating published | Features / Application (catalog p.1) |
| Winding arrangement | NP1∥NP2 between pins 1–2 with **dots at pin 2**; NF pins 3(dot)–4 (primary-side aux); NS between **pins 8(dot)–5 only — pins 6/7 have NO internal connection**. Flyback rule: secondary rectifier hangs on pin 8 (F37) | Circuit Diagram, catalog p.3/9 (read at 300 dpi) |

## 7. LEM HC5FW 900-S (`HC5FW-900-S.pdf`, HC5FW 900-S/SP1, 17-Jan-2020 V0)

| Parameter | Value | Citation |
|---|---|---|
| Sensitivity | G = **2.22 mV/A** typ @ U_C = 5 V (ratiometric: V_out = (U_C/5)·(V_O + G·I_P)) | Operating characteristics table p.3; note 1 |
| V_out @ 0 A | V_O = **2.5 V** typ (@25 °C, U_C = 5 V, hysteresis included) | Operating characteristics table p.3 |
| Supply current | I_C = 19 mA typ / 25 mA max @ U_C = 5 V | Operating characteristics table p.3 |
| Accuracy | Global accuracy @0 A: ±13 (table row X_G, @25 °C incl. hysteresis); sensitivity error ±0.6 %; linearity ±1 % FS; electrical offset ±2.5 mV; magnetic offset ±2 mV; TCV_OE ±0.08 mV/°C; TCG ±0.03 %/°C | Performance Data table p.3 |
| Bandwidth | BW ≥ **40 kHz** (−3 dB); step response 2–6 µs to 90 % @ 100 A/µs | Performance Data table p.3 |
| (Ranges) | I_PM = ±900 A measuring range; U_C = 4.75–5.25 V | Electrical Data table p.3 |
| Insulation (N7) | **"Cover without sleeve (reduced insulation)"**; family intro: "low voltage application" | Special feature + Features, p.1 |
| U_d / creepage / clearance / CTI | 2.5 kV rms 50 Hz 1 min (IEC 60664-1) / **3.6 mm** / **2.7 mm** / 550; R_IS ≥ 500 MΩ; no working-voltage rating | Absolute ratings table p.3 |

## 8. HIITIO HCM75S12T4K3 (`HCM75S12T4K3.pdf`, datasheet A2)

| Parameter | Value | Citation |
|---|---|---|
| V_GS recommended | **−5 / +18 V** (V_GS op) | Maximum ratings table |
| V_GS abs max | **−10 / +22 V** | Maximum ratings table |
| V_th range | V_GS(th) = 2.0 / 3.1 / 4.0 V (min/typ/max) @ V_DS = V_GS, I_D = 5 mA, 25 °C; 2.2 V typ @175 °C | Static characteristics table |
| R_DS(on) | **69 mΩ typ / 90 mΩ max** @ V_GS = 18 V, I_D = 20 A, Tvj = 25 °C; 111 mΩ typ @175 °C | Static characteristics table |

## 9. TLP152 (Toshiba, `TLP152.pdf`)

| Parameter | Value | Citation |
|---|---|---|
| VCC operating range | **10–30 V** (abs max 35 V) | Recommended Operating Conditions; Abs Max Ratings |
| Peak output current | ±2.5 A abs max (I_OPH/I_OPL, exp. waveform ≤0.2 µs); recommended max ±2.0 A; measured I_OPH −2.2 A typ / −1.0 A min, I_OPL +2.4 A typ / +1.0 A min @ VCC = 15 V | Abs Max; Rec. Operating; Electrical Char. |
| UVLO | V_UVLO+ = 7.8/8.7/9.7 V rising; V_UVLO− = 7.5/8.4/9.4 V falling; hysteresis 0.3 V typ | Electrical Characteristics |
| Isolation (N8) | BVS **3750 Vrms / 60 s** (UL1577, file E67349); creepage/clearance **5.0/5.0 mm**, DTI 0.4 mm; **no V_IORM/V_IOWM**; VDE EN 60747-5-5 only with the **(V4)** option; CMTI ±20 kV/µs | Abs Max + Isolation Characteristics p.3–4; Mechanical Parameters p.2; Safety standards p.1 Note 1 |

## 10. S32K39x (`S32K39.pdf` = S32K39/S32K37 Data Sheet Rev.3 03/2024, 119 pp)

| Parameter | Value | Citation |
|---|---|---|
| VDD_HV supply range | VDD_HV_A: **2.97–5.5 V** (nominal 3.3 or 5.0 V); VDD_HV_B same; VDD_HV_B = 5 V only allowed when VDD_HV_A = 5 V | Table 4 (Voltage and current operating requirements); Table 5 (use-cases) |
| V11 core spec | V11 = **1.14 V** typ, generated by on-chip regulator driving an **external NMOS ballast transistor** from the V15 rail (1.5 V); external NMOS: Vth ≤ 1.5 V (3.3 V supply) / ≤ 2 V (5 V supply), I_DS ≥ 3 A. Direct external V11 supply is not offered; external sourcing enters at **V15** ("non-SMPS mode where V15 is sourced externally", 1.425–1.65 V). Abs max V11 = 1.26 V | Table 11 (V11 regulator); Table 4 note 3; Table 3 (abs max); §6.4 |
| SD-ADC count | **4 SDADC instances** (16-bit sigma-delta) | Table 1/Table 2 Feature summary ("SDADC instances — 4") |
| Max ADC input | SAR ADC: V_AD_INPUT = VREFL to VREFH (VREFH_SAR up to 5.5 V max; accuracy guaranteed only in this range). SDADC: AVDD 4.5–5.5 V, VREFP = AVDD ±0.025 V; single-ended p-p range VREFP/GAIN; SDADC intended only with VDD_HV_A = 5 V | Table 37 (SAR_ADC); Table 38 (SDADC); Table 4 note 6 |

## 11. FS26 (`FS26.pdf` = FS26 Product data sheet Rev.3 17-Nov-2022, 235 pp)

| Parameter | Value | Citation |
|---|---|---|
| VMONEXT threshold/reference | Expected pin voltage **fixed 0.8 V** (external divider: V_rail·RL/(RL+RH) = 0.8 V). OTP thresholds as % of that: UV setting range 88–95.5 % (0.5 % steps), OV 104.5–112 % (0.5 % steps), detection accuracy ±1.2 % | §External voltage monitoring; Table 185 (VMONEXT monitoring electrical characteristics); OTP CFG_OVUV_11 |
| FS0B/FS1B drive type | Fail-safe **low-side (open) digital outputs, active low, external pull-up required**; internal 2 MΩ pulldown; V_OL ≤ 0.4 V; assertion current limit 4–22 mA; readback sense thresholds V_IL 0.7 V / V_IH 1.5 V | Pin function table (§ pins 12, 13: "Digital output, Open – 2 MΩ internal pulldown"); Table 196 (FS0B safety outputs) |
| VPRE output range | Synchronous buck, OTP-configurable **3.7–6.35 V @ 1.5 A** (VPRE_OTP[5:0]: 0x0A = 3.70 V … 0x3F = 6.35 V) | Block diagram; OTP_VPRE_VOLT1 (0x2B) |
| VCORE output range | Synchronous buck, OTP **0.8–3.35 V in 10 mV steps**, 0.8 A or 1.65 A (per part number), f_sw 2.25 MHz | Table row VCORE "Output voltage (OTP configuration, 10 mV step)"; OTP_CORE_VOLT1 (0x31); §VCORE |
| VSUP operating range | **VSUP_UVL to 36 V** (12 V typ). VSUP_UVH (rising) = 4.6/4.8/5.0 V (VSUP_UVTH_OTP = 0); abs max −1.2 to 40 V | Table 9 (Electrical characteristics); Table 3 abs max |
| TRKIN role (F48) | Input **supply of the VREF regulator** (abs-max group with LDOIN/CORE_IN, −0.3…8.5 V) — from VPRE, never grounded while VREF is used; CIN_TRK ≥0.5 µF eff at the pin | Table 3; CIN_TRK row |
| VCORE passives (F52) | L = 1/1.5/2.2 µH only (CORE_LSEL_OTP; 2.2 µH eff window 1.5–2.9); COUT 20–100 µF **effective**; CBOOT_CORE 47 nF (33–62 eff) | Table 106 |
| VPRE passives (F52) | COUT ≥22 µF (test condition); CBOOT_PRE 22–100 nF (22 typ); CIN ≥10 µF eff; L_VPRE 10 µH ↔ FPRE 450 kHz | Table 100/106; char. conditions |
| VREF COUT | 1.1–3.3 µF **effective** (judge the whole rail: CSB5+CMA1+CMA2) | COUT_VREF row |
| Real LQFP48 pin map | bound in `boards/control-card.tsx` SBC_PINS (F45) — incl. VDIG(21), VBOS(35), bootstraps 33/39, DEBUG(31) grounded via 10 k, unused-boost terminations per DS | Table 3 pin table + unused-pin table |

## 12. TPS55340 (`TPS55340.pdf`)

| Parameter | Value | Citation |
|---|---|---|
| FB reference voltage | V_REF = **1.229 V** (1.204–1.254 over temp; 1.220–1.238 V @25 °C) | §6.5 Electrical Char., Voltage and Current Control |
| Switch current limit | I_LIM = **5.25 / 6.6 / 7.75 A** (N-channel MOSFET, D = Dmax) | §6.5, OCP and SS |
| Real RTE-16 pin map (F45/F49) | SW 1/15/16 · VIN 2 · EN 3 · SS 4 · **SYNC 5 (abs max 7 V — tie to AGND when unused)** · AGND 6 · COMP 7 · FB 8 · FREQ 9 · NC 10/14 → GND · PGND 11–13 | Table 5-1; abs-max table |
| FREQ setting | R_FREQ(kΩ) = 57500·f_sw(kHz)^−1.03 → 80.6 kΩ ≈ 580 kHz | Eq. 1 |
| Compensation (F59) | series R3/C4 on COMP (start 2 kΩ + 100 nF) + optional small C5; f_z 796 Hz, sim PM 72–75° | §8.2.1.2.11 |

## 13. Mornsun QA01C (`QA01C.pdf`)

| Parameter | Value | Citation |
|---|---|---|
| Input voltage window | **13.5–16.5 V** (15 V nominal); surge 21 VDC 1 s max | Selection Guide; Input Specifications |
| Output voltage/current | **+20 V / −4 V dual output, ±100 mA** (76/80 % efficiency). F61: rails historically named V18A/V18Q — clamps verified for +20 V; QDIS gate ≈19.5 V vs +22 abs | Selection Guide |
| Real SIP-7 pins | 1 = Vin, 2 = GND(in), 5 = −Vo, 6 = 0 V, 7 = +Vo (positions 3/4 absent) — F45 | Design Reference figure |
| Isolation | I/O isolation test **3.5 kVAC / 6 kVDC** (6000 VDC @ ≤1 mA leakage); isolation resistance ≥1000 MΩ @500 VDC | Features; Isolation Specifications |

## 14. SMAJ13A (Vishay SMAJ series, `SMAJ-series.pdf`) — flyback primary clamp TVS (F38)

| Parameter | Value | Citation |
|---|---|---|
| V_WM / V_BR | 13 V standoff; **14.4–15.9 V breakdown** (stays dark below the 7.4 V reflected voltage) | ratings table, SMAJ13A row |
| V_C @ I_PP | 21.5 V @ 18.6 A → drain ≤ rail + ~22 V ≤ 60 V worst vs the 80 V FET | ratings table |

## 15. TDK ACT45B (`ACT45B.pdf`) — CAN common-mode chokes (F53)

| Parameter | Value | Citation |
|---|---|---|
| Physical windings | **pins 1–4 and 2–3** ("no polarity") — a 1-2/3-4 mapping crosses CANH onto CANL | Circuit diagram p.3 |
| Ratings | 51–100 µH, 150–200 mA, 50 V DC, AEC-Q200, −40…+150 °C | p.2 |

## 16. NCV4276C (`NCV4276C.pdf`) — V5GD LDO (5.0 fixed) + ULDO15/ULDOEX (DTADJ)

| Parameter | Value | Citation |
|---|---|---|
| Pin map DPAK-5 (F45) | 1 IN · 2 INH (low = off) · 3 GND · **4 NC (fixed) / VA (ADJ)** · 5 OUT | Pin Function Description |
| ADJ range / ref | 2.5–20 V out (VA regulated to 2.5 V); ±2 %; 400 mA; dropout ≤500 mV; **40 V operating / 45 V peak input** | Features; Abs Max |
| ADJ + ceramic COUT | requires **Cb across the upper divider leg** (11–18 kHz zero guidance for the 22 µF example) — 220 pF/49.9 k (ULDO15), 270 pF/38.3 k (ULDOEX) | Fig. 4 note; stability section |
| Ordering codes (F57) | fixed 5.0 DPAK = NCV4276CDT50RKG; **adjustable DPAK = NCV4276CDTADJRKG** | ordering table |

## 17. Coilcraft XAL4020-222 / XAL4040-103 (`XAL4000.pdf`) — LCOR / LB15+LSBC (rev A.4.4)

| Part | Values (from the XAL4000 table) | Use |
|---|---|---|
| XAL4020-222ME | 2.2 µH, DCR 35.2/38.7 mΩ, current columns 5.6/4.0/5.5 A | LCOR (FS26 VCORE, OTP 0x02) |
| XAL4040-103ME | 10 µH, DCR 84/92.4 mΩ, current columns 3.0/2.2/3.1 A | LB15 (boost, I_pk < 1 A) + LSBC (VPRE 450 kHz) |

## 18. PESD family (Nexperia) — real terminal counts (F45)

| Part | Configuration | Use |
|---|---|---|
| PESD5V0L1BA (`PESD5V0L1BA.pdf`) | **2-terminal bidirectional** (K1–K2) | motor-temp line clamps TVSM1/2 |
| PESD5V0U1UA (`PESD5V0U1UA.pdf`) | 2-terminal unidirectional (K=1, A=2) | NTC/HVIL line clamps |
| PESD2IVN24 (`PESD2IVN24.pdf`) | **3-pin dual-line** (1/2 = lines, 3 = common) | CAN pairs + resolver SIN/COS pairs |

## 19. Littelfuse TPSMC24CA (`TPSMC24CA.pdf`) — 12 V-rail TVS (context for F51/F55)

| Parameter | Value | Note |
|---|---|---|
| V_WM / V_BR / V_C | 20.5 V / 22.8–25.2 V / **33.2 V @ I_PP** | clamps the KL30 rails far above 16.5 V (QA01C) and 18 V (ALM2402) — hence ULDO15 + ULDOEX post-regulators |

## 20. TI ALM2402-Q1 (`ALM2402-Q1.pdf`) — resolver exciter (F45/F55)

| Parameter | Value | Citation |
|---|---|---|
| Supply | 4.5–16 V recommended, **18 V abs** on VCC and both VCC_O — fed from VEXD (12.1 V LDO) since A.4.2 | Abs Max; Rec. Operating |
| PWP-14 pin map | 1 IN1− · 2 IN1+ · **3 OTF/SHDN (floating/grounded = SHUTDOWN; open-drain OT flag — pull up through 10 k, never tie stiff)** · 4 IN2+ · 5 IN2− · 6+14 GND · 7/8 NC · 9 OUT2 · 10 VCC_O2 · 11 VCC · 12 VCC_O1 · 13 OUT1 | Pin Functions; Table 1 |

## 21. Nexperia 74LVC1G11 / 74LVC1G32 / TI SN74LVC1G74 — safety-chain logic (F45/F40)

| Part | Real pins (GW / DCU) |
|---|---|
| 74LVC1G11 (AND3) | A=1 · **GND=2** · B=3 · Y=4 · **VCC=5** · C=6 |
| 74LVC1G32 (OR2) | B=1 · A=2 · GND=3 · Y=4 · VCC=5 |
| SN74LVC1G74 (D-FF) | CLK=1 · D=2 · Q̄=3 · GND=4 · Q=5 · CLR̄=6 · PRĒ=7 · VCC=8 (both-low ⇒ both outputs high = ASC-safe) |

## 22. Murata MGJ2D150505SC — PS5B/PS5C bind (rev A.4.4) — **datasheet not yet archived**

Murata's PDF endpoints rejected non-browser downloads at refresh time. **PO gate stands:**
before ordering, archive the datasheet here and verify (1) reinforced-insulation
characterization (IEC 60664-1/62368-1) covering 850 VDC working, (2) the SIP-7 pin map vs
the drawn footprint. Rejected alternative kept as evidence: `RxxP.pdf` (RECOM R15P05S) —
insulation grade **basic**, 250 VACrms working; its 6.4 kV figure is a 1 s test (round 5).

## 23. TE 770669-1 (`TE-770669-1.pdf`) — vehicle connector (F60)

23-position AMPSEAL PCB header, right-angle shrouded; mates 770680-1 plug + 770520 contacts.
`TE-776231-1.pdf` is retained as F60 evidence: that drawing is the **35-position** header
(mates plug 776164) and was the previously-bound wrong part.

## 24a. HIITIO HCS600FH120D3C1 switching data (A.6 — `HCS600FH120D3C1.pdf` p.4/5/8)

| Parameter | Value | Citation |
|---|---|---|
| Switching @600 V/600 A, +18/−5 V, Rg 3.3/3.3 Ω | td(on) 93/81 ns, tr 58/50 ns, td(off) 182/221 ns, **tf 13/22 ns** (25/150 °C); Eon 18.68/17.85 mJ, Eoff 21.30/23.12 mJ | p.4 table |
| Body diode | VSD 6.3/5.6 V @600 A, −5 V gate (25/175 °C); Qrr 1.99/5.87 µC; Err 0.56/1.99 mJ | p.5 table |
| Eoff vs Rg (150 °C, 600 A) | ≈15.5 mJ @1.7 Ω · 23 @3.3 · ≈38.5 @6.8 · ≈55 @10 Ω (Fig.14 read) | p.8 Fig.14 |
| Gate / capacitance | RGint 1.1 Ω; Ciss 34.8 nF; QG 1240 nC (+18/−5); ≈1.09 µC for −5.1→+15.6 V (Fig.11) | p.4; p.7 |
| Module stray inductance | **NOT STATED** — hiitio RFQ item (sets the 850 V overshoot budget) | — |
| Short-circuit withstand | **NOT STATED** — vendor letter / contained SC test | — |

## 24b. HIITIO HCH900FH120D3ME7 (`HCH900FH120D3ME7.pdf`, RevX.0.1) — SiC/Si hybrid, EVALUATED in A.6, not adopted (platform stays SiC + IGBT)

| Parameter | Value | Citation |
|---|---|---|
| Construction / package | IGBT chips + **SiC Schottky diodes**, half-bridge, **152 × 62 mm D3 outline with the identical 11-pin map** (1 G_L, 2 E_L, 3 DC−, 4 DC+, 5/6 NTC, 7 G_H, 8 E_H, 9 HS C-sense, 10/11 AC) | p.1 Fig.1; p.2 Fig.2 |
| Ratings | 1200 V; IC **900 A** @Tc 75 °C; ICM 1800 A (1 ms); VGES ±20 V; Tvjop −40…175 °C (>150 °C only for overload) | p.3 |
| VCEsat (chip) @900 A, 15 V | 1.72 (25) / 2.00 (125) / 2.10 (150) / 2.18 (175 °C) V; lead resistance terminals–chip **0.8 mΩ** | p.4; p.3 |
| Switching @600 V/900 A, +15/−8 V, Rg 2.0 Ω | Eon 46.1/65.4 mJ, Eoff 95.1/104.6 mJ (25/150 °C); tf 142/146 ns | p.4 |
| SiC SBD | VF 1.85/2.60/2.90/3.10 V @900 A (25/125/150/175 °C); Qrr 2.51/1.44 µC; **Err 0.62/0.27 mJ** | p.5 |
| Gate | VGE(th) 5.0–6.5 V; QG 7.6 µC (±15 V), ≈5.4 µC for −5.1→+15.6 V (Fig.13); Cies 133.8 nF; RGint 0.5 Ω | p.4; p.8 |
| Thermal | Rth(j-c) **0.040** IGBT / **0.070** SBD; Rth(c-s) 0.015 / 0.025 (2.8 W/m·K grease) | p.4; p.5 |
| NTC | R25 5 kΩ, B25/50 3375 K (same as HCS600/HCG600) | p.3 |
| Short-circuit withstand | **NOT STATED** (unlike HCG900FH120D3RC, which publishes 3550 A for tP ≤ 8 µs at 600 V/150 °C) | — |

## 24. HIITIO HCG600FH120D3E1EA (`HCG600FH120D3E1EA.pdf`) — IGBT drop-in variant

| Parameter | Value | Citation |
|---|---|---|
| Package / pin map | **D3 (EconoDUAL 3), IDENTICAL 11-pin map to HCS600FH120D3C1** (1=G_L 2=E_L 3=DC− 4=DC+ 5/6=NTC 7=G_H 8=E_H 9=HS C-sense 10/11=AC) | p.8 Package Information |
| VCEsat (terminal) | 1.50 V @25 °C / **1.82 V @175 °C** (600 A, VGE 15 V) | Table 4 |
| VGE / VGE(th) | ±20 V abs; threshold **5.0/5.6/6.2 V** | Tables 3/4 |
| Eon+Eoff | 17.7+74.0 mJ @25 °C; **39.7+104 mJ hot** (Rg 0.51 Ω test) | Table 5 |
| Qg / Cres | **4.36 µC**; 0.49 nF | Table 4 |
| Isc / Tvjop | **1800 A for tP ≤ 6 µs** at Tvj 175 °C, VCC 800 V, VGE 15 V (A.6: the "10 µs class" used before was wrong); Tvjop −40…+150 °C | Table 5 (SC); Table 2 (Tvjop) |
| Diode (A.6) | VF 1.75/1.70 V terminal @600 A (25/175 °C); Erec 20.6/43.2 mJ @0.51 Ω, ≈40 mJ @1 Ω (Fig.11); Qrr 36.8/93.1 µC | Table 4; Table 6 |
| Eon vs Rg (175 °C, 600 A) | ≈40 mJ @0.5 Ω · ≈60 @1 Ω · ≈85 @1.5 Ω · ≈150 @3.3 Ω; Eoff ≈104–115 mJ, flat (Fig.5) | p.5 Fig.5 |
| Stray inductance | NOT STATED | — |
| RthJC | 0.07 K/W per IGBT · 0.10 per diode | Table 3 |
| NTC | B25/50 = **3375 K** | Table 8 |

---

# Archive index (48 PDFs, verified `%PDF` headers)

Every part on the released BOM has its datasheet here, except MGJ2 (see §22 — PO gate).
Evidence files for superseded/rejected parts are kept deliberately and labeled.

| File | Size (bytes) | Note |
|---|---|---|
| 1N4148WS.pdf | 125,797 | OK |
| 2N7002.pdf | 871,278 | OK |
| 74LVC1G11.pdf | 263,386 | OK |
| 74LVC1G32.pdf | 283,909 | OK |
| ACT45B.pdf | 446,768 | OK |
| ALM2402-Q1.pdf | 2,275,634 | OK |
| AMC1311.pdf | 1,782,662 | OK |
| BAT64-04.pdf | 1,142,341 | OK |
| BUK7Y14-80E.pdf | 777,306 | flyback switch since A.4.2 |
| BUK9Y14-80E.pdf | 777,115 | superseded part — F56 evidence |
| BZT52-series.pdf | 133,785 | OK |
| C3D1M206KFSA382.pdf | 541,956 | OK |
| CX3225GA.pdf | 596,614 | OK |
| HCG600FH120D3E1EA.pdf | (see dir) | IGBT drop-in variant module |
| HCH900FH120D3ME7.pdf | 684,436 | SiC/Si hybrid — evaluated in A.6, not adopted (evidence) |
| FS26.pdf | 5,421,631 | OK |
| HC5FW-900-S.pdf | 835,088 | OK |
| HCM75S12T4K3.pdf | 2,538,054 | OK |
| HCS600FH120D3C1.pdf | 671,796 | OK |
| MF-LSMF.pdf | 1,550,688 | OK |
| NCV4276C.pdf | 311,168 | OK |
| NRVBAF360T3G.pdf | 181,198 | OK |
| NSI6611A-Q1.pdf | 1,814,953 | OK |
| OPA333.pdf | 1,944,689 | OK |
| OPA348.pdf | 1,806,662 | OK |
| OPA376.pdf | 1,697,245 | OK |
| PESD2IVN24.pdf | 262,019 | OK |
| PESD5V0L1BA.pdf | 238,471 | OK |
| PESD5V0U1UA.pdf | 216,663 | OK |
| PMEG4010EH.pdf | 185,949 | OK |
| QA01C.pdf | 954,821 | OK |
| RxxP.pdf | 1,000,818 | REJECTED part (basic insulation) — round-5 evidence |
| S32K39.pdf | 2,146,090 | OK |
| SMAJ-series.pdf | 111,272 | Vishay SMAJ family (SMAJ13A clamp) |
| SMBJ-series.pdf | 129,599 | family doc (historical SMBJ85A analysis) |
| SN74LVC1G74.pdf | 1,427,201 | OK |
| SQ2310ES.pdf | 207,232 | OK |
| SS34.pdf | 1,055,136 | OK |
| STPS5L60.pdf | 485,990 | OK |
| TCAN1042.pdf | 1,356,386 | OK |
| TE-770669-1.pdf | 420,928 | JVEH 23-pos AMPSEAL header drawing |
| TE-776231-1.pdf | 500,402 | wrong 35-pos header — F60 evidence |
| TLP152.pdf | 354,551 | OK |
| TPS55340.pdf | 2,932,213 | OK |
| TPSMC24CA.pdf | 972,042 | OK |
| UCC28C43.pdf | 3,682,285 | SLUS458I family doc; design uses UCC28C40DR |
| US1M.pdf | 176,802 | OK |
| VGT12EEM-200S1A4.pdf | 404,556 | OK |
| XAL4000.pdf | 210,340 | Coilcraft XAL40xx family (LCOR, LB15, LSBC) |
