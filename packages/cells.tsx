// cells.tsx — parameterized schematic cells for the Traction Inverter (220 kW / 800 V).
// Schematic-complete netlist is the deliverable; PCB coordinates are a coarse grid only so
// builds succeed (layout is a later phase — same discipline as the DC-Modules platform).
//
// Pin maps: NSI6611A-Q1 map verified against datasheet rev 1.2 (carried from the DC-Modules
// platform R4-2 audit). MCU/SBC pin NUMBERS are symbolic (names are the real GEN3 port/pin
// names from SPF-91122); mark §VERIFY before layout. Module aux-pin numbering VERIFY vs the
// hiitio EconoDUAL 3 drawing.

// ---- footprints ------------------------------------------------------------------------
// Generic n-pad SMD row: keeps every build independent of footprinter string coverage.
export const SmdFP = (n: number, hints?: string[][]) => (
  <footprint>
    {Array.from({ length: n }, (_, i) => (
      <smtpad key={i} portHints={hints?.[i] ?? [`pin${i + 1}`]} pcbX={i * 1.6 - (n - 1) * 0.8} pcbY={0} width="0.9mm" height="1.4mm" shape="rect" />
    ))}
    <courtyardrect pcbX={0} pcbY={0} width={`${n * 1.6 + 1.5}mm`} height="3mm" />
  </footprint>
);
export const Smd2FP = () => SmdFP(2, [["pin1", "anode"], ["pin2", "cathode"]]);
export const StudFP = (d = 8.5, o = 16) => (
  <footprint>
    <platedhole portHints={["pin1"]} pcbX={0} pcbY={0} holeDiameter={`${d}mm`} outerDiameter={`${o}mm`} shape="circle" />
    <courtyardrect pcbX={0} pcbY={0} width={`${o + 4}mm`} height={`${o + 4}mm`} />
  </footprint>
);
export const FilmCanFP = () => (
  // 1100 V DC-link film can, 37.5 mm pitch
  <footprint>
    <platedhole portHints={["pin1"]} pcbX={-18.75} pcbY={0} holeDiameter="1.5mm" outerDiameter="2.8mm" shape="circle" />
    <platedhole portHints={["pin2"]} pcbX={18.75} pcbY={0} holeDiameter="1.5mm" outerDiameter="2.8mm" shape="circle" />
    <courtyardrect pcbX={0} pcbY={0} width="42mm" height="22mm" />
  </footprint>
);
export const FilmBoxFP = (pitch = 27.5) => (
  <footprint>
    <platedhole portHints={["pin1"]} pcbX={-pitch / 2} pcbY={0} holeDiameter="1.2mm" outerDiameter="2.2mm" shape="circle" />
    <platedhole portHints={["pin2"]} pcbX={pitch / 2} pcbY={0} holeDiameter="1.2mm" outerDiameter="2.2mm" shape="circle" />
    <courtyardrect pcbX={0} pcbY={0} width={`${pitch + 4}mm`} height="12mm" />
  </footprint>
);
export const DiscFP = (pitch = 10, dia = 14) => (
  <footprint>
    <platedhole portHints={["pin1"]} pcbX={-pitch / 2} pcbY={0} holeDiameter="1.2mm" outerDiameter="2.2mm" shape="circle" />
    <platedhole portHints={["pin2"]} pcbX={pitch / 2} pcbY={0} holeDiameter="1.2mm" outerDiameter="2.2mm" shape="circle" />
    <courtyardcircle pcbX={0} pcbY={0} radius={`${dia / 2 + 1}mm`} />
  </footprint>
);
export const AxialFP = (pitch = 25) => (
  <footprint>
    <platedhole portHints={["pin1"]} pcbX={-pitch / 2} pcbY={0} holeDiameter="1.1mm" outerDiameter="2mm" shape="circle" />
    <platedhole portHints={["pin2"]} pcbX={pitch / 2} pcbY={0} holeDiameter="1.1mm" outerDiameter="2mm" shape="circle" />
    <courtyardrect pcbX={0} pcbY={0} width={`${pitch + 6}mm`} height="10mm" />
  </footprint>
);
export const TO247_4L = () => (
  <footprint>
    {["pin1", "pin2", "pin3", "pin4"].map((h, i) => (
      <platedhole key={h} portHints={[h]} pcbX={-3.81 + i * 2.54} pcbY={0} holeDiameter="1.8mm" outerDiameter="2.4mm" shape="circle" />
    ))}
    <courtyardrect pcbX={0} pcbY={0} width="16.4mm" height="5.5mm" />
  </footprint>
);
// EconoDUAL 3 half-bridge: 3 power terminals (DC+, DC-, AC) + 8 aux pins.
// Aux numbering VERIFY vs the hiitio HCS600FH120D3C1 outline drawing.
export const EconoDual3FP = () => (
  <footprint>
    {[["pin1", -45], ["pin2", 0], ["pin3", 45]].map(([h, x]) => (
      <platedhole key={h as string} portHints={[h as string]} pcbX={x as number} pcbY={20} holeDiameter="5mm" outerDiameter="9mm" shape="circle" />
    ))}
    {[["pin4", -35], ["pin5", -25], ["pin6", -15], ["pin7", -5], ["pin8", 5], ["pin9", 15], ["pin10", 25], ["pin11", 35]].map(([h, x]) => (
      <platedhole key={h as string} portHints={[h as string]} pcbX={x as number} pcbY={-20} holeDiameter="1.2mm" outerDiameter="2.2mm" shape="circle" />
    ))}
    <courtyardrect pcbX={0} pcbY={0} width="122mm" height="62mm" />
  </footprint>
);
export const XfmrEEFP = (n = 6) => (
  <footprint>
    {Array.from({ length: n }, (_, i) => (
      <platedhole key={i} portHints={[`pin${i + 1}`]} pcbX={-7.5 + (i % 3) * 7.5} pcbY={i < 3 ? -6 : 6} holeDiameter="1.1mm" outerDiameter="2mm" shape="circle" />
    ))}
    <courtyardrect pcbX={0} pcbY={0} width="20mm" height="16mm" />
  </footprint>
);
export const Header = (n: number, rows = 1) => (
  <footprint>
    {Array.from({ length: n }, (_, i) => (
      <platedhole key={i} portHints={[`pin${i + 1}`]} pcbX={(i % Math.ceil(n / rows)) * 2.54} pcbY={Math.floor(i / Math.ceil(n / rows)) * 2.54} holeDiameter="1mm" outerDiameter="1.8mm" shape="circle" />
    ))}
    <courtyardrect pcbX={(Math.ceil(n / rows) - 1) * 1.27} pcbY={(rows - 1) * 1.27} width={`${Math.ceil(n / rows) * 2.54 + 2}mm`} height={`${rows * 2.54 + 2}mm`} />
  </footprint>
);

