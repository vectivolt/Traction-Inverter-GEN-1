# Extracted datasheet parameters — Traction Inverter

Source PDFs live in this directory. Citations reference the section/table of each datasheet.
Extraction date: 2026-09-09. All 36 requested datasheets downloaded OK (none failed).

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

## 2. UCC28C43 (SLUS458I, `UCC28C43.pdf`)

| Parameter | Value | Citation |
|---|---|---|
| FB (error-amp) reference | 2.5 V ±1 %: V_FB 2.475/2.5/2.525 V @25 °C; 2.45–2.55 V over temp | §6.5 Electrical Char. (Error Amplifier) |
| CS threshold V_CS | 0.9 / 1.0 / 1.1 V (max input signal, V_FB < 2.4 V) | §6.5, Current Sense |
| VCC UVLO on/off | VDD_ON (start) 7.8/8.4/9.0 V; VDD_OFF (min operating) 7.0/7.6/8.2 V — UCC28C43/45 grade | §6.5, Undervoltage Lockout |
| Abs max VCC | VDD = 20 V abs max (I_VDD 30 mA); recommended operating VVDD ≤ 18 V | §6.1 Abs Max; §6.3 Rec. Operating |
| Oscillator formula | **No closed-form equation given.** CCT charged from VREF (5 V) through RRT between 0.7 V and 3.0 V thresholds; trimmed 8.4 mA discharge sink. Frequency set from curves "Oscillator Frequency vs Timing Resistance and Capacitance" (Fig. 6-13). Anchors: RRT=10 kΩ + CCT=3.3 nF → f_OSC 50.5/53/55 kHz (spec); RRT=15.4 kΩ + CCT=1 nF → 110 kHz (design example). (These fit f ≈ 1.72/(R_RT·C_CT) within a few %.) | §7.3.5 Oscillator, Fig 7-4; §6.5 Oscillator; §7.3.1.4 |
| Max duty cycle | D_MAX 94–96 % (UCC28C42/43/40); D_MIN 0 % | §6.5, PWM |

## 3. AMC1311 / AMC1311B (`AMC1311.pdf`)

| Parameter | Value | Citation |
|---|---|---|
| VDD1 range | AMC1311: 4.5–5.5 V; AMC1311B: 3.0–5.5 V (abs max 6.5 V) | §7.3 Rec. Operating (Table 7-3); §7.1 |
| Input range | V_FSR specified linear full-scale = **−0.1 to +2 V** (IN to GND1); high-impedance input R_IN 1 GΩ | §7.3; §7.5 |
| Gain error | AMC1311: ±1 % max (0.4 % typ); AMC1311B: ±0.2 % max (±0.05 % typ), @25 °C | §7.5 Electrical Char., E_G |
| Output common-mode | V_CMout 1.39 / 1.44 / 1.49 V | §7.5, Analog Output |
| Fail-safe output on VDD1 loss | V_FAILSAFE = −2.6 to −2.5 V differential (active when SHTDN high, VDD1 undervoltage, or VDD1 missing). VDD1UV rising 2.5/2.7/2.9 V | §7.5; §8.3.2 Fail-Safe Output |

## 4. Faratronic C3D 20 µF / 1100 V (C3D1M206KFSA382, `C3D1M206KFSA382.pdf`)

| Parameter | Value | Citation |
|---|---|---|
| Rated ripple current @10 kHz | I_max = **15.4 A rms** (f = 10 kHz, Θ_amb = 70 °C, ΔΘ_case = 15 °C) | 技术参数 (Technical Parameters) table + note 1 |
| ESR @10 kHz | **7.8 mΩ** (tanδ 15×10⁻⁴ @1 kHz, 140×10⁻⁴ @10 kHz) | Technical Parameters table |
| dV/dt | **37 V/µs** | Technical Parameters table |
| (Ratings context) | U_N,85°C = 1000 Vdc; U_N,70°C = 1100 Vdc; 20 µF; 37.5 mm pitch | Technical Parameters table header |

## 5. BUK9Y14-80E (Nexperia, `BUK9Y14-80E.pdf`)

