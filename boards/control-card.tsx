// control-card.tsx — Traction Inverter CONTROL CARD (LV, ASIL-D brain): S32K396 lockstep MCU
// + FS2633D safety SBC (GEN3 architecture), hardware gate-enable chain, ASC latch, flyback-EN
// OR gates, resolver AFE (SWG excitation -> MFB filter -> ALM2402Q H-bridge), 3x hall-current
// chains, 2x CAN-FD, HVIL ladder, motor/module/board temperature inputs, vehicle connector,
// 40-way harness. Pin NUMBERS on MCU/SBC are symbolic; NAMES are the real GEN3 nets (§VERIFY).
import {
  Header, SmdFP, Smd2FP, CanFd, HallChain, NtcIn, Harness, gp,
} from "../packages/cells";

const NO_ROUTE = process.env.TSCI_NO_ROUTE === "1";
const PH = ["U", "V", "W"] as const;

// MCU pin map — rev A.12 PIN FREEZE: every connected ball of the S32K396 289-MAPBGA, label = <ball>_<signal>.
// Source: NXP EV-INVERTERGEN3 S32K396-HPWR-MC schematic (SPF-91122 rev C, sheets 8-13) parsed ball-by-ball and
// anchored on the S32K39 datasheet supply balls; peripheral function of every signal ball verified against the
// pin's alternate-function list (calculations/mcu-ballmap.json is the manifest; the ERC cross-checks this table
// against it). Corrections to the A.4 "GEN3-exact" port list found by the freeze: PTG10 had no PWM (W low is now
// PTC8 = PWM_1_B[2], U/V lows on PWM_1_B[0]/B[1]); PTB0 had no ADC (V_DC2 -> PTE1); PTB4/PTB5 no ADC (HW_ID -> PTC6,
// V5GD -> PTD27); PTB2/PTB3 are mux-address OUTPUTS (TMOD_V/W -> PTE2/PTE5); PTA10 is JTAG_TDO (NTC_A -> PTD29);
// PTA11-13 do not exist on this package (NTC_H/MT1/MT2 -> PTE6/PTA15/PTE18); PTF5 no ADC (INTRLOK_N -> PTE23).
// Unused GPIO balls (158) are left open and listed in docs/mcu-pin-manifest.md.
const MCU_PINS: [string, string][] = [
  ["A2_ISU", "ISNS_U"],  // PTA8 ADC3_P1 — phase current U
  ["A3_RESET_B", "RESET_B"],  // PTA5 RESET_b — reset
  ["A5_VDC2", "VDC2_SE"],  // PTE1 ADC1_P6 — V_DC channel 2 (was PTB0: no ADC)
  ["A7_VDDA_SWG01", "V5A"],  // VDDA_SWG01 — SWG analog supply (filtered 5 V)
  ["A8_SWG1", "SWG1"],  // SWG1_0 SWG1_0 — resolver excitation source
  ["A9_VREFH_R2R", "VREF5"],  // VREFH_R2R — ADC reference high (FS26 VREF 5 V)
  ["A11_MT1", "MT1_SIG"],  // PTA15 ADC3_P4 — motor temp 1 (PTA12 absent)
  ["A12_VREXMP", "VREXM_P"],  // PTA16 SDADC1_AN[0] — excitation monitor + (SDADC1 AN0)
  ["A13_VDC1", "VDC1_SE"],  // PTA0 ADC6_P4 — V_DC channel 1
  ["A14_ISV", "ISNS_V"],  // PTB8 ADC4_P5 — phase current V
  ["A15_V5GD", "V5GD_SNS"],  // PTD27 ADC4_P6 — V5GD/2 monitor (was PTB5: no ADC)
  ["A16_SINN", "SIN_N"],  // PTD26 SDADC2_AN[1] — resolver SIN- (SDADC2 AN1)
  ["B2_VSS", "DGND"],  // VSS — ground
  ["B3_TMU", "TMOD_U"],  // PTA9 ADC0_P7 — module NTC U
  ["B4_ILKP", "INTRLOK_P"],  // PTE31 — HVIL ladder drive
  ["B8_VSSA_SWG01", "AGND"],  // VSSA_SWG01 — SWG analog ground
  ["B9_VREFL_R2R", "AGND"],  // VREFL_R2R — ADC reference low
  ["B10_TMV", "TMOD_V"],  // PTE2 ADC3_P2 — module NTC V (was PTB2: mux-address output)
  ["B11_NTCH", "NTC_H"],  // PTE6 ADC3_P3 — board NTC hot zone (PTA11 absent on this package)
  ["B12_INTB", "SBC_INTB"],  // PTC7 EIRQ[7] — FS26 INTB (EIRQ)
  ["B13_COSP", "COS_P"],  // PTD28 SDADC3_AN[0] — resolver COS+ (SDADC3 AN0)
  ["B14_COSN", "COS_N"],  // PTA1 SDADC3_AN[1] — resolver COS- (SDADC3 AN1)
  ["B16_VSS", "DGND"],  // VSS — ground
  ["C2_FCCU1", "FCCU_ERR1"],  // PTE16 FCCU_ERR1 — FCCU error out 1
  ["C3_CAN1RX", "CAN1_RX"],  // PTA22 CAN1_RX — FlexCAN1 RX
  ["B5_HWID", "HW_ID"],  // PTE0 ADC3_P0 — SKU identity. ROUND 14: moved from C12 (PTC6) — C12 is open on the NXP GEN3 board, so its port had no netlist cross-check; B5 = PTE0 is confirmed by NET-91122_C
  ["C13_ENFLYL", "MCU_EN_FLYBK_LS"],  // PTD31 — flyback LS enable (OR)
  ["T15_NTCA", "NTC_A"],  // PTC11 ADC5_S11 — board NTC ambient. ROUND 14: moved from C14 (PTD29, no netlist cross-check); T15 = PTC11 confirmed by NET-91122_C
  ["C15_SINP", "SIN_P"],  // PTB9 SDADC2_AN[0] — resolver SIN+ (SDADC2 AN0)
  ["C17_AMUX", "SBC_AMUX"],  // PTB11 ADC0_S14 — FS26 AMUX (was a named ADC0_S12 pin)
  ["D2_FCCU0", "FCCU_ERR0"],  // PTE15 FCCU_ERR0 — FCCU error out 0
  ["D3_CAN1TX", "CAN1_TX"],  // PTA23 CAN1_TX — FlexCAN1 TX
  ["D4_VSS", "DGND"],  // VSS — ground
  ["D8_ILKN", "INTRLOK_N"],  // PTE23 ADC1_P7 — HVIL signature (was PTF5: no ADC)
  ["D9_VSS", "DGND"],  // VSS — ground
  ["D5_MT2", "MT2_SIG"],  // PTE26 ADC1_P0 — motor temp 2. ROUND 14: moved from D11 (PTE18, no netlist cross-check); D5 = PTE26 confirmed by NET-91122_C
  ["D12_VREXMN", "VREXM_N"],  // PTE17 SDADC1_AN[1] — excitation monitor - (SDADC1 AN1)
  ["D13_ENFLYH", "MCU_EN_FLYBK_HS"],  // PTD30 — flyback HS enable (OR)
  ["D14_VDD_HV_A", "V5A"],  // VDD_HV_A — 5 V I/O and analog domain A
  ["D15_RDYHS", "RDY_HS"],  // PTB10 EIRQ[24] — RDY HS bank (GPIO/EIRQ)
  ["E5_VDD_HV_A", "V5A"],  // VDD_HV_A — 5 V I/O and analog domain A
  ["E6_VREFH_SAR_456", "VREF5"],  // VREFH_SAR_456 — ADC reference high (FS26 VREF 5 V)
  ["E7_VREFL_SAR_456", "AGND"],  // VREFL_SAR_456 — ADC reference low
  ["E8_V11", "V11"],  // V11 — 1.1 V core (from the external NMOS ballast)
  ["E9_VREFL_SDADC_01", "AGND"],  // VREFL_SDADC_01 — ADC reference low
  ["E10_VREFH_SDADC_01", "VREF5"],  // VREFH_SDADC_01 — ADC reference high (FS26 VREF 5 V)
  ["E11_VSSA_SDADC", "AGND"],  // VSSA_SDADC — SDADC analog ground
  ["E12_VDDA_SDADC", "V5A"],  // VDDA_SDADC — SDADC analog supply (filtered 5 V)
  ["E13_VSS", "DGND"],  // VSS — ground
  ["F1_NMOS_CTRL", "BCTRL"],  // NMOS_CTRL — gate of the external V11 ballast NMOS
  ["F2_TMW", "TMOD_W"],  // PTE5 ADC1_S8 — module NTC W (was PTB3: mux-address output)
  ["F5_VSS", "DGND"],  // VSS — ground
  ["F6_TMS", "TMS"],  // PTA4 JTAG_TMS — JTAG TMS / SWDIO
  ["F7_TCK", "TCK"],  // PTC4 JTAG_TCK — JTAG TCK / SWCLK
  ["F8_TDI", "TDI"],  // PTC5 JTAG_TDI — JTAG TDI
  ["F13_VREFH_SDADC_23", "VREF5"],  // VREFH_SDADC_23 — ADC reference high (FS26 VREF 5 V)
  ["G5_VSS", "DGND"],  // VSS — ground
  ["G7_VSS", "DGND"],  // VSS — ground
  ["G8_TDO", "TDO"],  // PTA10 JTAG_TDO — JTAG TDO / SWO
  ["G10_VDD_HV_A", "V5A"],  // VDD_HV_A — 5 V I/O and analog domain A
  ["G11_VSS", "DGND"],  // VSS — ground
  ["G13_VREFL_SDADC_23", "AGND"],  // VREFL_SDADC_23 — ADC reference low
  ["G15_CS", "SBC_CS"],  // PTF16 LPSPI3_PCS0 — LPSPI3 PCS0
  ["H1_IGN", "IGN_SNS"],  // PTA25 ADC0_S8 — KL15 sense
  ["H5_V15", "V15S"],  // V15 — 1.5 V core-regulator input/sense (GEN3 net VCORE; DS Fig. 6). ROUND 14 A12-R01: A.12 had this ball on VREF5 (5 V > 2.75 V abs max)
  ["H6_VREFH_SAR_0123", "VREF5"],  // VREFH_SAR_0123 — ADC reference high (FS26 VREF 5 V)
  ["H7_VDD_HV_A", "V5A"],  // VDD_HV_A — 5 V I/O and analog domain A
  ["H8_V11", "V11"],  // V11 — 1.1 V core (from the external NMOS ballast)
  ["H9_V11", "V11"],  // V11 — 1.1 V core (from the external NMOS ballast)
  ["H10_V11", "V11"],  // V11 — 1.1 V core (from the external NMOS ballast)
  ["H13_VDD_HV_A", "V5A"],  // VDD_HV_A — 5 V I/O and analog domain A
  ["H16_ISW", "ISNS_W"],  // PTB13 ADC0_S19 — phase current W
  ["J1_VSS", "DGND"],  // VSS — ground
  ["J4_VSS", "DGND"],  // VSS — ground
  ["J5_VSS_DCDC", "DGND"],  // VSS_DCDC — internal DC/DC ground (PMIC option)
  ["J6_VREFL_SAR_0123", "AGND"],  // VREFL_SAR_0123 — ADC reference low
  ["J7_V25", "V25"],  // V25 — internal 2.5 V flash-regulator OUTPUT: 220 nF to ground (COUT_V25 140 nF min effective; GEN3 C79/C80). ROUND 14 A12-R02: A.12 had it grounded; g
  ["J8_V11", "V11"],  // V11 — 1.1 V core (from the external NMOS ballast)
  ["J9_VSS", "DGND"],  // VSS — ground
  ["J10_V11", "V11"],  // V11 — 1.1 V core (from the external NMOS ballast)
  ["J13_V11", "V11"],  // V11 — 1.1 V core (from the external NMOS ballast)
  ["J14_VSS", "DGND"],  // VSS — ground
  ["K1_EXTAL", "EXTAL"],  // EXTAL — 40 MHz crystal
  ["K8_V11", "V11"],  // V11 — 1.1 V core (from the external NMOS ballast)
  ["K9_V11", "V11"],  // V11 — 1.1 V core (from the external NMOS ballast)
  ["K10_V11", "V11"],  // V11 — 1.1 V core (from the external NMOS ballast)
  ["K11_VDD_HV_A", "V5A"],  // VDD_HV_A — 5 V I/O and analog domain A
  ["K15_MISO", "SBC_MISO"],  // PTD20 LPSPI3_SIN — LPSPI3 SIN
  ["L1_XTAL", "XTAL"],  // XTAL — 40 MHz crystal
  ["L5_VDD_DCDC", "V5A"],  // VDD_DCDC — internal DC/DC input — follows HV_A (DS: never above VDD_HV_A; GEN3 de
  ["L7_VSS", "DGND"],  // VSS — ground
  ["L8_VDD_HV_A", "V5A"],  // VDD_HV_A — 5 V I/O and analog domain A
  ["L11_VSS", "DGND"],  // VSS — ground
  ["L14_PWMUH", "PWM_UH"],  // PTC31 PWM_1_A[0] — PWM U high
  ["L17_MOSI", "SBC_MOSI"],  // PTA17 LPSPI3_SOUT — LPSPI3 SOUT
  ["M5_VSS", "DGND"],  // VSS — ground
  ["M15_PWMUL", "PWM_UL"],  // PTA6 PWM_1_B[0] — PWM U low
  ["M16_PWMVL", "PWM_VL"],  // PTA7 PWM_1_B[1] — PWM V low
  ["M17_SCK", "SBC_SCK"],  // PTE7 LPSPI3_SCK — LPSPI3 SCK
  ["N4_VDD_HV_B", "V5A"],  // VDD_HV_B — 5 V I/O domain B (5 V like GEN3: the safety gates are 5 V LVC — a 3.3 
  ["N5_VDD_LVDS", "V3B"],  // VDD_LVDS — LVDS/LFAST supply 2.97–3.63 V (unused LFAST; DS: ramp after HV_A) — th
  ["N6_VSS", "DGND"],  // VSS — ground
  ["N7_V11", "V11"],  // V11 — 1.1 V core (from the external NMOS ballast)
  ["N8_VSS", "DGND"],  // VSS — ground
  ["N9_VDD_HV_A", "V5A"],  // VDD_HV_A — 5 V I/O and analog domain A
  ["N10_VSS", "DGND"],  // VSS — ground
  ["N15_PWMWL", "PWM_WL"],  // PTC8 PWM_1_B[2] — PWM W low
  ["N16_PWMWH", "PWM_WH"],  // PTC29 PWM_1_A[2] — PWM W high
  ["N17_PWMVH", "PWM_VH"],  // PTC30 PWM_1_A[1] — PWM V high
  ["P2_GATEEN", "MCU_GATE_EN"],  // PTD16 — gate enable (UAND1.B)
  ["P4_VSS", "DGND"],  // VSS — ground
  ["P14_VSS", "DGND"],  // VSS — ground
  ["P15_FLTHS", "FLT_HS_N"],  // PTC26 PWM_1_FAULT[0] — driver fault HS bank -> eFlexPWM1 FAULT0 (hardware PWM inhibit, FW-15)
  ["R7_VDD_HV_B", "V5A"],  // VDD_HV_B — 5 V I/O domain B (5 V like GEN3: the safety gates are 5 V LVC — a 3.3 
  ["R8_ASCCLR", "ASC_CLR_M"],  // PTD8 — ASC clear
  ["R10_VDD_HV_B", "V5A"],  // VDD_HV_B — 5 V I/O domain B (5 V like GEN3: the safety gates are 5 V LVC — a 3.3 
  ["R13_CAN0TX", "CAN0_TX"],  // PTC21 CAN0_TX — FlexCAN0 TX (GEN3 choice)
  ["R14_FLTLS", "FLT_LS_N"],  // PTC25 PWM_1_FAULT[2] — driver fault LS bank -> eFlexPWM1 FAULT2
  ["R17_VOFS", "VOFS"],  // PTB1 ADC4_S11 — receiver offset monitor
  ["T2_VSS", "DGND"],  // VSS — ground
  ["U4_ASCREQ", "ASC_REQ"],  // PTD7 — ASC request (latch clock). ROUND 14: moved from T5 (PTD6, no netlist cross-check); U4 = PTD7 confirmed by NET-91122_C
  ["T6_DRVENRB", "DRV_EN_RB"],  // PTD10 — DRV_EN read-back
  ["T7_VSS", "DGND"],  // VSS — ground
  ["T10_VSS", "DGND"],  // VSS — ground
  ["T16_VSS", "DGND"],  // VSS — ground
  ["U2_RDYLS", "RDY_LS"],  // PTB5 EIRQ[13] — RDY LS bank (GPIO/EIRQ)
  ["U5_QDIS", "QDIS_M"],  // PTD5 — discharge command
  ["U6_ASCRB", "ASC_CMD_RB"],  // PTD11 — ASC_CMD read-back
  ["U8_FLTCLR", "FLT_CLR_M"],  // PTD9 — fault-latch clear one-shot
  ["U14_CAN0RX", "CAN0_RX"],  // PTC23 CAN0_RX — FlexCAN0 RX (GEN3 choice)
];