// coarse placement grid so netlist builds do not stack parts
let __pi = 0;
export const gp = () => {
  const i = __pi++;
  return { pcbX: (i % 26) * 20 - 250, pcbY: Math.floor(i / 26) * 20 - 250 };
};

// ---- gate-drive channel (one per switch, x6) -------------------------------------------
// NSI6611A-Q1 verified map — driver side: 1 ASC, 2 DESAT, 3 GND2 (Kelvin), 4 OUTH, 5 VCC2,
// 6 OUTL, 7 CLAMP, 8 VEE2 · input side: 9 GND1, 10 IN+, 11 IN-, 12 RDY, 13 FLT#, 14 RST/EN,
// 15 VCC1, 16 TEST.
// GEN3 patterns carried: complementary PWM on IN- (shoot-through lockout), DESAT via
// BAT64-04 + 100R + 2x US1M to drain, 47 pF blanking, Miller clamp linked 0R to gate,
// 10k G-S bleed + 18V/5V1 zener stack at driver, 1M HV pulldown at module pin, secondary
// winding zener-split +15/-4.3 vs Kelvin.
export const GateDrive = ({ ph, side, drain, gate, ks, pwmP, pwmN, en, flt, rdy, asc, wA, wB }: {
  ph: string; side: "H" | "L"; drain: string; gate: string; ks: string;
  pwmP: string; pwmN: string; en: string; flt: string; rdy: string;
  asc?: string; wA: string; wB: string;
}) => {
  const p = `${ph}${side}`;
  const vcc = `net.VCC_${p}`, vee = `net.VEE_${p}`, dst = `net.DST_${p}`;
  return (
    <group>
      <chip name={`U${p}G`} footprint={SmdFP(16)} {...gp()}
        pinLabels={{ pin1: "ASC", pin2: "DESAT", pin3: "GND2", pin4: "OUTH", pin5: "VCC2", pin6: "OUTL", pin7: "CLAMP", pin8: "VEE2", pin9: "GND1", pin10: "INP", pin11: "INN", pin12: "RDY", pin13: "FLT", pin14: "EN", pin15: "VCC1", pin16: "TEST" }}
        connections={{
          ASC: asc ?? ks, DESAT: dst, GND2: ks, VCC2: vcc, VEE2: vee,
          GND1: "net.DGND", INP: pwmP, INN: pwmN, RDY: rdy, FLT: flt, EN: en, VCC1: "net.V5GD", TEST: "net.DGND",
          OUTH: `net.OH_${p}`, OUTL: `net.OL_${p}`, CLAMP: `net.CLP_${p}`,
        }} />
      {/* gate network */}
      <resistor name={`R${p}ON`} resistance="1.5" footprint={SmdFP(2)} {...gp()} connections={{ pin1: `net.OH_${p}`, pin2: gate }} />
      <resistor name={`R${p}OFF`} resistance="1" footprint={SmdFP(2)} {...gp()} connections={{ pin1: `net.OL_${p}`, pin2: gate }} />
      <resistor name={`R${p}MC`} resistance="0" footprint="0805" {...gp()} connections={{ pin1: `net.CLP_${p}`, pin2: gate }} />
      <resistor name={`R${p}GS`} resistance="10k" footprint="0805" {...gp()} connections={{ pin1: gate, pin2: ks }} />
      <resistor name={`R${p}PD`} resistance="1M" footprint="1206" {...gp()} connections={{ pin1: gate, pin2: ks }} />
      <diode name={`D${p}Z1`} footprint={Smd2FP()} {...gp()} connections={{ anode: `net.ZK_${p}`, cathode: gate }} />
      <diode name={`D${p}Z2`} footprint={Smd2FP()} {...gp()} connections={{ anode: `net.ZK_${p}`, cathode: ks }} />
      {/* DESAT chain: VCC -> BAT64 -> node -> 100R -> 2x US1M -> drain; 47p blanking */}
      <chip name={`D${p}SB`} footprint={SmdFP(3)} {...gp()} pinLabels={{ pin1: "A1", pin2: "A2", pin3: "K" }}
        connections={{ A1: vcc, A2: dst, K: dst }} />
      <resistor name={`R${p}DS`} resistance="100" footprint="0805" {...gp()} connections={{ pin1: dst, pin2: `net.DS1_${p}` }} />
      <diode name={`D${p}S1`} footprint={Smd2FP()} {...gp()} connections={{ anode: `net.DS1_${p}`, cathode: `net.DS2_${p}` }} />
      <diode name={`D${p}S2`} footprint={Smd2FP()} {...gp()} connections={{ anode: `net.DS2_${p}`, cathode: drain }} />
      <capacitor name={`C${p}BL`} capacitance="47pF" footprint="0603" {...gp()} connections={{ pin1: dst, pin2: ks }} />
      {/* driver rail decoupling */}
      <capacitor name={`C${p}B1`} capacitance="100nF" footprint="0603" {...gp()} connections={{ pin1: vcc, pin2: ks }} />
      <capacitor name={`C${p}B2`} capacitance="4.7uF" footprint="1206" {...gp()} connections={{ pin1: vcc, pin2: ks }} />
      {/* floating secondary: rectifier -> VCC ; zener splits winding return -> VEE (-4.3) */}
      <diode name={`D${p}R`} footprint={Smd2FP()} {...gp()} connections={{ anode: wA, cathode: vcc }} />
      <diode name={`Z${p}V`} footprint={Smd2FP()} {...gp()} connections={{ anode: vee, cathode: ks }} />
      <capacitor name={`C${p}V1`} capacitance="4.7uF" footprint="1206" {...gp()} connections={{ pin1: vcc, pin2: ks }} />
      <capacitor name={`C${p}E1`} capacitance="10uF" footprint="1206" {...gp()} connections={{ pin1: ks, pin2: vee }} />
      <resistor name={`R${p}BL`} resistance="5.1k" footprint="0805" {...gp()} connections={{ pin1: vcc, pin2: vee }} />
      <trace from={vee} to={wB} />
    </group>
  );
};

