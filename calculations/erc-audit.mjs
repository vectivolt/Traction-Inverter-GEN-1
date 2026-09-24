#!/usr/bin/env node
// erc-audit.mjs — structural electrical-rules audit of both netlists, from circuit JSON.
// Beyond the geometric pin-verify (which proves the SHEETS match the netlist), this checks
// the NETLIST matches the DESIGN INTENT: pairing rules, chain topology, polarity, rail
// assignment, single-pin nets, ground-domain separation. Every assertion is a named fact —
// a failure prints what was expected and what the netlist actually says.
// Run (after tsci builds): node calculations/erc-audit.mjs

import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { DB, SKUS } from "./parts-db.mjs";
const ROOT = join(dirname(fileURLToPath(import.meta.url)), "..");

let pass = 0, fail = 0, warn = 0;
const failures = [], warnings = [];
const ok = (cond, name, detail = "") => {
  if (cond) { pass++; }
  else { fail++; failures.push(`${name}${detail ? " — " + detail : ""}`); }
};
const wr = (cond, name, detail = "") => {
  if (cond) { pass++; }
  else { warn++; warnings.push(`${name}${detail ? " — " + detail : ""}`); }
};

function load(board) {
  const j = JSON.parse(readFileSync(join(ROOT, "dist", "boards", board, "circuit.json"), "utf8"));
  const comps = j.filter((e) => e.type === "source_component");
  const ports = j.filter((e) => e.type === "source_port");
  const nets = new Map(j.filter((e) => e.type === "source_net").map((n) => [n.source_net_id, n.name]));
  const traces = j.filter((e) => e.type === "source_trace");
  const parent = new Map();
  const find = (x) => { while (parent.get(x) !== x) { parent.set(x, parent.get(parent.get(x))); x = parent.get(x); } return x; };
  const uni = (a, b) => { a = find(a); b = find(b); if (a !== b) parent.set(a, b); };
  for (const p of ports) parent.set(p.source_port_id, p.source_port_id);
  const groupNet = new Map();
  for (const t of traces) {
    const ps = t.connected_source_port_ids ?? [];
    for (let i = 1; i < ps.length; i++) uni(ps[0], ps[i]);
    for (const nid of t.connected_source_net_ids ?? []) if (ps.length) groupNet.set(find(ps[0]), nets.get(nid));
  }
  for (const [g, n] of [...groupNet]) groupNet.set(find(g), n);
  const compName = new Map(comps.map((c) => [c.source_component_id, c.name]));
  // pin -> net name (named nets only; anonymous junction = symbol "@root")
  const pinNet = new Map();   // "REF.pinName" and "REF.pinNumber" -> net
  const netPins = new Map();  // net/root -> [REF.pin]
  for (const p of ports) {
    const r = find(p.source_port_id);
    const net = groupNet.get(r) ?? `@${r}`;
    const ref = compName.get(p.source_component_id);
    // key by name, number, and every port hint (anode/cathode live only in port_hints)
    for (const key of [`${ref}.${p.name}`, `${ref}.#${p.pin_number}`,
      ...(p.port_hints ?? []).map((h) => `${ref}.${h}`)]) pinNet.set(key, net);
    if (!netPins.has(net)) netPins.set(net, []);
    netPins.get(net).push(`${ref}.${p.name ?? p.pin_number}`);
  }
  const vals = new Map(comps.map((c) => [c.name, Number(c.resistance ?? c.capacitance ?? c.inductance)]));
  return { comps, compName, pinNet, netPins, vals };
}

const PWR = load("power");
const CB = load("capbank");
const DIS = load("discharge");
const CARD = load("control-card");
const P = (k) => PWR.pinNet.get(k);
const B = (k) => CB.pinNet.get(k);
const D = (k) => DIS.pinNet.get(k);
const C = (k) => CARD.pinNet.get(k);
const same = (a, b) => a !== undefined && a === b;
const V = (DD, ref) => DD.vals.get(ref);
const near = (x, y) => Number.isFinite(x) && Math.abs(x - y) <= 1e-3 * Math.abs(y);

// ---------- generic: single-pin named nets (dead labels) ----------
for (const [b, DD] of [["power", PWR], ["capbank", CB], ["discharge", DIS], ["card", CARD]]) {
  for (const [net, pins] of DD.netPins) {
    if (net.startsWith("@") || net.startsWith("NC_")) continue;
    // de-dup alias ports (anode+pin1 on one physical pin)
    const uniq = new Set(pins.map((p) => p.split(".")[0] + "." + p.split(".")[1]));
    wr(uniq.size >= 2 || pins.length >= 2, `[${b}] net ${net} has a single pin`, pins.join(","));
  }
}

// ---------- power board: per-phase gate-drive structure ----------
for (const x of ["U", "V", "W"]) {
  for (const s of ["H", "L"]) {
    const d = `U${x}${s}G`;
    const inP = s === "H" ? `PWM_${x}H` : `PWM_${x}L`;
    const inN = s === "H" ? `PWM_${x}L` : `PWM_${x}H`;
    ok(same(P(`${d}.INP`), inP), `${d} IN+ on ${inP}`, `got ${P(`${d}.INP`)}`);
    ok(same(P(`${d}.INN`), inN), `${d} IN- on ${inN} (shoot-through lockout)`, `got ${P(`${d}.INN`)}`);
    ok(same(P(`${d}.EN`), "DRV_EN"), `${d} EN on DRV_EN`);
    ok(same(P(`${d}.VCC1`), "V5GD"), `${d} VCC1 on V5GD`);
    ok(same(P(`${d}.GND1`), "DGND"), `${d} GND1 on DGND`);
    ok(same(P(`${d}.GND2`), `KS_${x}${s}`), `${d} Kelvin on KS_${x}${s}`);
    ok(same(P(`${d}.FLT`), `FLT_${s}S_N`), `${d} FLT wired-OR ${s}S`);
    ok(same(P(`${d}.RDY`), `RDY_${s}S`), `${d} RDY ganged ${s}S`);
    // gate node: OUTH -> RON -> G ; OUTL -> ROFF -> G ; CLAMP -> RMC -> G
    ok(same(P(`R${x}${s}ON.pin1`), P(`${d}.OUTH`)) && same(P(`R${x}${s}ON.pin2`), `G_${x}${s}`), `R${x}${s}ON OUTH->gate`);
    ok(same(P(`R${x}${s}OFF.pin1`), P(`${d}.OUTL`)) && same(P(`R${x}${s}OFF.pin2`), `G_${x}${s}`), `R${x}${s}OFF OUTL->gate`);
    ok(same(P(`R${x}${s}MC.pin1`), P(`${d}.CLAMP`)) && same(P(`R${x}${s}MC.pin2`), `G_${x}${s}`), `R${x}${s}MC clamp->gate`);
    // DESAT chain lands on the right drain
    const drain = s === "H" ? `DSH_${x}` : `PH${x}`;   // HS DESAT senses the module aux drain pin
    ok(same(P(`D${x}${s}S2.cathode`), drain), `D${x}${s}S2 cathode on ${drain}`, `got ${P(`D${x}${s}S2.cathode`)}`);
    ok(same(P(`D${x}${s}S1.cathode`), P(`D${x}${s}S2.anode`)), `${x}${s} DESAT diodes in series`);
    ok(same(P(`C${x}${s}BL.pin1`), P(`${d}.DESAT`)), `C${x}${s}BL blanking at DESAT pin`);
    // zener stack: Z1 cathode gate, Z2 cathode Kelvin, anodes common
    ok(same(P(`D${x}${s}Z1.cathode`), `G_${x}${s}`) && same(P(`D${x}${s}Z2.cathode`), `KS_${x}${s}`)
      && same(P(`D${x}${s}Z1.anode`), P(`D${x}${s}Z2.anode`)), `${x}${s} gate zener stack orientation`);
    ok(same(P(`R${x}${s}GS.pin1`), `G_${x}${s}`) && same(P(`R${x}${s}GS.pin2`), `KS_${x}${s}`), `R${x}${s}GS gate bleed`);
    ok(same(P(`R${x}${s}PD.pin1`), `G_${x}${s}`) && same(P(`R${x}${s}PD.pin2`), `KS_${x}${s}`), `R${x}${s}PD HV pulldown`);
    // floating bias: winding A -> rectifier -> VCC; VEE == winding B; zener VEE->KS
    ok(same(P(`D${x}${s}R.anode`), `W_${x}${s}_A`) && same(P(`D${x}${s}R.cathode`), `VCC_${x}${s}`), `${x}${s} bias rectifier`);
    ok(same(P(`Z${x}${s}V.anode`), `VEE_${x}${s}`) && same(P(`Z${x}${s}V.cathode`), `KS_${x}${s}`), `${x}${s} -4.3 V zener split`);
    ok(same(P(`Z${x}${s}V.anode`), `VEE_${x}${s}`), `${x}${s} VEE is the winding return rail`);
    ok(same(P(`${d}.VCC2`), `VCC_${x}${s}`) && same(P(`${d}.VEE2`), `VEE_${x}${s}`), `${d} on its floating rails`);
  }
  // module wiring
  ok(same(P(`MOD${x}.DCP`), "DCP") && same(P(`MOD${x}.DCN`), "DCN") && same(P(`MOD${x}.AC1`), `PH${x}`) && same(P(`MOD${x}.AC2`), `PH${x}`), `MOD${x} power terminals (both AC posts)`);
  ok(same(P(`MOD${x}.DSH`), `DSH_${x}`), `MOD${x} HS drain-sense aux pin`);
  ok(same(P(`MOD${x}.GH`), `G_${x}H`) && same(P(`MOD${x}.GL`), `G_${x}L`), `MOD${x} gate pins`);
  ok(same(P(`MOD${x}.KSH`), `KS_${x}H`) && same(P(`MOD${x}.KSL`), `KS_${x}L`), `MOD${x} Kelvin pins`);
  ok(same(P(`JM${x}.P`), `PH${x}`), `JM${x} phase stud`);
  ok(same(P(`C${x}SN.pin1`), "DCP") && same(P(`C${x}SN.pin2`), "DCN"), `C${x}SN snubber across link`);
}
// LS ASC pins ganged on the buffer; HS ASC pins parked on their own Kelvin
for (const x of ["U", "V", "W"]) {
  ok(same(P(`R${x}LAS.pin1`), "ASC_DRV") && same(P(`U${x}LG.ASC`), P(`R${x}LAS.pin2`)), `U${x}LG ASC fed from ASC_DRV (through its 1 k)`);
  ok(same(P(`U${x}HG.ASC`), `KS_${x}H`), `U${x}HG ASC parked inactive`);
}

