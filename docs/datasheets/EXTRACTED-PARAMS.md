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
| Terminals (round 12, R1-F04) | **1 V_ref** (reference output, "not connected" in the DS schematic; optional 1–4.7 nF to Gnd) · **2 V_out** · **3 Gnd** · **4 U_C (5 V)** · **E1–E4 mass pins → Gnd**; "Connector type: none" — a PCB-mount THT device (ASIC pins Ø 1 mm, mass pins Ø 2 mm) | Dimensions drawing + electronic schematic, p.2; soldering p.7 |
| Load | R_L ≥ 10 kΩ; C_L 4.7 nF typ / 6.8 nF max; R_out ≤ 10 Ω; V_out 0.2–4.8 V at U_C = 5 V | Operating characteristics p.3 |
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
| SWG output (round 12) | sine amplitude programmable **0.394–2.302 V pk-pk** (MAXAPP 1.884 / 2.093 / 2.302 V min/typ/max, MINAPP 0.394–0.482 V), amplitude variation ±10 %, common voltage 1.3 V typ ±6 %; input clock 12–20 MHz | Table 40 "Sine wave generator" |
| Pin injection (round 12) | I_INJPAD_DC_OP **−3…+3 mA per I/O pin continuous** (−2…+3 mA at VDD_HV_A ≥ 2.97 V); sum over all pins ±30 mA; LVDS pins 100 µA | Operating conditions table; note 10 (see AN4731) |
| SDADC input impedance (round 12) | Z_DIFF **215–380 kΩ** (gain 1, Fs 40 MHz; 430–760 kΩ at 20 MHz), 40 kΩ min at gain 8/16; Z_CM 400–640 kΩ (gain 1, 20 MHz) | Table 38 |
| SDADC external anti-alias filter (round 13) | R_AAF 5 kΩ typ / 20 kΩ max series; **C_AAF 180 pF min / 220 pF typ** across the inputs | Table 38 |
| Max ADC input | SAR ADC: V_AD_INPUT = VREFL to VREFH (VREFH_SAR up to 5.5 V max; accuracy guaranteed only in this range). SDADC: AVDD 4.5–5.5 V, VREFP = AVDD ±0.025 V; single-ended p-p range VREFP/GAIN; SDADC intended only with VDD_HV_A = 5 V | Table 37 (SAR_ADC); Table 38 (SDADC); Table 4 note 6 |

### 10a. S32K396 289-MAPBGA ball map — pin freeze (rev A.12)

