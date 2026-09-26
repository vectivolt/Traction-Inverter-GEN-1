// kicad-fp-gen.mjs — build the project footprint library shipped with every schematic set (round 22, F210).
//
//   kicad/traction/traction.pretty/          one .kicad_mod per footprint the sheets reference:
//        <name>.kicad_mod                    - copied VERBATIM from KiCad 10's <lib>.pretty (name kept, sha256 recorded), or
//                                            - drawn from the archived datasheet (MANIFEST.md — hand-written, never touched here)
//        SOURCES.json · README.md            provenance of every file (KiCad library + version, or "datasheet")
//   kicad/traction/fp-lib-table              (version 7) one library "traction" at ${KIPRJMOD}/traction.pretty
//   kicad5/traction/, kicad5/traction-native/ the same .pretty + an fp-lib-table KiCad 5 reads
//
// Every entry of footprints.mjs KFP must resolve to a file, so the sheets can only ship with a complete library;
// a copied file that no code references any more is removed. Exit 1 on anything missing.
// Run: node calculations/kicad-fp-gen.mjs      (KICAD_FP_DIR / KICAD_CLI override the KiCad paths)
import { readFileSync, writeFileSync, mkdirSync, existsSync, readdirSync, unlinkSync, copyFileSync } from "node:fs";
import { execFileSync } from "node:child_process";
import { createHash } from "node:crypto";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { KFP, LIB, parseKfp } from "./footprints.mjs";
import { DB, OVERRIDES, SAFETY_ROWS, DISCHARGE_ROWS, SKUS } from "./parts-db.mjs";

const ROOT = join(dirname(fileURLToPath(import.meta.url)), "..");
const KICAD_FP = process.env.KICAD_FP_DIR ?? "/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints";
const CLI = process.env.KICAD_CLI ?? "/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli";
const OUT = join(ROOT, "kicad", "traction");
const PRETTY = join(OUT, `${LIB}.pretty`);
mkdirSync(PRETTY, { recursive: true });

const sha = (p) => createHash("sha256").update(readFileSync(p)).digest("hex").slice(0, 16);
let kicadVersion = "KiCad (version unknown)";
try { kicadVersion = `KiCad ${execFileSync(CLI, ["version"]).toString().trim()}`; } catch {}

const wanted = new Map();                      // name → { lib } | { lib: null }
// every per-part kfp override (parts-db rules, variant rows, overrides) is wanted too, under its own name
const rules = [...DB, ...Object.values(OVERRIDES ?? {}), ...(SAFETY_ROWS ?? []), ...(DISCHARGE_ROWS ?? []), ...Object.values(SKUS ?? {}).flatMap((v) => v.rows ?? [])];
const entries = [...Object.entries(KFP), ...rules.filter((r) => r && r.kfp).map((r) => [`kfp:${r.m}`, parseKfp(r.kfp)])];
for (const [code, r] of entries) {
  if (!r) continue;
  const prev = wanted.get(r.name);
  if (prev && prev.lib !== r.lib) throw new Error(`footprint ${r.name} is claimed both as a KiCad copy and as custom`);
  wanted.set(r.name, { lib: r.lib, codes: [...(prev?.codes ?? []), code] });
}

const sources = {};
const missing = [];
let copied = 0, custom = 0;
for (const [name, w] of [...wanted].sort()) {
  const dst = join(PRETTY, `${name}.kicad_mod`);
  if (w.lib) {
    const src = join(KICAD_FP, `${w.lib}.pretty`, `${name}.kicad_mod`);
    if (!existsSync(src)) { missing.push(`${name} ← ${w.lib}.pretty (not in ${KICAD_FP})`); continue; }
    copyFileSync(src, dst);
    sources[name] = { source: `${kicadVersion} ${w.lib}.pretty (verbatim copy)`, sha256: sha(dst), codes: w.codes };
    copied++;
  } else {
    if (!existsSync(dst)) { missing.push(`${name} (custom — draw it from the datasheet into ${LIB}.pretty, see MANIFEST.md)`); continue; }
    sources[name] = { source: "drawn from the archived datasheet (MANIFEST.md)", sha256: sha(dst), codes: w.codes };
    custom++;
  }
}
// orphans: a copied file no code references (a hand-drawn one is left alone — the manifest owns it)
const prevSources = existsSync(join(PRETTY, "SOURCES.json")) ? JSON.parse(readFileSync(join(PRETTY, "SOURCES.json"), "utf8")) : {};
for (const f of readdirSync(PRETTY).filter((f) => f.endsWith(".kicad_mod"))) {
  const name = f.slice(0, -".kicad_mod".length);
  if (!wanted.has(name) && /verbatim copy/.test(prevSources[name]?.source ?? "")) { unlinkSync(join(PRETTY, f)); console.log(`  removed orphan copy ${f}`); }
}
writeFileSync(join(PRETTY, "SOURCES.json"), JSON.stringify(sources, null, 2) + "\n");
writeFileSync(join(PRETTY, "README.md"), `# traction.pretty — the project footprint library

One library, shipped with every schematic set (KiCad 9/10 in kicad/, KiCad 5–8 and EasyEDA in kicad5/): every symbol's
Footprint field is "${LIB}:<name>" and resolves here, so "Update PCB from Schematic" needs nothing from the recipient's
own libraries. ${copied} patterns are verbatim copies from the ${kicadVersion} footprint libraries (their names kept;
KiCad libraries licence CC-BY-SA 4.0 with the KiCad libraries exception — free to use in a design; SOURCES.json records
the library and the sha256 of each copy), ${custom} are drawn from the archived manufacturer datasheets and documented
pad by pad in MANIFEST.md. Generated by calculations/kicad-fp-gen.mjs from calculations/footprints.mjs (the code → footprint
map); calculations/kicad-sch-verify.mjs proves with kicad-cli that every symbol resolves (ERC footprint_link_issues = 0)
and that every symbol pin number has a pad of that number in its footprint.
`);
const TABLE7 = `(fp_lib_table\n\t(version 7)\n\t(lib (name "${LIB}")(type "KiCad")(uri "\${KIPRJMOD}/${LIB}.pretty")(options "")(descr "Traction Inverter GEN-1 footprints (generated + datasheet-drawn)"))\n)\n`;
const TABLE5 = `(fp_lib_table\n  (lib (name ${LIB})(type KiCad)(uri \${KIPRJMOD}/${LIB}.pretty)(options "")(descr "Traction Inverter GEN-1 footprints (generated + datasheet-drawn)"))\n)\n`;
writeFileSync(join(OUT, "fp-lib-table"), TABLE7);
for (const d of ["kicad5/traction", "kicad5/traction-native"]) {
  const dir = join(ROOT, d), p = join(dir, `${LIB}.pretty`);
  mkdirSync(p, { recursive: true });
  for (const f of readdirSync(p)) unlinkSync(join(p, f));
  for (const f of readdirSync(PRETTY)) copyFileSync(join(PRETTY, f), join(p, f));
  writeFileSync(join(dir, "fp-lib-table"), TABLE5);
}
console.log(`${LIB}.pretty: ${copied} copied from ${kicadVersion}, ${custom} drawn from datasheets, ${wanted.size} referenced by ${Object.keys(KFP).length} codes → kicad/traction (+ kicad5/traction, kicad5/traction-native)`);
if (missing.length) { console.log(`FAIL — ${missing.length} footprint(s) missing:\n  ${missing.join("\n  ")}`); process.exit(1); }