| Parameter | Value | Citation |
|---|---|---|
| V_DS | 80 V (Tj 25–175 °C) | §6 Limiting values (Table 4) |
| R_DS(on) | 12.2 mΩ typ / 15 mΩ max @ V_GS = 5 V, I_D = 15 A, 25 °C; 11.3/14 mΩ @ V_GS = 10 V | §8 Characteristics (Table 6) |
| I_D | 62 A max @ V_GS = 5 V, T_mb = 25 °C | §6 Limiting values |
| Q_G(tot) | 28.9 nC typ @ I_D = 15 A, V_DS = 64 V, V_GS = 5 V (Q_GS 8.1 nC, Q_GD 8.7 nC) | §8 Dynamic characteristics |

## 6. TDK VGT12EEM-200S1A4 (`VGT12EEM-200S1A4.pdf`, VGT series catalog p.3/9)

| Parameter | Value | Citation |
|---|---|---|
| Turns ratio | NP : NF : NS = **1 : 1.6 : 2.9** | Characteristics Specification Table |
| Isolation rating | Withstanding voltage NP,NF–NS = **2.6 kVrms/1 min** (sense 1 mA); coil–core 1.3 kVrms/1 min | Characteristics Specification Table |
| Primary inductance | NP = **10 µH ±20 %** (100 kHz/1 V); leakage ≤ 0.2 µH (NS shorted) | Characteristics Specification Table |
| Power/VA class | **Not specified in datasheet** — described only as "power transformer for IPM drive of motor inverter" (flyback use). No VA/W rating published | Features / Application (catalog p.1) |
| Winding arrangement | Primary side: NP split as NP2+NP1 in series on pins 1–2; **NF (pins 3–4) is the feedback/aux winding on the primary (non-isolated) side**; NS single isolated secondary on pins 5–8 side | Circuit Diagram, catalog p.3 |

## 7. LEM HC5FW 900-S (`HC5FW-900-S.pdf`, HC5FW 900-S/SP1, 17-Jan-2020 V0)

| Parameter | Value | Citation |
|---|---|---|
| Sensitivity | G = **2.22 mV/A** typ @ U_C = 5 V (ratiometric: V_out = (U_C/5)·(V_O + G·I_P)) | Operating characteristics table p.3; note 1 |
| V_out @ 0 A | V_O = **2.5 V** typ (@25 °C, U_C = 5 V, hysteresis included) | Operating characteristics table p.3 |
| Supply current | I_C = 19 mA typ / 25 mA max @ U_C = 5 V | Operating characteristics table p.3 |
| Accuracy | Global accuracy @0 A: ±13 (table row X_G, @25 °C incl. hysteresis); sensitivity error ±0.6 %; linearity ±1 % FS; electrical offset ±2.5 mV; magnetic offset ±2 mV; TCV_OE ±0.08 mV/°C; TCG ±0.03 %/°C | Performance Data table p.3 |
| Bandwidth | BW ≥ **40 kHz** (−3 dB); step response 2–6 µs to 90 % @ 100 A/µs | Performance Data table p.3 |
| (Ranges) | I_PM = ±900 A measuring range; U_C = 4.75–5.25 V | Electrical Data table p.3 |

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

## 12. TPS55340 (`TPS55340.pdf`)

| Parameter | Value | Citation |
|---|---|---|
| FB reference voltage | V_REF = **1.229 V** (1.204–1.254 over temp; 1.220–1.238 V @25 °C) | §6.5 Electrical Char., Voltage and Current Control |
| Switch current limit | I_LIM = **5.25 / 6.6 / 7.75 A** (N-channel MOSFET, D = Dmax) | §6.5, OCP and SS |

## 13. Mornsun QA01C (`QA01C.pdf`)

| Parameter | Value | Citation |
|---|---|---|
| Input voltage window | **13.5–16.5 V** (15 V nominal); surge 21 VDC 1 s max | Selection Guide; Input Specifications |
| Output voltage/current | **+20 V / −4 V dual output, ±100 mA** (efficiency 76 % min / 80 % typ at full load) | Selection Guide |
| Isolation | I/O isolation test **3.5 kVAC / 6 kVDC** (6000 VDC @ ≤1 mA leakage); isolation resistance ≥1000 MΩ @500 VDC | Features; Isolation Specifications |