The datasheet on disk carries the supply balls only (PMIC-option figure: K9/K10/N7 V11, H5 V15, B8 VSSA_SWG01,
E11 VSSA_SDADC, J6 VREFL_SAR_0123, E7 VREFL_SAR_456, E9 VREFL_SDADC_01, G13 VREFL_SDADC_23, F1 NMOS_CTRL). The
full ball ↔ port/function table was taken from NXP's own GEN3 control-card schematic **SPF-91122 rev C**
(sheets 8–13, on the user's disk), whose MCU symbol prints every ball with its alternate-function list. The
text layer pairs a ball with the function string 2.4 px below it (the string 1.1 px above belongs to the
previous pin); all eight datasheet balls confirm that rule, and it reproduces GEN3's resolver as matched
SDADC AN[0]/AN[1] pairs. Result: `calculations/mcu-ballmap.json` (289 balls: 63 signal, 67 supply/ground,
159 open) and `docs/mcu-pin-manifest.md`.

**Round 14 correction (A12-R01/R02, rev A.13).** The A.12 map had two supply balls wrong: **H5 is V15** (the
1.5 V core-regulator input; GEN3 net `VCORE`) — A.12 had put the 5 V VREF5 on it (2.75 V abs max); **J7 is V25**
(the internal 2.5 V flash-regulator output, COUT_V25 140 nF min / 220 nF typ, DS Table 11) — A.12 had grounded
it. Both are now bound (H5 → V15S, J7 → V25 + CV25 220 nF) and the whole map was re-derived against NXP's own
GEN3 net report `NET-91122_C.net` (MCU refdes U513, 244 connected balls): every supply ball matches a GEN3 net
and all 165 port-named GEN3 nets equal the text-parse port except G6 (GEN3 `PTB30`, text `PTE4`; unused). The
four signals that sat on balls the GEN3 board leaves open moved to netlist-confirmed balls (HW_ID → B5/PTE0,
NTC_A → T15/PTC11, MT2_SIG → D5/PTE26, ASC_REQ → U4/PTD7). K5 (PMOS_CTRL) stays open in the PMIC option; the DS
NMOS ballast network gets its 1 nF CNMOS (CBAL, Table 13). GEN3 runs VDD_HV_B at 3.3 V (LDO1); ours is 5 V,
allowed with VDD_HV_A = 5 V (DS Table 5); VDD_LVDS (N5) stays 3.3 V.

### 10b. FS26 pin map — verified (rev A.12)

`SBC_PINS` (48 + EP) matches FS26 DS Table 3 pin for pin: 1 VBST_PG · 2 WAKE2 · 3 GPIO1 · 4 TRK1 · 5 TRK2 ·
6 GPIO2 · 7 TRKIN · 8 VREF · 9 LDO2OUT · 10 LDOIN · 11 LDO1OUT · 12 FS1B · 13 FS0B · 14 VMONEXT · 15 VMONCORE ·
16 RSTB · 17 FCCU1 · 18 FCCU2 · 19 GNDFS · 20 GND · 21 VDIG · 22 GNDSUB · 23 VDDIO · 24 INTB · 25 MISO ·
26 MOSI · 27 SCLK · 28 CSB · 29 AMUX · 30 CORE_FB · 31 DEBUG · 32 CORE_SW · 33 CORE_BT · 34 CORE_IN · 35 VBOS ·
36 VMONPRE · 37 VPRE_FB · 38 NC · 39 VPRE_BT · 40 VPRE_SW · 41 VSUP_PWR · 42 VSUP · 43 VBST_FB · 44 VBST_ISL ·
45 VBST_G · 46 VBST_ISH · 47 WAKE1 · 48 BATSENSE · EP.

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

## 13. Mornsun QA01C-18 — the fitted PSASC/PSQD part (round 9, A8-N01)

The archived `QA01C.pdf` (2019.01.11-A/3) is the **base QA01C, +20/−4 V**. The BOM orders **QA01C-18**,
which has its own sheet: Mornsun `QA01C-18.pdf` rev **2018.12.11-A/0**, fetched from mornsun-power.com for
round 9 and not archived. F61 (rev A.5) had bound the base part's +20/−4 V figures to the -18 part.

| Parameter (QA01C-18) | Value | Citation |
|---|---|---|
| Input voltage window | **13.5–16.5 V** (15 V nominal); surge 21 VDC 1 s max; no-load input 16 mA typ / 30 mA max | Selection Guide; Input Specifications |
| Output voltage/current | **+18 V / −3 V, ±100 mA** (76/79 % efficiency); only +Vo is used here | Selection Guide p.1 |
| +18 V tolerance envelope | Fig. 1: max **+9 % → +2 %**, typ +4 % → −3 %, min **0 % → −7 %** over 10–100 % load; load regulation 6 % typ / 10 % max (10–100 %) | p.2 Fig. 1; Output Specifications |
| Line regulation / tempco | ±1.1 typ / **±1.3 max %/%**; ±0.03 %/°C at 100 % load | Output Specifications |
| Used here | V15 = 15.0 V ±3 % (NCV4276C-ADJ) and 2–8 % load → **16.9–20.9 V** (the max line extrapolated below 10 % load). The QDIS gate is divided 1.5 k/10 k (1 %) → 13.3–18.2 V: a divider, not a clamp | design-verify QA18 |

Base QA01C, for reference only (not fitted):

| Parameter | Value | Citation |
|---|---|---|
| Input voltage window | **13.5–16.5 V** (15 V nominal); surge 21 VDC 1 s max | Selection Guide; Input Specifications |
| Output voltage/current | +20 V / −4 V dual output, ±100 mA (76/80 % efficiency) — **the base part, not the fitted -18** | Selection Guide |
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

### 16a. NCV4276C thermal (round 13, A11-R04)

| Package | RθJA, 1 oz copper 0.26 in² (168 mm²) | RθJA, 1 oz copper 1.14 in² (736 mm²) | Citation |
|---|---|---|---|
| DPAK 5-pin (U5LB/U5LC, UGDL) | 75.1 K/W | **58.5 K/W** | thermal table p.3, notes 4/5 |
| D2PAK 5-pin | 54.2 K/W (0.373 in²) | 43.3 K/W (1.222 in²) | notes 6/7 |

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
| V_WM / V_BR / V_C | 20.5 V / 22.8–25.2 V / **33.2 V @ I_PP** | these are the **classic** TPSMC24CA figures ("24" = nominal breakdown); clamps the KL30 rails far above 16.5 V (QA01C) and 18 V (ALM2402) — hence ULDO15 + ULDOEX post-regulators |
| **Round 12 correction** | the archived `TPSMC24CA.pdf` is the **TPSMC-VR series** (24 = stand-off; V_BR 26.7–29.5 V, V_C 38.9 V at 38.6 A), a different voltage class from the classic part the notes described. The classic part conducts at the 24 V/60 s jump start (V_BR min 22.8 V) and takes 3× its 400 ms capability in an ISO 16750-2 test B at Ri ≤ 2 Ω. **Bound explicitly since A.11: TPSMC24CA-VR** (AEC-Q101, 24 V stand-off, V_BR 26.7–29.5 V, V_C 38.9 V at 38.6 A — the archived `TPSMC24CA.pdf`). The classic series sheet is archived as `TPSMC-classic.pdf` and the 5 kW 5.0SMDJ series as `5.0SMDJ-series.pdf` (V_BR 26.7–29.5 V, 38.9 V at 129 A; not AEC-Q101) as evidence; none of the three rates a pulse beyond 1 ms | Opus check of R1-F14/R2-F10; Sonnet look-up |

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

## 22. VDC-channel bias: TI UCC12050 (rev A.11) — the MGJ2 bind was invalid

**Round 12 (R1-F12, R2-F02/F03).** The order code "MGJ2D150505SC" bound in rev A.4.4 does not exist:
Murata's MGJ2 selection guide (KDC_MGJ2, ©2019) lists only asymmetric gate-drive outputs (+15/−5,
+15/−8.7, ±15, +18/−2.5, +20/−3.5, +20/−5 V) for 5/12/15/24 V inputs, and the family's safety approval
is UL 60950 **reinforced to 150 Vrms working** (basic 300 Vrms); no VDE 0884 V_IORM is published. Its
5.2 kV figure is a 1 s hipot. So no MGJ2 could close an 850 VDC reinforced barrier. Also checked and
rejected on the same criterion: Murata MGJ1 (250 Vrms), NXE1 (125 Vrms), NXJ1 (200 Vrms), RECOM RxxP2
(34 Vrms), R1SX (functional only), Mornsun QA01-04 (no working-voltage figure), ADI ADuM6028 (V_IORM
565 Vpk). Datasheets consulted on-line (24 Sep 2026); not archived here — PO gate: archive
`ucc12050.pdf` (SNVSB38D) before ordering.