// ---------- discharge board (bolt-on) + power-board interface ----------
ok(same(P("JDIS.V15"), "V15") && same(P("JDIS.CMD"), "QDIS_CMD") && same(P("JDIS.GND1"), "DGND"), "power board feeds the discharge board (V15/CMD/GND)");
ok(same(D("JCTL.V15"), "V15") && same(D("JCTL.CMD"), "QDIS_CMD") && same(D("JCTL.GND1"), "DGND"), "discharge board control header");
ok(same(D("JDCP.P"), "DCP") && same(D("JDCN.P"), "DCN"), "discharge board bolt terminals");
ok(same(D("RDIS1.pin1"), "DCP"), "active discharge string starts at DCP");
ok(same(D("QDIS.S"), "DCN") && same(D("QDIS.KS"), "DCN"), "QDIS source/Kelvin on DCN");
ok(same(D("RQDG.pin1"), D("UQD.VO")) && same(D("RQDG.pin2"), D("QDIS.G")), "opto drives QDIS gate");
ok(same(D("RQDPD.pin1"), D("QDIS.G")) && same(D("RQDPD.pin2"), "DCN"), "QDIS gate default-OFF pulldown");
ok(same(D("UQD.GND"), "DCN") && same(D("PSQD.COM"), "DCN"), "discharge bias DCN-referenced");
ok(same(P("ZASC.cathode"), "ASC_DRV") && same(P("ZASC.anode"), "DCN"), "ASC 5.1 V clamp fitted (F28)");
ok(same(P("RASCG.pin2"), "ASC_DRV") && same(P("RASCPD.pin1"), "ASC_DRV") && same(P("RASCPD.pin2"), "DCN"), "ASC drive series + default-OFF pulldown");
for (const st of [0, 1]) {
  ok(same(D(`RBLD${st * 6 + 1}.pin1`), "DCP") && same(D(`RBLD${st * 6 + 6}.pin2`), "DCN"), `bleeder string ${st + 1} spans DCP->DCN`);
  for (let k = 1; k < 6; k++)
    ok(same(D(`RBLD${st * 6 + k}.pin2`), D(`RBLD${st * 6 + k + 1}.pin1`)), `bleeder string ${st + 1} link ${k}`);
}
ok(DIS.comps.filter((c) => /^RBLD\d+$/.test(c.name)).length === 12, "bleeder = 2 strings x 6 (review A.6 F24 voltage margin)");
let nCDC = 0;
for (let k = 1; k <= 16; k++) if (same(B(`CDC${k}.pin1`), "DCP") && same(B(`CDC${k}.pin2`), "DCN")) nCDC++;
ok(nCDC === 16, "cap bank: 16 link cans across DCP/DCN", `${nCDC}`);

// ---------- interboard links: both ends of every board-to-board connection ----------
// L3 busbar: every terminal position on the cap-bank drawing, and its mate on the other sheet
for (const t of ["E", "U", "V", "W", "D"]) {
  ok(same(B(`JCB${t}P.P`), "DCP") && same(B(`JCB${t}N.P`), "DCN"), `busbar terminal pair ${t} on DCP/DCN`);
}
ok(same(P("JHVP.P"), "DCP") && same(P("JHVN.P"), "DCN"), "power-board entry taps mate the busbar entry lugs");
// L2 power<->discharge control link: 4-way, role-for-role including the redundant ground
ok(same(P("JDIS.GND2"), "DGND") && same(D("JCTL.GND2"), "DGND"), "discharge link redundant ground on both ends");
// L4 card<->LEM halls: connector supplies each sensor and returns its output, per phase
for (const x of ["U", "V", "W"]) {
  ok(same(C(`JLEM.S5${x}`), `V5S_${x}`) && same(C(`USNS${x}.VCC`), `V5S_${x}`), `LEM ${x} supply through JLEM`);
  ok(same(C(`JLEM.O${x}`), `HALL_${x}`) && same(C(`USNS${x}.OUT`), `HALL_${x}`), `LEM ${x} signal through JLEM`);
}
ok(same(P("CY1.pin1"), "DCP") && same(P("CY1.pin2"), "PE") && same(P("CY2.pin1"), "DCN") && same(P("CY2.pin2"), "PE"), "Y caps to chassis");

// ---------- iso sensing ----------
for (const [id, U, RT, outP] of [["1", "UIVDC", "RVDD", "VDC1"], ["2", "UIVB", "RVBD", "VDC2"]]) {
  ok(same(P(`${RT}1.pin1`), "DCP"), `divider ${id} top at DCP`);
  ok(same(P(`${RT}L.pin2`), "DCN"), `divider ${id} bottom at DCN`);
  ok(same(P(`${U}.IN`), P(`${RT}L.pin1`)), `AMC ${id} analog input (package pin 2) at the tap`);
  ok(same(P(`${U}.GND1`), "DCN") && same(P(`${U}.SHTDN`), "DCN"), `AMC ${id} HV ground/enable`);
  ok(same(P(`${U}.GND2`), "AGND") && same(P(`${U}.VDD2`), "V5GD"), `AMC ${id} LV side rails`);
  ok(same(P(`${U}.VOUTP`), `${outP}_P`) && same(P(`${U}.VOUTN`), `${outP}_N`), `AMC ${id} outputs to harness`);
}

// ---------- flybacks ----------
for (const id of ["H", "L"]) {
  ok(same(P(`UF${id}.VCC`), P(`DF${id}A.cathode`)), `FLY-${id} VCC fed by aux rectifier`);
  ok(same(P(`RF${id}ST.pin2`), P(`UF${id}.VCC`)), `FLY-${id} trickle-start feeds VCC`);
  ok(same(P(`QF${id}E2.D`), P(`UF${id}.COMP`)), `FLY-${id} enable clamp on COMP`);
  ok(same(P(`QF${id}E1.G`), `EN_FLYBK_${id}S`), `FLY-${id} enable from harness line`);
  for (const [k, x] of [[1, "U"], [2, "V"], [3, "W"]])
    ok(same(P(`TF${id}${k}.S1`), `W_${x}${id}_A`) && same(P(`TF${id}${k}.S2`), `VEE_${x}${id}`), `TF${id}${k} secondary -> phase ${x}${id}`);
  ok(same(P(`TF${id}1.P1`), P(`TF${id}2.P1`)) && same(P(`TF${id}1.P2`), P(`TF${id}2.P2`)), `FLY-${id} primaries paralleled`);
}

