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
  ["PTD6_ASCREQ", "ASC_REQ"], ["PTD8_ASCCLR", "ASC_CLR_M"], ["PTD5_QDIS", "QDIS_CMD"],
  ["PTD9_FLTCLR", "FLT_CLR_N"],
  // analog
  ["PTA0_VDC1", "VDC1_SE"], ["PTB0_VDC2", "VDC2_SE"],
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
        one polyfuse per bank, out over harness pins 31/32 (H) and 35/36 (L) */}
    <chip name="FVBH" footprint={SmdFP(2)} {...gp()} pinLabels={{ pin1: "A", pin2: "B" }}
      connections={{ A: "net.NRC", B: "net.VBAT_H" }} />
    <chip name="FVBL" footprint={SmdFP(2)} {...gp()} pinLabels={{ pin1: "A", pin2: "B" }}
      connections={{ A: "net.NRC", B: "net.VBAT_L" }} />

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
      ["CSB5", "VREF5", "2.2uF"], ["CSB6", "V3B", "4.7uF"], ["CSB7", "V5A", "10uF"], ["CSB8", "V5A", "10uF"]].map(([n, r, v]) => (
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
    {/* FS0B/FS1B are open low-side outputs with a 4-22 mA clamp (FS26 DS Table 196):
        1 k pull-ups sink 4.6 mA — the 120 R GEN3-style pulls would force 42 mA (F35) */}
    <resistor name="RENP1" resistance="1k" footprint="0603" {...gp()} connections={{ pin1: "net.V5A", pin2: "net.FS0B_N" }} />
    <resistor name="RENP2" resistance="1k" footprint="0603" {...gp()} connections={{ pin1: "net.V5A", pin2: "net.FS1B_N" }} />
    <chip name="UAND1" footprint={SmdFP(6)} {...gp()} pinLabels={{ pin1: "A", pin2: "GND", pin3: "B", pin4: "Y", pin5: "VCC", pin6: "C" }}
      connections={{ A: "net.FS0B_N", GND: "net.DGND", B: "net.MCU_GATE_EN", Y: "net.ENX1", VCC: "net.V5A", C: "net.RDY_HS" }} />
    <chip name="UAND2" footprint={SmdFP(6)} {...gp()} pinLabels={{ pin1: "A", pin2: "GND", pin3: "B", pin4: "Y", pin5: "VCC", pin6: "C" }}
      connections={{ A: "net.ENX1", GND: "net.DGND", B: "net.RDY_LS", Y: "net.DRV_EN", VCC: "net.V5A", C: "net.FLT_OK" }} />
    <capacitor name="CAND1" capacitance="100nF" footprint="0603" {...gp()} connections={{ pin1: "net.V5A", pin2: "net.DGND" }} />
    <capacitor name="CAND2" capacitance="100nF" footprint="0603" {...gp()} connections={{ pin1: "net.V5A", pin2: "net.DGND" }} />
    {/* hardware fault latch (rev A.4): either driver-bank FLT pulls FLT_CMB_N low ->
        async-PRESET -> /Q(FLT_OK) low -> DRV_EN low on ALL six channels within the gate
        delay; the drivers' own DESAT soft-shutdown handles the faulted switch in ~us.
        Latched: EN dropping resets the driver's FLT, but the latch holds until the MCU
        pulses FLT_CLR_N after diagnosis. */}
    <diode name="DFLT1" footprint={Smd2FP()} {...gp()} connections={{ anode: "net.FLT_CMB_N", cathode: "net.FLT_HS_N" }} />
    <diode name="DFLT2" footprint={Smd2FP()} {...gp()} connections={{ anode: "net.FLT_CMB_N", cathode: "net.FLT_LS_N" }} />
    <resistor name="RFLTC" resistance="10k" footprint="0603" {...gp()} connections={{ pin1: "net.V5A", pin2: "net.FLT_CMB_N" }} />
    <chip name="ULAT2" footprint={SmdFP(8)} {...gp()}
      pinLabels={{ pin1: "CLK", pin2: "D", pin3: "QN", pin4: "GND", pin5: "Q", pin6: "CLR_N", pin7: "PRE_N", pin8: "VCC" }}
      connections={{ CLK: "net.DGND", D: "net.DGND", QN: "net.FLT_OK", GND: "net.DGND", Q: "net.NC_FLTQ", CLR_N: "net.FLT_CLR_N", PRE_N: "net.FLT_CMB_N", VCC: "net.V5A" }} />
    <resistor name="RLAT2" resistance="10k" footprint="0603" {...gp()} connections={{ pin1: "net.V5A", pin2: "net.FLT_CLR_N" }} />
    <capacitor name="CLAT2" capacitance="100nF" footprint="0603" {...gp()} connections={{ pin1: "net.V5A", pin2: "net.DGND" }} />
    <resistor name="RGPD" resistance="10k" footprint="0603" {...gp()} connections={{ pin1: "net.DRV_EN", pin2: "net.DGND" }} />
    <resistor name="RFLTP1" resistance="5.1k" footprint="0603" {...gp()} connections={{ pin1: "net.V5A", pin2: "net.FLT_HS_N" }} />
    <resistor name="RFLTP2" resistance="5.1k" footprint="0603" {...gp()} connections={{ pin1: "net.V5A", pin2: "net.FLT_LS_N" }} />
    <resistor name="RRDYP1" resistance="5.1k" footprint="0603" {...gp()} connections={{ pin1: "net.V5A", pin2: "net.RDY_HS" }} />
    <resistor name="RRDYP2" resistance="5.1k" footprint="0603" {...gp()} connections={{ pin1: "net.V5A", pin2: "net.RDY_LS" }} />
    <capacitor name="CFLTF" capacitance="100pF" footprint="0603" {...gp()} connections={{ pin1: "net.FLT_HS_N", pin2: "net.DGND" }} />
    {/* ASC latch: MCU request clocks it, FS1B (via strap) async-sets it, MCU clear releases */}
    <chip name="ULAT" footprint={SmdFP(8)} {...gp()}
      pinLabels={{ pin1: "CLK", pin2: "D", pin3: "QN", pin4: "GND", pin5: "Q", pin6: "CLR_N", pin7: "PRE_N", pin8: "VCC" }}
      connections={{ CLK: "net.ASC_REQ", D: "net.V5A", QN: "net.NC_LATQN", GND: "net.DGND", Q: "net.ASC_CMD", CLR_N: "net.ASC_CLR_N", PRE_N: "net.ASC_SET_N", VCC: "net.V5A" }} />
    {/* FS1B (open low-side, 1k pull-up) sets through 1k series into a 10k pulled-up node:
        asserted level 0.45 V << VIL; MCU clear through 1k series against the 10k pull-up */}
    <resistor name="RFS1" resistance="1k" footprint="0603" {...gp()} connections={{ pin1: "net.FS1B_N", pin2: "net.ASC_SET_N" }} />
    <resistor name="RFS2" resistance="10k" footprint="0603" {...gp()} connections={{ pin1: "net.V5A", pin2: "net.ASC_SET_N" }} />
    <resistor name="RFS3" resistance="1k" footprint="0603" {...gp()} connections={{ pin1: "net.ASC_CLR_M", pin2: "net.ASC_CLR_N" }} />
    <resistor name="RFS4" resistance="1k" footprint="0603" {...gp()} connections={{ pin1: "net.FS1B_N", pin2: "net.FAULT_OUT" }} />
    <resistor name="RASCP" resistance="10k" footprint="0603" {...gp()} connections={{ pin1: "net.ASC_CMD", pin2: "net.DGND" }} />
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
    {/* exciter: SWG -> 3rd-order MFB LPF (OPA348) -> ALM2402 H-bridge -> R1/R2 */}
    <resistor name="REXA1" resistance="3k" footprint="0603" {...gp()} connections={{ pin1: "net.SWG1", pin2: "net.NEX1" }} />
    <capacitor name="CEXA3" capacitance="100nF" footprint="0603" {...gp()} connections={{ pin1: "net.NEX1", pin2: "net.NEX2" }} />
    <resistor name="REXA2" resistance="10k" footprint="0603" {...gp()} connections={{ pin1: "net.NEX2", pin2: "net.NEX3" }} />
    <capacitor name="CEXA2" capacitance="300pF" footprint="0603" {...gp()} connections={{ pin1: "net.NEX3", pin2: "net.AGND" }} />
    <resistor name="REXA3" resistance="10k" footprint="0603" {...gp()} connections={{ pin1: "net.NEX3", pin2: "net.NEX4" }} />
    <capacitor name="CEXA1" capacitance="4.7nF" footprint="0603" {...gp()} connections={{ pin1: "net.REX_F", pin2: "net.NEX3" }} />
    <resistor name="REXA4" resistance="24k" footprint="0603" {...gp()} connections={{ pin1: "net.REX_F", pin2: "net.NEX4" }} />
    <chip name="UEXF" footprint={SmdFP(5)} {...gp()} pinLabels={{ pin1: "OUT", pin2: "VN", pin3: "INP", pin4: "INN", pin5: "VP" }}
      connections={{ OUT: "net.REX_F", VN: "net.AGND", INP: "net.VMID_REX", INN: "net.NEX4", VP: "net.V5A" }} />
    {/* resolver-amp supply: ALM2402 abs max is 18 V (rec 16 V) — VBATC can see 24 V jump
        start and ~33 V clamped transients, and TPSMC24CA clamps far above 18 V. ULDOEX
        (same 40 V ADJ LDO family as ULDO15) makes a protected 12.1 V VEXD rail; in dropout
        at crank it degrades exactly like the old direct feed (rev A.4.2). */}
    <chip name="ULDOEX" footprint={SmdFP(5)} {...gp()} pinLabels={{ pin1: "IN", pin2: "INH", pin3: "GND", pin4: "VA", pin5: "OUT" }}
      connections={{ IN: "net.VBATC", INH: "net.VBATC", GND: "net.AGND", VA: "net.VEXVA", OUT: "net.VEXD" }} />
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
    {/* sin/cos conditioning: 680R bias pair to VMID + 330R series + clamps + RC to SDADC */}
    {[["SIN", "S1", "S3"], ["COS", "S2", "S4"]].map(([s, a, b]) => (
      <group key={s}>
        <resistor name={`R${s}1`} resistance="680" footprint="0603" {...gp()} connections={{ pin1: `net.RSLV_${a}`, pin2: "net.VMID_RSV" }} />
        <resistor name={`R${s}2`} resistance="680" footprint="0603" {...gp()} connections={{ pin1: `net.RSLV_${b}`, pin2: "net.VMID_RSV" }} />
        <resistor name={`R${s}F1`} resistance="330" footprint="0603" {...gp()} connections={{ pin1: `net.RSLV_${a}`, pin2: `net.${s}F_P` }} />
        <resistor name={`R${s}F2`} resistance="330" footprint="0603" {...gp()} connections={{ pin1: `net.RSLV_${b}`, pin2: `net.${s}F_N` }} />
        <chip name={`D${s}P`} footprint={SmdFP(3)} {...gp()} pinLabels={{ pin1: "A", pin2: "B", pin3: "G" }}
          connections={{ A: `net.${s}F_P`, B: `net.${s}F_N`, G: "net.AGND" }} />
        <capacitor name={`C${s}D`} capacitance="220pF" footprint="0603" {...gp()} connections={{ pin1: `net.${s}F_P`, pin2: `net.${s}F_N` }} />
        <capacitor name={`C${s}F`} capacitance="22pF" footprint="0603" {...gp()} connections={{ pin1: `net.${s}F_P`, pin2: "net.VMID_RSV" }} />
        <resistor name={`R${s}R1`} resistance="120" footprint="0603" {...gp()} connections={{ pin1: `net.${s}F_P`, pin2: `net.${s}_P` }} />
        <resistor name={`R${s}R2`} resistance="120" footprint="0603" {...gp()} connections={{ pin1: `net.${s}F_N`, pin2: `net.${s}_N` }} />
        <capacitor name={`C${s}A1`} capacitance="100pF" footprint="0603" {...gp()} connections={{ pin1: `net.${s}_P`, pin2: "net.AGND" }} />
        <capacitor name={`C${s}A2`} capacitance="2.2nF" footprint="0603" {...gp()} connections={{ pin1: `net.${s}_P`, pin2: `net.${s}_N` }} />
      </group>
    ))}

    {/* ---- PHASE CURRENT: hall harness + 3x buffer chains (GEN3) ---- */}
    <chip name="JLEM" footprint={Header(10, 2)} {...gp()}
      pinLabels={{ pin1: "S5U", pin2: "OU", pin3: "G1", pin4: "S5V", pin5: "OV", pin6: "G2", pin7: "S5W", pin8: "OW", pin9: "SH", pin10: "G3" }}
      connections={{ S5U: "net.V5S_U", OU: "net.HALL_U", G1: "net.AGND", S5V: "net.V5S_V", OV: "net.HALL_V", G2: "net.AGND", S5W: "net.V5S_W", OW: "net.HALL_W", SH: "net.AGND", G3: "net.AGND" }} />
    {PH.map((x) => (
      <chip key={x} name={`USNS${x}`} footprint={SmdFP(3)} {...gp()} pinLabels={{ pin1: "VCC", pin2: "OUT", pin3: "GND" }}
        connections={{ VCC: `net.V5S_${x}`, OUT: `net.HALL_${x}`, GND: "net.AGND" }} />
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
      connections={{ KL30: "net.KL30", GND1: "net.DGND", KL15: "net.KL15R", CANH1: "net.VCANH1", CANL1: "net.VCANL1", CANH2: "net.VCANH2", CANL2: "net.VCANL2", FAULT: "net.FAULT_OUT", MT1P: "net.MT1_RAW", MT1R: "net.AGND", MT2P: "net.MT2_RAW", MT2R: "net.AGND", R1: "net.VREX_P", R2: "net.VREX_N", S1: "net.RSLV_S1", S3: "net.RSLV_S3", S2: "net.RSLV_S2", S4: "net.RSLV_S4", SHLDR: "net.AGND", SHLDS: "net.AGND", GND2: "net.DGND", SP1: "net.NC_VSP1", SP2: "net.NC_VSP2" }} />
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
