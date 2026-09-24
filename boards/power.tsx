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
        carries its bias + command; QDIS_CMD keeps its default-OFF pulldown on this board.
        Pin order V15, GND, CMD, GND (round 8 cross-check): CMD is never next to V15 — a pin short
        would push 15 V into the card's USCH2 output and its V5A rail. */}
    <chip name="JDIS" footprint={Header(4)} {...gp()}
      pinLabels={{ pin1: "V15", pin2: "GND1", pin3: "CMD", pin4: "GND2" }}
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
    {/* TLP152 LED 261 R 1 % from UASCG: 10.3-14.8 mA over V5A 4.9-5.1 V and -40...100 C, inside the
        DS 10-15 mA recommended I_F (I_FLH 7.5 mA max). Round 7: 470 R gave 5.4-7 mA; round-8
        cross-check: 270 R dipped to 9.9 mA cold. */}
    <resistor name="RASCL" resistance="261" footprint="0603" {...gp()} connections={{ pin1: "net.ASC_CMD", pin2: "net.ASCA" }} />
    {/* NSI6611 ASC abs max = GND2+6 V (DS 1.2 §2) — series 2.2k + 5.1 V zener clamp the
        18 V opto swing to a legal ASC level (F28) */}
    <resistor name="RASCG" resistance="2.2k" footprint="0603" {...gp()} connections={{ pin1: "net.ASCVO", pin2: "net.ASC_DRV" }} />
    <diode name="ZASC" footprint={Smd2FP()} {...gp()} connections={{ anode: "net.DCN", cathode: "net.ASC_DRV" }} />
    <resistor name="RASCPD" resistance="10k" footprint="0603" {...gp()} connections={{ pin1: "net.ASC_DRV", pin2: "net.DCN" }} />
    {/* ASC break-before-make, delay half (round 7, RR05): 12 nF on the 1.8 k Thevenin holds
        ASC below its 2.7 V rising threshold for >= 3.5 us (fast corner: QA01C-18 at its 20.9 V
        top, C -5 %), so with tASC_r (0.39 us min) the low sides start >= 3.9 us after the latch sets. The
        high sides are already turning off: FS0B dropped DRV_EN (FS1B path) or the eFlexPWM
        fault forced PWM off (MCU path) before ASC_REQ. Entry completes <= 7.5 us (16.9 V low
        end, round 9); release is
        fast through DASCR into the opto output (<= 0.75 us). Each LS ASC pin has its own 1 k
        (GateDrive cell). The ASC input has hysteresis (2.7-3.2 / 1.3-1.7 V). */}
    <capacitor name="CASCD" capacitance="12nF" footprint="0603" {...gp()} connections={{ pin1: "net.ASC_DRV", pin2: "net.DCN" }} />
    <diode name="DASCR" footprint={Smd2FP()} {...gp()} connections={{ anode: "net.ASC_DRV", cathode: "net.ASCVO" }} />
    <capacitor name="CASC" capacitance="100nF" footprint="0603" {...gp()} connections={{ pin1: "net.V18A", pin2: "net.DCN" }} />

    {/* ---- ISOLATED DC-LINK SENSING: two independent channels, each with its OWN reinforced bias ---- */}
    <IsoVSense id="1" outP="net.VDC1_P" outN="net.VDC1_N" />
    <IsoVSense id="2" outP="net.VDC2_P" outN="net.VDC2_N" />
    {/* Round 12 (R1-F12, R2-F02/F03): the bound "MGJ2D150505SC" does not exist — the MGJ2 family only
        makes gate-drive pairs, and its reinforced approval is 150 Vrms — so each channel's bias is now a
        TI UCC12050 (5 V in -> 5.0 V iso, SEL tied to VISO, EN to VINP; V_IORM 1697 Vpk, V_IOWM
        1200 Vrms / 1697 VDC reinforced per VDE 0884-11, the same class as the AMC1311 it feeds).
        It needs 5 V in and idles at 50 mA, so each has its own NCV4276C from V15 (0.6 W in DPAK,
        instead of 1.2 W on one part or on the V5GD LDO); the two channels stay independent (F4).
        Pins per TI Table 5-1: 1 EN, 2 GNDP, 3 VINP, 4 SYNC (tie GNDP = internal osc), 5 SYNC_OK
        (open drain, unused), 6-8 NC (primary domain -> GNDP), 9/15/16 GNDS (15 is the bypass return),
        10-12 NC (isolated domain -> GNDS), 13 SEL, 14 VISO. 10 uF 16 V X7R on both sides. */}
    {[["B", "V5ISO"], ["C", "V5ISO2"]].map(([k, v]) => (
      <group key={k}>
        <chip name={`U5L${k}`} footprint={SmdFP(5)} {...gp()} pinLabels={{ pin1: "IN", pin2: "INH", pin3: "GND", pin4: "NC", pin5: "OUT" }}
          connections={{ IN: "net.V15", INH: "net.V15", GND: "net.DGND", NC: `net.NC_U5L${k}4`, OUT: `net.V5S${k}` }} />
        <capacitor name={`C5L${k}1`} capacitance="1uF" footprint="0805" {...gp()} connections={{ pin1: "net.V15", pin2: "net.DGND" }} />
        <capacitor name={`C5L${k}2`} capacitance="10uF" footprint="1206" {...gp()} connections={{ pin1: `net.V5S${k}`, pin2: "net.DGND" }} />
        <chip name={`PS5${k}`} footprint={SmdFP(16)} {...gp()}
          pinLabels={{ pin1: "EN", pin2: "GNDP", pin3: "VINP", pin4: "SYNC", pin5: "SYNC_OK", pin6: "NC6", pin7: "NC7", pin8: "NC8", pin9: "GNDS9", pin10: "NC10", pin11: "NC11", pin12: "NC12", pin13: "SEL", pin14: "VISO", pin15: "GNDS", pin16: "GNDS16" }}
          connections={{ EN: `net.V5S${k}`, GNDP: "net.DGND", VINP: `net.V5S${k}`, SYNC: "net.DGND", SYNC_OK: `net.NC_PS5${k}5`, NC6: "net.DGND", NC7: "net.DGND", NC8: "net.DGND", GNDS9: "net.DCN", NC10: "net.DCN", NC11: "net.DCN", NC12: "net.DCN", SEL: `net.${v}`, VISO: `net.${v}`, GNDS: "net.DCN", GNDS16: "net.DCN" }} />
        <capacitor name={`C5${k}1`} capacitance="10uF" footprint="1206" {...gp()} connections={{ pin1: `net.${v}`, pin2: "net.DCN" }} />
        <capacitor name={`C5${k}2`} capacitance="100nF" footprint="0603" {...gp()} connections={{ pin1: `net.${v}`, pin2: "net.DCN" }} />
      </group>
    ))}

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
        AMC1311 LV sides stay alive whenever the card is awake (the whole LV feed is switched
        on the card since round 9, N17 — no parking drain) — gate POWER stays separately
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
    {/* UB15 loop compensation per TI §8.2.1.2.11: series R3/C4 on COMP make the pole+zero
        (starting values 2 k / 100 nF from the DS); small parallel C5 for the HF pole.
        Rev A.4.3 — a lone 10 nF to ground gave no zero and ~0° screening phase margin. */}
    <resistor name="RB15C" resistance="2k" footprint="0603" {...gp()} connections={{ pin1: "net.B15CO", pin2: "net.B15CZ" }} />
    <capacitor name="CB15CC" capacitance="100nF" footprint="0603" {...gp()} connections={{ pin1: "net.B15CZ", pin2: "net.DGND" }} />
    <capacitor name="CB15C" capacitance="470pF" footprint="0603" {...gp()} connections={{ pin1: "net.B15CO", pin2: "net.DGND" }} />
    {/* V15 protective post-regulator (rev A.4.1): a boost cannot regulate below its input —
        when V12L rides above ~15.5 V (24 V jump start, clamped load dump) the LB15/DB15
        path feeds V15 directly. NCV4276C-ADJ (40 V in, 400 mA) sits in mild dropout in
        normal operation (V15 ≈ 15.0-15.2 V) and CLAMPS at 15.0 V during pass-through, so
        the QA01C modules (13.5-16.5 V window) never see the raw rail. Vout = 2.5·(1+49.9/10). */}
    <chip name="ULDO15" footprint={SmdFP(5)} {...gp()} pinLabels={{ pin1: "IN", pin2: "INH", pin3: "GND", pin4: "VA", pin5: "OUT" }}
      connections={{ IN: "net.V15B", INH: "net.V15B", GND: "net.DGND", VA: "net.V15VA", OUT: "net.V15" }} />
    <resistor name="RLD1" resistance="49.9k" footprint="0603" {...gp()} connections={{ pin1: "net.V15", pin2: "net.V15VA" }} />
    <resistor name="RLD2" resistance="10k" footprint="0603" {...gp()} connections={{ pin1: "net.V15VA", pin2: "net.DGND" }} />
    {/* ADJ + low-ESR ceramic COUT needs the Cb feed-forward across the top divider leg
        (onsemi Fig. 4 / stability section): 220 pF on 49.9 k -> f_z 14.5 kHz, inside the
        11-18 kHz guidance for the 22 uF example (rev A.4.2) */}
    <capacitor name="CLDC" capacitance="220pF" footprint="0603" {...gp()} connections={{ pin1: "net.V15", pin2: "net.V15VA" }} />
    <capacitor name="CLD15" capacitance="22uF" footprint="1210" {...gp()} connections={{ pin1: "net.V15", pin2: "net.DGND" }} />

    {/* ---- CONTROL INTERFACE: harness + default-OFF pulldowns ---- */}
    <Harness name="JIC" />
    {["PWM_UH", "PWM_UL", "PWM_VH", "PWM_VL", "PWM_WH", "PWM_WL", "EN_FLYBK_HS", "EN_FLYBK_LS", "ASC_CMD", "QDIS_CMD", "DRV_EN"].map((n, i) => (
      <resistor key={n} name={`RPD${i}`} resistance="10k" footprint="0603" {...gp()}
        connections={{ pin1: `net.${n}`, pin2: "net.DGND" }} />
    ))}
    {/* SKU identity (review A.6 platform): one resistor per build variant on harness pin 2,
        read by the card ADC against its 10 k pull-up — 8XX-SiC 10k, 8XX-IGBT 4.7k, 4XX-IGBT
        2.2k, 4XX-SiC 22k (values per SKU BOM). Open/short = invalid = no DRV_EN. */}
    <resistor name="RHWID" resistance="10k" footprint="0603" {...gp()} connections={{ pin1: "net.HW_ID", pin2: "net.DGND" }} />
    {/* AGND-DGND single-point tie lives on the card; PE bond here */}
    <resistor name="RPET" resistance="1M" footprint="1206" {...gp()} connections={{ pin1: "net.DGND", pin2: "net.PE" }} />
    <capacitor name="CPET" capacitance="4.7nF" footprint={FilmBoxFP(10)} {...gp()} connections={{ pin1: "net.DGND", pin2: "net.PE" }} />
  </board>
);