// ---------- rev A.4: external-review corrections, locked in ----------
// Flyback transformer phasing (TDK VGT12EEM: NP dot=pin2, NS dot=pin8, NF dot=pin3):
// the secondary rectifier hangs on the DOT end (label S1 = physical pin 8), winding return
// is VEE; aux rectifier on F1 (dot). Wrong-way wiring = forward-mode ~35 V into the gates.
for (const id of ["H", "L"]) {
  for (const [k, ph] of [[1, "U"], [2, "V"], [3, "W"]]) {
    ok(same(P(`TF${id}${k}.S1`), `W_${ph}${id}_A`) && same(P(`TF${id}${k}.S2`), `VEE_${ph}${id}`),
      `TF${id}${k} secondary: rectifier on the dot end, return to VEE`);
    ok((P(`TF${id}${k}.NC1`) ?? "NC").startsWith("NC") && (P(`TF${id}${k}.NC2`) ?? "NC").startsWith("NC"),
      `TF${id}${k} pins 6/7 not connected (no internal winding)`);
  }
  ok(same(P(`TF${id}1.F1`), `FAX_${id}`) && same(P(`TF${id}1.F2`), "DGND"), `TF${id}1 aux winding feeds VCC regulation`);
  // primary clamp: blocking diode from drain into a TVS returned to the rail (never a
  // forward diode straight across drain->rail)
  ok(same(P(`DF${id}SN.anode`), `FSW_${id}`) && same(P(`DF${id}SN.cathode`), `FCL_${id}`), `DF${id}SN blocks into the clamp node`);
  ok(same(P(`ZF${id}SN.cathode`), `FCL_${id}`) && same(P(`ZF${id}SN.anode`), `V12${id}`), `ZF${id}SN TVS returns clamp energy to the rail`);
  // BUK9Y14 LFPAK56: gate on pin 4, source pins commoned to the CS node
  ok(same(P(`QF${id}.G`), `FG_${id}`) && same(P(`QF${id}.S1`), P(`QF${id}.S2`)) && same(P(`QF${id}.D`), `FSW_${id}`), `QF${id} LFPAK56 pin roles`);
}
// DESAT clamp: BAT64-04 series pair, anode end on DESAT, cathode end on VCC2 (clamp only)
for (const ph of ["U", "V", "W"]) for (const sd of ["H", "L"]) {
  const q = `${ph}${sd}`;
  ok(same(P(`D${q}SB.A`), `DST_${q}`) && same(P(`D${q}SB.K`), `VCC_${q}`), `D${q}SB clamps DESAT to VCC2 (not VCC into DESAT)`);
  ok(same(P(`C${q}IN.pin1`), "V5GD") && same(P(`C${q}IN.pin2`), "DGND"), `C${q}IN input-side driver bypass`);
}
// QA01C-class SIP-7 module: +Vo on pin7-label VOP, output common on COM, -Vo unloaded
ok(P("PSASC.VOP") !== undefined && P("PSASC.COM") !== undefined, "PSASC real SIP-7 output pins bound");
// Round 12 (R2-F02/F03): the VDC biases are UCC12050s, one per channel, each behind its own LDO from V15
for (const [k, v] of [["B", "V5ISO"], ["C", "V5ISO2"]]) {
  ok(same(P(`PS5${k}.VISO`), v) && same(P(`PS5${k}.SEL`), v) && same(P(`PS5${k}.GNDS`), "DCN") && same(P(`PS5${k}.GNDS9`), "DCN") && same(P(`PS5${k}.GNDS16`), "DCN"),
    `UCC12050 ${k}: VISO/SEL on ${v} (5.0 V select), all GNDS on DCN`);
  ok(same(P(`PS5${k}.VINP`), `V5S${k}`) && same(P(`PS5${k}.EN`), `V5S${k}`) && same(P(`PS5${k}.GNDP`), "DGND") && same(P(`PS5${k}.SYNC`), "DGND")
    && same(P(`PS5${k}.NC6`), "DGND") && same(P(`PS5${k}.NC10`), "DCN"), `UCC12050 ${k}: VINP/EN on its own 5 V, SYNC low, NC pins to their domains`);
  ok(same(P(`U5L${k}.IN`), "V15") && same(P(`U5L${k}.INH`), "V15") && same(P(`U5L${k}.OUT`), `V5S${k}`) && same(P(`C5L${k}2.pin1`), `V5S${k}`) && same(P(`C5${k}1.pin1`), v),
    `bias LDO ${k} from V15 with its 10 uF, UCC12050 ${k} output 10 uF`);
}
ok(same(D("PSQD.VOP"), "V18Q") && same(D("PSQD.COM"), "DCN"), "PSQD real SIP-7 output pins bound");
// VCC1 LDO alive whenever LV is present (sensing decoupled from gate-power enable)
ok(same(P("UGDL.INH"), "GDL_ON") && same(P("RGDLE.pin1"), "V12L") && same(P("RGDLE.pin2"), "GDL_ON"),
  "V5GD LDO enable is tied on (not slaved to EN_FLYBK_LS)");
ok(same(P("UGDL.OUT"), "V5GD"), "NCV4276C output on pin 5 (fixed version: pin 4 is NC)");
// TPS55340 required programming pins present
ok(same(P("UB15.SS"), "B15SS") && same(P("CB15S.pin1"), "B15SS"), "UB15 soft-start cap fitted");
ok(same(P("UB15.FREQ"), "B15FQ") && same(P("RB15Q.pin1"), "B15FQ"), "UB15 FREQ resistor fitted");

// ---------- rev A.4.1: second-round review corrections ----------
ok(same(P("UB15.SYNC"), "DGND"), "TPS55340 SYNC (pin 5, 7 V abs) grounded — it is NOT a second VIN");
ok(same(P("ULDO15.IN"), "V15B") && same(P("ULDO15.OUT"), "V15") && same(P("DB15.cathode"), "V15B"),
  "V15 loads sit behind the protective LDO (boost pass-through cannot reach the QA01C window)");
ok(same(P("RLD1.pin1"), "V15") && same(P("RLD1.pin2"), "V15VA") && same(P("RLD2.pin2"), "DGND"),
  "ULDO15 VA divider 49.9k/10k -> 15.0 V");
ok(same(C("USBC.TRKIN"), "VPRE"), "FS26 TRKIN supplied from VPRE (it feeds the VREF regulator — never ground while VREF is used)");

// ---------- rev A.4.2: third-round review corrections ----------
// CAN chokes: ACT45B windings are pins 1-4 and 2-3 (TDK circuit diagram, no polarity)
for (const k of ["1", "2"]) {
  ok(same(C(`LCAN${k}.A1`), `CANH${k}_T`) && same(C(`LCAN${k}.A2`), `CANH${k}`),
    `LCAN${k} winding A (pins 1-4) carries CANH end-to-end`);
  ok(same(C(`LCAN${k}.B1`), `CANL${k}_T`) && same(C(`LCAN${k}.B2`), `CANL${k}`),
    `LCAN${k} winding B (pins 2-3) carries CANL end-to-end`);
}
// Resolver amp behind its protective LDO (ALM2402 abs 18 V vs 24 V jump start on VBATC)
ok(same(C("UEXD.VCC"), "VEXD") && same(C("UEXD.VCCO1"), "VEXD") && same(C("UEXD.VCCO2"), "VEXD"),
  "ALM2402 all three supply pins on the protected VEXD rail");
ok(same(C("ULDOEX.IN"), "VBATC") && same(C("ULDOEX.OUT"), "VEXD") && same(C("RLDE1.pin2"), "VEXVA"),
  "ULDOEX chain (VBATC -> 12.1 V VEXD)");
ok(same(C("CLDEC.pin1"), "VEXD") && same(C("CLDEC.pin2"), "VEXVA"), "ULDOEX feed-forward compensation fitted");
// ULDO15 compensation (ADJ + ceramic COUT needs Cb across the top divider leg)
ok(same(P("CLDC.pin1"), "V15") && same(P("CLDC.pin2"), "V15VA"), "ULDO15 feed-forward compensation fitted");

// ---------- rev A.4.3: fourth-round review corrections ----------
ok(same(P("RB15C.pin1"), "B15CO") && same(P("CB15CC.pin1"), "B15CZ") && same(P("CB15CC.pin2"), "DGND"),
  "UB15 COMP has the series R3/C4 pole-zero network (TI 8.2.1.2.11)");