// ---- gate-power flyback chain (one per side: HS / LS — GEN3 dual-flyback pattern) -------
// DFM rev: UCC28C42-class current-mode controller (universally stocked — LCSC/DigiKey/Mouser,
// AEC variant available) replaces the thin-distribution NJW4140. Pin map is the real
// UCC28C42 SOIC-8: 1 COMP, 2 FB, 3 CS, 4 RT/CT, 5 GND, 6 OUT, 7 VDD, 8 VREF.
// It has no EN pin, so enable is a 2-transistor default-OFF clamp: EN low/floating -> COMP
// held low -> no switching (matches the harness default-OFF discipline).
// LFPAK switch + 3 transformers (primaries paralleled), one floating secondary per phase;
// primary-side regulated from TF?1 aux winding.
export const FlybackChain = ({ id, v12 }: { id: "H" | "L"; v12: string }) => {
  const sw = `net.FSW_${id}`, cs = `net.FCS_${id}`, vcc = `net.FVCC_${id}`, fb = `net.FFB_${id}`, vr = `net.FVR_${id}`;
  return (
    <group>
      <chip name={`UF${id}`} footprint={SmdFP(8)} {...gp()}
        pinLabels={{ pin1: "COMP", pin2: "FB", pin3: "CS", pin4: "RTCT", pin5: "GND", pin6: "OUT", pin7: "VCC", pin8: "VREF" }}
        connections={{ COMP: `net.FCO_${id}`, FB: fb, CS: `net.FSI_${id}`, RTCT: `net.FCT_${id}`, GND: "net.DGND", OUT: `net.FDR_${id}`, VCC: vcc, VREF: vr }} />
      <chip name={`QF${id}`} footprint={SmdFP(4)} {...gp()} pinLabels={{ pin1: "G", pin2: "S", pin3: "S2", pin4: "D" }}
        connections={{ G: `net.FG_${id}`, S: cs, S2: cs, D: sw }} />
      <resistor name={`RF${id}G`} resistance="10" footprint="0603" {...gp()} connections={{ pin1: `net.FDR_${id}`, pin2: `net.FG_${id}` }} />
      <resistor name={`RF${id}GO`} resistance="20" footprint="0603" {...gp()} connections={{ pin1: `net.FG_${id}`, pin2: `net.FGO_${id}` }} />
      <diode name={`DF${id}G`} footprint={Smd2FP()} {...gp()} connections={{ anode: `net.FGO_${id}`, cathode: `net.FDR_${id}` }} />
      <diode name={`ZF${id}G`} footprint={Smd2FP()} {...gp()} connections={{ anode: cs, cathode: `net.FG_${id}` }} />
      <resistor name={`RF${id}GS`} resistance="51k" footprint="0603" {...gp()} connections={{ pin1: `net.FG_${id}`, pin2: cs }} />
      <resistor name={`RF${id}CS`} resistance="0.033" footprint="1206" {...gp()} connections={{ pin1: cs, pin2: "net.DGND" }} />
      <resistor name={`RF${id}SI`} resistance="100" footprint="0603" {...gp()} connections={{ pin1: cs, pin2: `net.FSI_${id}` }} />
      <capacitor name={`CF${id}SI`} capacitance="100pF" footprint="0603" {...gp()} connections={{ pin1: `net.FSI_${id}`, pin2: "net.DGND" }} />
      {/* RT/CT: 10k from VREF + 3.3nF -> ~52 kHz (fsw = 1.72/(RT*CT)) */}
      <resistor name={`RF${id}RT`} resistance="10k" footprint="0603" {...gp()} connections={{ pin1: vr, pin2: `net.FCT_${id}` }} />
      <capacitor name={`CF${id}CT`} capacitance="3.3nF" footprint="0603" {...gp()} connections={{ pin1: `net.FCT_${id}`, pin2: "net.DGND" }} />
      <capacitor name={`CF${id}VR`} capacitance="100nF" footprint="0603" {...gp()} connections={{ pin1: vr, pin2: "net.DGND" }} />
      <capacitor name={`CF${id}CO`} capacitance="22nF" footprint="0603" {...gp()} connections={{ pin1: `net.FCO_${id}`, pin2: "net.DGND" }} />
      <capacitor name={`CF${id}CF`} capacitance="47pF" footprint="0603" {...gp()} connections={{ pin1: `net.FCO_${id}`, pin2: fb }} />
      {/* default-OFF enable: EN high -> QE1 on -> QE2 gate low -> COMP released (run);
          EN low/floating -> QE2 clamps COMP (stopped) */}
      <chip name={`QF${id}E1`} footprint={SmdFP(3)} {...gp()} pinLabels={{ pin1: "G", pin2: "S", pin3: "D" }}
        connections={{ G: `net.EN_FLYBK_${id}S`, S: "net.DGND", D: `net.FEN_${id}` }} />
      <resistor name={`RF${id}EN`} resistance="100k" footprint="0603" {...gp()} connections={{ pin1: vr, pin2: `net.FEN_${id}` }} />
      <chip name={`QF${id}E2`} footprint={SmdFP(3)} {...gp()} pinLabels={{ pin1: "G", pin2: "S", pin3: "D" }}
        connections={{ G: `net.FEN_${id}`, S: "net.DGND", D: `net.FCO_${id}` }} />
      {/* snubber + clamp on the switch node */}
      <resistor name={`RF${id}SN`} resistance="100" footprint="0805" {...gp()} connections={{ pin1: sw, pin2: `net.FSN_${id}` }} />
      <capacitor name={`CF${id}SN`} capacitance="100pF" footprint="1206" {...gp()} connections={{ pin1: `net.FSN_${id}`, pin2: v12 }} />
      <diode name={`DF${id}SN`} footprint={Smd2FP()} {...gp()} connections={{ anode: sw, cathode: v12 }} />
      {/* VCC: trickle start from the 12 V rail, then the aux winding takes over
          (the aux-only wiring could never start — startup feed added at DFM review) */}
      <resistor name={`RF${id}ST`} resistance="4.7k" footprint="0805" {...gp()} connections={{ pin1: v12, pin2: vcc }} />
      {/* primary-side regulation: aux winding on TF?1 -> VCC + FB divider (18k/15k/1.3k) */}
      <diode name={`DF${id}A`} footprint={Smd2FP()} {...gp()} connections={{ anode: `net.FAX_${id}`, cathode: vcc }} />
      <capacitor name={`CF${id}A`} capacitance="4.7uF" footprint="1206" {...gp()} connections={{ pin1: vcc, pin2: "net.DGND" }} />
      <resistor name={`RF${id}FB1`} resistance="18k" footprint="0603" {...gp()} connections={{ pin1: vcc, pin2: fb }} />
      <resistor name={`RF${id}FB2`} resistance="15k" footprint="0603" {...gp()} connections={{ pin1: fb, pin2: `net.FFB2_${id}` }} />
      <resistor name={`RF${id}FB3`} resistance="1.3k" footprint="0603" {...gp()} connections={{ pin1: `net.FFB2_${id}`, pin2: "net.DGND" }} />
      {/* three transformers, primaries paralleled on the switch node; TF?1 carries the aux */}
      {[1, 2, 3].map((k) => (
        <chip key={k} name={`TF${id}${k}`} footprint={XfmrEEFP(6)} {...gp()}
          pinLabels={{ pin1: "P1", pin2: "P2", pin3: "A1", pin4: "A2", pin5: "S1", pin6: "S2" }}
          connections={{
            P1: v12, P2: sw,
            A1: k === 1 ? `net.FAX_${id}` : `net.NC_TF${id}${k}A1`, A2: k === 1 ? "net.DGND" : `net.NC_TF${id}${k}A2`,
            S1: `net.W_${["U", "V", "W"][k - 1]}${id}_A`, S2: `net.W_${["U", "V", "W"][k - 1]}${id}_B`,
          }} />
      ))}
    </group>
  );
};

