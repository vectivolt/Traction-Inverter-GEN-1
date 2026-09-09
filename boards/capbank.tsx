// capbank.tsx — Traction Inverter CAP BANK assembly (laminated busbar + can array — NOT an
// FR4 PCB: at 340 Arms / ~200 A ripple the interconnect is two copper plates with an
// insulation film, and the 16 film cans solder/bolt to it). This sheet is the electrical
// drawing handed to the busbar vendor: which cans, how many, and the terminal studs.
// The DISCHARGE board (its own sheet) bolts across THIS assembly's DCP/DCN studs.
import { StudFP, FilmCanFP, gp } from "../packages/cells";

const NO_ROUTE = process.env.TSCI_NO_ROUTE === "1";

export default () => (
  <board routingDisabled={NO_ROUTE} width="200mm" height="150mm" schTraceAutoLabelEnabled schMaxTraceDistance={0}>
    <net name="DCP" isForPower />
    <net name="DCN" isForPower />

    {/* ---- every terminal position on the busbar (this sheet IS the busbar drawing) ----
        entry lugs (M8, HV cable side) · 3x module DC tab pairs (screw onto the EconoDUAL
        DC terminals) · discharge-board studs (M6 — mate of JDCP/JDCN on sheet 3) */}
    <chip name="JCBEP" footprint={StudFP(8.5, 16)} {...gp()} pinLabels={{ pin1: "P" }} connections={{ P: "net.DCP" }} />
    <chip name="JCBEN" footprint={StudFP(8.5, 16)} {...gp()} pinLabels={{ pin1: "P" }} connections={{ P: "net.DCN" }} />
    {["U", "V", "W"].map((x) => (
      <chip key={x} name={`JCB${x}P`} footprint={StudFP(6.5, 12)} {...gp()} pinLabels={{ pin1: "P" }} connections={{ P: "net.DCP" }} />
    ))}
    {["U", "V", "W"].map((x) => (
      <chip key={x} name={`JCB${x}N`} footprint={StudFP(6.5, 12)} {...gp()} pinLabels={{ pin1: "P" }} connections={{ P: "net.DCN" }} />
    ))}
    <chip name="JCBDP" footprint={StudFP(6.5, 12)} {...gp()} pinLabels={{ pin1: "P" }} connections={{ P: "net.DCP" }} />
    <chip name="JCBDN" footprint={StudFP(6.5, 12)} {...gp()} pinLabels={{ pin1: "P" }} connections={{ P: "net.DCN" }} />

    {/* ---- DC LINK: 16x 20uF 1100V film (Faratronic C3D class — LCSC-stocked; DFM rev) ---- */}
    {Array.from({ length: 16 }, (_, i) => i + 1).map((k) => (
      <capacitor key={k} name={`CDC${k}`} capacitance="20uF" footprint={FilmCanFP()} {...gp()}
        connections={{ pin1: "net.DCP", pin2: "net.DCN" }} />
    ))}
  </board>
);