ok(same(P("CB15C.pin1"), "B15CO") && same(P("CB15C.pin2"), "DGND"), "UB15 COMP high-frequency pole cap");
// Card: gate-power feeds are actually sourced (polyfused off the reverse-protected node)
ok(same(C("FVBH.A"), "VBSW") && same(C("FVBH.B"), "VBAT_H"), "VBAT_H sourced on the card, from the switched feed (round 9, N17)");
ok(same(C("FVBL.A"), "VBSW") && same(C("FVBL.B"), "VBAT_L"), "VBAT_L sourced on the card, from the switched feed (round 9, N17)");
// Card: IGN sense is divided + filtered, not a raw diode into the MCU
ok(same(C("DIGN.cathode"), "IGN_D") && same(C("RIGNS1.pin1"), "IGN_D") && same(C("RIGNS1.pin2"), "IGN_SNS")
  && same(C("RIGNS2.pin1"), "IGN_SNS") && same(C("RIGNS2.pin2"), "AGND"), "KL15 sense divided 47k/10k to the ADC pin");
// Card: logic actually powered, real Nexperia/TI pin roles
ok(same(C("UAND1.VCC"), "V5A") && same(C("UAND2.VCC"), "V5A") && same(C("UOR1.VCC"), "V5A") && same(C("UOR2.VCC"), "V5A"),
  "single-gate logic has VCC bound");
// Card: hardware fault latch gates DRV_EN; either bank FLT sets it; MCU clears it
ok(same(C("UAND2.C"), C("USCH.Y2")) && same(C("USCH.A2"), "FLT_OKD") && same(C("RFLTD.pin1"), "FLT_OK") && same(C("RFLTD.pin2"), "FLT_OKD"), "DRV_EN chain includes the fault latch (through the soft-off delay RC and its Schmitt buffer)");
ok(same(C("DFLT1.cathode"), "FLT_HS_N") && same(C("DFLT2.cathode"), "FLT_LS_N") && same(C("DFLT1.anode"), "FLT_CMB_N"),
  "FLT diode-OR into the latch preset");
ok(same(C("ULAT2.PRE_N"), C("USCH2.Y1")) && same(C("USCH2.A1"), "FLT_CMB_N") && same(C("ULAT2.QN"), "FLT_OK") && same(C("ULAT2.CLR_N"), C("USCH.Y1")) && same(C("USCH.A1"), "FLT_CLR_N"),
  "fault latch preset/clear/output roles");
// Card: ASC latch network makes real logic levels (1k series into 10k pull-ups)
ok(same(C("RFS1.pin1"), "FS1B_N") && same(C("RFS2.pin1"), "V5A") && same(C("RFS3.pin1"), "ASC_CLR_M"),
  "ASC latch set/clear network (rev A.4 values)");
// Card: AMC fail-safe discrimination — receiver zero offset from a buffered 0.5 V
ok(same(C("UVOF.OUT"), "VOFS") && same(C("RVD1B.pin2"), "VOFS") && same(C("RVD2B.pin2"), "VOFS"),
  "VDC receivers referenced to the 0.5 V offset");
// Card: ALM2402 output-stage supplies bound, SHDN pulled up through a resistor only
ok(same(C("UEXD.VCCO1"), "VEXD") && same(C("UEXD.VCCO2"), "VEXD"), "ALM2402 VCC_O pins powered (protected VEXD rail since A.4.2)");
ok(same(C("UEXD.SDN"), "EXSD") && same(C("RSDN.pin2"), "EXSD"), "ALM2402 SHDN pulled high via 10k (flag stays readable)");
// Card: FS26 mandatory support pins
ok(same(C("USBC.VDIG"), "VDIG") && same(C("CVDIG.pin1"), "VDIG"), "FS26 VDIG decoupled");
ok(same(C("USBC.VBOS"), "VBOS") && same(C("CVBOS.pin1"), "VBOS"), "FS26 VBOS decoupled");
ok(same(C("USBC.VPRE_BT"), "PREBT") && same(C("CBTP.pin2"), "SWPRE"), "FS26 VPRE bootstrap fitted");
ok(same(C("USBC.CORE_BT"), "CORBT") && same(C("CBTC.pin2"), "SWCORE"), "FS26 VCORE bootstrap fitted");
ok(same(C("USBC.LDOIN"), "VPRE") && same(C("USBC.CORE_IN"), "VPRE"), "FS26 LDO + core buck fed from VPRE");
ok(same(C("USBC.VSUP"), "VBATC") && same(C("USBC.VSUP_PWR"), "VBATC"), "FS26 both supply pins bound");
ok(same(C("USBC.VBST_FB"), "DGND") && same(C("USBC.VBST_ISL"), "DGND") && same(C("USBC.VBST_ISH"), "DGND"),
  "FS26 unused boost front-end terminated per DS");
ok(same(C("RDBG.pin1"), "SBC_DBG") && same(C("USBC.DEBUG"), "SBC_DBG"), "FS26 DEBUG strapped for normal mode");

// ---------- harness equality across boards ----------
// (HARNESS40 lives in cells.tsx which node cannot import — parse it from source instead,
// so this audit always checks against the map the boards were actually built from)
const HARNESS40 = [...readFileSync(join(ROOT, "packages/cells.tsx"), "utf8")
  .match(/\[(\d+), "([A-Z0-9_]+)"\]/g)].map((m) => {
  const t = m.match(/\[(\d+), "([A-Z0-9_]+)"\]/);
  return [Number(t[1]), t[2]];
});
ok(HARNESS40.length === 40, "HARNESS40 map parsed (40 entries)", `${HARNESS40.length}`);
for (const [pin, net] of HARNESS40) {
  ok(same(P(`JIC.P${pin}_${net}`), net), `power harness pin ${pin} on ${net}`);
  ok(same(C(`JICC.P${pin}_${net}`), net), `card harness pin ${pin} on ${net}`);
}

// ---------- ground-domain separation (power board: AGND joins DGND only via the card) ----------
{
  const roots = new Set();
  for (const [net] of PWR.netPins) roots.add(net);
  ok(P("UIVDC.GND2") === "AGND" && P("UASC.CAT") === "DGND", "power board keeps AGND/DGND distinct nets");
}

// ---------- card: safety chain ----------
ok(same(C("UAND1.A"), C("USCH.Y3")) && same(C("USCH.A3"), "FS0B_N") && same(C("UAND1.B"), "MCU_GATE_EN") && same(C("UAND1.C"), "RDY_HS_B"), "AND1 inputs FS0B (buffered)/MCU_EN/RDY_HS (buffered)");
ok(same(C("UAND2.A"), C("UAND1.Y")) && same(C("UAND2.B"), "RDY_LS_B") && same(C("UAND2.Y"), "DRV_EN"), "AND2 chains to DRV_EN (RDY_LS buffered)");
ok(same(C("RGPD.pin1"), "DRV_EN") && same(C("RGPD.pin2"), "DGND"), "DRV_EN default-OFF");
ok(same(C("ULAT.CLK"), "ASC_REQ") && same(C("ULAT.Q"), C("UASCG.A")) && same(C("UASCG.Y"), "ASC_CMD") && same(C("ULAT.PRE_N"), C("USCH2.Y2")) && same(C("USCH2.A2"), "ASC_SET_N") && same(C("ULAT.CLR_N"), "ASC_CLR_N"), "ASC latch wiring (preset buffered, output gated by FLT)");
ok(same(C("RFS1.pin1"), "FS1B_N") && same(C("RFS1.pin2"), "ASC_SET_N"), "FS1B can set ASC (strap)");
ok(same(C("UOR1.A"), "MCU_EN_FLYBK_HS") && same(C("UOR1.B"), "FS_GPIO1") && same(C("UOR1.Y"), "EN_FLYBK_HS"), "OR1 flyback-HS enable");
ok(same(C("UOR2.A"), "MCU_EN_FLYBK_LS") && same(C("UOR2.B"), "FS_GPIO1") && same(C("UOR2.Y"), "EN_FLYBK_LS"), "OR2 flyback-LS enable");
ok(same(C("USBC.FS0B"), "FS0B_N") && same(C("USBC.FS1B"), "FS1B_N") && same(C("USBC.GPIO1"), "FS_GPIO1"), "FS26 safety pins landed");
ok(same(C("USBC.FCCU1"), C("UMCU.PTE15_FCCU0")) && same(C("USBC.FCCU2"), C("UMCU.PTE16_FCCU1")), "FCCU pair MCU<->SBC");
ok(same(C("USBC.RSTB"), C("UMCU.RESET_B")), "SBC resets MCU");