| UCC12050 (TI DS SNVSB38D) | Value | Citation |
|---|---|---|
| Input / output | 4.5–5.5 V in → VISO 5.0 V (SEL tied to VISO), 3.3/3.7/5.4 V selectable; I_ISO ≤ 100 mA | §6.9, §8.2.1 |
| No-load input current | **50 mA typ** (EN high, 5.0 V select); ≤ 100 µA disabled — **production UCC12051-Q1 (SNVSBY2A §6.9): 52 mA typ / 80 mA MAX at the 5 V select** (96 typ / 140 max at 3.3 V), the round-14 basis of the 96 mA bias budget and the 47 Ω LDO ballast | §6.9 (I_VINO, I_VINQ) |
| Barrier | V_ISO 5 kVrms (UL 1577), **V_IORM 1697 Vpk, V_IOWM 1200 Vrms / 1697 VDC**, V_IOSM 6250 Vpk, DTI > 120 µm, creepage/clearance > 8 mm, CTI > 600 (Group I); DIN V VDE V 0884-11 reinforced, UL/IEC 62368-1 | §6.6 |
| Pins (SOIC-16 DVE, Table 5-1) | 1 EN · 2 GNDP · 3 VINP · 4 SYNC · 5 SYNC_OK (open drain) · 6/7/8 NC (primary domain → GNDP) · 9/15/16 GNDS (15 = bypass return) · 10/11/12 NC (isolated domain → GNDS) · 13 SEL · 14 VISO | §5 |
| Caps | 10 µF 16 V X7R ±10 % VINP–GNDP and VISO–GNDS; optional 100 nF | Table 8-1 |
| Thermal / temperature | RθJA 63.8 K/W (UCC12051-Q1: 57.5 K/W); T_A −40…125 °C | §6.4, §6.3 |
| Automotive variant | UCC12051-Q1 (AEC-Q100 Grade 1), same barrier table; TI product page lists the VDE/UL/CQC certificates (re-verify validity dates at PO) | ti.com/product/UCC12051-Q1 |

Fed from its own NCV4276C (V15 → 5 V, ≈ 62 mA, 0.62 W in DPAK) per channel. The AMC1311 it feeds is
itself V_IOWM 1.2 kVrms reinforced, so the barrier class is now consistent along the whole channel.

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

## 25. Round-7 safety-logic data (rev A.7)

**74LVC3G17-Q100** (USCH). Nexperia, Rev. 5, 24 Aug 2023 —
https://assets.nexperia.com/documents/data-sheet/74LVC3G17_Q100.pdf. **Not yet archived**; the
URL is cited.

| Parameter | Value | Citation |
|---|---|---|
| Input transition rate | **no Δt/ΔV row** in the operating conditions (Schmitt buffer) | Table 6 |
| V_T+ (−40…125 °C) | 1.90–3.30 V at 4.5 V · 2.20–3.80 V at 5.5 V | Table 8 |
| V_T− | 1.00–2.20 V at 4.5 V · 1.20–2.50 V at 5.5 V | Table 8 |
| t_pd max | 5.4 ns (−40…125 °C, V_CC 4.5–5.5 V) | Table 9 |
| Pins (DC, VSSOP-8) | 1=1A · 2=3Y · 3=2A · 4=GND · 5=2Y · 6=3A · 7=1Y · 8=VCC | Table 3 |
| Grade | AEC-Q100 Grade 1 | §1 |

**74LVC1G74-Q100** (ULAT/ULAT2). Nexperia, Rev. 7, 22 Sep 2025 —
https://assets.nexperia.com/documents/data-sheet/74LVC1G74_Q100.pdf. **Not yet archived**.

- Pin-identical to TI DCU (1=CP, 2=D, 3=Q̄, 4=GND, 5=Q, 6=R̄D, 7=S̄D, 8=VCC).
- **Table 4 (asynchronous operation): SD = L, RD = L ⇒ Q = H, Q̄ = H**, the same as TI. The
  fault-latch recovery depends on this row (read from the Rev 7 PDF during the round-7
  cross-check).
- AEC-Q100 Grade 1.
- Δt/ΔV ≤ 10 ns/V at 2.7–5.5 V, despite the "Schmitt-trigger action" prose. The same applies
  to the 74LVC1G11-Q100 and 74LVC1G08-Q100.
- The archived TI SN74LVC1G74 (SCES794G) is a catalog part with no AEC-Q100 statement:
  Δt/Δv ≤ 5 ns/V at 5 V, no hysteresis.

**Other round-7 values, all from the archived PDFs:**

