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

// MCU signal map: label -> net (labels are GEN3 port names where known)
const MCU_PINS: [string, string][] = [
  // power
  ["VDD5_1", "V5A"], ["VDD5_2", "V5A"], ["VDD5_3", "V5A"], ["VDD5_4", "V5A"],
  ["VDD3_1", "V3B"], ["VDD3_2", "V3B"],
  ["V11_1", "V11"], ["V11_2", "V11"], ["V11_3", "V11"], ["V11_4", "V11"],
  ["V15_IN", "V15S"], ["BCTRL", "BCTRL"],
  ["VDDA", "V5A"], ["VREFH1", "VREF5"], ["VREFH2", "VREF5"], ["VREFL", "AGND"],
  ["VSS1", "DGND"], ["VSS2", "DGND"], ["VSS3", "DGND"],
  // clock + reset + debug
  ["XTAL", "XTAL"], ["EXTAL", "EXTAL"], ["RESET_B", "RESET_B"],
  ["TCK", "TCK"], ["TMS", "TMS"], ["TDI", "TDI"], ["TDO", "TDO"],
  // FS26 SBC (GEN3: PTD20/PTA17/PTE7/PTF16, PTE15/16, PTC7, PTE1)
  ["PTD20_MISO", "SBC_MISO"], ["PTA17_MOSI", "SBC_MOSI"], ["PTE7_SCK", "SBC_SCK"], ["PTF16_CS", "SBC_CS"],
  ["PTE15_FCCU0", "FCCU_ERR0"], ["PTE16_FCCU1", "FCCU_ERR1"],
  ["PTC7_INTB", "SBC_INTB"], ["ADC0_S12", "SBC_AMUX"],
  // PWM (GEN3 exact ports)
  ["PTC31_PWMUH", "PWM_UH"], ["PTG10_PWMUL", "PWM_UL"], ["PTC30_PWMVH", "PWM_VH"],
  ["PTA6_PWMVL", "PWM_VL"], ["PTC29_PWMWH", "PWM_WH"], ["PTA7_PWMWL", "PWM_WL"],
  // driver feedback + enables
  ["PTC26_FLTHS", "FLT_HS_N"], ["PTC25_FLTLS", "FLT_LS_N"],
  ["PTB10_RDYHS", "RDY_HS"], ["PTB11_RDYLS", "RDY_LS"],
  ["PTD16_GATEEN", "MCU_GATE_EN"],
  ["PTD30_ENFLYH", "MCU_EN_FLYBK_HS"], ["PTD31_ENFLYL", "MCU_EN_FLYBK_LS"],
  ["PTD6_ASCREQ", "ASC_REQ"], ["PTD8_ASCCLR", "ASC_CLR_M"], ["PTD5_QDIS", "QDIS_M"],
  ["PTD9_FLTCLR", "FLT_CLR_M"],
  ["PTD10_DRVENRB", "DRV_EN_RB"], ["PTD11_ASCRB", "ASC_CMD_RB"],
  // analog
  ["PTA0_VDC1", "VDC1_SE"], ["PTB0_VDC2", "VDC2_SE"],
  // review A.6: the receivers' shared +0.5 V offset (VOFS) and the SKU identity are read too —
  // a failed UVOF would shift BOTH VDC channels equally (invisible to the 5 % cross-check)
  ["PTB1_VOFS", "VOFS"], ["PTB4_HWID", "HW_ID"],
  // round 9 cross-check (R9X-01/06): V5GD itself is read — a dead or back-powered (hovering) V5GD
  // also unpowers the AMC1311 LV sides, whose receivers would then read a false "0 V bus"
  ["PTB5_V5GD", "V5GD_SNS"],
  ["PTA8_ISU", "ISNS_U"], ["PTB8_ISV", "ISNS_V"], ["PTB13_ISW", "ISNS_W"],
  ["PTA9_TMU", "TMOD_U"], ["PTB2_TMV", "TMOD_V"], ["PTB3_TMW", "TMOD_W"],
  ["PTA10_NTCA", "NTC_A"], ["PTA11_NTCH", "NTC_H"],
  ["PTA12_MT1", "MT1_SIG"], ["PTA13_MT2", "MT2_SIG"],
  // resolver (SWG excitation + SDADC differential pairs — GEN3)
  ["SWG1", "SWG1"], ["SDADC1_P", "SIN_P"], ["SDADC1_N", "SIN_N"],
  ["SDADC2_P", "COS_P"], ["SDADC2_N", "COS_N"],
  ["SDD01_P", "VREXM_P"], ["SDD01_N", "VREXM_N"],
  // interlock + CAN + ignition
  ["PTE31_ILKP", "INTRLOK_P"], ["PTF5_ILKN", "INTRLOK_N"],
  ["CAN0_TX", "CAN0_TX"], ["CAN0_RX", "CAN0_RX"], ["CAN1_TX", "CAN1_TX"], ["CAN1_RX", "CAN1_RX"],
  ["PTA25_IGN", "IGN_SNS"],
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

    {/* ---- LV INPUT: fuse + reverse + TVS + bead (GEN3 exact parts) ---- */}
    <chip name="FLVC" footprint={SmdFP(2)} {...gp()} pinLabels={{ pin1: "A", pin2: "B" }}
      connections={{ A: "net.KL30", B: "net.FCO" }} />
    <diode name="DREVC" footprint={Smd2FP()} {...gp()} connections={{ anode: "net.FCO", cathode: "net.NRC" }} />
    <diode name="DTVSC" footprint={Smd2FP()} {...gp()} connections={{ anode: "net.DGND", cathode: "net.NRC" }} />
    <inductor name="LFC" inductance="1uH" footprint="1206" {...gp()} connections={{ pin1: "net.NRC", pin2: "net.VBATC" }} />
    <capacitor name="CLVC1" capacitance="4.7uF" footprint="1206" {...gp()} connections={{ pin1: "net.NRC", pin2: "net.DGND" }} />
    <capacitor name="CLVC2" capacitance="22uF" footprint="1210" {...gp()} connections={{ pin1: "net.VBATC", pin2: "net.DGND" }} />
    {/* gate-power feeds to the power board: sourced HERE from the reverse-protected node,
        one polyfuse per bank, out over harness pins 19/20 (H) and 39/40 (L).
        Round 9 (self-found N17): the power board's whole LV side — V15 boost and its four
        isolated bias modules, the V5GD LDO, both flyback controllers — hung on unswitched KL30,
        about 150 mA while the vehicle sleeps (FS26 in LPOFF). QLVS now switches the feed and is
        on only while V5A is up, i.e. while the FS26 is awake (MCU resets included). 4.7 k/10 k set
        V_GS -7.9 V at 12 V KL30 (-5.8 V at 9 V; NRC is ~0.5 V below KL30), and keep a hot
        2N7002's off-state leakage (tens of uA at 85 C) under 0.5 V of gate drive. ZLVS holds V_GS
        <= 15.6 V up to the 39 V TVS clamp. CLVSM + RLVSM (drain-gate) set the turn-on slew to
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
    {/* ignition wake divider + steering */}
    <resistor name="RIGN1" resistance="5.1k" footprint="0603" {...gp()} connections={{ pin1: "net.KL15", pin2: "net.WAKE1" }} />
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
    <resistor name="RFS4" resistance="1k" footprint="0603" {...gp()} connections={{ pin1: "net.FS1B_N", pin2: "net.FOUT_K" }} />
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
        gain -1.85, f0 17.9 kHz, Q 0.77, |H(10 kHz)| 1.85 (+/-6 % over +/-10 % caps and 1 % resistors):
        SWG 2.09 V pp -> 3.9 V pp at REX_F -> 7.7 V pp differential (7.0-8.5 over the SWG range).
        24 k is the ceiling: the ALM2402's ~0.13 V/us slew at -40 C caps each output near 2.07 V pk
        (8.3 V pp differential). The SWG amplitude register is the firmware knob against the
        excitation monitor (REXM). CEXA4 gives the SWG its specified 25-100 pF load (Table 40).
        OPA348: +0.15 % / -2.3 deg at 10 kHz from its 1 MHz GBW (Opus check, round 12). */}
    <capacitor name="CEXA4" capacitance="47pF" footprint="0603" {...gp()} connections={{ pin1: "net.SWG1", pin2: "net.AGND" }} />
    <resistor name="REXA1" resistance="3k" footprint="0603" {...gp()} connections={{ pin1: "net.SWG1", pin2: "net.NEX1" }} />
    <capacitor name="CEXA3" capacitance="100nF" footprint="0603" {...gp()} connections={{ pin1: "net.NEX1", pin2: "net.NEX2" }} />
    <resistor name="REXA2" resistance="10k" footprint="0603" {...gp()} connections={{ pin1: "net.NEX2", pin2: "net.NEX3" }} />
    <capacitor name="CEXA2" capacitance="1.5nF" footprint="0603" {...gp()} connections={{ pin1: "net.NEX3", pin2: "net.AGND" }} />
    <resistor name="REXA3" resistance="10k" footprint="0603" {...gp()} connections={{ pin1: "net.NEX3", pin2: "net.NEX4" }} />
    <capacitor name="CEXA1" capacitance="220pF" footprint="0603" {...gp()} connections={{ pin1: "net.REX_F", pin2: "net.NEX4" }} />
    <resistor name="REXA4" resistance="24k" footprint="0603" {...gp()} connections={{ pin1: "net.REX_F", pin2: "net.NEX3" }} />
    <chip name="UEXF" footprint={SmdFP(5)} {...gp()} pinLabels={{ pin1: "OUT", pin2: "VN", pin3: "INP", pin4: "INN", pin5: "VP" }}
      connections={{ OUT: "net.REX_F", VN: "net.AGND", INP: "net.VMID_REX", INN: "net.NEX4", VP: "net.V5A" }} />
    {/* resolver-amp supply: ALM2402 abs max is 18 V (rec 16 V) — VBATC can see 24 V jump
        start and ~33 V clamped transients, and TPSMC24CA clamps far above 18 V. ULDOEX
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
    <resistor name="REXB1" resistance="24k" footprint="0603" {...gp()} connections={{ pin1: "net.REX_F", pin2: "net.EXN1" }} />
    <resistor name="REXB2" resistance="24k" footprint="0603" {...gp()} connections={{ pin1: "net.VREX_P", pin2: "net.EXN1" }} />
    <resistor name="REXB3" resistance="24k" footprint="0603" {...gp()} connections={{ pin1: "net.VREX_P", pin2: "net.EXN2" }} />
    <resistor name="REXB4" resistance="24k" footprint="0603" {...gp()} connections={{ pin1: "net.VREX_N", pin2: "net.EXN2" }} />
    <capacitor name="CEXD" capacitance="4.7uF" footprint="1206" {...gp()} connections={{ pin1: "net.VEXD", pin2: "net.AGND" }} />
    {/* excitation monitor dividers (GEN3: 4.99k/12.1k, 4.99k/24k) */}
    <resistor name="REXM1" resistance="5.1k" footprint="0603" {...gp()} connections={{ pin1: "net.VREX_P", pin2: "net.VREXM_P" }} />
    <resistor name="REXM2" resistance="12k" footprint="0603" {...gp()} connections={{ pin1: "net.VREXM_P", pin2: "net.AGND" }} />
    <resistor name="REXM3" resistance="5.1k" footprint="0603" {...gp()} connections={{ pin1: "net.VREX_N", pin2: "net.VREXM_N" }} />
    <resistor name="REXM4" resistance="24k" footprint="0603" {...gp()} connections={{ pin1: "net.VREXM_N", pin2: "net.AGND" }} />
    {/* sin/cos conditioning: 10 k bias pair to VMID + 10 k series + clamps + RC to SDADC.
        Round 12 (R2-F13): the resolver wires share the vehicle connector with KL30. With the GEN3
        680 R / 330 R a wire shorted to 16 V pushed (16 - 5.7)/450 = 23 mA into an SDADC pin against
        the S32K39's 3 mA limit (operating AND absolute maximum, no transient allowance) and 20 mA
        into the VMID buffer. 10 k series: 1.0 / 1.8 / 2.9 mA at 16 / 24 / 35 V; 10 k bias: both legs
        pull up through the winding, 2 x 1.35 mA into the OPA348 (it sinks ~7 mA at 125 C). The
        caps are rescaled with the 30x higher source: 47 pF + 100 pF differential, 22 pF common-mode
        on BOTH legs (the P-only caps converted common mode to differential): corner ~47 kHz,
        -12 deg at 10 kHz on both channels; source 20 k vs the SDADC's Z_DIFF >= 215 k is a
        ratio-cancelled 9 % gain term, calibrated at EOL. 100 pF at the pin is ~800x the SDADC
        sampling capacitor (Opus check, round 12). */}
    {[["SIN", "S1", "S3"], ["COS", "S2", "S4"]].map(([s, a, b]) => (
      <group key={s}>
        <resistor name={`R${s}1`} resistance="10k" footprint="0603" {...gp()} connections={{ pin1: `net.RSLV_${a}`, pin2: "net.VMID_RSV" }} />
        <resistor name={`R${s}2`} resistance="10k" footprint="0603" {...gp()} connections={{ pin1: `net.RSLV_${b}`, pin2: "net.VMID_RSV" }} />
        <resistor name={`R${s}F1`} resistance="10k" footprint="0603" {...gp()} connections={{ pin1: `net.RSLV_${a}`, pin2: `net.${s}F_P` }} />
        <resistor name={`R${s}F2`} resistance="10k" footprint="0603" {...gp()} connections={{ pin1: `net.RSLV_${b}`, pin2: `net.${s}F_N` }} />
        <chip name={`D${s}P`} footprint={SmdFP(3)} {...gp()} pinLabels={{ pin1: "A", pin2: "B", pin3: "G" }}
          connections={{ A: `net.${s}F_P`, B: `net.${s}F_N`, G: "net.AGND" }} />
        <capacitor name={`C${s}D`} capacitance="47pF" footprint="0603" {...gp()} connections={{ pin1: `net.${s}F_P`, pin2: `net.${s}F_N` }} />
        <capacitor name={`C${s}F1`} capacitance="22pF" footprint="0603" {...gp()} connections={{ pin1: `net.${s}F_P`, pin2: "net.VMID_RSV" }} />
        <capacitor name={`C${s}F2`} capacitance="22pF" footprint="0603" {...gp()} connections={{ pin1: `net.${s}F_N`, pin2: "net.VMID_RSV" }} />
        <resistor name={`R${s}R1`} resistance="120" footprint="0603" {...gp()} connections={{ pin1: `net.${s}F_P`, pin2: `net.${s}_P` }} />
        <resistor name={`R${s}R2`} resistance="120" footprint="0603" {...gp()} connections={{ pin1: `net.${s}F_N`, pin2: `net.${s}_N` }} />
        <capacitor name={`C${s}A1`} capacitance="22pF" footprint="0603" {...gp()} connections={{ pin1: `net.${s}_P`, pin2: "net.AGND" }} />
        <capacitor name={`C${s}A3`} capacitance="22pF" footprint="0603" {...gp()} connections={{ pin1: `net.${s}_N`, pin2: "net.AGND" }} />
        <capacitor name={`C${s}A2`} capacitance="100pF" footprint="0603" {...gp()} connections={{ pin1: `net.${s}_P`, pin2: `net.${s}_N` }} />
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
        <chip name={`TVSM${k}`} footprint={SmdFP(2)} {...gp()} pinLabels={{ pin1: "L", pin2: "G" }}
          connections={{ L: `net.${m}_F`, G: "net.AGND" }} />
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
      connections={{ KL30: "net.KL30", GND1: "net.DGND", KL15: "net.KL15R", CANH1: "net.VCANH1", CANL1: "net.VCANL1", CANH2: "net.VCANH2", CANL2: "net.VCANL2", FAULT: "net.FAULT_OUT", MT1P: "net.MT1_RAW", MT1R: "net.AGND", MT2P: "net.MT2_RAW", MT2R: "net.AGND", R1: "net.VREX_P", R2: "net.VREX_N", S1: "net.RSLV_S1", S3: "net.RSLV_S3", S2: "net.RSLV_S2", S4: "net.RSLV_S4", SHLDR: "net.DGND", SHLDS: "net.DGND", GND2: "net.DGND", SP1: "net.NC_VSP1", SP2: "net.NC_VSP2" }} />
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