// ---------- card: analog chains ----------
for (const x of ["U", "V", "W"]) {
  ok(same(C(`U${x}B1.INN`), C(`U${x}B1.OUT`)), `hall buffer1 ${x} unity feedback`);
  ok(same(C(`U${x}B2.INN`), C(`U${x}B2.OUT`)), `hall buffer2 ${x} unity feedback`);
  ok(same(C(`R${x}B3.pin2`), `ISNS_${x}`), `hall ${x} lands on ISNS_${x}`);
  ok(same(C(`USNS${x}.OUT`), `HALL_${x}`) && same(C(`USNS${x}.GND`), "AGND") && same(C(`USNS${x}.VCC`), `V5S_${x}`), `hall sensor ${x} wiring`);
}
for (const k of ["1", "2"]) {
  ok(same(C(`UVD${k}.OUT`), `VDC${k}_SE`) && same(C(`RVD${k}D.pin2`), `VDC${k}_SE`), `VDC${k} diff-amp closes on output`);
  ok(same(C(`UMCU.PTA0_VDC1`), "VDC1_SE") || k === "2", `MCU reads VDC1`);
}
ok(same(C("UMCU.PTB0_VDC2"), "VDC2_SE"), "MCU reads VDC2 on a second ADC");
ok(same(C("UEXF.INN"), C("CEXA1.pin2")) && same(C("UEXF.OUT"), "REX_F"), "exciter MFB closes (feedback cap to the inverting input)");
ok(same(C("UEXD.OUT1"), "VREX_P") && same(C("UEXD.OUT2"), "VREX_N"), "resolver H-bridge outputs");
ok(same(C("JVEH.R1"), "VREX_P") && same(C("JVEH.R2"), "VREX_N"), "resolver drive reaches vehicle connector");
for (const s of ["SIN", "COS"])
  ok(same(C(`R${s}R1.pin2`), `${s}_P`) && same(C(`R${s}R2.pin2`), `${s}_N`), `${s} pair reaches SDADC nets`);
ok(same(C("UCAN1.TXD"), "CAN0_TX") && same(C("UCAN1.RXD"), "CAN0_RX"), "CAN1 TX/RX not swapped");
ok(same(C("UCAN2.TXD"), "CAN1_TX") && same(C("UCAN2.RXD"), "CAN1_RX"), "CAN2 TX/RX not swapped");
ok(same(C("RTMR.pin1"), "TMOD_RTN") && same(C("RTMR.pin2"), "AGND"), "module-NTC return star-tied to AGND on card");
ok(same(C("RAGT.pin1"), "AGND") && same(C("RAGT.pin2"), "DGND"), "single-point AGND-DGND tie on card");

// ---------- rev A.6 lock-ins (review A.6 — see docs/review-A6-disposition.md) ----------
for (const x of ["U", "V", "W"]) for (const s of ["H", "L"]) {
  ok(near(V(PWR, `R${x}${s}ON`), 3.3) && near(V(PWR, `R${x}${s}OFF`), 6.8), `R${x}${s}ON 3.3 R (DS point) / OFF 6.8 R (overshoot start value, F01/F40)`);
  ok(near(V(PWR, `C${x}${s}BL`), 47e-12), `C${x}${s}BL 47 pF on the SiC base build`);
}
for (const k of ["H", "L"]) {
  ok(same(P(`RF${k}ST.pin1`), `V12${k}`) && same(P(`RF${k}ST.pin2`), `FVCC_${k}`) && near(V(PWR, `RF${k}ST`), 2.2e3), `flyback ${k} trickle start 2.2 k from V12${k} (F18)`);
  ok(same(P(`CF${k}A.pin1`), `FVCC_${k}`) && near(V(PWR, `CF${k}A`), 47e-6), `flyback ${k} VDD reservoir 47 uF (F18 — 0.4 V UVLO hysteresis)`);
  ok(same(P(`DF${k}VZ.cathode`), `FVCC_${k}`) && same(P(`DF${k}VZ.anode`), "DGND"), `flyback ${k} VDD 18 V clamp (no internal clamp; 2.2 k start path)`);
}
ok(DIS.comps.filter((c) => /^RBLD\d+$/.test(c.name)).every((c) => near(Number(c.resistance), 22e3)), "bleeder parts 22 k (66 k total)");
ok(same(P("RHWID.pin1"), "HW_ID") && same(P("RHWID.pin2"), "DGND"), "SKU identity resistor on the power board (platform)");
ok(same(C("RHWP.pin1"), "VREF5") && same(C("RHWP.pin2"), "HW_ID") && same(C("UMCU.PTB4_HWID"), "HW_ID"), "SKU identity read by the MCU ADC");
ok(same(C("UMCU.PTB1_VOFS"), "VOFS"), "shared VDC receiver offset monitored by the MCU (F11 common cause)");
ok(same(C("JVEH.SHLDR"), "DGND") && same(C("JVEH.SHLDS"), "DGND"), "resolver shields return at the connector ground, not AGND (F35)");
ok(same(C("CFLTD.pin1"), "FLT_OKD") && same(C("CFLTD.pin2"), "DGND") && near(V(CARD, "CFLTD"), 3.3e-9) && near(V(CARD, "RFLTD"), 10e3),
  "global fault drop delayed 10 k/3.3 nF past the driver soft turn-off (F05)");
ok(same(C("UMCU.PTD9_FLTCLR"), "FLT_CLR_M") && same(C("CCLR.pin1"), "FLT_CLR_M") && same(C("CCLR.pin2"), "FLT_CLR_N")
  && same(C("RLAT2.pin2"), "FLT_CLR_N") && same(C("DCLR.anode"), "FLT_CLR_N") && same(C("DCLR.cathode"), "V5A"),
  "fault-latch clear is a hardware one-shot (stuck MCU pin cannot hold the chain permissive — F06)");

// ---------- round-7 lock-ins (reviews RR01–RR10 / A6-R01–R14 + cross-check — see docs/review-A7-disposition.md) ----------
ok(same(C("USCH.VCC"), "V5A") && same(C("USCH.GND"), "DGND"), "Schmitt buffer powered from the logic rail");
// keyed by PIN NUMBER, so a pinLabels swap cannot pass (74LVC3G17 DC: 1=1A 2=3Y 3=2A 5=2Y 6=3A 7=1Y; 74LVC1G74 DC: 3=QN 5=Q 6=RD 7=SD)
ok(same(C("USCH.#1"), "FLT_CLR_N") && same(C("USCH.#7"), C("ULAT2.#6")) && same(C("USCH.#3"), "FLT_OKD") && same(C("USCH.#5"), C("UAND2.C"))
  && same(C("USCH.#6"), "FS0B_N") && same(C("USCH.#2"), C("UAND1.A")) && same(C("USCH.#8"), "V5A") && same(C("USCH.#4"), "DGND"),
  "USCH channels by pin number: one-shot -> ULAT2 RD, delay -> UAND2, FS0B -> UAND1");
ok(same(C("ULAT2.#7"), "FLT_CMB_B") && same(C("ULAT2.#3"), "FLT_OK") && same(C("ULAT.#5"), "ASC_Q") && same(C("ULAT.#7"), "ASC_SET_B") && same(C("ULAT.#6"), "ASC_CLR_N"),
  "fault and ASC latch roles by pin number");
ok(!same(C("ULAT2.CLR_N"), "FLT_CLR_N") && !same(C("UAND2.C"), "FLT_OKD") && !same(C("UAND1.A"), "FS0B_N"),
  "the two RC timing nodes and the 5.1 k FS0B line reach the LVC inputs only through the Schmitt buffer (RR01/RR02)");
ok(same(C("RSCH.pin1"), "FS0B_B") && same(C("RSCH.pin2"), "DGND"), "a dead/unpowered USCH (Hi-Z outputs) forces DRV_EN low");
ok(near(V(CARD, "RENP1"), 5.1e3) && V(CARD, "RENP2") === undefined && near(V(CARD, "RFS1"), 1e3) && near(V(CARD, "RFS2"), 10e3) && near(V(CARD, "RFS4"), 1e3),
  "FS0B pulled up 5.1 k; FS1B pulled up only through the 1 k/10 k strap (A6-R01, A7-N04: no FS1B path into V5A)");