| Part | Parameter | Value | Citation |
|---|---|---|---|
| FS26 | FS0B/FS1B current limit · V_OL · read-back low/high | 4–22 mA · ≤ 0.4 V at 2 mA · < 0.7 V / > 1.5 V | Tables 196/197 |
| FS26 | recommended pull-up to VDDIO | 5.1 kΩ | §22.11.2 |
| FS26 | FS1B at power-up | **asserted low after each POR or wake-up**, until the MCU releases it (drives the §9 boot order) | §22.11.3 |
| FS26 | reference FS1B network | 10 nF at the pin; 5.1 kΩ to VDDIO or 10 kΩ to VSUP; 22 nF toward the fail-safe circuitry (EMC-phase item) | Fig. 62 |
| FS26 | FS1B_TDELAY / TDUR | 0 or 5 ms…10 s / 100 ms default … infinite | Tables 95/96 |
| FS26 | WD_ERR_LIMIT / WD_FS_REACTION | 8/6/4/2 (6 default) / RSTB+FS0B default | Tables 75/76 |
| NSI6611 | ASC thresholds (rising / falling) | 2.7–3.2 V / 1.3–1.7 V | p.7 |
| NSI6611 | ASC rising / falling delay | tASC_r 0.39–1.1 µs · tASC_f 0.15–0.48 µs | p.7 |
| NSI6611 | EN deglitch · t_RST_FIL · t_FLT_MUTE | 28–60 ns · 0.48–0.8 µs · 0.55–1.3 ms | p.7/p.9 |
| NSI6611 | protection priority | VCC2-UVLO > DESAT > ASC > VCC1-UVLO | §8.11 |
| NSI6611 | **DESAT vs ASC by EN state** | function table: EN/RST **low** + ASC high ⇒ OUT high, DESAT "X" (irrelevant), FLT HIZ. DESAT wins over ASC only in the row EN high, IN+ high, IN− low. Hence ASC is held as PWM-ASC with EN high (firmware-contract §4c) | §8.12 table, Fig. 8.11 |
| NSI6611 | RST/EN internal pull-down | 50 kΩ (block diagram); none is drawn on ASC | Fig. 8.1 |
| TLP152 | I_FLH (turn-on threshold) | typ 1.5 / **max 7.5 mA**; recommended I_F(ON) 10–15 mA; V_F 1.40–1.80 V at 10 mA | §8/§9 |
| TLP152 | t_pLH / t_pHL max (−40…100 °C) | 170 / 190 ns | §11 |
| UCC28C4x | I_VDD | typ 2.3 / max 3 mA, **no minimum**; no internal VDD clamp (20 V abs) | §6.5, §7.3.1.7 |
| TPS55340 | abs max VIN / SW | 34 V / 40 V; no pass-through statement — **these are the COMMERCIAL part's figures (SLVSBD4E, the archived `TPS55340.pdf`)**. The fitted **TPS55340QRTERQ1 (-Q1, SLVSBV5C §6.1/6.3): VIN 38 V recommended max, 40 V absolute, SW 40 V** — see `TPS55340-Q1.pdf` (round 13, A11-R03) | §6.1 |
| AMC1311B | IN→OUT delay 50–50 % | typ 1.6 / max 2.1 µs | §7.10 |

## 27. Vishay VOW3120 (`VOW3120.pdf`, doc 82442 rev 1.3) — UASC/UQD since rev A.12

| Parameter | Value | Where |
|---|---|---|
| Package / order code | SMD-8 widebody, tape, VDE option: **VOW3120-X017T** | p.1 |
| Pins | 1 NC · 2 A · 3 C · 4 NC · 5 VEE · 6 NC · 7 VO · 8 VCC | p.1 (DIP-8 numbering) |
| Isolation | V_ISO 5300 Vrms (UL 1577); V_IOTM 8000 Vpk; **V_IORM 1414 Vpk**; DIN EN 60747-5-5 (VDE 0884-5) option 1; creepage/clearance ≥ 10 mm | p.5 |
| Approvals | UL/cUL, VDE, CQC — **no AEC-Q101** | p.1 |
| LED | V_F 1.0 / 1.36 / 1.6 V at 10 mA; ΔV_F/ΔT −1.4 mV/°C (typ); I_FLH 3.4 typ / **8 mA max**; I_F(ON) recommended 10–16 mA; **25 mA abs**; V_R 5 V | p.3–4 |
| Output | I_OH/I_OL 2.5 A peak (0.5 A at V_CC − 4 V / V_EE + 2.5 V); V_OH ≥ V_CC − 4 V at −100 mA; V_OL ≤ 0.5 V at 100 mA; I_CC ≤ 2.5 mA | p.3 |
| Supply | V_CC − V_EE 15–32 V recommended, 35 V abs; **UVLO 11–13.5 V rising / 9.5–12 V falling**, 1.6 V hysteresis | p.3 |
| Timing | t_PLH / t_PHL 0.1 / 0.25 / **0.5 µs** max; CMR 50 kV/µs typ at 1500 V | p.4 |
| Thermal | T_amb −40…100 °C; P_diss 220 mW output, 260 mW total; θ_BA 50 °C/W | p.2–3 |

Design use: 270 Ω 1 % LED series from the 5 V LVC outputs (10.8–15.8 mA, verifier row); ASC path through
2.2 k + 5.1 V clamp; discharge gate through 1.5 k/10 k with the V_OH bound (verifier row).

## 28. TI UCC14141-Q1 (`UCC14141-Q1.pdf`, SLUSF10B) — PSASC/PSQD since rev A.12