// FS2633D REAL LQFP48 pin map (FS26 DS Rev.3 Table 3) — index = package pin. Unused pins
// terminated per the DS "recommendation for unused pins" table (boost front-end unused:
// FB/ISL/ISH grounded, gate open, PG grounded; trackers/GPIO2/WAKE2 open, OTP-disabled).
const SBC_PINS: [string, string][] = [
  ["VBST_PG", "DGND"],        // 1  boost PG — unused: grounded
  ["WAKE2", "NC_WAKE2"],      // 2  open (OTP pulldown)
  ["GPIO1", "FS_GPIO1"],      // 3  flyback hold-up enable to the OR gates
  ["TRK1", "NC_TRK1"],        // 4  tracker off by OTP
  ["TRK2", "NC_TRK2"],        // 5
  ["GPIO2", "NC_GPIO2"],      // 6
  ["TRKIN", "VPRE"],          // 7  input SUPPLY of the VREF regulator (DS: pair of LDOIN) — from VPRE, never grounded while VREF is used
  ["VREF", "VREF5"],          // 8
  ["LDO2", "V5A"],            // 9  LDO2OUT
  ["LDOIN", "VPRE"],          // 10 LDO input supply from VPRE
  ["LDO1", "V3B"],            // 11 LDO1OUT
  ["FS1B", "FS1B_N"],         // 12
  ["FS0B", "FS0B_N"],         // 13
  ["VMONEXT", "VMONX"],       // 14
  ["VMONCORE", "V15S"],       // 15
  ["RSTB", "RESET_B"],        // 16
  ["FCCU1", "FCCU_ERR0"],     // 17
  ["FCCU2", "FCCU_ERR1"],     // 18
  ["GNDFS", "DGND"],          // 19
  ["GND", "DGND"],            // 20
  ["VDIG", "VDIG"],           // 21 1.6 V digital supply — 1 uF decoupler mandatory
  ["GNDSUB", "DGND"],         // 22
  ["VDDIO", "V5A"],           // 23
  ["INTB", "SBC_INTB"],       // 24
  ["MISO", "SBC_MISO"],       // 25
  ["MOSI", "SBC_MOSI"],       // 26
  ["SCLK", "SBC_SCK"],        // 27
  ["CSB", "SBC_CS"],          // 28
  ["AMUX", "AMUXO"],          // 29
  ["VCOREFB", "V15S"],        // 30 CORE_FB senses the V15S output
  ["DEBUG", "SBC_DBG"],       // 31 mandatory — grounded via R for normal mode
  ["SWCORE", "SWCORE"],       // 32 CORE_SW
  ["CORE_BT", "CORBT"],       // 33 bootstrap cap to SWCORE
  ["CORE_IN", "VPRE"],        // 34 core-buck input from VPRE
  ["VBOS", "VBOS"],           // 35 best-of-supply decoupling — mandatory
  ["VMONPRE", "VPRE"],        // 36
  ["VPREFB", "VPRE"],         // 37 VPRE_FB
  ["NC38", "NC_SBC38"],       // 38
  ["VPRE_BT", "PREBT"],       // 39 bootstrap cap to SWPRE
  ["SWPRE", "SWPRE"],         // 40 VPRE_SW
  ["VSUP_PWR", "VBATC"],      // 41
  ["VSUP", "VBATC"],          // 42
  ["VBST_FB", "DGND"],        // 43 boost unused: grounded
  ["VBST_ISL", "DGND"],       // 44 boost unused: grounded
  ["VBST_G", "NC_VBSTG"],     // 45 boost unused: open
  ["VBST_ISH", "DGND"],       // 46 boost unused: grounded
  ["WAKE1", "WAKE1"],         // 47
  ["BATSENSE", "BATSNS"],     // 48
  ["EP", "DGND"],             // 49 exposed pad
];