---

# Download summary

All 36 files verified as real PDFs (`%PDF` header). Sizes in bytes.

| File | Size | Status | Source |
|---|---|---|---|
| 2N7002.pdf | 871,278 | OK | assets.nexperia.com |
| 74LVC1G11.pdf | 263,386 | OK | assets.nexperia.com |
| 74LVC1G32.pdf | 283,909 | OK | assets.nexperia.com |
| ACT45B.pdf | 446,768 | OK | tdk-electronics.tdk.com (act45b.pdf, CAN CM choke) |
| ALM2402-Q1.pdf | 2,275,634 | OK | ti.com |
| AMC1311.pdf | 1,782,662 | OK | ti.com (AMC1311/AMC1311B) |
| BAT64-04.pdf | 1,142,341 | OK | infineon.com (BAT64 series) |
| BUK9Y14-80E.pdf | 777,115 | OK | assets.nexperia.com |
| C3D1M206KFSA382.pdf | 541,956 | OK | LCSC (wmsc.lcsc.com, C2840809) |
| CX3225GA.pdf | 596,614 | OK | ele.kyocera.com (cx3225ga_e.pdf) |
| FS26.pdf | 5,421,631 | OK | Arrow-hosted full product DS Rev.3 (nxp.com gated; fetched via browser) |
| HC5FW-900-S.pdf | 835,088 | OK | TI e2e mirror of LEM HC5FW 900-S/SP1 (lem.com serves HTML) |
| HCM75S12T4K3.pdf | 2,538,054 | OK | hiitio.b-cdn.net |
| HCS600FH120D3C1.pdf | 671,796 | OK | hiitio.b-cdn.net (2025/03, NOT gated) |
| MF-LSMF.pdf | 1,550,688 | OK | bourns.com |
| NCV4276C.pdf | 311,168 | OK | LCSC mirror (onsemi.com blocks curl) |
| NRVBAF360T3G.pdf | 181,198 | OK | onsemi.com mbraf360-d.pdf (MBRAF360T3G/NRVBAF360T3G; via browser) |
| NSI6611A-Q1.pdf | 1,814,953 | OK | novosns.com (NSI66x1A-Q1 Rev 1.2) |
| OPA333.pdf | 1,944,689 | OK | ti.com |
| OPA348.pdf | 1,806,662 | OK | ti.com |
| OPA376.pdf | 1,697,245 | OK | ti.com |
| PESD2IVN24.pdf | 262,019 | OK | assets.nexperia.com (PESD2IVN24-T) |
| PESD5V0U1UA.pdf | 216,663 | OK | assets.nexperia.com |
| PMEG4010EH.pdf | 185,949 | OK | assets.nexperia.com |
| QA01C.pdf | 954,821 | OK | mornsun-power.com |
| S32K39.pdf | 2,146,090 | OK | Mouser mirror of S32K39-S32K37-DS Rev.3 (nxp.com gated; fetched via browser) |
| SN74LVC1G74.pdf | 1,427,201 | OK | ti.com |
| STPS5L60.pdf | 485,990 | OK | LCSC mirror (st.com blocks curl) |
| TCAN1042.pdf | 1,356,386 | OK | ti.com — NOTE: TCAN1042H family doc (SLLSES7, TCAN1042H/HG/HGV/HV) |
| TE-776231-1.pdf | 500,402 | OK | te.com customer drawing 776231 (AMPSEAL 35-pos) |
| TLP152.pdf | 354,551 | OK | LCSC mirror (toshiba docget needs session) |
| TPS55340.pdf | 2,932,213 | OK | ti.com |
| TPSMC24CA.pdf | 972,042 | OK | LCSC mirror (littelfuse.com blocks curl) |
| UCC28C43.pdf | 3,682,285 | OK | ti.com (UCC28C4x, SLUS458I) |
| US1M.pdf | 176,802 | OK | diodes.com (US1A–US1M, DS16008) |
| VGT12EEM-200S1A4.pdf | 404,556 | OK | LCSC mirror of TDK VGT catalog (product.tdk.com blocks curl) |
