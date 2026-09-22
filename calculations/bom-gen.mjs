#!/usr/bin/env node
// bom-gen.mjs — BOM generator: reads the built circuit JSON of all three boards, classifies every
// component via parts-db patterns, emits per-board CSVs + a combined costed docs/bom.md.
// UNMATCHED components are listed loudly — the BOM is not done until that list is empty.
// Run (after tsci builds): node calculations/bom-gen.mjs

import { readFileSync, writeFileSync, existsSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { DB, OVERRIDES, SKUS } from "./parts-db.mjs";
import { REV } from "./rev.mjs";
const ROOT = join(dirname(fileURLToPath(import.meta.url)), "..");
// SKU = BOM_VARIANT (sic8 default; "igbt" kept as the old name of igbt8). Output suffix keeps
// the historic file names: bom.md (sic8), bom-igbt.md (igbt8), bom-igbt4.md, bom-sic4.md.
const SKU = { "": "sic8", igbt: "igbt8" }[process.env.BOM_VARIANT ?? ""] ?? process.env.BOM_VARIANT;
if (!SKUS[SKU]) { console.error(`unknown BOM_VARIANT ${SKU} — one of ${Object.keys(SKUS).join(", ")}`); process.exit(1); }
const VARIANT = SKU === "sic8" ? "" : SKU;
const SFX = { sic8: "", igbt8: "-igbt" }[SKU] ?? `-${SKU}`;   // bom.md, bom-igbt.md, bom-igbt4.md, bom-sic4.md
// SKU rows shadow the base DB (first match wins)
const XDB = [...SKUS[SKU].rows.map((v) => ({ mfr: "any", alt: "-", fp: "-", ...v })), ...DB];

const eng = (x, unit) => {
  if (!(x > 0)) return "";
  const p = [[1e6, "M"], [1e3, "k"], [1, ""], [1e-3, "m"], [1e-6, "u"], [1e-9, "n"], [1e-12, "p"]];
  for (const [m, sfx] of p) if (x >= m * 0.9999) return `${Number((x / m).toPrecision(3))}${sfx}${unit}`;
  return `${x}${unit}`;
};
const valueOf0 = (c) => c.ftype === "simple_resistor"
  ? (Number(c.resistance) === 0 ? "0R" : eng(Number(c.resistance), ""))
  : c.ftype === "simple_capacitor" ? eng(Number(c.capacitance), "F")
  : c.ftype === "simple_inductor" ? (Number.isFinite(Number(c.inductance)) ? eng(Number(c.inductance), "H") : String(c.inductance)) : "";
// Value/MPN agreement gate: a class MPN that encodes a value (R0603-10k, MLCC-4.7uF-50V,
// R2512-3R3-2W) must encode the value the BOM line prints. F40/F52 fixes once lived only in
// the netlist while the MPN still ordered the old part — this makes that a build failure.
const num = (s) => { const m = String(s).match(/^([0-9.]+)([pnumkM]?)/); return m ? Number(m[1]) * ({ p: 1e-12, n: 1e-9, u: 1e-6, m: 1e-3, k: 1e3, M: 1e6, "": 1 }[m[2]]) : NaN; };
const mpnValue = (mpn) => {
  let m;
  if ((m = mpn.match(/^R\d{4}-(\d+)([kMR])(\d*)(?:-|$)/))) return Number(`${m[1]}.${m[3] || 0}`) * { k: 1e3, M: 1e6, R: 1 }[m[2]];
  if ((m = mpn.match(/-([0-9.]+)(p|n|u)F(?:-|$)/))) return Number(m[1]) * { p: 1e-12, n: 1e-9, u: 1e-6 }[m[2]];
  return null;
};
const mismatches = [];

const CAT = (mpn, desc) =>
  /SiC|module|MOSFET|NFET/i.test(desc) ? "power semiconductors"
    : /driver|SBC|MCU|transceiver|amplifier|op-amp|watchdog|latch|OR gate|AND|LDO|boost|flyback controller|buffer/i.test(desc) ? "drive + control ICs"
    : /iso |isolated|reinforced/i.test(desc) ? "isolation"
    : /transformer|choke|bead|inductor/i.test(desc) ? "magnetics"
    : /film|uF|nF|pF|MLCC/i.test(desc) ? "capacitors"
    : /resistor|divider|pull|bleeder|strap|termination|R \d|68 k|560 R/i.test(desc) ? "resistors"
    : /fuse|TVS|clamp|ESD|Schottky|zener|diode/i.test(desc) ? "protection + diodes"
    : /stud|header|connector|harness|hall/i.test(desc) ? "connectors + sensors"
    : "misc";

// Subsystem contribution map (first match wins) — the "where does the money go" view.
const SUBSYS = [
  ["SiC power modules", /^MOD[UVW]$/],
  ["DC-link film caps", /^CDC\d+$/],
  ["Module snubbers", /^C[UVW]SN$/],
  ["Discharge (active+passive)", /^(RBLD\d|RDIS\d|QDIS|UQD|PSQD|RQD|CQD)/],
  ["Gate drivers + networks", /^(U[UVW][HL]G|[RDCZ][UVW][HL])/],
  ["Gate-power flybacks", /^([UQ]F[HL]|[RCD]F[HL]|ZF[HL]|TF[HL])/],
  ["VDC iso sensing + bias", /^(RV[DB]D|CV[DB]DF|UIV|PS5B|C5B)/],
  ["ASC buffer", /^(PSASC|UASC|RASC|CASC)/],
  ["LV power (prot+LDO+boost)", /^(F[HL]1|DR[HL]|DTV[HL]|LF[HL]1|CLV[HL]|UGDL|C5G|UB15|LB15|DB15|CB15|RB15)/],
  ["HV entry/Y-caps/HVIL/studs", /^(JHV|JPE|JM[UVW]|CY[12]|RPET|CPET|JHVIL|RHVL|DTVSH)/],
  ["Harness + pulldowns", /^(JIC$|RPD\d+|JICC|RCPD)/],
  ["Module NTC routing", /^([RC][UVW]T[SF]|D[UVW]TP)/],
  ["MCU + clock + debug", /^(UMCU|Y1|CY[AB]|CMD|CMA|RMRST|JSWD|RBOOT|CRST)/],
  ["FS26 SBC + LV input + wake", /^(USBC|DBAT|LSBC|LCOR|CSB|RSB|RAGT|FLVC|DREVC|DTVSC|LFC|CLVC|RIGN|DIGN|CIGN)/],
  ["Safety chain (EN/ASC/ILK)", /^(UAND|UOR|ULAT|REN|RFS|RGPD|RFLTP|RRDYP|CFLTF|RLAT|RASCP|CLAT|RILK|CILK)/],
  ["Resolver AFE", /^(UEX|UVMB|REX|CEX|RVM|CVM|[RDC](SIN|COS))/],
  ["Hall sensors + AFE", /^(USNS|JLEM|[ULRC][UVW]B\d?|C[UVW]S[12])/],
  ["VDC receivers (card)", /^([RUC]VD[12])/],
  ["CAN-FD x2", /^(UCAN|LCAN|TVSC|RCT|CCT|CCAN)/],
  ["Temps (module/board/motor)", /^(RSN|CSN|JT(HS|AMB)|RT(HS|AMB)|CT(HS|AMB)|FMT|TVSM|UMT|RMT|CMT)/],
  ["Vehicle connector + prot", /^(JVEH|FVS|LVS)/],
];
const subTotal = new Map();

let md = `# Traction Inverter — ${SKUS[SKU].title}, ${SKUS[SKU].bus} bus — BOM (rev ${REV}, generated ${new Date().toISOString().slice(0, 10)})

220 kW pk / 800 V SiC traction inverter — Power board + Cap-bank busbar + bolt-on Discharge board + Control card.
Generated from the built netlists by \`calculations/bom-gen.mjs\`; the sheets, the BOM and the
LCSC fields resolve parts through the same parts-db, so they cannot disagree.
Prices are INR planning figures at ~1k-inverter aggregate (RFQ ±30 %); hiitio module and
LEM sensor prices are quote-gated — figures below are the planning assumptions.
\`CLASS\` = buy to the rating printed on the sheet; \`ALT\` = footprint-compatible second source.
Same PCBs for every SKU — this BOM differs from the others only in the rows listed in \`parts-db.mjs\` SKUS.${SKU}.
All SKUs: [8XX SiC](bom.md) · [8XX IGBT](bom-igbt.md) · [4XX IGBT](bom-igbt4.md) · [4XX SiC](bom-sic4.md) — comparison in [\`variants.md\`](variants.md).

`;
let grand = 0;
const catTotal = new Map();
for (const [board, path] of [["power", "power"], ["capbank", "capbank"], ["discharge", "discharge"], ["control-card", "control-card"]]) {
  const p = join(ROOT, "dist", "boards", path, "circuit.json");
  if (!existsSync(p)) { console.log(`!! missing build: ${board} — run npm run build first`); continue; }
  const j = JSON.parse(readFileSync(p, "utf8"));
  const lines = new Map();
  const unmatched = [];
  for (const c of j.filter((e) => e.type === "source_component")) {
    if (/^NC_/.test(c.name)) continue;
    const rule = (VARIANT ? undefined : OVERRIDES[c.name]) ?? XDB.find((r) => r.m.test(c.name));
    if (!rule) { unmatched.push(c.name); continue; }
    const val = rule.value ?? valueOf0(c);
    const mv = mpnValue(rule.mpn);
    if (mv !== null && val && Math.abs(mv - num(val)) > 0.02 * mv) mismatches.push(`${c.name}: value ${val} vs MPN ${rule.mpn}`);
    const key = `${rule.mpn}|${val}`;
    if (!lines.has(key)) lines.set(key, { rule, val, refs: [] });
    lines.get(key).refs.push(c.name);
  }
  const rows = [...lines.values()].sort((a, b) =>
    (b.rule.price1k * b.refs.length) - (a.rule.price1k * a.refs.length));
  let total = 0;
  const csv = ["Qty,Designators,Value,MPN,Manufacturer,Description,LCSC,Footprint,Price1k_INR,Ext_INR,Alt"];
  for (const { rule, val, refs } of rows) {
    const ext = rule.price1k * refs.length;
    total += ext;
    catTotal.set(CAT(rule.mpn, rule.desc), (catTotal.get(CAT(rule.mpn, rule.desc)) ?? 0) + ext);
    for (const ref of refs) {
      const hit = SUBSYS.find(([, rx]) => rx.test(ref));
      const key = hit ? hit[0] : "misc";
      const e = subTotal.get(key) ?? { v: 0, q: 0 };
      e.v += rule.price1k; e.q += 1;
      subTotal.set(key, e);
    }
    const clean = (s) => `"${String(s ?? "").replace(/"/g, "'")}"`;
    csv.push([refs.length, clean(refs.sort().join(" ")), clean(val), clean(rule.mpn), clean(rule.mfr),
      clean(rule.desc), rule.lcsc ?? "CLASS", clean(rule.fp ?? ""), rule.price1k, ext, clean(rule.alt)].join(","));
  }
  const csvPath = join(ROOT, "docs", `bom-${board}${SFX}.csv`);
  writeFileSync(csvPath, csv.join("\n") + "\n");
  grand += total;
  md += `## ${board} — ${[...lines.values()].reduce((a, l) => a + l.refs.length, 0)} components, ${lines.size} BOM lines, ≈ ₹${Math.round(total).toLocaleString("en-IN")} @1k\n\n`;
  md += `CSV: [\`docs/bom-${board}${SFX}.csv\`](bom-${board}${SFX}.csv). Top cost lines:\n\n`;
  md += `| Qty | MPN | Description | ₹ ext @1k | Alt |\n|---|---|---|---|---|\n`;
  for (const { rule, refs } of rows.slice(0, 12))
    md += `| ${refs.length} | ${rule.mpn} | ${rule.desc.split("(")[0].trim().slice(0, 60)} | ${Math.round(rule.price1k * refs.length).toLocaleString("en-IN")} | ${String(rule.alt).slice(0, 40)} |\n`;
  md += "\n";
  if (unmatched.length) {
    md += `**UNMATCHED (fix parts-db): ${unmatched.join(", ")}**\n\n`;
    process.exitCode = 1;
  }
  console.log(`${board}: ${lines.size} lines · ₹${Math.round(total).toLocaleString("en-IN")} @1k${unmatched.length ? ` · UNMATCHED: ${unmatched.join(",")}` : ""}`);
}
md += `## BOM contribution by subsystem (all boards, ₹ @1k)\n
Where the money actually goes — cumulative share shows the Pareto: the first two rows are
~87 % of the electronics BOM, so those are the only two lines worth an RFQ fight.\n
| Subsystem | ₹ | share | cumulative | parts |\n|---|---|---|---|---|\n`;
{
  let csum = 0;
  for (const [name, { v, q }] of [...subTotal].sort((a, b) => b[1].v - a[1].v)) {
    csum += v;
    md += `| ${name} | ${Math.round(v).toLocaleString("en-IN")} | ${(100 * v / grand).toFixed(1)}% | ${(100 * csum / grand).toFixed(1)}% | ${q} |\n`;
  }
  md += "\n";
}
md += `## Cost by category (all boards, ₹ @1k)\n\n| Category | ₹ | share |\n|---|---|---|\n`;
for (const [cat, v] of [...catTotal].sort((a, b) => b[1] - a[1]))
  md += `| ${cat} | ${Math.round(v).toLocaleString("en-IN")} | ${(100 * v / grand).toFixed(1)}% |\n`;
md += `| **TOTAL (electronics, ex-PCB/mech/busbar/coldplate)** | **${Math.round(grand).toLocaleString("en-IN")}** | 100% |\n\n`;
md += `The three HCS600FH120D3C1 modules dominate (as they should at this power class); every
other line is distributor-standard. Swapping the module vendor swaps one BOM line.\n`;
if (mismatches.length) {
  console.log(`!! VALUE/MPN MISMATCH (${mismatches.length}) — fix parts-db:\n   ${mismatches.join("\n   ")}`);
  process.exitCode = 1;
}
writeFileSync(join(ROOT, "docs", `bom${SFX}.md`), md);
console.log(`→ docs/bom${SFX}.md + per-board CSVs · TOTAL ≈ ₹${Math.round(grand).toLocaleString("en-IN")} @1k`);
