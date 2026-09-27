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
import { readFileSync, writeFileSync, mkdirSync, existsSync, readdirSync, unlinkSync, copyFileSync, rmSync } from "node:fs";
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

// round 23 (A21-R01): a pad-per-pin proof cannot see that an SMT part sits on a through-hole land. The ordering codes that
// state the mounting technology are locked against the footprint's (attr …): every rule whose MPN matches must resolve to a
// footprint of that attribute. Extend the table when a new connector family is bound.
const MOUNT = [
  [/^FTSH-\d+-\d+-[A-Z]+-DV/, "smd"],            // Samtec FTSH … -DV = double-row vertical SURFACE-MOUNT
  [/^IPL1-\d+-\d+-[A-Z]-[SD]-K$/, "through_hole"], // Samtec IPL1 … -S-K / -D-K = through-hole shrouded
  [/^T2M-\d+-\d+-[A-Z]-[A-Z]-TH/, "through_hole"], // Samtec T2M … -TH = through-hole
  [/^(TE )?770669-1/, "through_hole"],            // TE AMPSEAL 23 PCB header
  [/^HC5FW/, "through_hole"],
];
const attrOf = (file) => /\(attr (smd|through_hole)/.exec(readFileSync(file, "utf8"))?.[1] ?? "unspecified";
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
// mounting-technology lock (after the copies exist)
for (const r of rules) {
  if (!r || !r.mpn) continue;
  const want = MOUNT.find(([re]) => re.test(String(r.mpn)))?.[1];
  if (!want) continue;
  let fp; try { fp = r.kfp ? parseKfp(r.kfp) : (r.fp ? KFP[r.fp] : undefined); } catch { fp = undefined; }
  if (!fp) continue;
  const file = join(PRETTY, `${fp.name}.kicad_mod`);
  if (!existsSync(file)) continue;                 // reported as missing above
  const have = attrOf(file);
  if (have !== want) missing.push(`${r.m} ${r.mpn}: the ordering code says ${want}, footprint ${fp.name} is (attr ${have})`);
}
writeFileSync(join(PRETTY, "SOURCES.json"), JSON.stringify(sources, null, 2) + "\n");
writeFileSync(join(PRETTY, "README.md"), `# traction.pretty — the project footprint library

One library, shipped with the KiCad 10 project in kicad/ (**KiCad 10.0.6 or later**: the copied patterns carry the 10.x file
format, which KiCad 5–9 cannot read — the KiCad 5 / EasyEDA sets in kicad5/ name the same footprints in their F2 fields but
ship no library). Every symbol's Footprint field is "${LIB}:<name>" and resolves here, so "Update PCB from Schematic" in
KiCad 10 needs nothing from the recipient's own libraries. ${copied} patterns are verbatim copies from the ${kicadVersion} footprint libraries (their names kept;
KiCad libraries licence CC-BY-SA 4.0 with the KiCad libraries exception — free to use in a design; SOURCES.json records
the library and the sha256 of each copy), ${custom} are drawn from the archived manufacturer datasheets and documented
pad by pad in MANIFEST.md. Generated by calculations/kicad-fp-gen.mjs from calculations/footprints.mjs (the code → footprint
map); calculations/kicad-sch-verify.mjs proves with kicad-cli that every symbol resolves (ERC footprint_link_issues = 0)
and that every symbol pin number has a pad of that number in its footprint.
`);
const TABLE7 = `(fp_lib_table\n\t(version 7)\n\t(lib (name "${LIB}")(type "KiCad")(uri "\${KIPRJMOD}/${LIB}.pretty")(options "")(descr "Traction Inverter GEN-1 footprints (generated + datasheet-drawn)"))\n)\n`;
writeFileSync(join(OUT, "fp-lib-table"), TABLE7);
// round 23 (A21-R01/R02 of the A.21 rechecks): the copied patterns are KiCad 10.0.6 files ("(footprint … (version 20260206)"):
// KiCad 5 parses only "module"/"kicad_pcb" roots and KiCad 6–9 refuse a newer format version, so the library is NOT mirrored
// into the KiCad 5 sets any more — their F2 fields name the footprints, the library lives in kicad/traction only.
for (const d of ["kicad5/traction", "kicad5/traction-native"]) {
  const dir = join(ROOT, d), p = join(dir, `${LIB}.pretty`);
  if (existsSync(p)) { for (const f of readdirSync(p)) unlinkSync(join(p, f)); rmSync(p, { recursive: true, force: true }); }
  if (existsSync(join(dir, "fp-lib-table"))) unlinkSync(join(dir, "fp-lib-table"));
}
console.log(`${LIB}.pretty: ${copied} copied from ${kicadVersion}, ${custom} drawn from datasheets, ${wanted.size} referenced by ${Object.keys(KFP).length} codes → kicad/traction (KiCad 10.0.6+ format; not mirrored into the KiCad 5 sets)`);
if (missing.length) { console.log(`FAIL — ${missing.length} footprint(s) missing:\n  ${missing.join("\n  ")}`); process.exit(1); }
