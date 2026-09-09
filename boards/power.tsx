// power.tsx — Traction Inverter POWER board (HV): DC entry, DC link, discharge (active +
// passive), 3x EconoDUAL 3 SiC half-bridge modules with per-switch isolated gate drive,
// dual gate-power flybacks, isolated DC-link sensing x2, module NTC routing, LV protection,
// ASC buffer, 40-way harness. Schematic-complete; layout is a later phase.
import {
  StudFP, FilmCanFP, FilmBoxFP, DiscFP, AxialFP, TO247_4L, EconoDual3FP, Header, Sip7FP,
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

    {/* The 16-can DC-link lives on the SEPARATE CAP BANK busbar assembly (sheet 2) — FR4
        cannot carry the bus current; DCP/DCN reach this board only as sense/bias taps.
        Bleeder + active discharge live on the SEPARATE bolt-on DISCHARGE BOARD (sheet 3 —
        the XM3 pattern: the network physically stays with the cap bank/busbar). This header
        carries its bias + command; QDIS_CMD keeps its default-OFF pulldown on this board. */}
    <chip name="JDIS" footprint={Header(4)} {...gp()}
      pinLabels={{ pin1: "V15", pin2: "CMD", pin3: "GND1", pin4: "GND2" }}
      connections={{ V15: "net.V15", CMD: "net.QDIS_CMD", GND1: "net.DGND", GND2: "net.DGND" }} />

    {/* ---- PHASES: module + snubber + NTC route + 2x gate-drive channel ---- */}
    {PH.map((x) => (
      <chip key={x} name={`MOD${x}`} footprint={EconoDual3FP()} {...gp()}
        pinLabels={{ pin1: "GL", pin2: "KSL", pin3: "DCN", pin4: "DCP", pin5: "NT1", pin6: "NT2", pin7: "GH", pin8: "KSH", pin9: "DSH", pin10: "AC1", pin11: "AC2" }}
        connections={{
          GL: `net.G_${x}L`, KSL: `net.KS_${x}L`, DCN: "net.DCN", DCP: "net.DCP",
          NT1: `net.NT_${x}A`, NT2: "net.TMOD_RTN", GH: `net.G_${x}H`, KSH: `net.KS_${x}H`,
          DSH: `net.DSH_${x}`, AC1: `net.PH${x}`, AC2: `net.PH${x}`,
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
      <ModNtc key={x} ph={x} ntcA={`net.NT_${x}A`} />
    ))}
    {PH.map((x) => (
      <GateDrive key={`${x}H`} ph={x} side="H"
        drain={`net.DSH_${x}`} gate={`net.G_${x}H`} ks={`net.KS_${x}H`}
        pwmP={`net.PWM_${x}H`} pwmN={`net.PWM_${x}L`}
        en="net.DRV_EN" flt="net.FLT_HS_N" rdy="net.RDY_HS"
        wA={`net.W_${x}H_A`} />
    ))}
    {PH.map((x) => (
      <GateDrive key={`${x}L`} ph={x} side="L"
        drain={`net.PH${x}`} gate={`net.G_${x}L`} ks={`net.KS_${x}L`}
        pwmP={`net.PWM_${x}L`} pwmN={`net.PWM_${x}H`}
        en="net.DRV_EN" flt="net.FLT_LS_N" rdy="net.RDY_LS"
        asc="net.ASC_DRV" wA={`net.W_${x}L_A`} />
    ))}

    {/* ---- GATE POWER: dual flyback chains (GEN3 pattern) ---- */}
    <FlybackChain id="H" v12="net.V12H" />
    <FlybackChain id="L" v12="net.V12L" />

    {/* ---- ASC buffer: DCN-referenced, drives the 3 LS ASC pins ---- */}
    {/* QA01C-18 real SIP-7: 1=Vin 2=GND(in) 5=-Vo 6=0V 7=+Vo — +18 V used, -Vo unloaded */}
    <chip name="PSASC" footprint={Sip7FP()} {...gp()} pinLabels={{ pin1: "VIN", pin2: "GND", pin5: "VON", pin6: "COM", pin7: "VOP" }}
      connections={{ VIN: "net.V15", GND: "net.DGND", VON: "net.NC_PSASCN", COM: "net.DCN", VOP: "net.V18A" }} />
    <chip name="UASC" footprint={SmdFP(6)} {...gp()} pinLabels={{ pin1: "ANO", pin2: "NC2", pin3: "CAT", pin4: "GND", pin5: "VO", pin6: "VCC" }}
      connections={{ ANO: "net.ASCA", CAT: "net.DGND", GND: "net.DCN", VO: "net.ASCVO", VCC: "net.V18A" }} />
    <resistor name="RASCL" resistance="470" footprint="0603" {...gp()} connections={{ pin1: "net.ASC_CMD", pin2: "net.ASCA" }} />
    {/* NSI6611 ASC abs max = GND2+6 V (DS 1.2 §2) — series 2.2k + 5.1 V zener clamp the
        18 V opto swing to a legal ASC level (F28) */}
    <resistor name="RASCG" resistance="2.2k" footprint="0603" {...gp()} connections={{ pin1: "net.ASCVO", pin2: "net.ASC_DRV" }} />
    <diode name="ZASC" footprint={Smd2FP()} {...gp()} connections={{ anode: "net.DCN", cathode: "net.ASC_DRV" }} />
    <resistor name="RASCPD" resistance="10k" footprint="0603" {...gp()} connections={{ pin1: "net.ASC_DRV", pin2: "net.DCN" }} />
    <capacitor name="CASC" capacitance="100nF" footprint="0603" {...gp()} connections={{ pin1: "net.V18A", pin2: "net.DCN" }} />

    {/* ---- ISOLATED DC-LINK SENSING: two independent channels + shared reinforced bias ---- */}
    <IsoVSense id="1" outP="net.VDC1_P" outN="net.VDC1_N" />
    <IsoVSense id="2" outP="net.VDC2_P" outN="net.VDC2_N" />
    <chip name="PS5B" footprint={Sip7FP()} {...gp()} pinLabels={{ pin1: "VIN", pin2: "GND", pin5: "VON", pin6: "COM", pin7: "VOP" }}
      connections={{ VIN: "net.V15", GND: "net.DGND", VON: "net.NC_PS5BN", COM: "net.DCN", VOP: "net.V5ISO" }} />
    <capacitor name="C5B1" capacitance="1uF" footprint="0603" {...gp()} connections={{ pin1: "net.V5ISO", pin2: "net.DCN" }} />
    <capacitor name="C5B2" capacitance="100nF" footprint="0603" {...gp()} connections={{ pin1: "net.V5ISO", pin2: "net.DCN" }} />
    {/* channel-2 bias is its OWN module — the independence claim of the dual VDC sense must
        not share a common bias supply (ERC finding F4) */}
    <chip name="PS5C" footprint={Sip7FP()} {...gp()} pinLabels={{ pin1: "VIN", pin2: "GND", pin5: "VON", pin6: "COM", pin7: "VOP" }}
      connections={{ VIN: "net.V15", GND: "net.DGND", VON: "net.NC_PS5CN", COM: "net.DCN", VOP: "net.V5ISO2" }} />
    <capacitor name="C5C1" capacitance="1uF" footprint="0603" {...gp()} connections={{ pin1: "net.V5ISO2", pin2: "net.DCN" }} />

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
    {/* driver logic 5V (VCC1): NCV4276C real map 1=IN 2=INH 3=GND 4=NC/VA 5=OUT (fixed-5V
        version: pin 4 NC). INH is tied on through 100k so driver diagnostics and both
        AMC1311 LV sides stay alive whenever KL30 is present — gate POWER stays separately
        default-OFF via the flyback enables (rev A.4: was slaved to EN_FLYBK_LS). */}
    <chip name="UGDL" footprint={SmdFP(5)} {...gp()} pinLabels={{ pin1: "IN", pin2: "INH", pin3: "GND", pin4: "NC", pin5: "OUT" }}
      connections={{ IN: "net.V12L", INH: "net.GDL_ON", GND: "net.DGND", NC: "net.NC_UGDL4", OUT: "net.V5GD" }} />
    <resistor name="RGDLE" resistance="100k" footprint="0603" {...gp()} connections={{ pin1: "net.V12L", pin2: "net.GDL_ON" }} />
    <capacitor name="C5G1" capacitance="1uF" footprint="0805" {...gp()} connections={{ pin1: "net.V12L", pin2: "net.DGND" }} />
    <capacitor name="C5G2" capacitance="10uF" footprint="1206" {...gp()} connections={{ pin1: "net.V5GD", pin2: "net.DGND" }} />
    {/* V15 boost for the reinforced sense-bias modules */}
    <chip name="UB15" footprint={SmdFP(16)} {...gp()}
      pinLabels={{ pin1: "SW1", pin2: "VIN1", pin3: "EN", pin4: "SS", pin5: "SYNC", pin6: "AGND", pin7: "COMP", pin8: "FB", pin9: "FREQ", pin10: "NC1", pin11: "PGND1", pin12: "PGND2", pin13: "PGND3", pin14: "NC2", pin15: "SW2", pin16: "SW3" }}
      connections={{ SW1: "net.B15SW", VIN1: "net.V12L", EN: "net.V12L", SS: "net.B15SS", SYNC: "net.DGND", AGND: "net.DGND", COMP: "net.B15CO", FB: "net.B15FB", FREQ: "net.B15FQ", NC1: "net.DGND", PGND1: "net.DGND", PGND2: "net.DGND", PGND3: "net.DGND", NC2: "net.DGND", SW2: "net.B15SW", SW3: "net.B15SW" }} />
    {/* soft-start + switching-frequency programming (RTE16 required pins): 80.6k -> ~580 kHz */}
    <capacitor name="CB15S" capacitance="47nF" footprint="0603" {...gp()} connections={{ pin1: "net.B15SS", pin2: "net.DGND" }} />
    <resistor name="RB15Q" resistance="80.6k" footprint="0603" {...gp()} connections={{ pin1: "net.B15FQ", pin2: "net.DGND" }} />
    <inductor name="LB15" inductance="10uH" footprint="1210" {...gp()} connections={{ pin1: "net.V12L", pin2: "net.B15SW" }} />
    <diode name="DB15" footprint={Smd2FP()} {...gp()} connections={{ anode: "net.B15SW", cathode: "net.V15B" }} />
    <capacitor name="CB15I" capacitance="10uF" footprint="1206" {...gp()} connections={{ pin1: "net.V12L", pin2: "net.DGND" }} />
    <capacitor name="CB15O1" capacitance="22uF" footprint="1210" {...gp()} connections={{ pin1: "net.V15B", pin2: "net.DGND" }} />
    <capacitor name="CB15O2" capacitance="22uF" footprint="1210" {...gp()} connections={{ pin1: "net.V15B", pin2: "net.DGND" }} />
    <resistor name="RB15F1" resistance="110k" footprint="0603" {...gp()} connections={{ pin1: "net.V15B", pin2: "net.B15FB" }} />
    <resistor name="RB15F2" resistance="9.53k" footprint="0603" {...gp()} connections={{ pin1: "net.B15FB", pin2: "net.DGND" }} />
    <capacitor name="CB15C" capacitance="10nF" footprint="0603" {...gp()} connections={{ pin1: "net.B15CO", pin2: "net.DGND" }} />
    {/* V15 protective post-regulator (rev A.4.1): a boost cannot regulate below its input —
        when V12L rides above ~15.5 V (24 V jump start, clamped load dump) the LB15/DB15
        path feeds V15 directly. NCV4276C-ADJ (40 V in, 400 mA) sits in mild dropout in
        normal operation (V15 ≈ 15.0-15.2 V) and CLAMPS at 15.0 V during pass-through, so
        the QA01C modules (13.5-16.5 V window) never see the raw rail. Vout = 2.5·(1+49.9/10). */}
    <chip name="ULDO15" footprint={SmdFP(5)} {...gp()} pinLabels={{ pin1: "IN", pin2: "INH", pin3: "GND", pin4: "VA", pin5: "OUT" }}
      connections={{ IN: "net.V15B", INH: "net.V15B", GND: "net.DGND", VA: "net.V15VA", OUT: "net.V15" }} />
    <resistor name="RLD1" resistance="49.9k" footprint="0603" {...gp()} connections={{ pin1: "net.V15", pin2: "net.V15VA" }} />
    <resistor name="RLD2" resistance="10k" footprint="0603" {...gp()} connections={{ pin1: "net.V15VA", pin2: "net.DGND" }} />
    <capacitor name="CLD15" capacitance="4.7uF" footprint="1206" {...gp()} connections={{ pin1: "net.V15", pin2: "net.DGND" }} />

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