| Parameter | Value | Where |
|---|---|---|
| Order code / package | **UCC14141QDWNRQ1**, 36-pin DWN (wide SSOP 12.83 × 7.50 mm); AEC-Q100 | p.1, addendum |
| Pins | GNDP 1,2,5,8–18 · PG 3 · ENA 4 · VIN 6,7 · VEE 19–27,30,31,36 · VDD 28,29 · RLIM 32 · FBVEE 33 · FBVDD 34 · VEEA 35 | Table 6-1 |
| Input | V_IN 8–18 V (one bin); 1 W typical over the bin (1.5 W within 10.8–13.2 V); 2.5 W abs (VDD−VEE, 25 °C) | §5, §7.1 |
| Regulation | V_FBVDD_REF / V_FBVEE_REF 2.4675 / 2.5 / 2.5325 V; FB hysteresis 9 / 10 / 12.3 mV; VDD−VEE = 2.5 V × (1 + R_TOP/R_BOT) — **62 k/10 k → 18.0 V, 17.4–18.6 V** with 1 % parts | §7.5, §8.3.1.1 |
| Single-output configuration | FBVEE tied to FBVDD, RLIM open ("optional"), VEEA to VEE; 330 pF at FBVDD | Fig. 9-2, Table 6-1 |
| Capacitors | C_IN 2 × 10 µF + 0.1 µF at VIN–GNDP; C_OUT1 10 µF + 0.1 µF at VDD–VEE; 0.1 µF parts at the pins, no vias between | Table 9-2, §9.5.1 |
| ENA / PG | V_EN_IR 2.1 V max rising, V_EN_IF 0.8 V min falling; ENA 0–5.5 V recommended (7 V abs) — ours: V15 through 10 k/4.7 k = 4.65–4.94 V; PG open-drain, left open | §7.5, Table 6-1 |
| Isolation | **V_IORM 1414 Vpk; V_IOWM 1000 Vrms / 1414 VDC; V_IOTM 7071 Vpk**; reinforced per DIN EN IEC 60747-17 (VDE 0884-17); V_ISO 5000 Vrms (UL 1577) | §7.5 |
| Certificates | VDE / UL / CQC each listed **"(planned)"** — no file numbers issued in SLUSF10B; check at PO (gate ㉔ residual) | §7.6 |
| Thermal | RθJA 52.3 °C/W; T_J −40…150 °C | §7.4 |

## 29. Y-caps: Vishay VY1 (`VY1-series.pdf`, doc 28537) and Murata DE1 (`DE1-RA.pdf`) — CY1/CY2 since rev A.12

| Parameter | Vishay VY1 4.7 nF | Murata DE1 4.7 nF (alternate) |
|---|---|---|
| Order code | **VY1472M63Y5UQ6TV0** (kinked leads, 10 mm) / …TL0 straight; K for ±10 % | DE1E3RA472MJ4BP01F (bulk), …MA4BP01F ammo, …MN4AP01F tape |
| Class / rating | **Y1 500 VAC, X1 760 VAC, 1500 VDC** | Y1 300 VAC, X1 440 VAC, 1500 VDC |
| Body | D 16.0 mm max, T 5.0 mm, leads 0.6 mm, 30 ± 5 mm | D 12.0 mm, T 5.0 mm, F 10.0 mm |
| Dielectric | Y5U (size code 63) | RA |

## 31. Round-14 protection parts (rev A.13): SMCJ8.5CA, MF-MSMF020, 0438.375WRA, ALM2402 output stage

| Part | Parameter | Value | Where |
|---|---|---|---|
| Littelfuse SMCJ8.5CA (TVSEP/TVSEN) | V_RWM / V_BR / V_C at I_PP | 8.5 V / 9.44–10.40 V / 14.4 V at 104.2 A (10/1000 µs); I_R 20 µA | `SMCJ-series.pdf` p.2 |
| | P_PP / P_D | 1500 W at 1 ms (Table 1); ≈ 550 W at 10 ms (Fig. 2, read graphically; the axis stops at 10 ms); 6.5 W steady at T_L 50 °C | p.1, p.3 |
| | AEC-Q101 | not stated on the commercial sheet — the automotive-line variant if the OEM requires it | all pages |
| Vishay SMAJ8.5CA (comparison) | V_BR / V_C / I_PP | 9.44–10.4 V / 14.4 V / 27.8 A (400 W) | `SMAJ-series.pdf` p.2 |
| Bourns MF-MSMF020/33X (FEXP/FEXN since A.14; A.13 had the unsuffixed 020) | I_hold / I_trip / V_max / I_max | 0.20 A / 0.40 A / **33 V** / 40 A; package **1812** (4.37 × 3.07 mm — the A.13 BOM had said 1206, A13-R03) | `MF-MSMF.pdf` p.1 |
| | R_min / R_1max · time-to-trip · P_trip | 0.35 Ω / 5.0 Ω (1 h after a trip) · **0.02 s at 8 A** · 0.8 W typ; −40…85 °C | |
| | hold-current derating | **0.09 A at 85 °C** (0.13 A is the 60 °C figure — A13-R06) against the 40–60 mA rms excitation | p.9 |
| | unsuffixed MF-MSMF020 | 30 V, 80 A, 0.40/6.0 Ω, 0.06 s at 6 A; flagged not-for-new-designs — hence the /33X | |
| | why not MF-LSMF | the family has no 0.2 A part (lowest MF-LSMF030X); MF-LSMF050X is 60 V / 0.5 A (4 s at 2.5 A), MF-LSMF075X 30 V / 0.75 A | `MF-LSMF.pdf` |
| Littelfuse 0438.375WRA (FMT1/FMT2) | rating / V / I²t / R | 375 mA, 63 V DC, 0.0041 A²s melting, 1.247 Ω, 0.488 V drop at rating, −55…150 °C, **AEC-Q200** (438A series, 0603) | `Littelfuse-438A.pdf` p.1, p.3 |
| | alternates evaluated | Bourns SF-0603FP0375F-2 (65 V, I²t 0.0041, cUL only); Littelfuse 0466.375 (1206, 125 V, I²t 0.0045, UL/CSA only) | archived |
| TI ALM2402-Q1 (UEXD) | output abs max / current / limits | V_OUT −0.3…18 V; 400 mA source/sink continuous; internal limits ≈ 750 mA (short to GND) / ≈ 550 mA (short to supply) | `ALM2402-Q1.pdf` §7.1, §7.4, §8.3.3 |
| | reverse current | the output transistors' body diodes conduct if an output is forced above the supply — "limit to pulsed operation"; the current limit does not act in reverse (§8.3.6) → the round-14 clamp sits BELOW the rail + a diode so the case never arises | p.12 |