ok(same(C("DFO.anode"), "FAULT_OUT") && same(C("DFO.cathode"), C("RFS4.pin2")) && !same(C("DFO.cathode"), "FAULT_OUT") && same(C("RFS4.pin1"), "FS1B_N")
  && same(C("ZSET.cathode"), "ASC_SET_N") && same(C("ZSET.anode"), "DGND") && !C("DSET.anode"),
  "FAULT_OUT is sink-only (a grounded wire cannot preset ASC); a KL30 short is clamped to ground, not into V5A (A7-N04)");
ok(same(C("ULAT.QN"), "NC_LATQN") && !C("DASC.anode"),
  "ASC does not drive EN: the NSI6611 gives DESAT priority over ASC only with EN high (DS §8.12)");
ok(same(C("RDRB.pin1"), "DRV_EN") && same(C("UMCU.PTD10_DRVENRB"), C("RDRB.pin2")) && same(C("RARB.pin1"), "ASC_CMD") && same(C("UMCU.PTD11_ASCRB"), C("RARB.pin2")),
  "DRV_EN and ASC_CMD read back by the MCU (boot self-test, FW-16)");
ok(same(P("CASCD.pin1"), "ASC_DRV") && same(P("CASCD.pin2"), "DCN") && near(V(PWR, "CASCD"), 12e-9)
  && same(P("DASCR.anode"), "ASC_DRV") && same(P("DASCR.cathode"), "ASCVO") && near(V(PWR, "RASCG"), 2.2e3) && near(V(PWR, "RASCPD"), 10e3),
  "LS ASC delayed 12 nF on the 2.2 k/10 k Thevenin, fast release into the opto (RR05)");
for (const x of ["U", "V", "W"]) {
  ok(same(P(`R${x}LAS.pin1`), "ASC_DRV") && same(P(`U${x}LG.ASC`), P(`R${x}LAS.pin2`)) && !same(P(`U${x}LG.ASC`), "ASC_DRV") && near(V(PWR, `R${x}LAS`), 1e3),
    `U${x}LG ASC pin through its own 1 k from the shared ASC node`);
}
ok(near(V(PWR, "RASCL"), 261), "TLP152 LED driven 10-15 mA (recommended I_F; I_FLH 7.5 mA max)");
for (const k of ["H", "L"]) {
  ok(same(P(`DF${k}FS.anode`), `FAX_${k}`) && same(P(`DF${k}FS.cathode`), P(`RF${k}FS.pin1`)) && !same(P(`RF${k}FS.pin1`), `FVCC_${k}`)
    && same(P(`RF${k}FS.pin2`), `FFS_${k}`) && same(P(`CF${k}FS.pin1`), `FFS_${k}`)
    && same(P(`RF${k}FB1.pin1`), `FFS_${k}`) && !same(P(`RF${k}FB1.pin1`), `FVCC_${k}`) && near(V(PWR, `RF${k}FB1`), 52.3e3) && near(V(PWR, `RF${k}FB2`), 15e3),
    `flyback ${k} FB senses its own aux rectifier (52.3k/15k): the start feed cannot hold FB up (A6-R06/R07)`);
}

// ---------- round-8 lock-ins (reviews R7-01…R7-07 / A7-N01…N05 — see docs/review-A8-disposition.md) ----------
// USCH2 = 74LVC3G17 DC by pin number: 1=1A 2=3Y 3=2A 4=GND 5=2Y 6=3A 7=1Y 8=VCC; UASCG = 74LVC1G08 GW: 1=B 2=A 3=GND 4=Y 5=VCC
ok(same(C("USCH2.#1"), "FLT_CMB_N") && same(C("USCH2.#7"), C("ULAT2.#7")) && same(C("USCH2.#7"), C("UASCG.#1"))
  && same(C("USCH2.#3"), "ASC_SET_N") && same(C("USCH2.#5"), C("ULAT.#7"))
  && same(C("USCH2.#6"), "QDIS_M") && same(C("USCH2.#2"), "QDIS_CMD") && same(C("USCH2.#8"), "V5A") && same(C("USCH2.#4"), "DGND"),
  "USCH2 channels by pin number: FLT diode-OR -> fault-latch preset + ASC gate, FS1B strap -> ASC-latch preset, MCU -> QDIS_CMD");
ok(same(C("UASCG.#2"), C("ULAT.#5")) && same(C("UASCG.#4"), "ASC_CMD") && same(C("UASCG.#5"), "V5A") && same(C("UASCG.#3"), "DGND"),
  "ASC_CMD = ASC latch AND no driver FLT: a latched DESAT masks ASC on every path (R7-01/A7-N01)");
ok(!same(C("ULAT2.PRE_N"), "FLT_CMB_N") && !same(C("ULAT.PRE_N"), "ASC_SET_N") && !same(C("ULAT.Q"), "ASC_CMD"),
  "no open-drain node on a flip-flop asynchronous input; the latch output never drives the opto directly (R7-02)");
ok(same(C("UMCU.PTD5_QDIS"), "QDIS_M") && same(C("RQDM.pin1"), "QDIS_M") && same(C("RQDM.pin2"), "DGND") && !same(C("UMCU.PTD5_QDIS"), "QDIS_CMD"),
  "discharge command buffered (MCU pin sees a CMOS input) and default-off through reset (R7-03)");
ok(near(V(DIS, "RQDL"), 261) && near(V(PWR, "RASCL"), 261), "both TLP152 LEDs driven 10.3-14.8 mA from buffered 5 V logic (R7-03/A7-N03, cross-check R8X-04)");

// ---------- round-8 cross-check lock-ins (R8X-01…17 — docs/review-A8-disposition.md) ----------
// R8X-08: the FLT diode-OR itself (a moved DFLT2 anode passed 854/0), and the A7-N04 back-feed by net, not by name
ok(same(C("DFLT1.anode"), "FLT_CMB_N") && same(C("DFLT1.cathode"), "FLT_HS_N") && same(C("DFLT2.anode"), "FLT_CMB_N") && same(C("DFLT2.cathode"), "FLT_LS_N")
  && same(C("RFLTC.pin1"), "V5GD") && same(C("RFLTC.pin2"), "FLT_CMB_N"),
  "FLT diode-OR: either bank's FLT pulls FLT_CMB_N (latch preset + ASC mask) — R8X-08");
const netsOf = (DD, ref) => new Set([...DD.netPins].filter(([, ps]) => ps.some((q) => q.startsWith(`${ref}.`))).map(([n]) => n));
const bridges = (DD, a, b) => DD.comps.map((c) => c.name).filter((r) => { const n = netsOf(DD, r); return n.size === 2 && n.has(a) && n.has(b); });
ok(bridges(CARD, "FS1B_N", "V5A").length === 0 && bridges(CARD, "ASC_SET_N", "V5A").join() === "RFS2",
  "no part ties FS1B_N to V5A; only the 10 k RFS2 ties ASC_SET_N to it (A7-N04 back-feed, keyed by net)", `${bridges(CARD, "FS1B_N", "V5A")} / ${bridges(CARD, "ASC_SET_N", "V5A")}`);
// dead-state pulls: a Hi-Z (unpowered) USCH2 or ULAT output must read fail-safe (R8X-06 + cross-check)
ok(same(C("RFCB.pin1"), "FLT_CMB_B") && same(C("RFCB.pin2"), "DGND") && same(C("RASCP.pin1"), "ASC_Q") && same(C("RASCP.pin2"), "DGND"),
  "dead USCH2 reads as a latched FLT (SPO, ASC masked); dead ULAT reads no-ASC");
ok(same(P("RPD8.pin1"), "ASC_CMD") && same(P("RPD8.pin2"), "DGND") && same(P("RPD9.pin1"), "QDIS_CMD") && same(P("RPD9.pin2"), "DGND"),
  "ASC_CMD and QDIS_CMD default-off at the power board (the card-side RASCP now holds the latch output)");
ok(same(C("CFLTF.pin1"), "FLT_HS_N") && same(C("CFLTF2.pin1"), "FLT_LS_N") && same(C("CFLTF2.pin2"), "DGND") && near(V(CARD, "CFLTF2"), 100e-12),
  "both FLT lines filtered alike (a glitch latches SPO; no automatic retry) — R8X-12");
// R8X-07: the discharge command never sits next to V15 on the 4-way header (pin numbers, both ends)
ok(same(P("JDIS.#1"), "V15") && same(P("JDIS.#2"), "DGND") && same(P("JDIS.#3"), "QDIS_CMD") && same(P("JDIS.#4"), "DGND")
  && same(D("JCTL.#1"), "V15") && same(D("JCTL.#2"), "DGND") && same(D("JCTL.#3"), "QDIS_CMD") && same(D("JCTL.#4"), "DGND"),
  "discharge header V15-GND-CMD-GND: a pin short cannot put 15 V on USCH2/V5A");