// ---- isolated DC-link voltage sense (x2 independent channels) ---------------------------
// Divider referenced to DCN; AMC1311 HV side biased by the shared reinforced 5 V module
// (PS5B, DCN-referenced); LV side on V5GD/AGND; differential output crosses the harness.
export const IsoVSense = ({ id, outP, outN }: { id: string; outP: string; outN: string }) => {
  const U = id === "1" ? "UIVDC" : "UIVB";
  const RT = id === "1" ? "RVDD" : "RVBD";
  const RL = id === "1" ? "RVDDL" : "RVBDL";
  const CF = id === "1" ? "CVDDF" : "CVBDF";
  const tap = `net.VTAP${id}`;
  return (
    <group>
      {[1, 2, 3, 4, 5, 6].map((k) => (
        <resistor key={k} name={`${RT}${k}`} resistance="470k" footprint="1206" {...gp()}
          connections={{ pin1: k === 1 ? "net.DCP" : `net.${RT}_${k - 1}${k}`, pin2: k === 6 ? tap : `net.${RT}_${k}${k + 1}` }} />
      ))}
      <resistor name={RL} resistance="6.65k" footprint="0603" {...gp()} connections={{ pin1: tap, pin2: "net.DCN" }} />
      <capacitor name={CF} capacitance="1nF" footprint="0603" {...gp()} connections={{ pin1: tap, pin2: "net.DCN" }} />
      <chip name={U} footprint={SmdFP(8)} {...gp()}
        pinLabels={{ pin1: "VDD1", pin2: "SHTDN", pin3: "VINP", pin4: "GND1", pin5: "GND2", pin6: "VOUTN", pin7: "VOUTP", pin8: "VDD2" }}
        connections={{ VDD1: "net.V5ISO", SHTDN: "net.DCN", VINP: tap, GND1: "net.DCN", GND2: "net.AGND", VOUTN: outN, VOUTP: outP, VDD2: "net.V5GD" }} />
    </group>
  );
};