## 32. TI UCC14141-Q1 VDE certificate 40058888 (`UCC14141-Q1-VDE-40058888.pdf`)

Issued 2024-07-24, updated 2024-11-28; DIN EN IEC 60747-17 (VDE 0884-17):2021-10 / EN IEC 60747-17:2020+AC:2021;
lists six types including **UCC14141QDWNRQ1** (item 2 of 6). The cover certificate does not print V_IORM/V_IOWM
(they are in annexes 200K1/200K2/300M1 not in the download); the datasheet's §7.5 values (V_IORM 1414 Vpk,
V_IOWM 1000 Vrms / 1414 VDC, V_IOTM 7071 Vpk) stand, and the datasheet's "planned" wording (SLUSF10B, 2023)
simply predates the certificate. UL 1577 recognition is still listed as planned by TI — no UL file found.
This closes the gate ㉔ component residual on the VDE basis (round 14, N01).

## 30. Discharge resistors: TT/Welwyn SQP10 (`SQP.pdf`) and Yageo SQP (`Yageo-SQP.pdf`) — RDIS since rev A.12

Bound: **SQP10-470RJB15** (8XX) / **SQP10-220RJB15** (4XX), TT Electronics (Welwyn) SQP series — 10 W ceramic-cased
wirewound, the sheet archived as `SQP.pdf` (round 12). Alternate: Yageo **SQP10AJB-470R / -220R** (V.5 sheet):
body 48 × 9.5 × 9.0 mm, 0.8 mm leads, 10 W at 40 °C, 500 V working / 1000 V overload, "flameproof ceramic case"
(no UL 94 class printed). Neither sheet publishes fail-open data — gate ㉖ stays.

## 26. Round-8 data (rev A.8)

**Nexperia 74LVC1G74 / 74LVC1G08 / 74LVC3G17-Q100.** One LVC family; identical output limits.
Sources: 1G74 Rev 7 Table 8; 1G08 Rev 7 Table 7; 3G17 Rev 5 Table 7.

| Parameter | −40…85 °C | −40…125 °C |
|---|---|---|
| V_OH at V_CC 4.5 V, I_O −32 mA | ≥ 3.8 V | **≥ 3.4 V** |
| V_OL at V_CC 4.5 V, I_O 32 mA | ≤ 0.55 V | ≤ 0.80 V |
| I_I / I_OFF | ±1 / ±2 µA | ±1 / ±2 µA |

- The design uses the 125 °C point as a linear bound, R_out ≤ 34.4 Ω.
- **74LVC1G08-Q100 GW** (SOT353-1): 1 = B, 2 = A, 3 = GND, 4 = Y, 5 = VCC. Δt/ΔV ≤ 10 ns/V at 2.7–5.5 V.
- **74LVC3G17-Q100:** its operating-conditions table has **no Δt/ΔV row**.
- **74LVC1G74-Q100:** Table 4 gives SD = RD = L ⇒ Q = Q̄ = H.

**Toshiba TLP152 (local, Rev 6.0).**

| Parameter | Value | Source |
|---|---|---|
| I_F absolute max | 20 mA | §7 |
| Recommended I_F(ON) | 10–15 mA | §8 |
| T_opr | −40…100 °C | §7 |
| V_F | 1.40 / 1.57 / 1.80 V at 10 mA, 25 °C; ΔV_F/ΔTa typ −1.8 mV/°C (**no curve in this DS**) | §9 |
| I_FLH | max 7.5 mA over −40…100 °C | §9 |
| t_pLH / t_pHL max | 170 / 190 ns at I_F 10 mA only (no I_F curve) | §11 |

**NOVOSENSE NSI6611A-Q1.**

| Parameter | Value | Source |
|---|---|---|
| V_FLT_L, V_RDY_L | ≤ 0.3 V at 5 mA | p.6, p.8 |
| t_RST_FIL | 480–800 ns | p.8 |
| t_FLT_MUTE | 0.55–1.3 ms | p.8 |
| Reset semantics | **inconsistent in the DS**: §9.4 prose says "held for at least t_FLT_MUTE"; §8.10 and Fig. 8.8 show resets ignored during the mute time, then any ≥ t_RST_FIL pulse releasing FLT at its rising edge. FW-15's ≥ 1.5 ms low satisfies both. | §8.10, §9.4, Fig. 8.8 |
| **ASC vs a latched DESAT** | ASC high throughout; DESAT → soft-off, FLT low; the gate stays **off** as IN goes low and through an RST/EN pulse, and returns (following ASC) only at the post-mute reset edge. Without a latched fault, ASC drives the gate high regardless of IN and EN. | Fig. 8.11, §8.12 |

**HIITIO transient thermal impedance (Foster, printed on the Zth charts).** In each case ΣR is the
static R_th that S4 uses as its upper bound.