export default () => (
  <board routingDisabled={NO_ROUTE} width="180mm" height="140mm" schTraceAutoLabelEnabled schMaxTraceDistance={0}>
    <net name="VBATC" isForPower />
    <net name="VPRE" isForPower />
    <net name="V11" isForPower />
    <net name="V5A" isForPower />
    <net name="V5GD" isForPower />
    <net name="V3B" isForPower />
    <net name="VREF5" isForPower />
    <net name="DGND" isGround />
    <net name="AGND" isGround />

    {/* ---- LV INPUT: fuse -> pin-side TVS pair -> reverse Schottky -> bulk + bead ----
        Round 17 (F185-F189), ISO 16750-2 test B by LET-THROUGH: no TVS on a KL30-derived net conducts at the
        35 V suppressed load dump (knee >= 36.95 V at 18 C), so the result no longer depends on the generator's
        R_i or on how its central clamp is built; everything downstream is rated for 35 V. FLVC is a 5 A
        slow-blow fuse (the 3 A polyfuse held 1.50 A at 85 C against the inverter's 2.54 A at 9 V).
        DTVSC (33 V stand-off, cathode on FCO) + DTVSC2 (18 V stand-off, cathode on DGND) in anti-series AHEAD of
        DREVC: positive surges clamp at V_BR(33) + V_F(18); negative ones (pulse 1 / 3a) at V_BR(18) + V_F(33)
        ~ 25 V, so DREVC (60 V) sees <= 38 V — with the TVS behind it, pulse 1 avalanched DREVC (~654 W vs
        P_ARM 144 W). DTVSC2 stays dark at the -14 V reverse battery. CLVC3 (100 uF hybrid polymer, POLARISED:
        pin1 + on NRC) holds pulse 2a (+112 V / 2 ohm) on NRC below the TVS knee and the 40 V FS26 / TPS55340-Q1
        ratings; the bead + MLCCs alone let it through. */}
    <chip name="FLVC" footprint={SmdFP(2)} {...gp()} pinLabels={{ pin1: "A", pin2: "B" }}
      connections={{ A: "net.KL30", B: "net.FCO" }} />
    <diode name="DTVSC" footprint={Smd2FP()} {...gp()} connections={{ anode: "net.TVSM", cathode: "net.FCO" }} />
    <diode name="DTVSC2" footprint={Smd2FP()} {...gp()} connections={{ anode: "net.TVSM", cathode: "net.DGND" }} />
    <diode name="DREVC" footprint={Smd2FP()} {...gp()} connections={{ anode: "net.FCO", cathode: "net.NRC" }} />
    <inductor name="LFC" inductance="1uH" footprint="1206" {...gp()} connections={{ pin1: "net.NRC", pin2: "net.VBATC" }} />
    <capacitor name="CLVC1" capacitance="4.7uF" footprint="1206" {...gp()} connections={{ pin1: "net.NRC", pin2: "net.DGND" }} />
    <capacitor name="CLVC3" capacitance="100uF" footprint={SmdFP(2)} {...gp()} connections={{ pin1: "net.NRC", pin2: "net.DGND" }} />
    <capacitor name="CLVC2" capacitance="22uF" footprint="1210" {...gp()} connections={{ pin1: "net.VBATC", pin2: "net.DGND" }} />
    {/* gate-power feeds to the power board: sourced HERE from the reverse-protected node,
        one polyfuse per bank, out over harness pins 19/20 (H) and 39/40 (L).
        Round 9 (self-found N17): the power board's whole LV side — V15 boost and its four
        isolated bias modules, the V5GD LDO, both flyback controllers — hung on unswitched KL30,
        about 150 mA while the vehicle sleeps (FS26 in LPOFF). QLVS now switches the feed and is
        on only while V5A is up, i.e. while the FS26 is awake (MCU resets included). 4.7 k/10 k set
        V_GS -7.9 V at 12 V KL30 (-5.8 V at 9 V; NRC is ~0.5 V below KL30), and keep a hot
        2N7002's off-state leakage (tens of uA at 85 C) under 0.5 V of gate drive. ZLVS holds V_GS
        <= 15.6 V up to the ~42 V pin-side clamp. CLVSM + RLVSM (drain-gate) set the turn-on slew to
        ~15 V/ms, so the ~45 uF of power-board input charges at <= ~1 A, not a 10-30 A spike
        at every wake (round 9 cross-check R9X-04/R9X-10). */}
    <chip name="QLVS" footprint={SmdFP(4)} {...gp()} pinLabels={{ pin1: "G", pin2: "D", pin3: "S", pin4: "TAB" }}
      connections={{ G: "net.LVS_G", D: "net.VBSW", S: "net.NRC", TAB: "net.VBSW" }} />
    <resistor name="RLVSG" resistance="10k" footprint="0603" {...gp()} connections={{ pin1: "net.NRC", pin2: "net.LVS_G" }} />
    <diode name="ZLVS" footprint={Smd2FP()} {...gp()} connections={{ anode: "net.LVS_G", cathode: "net.NRC" }} />
    <resistor name="RLVSD" resistance="4.7k" footprint="0603" {...gp()} connections={{ pin1: "net.LVS_G", pin2: "net.LVS_D" }} />
    <resistor name="RLVSM" resistance="1k" footprint="0603" {...gp()} connections={{ pin1: "net.LVS_G", pin2: "net.LVS_M" }} />
    <capacitor name="CLVSM" capacitance="100nF" footprint="0603" {...gp()} connections={{ pin1: "net.LVS_M", pin2: "net.VBSW" }} />
    <chip name="QLVN" footprint={SmdFP(3)} {...gp()} pinLabels={{ pin1: "G", pin2: "S", pin3: "D" }}
      connections={{ G: "net.V5A", S: "net.DGND", D: "net.LVS_D" }} />
    <chip name="FVBH" footprint={SmdFP(2)} {...gp()} pinLabels={{ pin1: "A", pin2: "B" }}
      connections={{ A: "net.VBSW", B: "net.VBAT_H" }} />
    <chip name="FVBL" footprint={SmdFP(2)} {...gp()} pinLabels={{ pin1: "A", pin2: "B" }}
      connections={{ A: "net.VBSW", B: "net.VBAT_L" }} />

    {/* ---- FS2633D SBC + bucks + monitors ---- */}
    <chip name="USBC" footprint={SmdFP(49)} {...gp()}
      pinLabels={Object.fromEntries(SBC_PINS.map(([l], i) => [`pin${i + 1}`, l]))}
      connections={Object.fromEntries(SBC_PINS.map(([l, n]) => [l, `net.${n}`]))} />
    <diode name="DBAT" footprint={Smd2FP()} {...gp()} connections={{ anode: "net.VBATC", cathode: "net.BATSNS" }} />
    {/* FS26 mandatory support pins (DS Rev.3): VDIG + VBOS decouplers, both buck bootstraps,
        DEBUG strapped to ground for normal mode */}
    <capacitor name="CVDIG" capacitance="1uF" footprint="0603" {...gp()} connections={{ pin1: "net.VDIG", pin2: "net.DGND" }} />
    <capacitor name="CVBOS" capacitance="4.7uF" footprint="0805" {...gp()} connections={{ pin1: "net.VBOS", pin2: "net.DGND" }} />
    <capacitor name="CBTP" capacitance="22nF" footprint="0603" {...gp()} connections={{ pin1: "net.PREBT", pin2: "net.SWPRE" }} />
    <capacitor name="CBTC" capacitance="47nF" footprint="0603" {...gp()} connections={{ pin1: "net.CORBT", pin2: "net.SWCORE" }} />
    <resistor name="RDBG" resistance="10k" footprint="0603" {...gp()} connections={{ pin1: "net.SBC_DBG", pin2: "net.DGND" }} />
    <inductor name="LSBC" inductance="10uH" footprint="1210" {...gp()} connections={{ pin1: "net.SWPRE", pin2: "net.VPRE" }} />
    {/* S32K39 core topology per DS Rev.3 Table 11: FS26 VCORE buck makes the 1.5 V V15 rail;
        the 1.14 V V11 core comes through an external NMOS ballast the MCU regulates via its
        BCTRL loop (F36 — direct VCORE->V11 is not a supported topology) */}
    {/* VCORE buck inductor: DS Table 106 selections are 1/1.5/2.2 uH only (CORE_LSEL_OTP);
        2.2 uH chosen (OTP 0x02) — 4.7 uH matched no listed selection (rev A.4.2) */}
    <inductor name="LCOR" inductance="2.2uH" footprint="1210" {...gp()} connections={{ pin1: "net.SWCORE", pin2: "net.V15S" }} />
    <chip name="QBAL" footprint={SmdFP(3)} {...gp()} pinLabels={{ pin1: "G", pin2: "S", pin3: "D" }}
      connections={{ G: "net.BCTRL", S: "net.V11", D: "net.V15S" }} />
    {/* Round 14 (A12-R04): CNMOS — NXP's NMOS gate-stability capacitor, 1 nF typ (DS Table 13), NMOS_CTRL to VSS */}
    <capacitor name="CBAL" capacitance="1nF" footprint="0603" {...gp()} connections={{ pin1: "net.BCTRL", pin2: "net.DGND" }} />
    {/* Round 14 (A12-R02): COUT_V25 — the J7 internal 2.5 V flash-regulator output capacitor, 220 nF typ / 140 nF min
        effective (DS Table 11): X7R 10 V 0603 keeps >= 180 nF after tolerance and the 2.5 V bias */}
    <capacitor name="CV25" capacitance="220nF" footprint="0603" {...gp()} connections={{ pin1: "net.V25", pin2: "net.DGND" }} />
    {/* FS26 output caps per DS: LDO1 COUT 4.7 uF (2.35-15 eff) · VREF COUT 2.2 uF
        (1.1-3.3 eff) · VBOS 4.7 uF — rev A.4.1 value completions */}
    {[["CSB1", "VPRE", "22uF"], ["CSB2", "VPRE", "22uF"], ["CSB3", "V15S", "22uF"], ["CSB3B", "V15S", "22uF"], ["CSB4", "V11", "10uF"],
      ["CSB5", "VREF5", "1uF"], ["CSB6", "V3B", "4.7uF"], ["CSB7", "V5A", "10uF"], ["CSB8", "V5A", "10uF"]].map(([n, r, v]) => (
      <capacitor key={n} name={n} capacitance={v} footprint="0805" {...gp()}
        connections={{ pin1: `net.${r}`, pin2: "net.DGND" }} />
    ))}
    {/* FS26 VMONEXT compares against a FIXED 0.8 V reference (DS Rev.3 Table 185) — divider
        scales 5 V -> 0.794 V (F34; the 10k/18.7k arrangement fed it 3.26 V = permanent OV) */}
    <resistor name="RSB1" resistance="52.3k" footprint="0603" {...gp()} connections={{ pin1: "net.V5A", pin2: "net.VMONX" }} />
    <resistor name="RSB2" resistance="10k" footprint="0603" {...gp()} connections={{ pin1: "net.VMONX", pin2: "net.DGND" }} />
    <resistor name="RSB3" resistance="220" footprint="0603" {...gp()} connections={{ pin1: "net.AMUXO", pin2: "net.SBC_AMUX" }} />
    <resistor name="RSB4" resistance="5.1k" footprint="0603" {...gp()} connections={{ pin1: "net.V5A", pin2: "net.SBC_INTB" }} />
    <resistor name="RMRST" resistance="10k" footprint="0603" {...gp()} connections={{ pin1: "net.V5A", pin2: "net.RESET_B" }} />
    {/* ignition wake divider + steering. Round 17 (F188): RIGN1 hangs BEHIND DIGN (US1M, 1000 V), so ISO 7637-2
        pulse 1 (-150 V) and a reversed KL15 are blocked before the FS26 WAKE1 pin (-0.3 V abs min, -5 mA
        reverse DC max) — straight from KL15 through 5.1 k it took ~29 mA at pulse 1. WAKE1 still reads
        (6 - 0.6) x 10/15.1 = 3.6 V at a 6 V KL15 (V_IH 0.7 x VBOS = 3.5 V worst) */}
    <resistor name="RIGN1" resistance="5.1k" footprint="0603" {...gp()} connections={{ pin1: "net.IGN_D", pin2: "net.WAKE1" }} />
    <resistor name="RIGN2" resistance="10k" footprint="0603" {...gp()} connections={{ pin1: "net.WAKE1", pin2: "net.DGND" }} />
    {/* IGN sense: 14 V -> 2.33 V at the ADC pin; 47k series limits load-dump injection to
        <1 mA (S32K39 spec 3 mA); reads as analog, thresholds in firmware (rev A.4) */}
    <diode name="DIGN" footprint={Smd2FP()} {...gp()} connections={{ anode: "net.KL15", cathode: "net.IGN_D" }} />
    <resistor name="RIGNS1" resistance="47k" footprint="0603" {...gp()} connections={{ pin1: "net.IGN_D", pin2: "net.IGN_SNS" }} />
    <resistor name="RIGNS2" resistance="10k" footprint="0603" {...gp()} connections={{ pin1: "net.IGN_SNS", pin2: "net.AGND" }} />
    <capacitor name="CIGNS" capacitance="100nF" footprint="0603" {...gp()} connections={{ pin1: "net.IGN_SNS", pin2: "net.AGND" }} />
    <capacitor name="CIGN" capacitance="100nF" footprint="0603" {...gp()} connections={{ pin1: "net.WAKE1", pin2: "net.DGND" }} />
    {/* AGND-DGND single-point tie */}
    <resistor name="RAGT" resistance="0" footprint="0805" {...gp()} connections={{ pin1: "net.AGND", pin2: "net.DGND" }} />
    {/* SKU identity pull-up (with the power board's RHWID on harness pin 2) */}
    <resistor name="RHWP" resistance="10k" footprint="0603" {...gp()} connections={{ pin1: "net.VREF5", pin2: "net.HW_ID" }} />

    {/* ---- MCU: S32K396 + clock + decoupling ---- */}
    <chip name="UMCU" footprint={SmdFP(MCU_PINS.length)} {...gp()}
      pinLabels={Object.fromEntries(MCU_PINS.map(([l], i) => [`pin${i + 1}`, l]))}
      connections={Object.fromEntries(MCU_PINS.map(([l, n]) => [l, `net.${n}`]))} />
    <chip name="Y1" footprint={SmdFP(2)} {...gp()} pinLabels={{ pin1: "A", pin2: "B" }}
      connections={{ A: "net.XTAL", B: "net.EXTAL" }} />
    <capacitor name="CYA" capacitance="12pF" footprint="0603" {...gp()} connections={{ pin1: "net.XTAL", pin2: "net.DGND" }} />
    <capacitor name="CYB" capacitance="12pF" footprint="0603" {...gp()} connections={{ pin1: "net.EXTAL", pin2: "net.DGND" }} />
    {[["CMD1", "V5A"], ["CMD2", "V5A"], ["CMD3", "V5A"], ["CMD4", "V5A"], ["CMD5", "V3B"], ["CMD6", "V3B"],
      ["CMD7", "V11"], ["CMD8", "V11"], ["CMD9", "V11"], ["CMD10", "V11"], ["CMD11", "V5A"], ["CMD12", "V5A"]].map(([n, r]) => (
      <capacitor key={n} name={n} capacitance="100nF" footprint="0603" {...gp()}
        connections={{ pin1: `net.${r}`, pin2: "net.DGND" }} />
    ))}
    <capacitor name="CMA1" capacitance="1uF" footprint="0603" {...gp()} connections={{ pin1: "net.VREF5", pin2: "net.AGND" }} />
    <capacitor name="CMA2" capacitance="100nF" footprint="0603" {...gp()} connections={{ pin1: "net.VREF5", pin2: "net.AGND" }} />
    {/* SWD/JTAG + boot */}
    <chip name="JSWD" footprint={Header(10, 2)} {...gp()}
      pinLabels={{ pin1: "VREF", pin2: "TMS", pin3: "GND1", pin4: "TCK", pin5: "GND2", pin6: "TDO", pin7: "NC7", pin8: "TDI", pin9: "GND3", pin10: "RST" }}
      connections={{ VREF: "net.V5A", TMS: "net.TMS", GND1: "net.DGND", TCK: "net.TCK", GND2: "net.DGND", TDO: "net.TDO", TDI: "net.TDI", GND3: "net.DGND", RST: "net.RESET_B" }} />
    <resistor name="RBOOT" resistance="10k" footprint="0603" {...gp()} connections={{ pin1: "net.TMS", pin2: "net.V5A" }} />
    <capacitor name="CRST" capacitance="100pF" footprint="0603" {...gp()} connections={{ pin1: "net.RESET_B", pin2: "net.DGND" }} />

    {/* ---- SAFETY: gate-enable chain + ASC latch + flyback-EN OR gates + straps ---- */}
    {/* FS0B/FS1B are open low-side outputs: 4-22 mA current limit, V_OL <= 0.4 V only up to
        2 mA, and the SBC's own read-back calls the pin low only below 0.7 V (FS26 DS Tables
        196/197). Round 7 (A6-R01): FS0B 5.1 k, the NXP value for a VDDIO pull-up. Round 8
        (A7-N04): FS1B has no pull-up of its own — RFS1+RFS2 (11 k to V5A) pull it up, so a
        FAULT_OUT short to KL30 has no low-impedance path into V5A. FS1B sinks 0.43 mA (strap)
        + <= 0.40 mA FAULT_OUT load = 0.83 mA -> ASC_SET_N <= 0.84 V. */}
    <resistor name="RENP1" resistance="5.1k" footprint="0603" {...gp()} connections={{ pin1: "net.V5A", pin2: "net.FS0B_N" }} />
    <chip name="UAND1" footprint={SmdFP(6)} {...gp()} pinLabels={{ pin1: "A", pin2: "GND", pin3: "B", pin4: "Y", pin5: "VCC", pin6: "C" }}
      connections={{ A: "net.FS0B_B", GND: "net.DGND", B: "net.MCU_GATE_EN", Y: "net.ENX1", VCC: "net.V5A", C: "net.RDY_HS_B" }} />
    <chip name="UAND2" footprint={SmdFP(6)} {...gp()} pinLabels={{ pin1: "A", pin2: "GND", pin3: "B", pin4: "Y", pin5: "VCC", pin6: "C" }}
      connections={{ A: "net.ENX1", GND: "net.DGND", B: "net.RDY_LS_B", Y: "net.DRV_EN", VCC: "net.V5A", C: "net.FLT_OKB" }} />
    {/* Round 10 (schematic recheck A9-S01): the open-drain RDY lines (5.1 k to V5GD, harness + driver
        capacitance) rise at ~20-100 ns/V, while the 74LVC1G11-Q100 allows 10 ns/V at 2.7-5.5 V despite
        its "Schmitt action" wording. A third 74LVC3G17-Q100 conditions both, so no LVC input on the
        shutdown chain runs outside its datasheet. The MCU keeps reading the raw RDY lines (PTB10/11).
        RRDB: a dead/unpowered USCH3 reads RDY_HS low -> DRV_EN low (as RSCH, RFCB); FW-16 d/e test
        each channel. Channel 3 is unused, input tied low. */}
    <chip name="USCH3" footprint={SmdFP(8)} {...gp()}
      pinLabels={{ pin1: "A1", pin2: "Y3", pin3: "A2", pin4: "GND", pin5: "Y2", pin6: "A3", pin7: "Y1", pin8: "VCC" }}
      connections={{ A1: "net.RDY_HS", Y3: "net.NC_SCH3Y", A2: "net.RDY_LS", GND: "net.DGND", Y2: "net.RDY_LS_B", A3: "net.DGND", Y1: "net.RDY_HS_B", VCC: "net.V5A" }} />
    <capacitor name="CSCH3" capacitance="100nF" footprint="0603" {...gp()} connections={{ pin1: "net.V5A", pin2: "net.DGND" }} />
    <resistor name="RRDB" resistance="100k" footprint="0603" {...gp()} connections={{ pin1: "net.RDY_HS_B", pin2: "net.DGND" }} />
    {/* Round 7 (RR01/RR02, A6-R02/R03): the two RC timing nodes and the 5.1 k FS0B edge reach
        the LVC flip-flop/AND inputs (5-10 ns/V max) only through Schmitt buffers, which carry
        no input-transition limit (74LVC3G17-Q100 DS Table 6). 1A/1Y = clear one-shot,
        2A/2Y = soft-off delay, 3A/3Y = FS0B. LVC outputs go Hi-Z unpowered (IOFF), so RSCH
        pulls FS0B_B low: a dead or open buffer forces DRV_EN low (cross-check item 4). */}
    <chip name="USCH" footprint={SmdFP(8)} {...gp()}
      pinLabels={{ pin1: "A1", pin2: "Y3", pin3: "A2", pin4: "GND", pin5: "Y2", pin6: "A3", pin7: "Y1", pin8: "VCC" }}
      connections={{ A1: "net.FLT_CLR_N", Y3: "net.FS0B_B", A2: "net.FLT_OKD", GND: "net.DGND", Y2: "net.FLT_OKB", A3: "net.FS0B_N", Y1: "net.FLT_CLR_B", VCC: "net.V5A" }} />
    <capacitor name="CSCH" capacitance="100nF" footprint="0603" {...gp()} connections={{ pin1: "net.V5A", pin2: "net.DGND" }} />
    <resistor name="RSCH" resistance="100k" footprint="0603" {...gp()} connections={{ pin1: "net.FS0B_B", pin2: "net.DGND" }} />
    {/* read-backs (round 7 cross-check): the MCU can test the whole FS0B -> DRV_EN chain and the
        ASC latch at every boot with gate power off — latent faults in USCH/UAND/ULAT no longer
        wait for the EOL rig (FW-16) */}
    <resistor name="RDRB" resistance="10k" footprint="0603" {...gp()} connections={{ pin1: "net.DRV_EN", pin2: "net.DRV_EN_RB" }} />
    <resistor name="RARB" resistance="10k" footprint="0603" {...gp()} connections={{ pin1: "net.ASC_CMD", pin2: "net.ASC_CMD_RB" }} />
    <capacitor name="CAND1" capacitance="100nF" footprint="0603" {...gp()} connections={{ pin1: "net.V5A", pin2: "net.DGND" }} />
    <capacitor name="CAND2" capacitance="100nF" footprint="0603" {...gp()} connections={{ pin1: "net.V5A", pin2: "net.DGND" }} />
    {/* hardware fault latch (rev A.4, A.6): either driver-bank FLT pulls FLT_CMB_N low ->
        async-PRESET -> /Q (FLT_OK) low -> RC -> DRV_EN low on ALL six channels.
        A.6 (review F05/F06, NSI6611 DS 1.2), thresholds per round 7 (USCH Schmitt buffer):
        - 10 k / 3.3 nF holds the global drop 22-53 us, past the faulted driver's own soft
          turn-off (the DS is silent on RST/EN during soft-off).
        - The driver releases FLT ONLY on an RST/EN rising edge after >=1.3 ms low, so the
          clear MUST briefly re-enable DRV_EN while FLT is still low (PRE=CLR=L gives /Q=H):
          a "fault-dominant" latch would deadlock recovery. The clear is therefore a hardware
          ONE-SHOT: 15 nF C0G from the MCU pin into the 10 k pull-up holds CLR low 72-210 us per
          falling edge, so a stuck-low pin cannot hold the chain permissive; the diode clamps
          the rising-edge overshoot to V5A. It does NOT stop code that keeps re-pulsing the
          pin (review RR03): that case is covered by FW-15 (FLT_HS/FLT_LS on the eFlexPWM
          fault inputs, fail-safe mode, locked: PWM stays forced low while FLT is asserted),
          the drivers' own FLT latch (no >=0.55 ms-low reset edge while DRV_EN is held high),
          and the FS26 Q&A watchdog -> FS0B (FW-12). */}
    {/* FLT diode-OR is Schottky (BAT46): FLT_CMB_N low <= 0.3 V V_OL + 0.45 V cold drop, under the
        USCH2 Schmitt V_T- minimum of 1.0 V (a 1N4148 would sit right at it cold) — round 8 */}
    <diode name="DFLT1" footprint={Smd2FP()} {...gp()} connections={{ anode: "net.FLT_CMB_N", cathode: "net.FLT_HS_N" }} />
    <diode name="DFLT2" footprint={Smd2FP()} {...gp()} connections={{ anode: "net.FLT_CMB_N", cathode: "net.FLT_LS_N" }} />
    <resistor name="RFLTC" resistance="10k" footprint="0603" {...gp()} connections={{ pin1: "net.V5GD", pin2: "net.FLT_CMB_N" }} />
    <chip name="ULAT2" footprint={SmdFP(8)} {...gp()}
      pinLabels={{ pin1: "CLK", pin2: "D", pin3: "QN", pin4: "GND", pin5: "Q", pin6: "CLR_N", pin7: "PRE_N", pin8: "VCC" }}
      connections={{ CLK: "net.DGND", D: "net.DGND", QN: "net.FLT_OK", GND: "net.DGND", Q: "net.NC_FLTQ", CLR_N: "net.FLT_CLR_B", PRE_N: "net.FLT_CMB_B", VCC: "net.V5A" }} />
    <resistor name="RFLTD" resistance="10k" footprint="0603" {...gp()} connections={{ pin1: "net.FLT_OK", pin2: "net.FLT_OKD" }} />
    <capacitor name="CFLTD" capacitance="3.3nF" footprint="0603" {...gp()} connections={{ pin1: "net.FLT_OKD", pin2: "net.DGND" }} />
    <resistor name="RLAT2" resistance="10k" footprint="0603" {...gp()} connections={{ pin1: "net.V5A", pin2: "net.FLT_CLR_N" }} />
    <capacitor name="CCLR" capacitance="15nF" footprint="0603" {...gp()} connections={{ pin1: "net.FLT_CLR_M", pin2: "net.FLT_CLR_N" }} />
    <diode name="DCLR" footprint={Smd2FP()} {...gp()} connections={{ anode: "net.FLT_CLR_N", cathode: "net.V5A" }} />
    <capacitor name="CLAT2" capacitance="100nF" footprint="0603" {...gp()} connections={{ pin1: "net.V5A", pin2: "net.DGND" }} />
    <resistor name="RGPD" resistance="10k" footprint="0603" {...gp()} connections={{ pin1: "net.DRV_EN", pin2: "net.DGND" }} />
    {/* FLT/RDY pull-ups and the diode-OR pull-up sit on the DRIVERS' rail, V5GD (harness pin 1),
        not V5A (round 9, A8-01): the NSI6611 rates FLT/RDY to VCC1 with no +0.3 V, and the two
        5 V regulators may sit 0.2 V apart. A lost V5GD now reads as FLT/RDY low — SPO, ASC masked,
        PWM and EN driven low by the eFlexPWM fault and the chain — so nothing back-powers the dead
        domain. RV5GP/RV5GS define an open pin 1 the same way and read V5GD for the MCU. */}
    <resistor name="RFLTP1" resistance="5.1k" footprint="0603" {...gp()} connections={{ pin1: "net.V5GD", pin2: "net.FLT_HS_N" }} />
    <resistor name="RFLTP2" resistance="5.1k" footprint="0603" {...gp()} connections={{ pin1: "net.V5GD", pin2: "net.FLT_LS_N" }} />
    <resistor name="RRDYP1" resistance="5.1k" footprint="0603" {...gp()} connections={{ pin1: "net.V5GD", pin2: "net.RDY_HS" }} />
    <resistor name="RRDYP2" resistance="5.1k" footprint="0603" {...gp()} connections={{ pin1: "net.V5GD", pin2: "net.RDY_LS" }} />
    {/* the V5GD pull-down is also its monitor divider (round 9 cross-check R9X-01): 47 k + 47 k put
        V5GD/2 on PTB5. The firmware treats V5GD outside 4.75-5.25 V as a supply fault. */}
    <resistor name="RV5GP" resistance="47k" footprint="0603" {...gp()} connections={{ pin1: "net.V5GD", pin2: "net.V5GD_SNS" }} />
    <resistor name="RV5GS" resistance="47k" footprint="0603" {...gp()} connections={{ pin1: "net.V5GD_SNS", pin2: "net.DGND" }} />
    {/* both FLT lines filtered alike (round 8 cross-check): a glitch now also drops ASC through UASCG and
        latches SPO, and a DESAT has no automatic retry (FW-15) — noise must not cost the drive */}
    <capacitor name="CFLTF" capacitance="100pF" footprint="0603" {...gp()} connections={{ pin1: "net.FLT_HS_N", pin2: "net.DGND" }} />
    <capacitor name="CFLTF2" capacitance="100pF" footprint="0603" {...gp()} connections={{ pin1: "net.FLT_LS_N", pin2: "net.DGND" }} />
    {/* ASC latch: MCU request clocks it, FS1B (via strap) async-sets it, MCU clear releases */}
    <chip name="ULAT" footprint={SmdFP(8)} {...gp()}
      pinLabels={{ pin1: "CLK", pin2: "D", pin3: "QN", pin4: "GND", pin5: "Q", pin6: "CLR_N", pin7: "PRE_N", pin8: "VCC" }}
      connections={{ CLK: "net.ASC_REQ", D: "net.V5A", QN: "net.NC_LATQN", GND: "net.DGND", Q: "net.ASC_Q", CLR_N: "net.ASC_CLR_N", PRE_N: "net.ASC_SET_B", VCC: "net.V5A" }} />
    {/* Round 8 (R7-01/A7-N01): any latched driver fault masks ASC — ASC_CMD = latch AND no-FLT.
        A faulted NSI6611 holds its own gate off through IN-low and EN-low with ASC high (DS 1.2
        Fig. 8.11) and turns back on only at an RST/EN rising edge; the HEALTHY low sides would
        stay on through ASC. The gate drops ASC on every path, FS1B-ASC included, so a DESAT
        always ends in SPO (§6) until the MCU deliberately re-enters ASC after the FW-15 reset. */}
    <chip name="UASCG" footprint={SmdFP(5)} {...gp()} pinLabels={{ pin1: "B", pin2: "A", pin3: "GND", pin4: "Y", pin5: "VCC" }}
      connections={{ B: "net.FLT_CMB_B", A: "net.ASC_Q", GND: "net.DGND", Y: "net.ASC_CMD", VCC: "net.V5A" }} />
    <capacitor name="CASCG" capacitance="100nF" footprint="0603" {...gp()} connections={{ pin1: "net.V5A", pin2: "net.DGND" }} />
    {/* Round 8 (R7-02, R7-03): second Schmitt buffer. 1A/1Y = FLT diode-OR -> fault-latch preset and
        the ASC gate (was a 111-135 ns/V edge into the latch); 2A/2Y = FS1B strap -> ASC-latch
        preset; 3A/3Y = MCU discharge command -> harness QDIS_CMD, so the discharge opto LED gets
        a guaranteed drive (the MCU pin only sees a CMOS input; RQDM keeps it off in reset).
        RFCB (cross-check): LVC outputs are Hi-Z unpowered, so a dead/open USCH2 reads as a latched
        FLT — SPO with ASC masked, caught by FW-16 step b — never a silently lost latch and mask. */}
    <chip name="USCH2" footprint={SmdFP(8)} {...gp()}
      pinLabels={{ pin1: "A1", pin2: "Y3", pin3: "A2", pin4: "GND", pin5: "Y2", pin6: "A3", pin7: "Y1", pin8: "VCC" }}
      connections={{ A1: "net.FLT_CMB_N", Y3: "net.QDIS_CMD", A2: "net.ASC_SET_N", GND: "net.DGND", Y2: "net.ASC_SET_B", A3: "net.QDIS_M", Y1: "net.FLT_CMB_B", VCC: "net.V5A" }} />
    <capacitor name="CSCH2" capacitance="100nF" footprint="0603" {...gp()} connections={{ pin1: "net.V5A", pin2: "net.DGND" }} />
    <resistor name="RQDM" resistance="10k" footprint="0603" {...gp()} connections={{ pin1: "net.QDIS_M", pin2: "net.DGND" }} />
    <resistor name="RFCB" resistance="100k" footprint="0603" {...gp()} connections={{ pin1: "net.FLT_CMB_B", pin2: "net.DGND" }} />
    {/* ASC entry is break-before-make without touching EN (round 7, RR05): the NSI6611 honours
        DESAT over ASC only with EN HIGH and IN+ high/IN- low (DS 1.2 §8.12) — holding EN low
        during ASC would switch the low sides' DESAT off. MCU path: eFlexPWM fault -> high-side
        PWM off, ASC_REQ, then PWM-ASC after the dead time (LS on via IN+, EN high). FS1B path:
        FS0B drops DRV_EN.
        Either way the power board's CASCD holds the LS ASC off >= 3.4 us (firmware §4c). */}
    {/* FS1B (open low-side) sets through 1k series into a 10k pulled-up node: asserted level
        <= 0.84 V (FS1B at its 0.4 V V_OL) vs the USCH2 V_T- minimum of 1.0 V; MCU clear through
        1k series against the 10k pull-up. FAULT_OUT (vehicle wire) can only SINK through DFO: a
        wire shorted to ground, a dead VCU input or a negative spike cannot reach FS1B_N and
        preset ASC. A short to KL30 is clamped at ASC_SET_N by ZSET to ground (round 8, A7-N04:
        the round-7 BAT46 clamp into V5A back-fed a sleeping rail with ~8 mA; now <= 0.7 mA).
        ZSET is the +/-2 % B5V6: <= 6.34 V at a 35 V load dump, 125 C, under the USCH2 6.5 V V_I
        abs max (a C5V6 reaches 6.62 V; a C5V1's soft knee through RFS2 would sag the released
        level toward V_T+). */}
    <resistor name="RFS1" resistance="1k" footprint="0603" {...gp()} connections={{ pin1: "net.FS1B_N", pin2: "net.ASC_SET_N" }} />
    <resistor name="RFS2" resistance="10k" footprint="0603" {...gp()} connections={{ pin1: "net.V5A", pin2: "net.ASC_SET_N" }} />
    <resistor name="RFS3" resistance="1k" footprint="0603" {...gp()} connections={{ pin1: "net.ASC_CLR_M", pin2: "net.ASC_CLR_N" }} />
    <resistor name="RFS4" resistance="1k" footprint="1206" {...gp()} connections={{ pin1: "net.FS1B_N", pin2: "net.FOUT_K" }} />  {/* round 17: 1206 anti-surge (ESR18, 0.5 W) so the 24 V/60 s jump start with FS1B held (0.48 W) sits inside the rating */}
    <diode name="DFO" footprint={Smd2FP()} {...gp()} connections={{ anode: "net.FAULT_OUT", cathode: "net.FOUT_K" }} />
    <diode name="ZSET" footprint={Smd2FP()} {...gp()} connections={{ anode: "net.DGND", cathode: "net.ASC_SET_N" }} />
    {/* RASCP on the latch output (round 8 cross-check): behind UASCG a dead/unpowered ULAT (Hi-Z) would
        float the gate input — now it reads "no ASC". ASC_CMD keeps RPD8 on the power board. */}
    <resistor name="RASCP" resistance="10k" footprint="0603" {...gp()} connections={{ pin1: "net.ASC_Q", pin2: "net.DGND" }} />
    <resistor name="RLAT1" resistance="10k" footprint="0603" {...gp()} connections={{ pin1: "net.V5A", pin2: "net.ASC_CLR_N" }} />
    <capacitor name="CLAT" capacitance="100nF" footprint="0603" {...gp()} connections={{ pin1: "net.V5A", pin2: "net.DGND" }} />
    {/* flyback enables: MCU OR FS26-GPIO1 (SBC keeps gate power up for ASC) */}
    <chip name="UOR1" footprint={SmdFP(5)} {...gp()} pinLabels={{ pin1: "B", pin2: "A", pin3: "GND", pin4: "Y", pin5: "VCC" }}
      connections={{ B: "net.FS_GPIO1", A: "net.MCU_EN_FLYBK_HS", GND: "net.DGND", Y: "net.EN_FLYBK_HS", VCC: "net.V5A" }} />
    <chip name="UOR2" footprint={SmdFP(5)} {...gp()} pinLabels={{ pin1: "B", pin2: "A", pin3: "GND", pin4: "Y", pin5: "VCC" }}
      connections={{ B: "net.FS_GPIO1", A: "net.MCU_EN_FLYBK_LS", GND: "net.DGND", Y: "net.EN_FLYBK_LS", VCC: "net.V5A" }} />
    <capacitor name="COR1" capacitance="100nF" footprint="0603" {...gp()} connections={{ pin1: "net.V5A", pin2: "net.DGND" }} />
    <capacitor name="COR2" capacitance="100nF" footprint="0603" {...gp()} connections={{ pin1: "net.V5A", pin2: "net.DGND" }} />
    {/* HVIL ladder: drive from INTRLOK_P, sense signature on INTRLOK_N (open = V5A/2) */}
    <resistor name="RILK1" resistance="10k" footprint="0603" {...gp()} connections={{ pin1: "net.INTRLOK_P", pin2: "net.HVIL_A" }} />
    <resistor name="RILK2" resistance="10k" footprint="0603" {...gp()} connections={{ pin1: "net.HVIL_B", pin2: "net.INTRLOK_N" }} />
    <resistor name="RILK3" resistance="10k" footprint="0603" {...gp()} connections={{ pin1: "net.V5A", pin2: "net.INTRLOK_N" }} />
    <resistor name="RILK4" resistance="10k" footprint="0603" {...gp()} connections={{ pin1: "net.INTRLOK_N", pin2: "net.DGND" }} />
    <capacitor name="CILK" capacitance="2.2nF" footprint="0603" {...gp()} connections={{ pin1: "net.INTRLOK_N", pin2: "net.DGND" }} />

    {/* ---- VDC differential receivers (from the power board iso amps) ----
        Receiver zero is offset to +0.5 V (buffered VREF5 divider into the + leg): a healthy
        0 V bus reads 0.5 V, while the AMC1311 fail-safe state (negative differential when
        the HV side is dead) rails the receiver to ~0 V — the MCU can tell them apart. */}
    <resistor name="ROF1" resistance="9.1k" footprint="0603" {...gp()} connections={{ pin1: "net.VREF5", pin2: "net.VOFD" }} />
    <resistor name="ROF2" resistance="1k" footprint="0603" {...gp()} connections={{ pin1: "net.VOFD", pin2: "net.AGND" }} />
    <capacitor name="COF1" capacitance="100nF" footprint="0603" {...gp()} connections={{ pin1: "net.VOFD", pin2: "net.AGND" }} />
    <chip name="UVOF" footprint={SmdFP(5)} {...gp()} pinLabels={{ pin1: "OUT", pin2: "VN", pin3: "INP", pin4: "INN", pin5: "VP" }}
      connections={{ OUT: "net.VOFS", VN: "net.AGND", INP: "net.VOFD", INN: "net.VOFS", VP: "net.V5A" }} />
    {[["1", "VDC1"], ["2", "VDC2"]].map(([k, v]) => (
      <group key={k}>
        <resistor name={`RVD${k}A`} resistance="10k" footprint="0603" {...gp()} connections={{ pin1: `net.${v}_P`, pin2: `net.VDP${k}` }} />
        <resistor name={`RVD${k}B`} resistance="10k" footprint="0603" {...gp()} connections={{ pin1: `net.VDP${k}`, pin2: "net.VOFS" }} />
        <resistor name={`RVD${k}C`} resistance="10k" footprint="0603" {...gp()} connections={{ pin1: `net.${v}_N`, pin2: `net.VDN${k}` }} />
        <resistor name={`RVD${k}D`} resistance="10k" footprint="0603" {...gp()} connections={{ pin1: `net.VDN${k}`, pin2: `net.${v}_SE` }} />
        <chip name={`UVD${k}`} footprint={SmdFP(5)} {...gp()} pinLabels={{ pin1: "OUT", pin2: "VN", pin3: "INP", pin4: "INN", pin5: "VP" }}
          connections={{ OUT: `net.${v}_SE`, VN: "net.AGND", INP: `net.VDP${k}`, INN: `net.VDN${k}`, VP: "net.V5A" }} />
        <capacitor name={`CVD${k}`} capacitance="100pF" footprint="0603" {...gp()} connections={{ pin1: `net.${v}_SE`, pin2: "net.AGND" }} />
      </group>
    ))}

    {/* ---- RESOLVER AFE (GEN3 chain) ---- */}
    {/* VMID buffers */}
    <resistor name="RVM1" resistance="24k" footprint="0603" {...gp()} connections={{ pin1: "net.V5A", pin2: "net.VMDIV1" }} />
    <resistor name="RVM2" resistance="24k" footprint="0603" {...gp()} connections={{ pin1: "net.VMDIV1", pin2: "net.AGND" }} />
    <capacitor name="CVM1" capacitance="220nF" footprint="0603" {...gp()} connections={{ pin1: "net.VMDIV1", pin2: "net.AGND" }} />
    <chip name="UVMB1" footprint={SmdFP(5)} {...gp()} pinLabels={{ pin1: "OUT", pin2: "VN", pin3: "INP", pin4: "INN", pin5: "VP" }}
      connections={{ OUT: "net.VMID_REX", VN: "net.AGND", INP: "net.VMDIV1", INN: "net.VMID_REX", VP: "net.V5A" }} />
    <resistor name="RVM3" resistance="24k" footprint="0603" {...gp()} connections={{ pin1: "net.V5A", pin2: "net.VMDIV2" }} />
    <resistor name="RVM4" resistance="24k" footprint="0603" {...gp()} connections={{ pin1: "net.VMDIV2", pin2: "net.AGND" }} />
    <capacitor name="CVM2" capacitance="220nF" footprint="0603" {...gp()} connections={{ pin1: "net.VMDIV2", pin2: "net.AGND" }} />
    <chip name="UVMB2" footprint={SmdFP(5)} {...gp()} pinLabels={{ pin1: "OUT", pin2: "VN", pin3: "INP", pin4: "INN", pin5: "VP" }}
      connections={{ OUT: "net.VMID_RSV", VN: "net.AGND", INP: "net.VMDIV2", INN: "net.VMID_RSV", VP: "net.V5A" }} />
    {/* exciter: SWG -> AC coupling -> 2nd-order MFB LPF (OPA348, gain -2.4) -> ALM2402 H-bridge -> R1/R2.
        Round 12 (R2-F04): the drawn network had the MFB feedback pair swapped (4.7 nF from the output
        to the summing node, 24 k to the inverting input), which made a first-order 2 kHz roll-off:
        |H(10 kHz)| = 0.18, so the S32K39 SWG (0.39-2.30 V pk-pk, Table 40) would have driven ~0.7 V pp
        into the resolver instead of 8 V pp. Now a textbook MFB: REXA2 in, REXA4 output->summing node,
        CEXA2 summing node->ground, REXA3 to the inverting input, CEXA1 output->inverting input.
        At 10 kHz the 100 nF is a short, so the input resistance is REXA1 + REXA2 = 13 k: passband
        gain -1.85 with the round-12 24 k (round 16: 28 k, gain -2.15, |H(10 kHz)| ~2.09 — see REXA4);
        the ALM2402's ~0.13 V/us slew at -40 C caps each output near 2.07 V pk (8.3 V pp differential),
        which the firmware trim respects by ramping the SWG amplitude up to the monitor target. The SWG amplitude register is the firmware knob against the
        excitation monitor (REXM). CEXA4 gives the SWG its specified 25-100 pF load (Table 40).
        OPA348: +0.15 % / -2.3 deg at 10 kHz from its 1 MHz GBW (Opus check, round 12). */}
    <capacitor name="CEXA4" capacitance="47pF" footprint="0603" {...gp()} connections={{ pin1: "net.SWG1", pin2: "net.AGND" }} />
    <resistor name="REXA1" resistance="3k" footprint="0603" {...gp()} connections={{ pin1: "net.SWG1", pin2: "net.NEX1" }} />
    <capacitor name="CEXA3" capacitance="100nF" footprint="0603" {...gp()} connections={{ pin1: "net.NEX1", pin2: "net.NEX2" }} />
    <resistor name="REXA2" resistance="10k" footprint="0603" {...gp()} connections={{ pin1: "net.NEX2", pin2: "net.NEX3" }} />
    <capacitor name="CEXA2" capacitance="1.5nF" footprint="0603" {...gp()} connections={{ pin1: "net.NEX3", pin2: "net.AGND" }} />
    <resistor name="REXA3" resistance="10k" footprint="0603" {...gp()} connections={{ pin1: "net.NEX3", pin2: "net.NEX4" }} />
    <capacitor name="CEXA1" capacitance="220pF" footprint="0603" {...gp()} connections={{ pin1: "net.REX_F", pin2: "net.NEX4" }} />
    {/* Round 16 (A14-R04, review 3): with the series RSX + PTC losses (x0.909 to the winding) the SWG's LOW
        corner (MAXAPP 1.884 V pp) delivered only 6.34 V pp at the winding through the 24 k (gain 1.85) —
        under the 6.5 V pp resolver floor. 28 k: gain 2.15 (|H(10 kHz)| ~2.09, f0 16.6 kHz): low corner
        7.9 V pp at the amplifier -> 7.2 V pp at the winding. The firmware SWG trim (FW-10) holds 7.2 V pp
        at the MONITOR plane (needs SWG ~1.83 V pp <= 1.884 low corner, 3 % headroom), i.e. 7.64 V pp at the amplifier
        (1.9 V pk per output, under the -40 C slew ceiling) and 6.94 V pp at the winding cold; the untrimmed
        SWG max corner would slew-limit, so the trim ramps up from ~1.5 V pp — the register never starts at
        its maximum. Post-trip (PTC 5 R) the winding sees ~6.3 V pp and the FW-10 window flags the line. */}
    <resistor name="REXA4" resistance="28k" footprint="0603" {...gp()} connections={{ pin1: "net.REX_F", pin2: "net.NEX3" }} />
    <chip name="UEXF" footprint={SmdFP(5)} {...gp()} pinLabels={{ pin1: "OUT", pin2: "VN", pin3: "INP", pin4: "INN", pin5: "VP" }}
      connections={{ OUT: "net.REX_F", VN: "net.AGND", INP: "net.VMID_REX", INN: "net.NEX4", VP: "net.V5A" }} />
    {/* resolver-amp supply: ALM2402 abs max is 18 V (rec 16 V) — VBATC can see 24 V jump
        start and ~33 V clamped transients, and the KL30 clamp (TPSMC24CA then; the 33 V let-through pair since A.16, dark at 35 V) sits far above 18 V. ULDOEX
        (same 40 V ADJ LDO family as ULDO15) makes a protected 12.1 V VEXD rail; in dropout
        at crank it degrades exactly like the old direct feed (rev A.4.2). INH follows V5A (round 9
        cross-check R9X-03): tied to VBATC it kept the exciter drawing ~0.9 mA in FS26 LPOFF. */}
    <chip name="ULDOEX" footprint={SmdFP(5)} {...gp()} pinLabels={{ pin1: "IN", pin2: "INH", pin3: "GND", pin4: "VA", pin5: "OUT" }}
      connections={{ IN: "net.VBATC", INH: "net.V5A", GND: "net.AGND", VA: "net.VEXVA", OUT: "net.VEXD" }} />
    <resistor name="RLDE1" resistance="38.3k" footprint="0603" {...gp()} connections={{ pin1: "net.VEXD", pin2: "net.VEXVA" }} />
    <resistor name="RLDE2" resistance="10k" footprint="0603" {...gp()} connections={{ pin1: "net.VEXVA", pin2: "net.AGND" }} />
    <capacitor name="CLDEC" capacitance="270pF" footprint="0603" {...gp()} connections={{ pin1: "net.VEXD", pin2: "net.VEXVA" }} />
    <capacitor name="CLDE" capacitance="22uF" footprint="1210" {...gp()} connections={{ pin1: "net.VEXD", pin2: "net.AGND" }} />
    <chip name="UEXD" footprint={SmdFP(14)} {...gp()}
      pinLabels={{ pin1: "IN1N", pin2: "IN1P", pin3: "SDN", pin4: "IN2P", pin5: "IN2N", pin6: "GND1", pin7: "NC7", pin8: "NC8", pin9: "OUT2", pin10: "VCCO2", pin11: "VCC", pin12: "VCCO1", pin13: "OUT1", pin14: "GND2" }}
      connections={{ IN1N: "net.EXN1", IN1P: "net.VMID_REX", SDN: "net.EXSD", IN2P: "net.VMID_REX", IN2N: "net.EXN2", GND1: "net.AGND", NC7: "net.NC_EXD7", NC8: "net.NC_EXD8", OUT2: "net.VREX_N", VCCO2: "net.VEXD", VCC: "net.VEXD", VCCO1: "net.VEXD", OUT1: "net.VREX_P", GND2: "net.AGND" }} />
    <resistor name="RSDN" resistance="10k" footprint="0603" {...gp()} connections={{ pin1: "net.V5A", pin2: "net.EXSD" }} />
    {/* Round 14 (F04) / round 15 (A13-R01, R02): terminal-fault protection of the ALM2402 outputs (abs max 18 V;
        reverse output diodes are pulse-rated only, DS 8.3.6). Per line, from the amplifier outwards:
          OUT --RSX 2.2R-- VREX_xX --FEXP (PTC 0.2 A, 33 V)-- VREX_xC (vehicle connector)
                             |
                           TVSEx (SMCJ8.5CA) to AGND
        The TVS sits on the PROTECTED (amplifier) side of the PTC, so an external battery fault can reach
        the clamp only THROUGH the PTC — the round-14 drawing had the TVS on the connector node, where the
        fault current bypassed the PTC (A13-R01). Round 16: the TVS is UNIDIRECTIONAL (SMCJ8.5A, cathode on
        the node): the excitation never goes below ground (4-8 V around VMID), so a negative harness fault is
        carried by the TVS forward diode at -0.7...-1.2 V (I_FSM 200 A) and the amplifier's lower output diode
        sees < 0.3 A through RSX instead of 4.4 A (A14-R02). Positive clamp ~10.4-11.5 V: below the 12.1 V
        rail + a diode while VEXD is up; with VEXD absent/cranking the amplifier's reverse diode charges the
        26.7 uF rail (CLDE + CEXD) through RSX — 4.9 A peak decaying with tau = 59 us (0.19 mJ in the diode,
        rail then ~10.8 V, under the 18 V abs max) until the PTC trips; the diode's pulse envelope is not
        published (DS 8.3.6: pulsed use) — bench gate 28 measures it with VEXD off/cranking/on. The monitor
        (REXM) taps the protected node — the winding sees that minus the PTC/harness drop (x0.964 cold,
        x0.875 for an hour after a trip); the feedback (REXB) stays at the amplifier output. design-verify §7c:
        fault currents, TVS energy (conditional on the PTC clearing time), back-drive, amplitude planes. */}
    <resistor name="RSXP" resistance="2.2" footprint="1206" {...gp()} connections={{ pin1: "net.VREX_P", pin2: "net.VREX_PX" }} />
    <resistor name="RSXN" resistance="2.2" footprint="1206" {...gp()} connections={{ pin1: "net.VREX_N", pin2: "net.VREX_NX" }} />
    <chip name="TVSEP" footprint={SmdFP(2)} {...gp()} pinLabels={{ pin1: "K", pin2: "A" }} connections={{ K: "net.VREX_PX", A: "net.AGND" }} />
    <chip name="TVSEN" footprint={SmdFP(2)} {...gp()} pinLabels={{ pin1: "K", pin2: "A" }} connections={{ K: "net.VREX_NX", A: "net.AGND" }} />
    {/* Round 17 (gap closure of the A.15 OPEN back-drive row): with VEXD absent or cranking, a positive harness fault
        clamped by the TVS above the rail charges the 26.7 uF rail through the amplifier's internal reverse diodes
        (ALM2402 DS 8.3.6: pulsed use only, no energy envelope published). DEXP/DEXN give that charge a RATED path:
        a Schottky from each protected node to VEXD conducts at ~0.45 V, a good 0.3 V below the internal diodes, so
        the 4.9 A / 59 us exponential (0.29 mC) is carried by a part with a datasheet I_FSM; NCV4276C output abs max
        40 V tolerates the rail sitting at the clamp with its input at 0 V. Dark in normal operation: the node swings
        0.6-4.4 V, the rail is 12.1 V; dark during a rail-present fault too (SMCJ8.5A clamps below 12.8 V up to ~40 A). */}
    <diode name="DEXP" footprint={Smd2FP()} {...gp()} connections={{ anode: "net.VREX_PX", cathode: "net.VEXD" }} />
    <diode name="DEXN" footprint={Smd2FP()} {...gp()} connections={{ anode: "net.VREX_NX", cathode: "net.VEXD" }} />
    <chip name="FEXP" footprint={SmdFP(2)} {...gp()} pinLabels={{ pin1: "A", pin2: "B" }} connections={{ A: "net.VREX_PX", B: "net.VREX_PC" }} />
    <chip name="FEXN" footprint={SmdFP(2)} {...gp()} pinLabels={{ pin1: "A", pin2: "B" }} connections={{ A: "net.VREX_NX", B: "net.VREX_NC" }} />
    <resistor name="REXB1" resistance="24k" footprint="0603" {...gp()} connections={{ pin1: "net.REX_F", pin2: "net.EXN1" }} />
    <resistor name="REXB2" resistance="24k" footprint="0603" {...gp()} connections={{ pin1: "net.VREX_P", pin2: "net.EXN1" }} />
    <resistor name="REXB3" resistance="24k" footprint="0603" {...gp()} connections={{ pin1: "net.VREX_P", pin2: "net.EXN2" }} />
    <resistor name="REXB4" resistance="24k" footprint="0603" {...gp()} connections={{ pin1: "net.VREX_N", pin2: "net.EXN2" }} />
    <capacitor name="CEXD" capacitance="4.7uF" footprint="1206" {...gp()} connections={{ pin1: "net.VEXD", pin2: "net.AGND" }} />
    {/* excitation monitor dividers (GEN3: 4.99k/12.1k, 4.99k/24k) */}
    {/* Round 14 (F03/F05): the monitor dividers now hold the S32K39 3 mA per-pad injection limit with the MCU unpowered
        (an excitation wire shorted to 35 V: 35/(18 k x 0.99) = 1.96 mA; 50 V: 2.81 mA — the round-12 5.1 k gave 5.5-5.7 mA),
        same ratios as before (42.2/60.2 = 0.701, 84.5/102.5 = 0.824); Thevenin 12.6 k / 14.8 k per leg (<= 20 k R_AAF, Table 38)
        with the 220 pF C_AAF across the pair: corner ~26 kHz, -21 deg at 10 kHz vs -24 deg on SIN/COS — a fixed 3 deg
        demodulation-reference offset, calibrated with the channel matching (FW-20). */}
    <resistor name="REXM1" resistance="18k" footprint="0603" {...gp()} connections={{ pin1: "net.VREX_PX", pin2: "net.VREXM_P" }} />
    <resistor name="REXM2" resistance="42.2k" footprint="0603" {...gp()} connections={{ pin1: "net.VREXM_P", pin2: "net.AGND" }} />
    <resistor name="REXM3" resistance="18k" footprint="0603" {...gp()} connections={{ pin1: "net.VREX_NX", pin2: "net.VREXM_N" }} />
    <resistor name="REXM4" resistance="84.5k" footprint="0603" {...gp()} connections={{ pin1: "net.VREXM_N", pin2: "net.AGND" }} />
    <capacitor name="CEXM" capacitance="220pF" footprint="0603" {...gp()} connections={{ pin1: "net.VREXM_P", pin2: "net.VREXM_N" }} />
    {/* sin/cos conditioning: 12 k bias pair to VMID + 12 k series + clamps + RC to SDADC.
        Round 12 (R2-F13): the resolver wires share the vehicle connector with KL30. With the GEN3
        680 R / 330 R a wire shorted to 16 V pushed (16 - 5.7)/450 = 23 mA into an SDADC pin against
        the S32K39's 3 mA limit (operating AND absolute maximum, no transient allowance).
        Round 13 (A11-R02): the bound must hold with the MCU rail at 0 V too (clamp at ~0.7 V, or 0 V
        worst): 35 V into a 0 V node through 0.99 x (12 k + 120) = 2.92 mA. Both legs pull up through
        the winding, so the VMID buffer sinks 2 x (35 - 2.5)/12 k = 5.4 mA (OPA348 ~7 mA at 125 C;
        it saturates toward V5A in that fault, which FW-10 sees as an invalid resolver). Pulse
        dissipation 98 mW in a 12 k for 0.4 s. Caps: the SDADC needs C_AAF >= 180 pF (220 pF typ)
        directly across its inputs (S32K39 Table 38: R_AAF 5-20 k, C_AAF 180-220 pF) — the round-12
        100 pF was below that — so 220 pF C0G at the pins, 47 pF at the clamp node, 22 pF common-mode
        on BOTH legs: corner ~22 kHz, -24 deg at 10 kHz on both channels (the excitation-monitor
        path carries the phase reference); source 24 k vs the SDADC's Z_DIFF 215-380 k is a gain
        term that cancels only as far as the two channels match (worst independent corners 1.1 deg
        electrical), so it is an EOL calibration item (FW-20), not "ratio-cancelled". */}
    {[["SIN", "S1", "S3"], ["COS", "S2", "S4"]].map(([s, a, b]) => (
      <group key={s}>
        <resistor name={`R${s}1`} resistance="12k" footprint="0603" {...gp()} connections={{ pin1: `net.RSLV_${a}`, pin2: "net.VMID_RSV" }} />
        <resistor name={`R${s}2`} resistance="12k" footprint="0603" {...gp()} connections={{ pin1: `net.RSLV_${b}`, pin2: "net.VMID_RSV" }} />
        <resistor name={`R${s}F1`} resistance="12k" footprint="0603" {...gp()} connections={{ pin1: `net.RSLV_${a}`, pin2: `net.${s}F_P` }} />
        <resistor name={`R${s}F2`} resistance="12k" footprint="0603" {...gp()} connections={{ pin1: `net.RSLV_${b}`, pin2: `net.${s}F_N` }} />
        <chip name={`D${s}P`} footprint={SmdFP(3)} {...gp()} pinLabels={{ pin1: "A", pin2: "B", pin3: "G" }}
          connections={{ A: `net.${s}F_P`, B: `net.${s}F_N`, G: "net.AGND" }} />
        <capacitor name={`C${s}D`} capacitance="47pF" footprint="0603" {...gp()} connections={{ pin1: `net.${s}F_P`, pin2: `net.${s}F_N` }} />
        <capacitor name={`C${s}F1`} capacitance="22pF" footprint="0603" {...gp()} connections={{ pin1: `net.${s}F_P`, pin2: "net.VMID_RSV" }} />
        <capacitor name={`C${s}F2`} capacitance="22pF" footprint="0603" {...gp()} connections={{ pin1: `net.${s}F_N`, pin2: "net.VMID_RSV" }} />
        <resistor name={`R${s}R1`} resistance="120" footprint="0603" {...gp()} connections={{ pin1: `net.${s}F_P`, pin2: `net.${s}_P` }} />
        <resistor name={`R${s}R2`} resistance="120" footprint="0603" {...gp()} connections={{ pin1: `net.${s}F_N`, pin2: `net.${s}_N` }} />
        <capacitor name={`C${s}A1`} capacitance="22pF" footprint="0603" {...gp()} connections={{ pin1: `net.${s}_P`, pin2: "net.AGND" }} />
        <capacitor name={`C${s}A3`} capacitance="22pF" footprint="0603" {...gp()} connections={{ pin1: `net.${s}_N`, pin2: "net.AGND" }} />
        <capacitor name={`C${s}A2`} capacitance="220pF" footprint="0603" {...gp()} connections={{ pin1: `net.${s}_P`, pin2: `net.${s}_N` }} />
      </group>
    ))}

    {/* ---- PHASE CURRENT: hall harness + 3x buffer chains (GEN3) ---- */}
    <chip name="JLEM" footprint={Header(10, 2)} {...gp()}
      pinLabels={{ pin1: "S5U", pin2: "OU", pin3: "G1", pin4: "S5V", pin5: "OV", pin6: "G2", pin7: "S5W", pin8: "OW", pin9: "SH", pin10: "G3" }}
      connections={{ S5U: "net.V5S_U", OU: "net.HALL_U", G1: "net.AGND", S5V: "net.V5S_V", OV: "net.HALL_V", G2: "net.AGND", S5W: "net.V5S_W", OW: "net.HALL_W", SH: "net.AGND", G3: "net.AGND" }} />
    {/* Round 12 (R1-F04): the real HC5FW 900-S terminals (DS p.2): 1 V_ref (reference OUTPUT, left
        open — optional 1-4.7 nF to Gnd), 2 V_out, 3 Gnd, 4 U_C 5 V, E1-E4 mass pins to Gnd. It is a
        PCB-mount THT device with no connector, so it sits on the off-board carrier at each phase
        busbar; the carrier drawing derives from THIS symbol, not from the old 3-pin abstraction
        (which had VCC on terminal 1 = V_ref). RL >= 10 k, CL <= 6.8 nF, output 0.2-4.8 V. */}
    {PH.map((x) => (
      <chip key={x} name={`USNS${x}`} footprint={SmdFP(8)} {...gp()}
        pinLabels={{ pin1: "VREF", pin2: "OUT", pin3: "GND", pin4: "VCC", pin5: "E1", pin6: "E2", pin7: "E3", pin8: "E4" }}
        connections={{ VREF: `net.NC_SNS${x}R`, OUT: `net.HALL_${x}`, GND: "net.AGND", VCC: `net.V5S_${x}`, E1: "net.AGND", E2: "net.AGND", E3: "net.AGND", E4: "net.AGND" }} />
    ))}
    {PH.map((x) => (
      <HallChain key={x} ph={x} vout={`net.HALL_${x}`} adc={`net.ISNS_${x}`} />
    ))}

    {/* ---- TEMPERATURES ---- */}
    {PH.map((x) => (
      <group key={x}>
        <resistor name={`RSN${x}P`} resistance="5.1k" footprint="0603" {...gp()} connections={{ pin1: "net.VREF5", pin2: `net.TMOD_${x}` }} />
        <capacitor name={`CSN${x}F`} capacitance="47nF" footprint="0603" {...gp()} connections={{ pin1: `net.TMOD_${x}`, pin2: "net.AGND" }} />
      </group>
    ))}
    {/* module-NTC return from the harness ties to the analog reference here (star point) */}
    <resistor name="RTMR" resistance="0" footprint="0603" {...gp()} connections={{ pin1: "net.TMOD_RTN", pin2: "net.AGND" }} />
    <NtcIn id="AMB" out="net.NTC_A" />
    <NtcIn id="HS" out="net.NTC_H" />
    {/* motor temperature: fuse + clamp + zero-drift buffer (GEN3 exact) */}
    {[["1", "MT1"], ["2", "MT2"]].map(([k, m]) => (
      <group key={k}>
        <chip name={`FMT${k}`} footprint={SmdFP(2)} {...gp()} pinLabels={{ pin1: "A", pin2: "B" }}
          connections={{ A: `net.${m}_RAW`, B: `net.${m}_F` }} />
        {/* SMAJ5.0A (round 14, F18): cathode on the line, anode on AGND — a real 400 W clamp, drawn as the diode it is */}
        <chip name={`TVSM${k}`} footprint={SmdFP(2)} {...gp()} pinLabels={{ pin1: "K", pin2: "A" }}
          connections={{ K: `net.${m}_F`, A: "net.AGND" }} />
        <resistor name={`RMT${k}P`} resistance="10k" footprint="0603" {...gp()} connections={{ pin1: "net.VREF5", pin2: `net.${m}_F` }} />
        <resistor name={`RMT${k}S`} resistance="1k" footprint="0603" {...gp()} connections={{ pin1: `net.${m}_F`, pin2: `net.${m}_B` }} />
        <capacitor name={`CMT${k}F`} capacitance="47nF" footprint="0603" {...gp()} connections={{ pin1: `net.${m}_B`, pin2: "net.AGND" }} />
        <chip name={`UMT${k}`} footprint={SmdFP(5)} {...gp()} pinLabels={{ pin1: "OUT", pin2: "VN", pin3: "INP", pin4: "INN", pin5: "VP" }}
          connections={{ OUT: `net.${m}_SIG`, VN: "net.AGND", INP: `net.${m}_B`, INN: `net.${m}_SIG`, VP: "net.V5A" }} />
      </group>
    ))}

    {/* ---- COMMS: 2x CAN-FD ---- */}
    <CanFd id="1" tx="net.CAN0_TX" rx="net.CAN0_RX" stb="net.DGND" canh="net.CANH1" canl="net.CANL1" />
    <CanFd id="2" tx="net.CAN1_TX" rx="net.CAN1_RX" stb="net.DGND" canh="net.CANH2" canl="net.CANL2" />

    {/* ---- VEHICLE CONNECTOR (fused + beaded lines per GEN3) ---- */}
    <chip name="JVEH" footprint={Header(23)} {...gp()}
      pinLabels={{ pin1: "KL30", pin2: "GND1", pin3: "KL15", pin4: "CANH1", pin5: "CANL1", pin6: "CANH2", pin7: "CANL2", pin8: "FAULT", pin9: "MT1P", pin10: "MT1R", pin11: "MT2P", pin12: "MT2R", pin13: "R1", pin14: "R2", pin15: "S1", pin16: "S3", pin17: "S2", pin18: "S4", pin19: "SHLDR", pin20: "SHLDS", pin21: "GND2", pin22: "SP1", pin23: "SP2" }}
      connections={{ KL30: "net.KL30", GND1: "net.DGND", KL15: "net.KL15R", CANH1: "net.VCANH1", CANL1: "net.VCANL1", CANH2: "net.VCANH2", CANL2: "net.VCANL2", FAULT: "net.FAULT_OUT", MT1P: "net.MT1_RAW", MT1R: "net.AGND", MT2P: "net.MT2_RAW", MT2R: "net.AGND", R1: "net.VREX_PC", R2: "net.VREX_NC", S1: "net.RSLV_S1", S3: "net.RSLV_S3", S2: "net.RSLV_S2", S4: "net.RSLV_S4", SHLDR: "net.DGND", SHLDS: "net.DGND", GND2: "net.DGND", SP1: "net.NC_VSP1", SP2: "net.NC_VSP2" }} />
    <chip name="FVS1" footprint={SmdFP(2)} {...gp()} pinLabels={{ pin1: "A", pin2: "B" }}
      connections={{ A: "net.KL15R", B: "net.KL15" }} />
    {[["LVS1", "VCANH1", "CANH1"], ["LVS2", "VCANL1", "CANL1"], ["LVS3", "VCANH2", "CANH2"], ["LVS4", "VCANL2", "CANL2"]].map(([n, a, b]) => (
      <inductor key={n} name={n} inductance="1uH" footprint="0603" {...gp()} connections={{ pin1: `net.${a}`, pin2: `net.${b}` }} />
    ))}

    {/* ---- HARNESS ---- */}
    <Harness name="JICC" />
    {["MCU_GATE_EN", "ASC_REQ", "MCU_EN_FLYBK_HS", "MCU_EN_FLYBK_LS"].map((n, i) => (
      <resistor key={n} name={`RCPD${i}`} resistance="10k" footprint="0603" {...gp()}
        connections={{ pin1: `net.${n}`, pin2: "net.DGND" }} />
    ))}
  </board>
);
