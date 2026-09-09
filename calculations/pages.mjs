#!/usr/bin/env node
// pages.mjs — convert the compiled tscircuit netlists (dist/boards/*/circuit.json) into
// per-page section payloads for kicad5-gen: functional pages, blocks with reading flow,
// every pin carrying its net as signal_name, plus MPN resolution from parts-db.
//
// Ported from DC-Modules easyeda-pages.mjs + apply-gen merged: the Traction cells author
// REAL package pin numbers directly, so no pin-remap pass is needed.
//
// Output: calculations/out/pages/{power,card}-<PAGE>.json
// Run (after tsci builds): node calculations/pages.mjs

import { readFileSync, writeFileSync, mkdirSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { DB } from "./parts-db.mjs";
const ROOT = join(dirname(fileURLToPath(import.meta.url)), "..");
const OUT = join(ROOT, "calculations", "out", "pages");
mkdirSync(OUT, { recursive: true });

// ---- functional pages: page → ordered blocks → designator regexes (first match wins) ----
const PAGES = {
  power: [
    ["DC-INPUT", [
      ["ENTRY", [/^JHV[PN]$/, /^JPE$/]],
      ["EMI-Y", [/^CY[12]$/, /^RPET$/, /^CPET$/]],
      ["HVIL", [/^JHVIL$/, /^RHVL[12]$/, /^DTVSH$/]],
    ], ["ENTRY", "EMI-Y", "HVIL"]],
    ["DC-LINK", [
      ["DISCH-IF", [/^JDIS$/]],
    ], ["DISCH-IF"]],
    ["PHASE-U", [
      ["MODULE-U", [/^MODU$/, /^CUSN$/, /^JMU$/, /^RUTS$/, /^CUTF$/, /^DUTP$/]],
      ["DRIVE-UH", [/^(U|R|C|D|Z)UH/]],
      ["DRIVE-UL", [/^(U|R|C|D|Z)UL/]],
    ], ["MODULE-U", "DRIVE-UH", "DRIVE-UL"]],
    ["PHASE-V", [
      ["MODULE-V", [/^MODV$/, /^CVSN$/, /^JMV$/, /^RVTS$/, /^CVTF$/, /^DVTP$/]],
      ["DRIVE-VH", [/^(U|R|C|D|Z)VH/]],
      ["DRIVE-VL", [/^(U|R|C|D|Z)VL/]],
    ], ["MODULE-V", "DRIVE-VH", "DRIVE-VL"]],
    ["PHASE-W", [
      ["MODULE-W", [/^MODW$/, /^CWSN$/, /^JMW$/, /^RWTS$/, /^CWTF$/, /^DWTP$/]],
      ["DRIVE-WH", [/^(U|R|C|D|Z)WH/]],
      ["DRIVE-WL", [/^(U|R|C|D|Z)WL/]],
    ], ["MODULE-W", "DRIVE-WH", "DRIVE-WL"]],
    ["GATE-POWER", [
      ["FLY-HS", [/^(U|Q)FH/, /^[RCD]FH/, /^ZFH(G|SN)$/, /^TFH[123]$/]],
      ["FLY-LS", [/^(U|Q)FL/, /^[RCD]FL/, /^ZFL(G|SN)$/, /^TFL[123]$/]],
    ], ["FLY-HS", "FLY-LS"]],
    ["HV-SENSING", [
      ["SENSE-VDC", [/^RVDD[1-6]$/, /^RVDDL$/, /^CVDDF$/, /^UIVDC$/]],
      ["SENSE-VDC2", [/^RVBD[1-6]$/, /^RVBDL$/, /^CVBDF$/, /^UIVB$/]],
      ["ISO-BIAS", [/^PS5[BC]$/, /^C5[BC][12]$/]],
      ["ASC-BUFFER", [/^PSASC$/, /^UASC$/, /^RASC(L|G|PD)$/, /^CASC$/, /^ZASC$/]],
    ], ["SENSE-VDC", "SENSE-VDC2", "ISO-BIAS", "ASC-BUFFER"]],
    ["LV-POWER", [
      ["PROT-H", [/^FH1$/, /^DRH$/, /^DTVH$/, /^LFH1$/, /^CLVH[12]$/]],
      ["PROT-L", [/^FL1$/, /^DRL$/, /^DTVL$/, /^LFL1$/, /^CLVL[12]$/]],
      ["VCC1-LDO", [/^UGDL$/, /^RGDLE$/, /^C5G[12]$/]],
      ["BOOST-15V", [/^UB15$/, /^LB15$/, /^DB15$/, /^CB15(I|O1|O2|C|S)$/, /^RB15F[12]$/, /^RB15Q$/, /^ULDO15$/, /^RLD[12]$/, /^CLD(15|C)$/]],
    ], ["PROT-H", "PROT-L", "VCC1-LDO", "BOOST-15V"]],
    ["CONTROL-IF", [
      ["HARNESS", [/^JIC$/, /^RPD\d+$/]],
    ], ["HARNESS"]],
  ],
  capbank: [
    ["CAP-BANK", [
      ["BUS-STUDS", [/^JCB[EUVWD][PN]$/]],
      ["CAN-ARRAY", [/^CDC\d+$/]],
    ], ["BUS-STUDS", "CAN-ARRAY"]],
  ],
  disch: [
    ["DISCHARGE", [
      ["ENTRY", [/^JDC[PN]$/, /^JCTL$/]],
      ["BLEED", [/^RBLD\d+$/]],
      ["ACTIVE", [/^RDIS[1-4]$/, /^QDIS$/, /^UQD$/, /^PSQD$/, /^RQD(G|PD|L)$/, /^CQD$/]],
    ], ["ENTRY", "BLEED", "ACTIVE"]],
  ],
  card: [
    ["CONTROL", [
      ["MCU", [/^UMCU$/, /^Y1$/, /^CY[AB]$/, /^CMD\d+$/, /^CMA[12]$/, /^RMRST$/]],
      ["SWD-BOOT", [/^JSWD$/, /^RBOOT$/, /^CRST$/]],
    ], ["MCU", "SWD-BOOT"]],
    ["SBC", [
      ["LV-INPUT", [/^DREVC$/, /^FLVC$/, /^FVB[HL]$/, /^DTVSC$/, /^LFC$/, /^CLVC[12]$/]],
      ["FS26", [/^USBC$/, /^DBAT$/, /^LSBC$/, /^LCOR$/, /^QBAL$/, /^CSB\d+B?$/, /^RSB\d+$/, /^RAGT$/, /^CVDIG$/, /^CVBOS$/, /^CBT[PC]$/, /^RDBG$/]],
      ["WAKE", [/^RIGN[12]$/, /^RIGNS[12]$/, /^CIGN$/, /^CIGNS$/, /^DIGN$/]],
    ], ["LV-INPUT", "FS26", "WAKE"]],
    ["SAFETY", [
      ["GATE-EN", [/^UAND[12]$/, /^CAND[12]$/, /^RENP[12]$/, /^RGPD$/, /^RFLTP[12]$/, /^CFLTF$/, /^RRDYP[12]$/, /^DFLT[12]$/, /^RFLTC$/, /^ULAT2$/, /^RLAT2$/, /^CLAT2$/]],
      ["ASC-LATCH", [/^ULAT$/, /^RLAT1$/, /^CLAT$/, /^RASCP$/, /^RFS[1-4]$/]],
      ["FLYBK-EN", [/^COR[12]$/, /^UOR[12]$/]],
      ["INTERLOCK", [/^RILK[1-4]$/, /^CILK$/]],
    ], ["GATE-EN", "ASC-LATCH", "FLYBK-EN", "INTERLOCK"]],
    ["VDC-RECEIVE", [
      ["CH-1", [/^UVOF$/, /^ROF[12]$/, /^COF1$/, /^RVD1[A-D]$/, /^UVD1$/, /^CVD1$/]],
      ["CH-2", [/^RVD2[A-D]$/, /^UVD2$/, /^CVD2$/]],
    ], ["CH-1", "CH-2"]],
    ["RESOLVER", [
      ["VMID", [/^RVM[1-4]$/, /^CVM[12]$/, /^UVMB[12]$/]],
      ["EXCITER", [/^REX[ABM]\d$/, /^CEX[AD]\d?$/, /^UEXF$/, /^UEXD$/, /^RSDN$/, /^ULDOEX$/, /^RLDE[12]$/, /^CLDE[C]?$/]],
      ["SIN", [/^RSIN(1|2|F1|F2|R1|R2)$/, /^DSINP$/, /^CSIN[DFA]\d?$/]],
      ["COS", [/^RCOS(1|2|F1|F2|R1|R2)$/, /^DCOSP$/, /^CCOS[DFA]\d?$/]],
    ], ["VMID", "EXCITER", "SIN", "COS"]],
    ["PHASE-SENSE", [
      ["HALL-CONN", [/^JLEM$/, /^USNS[UVW]$/]],
      ["SENSE-IU", [/^LUB$/, /^CUS[12]$/, /^RUB[123]$/, /^CUB[12]$/, /^UUB[12]$/]],
      ["SENSE-IV", [/^LVB$/, /^CVS[12]$/, /^RVB[123]$/, /^CVB[12]$/, /^UVB[12]$/]],
      ["SENSE-IW", [/^LWB$/, /^CWS[12]$/, /^RWB[123]$/, /^CWB[12]$/, /^UWB[12]$/]],
    ], ["HALL-CONN", "SENSE-IU", "SENSE-IV", "SENSE-IW"]],
    ["TEMP", [
      ["MOD-NTC", [/^RSN[UVW]P$/, /^CSN[UVW]F$/, /^RTMR$/]],
      ["BOARD-NTC", [/^JT(HS|AMB)$/, /^RT(HS|AMB)P$/, /^CT(HS|AMB)F$/]],
      ["MOTOR-TEMP", [/^FMT[12]$/, /^TVSM[12]$/, /^UMT[12]$/, /^RMT[12][PS]$/, /^CMT[12]F$/]],
    ], ["MOD-NTC", "BOARD-NTC", "MOTOR-TEMP"]],
    ["COMMS", [
      ["CAN-FD1", [/^UCAN1$/, /^LCAN1$/, /^TVSC1$/, /^RCT1[AB]$/, /^CCT1$/, /^CCAN1$/]],
      ["CAN-FD2", [/^UCAN2$/, /^LCAN2$/, /^TVSC2$/, /^RCT2[AB]$/, /^CCT2$/, /^CCAN2$/]],
    ], ["CAN-FD1", "CAN-FD2"]],
    ["VEHICLE-IF", [
      ["CONNECTOR", [/^JVEH$/, /^FVS\d+$/, /^LVS\d+$/]],
    ], ["CONNECTOR"]],
    ["CARD-IF", [
      ["HARNESS", [/^JICC$/, /^RCPD\d+$/]],
    ], ["HARNESS"]],
  ],
};

// nets drawn as wires (path carries meaning) — everything else crossing blocks becomes a name
const WIRE_NETS = [/^PH[UVW]$/, /^G_/, /^GH_/, /^GL_/, /^KS_/, /^EXC/, /^SDD_/, /^SDC/];
const RAILS = ["V3P3", "V5", "V15", "VBAT", "VBATP", "DGND", "AGND", "PE", "DCP", "DCN", "VPRE", "VCORE", "VDDIO", "VREF"];

const f2 = (x) => JSON.stringify(x);
for (const side of ["power", "capbank", "disch", "card"]) {
  const j = JSON.parse(readFileSync(
    join(ROOT, "dist", "boards", side === "card" ? "control-card" : side === "disch" ? "discharge" : side === "capbank" ? "capbank" : "power", "circuit.json"), "utf8"));
  const comps = j.filter(e => e.type === "source_component");
  const ports = j.filter(e => e.type === "source_port");
  const nets = new Map(j.filter(e => e.type === "source_net").map(n => [n.source_net_id, n.name]));
  const traces = j.filter(e => e.type === "source_trace");
  // port → net: union-find over trace port groups + explicit net ids
  const portNet = new Map();
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
  // Name anonymous local junctions after what they actually join: an IC pin if one touches
  // (UQD_VO), else the shared stem of the series parts (RDIS1+RDIS2 -> RDIS_12). Names are
  // board-wide unique — labels merge BY NAME, a collision would silently short two nets.
  const compName = new Map(comps.map((c) => [c.source_component_id, c.name]));
  const pinCount = new Map();
  for (const p of ports) pinCount.set(p.source_component_id, (pinCount.get(p.source_component_id) ?? 0) + 1);
  const groupPorts = new Map();
  for (const p of ports) {
    const r = find(p.source_port_id);
    if (!groupPorts.has(r)) groupPorts.set(r, []);
    groupPorts.get(r).push(p);
  }
  const used = new Set([...nets.values()].filter(Boolean));
  const clean = (t) => String(t).replace(/[^A-Za-z0-9_]/g, "").slice(0, 22);
  const uniq = (base) => {
    const b = base || "NET";
    let n = b, i = 1;
    while (used.has(n)) n = `${b}_${++i}`;
    used.add(n);
    return n;
  };
  const anonNets = new Set();
  for (const [r, ps] of groupPorts) {
    if (groupNet.has(r) || ps.length < 2) continue;
    const ds = [...new Set(ps.map((p) => compName.get(p.source_component_id)).filter(Boolean))].sort();
    if (!ds.length) continue;
    const icPort = ps.find((p) => (pinCount.get(p.source_component_id) ?? 0) >= 3 && p.name);
    let base;
    if (icPort) base = `${compName.get(icPort.source_component_id)}_${clean(icPort.name)}`;
    else {
      const pref = ds.reduce((a, b) => {
        let i = 0;
        while (i < a.length && i < b.length && a[i] === b[i]) i++;
        return a.slice(0, i);
      });
      base = pref.length >= 3 ? `${pref}_M` : ds.slice(0, 2).join("_");
      // A series chain shares one stem across every tap: name the tap after the two parts it
      // sits between, so RVDD_12 is unmistakably the node joining RVDD1 to RVDD2.
      if (pref.length >= 3 && used.has(clean(base)) && ds.length === 2) {
        const tails = ds.map((d) => d.slice(pref.length)).filter(Boolean);
        if (tails.length === 2) base = `${pref}_${tails.join("")}`;
      }
    }
    const nm = uniq(clean(base));
    groupNet.set(r, nm);
    anonNets.add(nm);
  }
  for (const p of ports) {
    const n = groupNet.get(find(p.source_port_id));
    if (n) portNet.set(p.source_port_id, n);
  }
  // per-component pin lists
  const byComp = new Map();
  for (const p of ports) {
    const arr = byComp.get(p.source_component_id) ?? [];
    arr.push(p); byComp.set(p.source_component_id, arr);
  }
  const eng = (x, unit) => {
    if (!(x > 0)) return "";
    const p = [[1e6, "M"], [1e3, "k"], [1, ""], [1e-3, "m"], [1e-6, "u"], [1e-9, "n"], [1e-12, "p"]];
    for (const [m, s] of p) if (x >= m * 0.9999) return `${Number((x / m).toPrecision(3))}${s}${unit}`;
    return `${x}${unit}`;
  };
  const partInfo = (name, c) => {
    const rule = DB.find(r => r.m.test(name));
    const v = c.ftype === "simple_resistor" ? (Number(c.resistance) === 0 ? "0R" : eng(Number(c.resistance), ""))
      : c.ftype === "simple_capacitor" ? eng(Number(c.capacitance), "F") : (rule?.mpn ?? name);
    return { value: String(v).replace(/[^\x20-\x7E]/g, "") || (rule?.mpn ?? name), mpn: rule?.mpn ?? null };
  };
  // Diode ports must reach the sheet named A/C so the glyph seats the anode semantically.
  const pinName = (c, p) => {
    if (c.ftype === "simple_diode") {
      if (/anode|pin1|pos/i.test(p.name ?? "") || p.pin_number === 1) return "A";
      return "C";
    }
    return p.name ?? String(p.pin_number);
  };
  const seen = new Set();
  const pagesOut = [];
  for (const [page, blocks, flow] of PAGES[side]) {
    const members = [];
    for (const c of comps) {
      if (seen.has(c.name) || /^NC_/.test(c.name)) continue;
      for (const [bname, regexes] of blocks) {
        if (regexes.some(r => r.test(c.name))) {
          const allPins = (byComp.get(c.source_component_id) ?? []).map(p => ({
            pin_number: p.pin_number ?? p.name, name: pinName(c, p),
            signal_name: (portNet.get(p.source_port_id) ?? "").replace(/^NC_.*/, ""),
          }));
          // tscircuit emits duplicate alias ports (anode + pin1) for the same physical pin —
          // keep one per pin_number.
          const dedup = [...new Map(allPins.map((p) => [String(p.pin_number), p])).values()];
          const pins = dedup.filter(p => p.signal_name);
          const nc = dedup.filter(p => !p.signal_name).map(p => p.pin_number);
          members.push({ designator: c.name, block_name: bname, pins, nc_pins: nc, ...partInfo(c.name, c) });
          seen.add(c.name);
          break;
        }
      }
    }
    const perBlock = {};
    for (const m of members) perBlock[m.block_name] = (perBlock[m.block_name] ?? 0) + 1;
    pagesOut.push({ page, flow, count: members.length, perBlock, members });
  }
  const missed = comps.filter(c => !seen.has(c.name) && !/^NC_/.test(c.name)).map(c => c.name);
  // Emit in apply format: blocks in declared flow order.
  for (const p of pagesOut) {
    const byBlock = new Map();
    for (const m of p.members) {
      if (!byBlock.has(m.block_name)) byBlock.set(m.block_name, []);
      byBlock.get(m.block_name).push(m);
    }
    const order = [...new Set([...(p.flow || []), ...byBlock.keys()])].filter((b) => byBlock.has(b));
    const chunks = order.map((b) => byBlock.get(b).map(({ nc_pins, block_name, ...rest }) => rest));
    const nc = {};
    for (const m of p.members) if (m.nc_pins.length) nc[m.designator] = m.nc_pins;
    writeFileSync(join(OUT, `${side}-${p.page}.json`), JSON.stringify({
      page: `${side}-${p.page}`, block_order: order, chunks, nc, total: p.members.length,
    }, null, 1));
    console.log(`${side}/${p.page} (${p.count}): ${Object.entries(p.perBlock).map(([b, n]) => `${b}=${n}`).join(" ")}`);
  }
  console.log(`${side} UNASSIGNED: ${missed.length ? missed.join(",") : "none"}`);
  if (missed.length) process.exitCode = 1;
}
console.log(`→ ${OUT}/`);