| Module / device | τ_i (s) | R_i (K/W) | ΣR (K/W) | Source |
|---|---|---|---|---|
| HCS600 SiC MOSFET | 0.00022 / 0.00226 / 0.04676 / 0.14846 | 0.00200 / 0.00439 / 0.04444 / 0.01485 | 0.0657 | Fig. 17 |
| HCG600 IGBT | 7.0e-6 / 9.2e-5 / 2.9e-4 / 0.0289 / 0.0684 | 0.00116 / 0.00229 / 0.00661 / 0.03927 / 0.02094 | 0.0703 | Fig. 8 |
| HCG600 diode | 8.6e-6 / 1.0e-4 / 3.8e-4 / 0.0259 / 0.0739 | 0.00220 / 0.00435 / 0.01262 / 0.05414 / 0.02694 | 0.1003 | Fig. 13 |

**NXP FS26 (local Rev 3).**
- **FS0B_REQ** (bit 6) and **FS1B_REQ** (bit 2) in FS_SAFE_IOS_1 (0x52): SPI-requested assertion,
  self-clearing (Table 93/94). The FW-16 boot test uses this.
- LDO1/LDO2 output is OTP-selectable 3.3/5.0 V. **R_LDOx_DCHG is 20–60 Ω when an LDO is
  disabled** (Table 124). No reverse-current rating is given on the LDO outputs.
- GPIO1 stage is set by GPIOxSTAGE_OTP (input / LS / HS / push-pull). A push-pull GPIO1 sits low
  after OTP load and **goes high at its power-up slot if one is assigned** (Table 133). §8a now
  requires "not slotted".