// ---- module NTC route (power board side: series R + clamp + filter to harness) ----------
export const ModNtc = ({ ph, ntcA, ntcB }: { ph: string; ntcA: string; ntcB: string }) => (
  <group>
    <resistor name={`R${ph}TS`} resistance="100" footprint="0603" {...gp()} connections={{ pin1: ntcA, pin2: `net.TMOD_${ph}` }} />
    <capacitor name={`C${ph}TF`} capacitance="2.2nF" footprint="0603" {...gp()} connections={{ pin1: `net.TMOD_${ph}`, pin2: "net.TMOD_RTN" }} />
    <diode name={`D${ph}TP`} footprint={Smd2FP()} {...gp()} connections={{ anode: `net.TMOD_${ph}`, cathode: "net.V5GD" }} />
    <trace from={ntcB} to="net.TMOD_RTN" />
  </group>
);

// ---- CAN-FD channel (card, x2) — split termination + CMC + ESD (GEN3 values) ------------
export const CanFd = ({ id, tx, rx, stb, canh, canl }: { id: string; tx: string; rx: string; stb: string; canh: string; canl: string }) => (
  <group>
    <chip name={`UCAN${id}`} footprint={SmdFP(8)} {...gp()}
      pinLabels={{ pin1: "TXD", pin2: "GND", pin3: "VCC", pin4: "RXD", pin5: "VIO", pin6: "CANL", pin7: "CANH", pin8: "STB" }}
      connections={{ TXD: tx, GND: "net.DGND", VCC: "net.V5A", RXD: rx, VIO: "net.V5A", CANL: `net.CANL${id}_T`, CANH: `net.CANH${id}_T`, STB: stb }} />
    <chip name={`LCAN${id}`} footprint={XfmrEEFP(4)} {...gp()} pinLabels={{ pin1: "A1", pin2: "A2", pin3: "B1", pin4: "B2" }}
      connections={{ A1: `net.CANH${id}_T`, A2: canh, B1: `net.CANL${id}_T`, B2: canl }} />
    <resistor name={`RCT${id}A`} resistance="60.4" footprint="0603" {...gp()} connections={{ pin1: `net.CANH${id}_T`, pin2: `net.CANS${id}` }} />
    <resistor name={`RCT${id}B`} resistance="60.4" footprint="0603" {...gp()} connections={{ pin1: `net.CANS${id}`, pin2: `net.CANL${id}_T` }} />
    <capacitor name={`CCT${id}`} capacitance="4.7nF" footprint="0603" {...gp()} connections={{ pin1: `net.CANS${id}`, pin2: "net.DGND" }} />
    <chip name={`TVSC${id}`} footprint={SmdFP(3)} {...gp()} pinLabels={{ pin1: "A", pin2: "B", pin3: "G" }}
      connections={{ A: canh, B: canl, G: "net.DGND" }} />
    <capacitor name={`CCAN${id}`} capacitance="1uF" footprint="0603" {...gp()} connections={{ pin1: "net.V5A", pin2: "net.DGND" }} />
  </group>
);

