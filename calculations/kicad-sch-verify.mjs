#!/usr/bin/env node
// kicad-sch-verify.mjs — prove the KiCad 9/10 set (kicad/traction/) with a running KiCad (kicad-cli), independent of
// our own placement-rule verifier:
//  (a) ERC on every board sheet opened as its own project, and on the root hierarchy. FAIL on label_dangling,
//      unconnected_wire_endpoint, pin_not_connected on a pin circuit.json connects, lib_symbol_issues /
//      lib_symbol_mismatch, duplicate_reference, and the other stub/label construction defects listed in FAIL.
//      kicad-cli's ERC does not run the annotation test (two "R1" pass it and merge into one netlist component),
//      so duplicate references are checked here from the sheets and by the netlist component count.
//      JUDGED since round 22 (F210): footprint_link_issues — every F2 is "traction:<name>" in the shipped traction.pretty
//      (a FAIL except on an off-board part); every symbol pin number must have a pad of that number. Every other type is counted and printed (heuristics: power_pin_not_driven — no symbol
//      has a power-output pin; ground_pin_not_ground — a GND-named pin on an isolated-domain return such as DCN).
//  (b) netlist export (kicadxml) per board compared with circuit.json: same components, same net names (KiCad's
//      "/" local-label prefix stripped), same REF.pin membership in every net; no-connect flags only on pins
//      circuit.json leaves open. The root export must hold every component once with no net spanning two boards.
//  (c) the MCU's netlist pins are physical balls, each the manifest ball (calculations/mcu-ballmap.json) of its label.
// Run: node calculations/kicad-sch-verify.mjs [--dir <folder>]      (KICAD_CLI overrides the kicad-cli path)