**Other parts.**
- **Nexperia BZT52 series** (fetched Rev 2, Table 8; the local `BZT52-series.pdf` is **Vishay's**):

  | Type | V_Z at 5 mA | r_dif at 5 mA / 1 mA | S_Z (mV/K) | I_R |
  |---|---|---|---|---|
  | C5V6 | 5.2–6.0 V | ≤ 40 / ≤ 400 Ω | −2.0…+2.5 | ≤ 1 µA at 2 V |
  | **B5V6** (fitted as ZSET after the cross-check) | **5.49–5.71 V** | ≤ 40 / ≤ 400 Ω | −2.0…+2.5 | ≤ 1 µA at 2 V |
  | C5V1 (rejected for ZSET) | 4.8–5.4 V | ≤ 60 / ≤ 480 Ω | −2.7…+1.2 | ≤ 2 µA at 2 V |

  - C_d ≤ 300 pF for all three.
  - Clamp at 125 °C through the 1.98 k of RFS4 + RFS1:
    - C5V6: 6.24 / 6.40 / 6.62 V at 16 / 24 / 35 V;
    - B5V6: 5.96 / 6.12 / 6.33 V.
  - USCH2 V_I absolute maximum: 6.5 V.
  - Vishay BZT52B5V6: αVZ up to +6·10⁻⁴/K, ≤ 6.43 V at 35 V.
  - The C5V1's soft knee (480 Ω at 1 mA, negative S_Z) through the 10 k RFS2 can pull the released
    level toward the 74LVC3G17 V_T+ maximum (3.55 V at V_CC 5.0 V, 125 °C). That is why it was
    rejected.
- **74LVC1G08/3G17-Q100 V_OH at −32 mA, 4.5 V:** ≥ 3.8 V over −40…85 °C and ≥ 3.4 V over
  −40…125 °C, so R_out ≤ 21.9 / 34.4 Ω. The LED corners pair each with the matching V_F (cold with
  the 85 °C bound, hot with the 125 °C bound). 74LVC1G08-Q100 t_pd ≤ 5.5 ns at 4.5–5.5 V, 125 °C.
- **FS26 FS1B timing** (Tables 95/96, §22.11.3):
  - FS1B_FS0B_EN_OTP = 0 (default) selects delayed assertion.
  - FS1B_TDELAY 00000 means "asserted with FS0B".
  - FS1B_TDUR 11111 means "infinite (released by MCU)". The design uses the 100 ms default, so each
    assertion ends by itself.
  - After a POR, FS1B is held until the MCU releases it.
  - LDO2 UV threshold is OTP-selectable 88–95.5 %, ±1 %, so ≥ 4.35 V at the lowest setting.
- **FS26 fault-reaction table** (§ fault list): an FS1B short-to-high adds +1 to the fault error counter,
  does not assert FS0B, and asserts RSTB only if BACKUP_SAFETY_PATH_FS1B = 1 (FS_I_FSSM bit 6; bit 7
  is the FS0B equivalent). Round 9 sets FS1B's bit to 0 (FW-12).
- **NSI6611 DS 1.2 absolute maximum ratings:** RDY and FLT (input side) are rated GND1 − 0.3 V to
  **VCC1**, with no +0.3 V. IN+/IN−/RST are rated to VCC1 + 0.3 V. The FLT/RDY input current is 20 mA.
  Round 9 moves their pull-ups onto V5GD (= VCC1).
- **HIITIO HCM75S12T4K3:** V_GS −10/+22 V absolute maximum, −5/+18 V recommended; C_iss 1185 pF.
- **Nexperia 74LVC1G11 (UAND1/2):** Δt/ΔV ≤ **10 ns/V** at V_CC 2.7–5.5 V (recommended operating
  conditions), despite the "Schmitt-trigger action" wording. That is why the open-drain RDY lines
  now pass through USCH3 (round 10).
- **ROHM ESR03 (RFS4 1 k, RASCG 2.2 k):** 0.33 W at Ta 70 °C in both the R ≤ 1 kΩ and 1 kΩ < R rows
  (terminal limit 130 °C / 110 °C respectively); limiting element voltage 150 V; −55…155 °C
  (datasheet rev ESR03-IA-013E, 2025-10-01).
- **Toshiba TLP152:** V_CC 10–30 V (35 V absolute maximum); V_OH ≥ 6.0 V at V_CC 10 V, I_O −100 mA;
  UVLO+ 7.8–9.7 V. The V_F tempco of −1.8 mV/°C is typical only; the model uses a ±28 % band.
- **ST BAT46:** V_F ≤ 0.25 V at 0.1 mA and ≤ 0.45 V at 10 mA (25 °C). There is no cold curve, so
  0.45 V is taken at 0.45 mA/−40 °C.
- **NXP S32K39x 5 V GPIO:**
  - V_OH ≥ V_DD − 0.7 V at I_OH 1.6 mA (standard) to 12 mA (fast, DSE = 1);
  - V_IH 0.65·V_DD, V_IL 0.35·V_DD.

---

# Archive index (48 PDFs, verified `%PDF` headers)

Every part on the released BOM has its datasheet here (round 12 archived UCC12050/UCC12051-Q1, the Murata MGJ2 guide as rejection evidence, TE SQP, ROHM ESR03, Diodes DMP6023LEQ, Nexperia 74LVC3G17/1G08-Q100 and BAT46WJ, Mornsun QA01C-18).
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
| TPSMC24CA.pdf | 972,042 | the **TPSMC-VR** series sheet (stand-off 24 V) — the part bound explicitly since A.11 (TPSMC24CA-VR) |
| UCC28C43.pdf | 3,682,285 | SLUS458I family doc; design uses UCC28C40DR |
| US1M.pdf | 176,802 | OK |
| VGT12EEM-200S1A4.pdf | 404,556 | OK |
| XAL4000.pdf | 210,340 | Coilcraft XAL40xx family (LCOR, LB15, LSBC) |
| UCC12050.pdf | 1,916,915 | PS5B/PS5C VDC bias (rev A.11, SNVSB38D) |
| UCC12051-Q1.pdf | 2,365,925 | AEC-Q100 alternate for PS5B/PS5C (SNVSBY2A) |
| MGJ2-selection-guide.pdf | 285,522 | Murata MGJ2 guide — evidence that "MGJ2D150505SC" does not exist and the family is reinforced to 150 Vrms only |
| SQP.pdf | 442,362 | TE/CGS SQP series — RDIS1–4 candidate (round 12) |
| ESR03.pdf | 757,645 | ROHM ESR03 (RFS4, RASCG) — rounds 9/10 data, archived round 12 |
| DMP6023LEQ.pdf | 589,325 | Diodes DMP6023LEQ-13 (QLVS) — round 9 data, archived round 12 |
| 74LVC3G17-Q100.pdf | 230,580 | Nexperia triple Schmitt buffer (USCH/USCH2/USCH3) |
| 74LVC1G08-Q100.pdf | 256,270 | Nexperia AND2 (UASCG) |
| BAT46WJ.pdf | 274,947 | Nexperia BAT46WJ (DFLT1/2 class; the BOM binds ST BAT46ZFILM — same V_F class) |
| QA01C-18.pdf | 934,145 | Mornsun QA01C-18 (PSASC/PSQD) — round 9 data, archived round 12 |
| TPSMC-classic.pdf | 840,654 | Littelfuse classic TPSMC series (V_BR 22.8–25.2 V) — evidence for the round-12 TVS correction, not fitted |
| 5.0SMDJ-series.pdf | 1,079,284 | Littelfuse 5.0SMDJ 5 kW series — evaluated round 12, not fitted (no AEC-Q101) |
| TPS55340-Q1.pdf | 2,098,268 | TI SLVSBV5C — the fitted TPS55340QRTERQ1 (38 V rec / 40 V abs; round 13) |
| PESD2IVN24-T.pdf | 262,019 | Nexperia PESD2IVN24 (same document as PESD2IVN24.pdf; archived under the fitted -T code, round 13) |
| VOW3120.pdf | 223,172 | Vishay VOW3120 (UASC/UQD since A.12; doc 82442 rev 1.3) |
| UCC14141-Q1.pdf | 3,133,172 | TI UCC14141-Q1 (PSASC/PSQD since A.12; SLUSF10B) |
| VY1-series.pdf | 265,078 | Vishay VY1 Y1 disc series (CY1/CY2 since A.12; doc 28537) |
| DE1-RA.pdf | 527,564 | Murata DE1 Y1 series — CY alternate (lower AC class) |
| Yageo-SQP.pdf | 620,597 | Yageo SQP/NSP — RDIS alternate (SQP10AJB-470R / -220R) |
| UCC14141-Q1-VDE-40058888.pdf | 396,961 | TI's issued VDE certificate 40058888 (2024-07-24, updated 2024-11-28) listing UCC14141QDWNRQ1 — round 14 (N01) |
| SMCJ-series.pdf | 840,472 | Littelfuse SMCJ 1.5 kW TVS (TVSEP/TVSEN SMCJ8.5CA since A.13; Wayback copy of the Littelfuse asset) |
| MF-MSMF.pdf | 1,327,051 | Bourns MF-MSMF PTC (FEXP/FEXN MF-MSMF020 since A.13) |
| Littelfuse-438A.pdf | 360,460 | Littelfuse 438A 0603 fuse (FMT1/FMT2 0438.375WRA since A.13) |
| Littelfuse-0466.pdf | 566,688 | Littelfuse 466 1206 fuse — FMT alternate evaluated (125 V, no AEC-Q200) |
| SF-0603FP-F.pdf | 188,435 | Bourns SF-0603FP — FMT alternate evaluated (65 V, no AEC-Q200) |