// ---- phase-current hall chain (card, x3) — GEN3 exact: bead + filters + 2x OPA376 -------
export const HallChain = ({ ph, vout, adc }: { ph: string; vout: string; adc: string }) => (
  <group>
    <inductor name={`L${ph}B`} inductance="220nH" footprint="0805" {...gp()} connections={{ pin1: "net.V5A", pin2: `net.V5S_${ph}` }} />
    <capacitor name={`C${ph}S1`} capacitance="47nF" footprint="0603" {...gp()} connections={{ pin1: `net.V5S_${ph}`, pin2: "net.AGND" }} />
    <capacitor name={`C${ph}S2`} capacitance="4.7nF" footprint="0603" {...gp()} connections={{ pin1: `net.V5S_${ph}`, pin2: "net.AGND" }} />
    <resistor name={`R${ph}B1`} resistance="100" footprint="0603" {...gp()} connections={{ pin1: vout, pin2: `net.IS1_${ph}` }} />
    <capacitor name={`C${ph}B1`} capacitance="3.3nF" footprint="0603" {...gp()} connections={{ pin1: `net.IS1_${ph}`, pin2: "net.AGND" }} />
    <chip name={`U${ph}B1`} footprint={SmdFP(5)} {...gp()} pinLabels={{ pin1: "OUT", pin2: "VN", pin3: "INP", pin4: "INN", pin5: "VP" }}
      connections={{ OUT: `net.IS2_${ph}`, VN: "net.AGND", INP: `net.IS1_${ph}`, INN: `net.IS2_${ph}`, VP: "net.V5A" }} />
    <resistor name={`R${ph}B2`} resistance="100" footprint="0603" {...gp()} connections={{ pin1: `net.IS2_${ph}`, pin2: `net.IS3_${ph}` }} />
    <capacitor name={`C${ph}B2`} capacitance="100nF" footprint="0603" {...gp()} connections={{ pin1: `net.IS3_${ph}`, pin2: "net.AGND" }} />
    <chip name={`U${ph}B2`} footprint={SmdFP(5)} {...gp()} pinLabels={{ pin1: "OUT", pin2: "VN", pin3: "INP", pin4: "INN", pin5: "VP" }}
      connections={{ OUT: `net.IS4_${ph}`, VN: "net.AGND", INP: `net.IS3_${ph}`, INN: `net.IS4_${ph}`, VP: "net.V5A" }} />
    <resistor name={`R${ph}B3`} resistance="100" footprint="0603" {...gp()} connections={{ pin1: `net.IS4_${ph}`, pin2: adc }} />
  </group>
);

