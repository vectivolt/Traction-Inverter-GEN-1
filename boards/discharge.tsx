// discharge.tsx — Traction Inverter DISCHARGE board (XM3 pattern: a small PCB bolted across
// the DC-link capacitor bank / busbar, so the bleeder physically stays with the stored
// energy). Carries the always-on passive bleeder + the commanded active discharge, and takes
// V15 / QDIS_CMD / DGND from the power board over a 4-way header.
// RULE (also in the title block): never energize the inverter without this board fitted.
import { StudFP, AxialFP, TO247_4L, Header, SmdFP, Smd2FP, Sip7FP, gp } from "../packages/cells";

const NO_ROUTE = process.env.TSCI_NO_ROUTE === "1";

export default () => (
  <board routingDisabled={NO_ROUTE} width="120mm" height="80mm" schTraceAutoLabelEnabled schMaxTraceDistance={0}>
    <net name="DCP" isForPower />
    <net name="DCN" isForPower />
    <net name="DGND" isGround />
    <net name="V15" isForPower />

    {/* ---- bolt terminals onto the cap bank / laminated busbar ---- */}
    <chip name="JDCP" footprint={StudFP(6.5, 12)} {...gp()} pinLabels={{ pin1: "P" }} connections={{ P: "net.DCP" }} />
    <chip name="JDCN" footprint={StudFP(6.5, 12)} {...gp()} pinLabels={{ pin1: "P" }} connections={{ P: "net.DCN" }} />
    {/* control from the power board: V15 bias, discharge command, ground */}
    <chip name="JCTL" footprint={Header(4)} {...gp()}
      pinLabels={{ pin1: "V15", pin2: "CMD", pin3: "GND1", pin4: "GND2" }}
      connections={{ V15: "net.V15", CMD: "net.QDIS_CMD", GND1: "net.DGND", GND2: "net.DGND" }} />

    {/* ---- passive bleeder: 2 strings x 6 series 22k 2512 2W = 66k (always on) ----
        Review A.6 (F24): with +/-5 % parts the low-tolerance resistor of a 5 x 27k string
        carried 184 V at 850 V (92 % of a plain 2512's 200 V working); six per string holds
        154 V (77 %). Same 56 s / 65 s (nom / worst) to 60 V. 4XX SKU: 15k parts, same PCB. */}
    {[0, 1].map((st) => [1, 2, 3, 4, 5, 6].map((k) => (
      <resistor key={`${st}-${k}`} name={`RBLD${st * 6 + k}`} resistance="22k" footprint="2512" {...gp()}
        connections={{
          pin1: k === 1 ? "net.DCP" : `net.BL${st}${k - 1}`,
          pin2: k === 6 ? "net.DCN" : `net.BL${st}${k}`,
        }} />
    )))}

    {/* ---- active discharge: default-OFF opto + DCN-referenced bias + 1200V SiC + 4x 470R 10W ---- */}
    {/* QA01C-18 real SIP-7: 1=Vin 2=GND(in) 5=-Vo 6=0V 7=+Vo — +18 V used, -Vo unloaded */}
    <chip name="PSQD" footprint={Sip7FP()} {...gp()} pinLabels={{ pin1: "VIN", pin2: "GND", pin5: "VON", pin6: "COM", pin7: "VOP" }}
      connections={{ VIN: "net.V15", GND: "net.DGND", VON: "net.NC_PSQDN", COM: "net.DCN", VOP: "net.V18Q" }} />
    <chip name="UQD" footprint={SmdFP(6)} {...gp()} pinLabels={{ pin1: "ANO", pin2: "NC2", pin3: "CAT", pin4: "GND", pin5: "VO", pin6: "VCC" }}
      connections={{ ANO: "net.QDA", CAT: "net.DGND", GND: "net.DCN", VO: "net.QDVO", VCC: "net.V18Q" }} />
    <resistor name="RQDL" resistance="470" footprint="0603" {...gp()} connections={{ pin1: "net.QDIS_CMD", pin2: "net.QDA" }} />
    <resistor name="RQDG" resistance="47" footprint="0603" {...gp()} connections={{ pin1: "net.QDVO", pin2: "net.G_QDIS" }} />
    <resistor name="RQDPD" resistance="10k" footprint="0603" {...gp()} connections={{ pin1: "net.G_QDIS", pin2: "net.DCN" }} />
    <capacitor name="CQD" capacitance="100nF" footprint="0603" {...gp()} connections={{ pin1: "net.V18Q", pin2: "net.DCN" }} />
    <chip name="QDIS" footprint={TO247_4L()} {...gp()} pinLabels={{ pin1: "D", pin2: "S", pin3: "KS", pin4: "G" }}
      connections={{ D: "net.QD_D", S: "net.DCN", KS: "net.DCN", G: "net.G_QDIS" }} />
    {[1, 2, 3, 4].map((k) => (
      <resistor key={k} name={`RDIS${k}`} resistance="470" footprint={AxialFP(38)} {...gp()}
        connections={{ pin1: k === 1 ? "net.DCP" : `net.DIS${k - 1}`, pin2: k === 4 ? "net.QD_D" : `net.DIS${k}` }} />
    ))}
  </board>
);
