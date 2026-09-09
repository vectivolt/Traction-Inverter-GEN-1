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
  return { comps, compName, pinNet, netPins };
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
  ok(same(P(`U${x}LG.ASC`), "ASC_DRV"), `U${x}LG ASC on ASC_DRV`);
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
  ok(same(D(`RBLD${st * 5 + 1}.pin1`), "DCP") && same(D(`RBLD${st * 5 + 5}.pin2`), "DCN"), `bleeder string ${st + 1} spans DCP->DCN`);
  for (let k = 1; k < 5; k++)
    ok(same(D(`RBLD${st * 5 + k}.pin2`), D(`RBLD${st * 5 + k + 1}.pin1`)), `bleeder string ${st + 1} link ${k}`);
}
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
// QA01C-class SIP-7 modules: +Vo on pin7-label VOP, output common on COM, -Vo unloaded
for (const [b, u] of [["P", "PSASC"], ["P", "PS5B"], ["P", "PS5C"]]) {
  ok(P(`${u}.VOP`) !== undefined && P(`${u}.COM`) !== undefined, `${u} real SIP-7 output pins bound`);
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
// Card: gate-power feeds are actually sourced (polyfused off the reverse-protected node)
ok(same(C("FVBH.A"), "NRC") && same(C("FVBH.B"), "VBAT_H"), "VBAT_H sourced on the card");
ok(same(C("FVBL.A"), "NRC") && same(C("FVBL.B"), "VBAT_L"), "VBAT_L sourced on the card");
// Card: IGN sense is divided + filtered, not a raw diode into the MCU
ok(same(C("DIGN.cathode"), "IGN_D") && same(C("RIGNS1.pin1"), "IGN_D") && same(C("RIGNS1.pin2"), "IGN_SNS")
  && same(C("RIGNS2.pin1"), "IGN_SNS") && same(C("RIGNS2.pin2"), "AGND"), "KL15 sense divided 47k/10k to the ADC pin");
// Card: logic actually powered, real Nexperia/TI pin roles
ok(same(C("UAND1.VCC"), "V5A") && same(C("UAND2.VCC"), "V5A") && same(C("UOR1.VCC"), "V5A") && same(C("UOR2.VCC"), "V5A"),
  "single-gate logic has VCC bound");
// Card: hardware fault latch gates DRV_EN; either bank FLT sets it; MCU clears it
ok(same(C("UAND2.C"), "FLT_OK"), "DRV_EN chain includes the fault latch");
ok(same(C("DFLT1.cathode"), "FLT_HS_N") && same(C("DFLT2.cathode"), "FLT_LS_N") && same(C("DFLT1.anode"), "FLT_CMB_N"),
  "FLT diode-OR into the latch preset");
ok(same(C("ULAT2.PRE_N"), "FLT_CMB_N") && same(C("ULAT2.QN"), "FLT_OK") && same(C("ULAT2.CLR_N"), "FLT_CLR_N"),
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
ok(same(C("UAND1.A"), "FS0B_N") && same(C("UAND1.B"), "MCU_GATE_EN") && same(C("UAND1.C"), "RDY_HS"), "AND1 inputs FS0B/MCU_EN/RDY_HS");
ok(same(C("UAND2.A"), C("UAND1.Y")) && same(C("UAND2.B"), "RDY_LS") && same(C("UAND2.Y"), "DRV_EN"), "AND2 chains to DRV_EN");
ok(same(C("RGPD.pin1"), "DRV_EN") && same(C("RGPD.pin2"), "DGND"), "DRV_EN default-OFF");
ok(same(C("ULAT.CLK"), "ASC_REQ") && same(C("ULAT.Q"), "ASC_CMD") && same(C("ULAT.PRE_N"), "ASC_SET_N") && same(C("ULAT.CLR_N"), "ASC_CLR_N"), "ASC latch wiring");
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
  ok(same(C(`USNS${x}.OUT`), `HALL_${x}`) && same(C(`USNS${x}.GND`), "AGND"), `hall sensor ${x} wiring`);
}
for (const k of ["1", "2"]) {
  ok(same(C(`UVD${k}.OUT`), `VDC${k}_SE`) && same(C(`RVD${k}D.pin2`), `VDC${k}_SE`), `VDC${k} diff-amp closes on output`);
  ok(same(C(`UMCU.PTA0_VDC1`), "VDC1_SE") || k === "2", `MCU reads VDC1`);
}
ok(same(C("UMCU.PTB0_VDC2"), "VDC2_SE"), "MCU reads VDC2 on a second ADC");
ok(same(C("UEXF.INN"), C("REXA4.pin2")) && same(C("UEXF.OUT"), "REX_F"), "exciter MFB closes");
ok(same(C("UEXD.OUT1"), "VREX_P") && same(C("UEXD.OUT2"), "VREX_N"), "resolver H-bridge outputs");
ok(same(C("JVEH.R1"), "VREX_P") && same(C("JVEH.R2"), "VREX_N"), "resolver drive reaches vehicle connector");
for (const s of ["SIN", "COS"])
  ok(same(C(`R${s}R1.pin2`), `${s}_P`) && same(C(`R${s}R2.pin2`), `${s}_N`), `${s} pair reaches SDADC nets`);
ok(same(C("UCAN1.TXD"), "CAN0_TX") && same(C("UCAN1.RXD"), "CAN0_RX"), "CAN1 TX/RX not swapped");
ok(same(C("UCAN2.TXD"), "CAN1_TX") && same(C("UCAN2.RXD"), "CAN1_RX"), "CAN2 TX/RX not swapped");
ok(same(C("RTMR.pin1"), "TMOD_RTN") && same(C("RTMR.pin2"), "AGND"), "module-NTC return star-tied to AGND on card");
ok(same(C("RAGT.pin1"), "AGND") && same(C("RAGT.pin2"), "DGND"), "single-point AGND-DGND tie on card");

// ---------- report ----------
console.log(`\nERC AUDIT: ${pass} pass · ${warn} warn · ${fail} fail`);
if (warnings.length) { console.log("\nWARN:"); warnings.slice(0, 30).forEach((w) => console.log("  ~ " + w)); }
if (failures.length) { console.log("\nFAIL:"); failures.forEach((f) => console.log("  ✗ " + f)); }
process.exit(fail ? 1 : 0);