// ---- board NTC input (card) -------------------------------------------------------------
export const NtcIn = ({ id, out }: { id: string; out: string }) => (
  <group>
    <chip name={`JT${id}`} footprint={Header(2)} {...gp()} pinLabels={{ pin1: "A", pin2: "B" }}
      connections={{ A: `net.NTC_${id}`, B: "net.AGND" }} />
    <resistor name={`RT${id}P`} resistance="10k" footprint="0603" {...gp()} connections={{ pin1: "net.VREF5", pin2: `net.NTC_${id}` }} />
    <capacitor name={`CT${id}F`} capacitance="47nF" footprint="0603" {...gp()} connections={{ pin1: `net.NTC_${id}`, pin2: "net.AGND" }} />
    <trace from={`net.NTC_${id}`} to={out} />
  </group>
);

// ---- 40-way harness map (single source for both boards) ---------------------------------
// [pin, net] — same nets both sides; power board holds default-OFF pulldowns.
export const HARNESS40: [number, string][] = [
  [1, "PWM_UH"], [2, "PWM_UL"], [3, "PWM_VH"], [4, "PWM_VL"], [5, "PWM_WH"], [6, "PWM_WL"],
  [7, "DGND"], [8, "EN_FLYBK_HS"], [9, "EN_FLYBK_LS"], [10, "DRV_EN"],
  [11, "ASC_CMD"], [12, "QDIS_CMD"], [13, "DGND"], [14, "FLT_HS_N"], [15, "FLT_LS_N"],
  [16, "RDY_HS"], [17, "RDY_LS"], [18, "DGND"], [19, "VDC1_P"], [20, "VDC1_N"],
  [21, "VDC2_P"], [22, "VDC2_N"], [23, "AGND"], [24, "TMOD_U"], [25, "TMOD_V"],
  [26, "TMOD_W"], [27, "TMOD_RTN"], [28, "HVIL_A"], [29, "HVIL_B"], [30, "DGND"],
  [31, "VBAT_H"], [32, "VBAT_H"], [33, "DGND"], [34, "DGND"], [35, "VBAT_L"],
  [36, "VBAT_L"], [37, "DGND"], [38, "DGND"], [39, "DGND"], [40, "DGND"],
];
export const Harness = ({ name }: { name: string }) => (
  <chip name={name} footprint={Header(40, 2)} {...gp()}
    pinLabels={Object.fromEntries(HARNESS40.map(([p, n]) => [`pin${p}`, `P${p}_${n}`]))}
    connections={Object.fromEntries(HARNESS40.map(([p, n]) => [`P${p}_${n}`, `net.${n}`]))} />
);
