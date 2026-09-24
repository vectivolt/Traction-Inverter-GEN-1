#!/usr/bin/env node
// kicad5-verify.mjs — independently re-derive the netlist from the emitted KiCad 5.1 legacy
// sheets and diff it against design intent (calculations/out/pages payloads). Parses the
// library pin offsets, component placements, wire segments and labels, rebuilds connectivity
// by geometry, and trusts nothing the generator claims. Also checks drawing rules: every
// label on a wire end, no body overlaps, every frame closed.
// Run: node calculations/kicad5-verify.mjs

import { readFileSync, readdirSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const ROOT = join(dirname(fileURLToPath(import.meta.url)), "..");
const SRC = join(ROOT, "calculations/out/pages");
const SCH = join(ROOT, "kicad5/traction");

const libFile = readdirSync(SCH).find((f) => f.endsWith(".lib"));
const libText = readFileSync(join(SCH, libFile), "utf8");
const LIB = new Map();
for (const block of libText.split(/^DEF /m).slice(1)) {
  const name = block.split(/\s+/)[0];
  const pins = [];
  let x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  for (const line of block.split("\n")) {
    if (line.startsWith("X ")) {
      const t = line.split(/\s+/);
      pins.push({ num: t[2], x: +t[3], y: +t[4] });
    } else if (line.startsWith("S ")) {
      const t = line.split(/\s+/).map(Number);
      x0 = Math.min(t[1], t[3]); x1 = Math.max(t[1], t[3]);
      y0 = Math.min(t[2], t[4]); y1 = Math.max(t[2], t[4]);
    }
  }
  LIB.set(name, { pins, box: { x0, y0, x1, y1 } });
}

class DSU {
  constructor() { this.p = new Map(); }
  find(a) { if (!this.p.has(a)) this.p.set(a, a); while (this.p.get(a) !== a) { this.p.set(a, this.p.get(this.p.get(a))); a = this.p.get(a); } return a; }
  union(a, b) { a = this.find(a); b = this.find(b); if (a !== b) this.p.set(a, b); }
}

let totPins = 0, totOk = 0, totWrong = 0, totAbsent = 0, sheets = 0;
const cache = new Map();
let totLabels = 0, floating = 0, overlaps = 0, openFrames = 0;
const problems = [];

const seenPages = [];   // page ids — an assembly has many pages; a page id must appear once
for (const f of readdirSync(SRC).filter((x) => x.endsWith(".json")).sort()) {
  const page = JSON.parse(readFileSync(join(SRC, f), "utf8"));
  const board = `traction-${page.page.split("-")[0]}`;
  seenPages.push(page.page);
  if (!cache.has(board)) {
    const text = readFileSync(join(SCH, `${board}.sch`), "utf8");
    const lines = text.split("\n");
    const comps = [], wires = [], labels = [], notes = [];
    for (let i = 0; i < lines.length; i++) {
      const L = lines[i];
      if (L === "$Comp") {
        let ref = "", libn = "", x = 0, y = 0;
        for (let k = i + 1; k < lines.length && lines[k] !== "$EndComp"; k++) {
          if (lines[k].startsWith("L ")) { const t = lines[k].split(/\s+/); libn = t[1].split(":").pop(); ref = t[2]; }
          else if (lines[k].startsWith("P ")) { const t = lines[k].split(/\s+/); x = +t[1]; y = +t[2]; }
        }
        comps.push({ ref, lib: libn, x, y });
      } else if (L === "Wire Wire Line") {
        wires.push(lines[++i].trim().split(/\s+/).map(Number));
      } else if (L === "Wire Notes Line") {
        notes.push(lines[++i].trim().split(/\s+/).map(Number));
      } else if (L.startsWith("Text Label ") || L.startsWith("Text GLabel ")) {
        const t = L.split(/\s+/);
        labels.push({ x: +t[2], y: +t[3], net: lines[++i] });
      }
    }
    const key = (x, y) => `${x},${y}`;
    const dsu = new DSU();
    for (const [x1, y1, x2, y2] of wires) dsu.union(key(x1, y1), key(x2, y2));
    const pinAt = new Map();
    for (const c of comps) {
      const sym = LIB.get(c.lib);
      if (!sym) { problems.push(`${board}: unknown symbol ${c.lib} for ${c.ref}`); continue; }
      for (const p of sym.pins) {
        const k = key(c.x + p.x, c.y + p.y);
        if (!pinAt.has(k)) pinAt.set(k, []);
        pinAt.get(k).push(`${c.ref}.${p.num}`);
      }
    }
    const netAt = new Map();
    for (const l of labels) {
      const k = key(l.x, l.y);
      if (!dsu.p.has(k)) { floating++; if (problems.length < 40) problems.push(`${board}: label ${l.net} at ${k} touches no wire`); continue; }
      netAt.set(dsu.find(k), l.net);
    }
    const pinNet = new Map();
    for (const [k, refs] of pinAt) {
      if (!dsu.p.has(k)) continue;
      const n = netAt.get(dsu.find(k));
      if (n) for (const r of refs) pinNet.set(r, n);
    }
    // drawing rules
    const boxes = comps.map((c) => {
      const b = LIB.get(c.lib)?.box ?? { x0: 0, y0: 0, x1: 0, y1: 0 };
      return { ref: c.ref, x0: c.x + b.x0, x1: c.x + b.x1, y0: c.y - b.y1, y1: c.y - b.y0 };
    });
    for (let a = 0; a < boxes.length; a++) for (let b = a + 1; b < boxes.length; b++) {
      const p1 = boxes[a], p2 = boxes[b];
      if (p1.x0 < p2.x1 && p2.x0 < p1.x1 && p1.y0 < p2.y1 && p2.y0 < p1.y1) {
        overlaps++; if (problems.length < 40) problems.push(`${board}: symbols overlap — ${p1.ref} × ${p2.ref}`);
      }
    }
    if (notes.length % 4 !== 0) { openFrames++; problems.push(`${board}: ${notes.length} frame segments (not a multiple of 4)`); }
    totLabels += labels.length;
    cache.set(board, { pinNet, nComps: comps.length, nLabels: labels.length });
  }
  const { pinNet } = cache.get(board);

  let ok = 0, wrong = 0, absent = 0;
  for (const c of page.chunks.flat()) {
    for (const p of c.pins) {
      if (!p.signal_name) continue;
      totPins++;
      const got = pinNet.get(`${c.designator}.${p.pin_number}`);
      if (got === undefined) { absent++; if (problems.length < 40) problems.push(`${page.page}: ${c.designator}.${p.pin_number} unconnected (want ${p.signal_name})`); }
      else if (got !== p.signal_name) { wrong++; if (problems.length < 40) problems.push(`${page.page}: ${c.designator}.${p.pin_number} on ${got}, want ${p.signal_name}`); }
      else ok++;
    }
  }
  totOk += ok; totWrong += wrong; totAbsent += absent; sheets++;
  const verdict = wrong + absent ? `FAIL ${wrong} wrong · ${absent} unconnected` : "PASS";
  console.log(`${verdict.padEnd(34)} ${page.page.padEnd(22)} ${ok}/${ok + wrong + absent} pins`);
}

console.log(`\n== ${sheets} pages · ${totPins} connected pins · ${totLabels} labels ==`);
console.log(`correct ${totOk} · wrong ${totWrong} · unconnected ${totAbsent} → ${(100 * totOk / totPins).toFixed(2)}%`);
console.log(`floating labels ${floating} · overlapping symbols ${overlaps} · unclosed frames ${openFrames}`);
if (problems.length) { console.log("\nfirst problems:"); problems.slice(0, 20).forEach((p) => console.log("  " + p)); }
// Round 14 (A12-R03): the emitted MCU symbol must carry PHYSICAL ball IDs as its pin numbers, one record per
// connected ball of calculations/mcu-ballmap.json, number == the ball in the manifest label (a mutated number
// with an unchanged label fails here; a label without its ball fails here).
{
  const man = JSON.parse(readFileSync(join(SRC, "..", "..", "mcu-ballmap.json"), "utf8"));
  const want = new Map(man.balls.filter((r) => r.net).map((r) => [r.label, r.ball]));
  const mcu = [...LIB.entries()].find(([n]) => /^S32K396/.test(n));
  const recs = mcu ? mcu[1].pins : [];
  const names = mcu ? libText.split(/^DEF /m).slice(1).find((b) => b.startsWith(mcu[0])).split("\n").filter((l) => l.startsWith("X ")).map((l) => l.split(/\s+/)[1]) : [];
  let bad = 0;
  recs.forEach((p, i) => {
    const label = names[i];
    const ball = want.get(label);
    if (!/^[A-U]\d{1,2}$/.test(p.num)) { bad++; if (bad <= 5) problems.push(`MCU pin number not a ball ID: ${label} = ${p.num}`); }
    else if (ball !== p.num) { bad++; if (bad <= 5) problems.push(`MCU pin number ${p.num} does not match manifest ball for ${label} (${ball})`); }
  });
  if (!mcu || recs.length !== want.size || bad) {
    console.log(`FAIL: MCU symbol ball numbering — ${recs.length} records vs ${want.size} manifest balls, ${bad} mismatches`);
    problems.slice(0, 8).forEach((p) => console.log("  " + p));
    process.exit(1);
  }
  console.log(`MCU symbol: ${recs.length} pins numbered by physical ball, all match the manifest`);
}
// Round 12 (R2-F29): an empty expected-page set left every counter at zero and exited 0. Round 13
// (A11-R05): count DISTINCT assemblies, not JSON pages — the exact set must be present, each with pins.
const WANT = ["traction-power", "traction-capbank", "traction-disch", "traction-card"];
const seen = [...cache.keys()], missing = WANT.filter((b) => !seen.includes(b)), extra = seen.filter((b) => !WANT.includes(b));
const dup = seenPages.filter((b, i) => seenPages.indexOf(b) !== i);
if (missing.length || extra.length || dup.length || totPins < 1500)
  { console.log(`FAIL: assemblies compared ${seen.join(",")} — missing [${missing}] extra [${extra}] duplicate pages [${dup}]; ${totPins} pins (need >= 1500)`); process.exit(1); }
process.exit(totWrong + totAbsent + floating + overlaps + openFrames ? 1 : 0);