// R8X-08: parts-db resolution, first match per SKU exactly as bom-gen does it (no checker read parts-db before)
for (const [sku, k] of Object.entries(SKUS)) {
  const mpn = (ref) => [...k.rows, ...DB].find((r) => r.m.test(ref))?.mpn ?? "";
  const want = { DFLT1: /BAT46/, DFLT2: /BAT46/, ZSET: /BZT52-B5V6/, UASCG: /74LVC1G08/, USCH: /74LVC3G17/, USCH2: /74LVC3G17/,
    ULAT: /74LVC1G74/, ULAT2: /74LVC1G74/, RASCL: /261R/, RQDL: /261R/, RFCB: /100k/, CFLTF2: /100pF/ };
  const bad = Object.entries(want).filter(([r, rx]) => !rx.test(mpn(r))).map(([r]) => `${r}=${mpn(r) || "none"}`);
  ok(!bad.length, `${sku}: safety parts resolve to the intended MPNs (Schottky OR, ±2 % clamp, AND gate, Schmitt, D flip-flop, 261 R)`, bad.join(", "));
}

// ---------- round-9 lock-ins (A8-01…03, A8-N01…N03 — docs/review-A9-disposition.md) ----------
// A8-01: every pull-up on an NSI6611 FLT/RDY line (and the diode-OR above them) sits on the drivers' VCC1
// rail V5GD (harness pin 1); its 2 x 47 k pull-down doubles as the monitor divider into PTB5 (R9X-01/06)
ok(["RFLTP1", "RFLTP2", "RRDYP1", "RRDYP2", "RFLTC"].every((r) => same(C(`${r}.pin1`), "V5GD"))
  && same(C("RV5GP.pin1"), "V5GD") && same(C("RV5GP.pin2"), "V5GD_SNS") && same(C("RV5GS.pin1"), "V5GD_SNS") && same(C("RV5GS.pin2"), "DGND")
  && near(V(CARD, "RV5GP"), 47e3) && near(V(CARD, "RV5GS"), 47e3) && same(C("UMCU.PTB5_V5GD"), "V5GD_SNS"),
  "FLT/RDY pull-ups on the driver VCC1 rail (abs max = VCC1); an open V5GD pin reads FLT/RDY low; V5GD read by the MCU");
ok(bridges(CARD, "V5GD", "V5A").length === 0, "no part ties V5GD to V5A on the card (keyed by net — R9X-05)");
ok(["FLT_HS_N", "FLT_LS_N", "RDY_HS", "RDY_LS", "FLT_CMB_N"].every((n) => bridges(CARD, n, "V5A").length === 0),
  "no part ties a FLT/RDY node to V5A (keyed by net)");
{
  // R9X-02: the connector family is chosen at layout — check BOTH dual-row numberings: row by row (k faces
  // k+20, as drawn and as Molex-style parts number) and odd/even (2m-1 faces 2m, box/IDC headers)
  const at = (p) => HARNESS40.find(([q]) => q === p)?.[1];
  const rb = (p) => [...[p - 1, p + 1].filter((q) => (p <= 20 ? q >= 1 && q <= 20 : q >= 21 && q <= 40)), p <= 20 ? p + 20 : p - 20];
  const oe = (p) => [...[p - 2, p + 2].filter((q) => q >= 1 && q <= 40), p % 2 ? p + 1 : p - 1];
  const allow = { VBAT_H: ["DGND", "VBAT_H", "VBAT_L"], VBAT_L: ["DGND", "VBAT_L", "VBAT_H"], V5GD: ["DGND", "AGND", "HW_ID"] };
  const bad = HARNESS40.filter(([, n]) => allow[n]).flatMap(([p, n]) => [...rb(p), ...oe(p)].filter((q) => !allow[n].includes(at(q))).map((q) => `${n}@${p}~${at(q)}@${q}`));
  ok(!bad.length, "harness supply pins (VBAT_H/L, V5GD) touch only ground or their own rail in both dual-row numberings", bad.join(", "));
}
// A8-N01/N02: the QA01C-18 (+18 V) feeds both opto drivers; the discharge gate is a 1.5 k / 10 k divider
ok(near(V(DIS, "RQDG"), 1.5e3) && near(V(DIS, "RQDPD"), 10e3) && same(D("RQDG.pin2"), D("QDIS.G")) && same(D("RQDPD.pin2"), "DCN"),
  "QDIS gate divider 1.5 k / 10 k: V_GS 13.3-18.2 V with 1 % resistors from the QA01C-18 envelope, a divider, not a clamp (+22 V abs max)");
// N17 (self-found): the power board's LV feed is switched on the card and follows V5A (FS26 awake)
ok(same(C("QLVS.S"), "NRC") && same(C("QLVS.D"), "VBSW") && same(C("QLVS.TAB"), "VBSW") && same(C("QLVS.G"), "LVS_G")
  && same(C("RLVSG.pin1"), "NRC") && same(C("RLVSG.pin2"), "LVS_G") && same(C("ZLVS.anode"), "LVS_G") && same(C("ZLVS.cathode"), "NRC")
  && same(C("RLVSD.pin1"), "LVS_G") && same(C("QLVN.D"), C("RLVSD.pin2")) && same(C("QLVN.G"), "V5A") && same(C("QLVN.S"), "DGND")
  && same(C("FVBH.A"), "VBSW") && same(C("FVBL.A"), "VBSW") && near(V(CARD, "RLVSG"), 10e3) && near(V(CARD, "RLVSD"), 4.7e3)
  && same(C("RLVSM.pin1"), "LVS_G") && same(C("RLVSM.pin2"), C("CLVSM.pin1")) && same(C("CLVSM.pin2"), "VBSW") && near(V(CARD, "CLVSM"), 100e-9),
  "power-board LV feed switched by V5A (no parking drain); V_GS clamped 15 V; drain-gate slew network (R9X-04)");
// R9X-05: by PIN NUMBER — a gate/source label swap must not pass (DMP6023LEQ SOT-223: 1 G, 2 D, 3 S, tab D; 2N7002: 1 G, 2 S, 3 D)
ok(same(C("QLVS.#1"), "LVS_G") && same(C("QLVS.#2"), "VBSW") && same(C("QLVS.#3"), "NRC") && same(C("QLVS.#4"), "VBSW")
  && same(C("QLVN.#1"), "V5A") && same(C("QLVN.#2"), "DGND") && same(C("QLVN.#3"), "LVS_D"),
  "QLVS/QLVN roles by pin number");
// R9X-03: the resolver-exciter LDO sleeps with the FS26 (its INH on V5A)
ok(same(C("ULDOEX.INH"), "V5A") && same(C("ULDOEX.IN"), "VBATC"), "ULDOEX enabled by V5A, not by KL30 (no LPOFF drain)");
ok(["VBAT_H", "VBAT_L", "VBSW"].every((n) => bridges(CARD, "NRC", n).every((r) => r === "QLVS")),
  "nothing but QLVS bridges the unswitched KL30 node to the power-board feed (keyed by net)");

// ---------- round-12 lock-ins (system review of 4544715 — docs/review-A11-disposition.md) ----------
// R1-F04: the HC5FW is drawn with its real terminals (DS p.2: 1 V_ref out, 2 V_out, 3 Gnd, 4 U_C, E1-E4 Gnd)
for (const x of ["U", "V", "W"]) {
  ok(same(C(`USNS${x}.#2`), `HALL_${x}`) && same(C(`USNS${x}.#3`), "AGND") && same(C(`USNS${x}.#4`), `V5S_${x}`)
    && [5, 6, 7, 8].every((p) => same(C(`USNS${x}.#${p}`), "AGND")) && !/^(V5S|HALL|AGND)/.test(C(`USNS${x}.#1`) ?? "x"),
    `LEM ${x} by terminal number: 2 out, 3/E1-E4 ground, 4 supply, 1 (V_ref) left open`);
  // R1-F03: the open-wire pull-down the README had claimed is drawn — 100 k from the sensor line to AGND
  ok(same(C(`R${x}B0.pin1`), `HALL_${x}`) && same(C(`R${x}B0.pin2`), "AGND") && near(V(CARD, `R${x}B0`), 100e3),
    `hall ${x} signal pull-down 100 k (open wire reads 0 V)`);
}
// R2-F04: exciter is a textbook MFB — input R to the summing node, feedback R output->summing node, C to
// ground at the summing node, R to the inverting input, feedback C output->inverting input; values fixed
ok(same(C("REXA2.pin2"), "NEX3") && same(C("REXA4.pin1"), "REX_F") && same(C("REXA4.pin2"), "NEX3")
  && same(C("CEXA2.pin1"), "NEX3") && same(C("CEXA2.pin2"), "AGND") && same(C("REXA3.pin1"), "NEX3") && same(C("REXA3.pin2"), "NEX4")
  && same(C("CEXA1.pin1"), "REX_F") && same(C("CEXA1.pin2"), "NEX4") && same(C("UEXF.INN"), "NEX4")
  && near(V(CARD, "CEXA2"), 1.5e-9) && near(V(CARD, "CEXA1"), 220e-12) && near(V(CARD, "REXA4"), 24e3) && near(V(CARD, "REXA2"), 10e3) && near(V(CARD, "REXA3"), 10e3),
  "exciter MFB topology (feedback R to the summing node, feedback C to the inverting input) and values 10k/24k/10k, 1.5 nF/220 pF: |H(10 kHz)| 1.85");
