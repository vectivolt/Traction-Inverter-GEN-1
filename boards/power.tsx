// power.tsx — Traction Inverter POWER board (HV): DC entry, DC link, discharge (active +
// passive), 3x EconoDUAL 3 SiC half-bridge modules with per-switch isolated gate drive,
// dual gate-power flybacks, isolated DC-link sensing x2, module NTC routing, LV protection,
// ASC buffer, 40-way harness. Schematic-complete; layout is a later phase.
import {
  StudFP, FilmCanFP, FilmBoxFP, DiscFP, AxialFP, TO247_4L, EconoDual3FP, Header,
  SmdFP, Smd2FP, GateDrive, FlybackChain, IsoVSense, ModNtc, Harness, gp,
} from "../packages/cells";

const NO_ROUTE = process.env.TSCI_NO_ROUTE === "1";
const PH = ["U", "V", "W"] as const;

export default () => (
  <board routingDisabled={NO_ROUTE} width="420mm" height="380mm" schTraceAutoLabelEnabled schMaxTraceDistance={0}>
    <net name="DCP" isForPower />
    <net name="DCN" isForPower />
    <net name="PE" isGround />
    <net name="DGND" isGround />
    <net name="AGND" isGround />
    <net name="V12H" isForPower />
    <net name="V12L" isForPower />
    <net name="V15" isForPower />
    <net name="V5GD" isForPower />

    {/* ---- DC INPUT: studs + Y caps + HVIL loop pass-through ---- */}
    <chip name="JHVP" footprint={StudFP()} {...gp()} pinLabels={{ pin1: "P" }} connections={{ P: "net.DCP" }} />
    <chip name="JHVN" footprint={StudFP()} {...gp()} pinLabels={{ pin1: "P" }} connections={{ P: "net.DCN" }} />
    <chip name="JPE" footprint={StudFP(6.5, 12)} {...gp()} pinLabels={{ pin1: "P" }} connections={{ P: "net.PE" }} />
    <capacitor name="CY1" capacitance="4.7nF" footprint={DiscFP()} {...gp()} connections={{ pin1: "net.DCP", pin2: "net.PE" }} />
    <capacitor name="CY2" capacitance="4.7nF" footprint={DiscFP()} {...gp()} connections={{ pin1: "net.DCN", pin2: "net.PE" }} />
    <chip name="JHVIL" footprint={Header(2)} {...gp()} pinLabels={{ pin1: "A", pin2: "B" }}
      connections={{ A: "net.HVIL_LA", B: "net.HVIL_LB" }} />
    <resistor name="RHVL1" resistance="100" footprint="0603" {...gp()} connections={{ pin1: "net.HVIL_A", pin2: "net.HVIL_LA" }} />
    <resistor name="RHVL2" resistance="100" footprint="0603" {...gp()} connections={{ pin1: "net.HVIL_LB", pin2: "net.HVIL_B" }} />
    <diode name="DTVSH" footprint={Smd2FP()} {...gp()} connections={{ anode: "net.DGND", cathode: "net.HVIL_B" }} />

    {/* ---- DC LINK: 16x 20uF 1100V film (Faratronic C3D class — LCSC-stocked; DFM rev) ---- */}
    {Array.from({ length: 16 }, (_, i) => i + 1).map((k) => (
      <capacitor key={k} name={`CDC${k}`} capacitance="20uF" footprint={FilmCanFP()} {...gp()}
        connections={{ pin1: "net.DCP", pin2: "net.DCN" }} />
    ))}
    {/* passive bleeder — DFM rev: 2 strings x 5 series 27k standard 2512 2W (170 V/resistor
        at 850 V, inside plain-2512 200 V working — no specialty HV resistor needed).
        Net 67.5k: same 58 s to 60 V as the XM3 68k network. */}
    {[0, 1].map((st) => [1, 2, 3, 4, 5].map((k) => (
      <resistor key={`${st}-${k}`} name={`RBLD${st * 5 + k}`} resistance="27k" footprint="2512" {...gp()}
        connections={{
          pin1: k === 1 ? "net.DCP" : `net.BL${st}${k - 1}`,
          pin2: k === 5 ? "net.DCN" : `net.BL${st}${k}`,
        }} />
    )))}

    {/* ---- ACTIVE DISCHARGE: default-OFF opto + 1200V SiC FET + 4x 560R 10W ---- */}
    <chip name="PSQD" footprint={SmdFP(4)} {...gp()} pinLabels={{ pin1: "VIN", pin2: "GND", pin3: "P18", pin4: "COM" }}
      connections={{ VIN: "net.V15", GND: "net.DGND", P18: "net.V18Q", COM: "net.DCN" }} />
    <chip name="UQD" footprint={SmdFP(6)} {...gp()} pinLabels={{ pin1: "ANO", pin2: "NC2", pin3: "CAT", pin4: "GND", pin5: "VO", pin6: "VCC" }}
      connections={{ ANO: "net.QDA", CAT: "net.DGND", GND: "net.DCN", VO: "net.QDVO", VCC: "net.V18Q" }} />
    <resistor name="RQDL" resistance="470" footprint="0603" {...gp()} connections={{ pin1: "net.QDIS_CMD", pin2: "net.QDA" }} />
    <resistor name="RQDG" resistance="47" footprint="0603" {...gp()} connections={{ pin1: "net.QDVO", pin2: "net.G_QDIS" }} />
    <resistor name="RQDPD" resistance="10k" footprint="0603" {...gp()} connections={{ pin1: "net.G_QDIS", pin2: "net.DCN" }} />
    <capacitor name="CQD" capacitance="100nF" footprint="0603" {...gp()} connections={{ pin1: "net.V18Q", pin2: "net.DCN" }} />
    <chip name="QDIS" footprint={TO247_4L()} {...gp()} pinLabels={{ pin1: "D", pin2: "S", pin3: "KS", pin4: "G" }}
      connections={{ D: "net.QD_D", S: "net.DCN", KS: "net.DCN", G: "net.G_QDIS" }} />
    {[1, 2, 3, 4].map((k) => (
      <resistor key={k} name={`RDIS${k}`} resistance="560" footprint={AxialFP(38)} {...gp()}
        connections={{ pin1: k === 1 ? "net.DCP" : `net.DIS${k - 1}`, pin2: k === 4 ? "net.QD_D" : `net.DIS${k}` }} />
    ))}

    {/* ---- PHASES: module + snubber + NTC route + 2x gate-drive channel ---- */}
    {PH.map((x) => (
      <chip key={x} name={`MOD${x}`} footprint={EconoDual3FP()} {...gp()}
        pinLabels={{ pin1: "DCP", pin2: "AC", pin3: "DCN", pin4: "GH", pin5: "KSH", pin6: "GL", pin7: "KSL", pin8: "NT1", pin9: "NT2", pin10: "SP1", pin11: "SP2" }}
        connections={{
          DCP: "net.DCP", AC: `net.PH${x}`, DCN: "net.DCN",
          GH: `net.G_${x}H`, KSH: `net.KS_${x}H`, GL: `net.G_${x}L`, KSL: `net.KS_${x}L`,
          NT1: `net.NT_${x}A`, NT2: `net.NT_${x}B`, SP1: `net.NC_MOD${x}S1`, SP2: `net.NC_MOD${x}S2`,
        }} />
    ))}
    {PH.map((x) => (
      <capacitor key={x} name={`C${x}SN`} capacitance="1uF" footprint={FilmBoxFP()} {...gp()}
        connections={{ pin1: "net.DCP", pin2: "net.DCN" }} />
    ))}
    {PH.map((x) => (
      <chip key={x} name={`JM${x}`} footprint={StudFP()} {...gp()} pinLabels={{ pin1: "P" }} connections={{ P: `net.PH${x}` }} />
    ))}
    {PH.map((x) => (
      <ModNtc key={x} ph={x} ntcA={`net.NT_${x}A`} ntcB={`net.NT_${x}B`} />
    ))}
    {PH.map((x) => (
      <GateDrive key={`${x}H`} ph={x} side="H"
        drain="net.DCP" gate={`net.G_${x}H`} ks={`net.KS_${x}H`}
        pwmP={`net.PWM_${x}H`} pwmN={`net.PWM_${x}L`}
        en="net.DRV_EN" flt="net.FLT_HS_N" rdy="net.RDY_HS"
        wA={`net.W_${x}H_A`} wB={`net.W_${x}H_B`} />
    ))}
    {PH.map((x) => (
      <GateDrive key={`${x}L`} ph={x} side="L"
        drain={`net.PH${x}`} gate={`net.G_${x}L`} ks={`net.KS_${x}L`}
        pwmP={`net.PWM_${x}L`} pwmN={`net.PWM_${x}H`}
        en="net.DRV_EN" flt="net.FLT_LS_N" rdy="net.RDY_LS"
        asc="net.ASC_DRV" wA={`net.W_${x}L_A`} wB={`net.W_${x}L_B`} />
    ))}

    {/* ---- GATE POWER: dual flyback chains (GEN3 pattern) ---- */}
    <FlybackChain id="H" v12="net.V12H" />
    <FlybackChain id="L" v12="net.V12L" />

    {/* ---- ASC buffer: DCN-referenced, drives the 3 LS ASC pins ---- */}
    <chip name="PSASC" footprint={SmdFP(4)} {...gp()} pinLabels={{ pin1: "VIN", pin2: "GND", pin3: "P18", pin4: "COM" }}
      connections={{ VIN: "net.V15", GND: "net.DGND", P18: "net.V18A", COM: "net.DCN" }} />
    <chip name="UASC" footprint={SmdFP(6)} {...gp()} pinLabels={{ pin1: "ANO", pin2: "NC2", pin3: "CAT", pin4: "GND", pin5: "VO", pin6: "VCC" }}
      connections={{ ANO: "net.ASCA", CAT: "net.DGND", GND: "net.DCN", VO: "net.ASCVO", VCC: "net.V18A" }} />
    <resistor name="RASCL" resistance="470" footprint="0603" {...gp()} connections={{ pin1: "net.ASC_CMD", pin2: "net.ASCA" }} />
    <resistor name="RASCG" resistance="100" footprint="0603" {...gp()} connections={{ pin1: "net.ASCVO", pin2: "net.ASC_DRV" }} />
    <resistor name="RASCPD" resistance="10k" footprint="0603" {...gp()} connections={{ pin1: "net.ASC_DRV", pin2: "net.DCN" }} />
    <capacitor name="CASC" capacitance="100nF" footprint="0603" {...gp()} connections={{ pin1: "net.V18A", pin2: "net.DCN" }} />

    {/* ---- ISOLATED DC-LINK SENSING: two independent channels + shared reinforced bias ---- */}
    <IsoVSense id="1" outP="net.VDC1_P" outN="net.VDC1_N" />
    <IsoVSense id="2" outP="net.VDC2_P" outN="net.VDC2_N" />
    <chip name="PS5B" footprint={SmdFP(4)} {...gp()} pinLabels={{ pin1: "VIN", pin2: "GND", pin3: "P5", pin4: "COM" }}
      connections={{ VIN: "net.V15", GND: "net.DGND", P5: "net.V5ISO", COM: "net.DCN" }} />
    <capacitor name="C5B1" capacitance="1uF" footprint="0603" {...gp()} connections={{ pin1: "net.V5ISO", pin2: "net.DCN" }} />
    <capacitor name="C5B2" capacitance="100nF" footprint="0603" {...gp()} connections={{ pin1: "net.V5ISO", pin2: "net.DCN" }} />

    {/* ---- LV POWER: two protected 12V feeds + V5GD LDO + V15 boost ---- */}
    <chip name="FH1" footprint={SmdFP(2)} {...gp()} pinLabels={{ pin1: "A", pin2: "B" }}
      connections={{ A: "net.VBAT_H", B: "net.FHO" }} />
    <diode name="DRH" footprint={Smd2FP()} {...gp()} connections={{ anode: "net.FHO", cathode: "net.NRH" }} />
    <diode name="DTVH" footprint={Smd2FP()} {...gp()} connections={{ anode: "net.DGND", cathode: "net.NRH" }} />
    <inductor name="LFH1" inductance="1uH" footprint="1206" {...gp()} connections={{ pin1: "net.NRH", pin2: "net.V12H" }} />
    <capacitor name="CLVH1" capacitance="4.7uF" footprint="1206" {...gp()} connections={{ pin1: "net.NRH", pin2: "net.DGND" }} />
    <capacitor name="CLVH2" capacitance="4.7uF" footprint="1206" {...gp()} connections={{ pin1: "net.V12H", pin2: "net.DGND" }} />
    <chip name="FL1" footprint={SmdFP(2)} {...gp()} pinLabels={{ pin1: "A", pin2: "B" }}
      connections={{ A: "net.VBAT_L", B: "net.FLO" }} />
    <diode name="DRL" footprint={Smd2FP()} {...gp()} connections={{ anode: "net.FLO", cathode: "net.NRL" }} />
    <diode name="DTVL" footprint={Smd2FP()} {...gp()} connections={{ anode: "net.DGND", cathode: "net.NRL" }} />
    <inductor name="LFL1" inductance="1uH" footprint="1206" {...gp()} connections={{ pin1: "net.NRL", pin2: "net.V12L" }} />
    <capacitor name="CLVL1" capacitance="4.7uF" footprint="1206" {...gp()} connections={{ pin1: "net.NRL", pin2: "net.DGND" }} />
    <capacitor name="CLVL2" capacitance="4.7uF" footprint="1206" {...gp()} connections={{ pin1: "net.V12L", pin2: "net.DGND" }} />
    {/* driver logic 5V (VCC1) — INH from EN_FLYBK_LS, GEN3 exact */}
    <chip name="UGDL" footprint={SmdFP(4)} {...gp()} pinLabels={{ pin1: "IN", pin2: "INH", pin3: "GND", pin4: "OUT" }}
      connections={{ IN: "net.V12L", INH: "net.EN_FLYBK_LS", GND: "net.DGND", OUT: "net.V5GD" }} />
    <capacitor name="C5G1" capacitance="1uF" footprint="0805" {...gp()} connections={{ pin1: "net.V12L", pin2: "net.DGND" }} />
    <capacitor name="C5G2" capacitance="10uF" footprint="1206" {...gp()} connections={{ pin1: "net.V5GD", pin2: "net.DGND" }} />
    {/* V15 boost for the reinforced sense-bias modules */}
    <chip name="UB15" footprint={SmdFP(6)} {...gp()} pinLabels={{ pin1: "VIN", pin2: "EN", pin3: "SW", pin4: "FB", pin5: "COMP", pin6: "GND" }}
      connections={{ VIN: "net.V12L", EN: "net.V12L", SW: "net.B15SW", FB: "net.B15FB", COMP: "net.B15CO", GND: "net.DGND" }} />
    <inductor name="LB15" inductance="10uH" footprint="1210" {...gp()} connections={{ pin1: "net.V12L", pin2: "net.B15SW" }} />
    <diode name="DB15" footprint={Smd2FP()} {...gp()} connections={{ anode: "net.B15SW", cathode: "net.V15" }} />
    <capacitor name="CB15I" capacitance="10uF" footprint="1206" {...gp()} connections={{ pin1: "net.V12L", pin2: "net.DGND" }} />
    <capacitor name="CB15O1" capacitance="22uF" footprint="1210" {...gp()} connections={{ pin1: "net.V15", pin2: "net.DGND" }} />
    <capacitor name="CB15O2" capacitance="22uF" footprint="1210" {...gp()} connections={{ pin1: "net.V15", pin2: "net.DGND" }} />
    <resistor name="RB15F1" resistance="110k" footprint="0603" {...gp()} connections={{ pin1: "net.V15", pin2: "net.B15FB" }} />
    <resistor name="RB15F2" resistance="9.53k" footprint="0603" {...gp()} connections={{ pin1: "net.B15FB", pin2: "net.DGND" }} />
    <capacitor name="CB15C" capacitance="10nF" footprint="0603" {...gp()} connections={{ pin1: "net.B15CO", pin2: "net.DGND" }} />

    {/* ---- CONTROL INTERFACE: harness + default-OFF pulldowns ---- */}
    <Harness name="JIC" />
    {["PWM_UH", "PWM_UL", "PWM_VH", "PWM_VL", "PWM_WH", "PWM_WL", "EN_FLYBK_HS", "EN_FLYBK_LS", "ASC_CMD", "QDIS_CMD", "DRV_EN"].map((n, i) => (
      <resistor key={n} name={`RPD${i}`} resistance="10k" footprint="0603" {...gp()}
        connections={{ pin1: `net.${n}`, pin2: "net.DGND" }} />
    ))}
    {/* AGND-DGND single-point tie lives on the card; PE bond here */}
    <resistor name="RPET" resistance="1M" footprint="1206" {...gp()} connections={{ pin1: "net.DGND", pin2: "net.PE" }} />
    <capacitor name="CPET" capacitance="4.7nF" footprint={FilmBoxFP(10)} {...gp()} connections={{ pin1: "net.DGND", pin2: "net.PE" }} />
  </board>
);
