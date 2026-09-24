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
// EconoDUAL 3 half-bridge — REAL HCS600FH120D3C1 numbering (datasheet rev X.0.1 Fig 1/2):
// 1=G_L 2=S_L(aux) 3=DC- 4=DC+ 5/6=NTC 7=G_H 8=S_H(aux) 9=HS drain-sense(aux) 10/11=AC.
// Power terminals (3,4,10,11) are M6/M8 screw lands; aux pins are press-fit/solder posts.
export const EconoDual3FP = () => (
  <footprint>
    {[["pin3", -45], ["pin4", 45], ["pin10", -15], ["pin11", 15]].map(([h, x]) => (
      <platedhole key={h as string} portHints={[h as string]} pcbX={x as number} pcbY={20} holeDiameter="6.5mm" outerDiameter="12mm" shape="circle" />
    ))}
    {[["pin1", -35], ["pin2", -25], ["pin5", -12], ["pin6", -4], ["pin7", 12], ["pin8", 22], ["pin9", 32]].map(([h, x]) => (
      <platedhole key={h as string} portHints={[h as string]} pcbX={x as number} pcbY={-20} holeDiameter="1.2mm" outerDiameter="2.2mm" shape="circle" />
    ))}
    <courtyardrect pcbX={0} pcbY={0} width="152mm" height="62mm" />
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
// TI UCC14141-Q1 (DWN-36, SLUSF10B Table 6-1): GNDP 1,2,5,8-18 · PG 3 · ENA 4 · VIN 6,7 · VEE 19-27,30,31,36 ·
// VDD 28,29 · RLIM 32 · FBVEE 33 · FBVDD 34 · VEEA 35. Reinforced isolated bias (V_IORM 1414 Vpk,
// V_IOWM 1000 Vrms, DS §7.5) drawn in the single-output configuration of DS Fig. 9-2: FBVEE tied to
// FBVDD, RLIM and PG open, VEEA on VEE. VDD-VEE = 2.5 V x (1 + 62 k/10 k) = 18.0 V (17.4-18.6 V over the
// 2.4675-2.5325 V reference, 1 % resistors and the 12.3 mV hysteresis). CIN 2 x 10 uF + 100 nF at VIN,
// COUT1 10 uF (+ the board's 100 nF at the load) — DS Table 9-2. ENA from V15 through 10 k/4.7 k
// (4.65-4.94 V: above V_EN_IR 2.1 V, under the 5.5 V recommended maximum), so the bias is on
// whenever V15 is, as the QA01C-18 it replaces was.
const UCC14141_PINS: [number, string][] = [
  [1, "GNDP1"], [2, "GNDP2"], [3, "PG"], [4, "ENA"], [5, "GNDP5"], [6, "VIN6"], [7, "VIN7"],
  ...Array.from({ length: 11 }, (_, i) => [8 + i, `GNDP${8 + i}`] as [number, string]),
  ...Array.from({ length: 9 }, (_, i) => [19 + i, `VEE${19 + i}`] as [number, string]),
  [28, "VDD28"], [29, "VDD29"], [30, "VEE30"], [31, "VEE31"], [32, "RLIM"], [33, "FBVEE"], [34, "FBVDD"], [35, "VEEA"], [36, "VEE36"],
];
export const IsoBias18 = ({ p, name, vout, vin = "net.V15", gnd = "net.DGND", ref = "net.DCN" }:
  { p: string; name: string; vout: string; vin?: string; gnd?: string; ref?: string }) => {
  const fb = `net.FB_${p}`, ena = `net.ENA_${p}`;
  const conn: Record<string, string> = {};
  for (const [, l] of UCC14141_PINS) {
    if (l.startsWith("GNDP")) conn[l] = gnd;
    else if (l.startsWith("VIN")) conn[l] = vin;
    else if (l.startsWith("VEE")) conn[l] = ref;          // VEE19..36 and VEEA
    else if (l.startsWith("VDD")) conn[l] = vout;
    else if (l === "FBVDD" || l === "FBVEE") conn[l] = fb;
    else if (l === "ENA") conn[l] = ena;                   // PG, RLIM: open
  }
  return (
    <group>
      <chip name={name} footprint={SmdFP(36)} {...gp()}
        pinLabels={Object.fromEntries(UCC14141_PINS.map(([n, l]) => [`pin${n}`, l]))} connections={conn} />
      <capacitor name={`C${p}I1`} capacitance="10uF" footprint="1206" {...gp()} connections={{ pin1: vin, pin2: gnd }} />
      <capacitor name={`C${p}I2`} capacitance="10uF" footprint="1206" {...gp()} connections={{ pin1: vin, pin2: gnd }} />
      <capacitor name={`C${p}IB`} capacitance="100nF" footprint="0603" {...gp()} connections={{ pin1: vin, pin2: gnd }} />
      <resistor name={`R${p}E1`} resistance="10k" footprint="0603" {...gp()} connections={{ pin1: vin, pin2: ena }} />
      <resistor name={`R${p}E2`} resistance="4.7k" footprint="0603" {...gp()} connections={{ pin1: ena, pin2: gnd }} />
      <capacitor name={`C${p}O`} capacitance="10uF" footprint="1210" {...gp()} connections={{ pin1: vout, pin2: ref }} />
      <resistor name={`R${p}F1`} resistance="62k" footprint="0603" {...gp()} connections={{ pin1: vout, pin2: fb }} />
      <resistor name={`R${p}F2`} resistance="10k" footprint="0603" {...gp()} connections={{ pin1: fb, pin2: ref }} />
      <capacitor name={`C${p}F`} capacitance="330pF" footprint="0603" {...gp()} connections={{ pin1: fb, pin2: ref }} />
    </group>
  );
};
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
// RG_ON 3.3 R = the HCS600 datasheet's characterized point; RG_OFF 6.8 R — at 3.3 R the DS
// fall time (13 ns cold at 600 A) is ~30 kA/us, more than any EconoDUAL-class loop can carry
// at 850 V under a 1080 V guard (review A.6 F01/F40). The loss model books Eoff at 6.8 R;
// DPT tunes RG_OFF within 3.3-10 R. IGBT/hybrid SKUs fit 1.0/1.0 R (SKU BOM),
// 10k G-S bleed + 18V/5V1 zener stack at driver, 1M HV pulldown at module pin, secondary
// winding zener-split +15/-5.1 vs Kelvin (HCS600 DS Note1 recommends +15/-5).
export const GateDrive = ({ ph, side, drain, gate, ks, pwmP, pwmN, en, flt, rdy, asc, wA }: {
  ph: string; side: "H" | "L"; drain: string; gate: string; ks: string;
  pwmP: string; pwmN: string; en: string; flt: string; rdy: string;
  asc?: string; wA: string;
}) => {
  const p = `${ph}${side}`;
  const vcc = `net.VCC_${p}`, vee = `net.VEE_${p}`, dst = `net.DST_${p}`;
  return (
    <group>
      <chip name={`U${p}G`} footprint={SmdFP(16)} {...gp()}
        pinLabels={{ pin1: "ASC", pin2: "DESAT", pin3: "GND2", pin4: "OUTH", pin5: "VCC2", pin6: "OUTL", pin7: "CLAMP", pin8: "VEE2", pin9: "GND1", pin10: "INP", pin11: "INN", pin12: "RDY", pin13: "FLT", pin14: "EN", pin15: "VCC1", pin16: "TEST" }}
        connections={{
          ASC: asc ? `net.ASCP_${p}` : ks, DESAT: dst, GND2: ks, VCC2: vcc, VEE2: vee,
          GND1: "net.DGND", INP: pwmP, INN: pwmN, RDY: rdy, FLT: flt, EN: en, VCC1: "net.V5GD", TEST: "net.DGND",
          OUTH: `net.OH_${p}`, OUTL: `net.OL_${p}`, CLAMP: `net.CLP_${p}`,
        }} />
      {/* LS ASC pin through 1 k at the driver (round 7 cross-check item 5): the shared ASC_DRV node
          (12 nF to DC-) is stiff, while each pin is referenced to its own Kelvin source — the
          L_s·di/dt between them must not drive the pin's clamp cells hard (abs GND2-0.3/+6 V) */}
      {asc && <resistor name={`R${p}AS`} resistance="1k" footprint="0603" {...gp()} connections={{ pin1: asc, pin2: `net.ASCP_${p}` }} />}
      {/* gate network */}
      <resistor name={`R${p}ON`} resistance="3.3" footprint={SmdFP(2)} {...gp()} connections={{ pin1: `net.OH_${p}`, pin2: gate }} />
      <resistor name={`R${p}OFF`} resistance="6.8" footprint={SmdFP(2)} {...gp()} connections={{ pin1: `net.OL_${p}`, pin2: gate }} />
      <resistor name={`R${p}MC`} resistance="0" footprint="0805" {...gp()} connections={{ pin1: `net.CLP_${p}`, pin2: gate }} />
      <resistor name={`R${p}GS`} resistance="10k" footprint="0805" {...gp()} connections={{ pin1: gate, pin2: ks }} />
      <resistor name={`R${p}PD`} resistance="1M" footprint="1206" {...gp()} connections={{ pin1: gate, pin2: ks }} />
      <diode name={`D${p}Z1`} footprint={Smd2FP()} {...gp()} connections={{ anode: `net.ZK_${p}`, cathode: gate }} />
      <diode name={`D${p}Z2`} footprint={Smd2FP()} {...gp()} connections={{ anode: `net.ZK_${p}`, cathode: ks }} />
      {/* DESAT clamp: BAT64-04 SERIES pair (real config: A=pin1, K=pin2, junction=pin3 NC).
          Anode end on DESAT, cathode end on VCC2 -> clamps DESAT to VCC2+2Vf; never a
          forward path FROM the supply INTO the DESAT node (rev A.4 review fix). */}
      <chip name={`D${p}SB`} footprint={SmdFP(3)} {...gp()} pinLabels={{ pin1: "A", pin2: "K", pin3: "M" }}
        connections={{ A: dst, K: vcc, M: `net.NC_D${p}SBM` }} />
      <resistor name={`R${p}DS`} resistance="100" footprint="0805" {...gp()} connections={{ pin1: dst, pin2: `net.DS1_${p}` }} />
      <diode name={`D${p}S1`} footprint={Smd2FP()} {...gp()} connections={{ anode: `net.DS1_${p}`, cathode: `net.DS2_${p}` }} />
      <diode name={`D${p}S2`} footprint={Smd2FP()} {...gp()} connections={{ anode: `net.DS2_${p}`, cathode: drain }} />
      <capacitor name={`C${p}BL`} capacitance="47pF" footprint="0603" {...gp()} connections={{ pin1: dst, pin2: ks }} />
      {/* driver rail decoupling — both sides of the barrier (NSI6611 DS layout rule):
          input-side VCC1-GND1 bypass lives in each channel, not only at the LDO */}
      <capacitor name={`C${p}IN`} capacitance="100nF" footprint="0603" {...gp()} connections={{ pin1: "net.V5GD", pin2: "net.DGND" }} />
      <capacitor name={`C${p}B1`} capacitance="100nF" footprint="0603" {...gp()} connections={{ pin1: vcc, pin2: ks }} />
      <capacitor name={`C${p}B2`} capacitance="4.7uF" footprint="1206" {...gp()} connections={{ pin1: vcc, pin2: ks }} />
      {/* floating secondary: rectifier -> VCC ; zener splits winding return -> VEE (-5.1) */}
      <diode name={`D${p}R`} footprint={Smd2FP()} {...gp()} connections={{ anode: wA, cathode: vcc }} />
      <diode name={`Z${p}V`} footprint={Smd2FP()} {...gp()} connections={{ anode: vee, cathode: ks }} />
      <capacitor name={`C${p}V1`} capacitance="4.7uF" footprint="1206" {...gp()} connections={{ pin1: vcc, pin2: ks }} />
      <capacitor name={`C${p}E1`} capacitance="10uF" footprint="1206" {...gp()} connections={{ pin1: ks, pin2: vee }} />
      <resistor name={`R${p}BL`} resistance="5.1k" footprint="0805" {...gp()} connections={{ pin1: vcc, pin2: vee }} />
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
      {/* BUK7Y14-80E LFPAK56 real allocation: source 1/2/3, gate 4, drain = mounting base.
          Standard-level gate (+/-20 V abs) — the BUK9Y logic-level sibling is +/-10 V DC,
          under the 11.8 V VDD drive (rev A.4.2). */}
      <chip name={`QF${id}`} footprint={SmdFP(5)} {...gp()} pinLabels={{ pin1: "S1", pin2: "S2", pin3: "S3", pin4: "G", pin5: "D" }}
        connections={{ S1: cs, S2: cs, S3: cs, G: `net.FG_${id}`, D: sw }} />
      <resistor name={`RF${id}G`} resistance="10" footprint="0603" {...gp()} connections={{ pin1: `net.FDR_${id}`, pin2: `net.FG_${id}` }} />
      <resistor name={`RF${id}GO`} resistance="20" footprint="0603" {...gp()} connections={{ pin1: `net.FG_${id}`, pin2: `net.FGO_${id}` }} />
      <diode name={`DF${id}G`} footprint={Smd2FP()} {...gp()} connections={{ anode: `net.FGO_${id}`, cathode: `net.FDR_${id}` }} />
      <diode name={`ZF${id}G`} footprint={Smd2FP()} {...gp()} connections={{ anode: cs, cathode: `net.FG_${id}` }} />
      <resistor name={`RF${id}GS`} resistance="51k" footprint="0603" {...gp()} connections={{ pin1: `net.FG_${id}`, pin2: cs }} />
      <resistor name={`RF${id}CS`} resistance="0.33" footprint="1210" {...gp()} connections={{ pin1: cs, pin2: "net.DGND" }} />
      <resistor name={`RF${id}SI`} resistance="100" footprint="0603" {...gp()} connections={{ pin1: cs, pin2: `net.FSI_${id}` }} />
      <capacitor name={`CF${id}SI`} capacitance="100pF" footprint="0603" {...gp()} connections={{ pin1: `net.FSI_${id}`, pin2: "net.DGND" }} />
      {/* RT/CT: 10k + 680 pF -> ~250 kHz (F32: VGT12EEM Lp is only 10 uH — per-cycle energy
          must stay small; Ipk ~1.4 A DCM vs the 4.5 A CS limit) */}
      <resistor name={`RF${id}RT`} resistance="10k" footprint="0603" {...gp()} connections={{ pin1: vr, pin2: `net.FCT_${id}` }} />
      <capacitor name={`CF${id}CT`} capacitance="680pF" footprint="0603" {...gp()} connections={{ pin1: `net.FCT_${id}`, pin2: "net.DGND" }} />
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
      {/* switch-node RC snubber + leakage clamp ACROSS THE PRIMARY: fast blocking diode into
          a 13 V TVS returned to the rail. Reflected flyback voltage (~7.4 V) stays below the
          TVS standoff, the leakage spike is clamped to rail+~21 V, so the 80 V FET sees
          <=60 V even during a clamped-load-dump input (rev A.4 — replaces the SMBJ85A that
          forward-conducted every OFF interval and had a 94 V breakdown vs the 80 V FET). */}
      <resistor name={`RF${id}SN`} resistance="100" footprint="0805" {...gp()} connections={{ pin1: sw, pin2: `net.FSN_${id}` }} />
      <capacitor name={`CF${id}SN`} capacitance="100pF" footprint="1206" {...gp()} connections={{ pin1: `net.FSN_${id}`, pin2: v12 }} />
      <diode name={`DF${id}SN`} footprint={Smd2FP()} {...gp()} connections={{ anode: sw, cathode: `net.FCL_${id}` }} />
      <diode name={`ZF${id}SN`} footprint={Smd2FP()} {...gp()} connections={{ anode: v12, cathode: `net.FCL_${id}` }} />
      {/* VCC: trickle start from the 12 V rail, then the aux winding takes over
          (the aux-only wiring could never start — startup feed added at DFM review).
          Review A.6 (F18): 2.2 k so VDD_ON (7.5 V max) is reached with the start current AND
          the 71 k feedback divider at a 7.95 V rail (4.7 k needed 8.47 V; a 9 V KL30 leaves
          ~8.0-8.3 V here), and 47 uF on VDD because the UCC28C40 has only 0.4 V of UVLO
          hysteresis — the reservoir must carry ONE start burst to aux-winding takeover (S1:
          22 uF still stalls with every parameter at its worst corner). The UCC28C4x has NO
          internal VDD clamp: with the flyback disabled, 2.2 k would float VDD to ~18 V at a
          24 V jump start and past the 20 V abs max at a clamped load dump — 18 V zener. */}
      <resistor name={`RF${id}ST`} resistance="2.2k" footprint="1206" {...gp()} connections={{ pin1: v12, pin2: vcc }} />
      {/* primary-side regulation via the NF feedback winding (VGT NP:NF:NS = 1:1.6:2.9).
          Round 7 (A6-R06/R07): the divider senses its OWN small aux rectifier (FFS), not VDD.
          On VDD the 2.2 k start feed could hold FB above 2.5 V with the switch stopped (from
          ~15.5 V at the rail for a low-I_q part, certainly at a 24 V jump start) -> no restart,
          gate power lost. FFS is fed only by the winding, so a stopped converter always
          restarts. VCC2 = (V_FFS + Vf_FS)*NS/NF - Vf_sec - Vz: 52.3k/15k -> V_FFS 11.22 V ->
          VCC2 15.4 V nominal, 13.5-16.7 V corners (the 56k/VDD sense gave ~16.9 V once the aux
          diode drop is counted). 100 R/100 nF is the leakage-spike filter — bench knob; CF?FS is
          a soft-termination MLCC (a cracked short would force FB = 0, full duty). */}
      <diode name={`DF${id}A`} footprint={Smd2FP()} {...gp()} connections={{ anode: `net.FAX_${id}`, cathode: vcc }} />
      <capacitor name={`CF${id}A`} capacitance="47uF" footprint="1210" {...gp()} connections={{ pin1: vcc, pin2: "net.DGND" }} />
      <diode name={`DF${id}VZ`} footprint={Smd2FP()} {...gp()} connections={{ anode: "net.DGND", cathode: vcc }} />
      <diode name={`DF${id}FS`} footprint={Smd2FP()} {...gp()} connections={{ anode: `net.FAX_${id}`, cathode: `net.FFD_${id}` }} />
      <resistor name={`RF${id}FS`} resistance="100" footprint="0603" {...gp()} connections={{ pin1: `net.FFD_${id}`, pin2: `net.FFS_${id}` }} />
      <capacitor name={`CF${id}FS`} capacitance="100nF" footprint="0603" {...gp()} connections={{ pin1: `net.FFS_${id}`, pin2: "net.DGND" }} />
      <resistor name={`RF${id}FB1`} resistance="52.3k" footprint="0603" {...gp()} connections={{ pin1: `net.FFS_${id}`, pin2: fb }} />
      <resistor name={`RF${id}FB2`} resistance="15k" footprint="0603" {...gp()} connections={{ pin1: fb, pin2: "net.DGND" }} />
      {/* three transformers, primaries paralleled on the switch node; TF?1 carries the aux.
          VGT12EEM-200S1A4 REAL circuit (TDK DS p.3/9): NP1||NP2 = pins 1-2 with DOTS AT PIN 2;
          NF = pins 3(dot)-4; NS = pins 8(dot)-5; pins 6/7 exist mechanically but are NOT
          connected internally. Primary drive P1(rail)/P2(drain) makes the dotted ends negative
          during ON — so the secondary RECTIFIER MUST HANG ON PIN 8 (dot) to conduct only
          during the OFF interval (flyback). Rev A.4: S-winding use was inverted (pin 5 fed
          the rectifier = forward-mode ~2.9x Vin, over the module's +22 V gate abs max). */}
      {[1, 2, 3].map((k) => (
        <chip key={k} name={`TF${id}${k}`} footprint={XfmrEEFP(8)} {...gp()}
          pinLabels={{ pin1: "P1", pin2: "P2", pin3: "F1", pin4: "F2", pin5: "S2", pin6: "NC1", pin7: "NC2", pin8: "S1" }}
          connections={{
            P1: v12, P2: sw,
            F1: k === 1 ? `net.FAX_${id}` : `net.NC_TF${id}${k}F1`, F2: k === 1 ? "net.DGND" : `net.NC_TF${id}${k}F2`,
            S1: `net.W_${["U", "V", "W"][k - 1]}${id}_A`, S2: `net.VEE_${["U", "V", "W"][k - 1]}${id}`,
            NC1: `net.NC_TF${id}${k}T1`, NC2: `net.NC_TF${id}${k}T2`,
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
      <resistor name={RL} resistance="6.2k" footprint="0603" {...gp()} connections={{ pin1: tap, pin2: "net.DCN" }} />
      <capacitor name={CF} capacitance="1nF" footprint="0603" {...gp()} connections={{ pin1: tap, pin2: "net.DCN" }} />
      {/* AMC1311 REAL pins (TI Table 6-1): 1=VDD1 2=IN 3=SHTDN 4=GND1 / 5=GND2 6=OUTN
          7=OUTP 8=VDD2. SHTDN is active-HIGH with an internal 100k pull-up — it must be
          tied to GND1 or the channel silently shuts down (rev A.4.1: pins 2/3 were swapped,
          grounding both analog inputs). */}
      <chip name={U} footprint={SmdFP(8)} {...gp()}
        pinLabels={{ pin1: "VDD1", pin2: "IN", pin3: "SHTDN", pin4: "GND1", pin5: "GND2", pin6: "VOUTN", pin7: "VOUTP", pin8: "VDD2" }}
        connections={{ VDD1: id === "1" ? "net.V5ISO" : "net.V5ISO2", IN: tap, SHTDN: "net.DCN", GND1: "net.DCN", GND2: "net.AGND", VOUTN: outN, VOUTP: outP, VDD2: "net.V5GD" }} />
    </group>
  );
};

// ---- module NTC route (power board side: series R + clamp + filter to harness) ----------
export const ModNtc = ({ ph, ntcA }: { ph: string; ntcA: string }) => (
  <group>
    <resistor name={`R${ph}TS`} resistance="100" footprint="0603" {...gp()} connections={{ pin1: ntcA, pin2: `net.TMOD_${ph}` }} />
    <capacitor name={`C${ph}TF`} capacitance="2.2nF" footprint="0603" {...gp()} connections={{ pin1: `net.TMOD_${ph}`, pin2: "net.TMOD_RTN" }} />
    <diode name={`D${ph}TP`} footprint={Smd2FP()} {...gp()} connections={{ anode: `net.TMOD_${ph}`, cathode: "net.V5GD" }} />
  </group>
);

// ---- CAN-FD channel (card, x2) — split termination + CMC + ESD (GEN3 values) ------------
export const CanFd = ({ id, tx, rx, stb, canh, canl }: { id: string; tx: string; rx: string; stb: string; canh: string; canl: string }) => (
  <group>
    <chip name={`UCAN${id}`} footprint={SmdFP(8)} {...gp()}
      pinLabels={{ pin1: "TXD", pin2: "GND", pin3: "VCC", pin4: "RXD", pin5: "VIO", pin6: "CANL", pin7: "CANH", pin8: "STB" }}
      connections={{ TXD: tx, GND: "net.DGND", VCC: "net.V5A", RXD: rx, VIO: "net.V5A", CANL: `net.CANL${id}_T`, CANH: `net.CANH${id}_T`, STB: stb }} />
    {/* ACT45B real winding allocation (TDK circuit diagram): winding A = pins 1-4,
        winding B = pins 2-3, no polarity — rev A.4.2: the 1-2/3-4 mapping crossed
        transceiver CANH onto the external CANL net */}
    <chip name={`LCAN${id}`} footprint={XfmrEEFP(4)} {...gp()} pinLabels={{ pin1: "A1", pin2: "B1", pin3: "B2", pin4: "A2" }}
      connections={{ A1: `net.CANH${id}_T`, B1: `net.CANL${id}_T`, B2: canl, A2: canh }} />
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
    {/* Round 12 (R1-F03): the README had claimed an open-wire pull-down since A.4 that was never drawn.
        100 k at the card input: an open OUT wire or an unpowered sensor reads 0 V, outside the HC5FW
        0.2-4.8 V output range (FW-05 validity window); the sensor allows RL >= 10 k. Decay 2.5 -> 0.3 V
        through the 3.3 nF filter in ~0.7 ms. */}
    <resistor name={`R${ph}B0`} resistance="100k" footprint="0603" {...gp()} connections={{ pin1: vout, pin2: "net.AGND" }} />
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
// Round 12 (R2-F27): the BOM bought an 0603 NTC while the symbol drew a 2-pin header. FW-13 calls
// these "board NTCs", so the thermistor is an on-board 0603 part (RTAMB in free card air, RTHS at
// the card's hottest zone, placed at layout) — symbol and BOM now agree.
export const NtcIn = ({ id, out }: { id: string; out: string }) => (
  <group>
    <resistor name={`RT${id}`} resistance="10k" footprint="0603" {...gp()} connections={{ pin1: out, pin2: "net.AGND" }} />
    <resistor name={`RT${id}P`} resistance="10k" footprint="0603" {...gp()} connections={{ pin1: "net.VREF5", pin2: out }} />
    <capacitor name={`CT${id}F`} capacitance="47nF" footprint="0603" {...gp()} connections={{ pin1: out, pin2: "net.AGND" }} />
  </group>
);

// ---- 40-way harness map (single source for both boards) ---------------------------------
// [pin, net] — same nets both sides; power board holds default-OFF pulldowns.
// Round 9 cross-check (R9X-02): the connector family is chosen at layout, and dual-row parts number
// either row by row (Molex style: k faces k+20) or odd/even (box/IDC: 2m-1 faces 2m). The map is
// laid out so the supply pins are safe in BOTH: VBAT_H (19/20) and VBAT_L (39/40) touch only DGND
// or each other; V5GD (pin 1, the FLT/RDY pull-up reference, A8-01) touches only HW_ID, AGND and
// DGND. The ERC checks both numberings. The earlier map put VBAT across from ASC_CMD/QDIS_CMD/FLT/RDY
// in the drawn (row-by-row) footprint. In row-by-row numbering VBAT_H faces VBAT_L: both come from
// VBSW, so a bridge only parallels the two polyfuses — not a signal hazard, but the two branches are
// then no longer separately fused (round 10 note).
export const HARNESS40: [number, string][] = [
  [1, "V5GD"], [2, "HW_ID"], [3, "AGND"], [4, "PWM_UH"], [5, "PWM_UL"], [6, "PWM_VH"],
  [7, "PWM_VL"], [8, "PWM_WH"], [9, "PWM_WL"], [10, "DGND"], [11, "EN_FLYBK_HS"],
  [12, "EN_FLYBK_LS"], [13, "DRV_EN"], [14, "DGND"], [15, "ASC_CMD"], [16, "QDIS_CMD"],
  [17, "DGND"], [18, "DGND"], [19, "VBAT_H"], [20, "VBAT_H"],
  [21, "DGND"], [22, "DGND"], [23, "FLT_HS_N"], [24, "FLT_LS_N"], [25, "RDY_HS"],
  [26, "RDY_LS"], [27, "VDC1_P"], [28, "VDC1_N"], [29, "VDC2_P"], [30, "VDC2_N"],
  [31, "TMOD_U"], [32, "TMOD_V"], [33, "TMOD_W"], [34, "TMOD_RTN"], [35, "HVIL_A"],
  [36, "HVIL_B"], [37, "DGND"], [38, "DGND"], [39, "VBAT_L"], [40, "VBAT_L"],
];
export const Harness = ({ name }: { name: string }) => (
  <chip name={name} footprint={Header(40, 2)} {...gp()}
    pinLabels={Object.fromEntries(HARNESS40.map(([p, n]) => [`pin${p}`, `P${p}_${n}`]))}
    connections={Object.fromEntries(HARNESS40.map(([p, n]) => [`P${p}_${n}`, `net.${n}`]))} />
);