import { readFileSync, mkdtempSync, cpSync, copyFileSync, rmSync, existsSync, mkdirSync, readdirSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { spawnSync } from "node:child_process";
import { loadCircuit } from "./circuit-net.mjs";
import { DB } from "./parts-db.mjs";
import { isOffBoard, LIB as FP_LIB } from "./footprints.mjs";
const offBoard = (ref) => isOffBoard(DB.find((r) => r.m.test(ref)));

const ROOT = join(dirname(fileURLToPath(import.meta.url)), "..");
const arg = (k) => { const i = process.argv.indexOf(k); return i > 0 ? process.argv[i + 1] : undefined; };
const DIR = resolve(arg("--dir") ?? join(ROOT, "kicad/traction"));
const CLI = process.env.KICAD_CLI ?? "/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli";
const BOARDS = [["traction-power", "power"], ["traction-capbank", "capbank"], ["traction-disch", "discharge"], ["traction-card", "control-card"]];
const MCU = "UMCU";
const FAIL = new Set(["label_dangling", "unconnected_wire_endpoint", "lib_symbol_issues", "lib_symbol_mismatch", "duplicate_reference",
  "wire_dangling", "no_connect_dangling", "no_connect_connected", "multiple_net_names", "label_multiple_wires", "endpoint_off_grid"]);
const IGNORED = new Set();   // round 22 (F210): footprint_link_issues is judged — a FAIL except on an off-board part

if (!existsSync(CLI)) { console.log(`FAIL: kicad-cli not found at ${CLI} (install KiCad 9/10 or set KICAD_CLI)`); process.exit(1); }
for (const f of ["traction.kicad_pro", "traction.kicad_sch", "sym-lib-table", "traction.kicad_sym", ...BOARDS.map(([s]) => `${s}.kicad_sch`)])
  if (!existsSync(join(DIR, f))) { console.log(`FAIL: ${DIR}/${f} missing`); process.exit(1); }

// kicad-cli writes .kicad_prl files next to a project: work on a copy. Each board also gets a project file (a copy
// of traction.kicad_pro) so that, opened on its own, it reads the shipped sym-lib-table like the root does.
const WORK = mkdtempSync(join(tmpdir(), "kicad-sch-verify-"));
cpSync(DIR, WORK, { recursive: true });
for (const [s] of BOARDS) copyFileSync(join(WORK, "traction.kicad_pro"), join(WORK, `${s}.kicad_pro`));
// the footprint library and its table travel with the project (KIPRJMOD = the work copy)
for (const f of ["fp-lib-table"]) { if (!existsSync(join(DIR, f))) { console.log(`FAIL: ${f} missing in ${DIR}`); process.exit(1); } copyFileSync(join(DIR, f), join(WORK, f)); }
mkdirSync(join(WORK, `${FP_LIB}.pretty`), { recursive: true });   // the work copy may already carry it
for (const f of readdirSync(join(DIR, `${FP_LIB}.pretty`))) copyFileSync(join(DIR, `${FP_LIB}.pretty`, f), join(WORK, `${FP_LIB}.pretty`, f));
const cli = (...a) => {
  const r = spawnSync(CLI, a, { cwd: WORK, encoding: "utf8" });
  if (r.status !== 0) { console.log(`FAIL: kicad-cli ${a.join(" ")}\n${r.stdout}${r.stderr}`); process.exit(1); }
  return r.stdout + r.stderr;
};
const version = cli("version").trim();
const fails = [];
const fail = (m) => fails.push(m);

// ---- golden: circuit.json ------------------------------------------------------------------------------------
function golden(board) {
  const { comps, ports } = loadCircuit(board);
  const refs = new Set(comps.map((c) => c.name).filter((n) => !/^NC_/.test(n)));
  const nets = new Map(), anon = new Map(), all = new Set();
  for (const p of ports) {
    if (!refs.has(p.ref)) continue;
    // the MCU is numbered by BGA ball (its label is <ball>_<signal>); every other part by pad number
    const pin = p.ref === MCU ? /^([A-U]\d{1,2})_/.exec(p.name ?? "")?.[1] : String(p.pin_number);
    if (!pin) { fail(`${board}: ${p.ref} port ${p.name} carries no ball ID`); continue; }
    all.add(`${p.ref}.${pin}`);
    if (p.net.startsWith("NC_")) continue;                         // explicit no-connect
    const m = p.net.startsWith("@") ? anon : nets;
    if (!m.has(p.net)) m.set(p.net, new Set());
    m.get(p.net).add(`${p.ref}.${pin}`);
  }
  for (const [k, s] of anon) if (s.size < 2) anon.delete(k);       // a lone port on no net is an open pin
  const connected = new Set([...nets.values(), ...anon.values()].flatMap((s) => [...s]));
  return { refs, nets, anon: [...anon.values()], open: new Set([...all].filter((k) => !connected.has(k))) };
}

// ---- KiCad: ERC report and netlist ---------------------------------------------------------------------------
const ent = (s) => s.replace(/&quot;/g, '"').replace(/&apos;/g, "'").replace(/&lt;/g, "<").replace(/&gt;/g, ">").replace(/&amp;/g, "&");
const attr = (tag, k) => { const m = tag.match(new RegExp(`\\s${k}="([^"]*)"`)); return m ? ent(m[1]) : undefined; };
function netlist(file) {
  const warn = cli("sch", "export", "netlist", "--format", "kicadxml", "-o", `${file}.xml`, `${file}.kicad_sch`);
  const x = readFileSync(join(WORK, `${file}.xml`), "utf8");
  const comps = new Map([...x.matchAll(/<comp ref="([^"]*)">([\s\S]*?)<\/comp>/g)].map((m) => [ent(m[1]),
    { part: attr(m[2].match(/<libsource [^>]*>/)?.[0] ?? "", "part"), footprint: ent(m[2].match(/<footprint>([^<]*)<\/footprint>/)?.[1] ?? ""),
      fields: new Set([...m[2].matchAll(/<field name="([^"]*)"/g)].map((f) => f[1])) }]));
  const libpins = new Map([...x.matchAll(/<libpart lib="[^"]*" part="([^"]*)">([\s\S]*?)<\/libpart>/g)].map((m) => [ent(m[1]),
    new Map([...m[2].matchAll(/<pin [^>]*\/>/g)].map((p) => [attr(p[0], "num"), attr(p[0], "name")]))]));
  const nets = [...x.matchAll(/<net code="\d+" name="([^"]*)"[^>]*>([\s\S]*?)<\/net>/g)].map((m) => ({ name: ent(m[1]),
    nodes: [...m[2].matchAll(/<node [^>]*\/>/g)].map((n) => ({ ref: attr(n[0], "ref"), pin: attr(n[0], "pin"), type: attr(n[0], "pintype") })) }));
  return { comps, libpins, nets, warn: warn.includes("annotation errors") };
}
function erc(file, openOk) {
  cli("sch", "erc", "--format", "json", "--severity-all", "-o", `${file}.erc.json`, `${file}.kicad_sch`);
  const rep = JSON.parse(readFileSync(join(WORK, `${file}.erc.json`), "utf8"));
  const bySheet = new Map();
  for (const s of rep.sheets) for (const v of s.violations) {
    const c = bySheet.get(s.path) ?? bySheet.set(s.path, {}).get(s.path);
    c[v.type] = (c[v.type] ?? 0) + 1;
    const where = v.items.map((i) => i.description).join(" / ");
    if (v.type === "pin_not_connected") {
      // allowed only on a pin circuit.json leaves open ("Symbol REF Pin N [...]")
      const m = where.match(/Symbol (\S+) Pin (\S+) \[/);
      if (!m || !openOk(`${m[1]}.${m[2]}`)) fail(`${file} ERC pin_not_connected on a connected pin: ${where}`);
    } else if (v.type === "footprint_link_issues") {
      const ref = where.match(/Symbol (\S+)/)?.[1];
      if (!ref || !offBoard(ref)) fail(`${file} ERC footprint_link_issues: ${v.description} — ${where}`);
    } else if (FAIL.has(v.type)) fail(`${file} ERC ${v.type}: ${v.description} — ${where}`);
  }
  return bySheet;
}
// symbol references straight from a sheet file (a duplicate would merge silently in KiCad's netlist)
const sheetRefs = (file) => [...readFileSync(join(WORK, `${file}.kicad_sch`), "utf8")
  .matchAll(/\(symbol\s+\(lib_id "[^"]*"\)[\s\S]*?\(property "Reference" "((?:[^"\\]|\\.)*)"/g)].map((m) => m[1]);
const same = (a, b) => a.size === b.size && [...a].every((k) => b.has(k));
const fmt = (s) => [...s].sort().join(" ");
const typeCounts = (c) => Object.entries(c).sort().map(([t, n]) => `${t} ${n}`).join(" · ") || "none";

console.log(`kicad-cli ${version} on ${DIR}\n`);
const res = {};
for (const [file, board] of BOARDS) {
  const g = golden(board), x = netlist(file), refs = sheetRefs(file);
  const dup = refs.filter((r, i) => refs.indexOf(r) !== i);
  if (dup.length) fail(`${file}: duplicate references ${[...new Set(dup)].join(",")}`);
  if (x.comps.size !== refs.length) fail(`${file}: ${refs.length} symbols on the sheet but ${x.comps.size} netlist components`);
  if (!g.refs.size || !g.nets.size) fail(`${file}: empty golden netlist for ${board}`);
  // components
  const miss = [...g.refs].filter((r) => !x.comps.has(r)), extra = [...x.comps.keys()].filter((r) => !g.refs.has(r));
  if (miss.length || extra.length) fail(`${file}: components missing [${miss.slice(0, 10)}] extra [${extra.slice(0, 10)}]`);
  const bare = [...x.comps].filter(([, c]) => !c.fields.has("MPN") || !c.fields.has("LCSC")).map(([r]) => r);
  if (bare.length) fail(`${file}: components without MPN/LCSC fields [${bare.slice(0, 10)}]`);
  // footprints (round 22, F210): every symbol resolves in traction.pretty and every pin NUMBER has a pad of that number
  let bound = 0; const padOnly = [];
  for (const [ref, c] of x.comps) {
    if (offBoard(ref)) { if (c.footprint) fail(`${file}: ${ref} is off-board but carries footprint ${c.footprint}`); continue; }
    const m = new RegExp(`^${FP_LIB}:(.+)$`).exec(c.footprint);
    if (!m) { fail(`${file}: ${ref} has no ${FP_LIB} footprint (field "${c.footprint}")`); continue; }
    const fp = join(WORK, `${FP_LIB}.pretty`, `${m[1]}.kicad_mod`);
    if (!existsSync(fp)) { fail(`${file}: ${ref} footprint ${m[1]} is not in ${FP_LIB}.pretty`); continue; }
    const pads = new Set([...readFileSync(fp, "utf8").matchAll(/\(pad "([^"]*)"/g)].map((p) => p[1]).filter(Boolean));
    const pins = [...(x.libpins.get(c.part) ?? new Map()).keys()];
    const nopad = pins.filter((p) => !pads.has(p));
    if (nopad.length) fail(`${file}: ${ref} (${m[1]}) symbol pins with no pad of that number: ${nopad.slice(0, 8).join(",")}`);
    const extra = [...pads].filter((p) => !pins.includes(p));
    if (extra.length && ref !== MCU) padOnly.push(`${ref}:${extra.join("/")}`);   // the MCU's open balls are the manifest's 159 (docs/mcu-pin-manifest.md), not listed here
    bound++;
  }
  if (padOnly.length) console.log(`${"".padEnd(18)}pads with no symbol pin (thermal/mechanical, no net): ${padOnly.join(" ")}`);
  // nets
  const byName = new Map();
  let nc = 0, loose = 0, named = 0, junctions = 0;
  for (const n of x.nets) {
    const keys = n.nodes.map((d) => `${d.ref}.${d.pin}`);
    if (n.nodes.every((d) => d.type.endsWith("+no_connect")) || n.name.startsWith("unconnected-(")) {
      const flagged = n.nodes.every((d) => d.type.endsWith("+no_connect"));
      for (const k of keys) {
        flagged ? nc++ : loose++;
        if (!g.open.has(k)) fail(`${file}: ${k} is ${flagged ? "no-connect flagged" : "left unconnected"} but circuit.json connects it`);
      }
    } else if (!n.name.startsWith("/")) fail(`${file}: net ${n.name} {${fmt(new Set(keys))}} is not named by a local label`);
    else byName.set(n.name.slice(1), new Set(keys));
  }
  for (const [name, want] of g.nets) {
    const got = byName.get(name);
    if (!got) { fail(`${file}: net ${name} missing`); continue; }
    byName.delete(name);
    if (same(got, want)) named++;
    else fail(`${file}: net ${name} — only in KiCad {${fmt(new Set([...got].filter((k) => !want.has(k))))}} only in circuit.json {${fmt(new Set([...want].filter((k) => !got.has(k))))}}`);
  }
  for (const want of g.anon) {                                     // unnamed junctions: membership only
    const hit = [...byName].find(([, got]) => same(got, want));
    if (hit) { byName.delete(hit[0]); junctions++; } else fail(`${file}: junction {${fmt(want)}} missing`);
  }
  for (const [name, got] of byName) fail(`${file}: net ${name} {${fmt(got)}} is not in circuit.json`);
  const e = erc(file, (k) => g.open.has(k));
  res[file] = { g, x, refs };
  console.log(`${file.padEnd(17)} ${String(x.comps.size).padStart(3)}/${g.refs.size} components · ${bound} footprints bound · ${named}/${g.nets.size} nets by name + members`
    + `${g.anon.length ? ` · ${junctions}/${g.anon.length} junctions` : ""} · ${nc} no-connect flags · ${loose} unflagged open pins`
    + `${x.warn ? " · kicad-cli: 'annotation errors' (references not ending in a digit)" : ""}`);
  for (const [path, c] of e) console.log(`${"".padEnd(18)}ERC ${path === "/" ? "" : `${path} `}${typeCounts(c)}`);
}

// root: the four boards under one project
{
  const x = netlist("traction");
  const boardOf = new Map(BOARDS.flatMap(([file]) => res[file].refs.map((r) => [r, file])));
  const total = BOARDS.reduce((a, [file]) => a + res[file].x.comps.size, 0);
  const allRefs = BOARDS.flatMap(([file]) => res[file].refs), dup = allRefs.filter((r, i) => allRefs.indexOf(r) !== i);
  if (dup.length) fail(`root: references used twice in the hierarchy ${[...new Set(dup)].slice(0, 10)}`);
  if (x.comps.size !== total || x.comps.size !== boardOf.size) fail(`root: ${x.comps.size} netlist components, ${total} on the four boards`);
  const spans = x.nets.filter((n) => new Set(n.nodes.map((d) => boardOf.get(d.ref))).size !== 1);
  spans.slice(0, 5).forEach((n) => fail(`root: net ${n.name} spans boards`));
  const perBoard = BOARDS.reduce((a, [file]) => a + res[file].x.nets.length, 0);
  if (x.nets.length !== perBoard) fail(`root: ${x.nets.length} nets, the boards export ${perBoard}`);
  const e = erc("traction", (k) => BOARDS.some(([file]) => res[file].g.open.has(k)));
  console.log(`${"traction (root)".padEnd(17)} ${x.comps.size} components · ${x.nets.length} nets · ${spans.length} spanning two boards`);
  for (const [path, c] of e) console.log(`${"".padEnd(18)}ERC ${path} ${typeCounts(c)}`);
}

// (c) MCU pins by physical ball, against the manifest
{
  const man = JSON.parse(readFileSync(join(ROOT, "calculations", "mcu-ballmap.json"), "utf8"));
  const want = new Map(man.balls.filter((r) => r.net).map((r) => [r.label, r.ball]));
  const { x } = res["traction-card"];
  const names = x.libpins.get(x.comps.get(MCU)?.part) ?? new Map();
  const nodes = x.nets.flatMap((n) => n.nodes).filter((d) => d.ref === MCU);
  const bad = nodes.filter((d) => !/^[A-U]\d{1,2}$/.test(d.pin) || want.get(names.get(d.pin)) !== d.pin)
    .map((d) => `${names.get(d.pin)} on pin ${d.pin} (manifest ${want.get(names.get(d.pin))})`);
  if (bad.length || nodes.length !== want.size) fail(`MCU: ${nodes.length} netlist pins vs ${want.size} manifest balls; ${bad.slice(0, 5).join("; ")}`);
  else console.log(`\nMCU ${MCU}: ${nodes.length} netlist pins, each a physical ball equal to its manifest ball`);
}

rmSync(WORK, { recursive: true, force: true });
if (fails.length) { console.log(`\nFAIL (${fails.length}):`); fails.slice(0, 40).forEach((f) => console.log("  " + f)); process.exit(1); }
console.log(`PASS: KiCad ${version} reads all ${BOARDS.length} board sheets + the root; netlists equal circuit.json; every on-board symbol is bound to a ${FP_LIB}.pretty footprint whose pads cover its pin numbers (ERC footprint_link_issues 0 except off-board parts)`);