// R2-F27: the board NTCs are on-board 0603 parts, biased from VREF5
for (const [id, n] of [["AMB", "NTC_A"], ["HS", "NTC_H"]])
  ok(same(C(`RT${id}.pin1`), n) && same(C(`RT${id}.pin2`), "AGND") && same(C(`RT${id}P.pin1`), "VREF5") && same(C(`RT${id}P.pin2`), n),
    `board NTC RT${id} on-board with its 10 k pull-up`);
ok(bridges(PWR, "V5SB", "V5SC").length === 0 && bridges(PWR, "V5SB", "V5GD").length === 0 && bridges(PWR, "V5SC", "V5GD").length === 0,
  "the two VDC bias 5 V rails are separate from each other and from V5GD (keyed by net)");
// R2-F13: resolver-pin short to KL30 limited to the S32K39's 3 mA injection; R2-F11: VREF5 inside the FS26 window
ok(["RSINF1", "RSINF2", "RCOSF1", "RCOSF2", "RSIN1", "RSIN2", "RCOS1", "RCOS2"].every((r) => near(V(CARD, r), 10e3))
  && ["CSIND", "CCOSD"].every((r) => near(V(CARD, r), 47e-12)) && ["CSINA2", "CCOSA2"].every((r) => near(V(CARD, r), 100e-12))
  && ["CSINF1", "CSINF2", "CCOSF1", "CCOSF2", "CSINA1", "CSINA3", "CCOSA1", "CCOSA3"].every((r) => near(V(CARD, r), 22e-12))
  && same(C("CSINF2.pin1"), "SINF_N") && same(C("CSINA3.pin1"), "SIN_N") && same(C("CCOSF2.pin1"), "COSF_N") && same(C("CCOSA3.pin1"), "COS_N")
  && same(C("CEXA4.pin1"), "SWG1") && near(V(CARD, "CEXA4"), 47e-12),
  "resolver series/bias 10 k (pin injection <= 2.9 mA at 35 V), caps rescaled 47 p / 100 p diff + 22 p common-mode on BOTH legs; SWG 47 pF load");
ok(near(V(CARD, "CSB5"), 1e-6) && near(V(CARD, "CMA1"), 1e-6) && near(V(CARD, "CMA2"), 100e-9), "VREF5 rail 2.1 uF nominal (CSB5 1 uF + CMA1 + CMA2)");
// round-12 parts resolve per SKU: UCC12050 biases + their LDOs, 24 V-stand-off TVS (-VR) on all three LV entries, 0805 CSB5, 100 k hall pull-downs
for (const [sku, k] of Object.entries(SKUS)) {
  const mpn = (ref) => [...k.rows, ...DB].find((r) => r.m.test(ref))?.mpn ?? "";
  const bad = Object.entries({ PS5B: /^UCC12050/, PS5C: /^UCC12050/, U5LB: /^NCV4276C/, U5LC: /^NCV4276C/, C5B1: /10uF-16V-X7R/, C5LB2: /10uF-16V-X7R/, DTVSC: /^TPSMC24CA-VR$/, DTVH: /^TPSMC24CA-VR$/, DTVL: /^TPSMC24CA-VR$/, CSB5: /1uF-16V-X7R-0805/, RUB0: /100k/, UEXD: /^ALM2402QPWPRQ1$/ })
    .filter(([r, rx]) => !rx.test(mpn(r))).map(([r]) => `${r}=${mpn(r) || "none"}`);
  ok(!bad.length, `${sku}: round-12 parts resolve (UCC12050 + LDOs, TPSMC24CA-VR, CSB5 0805, hall pull-downs)`, bad.join(", "));
}
// R1-F05/R2-F26: the ALM2402 is bought as the 14-pin PWP package the symbol draws
{
  const rule = DB.find((r) => r.m.test("UEXD"));
  ok(rule && /^HTSSOP14/.test(rule.fp ?? "") && CARD.comps.find((c) => c.name === "UEXD") && CARD.pinNet.get("UEXD.#14") !== undefined && CARD.pinNet.get("UEXD.#15") === undefined,
    "UEXD BOM package is the 14-pin PWP (symbol has 14 pins, no pin 15)", rule?.fp);
}
for (const [sku, k] of Object.entries(SKUS)) {
  const mpn = (ref) => [...k.rows, ...DB].find((r) => r.m.test(ref))?.mpn ?? "";
  const bad = Object.entries({ QLVS: /^DMP6023/, ZLVS: /BZT52-C15/, PSASC: /^QA01C-18$/, PSQD: /^QA01C-18$/, RQDG: /1k5/, RV5GP: /47k/, RV5GS: /47k/, RFS4: /^ESR03EZPF1001$/, RLVSG: /10k/, RLVSD: /4k7/, CLVSM: /100nF-50V/, CASC: /50V/, CQD: /50V/ })
    .filter(([r, rx]) => !rx.test(mpn(r))).map(([r]) => `${r}=${mpn(r) || "none"}`);
  ok(!bad.length, `${sku}: round-9 parts resolve (QA01C-18 bias, 1.5 k gate divider, V5GD pull-down, anti-surge RFS4)`, bad.join(", "));
}

// ---------- round-10 lock-ins (schematic rechecks of 7235337 — docs/review-A10-disposition.md) ----------
// A9-S01: RDY reaches the AND gates only through the third Schmitt buffer (74LVC3G17 DC by pin number:
// 1=1A 2=3Y 3=2A 4=GND 5=2Y 6=3A 7=1Y 8=VCC); a dead buffer reads RDY_HS low (RRDB)
ok(same(C("USCH3.#1"), "RDY_HS") && same(C("USCH3.#7"), C("UAND1.#6")) && same(C("USCH3.#3"), "RDY_LS") && same(C("USCH3.#5"), C("UAND2.#3"))
  && same(C("USCH3.#6"), "DGND") && same(C("USCH3.#8"), "V5A") && same(C("USCH3.#4"), "DGND")
  && same(C("RRDB.pin1"), "RDY_HS_B") && same(C("RRDB.pin2"), "DGND") && near(V(CARD, "RRDB"), 100e3),
  "RDY_HS/RDY_LS reach UAND1/UAND2 only through USCH3 (no open-drain edge on an LVC AND input); dead buffer = DRV_EN low");
ok(!same(C("UAND1.#6"), "RDY_HS") && !same(C("UAND2.#3"), "RDY_LS"), "no raw RDY line on an AND input (A9-S01)");
for (const [sku, k] of Object.entries(SKUS)) {
  const mpn = (ref) => [...k.rows, ...DB].find((r) => r.m.test(ref))?.mpn ?? "";
  const bad = Object.entries({ USCH3: /74LVC3G17/, RRDB: /100k/, RASCG: /^ESR03EZPF2201$/ })
    .filter(([r, rx]) => !rx.test(mpn(r))).map(([r]) => `${r}=${mpn(r) || "none"}`);
  ok(!bad.length, `${sku}: round-10 parts resolve (RDY Schmitt buffer, its dead-state pull-down, 0.33 W RASCG)`, bad.join(", "));
}

// ---------- report ----------
console.log(`\nERC AUDIT: ${pass} pass · ${warn} warn · ${fail} fail`);
if (warnings.length) { console.log("\nWARN:"); warnings.slice(0, 30).forEach((w) => console.log("  ~ " + w)); }
if (failures.length) { console.log("\nFAIL:"); failures.forEach((f) => console.log("  ✗ " + f)); }
process.exit(fail ? 1 : 0);
