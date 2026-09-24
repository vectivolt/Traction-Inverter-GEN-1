// discharge.tsx — Traction Inverter DISCHARGE board (XM3 pattern: a small PCB bolted across
// the DC-link capacitor bank / busbar, so the bleeder physically stays with the stored
// energy). Carries the always-on passive bleeder + the commanded active discharge, and takes
// V15 / QDIS_CMD / DGND from the power board over a 4-way header.
// RULE (also in the title block): never energize the inverter without this board fitted.
import { StudFP, AxialFP, TO247_4L, Header, SmdFP, Smd2FP, IsoBias18, gp } from "../packages/cells";

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
    {/* pin order V15, GND, CMD, GND — CMD never next to V15 (round 8 cross-check, matches JDIS) */}
    <chip name="JCTL" footprint={Header(4)} {...gp()}
      pinLabels={{ pin1: "V15", pin2: "GND1", pin3: "CMD", pin4: "GND2" }}
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
    {/* Rev A.12 (barrier closure): TI UCC14141-Q1 bias (reinforced, V_IORM 1414 Vpk) in the single-output
        configuration, 18.0 V (17.4-18.6 V) — IsoBias18 in cells.tsx; Vishay VOW3120 opto (V_IORM
        1414 Vpk). The QA01C-18 / TLP152 pair had no published working voltage for the 850 V link. */}
    <IsoBias18 p="QD" name="PSQD" vout="net.V18Q" />
    {/* VOW3120 SMD-8 by pin: 1 NC · 2 A · 3 C · 4 NC · 5 VEE · 6 NC · 7 VO · 8 VCC (DS p.1); UVLO 11-13.5 V
        rising keeps the discharge switch off until the rail is up — default-OFF. */}
    <chip name="UQD" footprint={SmdFP(8)} {...gp()} pinLabels={{ pin1: "NC1", pin2: "ANO", pin3: "CAT", pin4: "NC4", pin5: "VEE", pin6: "NC6", pin7: "VO", pin8: "VCC" }}
      connections={{ ANO: "net.QDA", CAT: "net.DGND", VEE: "net.DCN", VO: "net.QDVO", VCC: "net.V18Q" }} />
    {/* VOW3120 LED 270 R 1 % from the card's Schmitt-buffer output: 10.8-15.8 mA, inside the DS
        10-16 mA recommended I_F (V_F 1.0-1.6 V; round 8, R7-03 — 470 R from the MCU pin gave 6.4-6.8 mA) */}
    <resistor name="RQDL" resistance="270" footprint="0603" {...gp()} connections={{ pin1: "net.QDIS_CMD", pin2: "net.QDA" }} />
    {/* gate divider (round 9, A8-N02; rail re-bounded A.12): PSQD regulates 17.4-18.6 V. 1.5 k / 10 k (1 %)
        scales that to V_GS 11.6-16.3 V — the low end takes the VOW3120's guaranteed V_OH >= V_CC - 4 V
        (its -100 mA figure; the drop at this ~1.5 mA load is far smaller) — 5.7 V under the +22 V abs
        max, above the +15 V the DS characterizes at. The discharge switch is slow by design —
        1.5 k x 1.2 nF Ciss is ~2 us. */}
    <resistor name="RQDG" resistance="1.5k" footprint="0603" {...gp()} connections={{ pin1: "net.QDVO", pin2: "net.G_QDIS" }} />
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
