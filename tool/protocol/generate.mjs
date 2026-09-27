#!/usr/bin/env node
// generate.mjs — regenerates the machine-readable protocol exports next to this file from the firmware sources, and
// verifies them against the firmware as compiled into the simulator bridge:
//   params.json      the calibration (cal_*) parameters and the contract constants — from firmware/tools/gen-params.mjs
//                    (the single source of truth), cross-checked against include/cal_ranges.h and the bridge's table;
//                    the cross-field rules extracted from src/nvm/params.c ti_params_validate()
//   dtcs.json        every DTC of src/comms/dtc.h (id, UDS code, name, description, firmware response, where it is set)
//   can-frames.json  the vehicle CAN-FD and UDS frame codec (IDs, byte/bit layout, scaling, clamps, E2E rules) and test
//                    vectors produced by the firmware's own encoders/decoder (sim_bridge --golden); every vector is
//                    re-encoded from the tables here and must match byte for byte
// Usage (Node >= 18 or Bun; run from anywhere):
//   node tool/protocol/generate.mjs           write the three files, exit 1 if any verification fails
//   node tool/protocol/generate.mjs --check   verify that the files on disk are what the sources give (writes nothing)
// It builds the bridge first (make -C tool/bridge) because the vectors and the cross-checks come from it.
import { execFileSync } from "node:child_process";
import { mkdtempSync, readdirSync, readFileSync, rmSync, statSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join, relative } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const HERE = dirname(fileURLToPath(import.meta.url));
const TOOL = join(HERE, "..");
const REPO = join(TOOL, "..");
const FW = join(REPO, "firmware");
const BRIDGE = join(TOOL, "bridge", "sim_bridge");
const CHECK_ONLY = process.argv.includes("--check");
const problems = [];
const problem = (m) => problems.push(m);
const rd = (p) => readFileSync(join(FW, p), "utf8");

// ---------------------------------------------------------------- the firmware's own tables
async function loadGenParams() {
  const src = rd("tools/gen-params.mjs");
  const imp = /^import\s*\{\s*writeFileSync\s*\}\s*from\s*"node:fs";\s*$/m;
  if (!imp.test(src)) throw new Error("firmware/tools/gen-params.mjs: its writeFileSync import changed — update loadGenParams()");
  // run it with its file writes and its log line disabled, and export the tables it builds
  const body = src.replace(/^#!.*\n/, "").replace(imp, "const writeFileSync = () => {};");
  const patched = `const console = { log() {} };\n${body}\nexport { SKUS, COMMON, CAL, perSku };\n`;
  const dir = mkdtempSync(join(tmpdir(), "tt-gen-params-"));
  const file = join(dir, "gen-params.extracted.mjs");
  writeFileSync(file, patched);
  try {
    return await import(pathToFileURL(file).href);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
}

function calRangesH() {
  const rows = new Map();
  const re = /\{"(cal_[a-z0-9_]+)",\s*offsetof\(ti_params_t,\s*\1\),\s*TI_CAL_(F32|U32|U16|U8),\s*([-0-9.e+]+)f,\s*([-0-9.e+]+)f\}/g;
  for (const m of rd("include/cal_ranges.h").matchAll(re)) rows.set(m[1], { type: m[2].toLowerCase(), min: +m[3], max: +m[4] });
  return rows;
}

// ti_params_validate(): every "bad += (<expr>) ? 0u : 1u;" line that is not the per-f_sw loop
function crossFieldRules() {
  const src = rd("src/nvm/params.c");
  const body = src.slice(src.indexOf("uint32_t ti_params_validate"));
  const locals = {};
  for (const m of body.matchAll(/const float (\w+) = ([^;]+);/g)) locals[m[1]] = m[2];
  const out = [];
  for (const m of body.matchAll(/bad \+= \(([^;]+?)\) \? 0u : 1u;/g)) {
    let e = m[1].trim();
    if (/\[i\]|RANGES/.test(e)) continue; // the f_sw loop and the range loop are described elsewhere
    for (const [k, v] of Object.entries(locals)) e = e.replace(new RegExp(`\\b${k}\\b`, "g"), `(${v})`);
    e = e.replace(/p->/g, "").replace(/\b(\d+)u\b/g, "$1").replace(/(\d)f\b/g, "$1");
    while (/^\((.*)\)$/.test(e) && balanced(e.slice(1, -1))) e = e.slice(1, -1);
    out.push({ expr: e, cal_fields: [...new Set(e.match(/\bcal_[a-z0-9_]+/g) ?? [])] });
  }
  return out;
}
const balanced = (s) => { let d = 0; for (const c of s) { d += c === "(" ? 1 : c === ")" ? -1 : 0; if (d < 0) return false; } return d === 0; };

function dtcEnum() {
  const src = rd("src/comms/dtc.h");
  const body = src.slice(src.indexOf("typedef enum"), src.indexOf("} dtc_id_t;"));
  const list = [];
  const re = /^\s*(DTC_[A-Z0-9_]+)\s*(?:=\s*[^,]+)?,?[ \t]*(\/\*([\s\S]*?)\*\/)?/gm;
  for (const m of body.matchAll(re)) {
    if (m[1] === "DTC_COUNT" || m[1] === "DTC_NONE") continue; /* ids start at 1; 0 = none */
    list.push({ name: m[1], comment: (m[3] ?? "").replace(/\s+/g, " ").trim() });
  }
  return list;
}

function sourceFiles(dir) {
  const out = [];
  for (const e of readdirSync(dir)) {
    const p = join(dir, e);
    if (statSync(p).isDirectory()) out.push(...sourceFiles(p));
    else if (/\.c$/.test(e)) out.push(p);
  }
  return out;
}

const fwId = () => (rd("src/app/app.h").match(/#define TI_FW_ID (0x[0-9A-Fa-f]+)u/) ?? [])[1] ?? "unknown";
function treeInfo() {
  try {
    const head = execFileSync("git", ["-C", REPO, "rev-parse", "--short", "HEAD"], { encoding: "utf8" }).trim();
    const dirty = execFileSync("git", ["-C", REPO, "status", "--porcelain", "--", "firmware/src", "firmware/include", "firmware/tools"], { encoding: "utf8" })
      .split("\n").filter(Boolean).map((l) => l.slice(3));
    return { git_head: head, firmware_files_modified_since_head: dirty };
  } catch {
    return { git_head: "unknown", firmware_files_modified_since_head: [] };
  }
}

// ---------------------------------------------------------------- the bridge (compiled firmware)
function bridge(args, input = "") {
  execFileSync("make", ["-s", "-C", join(TOOL, "bridge")], { stdio: ["ignore", "ignore", "inherit"] });
  return execFileSync(BRIDGE, args, { input, encoding: "utf8", maxBuffer: 64 << 20 })
    .split("\n").filter((l) => l.startsWith("{")).map((l) => JSON.parse(l));
}

// ---------------------------------------------------------------- params.json
const UNIT_OVERRIDES = {
  cal_isum_debounce: "samples", cal_hvil_debounce: "samples", cal_rslv_debounce: "updates", cal_isns_act_debounce: "samples",
  cal_can_ctr_max_jump: "counts", cal_fs26_prog_id: "id", cal_swg_code_init: "code", cal_rslv_restart_max: "restarts",
  cal_rslv_amp_min: "ratio", cal_rslv_amp_max: "ratio", cal_rslv_exc_min: "ratio", cal_rslv_exc_max: "ratio",
  cal_fc_fraction: "ratio", cal_mod_index_max: "ratio", cal_rslv_wind_per_mon: "ratio", cal_rslv_rate_tol_frac: "ratio",
  n_fsw: "count", fs26_wd_err_limit: "count", qdis_max_per_window: "count", sku: "", name: "", sic: "", class8: "",
  fs26_backup_fs0b: "", fs26_backup_fs1b: "", hwid_ratio_nom: "ratio", vdc_div_ratio: "ratio", exc_gain: "ratio",
  exc_amp_per_mon: "ratio", vsup_amux_ratio: "ratio", v5gd_sns_ratio: "ratio", s6_delay_tsw: "T_sw", fw03_pf: "ratio",
  fw03_mod_reserve: "ratio", ntc_b_k: "K", bntc_b_k: "K", fsw_hz: "Hz", fc_ceiling_hz: "Hz",
};
// longest suffix first
const SUFFIX_UNITS = [
  ["_rad_s2", "rad/s²"], ["_rad_s", "rad/s"], ["_nm_vs", "N·m/(V·s)"], ["_nm_v", "N·m/V"], ["_nm_s", "N·m/s"],
  ["_c_s", "°C/s"], ["_v_per_a", "V/A"], ["_rms_a", "A rms"], ["_vpp", "V pp"], ["_ohm", "Ω"], ["_frac", "ratio"],
  ["_rpm", "rpm"], ["_deg", "°"], ["_rad", "rad"], ["_hz", "Hz"], ["_ms", "ms"], ["_us", "µs"], ["_ns", "ns"],
  ["_nm", "N·m"], ["_s", "s"], ["_c", "°C"], ["_a", "A"], ["_v", "V"], ["_w", "W"], ["_j", "J"], ["_f", "F"], ["_k", "K"],
];
const unitOf = (n) => (n in UNIT_OVERRIDES ? UNIT_OVERRIDES[n] : (SUFFIX_UNITS.find(([s]) => n.endsWith(s)) ?? [null, ""])[1]);
const GROUPS = [
  [/^cal_(vdc_|isum|isns_offset|dtcomp)/, "DC link and current sensing"], [/^cal_isns/, "DC link and current sensing"],
  [/^cal_(ntc|temp|tmod|derate|coolant|peak)/, "Thermal (FW-04, FW-13)"], [/^cal_ign/, "Ignition (KL15)"],
  [/^cal_hvil/, "HVIL (FW-09)"], [/^cal_(can|bms|torque_ramp|dir)/, "Vehicle interface (FW-11)"],
  [/^cal_(rslv|speed_hold|swg|sd_irq)/, "Resolver (FW-10, FW-28..36)"],
  [/^cal_(rdy|fw16|oneshot|desat|asc)/, "Gate drive, DESAT and self-test (FW-06a, FW-14..16, FW-22)"],
  [/^cal_(precharge|qdis)/, "Precharge and discharge (FW-17..19, FW-26)"],
  [/^cal_(fc_|mod_index|vdyn|torque_max|spo|ripple)/, "Control and torque (FW-03, FW-25, section 6)"],
  [/^cal_dcl/, "DC-link trim (FW-08)"], [/^cal_(sensor_selftest|fs0b|fs26)/, "FS26 and start-up (FW-12, section 9)"],
  [/^cal_vsup/, "LV supply (FW-33)"],
  [/^(vdc|ov_|cap_|c_nom|c_min|c_max)/, "Link voltage and capacitor bank"], [/^(i_|isns)/, "Currents and current sensing"],
  [/^(n_fsw|fsw|fc_|dead|s6_)/, "Switching and current loop"], [/^(p_|fw03|peak)/, "Power envelope (FW-03)"],
  [/^hwid/, "Identity (FW-01, FW-02)"], [/^(r_|tau|qdis|passive|hv_safe)/, "Discharge (FW-17..19)"],
  [/^fw06|^asc_/, "Fast protection (FW-05, FW-06)"], [/^(vofs|v5gd)/, "Supplies and offsets (FW-07)"],
  [/^hvil/, "HVIL (FW-09)"], [/^can_/, "Vehicle interface (FW-11)"], [/^(fs26|vsup)/, "FS26 and LV supply (FW-12, FW-33)"],
  [/^(ntc|bntc|mt_)/, "Thermal (FW-04, FW-13)"], [/^(fw15|desat)/, "DESAT and recovery (FW-15)"], [/^fw16/, "Gate self-test (FW-16)"],
  [/^precharge/, "Precharge and discharge (FW-17..19, FW-26)"], [/^(rslv|exc|swg)/, "Resolver (FW-10, FW-28..36)"],
  [/^(sku|name|sic|class8)$/, "Identity (FW-01, FW-02)"],
];
const groupOf = (n) => (GROUPS.find(([re]) => re.test(n)) ?? [null, "Other"])[1];
const refsOf = (t) => [...new Set(String(t).match(/FW-\d+[a-z]?|§\s?\d+[a-z]?|R\d?[A-Z]?\d*-[A-Z]?\d+|A1\d-[RN]\d+|F\d{2,3}\b/g) ?? [])];
const jsonNum = (v) => (typeof v === "number" ? +v.toPrecision(9) : v);

async function buildParams(hello) {
  const { SKUS, COMMON, CAL, perSku } = await loadGenParams();
  const h = calRangesH();
  const fromBridge = new Map((hello?.params ?? []).map((p) => [p.name, p]));
  const typeName = { f: "f32", u: "u32", u16: "u16", u8: "u8" };
  const parameters = CAL.map(([name, def, min, max, t, basis]) => {
    const row = { name, type: typeName[t], unit: unitOf(name), min: jsonNum(min), max: jsonNum(max), default: jsonNum(def),
      group: groupOf(name), description: basis, refs: refsOf(basis), writable: true };
    const c = h.get(name);
    if (!c) problem(`params: ${name} is in gen-params.mjs but not in include/cal_ranges.h (regenerate the headers: make params)`);
    else if (c.type !== row.type || Math.abs(c.min - min) > 1e-6 * Math.max(1, Math.abs(min)) || Math.abs(c.max - max) > 1e-6 * Math.max(1, Math.abs(max)))
      problem(`params: ${name} differs between gen-params.mjs [${row.type} ${min}, ${max}] and cal_ranges.h [${c.type} ${c.min}, ${c.max}]`);
    const b = fromBridge.get(name);
    if (hello && !b) problem(`params: ${name} is not in the compiled bridge's table (rebuild: make -C tool/bridge)`);
    else if (b && (Math.abs(b.default - def) > 1e-4 * Math.max(1, Math.abs(def))) && name !== "cal_fs26_prog_id")
      problem(`params: ${name} default ${def} (gen-params) vs ${b.default} (compiled firmware)`);
    return row;
  });
  for (const n of h.keys()) if (!CAL.some(([c]) => c === n)) problem(`params: ${n} is in cal_ranges.h but not in gen-params.mjs`);
  const skuRows = SKUS.map((s) => perSku(s));
  const perSkuNames = skuRows[0].map(([f]) => f);
  const constants = [
    ...perSkuNames.map((name, i) => {
      const [, , src] = skuRows[0][i];
      const values = Object.fromEntries(SKUS.map((s, k) => [s.key, rawValue(skuRows[k][i][1])]));
      return { name, unit: unitOf(name), group: groupOf(name), description: src, refs: refsOf(src), per_sku: true, values, writable: false };
    }),
    ...COMMON.map(([name, v, src]) => ({ name, unit: unitOf(name), group: groupOf(name), description: src, refs: refsOf(src),
      per_sku: false, value: rawValue(v), writable: false })),
  ];
  return {
    $comment: "Generated by tool/protocol/generate.mjs from firmware/tools/gen-params.mjs — do not edit.",
    source: "firmware/tools/gen-params.mjs (cross-checked: firmware/include/cal_ranges.h, the compiled parameter table)",
    firmware: { fw_id: fwId(), ...treeInfo() },
    skus: SKUS.map((s, i) => ({ index: i + 1, key: s.key, id: s.id, name: s.name })),
    units_note: "Units are derived from the field-name suffix (firmware/include/ti_types.h conventions); the firmware stores f32/u32/u16/u8.",
    write_rules: [
      "Only the parameters below (the cal_* rows) are writable; the constants are the contract's and are fixed per SKU.",
      "A write is accepted only if the whole set still passes ti_params_validate(): every cal_* inside [min, max] and every cross-field rule below.",
      "Integer types (u32, u16, u8) take whole numbers only.",
      "The firmware validates the set at boot; values read only at initialisation (e.g. the current-loop gains from cal_fc_fraction) take effect at the next power-up.",
    ],
    cross_field_rules: { source: "firmware/src/nvm/params.c ti_params_validate() (extracted)", rules: crossFieldRules() },
    parameters,
    constants,
  };
}
function rawValue(v) {
  if (typeof v !== "string") return jsonNum(v);
  const arr = v.match(/^\{(.*)\}$/);
  if (arr) return arr[1].split(",").map((x) => +x.trim().replace(/[uf]$/, ""));
  return v.replace(/^"|"$/g, "");
}

// ---------------------------------------------------------------- dtcs.json
// [class, description, firmware response, requirement] per DTC name; class: no_arm (arming refused for the key cycle),
// fault (a section-6 row / the FAULT state), degrade (torque reduced or a ramp), info (a record), service (NVM lock)
const DTC_INFO = {
  DTC_FS26_PROGID: ["no_arm", "The FS26 OTP variant is not bound (cal_fs26_prog_id = 0xFFFF) or its M_PROGID is not the procured one.", "INIT fails: FS0B/FS1B are never released, no arming for the key cycle.", "FW-12"],
  DTC_FS26_OTP_CORRUPT: ["no_arm", "The FS26 reports OTP_CORRUPT.", "No arming for the key cycle.", "FW-12"],
  DTC_FS26_DEBUG_MODE: ["no_arm", "The FS26 is in debug mode (DBG_MODE).", "No arming for the key cycle.", "FW-12"],
  DTC_FS26_INIT_READBACK: ["no_arm", "An INIT_FS register (WD_CFG, FSSM, WDW_DURATION with their NOT complements, SAFE_IOS_2) does not read back.", "No arming for the key cycle.", "FW-12"],
  DTC_FS26_SPI: ["no_arm", "SPI communication with the FS26 failed (frame CRC or no answer).", "No arming for the key cycle.", "FW-12"],
  DTC_FS26_WD: ["fault", "A Q&A watchdog answer failed in the 1 ms task.", "Recorded; the FS26 itself counts the error (WD_ERR_LIMIT = 2) and asserts FS0B (hardware SPO) on a second failure.", "FW-12"],
  DTC_FS26_RELEASE: ["no_arm", "FS0B/FS1B could not be released (FLT_ERR_CNT not back to 0).", "No arming; SENSOR_SELFTEST goes to FAULT after cal_sensor_selftest_ms + cal_fs0b_release_ms.", "FW-12, section 9 step 4"],
  DTC_FS1B_SHORT_HIGH: ["no_arm", "FS1B is shorted high (the vehicle FAULT_OUT wire shorted to KL30).", "No arming until repaired; no MCU reset (BACKUP_SAFETY_PATH_FS1B = 0).", "FW-12, section 9"],
  DTC_FS26_GPIO1_OTP: ["no_arm", "Gate power (RDY) came up before section 9 step 6 after a power-on: FS_GPIO1 is slotted in the FS26 OTP.", "SENSOR_SELFTEST goes to FAULT; no arming.", "FW-12"],
  DTC_HWID_OPEN: ["no_arm", "HW_ID reads above 4.6 V (open).", "MCU_GATE_EN refused for the key cycle.", "FW-01"],
  DTC_HWID_SHORT: ["no_arm", "HW_ID reads below 0.2 V (short).", "MCU_GATE_EN refused for the key cycle.", "FW-01"],
  DTC_HWID_UNKNOWN: ["no_arm", "HW_ID is in no SKU window, or unstable.", "MCU_GATE_EN refused for the key cycle.", "FW-01"],
  DTC_SKU_MISMATCH: ["no_arm", "The HW_ID SKU, the parameter-set SKU and the calibration SKU disagree.", "MCU_GATE_EN refused (an 8XX OV trip on a 4XX bank would destroy it).", "FW-02"],
  DTC_CALIB_INVALID: ["no_arm", "The FW-20 calibration record is missing or fails its version, CRC, range, SKU, serial, motor-ID or f_sw check (hello.calib.err bits).", "No torque, no arming; n_x unknown (every decision takes the n >= n_x column).", "FW-20"],
  DTC_PARAMS_INVALID: ["no_arm", "ti_params_validate() found violations (a cal_* out of range or a cross-field rule).", "No arming for the key cycle.", "FW-20, section 2"],
  DTC_PWM_LOCK: ["no_arm", "The eFlexPWM fault lock-down does not match its image, or the REG_PROT lock does not read back (also when lost while running).", "No arming; while armed: the 'control lost' row.", "FW-24"],
  DTC_DESAT_HS: ["fault", "A high-side driver tripped DESAT (FLT_HS); raised again at a power-up while its NVM record (this or the previous key cycle) blocks automatic arming.", "Hardware SPO at once (PWM fault input, fault latch -> DRV_EN); FW-15 sequence; below n_x SPO, at or above n_x PWM-ASC after the >= 1.5 ms driver reset; keep_hv per FW-08b; FAULT. One VCU-authorised retry per key cycle, >= 1 s later, below n_x, at reduced torque. Round 23: at the next power-up the record's block is FAULT until that authorisation, and this DTC names it (the DTC store is RAM).", "FW-15, FW-22, section 6"],
  DTC_DESAT_LS: ["fault", "A low-side driver tripped DESAT (FLT_LS); raised again at a power-up while its NVM record (this or the previous key cycle) blocks automatic arming.", "SPO at every speed (LS-ASC could short the link through a failed high side); FW-15; FAULT. Round 23: at the next power-up the record's block is FAULT until the VCU's retry authorisation, and this DTC names it.", "FW-15, section 6"],
  DTC_DESAT_REPEAT: ["fault", "A second DESAT in the key cycle.", "Latched: no further retry this key cycle.", "FW-15"],
  DTC_DESAT_PENDING_BOOT: ["no_arm", "A driver FLT reads low at boot with V5GD healthy (a DESAT pending from before the reset).", "The state machine goes to FAULT, SPO with ASC masked, no arming.", "section 9"],
  DTC_FLT_RECOVERY_FAIL: ["fault", "The FW-15 recovery (>= 1.5 ms low, one-shot clear) did not release FLT, or RDY / ASC_CMD_RB read back wrong.", "The bridge stays in SPO; FAULT.", "FW-15"],
  DTC_OVERCURRENT: ["fault", "A phase current crossed +/-1.25 x sqrt(2) x I_pk,rms (601 A 8XX / 707 A 4XX) in the hardware ADC compare (software backstop in the ISR) — outside cal_asc_oc_window_ms of an LS-ASC entry (round 23: inside it, DTC_ASC_OC_TRANSIENT).", "High sides forced off in hardware; the latched 'control lost' row: SPO below n_x, LS-ASC at or above n_x or at unknown speed; cleared only by a VCU fault reset below n_x.", "FW-05, section 6"],
  DTC_OVERVOLTAGE: ["fault", "A V_DC channel crossed the OV trip (880 V 8XX / 530 V 4XX) in the hardware compare.", "High sides off, zero torque, ASC request <= 15.6 us after the crossing and PWM-ASC where section 6 permits; latched.", "FW-06, section 4c"],
  DTC_ISNS_OPEN: ["fault", "A phase-current channel is outside 0.2-4.8 V (open signal wire or unpowered sensor).", "Current sensing invalid: the 'control lost' row (SPO below n_x, LS-ASC above).", "FW-05"],
  DTC_ISNS_RANGE: ["fault", "Phase-current sensing invalid (range) while none of the more specific causes applies.", "The 'control lost' row.", "FW-05"],
  DTC_ISNS_SUM: ["fault", "|ia + ib + ic| above cal_isum_tol_a for cal_isum_debounce samples (latched).", "The 'control lost' row.", "FW-05"],
  DTC_ISNS_OFFSET: ["no_arm", "The standstill zero-current offset differs from the EOL calibration by more than cal_isns_offset_tol_v (set at the check since round 23).", "The sensor self-test does not pass (section 9 step 3) and times out to FAULT (DTC_SENSOR_SELFTEST).", "section 9 step 3"],
  DTC_ISNS_STALE: ["fault", "Phase-current samples are stale or lost (no complete triplet, or the current loop stopped: FW-31).", "The 'control lost' row.", "FW-27, FW-31"],
  DTC_VDC_DISAGREE: ["fault", "|VDC1 - VDC2| above max(5 %, cal_vdc_disagree_floor_v).", "V_DC invalid (HV state UNKNOWN): SPO under rule (a) or (b), otherwise LS-ASC.", "FW-07, section 6"],
  DTC_VOFS: ["fault", "The receivers' shared +0.5 V offset (VOFS) is outside 0.475-0.525 V: both channels invalid.", "The 'V_DC invalid' row.", "FW-07"],
  DTC_V5GD: ["fault", "The gate-logic supply V5GD is outside 4.75-5.25 V.", "The V5GD row: SPO, ASC_CLR, V_DC invalid, no arming.", "FW-07, section 4c"],
  DTC_VDC_FAILSAFE: ["fault", "A V_DC channel reads below 0.25 V (the AMC1311 fail-safe state, not a dead bus).", "The 'V_DC invalid' row.", "FW-07"],
  DTC_VDC_STALE: ["fault", "A V_DC channel sample is older than cal_vdc_stale_us.", "The 'V_DC invalid' row.", "FW-07"],
  DTC_VDC_BMS: ["fault", "With the contactors reported closed, |V_DC - V_pack| > 3 % for cal_vdc_bms_debounce_ms.", "The battery-path row: zero current below n_x, LS-ASC above.", "FW-07, FW-08"],
  DTC_RSLV_AMPLITUDE: ["fault", "Resolver sin^2 + cos^2 outside [cal_rslv_amp_min, cal_rslv_amp_max] of its calibrated value for cal_rslv_debounce frames in a row (round 23: a single frame outside is kept out of the angle observer, nothing more).", "Resolver invalid: no angle-dependent torque; section 6 decides on the speed bound |n_last| + cal_speed_accel_max_rpm_s x t (round 23): SPO while it stays below n_x, LS-ASC once it reaches n_x, the n >= n_x column once it passes n_max.", "FW-10, section 6"],
  DTC_RSLV_EXCITATION: ["fault", "The excitation monitor level is outside its window (the FW-30 planes).", "Resolver invalid (as DTC_RSLV_AMPLITUDE).", "FW-10, FW-30"],
  DTC_RSLV_TRACKING: ["fault", "The angle observer's tracking error exceeds cal_rslv_track_err_rad for cal_rslv_debounce updates.", "Resolver invalid.", "FW-10"],
  DTC_RSLV_ACCEL: ["fault", "The angle observer's innovation implies an acceleration beyond cal_rslv_accel_max_rad_s2 (driveline bound) for cal_rslv_debounce frames in a row (round 23: a single such frame is kept out of the observer).", "Resolver invalid.", "FW-10"],
  DTC_RSLV_RATE: ["fault", "The resolver speed disagrees with the back-EMF (voltage-model) speed while modulating.", "Resolver invalid.", "FW-10"],
  DTC_TEMP_MODULE: ["degrade", "A module NTC reading is invalid: open, short, or (round 23) its mean over cal_temp_rate_win_ms moving more than cal_temp_rate_db_codes ADC codes at more than cal_temp_rate_c_s — three such windows latch the channel for the key cycle.", "Derating falls to the continuous rating only while no module NTC is valid; no section-6 row. Passed when all three are valid again (round 23).", "FW-13, FW-04"],
  DTC_TEMP_BOARD: ["info", "A board NTC reading is invalid (round 23: set while either is, passed when both are valid).", "Recorded; the board NTCs take no part in the derating.", "FW-13"],
  DTC_TEMP_MOTOR: ["info", "A motor temperature sensor reading is invalid (round 23: set while either is, passed when both are valid).", "Recorded; the statistics (FW-43) and the fault records use the valid one.", "FW-13"],
  DTC_OVERTEMP: ["degrade", "The hottest valid module NTC at the end of its derating band, cal_tmod_derate_end_c (round 23; passed below that end less cal_derate_hyst_c).", "Information beside the response, which is the FW-04 derating (no torque left at the band's end); no section-6 row.", "FW-04"],
  DTC_HVIL_OPEN: ["degrade", "The HVIL signature reads open while armed.", "The command-lost row: torque ramped to zero within 100 ms, then SPO below n_x / current control kept above; HVIL open is not a licence to open the contactors at speed.", "FW-09, section 6"],
  DTC_HVIL_SHORT: ["degrade", "The HVIL loop is shorted to ground or to the supply while armed.", "The command-lost row (as DTC_HVIL_OPEN).", "FW-09"],
  DTC_CAN_TIMEOUT: ["fault", "VCU_CMD stale (> 20 ms, or a frozen alive counter) while armed.", "The command-lost row and the battery-path row (the contactor state is unknown): zero current below n_x, LS-ASC above; never 'hold the last value'.", "FW-11, section 6"],
  DTC_BMS_TIMEOUT: ["degrade", "VCU_BMS stale (> cal_bms_timeout_ms) while armed.", "Zero regen; motoring keeps the FW-03 envelope.", "FW-11"],
  DTC_CAN_E2E: ["info", "A vehicle frame failed its E2E CRC.", "The frame is discarded (its staleness then counts).", "FW-11"],
  DTC_QDIS_STUCK_OFF: ["info", "Active discharge commanded but the link did not decay within 200 ms.", "HV state from the witnesses; the passive bleeder bounds the time to < 60 V.", "FW-18"],
  DTC_QDIS_STUCK_ON: ["service", "The link decays at the active-discharge rate with QDIS off (a shorted switch).", "Latched service lock kept in NVM across key cycles: no arming, 'service required' and 'open the contactors' on CAN; cleared only by the UDS routine 0xF010 (FW-32) at the next power-up.", "FW-26, FW-32"],
  DTC_QDIS_RATE_LIMIT: ["info", "A discharge request beyond 3 per 5 min.", "Refused (thermal recovery of the resistors).", "FW-17"],
  DTC_TAU_MISMATCH: ["info", "The key-off discharge time constant is more than 20 % off the SKU's expected tau.", "Recorded (plausibility only, not the SKU proof).", "FW-02"],
  DTC_PRECHARGE_PLATEAU: ["no_arm", "The precharge settled below 97.5 % of the pack (a shorted QDIS or resistor string).", "Arming refused for the key cycle.", "FW-19"],
  DTC_PRECHARGE_TAU: ["no_arm", "The precharge 63 % time is shorter than cal_precharge_tau_min_s.", "Arming refused for the key cycle.", "FW-19"],
  DTC_PRECHARGE_TIMEOUT: ["no_arm", "No precharge verdict within cal_precharge_timeout_ms.", "Arming refused for the key cycle.", "FW-19"],
  DTC_SELFTEST_FAIL: ["no_arm", "An FW-16 gate self-test step did not read back as expected.", "No arming; the failed step is kept in NVM.", "FW-16"],
  DTC_SELFTEST_NO_PASS: ["no_arm", "FW-16 was skipped (its measured conditions did not hold) and no pass is stored from this or the previous key cycle.", "No arming.", "FW-16"],
  DTC_GATE_POWER: ["fault", "RDY_HS / RDY_LS not up within cal_rdy_timeout_ms, or lost.", "No PWM; FAULT.", "FW-14"],
  DTC_SPO_ENERGY: ["fault", "Section 6 holds an SPO while neither rule (a) nor rule (b) holds.", "'No safe state proven' on CAN (INV_STATUS b14.0).", "section 6, FW-08b"],
  DTC_CTRL_NONFINITE: ["fault", "A NaN/Inf was caught before the PWM (FOC or torque path).", "PWM off; the 'control lost' row.", "FW-25, section 6"],
  DTC_NVM: ["info", "An NVM record could not be written or read.", "Recorded; the write is retried by the queue.", "FW-15, FW-20"],
  DTC_SENSOR_SELFTEST: ["no_arm", "The section 9 step 3 sensor self-test did not pass within cal_sensor_selftest_ms (round 23: set at the timeout).", "FAULT, no arming for the key cycle.", "section 9"],
  DTC_GAINS: ["no_arm", "No current-loop gain set within the section 2 crossover ceiling for the calibrated f_sw and motor.", "No arming.", "section 2"],
  DTC_ARM_EVIDENCE: ["no_arm", "The FLT -> PWM fault route is not bound, or there is no valid EOL/HIL validation record for this image, SKU and card.", "No arming, fail closed; INV_STATUS b15 names the missing items.", "FW-24"],
  DTC_TORQUE_INFEASIBLE: ["degrade", "No voltage-feasible current exists even at iq = 0 at this speed and V_DC.", "Zero torque, a speed-limit request (INV_STATUS b14.3).", "FW-25"],
  DTC_ISNS_STUCK: ["fault", "A phase shows no current where its reference asks for it (a channel stuck at its zero level).", "The 'control lost' row.", "FW-05 addendum"],
  DTC_RSLV_STALE: ["fault", "No coherent resolver frame within cal_rslv_hold_us.", "The angle is withdrawn: the 'resolver invalid' row.", "FW-28, FW-29"],
  DTC_RSLV_SWG_SAT: ["no_arm", "The SWG trim is at its top code and the excitation monitor is still below the setpoint.", "The resolver never becomes ready: no arming.", "FW-30"],
  DTC_SERVICE_LOCK_CLEARED: ["info", "The UDS routine 0xF010 cleared the stuck-on QDIS service lock.", "A record; the clear takes effect at the next power-up.", "FW-32"],
  DTC_LV_OVERVOLTAGE: ["info", "VSUP (KL30 at the FS26) above 20 V.", "Information: RUN and the torque unchanged; its first/last stamps give the duration.", "FW-33"],
  DTC_LV_OV_SUSTAINED: ["degrade", "The LV overvoltage lasted longer than its band (500 ms above 27 V, 65 s at or below).", "The command-lost ramp until VSUP is back below 19.5 V.", "FW-33"],
  DTC_LV_VSUP_UNKNOWN: ["info", "No VSUP reading (below 4.2 V on the FS26 AMUX).", "LV supervision off (information).", "FW-33"],
  DTC_RSLV_REACQUIRED: ["info", "The resolver frame ring re-acquired after an ambiguous completion, or a synchronized producer restart ran.", "Information; the FW-28 age-out selects any safe state; restarts are bounded by cal_rslv_restart_max per key cycle.", "FW-35"],
  DTC_TORQUE_POSTCOND: ["fault", "Torque -> current refused its own result (a postcondition failure: a software fault).", "No current reference; the 'control lost' row while armed.", "FW-37"],
  DTC_OVERSPEED: ["degrade", "The measured speed (a valid resolver's) in the overspeed warning band (cal_ovs_warn_frac x n_max) or trip band (cal_ovs_trip_frac x n_max), debounced cal_ovs_debounce_ms, with hysteresis; one occurrence per event, passed after it.", "Warning: the command-lost row (torque ramped to zero), the speed-limit request, no arming. Trip: also the latched 'control lost' row — SPO below n_x under the energy rule, LS-ASC at or above it (section 6 decides; no ASC of its own).", "FW-42, section 6"],
  DTC_FW_UPDATE: ["no_arm", "The UDS programming session (0x10 02) was entered: a record of the session, not a failure.", "The FW-24 validated evidence is withdrawn and arming forbidden until the next power-up (FAULT; INV_STATUS b15 names the missing evidence); the section 6 actions keep their authority.", "FW-38"],
  DTC_FW_FALLBACK: ["info", "The bootloader refused or abandoned the last activation, or restored the last known good image (the boot record's `last`).", "A record at the power-up; the image that runs is the one the bootloader accepted.", "FW-38"],
  DTC_MC_ABORTED: ["info", "A commissioning routine (RoutineControl 0xF020) aborted; its reason is in the routine's results (index 0).", "The normal safe state: no service modulation, the bridge armed idle, or the section 6 decision when a fault row caused it. A record: the cause, if a failure, has its own DTC.", "FW-39"],
  DTC_MC_CAL_WRITTEN: ["info", "The commissioning sealed a new FW-20 calibration record version (RoutineControl 0xF021).", "A record: the running key cycle keeps its record; the next key cycle's init validates the new one.", "FW-39, FW-20"],
  DTC_ASC_OC_TRANSIENT: ["info", "A phase over-current inside cal_asc_oc_window_ms of an LS-ASC entry: the short-circuit transient of the winding's back-EMF (round 23).", "Information, no section-6 row (the low sides hold the ASC; FAULT1 opens only the high sides, which the ASC holds off). Once the window is over and the current back inside the compare, the compare is re-armed and the DTC passes; an over-current outside the window, or one that outlasts it, is DTC_OVERCURRENT.", "FW-05, section 6"],
};

function buildDtcs(hello) {
  const list = dtcEnum();
  const files = sourceFiles(join(FW, "src")).map((p) => ({ rel: relative(FW, p), text: readFileSync(p, "utf8") }));
  if (hello?.dtcs) {
    const names = hello.dtcs.slice(1);
    if (names.join() !== list.map((d) => d.name).join()) problem("dtcs: dtc.h and the compiled bridge disagree on the DTC list (rebuild the bridge)");
  }
  const dtcs = list.map((d, i) => {
    const id = i + 1;
    const raised = files.filter((f) => !/comms\/dtc\.c$/.test(f.rel) && new RegExp(`\\b${d.name}\\b`).test(f.text)).map((f) => f.rel);
    const info = DTC_INFO[d.name];
    if (!info) problem(`dtcs: ${d.name} has no description in generate.mjs DTC_INFO (added to dtc.h since) — add one`);
    const [cls, description, response, requirement] = info ?? ["unknown", d.comment || "(no description)", "(see firmware/src/comms/dtc.h and the contract)", ""];
    return { id, code: `0xD1${id.toString(16).toUpperCase().padStart(4, "0")}`, name: d.name, class: cls, description, response,
      requirement, header_comment: d.comment, set_in: raised, never_set: raised.length === 0 };
  });
  for (const n of Object.keys(DTC_INFO)) if (!list.some((d) => d.name === n)) problem(`dtcs: DTC_INFO describes ${n}, which dtc.h no longer has`);
  return {
    $comment: "Generated by tool/protocol/generate.mjs from firmware/src/comms/dtc.h — do not edit.",
    source: "firmware/src/comms/dtc.h (ids = enum order), set_in = the firmware sources that reference the DTC",
    firmware: { fw_id: fwId(), ...treeInfo() },
    code_rule: "UDS DTC code = 0xD10000 | id (manufacturer range; the OEM mapping comes at integration). id 0 = DTC_NONE.",
    status_bits: { "0x01": "testFailed", "0x02": "testFailedThisOperationCycle", "0x04": "pendingDTC", "0x08": "confirmedDTC",
      "0x10": "testNotCompletedSinceLastClear", "0x20": "testFailedSinceLastClear", "0x40": "testNotCompletedThisOperationCycle" },
    classes: { no_arm: "arming refused for the key cycle", fault: "a section 6 row / the FAULT state", degrade: "torque reduced or ramped",
      info: "a record, no change of behaviour", service: "an NVM-kept service lock", unknown: "not yet described here" },
    never_set: dtcs.filter((d) => d.never_set).map((d) => d.name),
    dtcs,
  };
}

// ---------------------------------------------------------------- can-frames.json
const enc = (op, k, lo, hi) => ({ op, k, clamp: [lo, hi], round: "toward zero (C float -> int cast)", arithmetic: "IEEE float32" });
const MESSAGES = [
  { name: "VCU_CMD", id: 0x101, bus: "vehicle", len: 8, direction: "VCU -> inverter", period_ms: 10,
    receive_rules: "E2E checked; stale after 20 ms (can_stale_ms) or on a repeated counter; the torque request passes the direction interlock (gear) and the limits",
    signals: [
      { name: "crc", byte: 0, bit: 0, bits: 8, kind: "e2e_crc" },
      { name: "ctr", byte: 1, bit: 0, bits: 4, kind: "e2e_counter" },
      { name: "gear", byte: 1, bit: 4, bits: 2, kind: "enum", values: { 0: "N", 1: "D", 2: "R", 3: "P" } },
      { name: "enable", byte: 1, bit: 6, bits: 1, kind: "bool" },
      { name: "fault_reset", byte: 1, bit: 7, bits: 1, kind: "bool" },
      { name: "torque_nm", byte: 2, bits: 16, kind: "int", signed: true, unit: "N·m", scale: 0.1, encode: enc("mul", 10, -32767, 32767) },
      { name: "contactors", byte: 4, bit: 0, bits: 2, kind: "enum", values: { 0: "invalid", 1: "open", 2: "precharge", 3: "closed" } },
      { name: "desat_retry_auth", byte: 4, bit: 2, bits: 1, kind: "bool" },
      { name: "discharge_req", byte: 4, bit: 3, bits: 1, kind: "bool" },
      { name: "shutdown_req", byte: 4, bit: 4, bits: 1, kind: "bool" },
      { name: "vspeed_valid", byte: 4, bit: 5, bits: 1, kind: "bool", note: "round 23 (FW-39): the vehicle speed below is valid" },
      { name: "reserved_b4", byte: 4, bit: 6, bits: 2, kind: "reserved" },
      { name: "coolant_c", byte: 5, bits: 8, kind: "int", signed: false, unit: "°C", scale: 1, offset: -40, invalid_raw: 255, encode: enc("add", 40, 0, 254) },
      { name: "vspeed_kmh", byte: 6, bits: 16, kind: "int", signed: false, unit: "km/h", scale: 0.01, encode: enc("mul", 100, 0, 65535), note: "round 23 (FW-39): the vehicle speed, 0.01 km/h; FW-39's service mode needs it valid and zero (vspeed_max_kmh 0.5)" },
    ] },
  { name: "VCU_BMS", id: 0x102, bus: "vehicle", len: 8, direction: "VCU (BMS relay) -> inverter", period_ms: 10,
    receive_rules: "E2E checked; stale after cal_bms_timeout_ms (100 ms) => zero regen",
    signals: [
      { name: "crc", byte: 0, bit: 0, bits: 8, kind: "e2e_crc" },
      { name: "ctr", byte: 1, bit: 0, bits: 4, kind: "e2e_counter" },
      { name: "reserved_b1", byte: 1, bit: 4, bits: 4, kind: "reserved" },
      { name: "v_pack_v", byte: 2, bits: 16, kind: "int", signed: false, unit: "V", scale: 0.1, encode: enc("mul", 10, 0, 65535) },
      { name: "p_chg_w", byte: 4, bits: 16, kind: "int", signed: false, unit: "W", scale: 100, encode: enc("div", 100, 0, 65535), note: "charge (regen) power limit, 0.1 kW" },
      { name: "p_dis_w", byte: 6, bits: 16, kind: "int", signed: false, unit: "W", scale: 100, encode: enc("div", 100, 0, 65535), note: "discharge (motoring) power limit, 0.1 kW" },
    ] },
  { name: "INV_STATUS", id: 0x201, bus: "vehicle", len: 20, direction: "inverter -> VCU", period_ms: 10,
    receive_rules: "the receiver checks the E2E CRC and the alive counter as the inverter does for its inputs",
    signals: [
      { name: "crc", byte: 0, bit: 0, bits: 8, kind: "e2e_crc" },
      { name: "ctr", byte: 1, bit: 0, bits: 4, kind: "e2e_counter" },
      { name: "self_test_done", byte: 1, bit: 4, bits: 1, kind: "bool" },
      { name: "keep_hv", byte: 1, bit: 5, bits: 1, kind: "bool", note: "FW-08b: keep the battery connected" },
      { name: "derate", byte: 1, bit: 6, bits: 1, kind: "bool" },
      { name: "fault", byte: 1, bit: 7, bits: 1, kind: "bool" },
      { name: "state", byte: 2, bits: 8, kind: "enum", values: { 0: "OFF", 1: "INIT", 2: "SENSOR_SELFTEST", 3: "VEHICLE_HANDSHAKE", 4: "PRECHARGE_WAIT", 5: "GATE_SELFTEST", 6: "ARMED_ZERO_TORQUE", 7: "RUN", 8: "DERATE", 9: "FAULT", 10: "DISCHARGE", 11: "SAFE_POWERDOWN" } },
      { name: "bridge", byte: 3, bit: 0, bits: 2, kind: "enum", values: { 0: "disarmed (SPO)", 1: "idle (EN high, PWM off)", 2: "modulating", 3: "PWM-ASC" } },
      { name: "hv", byte: 3, bit: 2, bits: 2, kind: "enum", values: { 0: "unknown", 1: "safe (< 60 V)", 2: "present" } },
      { name: "zero_torque", byte: 3, bit: 4, bits: 1, kind: "bool", note: "|torque command| < 0.5 N·m: a non-emergency opening may proceed (FW-08)" },
      { name: "discharging", byte: 3, bit: 5, bits: 1, kind: "bool" },
      { name: "precharge_refused", byte: 3, bit: 6, bits: 1, kind: "bool" },
      { name: "speed_valid", byte: 3, bit: 7, bits: 1, kind: "bool" },
      { name: "torque_nm", byte: 4, bits: 16, kind: "int", signed: true, unit: "N·m", scale: 0.1, encode: enc("mul", 10, -32767, 32767), note: "torque applied (round 23: what the issued current references represent; 0 without modulation)" },
      { name: "speed_rpm", byte: 6, bits: 16, kind: "int", signed: true, unit: "rpm", scale: 1, encode: enc("mul", 1, -32767, 32767) },
      { name: "vdc_v", byte: 8, bits: 16, kind: "int", signed: false, unit: "V", scale: 0.1, invalid_raw: 65535, valid_flag: "vdc_valid", encode: enc("mul", 10, 0, 65534) },
      { name: "t_module_c", byte: 10, bits: 8, kind: "int", signed: false, unit: "°C", scale: 1, offset: -40, encode: enc("add", 40, 0, 254), note: "hottest valid module NTC (-273 + 40 clamps to 0 when none is valid)" },
      { name: "n_dtc", byte: 11, bits: 8, kind: "int", signed: false, scale: 1, encode: enc("mul", 1, 0, 255), note: "confirmed DTCs, saturating at 255" },
      { name: "first_dtc", byte: 12, bits: 16, kind: "int", signed: false, scale: 1, encode: enc("mul", 1, 0, 65535), note: "id of the first active DTC (0 = none); code = 0xD10000 | id" },
      { name: "no_safe_state", byte: 14, bit: 0, bits: 1, kind: "bool" },
      { name: "service_required", byte: 14, bit: 1, bits: 1, kind: "bool" },
      { name: "open_contactors_req", byte: 14, bit: 2, bits: 1, kind: "bool" },
      { name: "speed_limit_req", byte: 14, bit: 3, bits: 1, kind: "bool" },
      { name: "reserved_b14", byte: 14, bit: 4, bits: 4, kind: "reserved" },
      { name: "evidence_missing", byte: 15, bit: 0, bits: 5, kind: "bits", values: { 1: "ROUTE_BOUND", 2: "CONFIG_MATCHES", 4: "PROTECTION_LOCKED", 8: "FAULT_ROUTE_VALIDATED", 16: "OVP_ROUTE_VALIDATED" } },
      { name: "reserved_b15", byte: 15, bit: 5, bits: 3, kind: "reserved" },
      { name: "torque_cmd_nm", byte: 16, bits: 16, kind: "int", signed: true, unit: "N·m", scale: 0.1, encode: enc("mul", 10, -32767, 32767), note: "round 23: the torque command after the limits, ramps and trims" },
      { name: "reserved", byte: 18, bits: 16, kind: "reserved" },
    ] },
];

const crc8 = (bytes, init = 0xff, xorout = 0xff) => {
  let c = init;
  for (const b of bytes) {
    c ^= b;
    for (let k = 0; k < 8; k++) c = c & 0x80 ? ((c << 1) ^ 0x1d) & 0xff : (c << 1) & 0xff;
  }
  return c ^ xorout;
};
const e2e = (id, data) => crc8([id & 0xff, ...data.slice(1)]);

// reference codec over the tables (the verification of the tables against the firmware's frames)
function rawOf(sig, v, input) {
  if (sig.kind === "e2e_counter") return v & 0x0f;
  if (sig.kind === "bool") return v ? 1 : 0;
  if (sig.kind === "enum") return +Object.keys(sig.values).find((k) => sig.values[k] === v || +k === v);
  if (sig.kind === "bits") return v & ((1 << sig.bits) - 1);
  if (sig.valid_flag && input[sig.valid_flag] === false) return sig.invalid_raw;
  const f = Math.fround;
  const e = sig.encode;
  let x = e.op === "mul" ? f(f(v) * e.k) : e.op === "div" ? f(f(v) / e.k) : f(f(v) + e.k);
  x = Math.min(Math.max(x, e.clamp[0]), e.clamp[1]);
  const r = Math.trunc(x);
  return r < 0 ? r + (1 << sig.bits) : r;
}
function encodeMsg(msg, input) {
  const d = new Array(msg.len).fill(0);
  for (const s of msg.signals) {
    if (s.kind === "e2e_crc" || s.kind === "reserved") continue;
    const v = s.kind === "e2e_counter" ? input.ctr : input[s.name];
    if (v === undefined) continue;
    const raw = rawOf(s, v, input);
    if (s.bits >= 8) for (let k = 0; k < s.bits / 8; k++) d[s.byte + k] = (raw >> (8 * k)) & 0xff; // little-endian
    else d[s.byte] |= (raw & ((1 << s.bits) - 1)) << s.bit;
  }
  d[0] = e2e(msg.id, d);
  return d;
}
function decodeMsg(msg, d) {
  const o = {};
  for (const s of msg.signals) {
    if (s.kind === "reserved") continue;
    let raw = 0;
    if (s.bits >= 8) for (let k = 0; k < s.bits / 8; k++) raw |= d[s.byte + k] << (8 * k);
    else raw = (d[s.byte] >> s.bit) & ((1 << s.bits) - 1);
    if (s.kind === "bool") o[s.name] = raw === 1;
    else if (s.kind === "enum") o[s.name] = s.values[raw];
    else if (s.kind === "int") {
      if (s.signed && raw >= 1 << (s.bits - 1)) raw -= 1 << s.bits;
      o[s.name] = raw === s.invalid_raw ? null : raw * s.scale + (s.offset ?? 0);
    } else o[s.name] = raw;
  }
  return o;
}
const hex = (a) => a.map((b) => b.toString(16).toUpperCase().padStart(2, "0")).join("");
const bytesOf = (h) => h.match(/../g).map((x) => parseInt(x, 16));

function buildCan(golden) {
  const check = hex([...new TextEncoder().encode("123456789")].map((x) => x)).length && crc8([...new TextEncoder().encode("123456789")]);
  if (check !== 0x4b) problem(`can: CRC-8/SAE-J1850 check value is 0x${check.toString(16)}, the catalogue says 0x4B`);
  const vectors = [];
  for (const g of golden) {
    const msg = MESSAGES.find((m) => m.name === g.frame.msg);
    const v = { msg: g.frame.msg, id: g.frame.id, len: g.frame.len, input: g.in, hex: g.frame.hex, firmware_decoded: g.decoded };
    if (msg) {
      if (g.frame.len !== msg.len) problem(`can: ${msg.name} is ${g.frame.len} bytes in the firmware, ${msg.len} in the table`);
      const ours = hex(encodeMsg(msg, g.in));
      if (ours !== g.frame.hex) problem(`can: ${msg.name} ${JSON.stringify(g.in)}: firmware ${g.frame.hex}, table ${ours}`);
      const bytes = bytesOf(g.frame.hex);
      if (e2e(msg.id, bytes) !== bytes[0]) problem(`can: ${msg.name} vector CRC does not verify`);
      v.table_decoded = decodeMsg(msg, bytes);
      if (g.decoded) {
        for (const [k, want] of Object.entries(g.decoded)) {
          if (k === "accepted" || k === "coolant_valid") continue;
          const got = v.table_decoded[k];
          const ok = typeof want === "number" ? got !== null && Math.abs(got - want) <= 1e-3 * Math.max(1, Math.abs(want)) : got === want;
          if (!ok && !(k === "coolant_c" && g.decoded.coolant_valid === false)) problem(`can: ${msg.name} decode ${k}: firmware ${want}, table ${got}`);
        }
      }
    }
    vectors.push(v);
  }
  return {
    $comment: "Generated by tool/protocol/generate.mjs from firmware/src/comms/can_cmd.h/.c and uds.h — do not edit. Every vector's bytes come from the firmware's own encoder and are re-encoded from these tables.",
    source: "firmware/src/comms/can_cmd.h (layout), can_cmd.c (encoders/decoder), uds.h, uds_diag.h, boot/uds_update.h, app/commission.h, diag/uds_capture.h (diagnostics); docs/firmware-contract.md FW-11, FW-08b, FW-24, FW-32, FW-38 to FW-41",
    firmware: { fw_id: fwId(), ...treeInfo() },
    status: "This repository's definition until the OEM DBC binds the frames (can_cmd.h).",
    conventions: {
      byte_order: "little-endian (Intel) for 16-bit fields; bit 0 = least significant bit of the byte",
      frame_format: "CAN FD, 11-bit identifiers; INV_STATUS is 20 bytes (a valid CAN FD length); VCU frames are 8 bytes",
      encode: "raw = (C int cast, toward zero) clamp(x OP k, lo, hi) in IEEE float32, as can_cmd.c does; decode: phys = raw x scale + offset (the firmware uses float32)",
      invalid_raw: "a raw value that means 'not available' (decode gives null)",
    },
    e2e: {
      crc: { name: "CRC-8/SAE-J1850", poly: "0x1D", init: "0xFF", xorout: "0xFF", refin: false, refout: false, check_123456789: "0x4B" },
      crc_coverage: "len bytes: the DataID (CAN ID & 0xFF) in place of byte 0, then bytes 1 .. len-1; the result is written to byte 0",
      alive_counter: "4 bits (byte 1 bits 0-3), +1 per frame modulo 16, one counter per message. Receiver (can_cmd.c counter_ok): the first frame is accepted; delta = (ctr - last) mod 16; delta 0 = a frozen sender, rejected; 1 <= delta <= cal_can_ctr_max_jump (2) accepted; a larger jump is rejected and resynchronises (the next consecutive frame is accepted)",
      staleness: "VCU_CMD: can_stale_ms = 20 ms without an accepted frame => stale (FW-11: ramp to zero; while armed also the battery-path row). VCU_BMS: cal_bms_timeout_ms = 100 ms => zero regen",
      length: "a vehicle frame shorter than 8 bytes is rejected (counted, n_len)",
    },
    messages: MESSAGES,
    diagnostics: {
      transport: "ISO 15765-2 on the CAN-FD diagnostic bus (TX_DL 64). A request to 0x7E1 is a single frame — classic (PCI 0x0L, L = 1..7) or the CAN-FD escape format (0x00, SF_DL <= 62) — or, for the FW-38 programming services and FW-46's 2E FD 46 only (round 23), a first frame (12-bit length <= 4095) and consecutive frames under this ECU's flow control (0x30 BS 4 STmin 0; N_Cr 1 s; a longer first frame: overflow 0x32; another service's segmented request: NRC 0x13). A response on 0x7E9 is a classic single frame (<= 7 bytes, 8-byte frame padded 0xAA), an escape single frame (<= 62 bytes, the smallest CAN-FD length holding it, padded 0xAA) or, for FW-40's longer responses (<= 1024 bytes), a first frame (62 data bytes) and consecutive frames (63) under the tester's flow control (BS, STmin, WAIT <= 16, overflow; N_As = N_Bs = 1 s); a new request aborts a pending response. Periodic data (0x2A) comes as unacknowledged frames [pDID, data] on 0x6E9.",
      request_id: "0x7E1", response_id: "0x7E9", periodic_id: "0x6E9", bus: "diagnostic CAN-FD (HAL_CAN_DIAG)",
      services: [
        { sid: "0x27", name: "SecurityAccess", requests: { "0x01": "requestSeed -> 67 01 seed[4] (a zero seed: already unlocked)", "0x02": "sendKey key[4] -> 67 02" },
          notes: "The key is a build-time hook (TI_UDS_KEY_FN); the default build has none: every seed request is refused with NRC 0x22 (fail closed). A seed answers one key; 3 invalid keys lock SecurityAccess until the MCU restarts (NRC 0x36). One unlock serves one 0xF010 run, one 0x14 clear, one FW-39 start or commit, one FW-46 table write (2E FD 46); RequestDownload needs it without spending it." },
        { sid: "0x31", name: "RoutineControl", requests: {
            "0x01 F0 10": "startRoutine 0xF010 clear the stuck-on QDIS service lock -> 71 01 F0 10 (FW-32)",
            "0x01 F0 20 [routine] [attest BE16]": "FW-39 start a commissioning routine (1 Rs, 2 Ld/Lq, 3 psi/zero; attest 0x4C4B locked rotor, 0x4446 / 0x4452 dyno forward / reverse) -> 71 01 F0 20 [routine]",
            "0x01 F0 20 02 [attest BE16] [k]": "FW-45 the Ld/Lq routine at bias index k = 0..5 (the breakpoint k's current on the d and q axes: a map point; an 8-byte request, the CAN-FD escape single frame) -> 71 01 F0 20 02; k > 5 or another routine: NRC 0x31",
            "0x02 F0 20": "FW-39 stop -> 71 02 F0 20 (NRC 0x24 when none runs)",
            "0x03 F0 20 [index]": "FW-39 results -> 71 03 F0 20 [index] [b1] [b2]: 0x00 state, reason | 0x01 routine, staged mask (bit 5 a FW-45 map point, bit 6 the FW-46 table) | 0x02 (FW-45) the staged points of the Ld map, of the Lq map | 0x10+q verdict, flags (bit 3: a biased run, k in bits 4-6) | 0x20+q value BE16 | 0x30+q uncertainty BE16 | 0x40+k / 0x50+k (FW-45) the Ld / Lq differential inductance at bias k, BE16 0.1 uH; every request of RID 0xF020 is the tool's heartbeat",
            "0x01 F0 21": "FW-39 commit the staged values as a new FW-20 record version -> 71 01 F0 21 (FW-45: a map committed whole — some points only: NRC 0x22; FW-46: with the staged ripple table)",
            "0x01 F0 41 [cfg]": "FW-41 arm the waveform capture -> 71 01 F0 41",
            "0x01 F0 42": "FW-41 trigger it -> 71 01 F0 42",
            "0x01 / 0x03 FF 01": "FW-38 verify the staged image (start / results)",
            "0x01 F0 38": "FW-38 activate the verified image" },
          notes: "0xF010, 0xF020 starts, 0xF021 need the unlock (else NRC 0x33); 0xF010 and 0x14 are refused (NRC 0x22) with HV present or unknown or the bridge armed; FW-39's preconditions (ARMED_ZERO_TORQUE through the normal path, no row, no DTC, a fresh VCU command without enable, the VCU's vehicle speed valid and zero, HV in the SKU window, the rotor's speed for the routine) refuse a start with NRC 0x22 and the reason in results index 0. NRC 0x72 if an NVM write cannot be queued." },
        { sid: "0x19", name: "ReadDTCInformation", requests: {
            "0x01 [mask]": "number of DTCs by status mask -> 59 01 7F 01 [count BE16]",
            "0x02 [mask]": "DTCs by status mask -> 59 02 7F ([DTC 3] [status])*",
            "0x04 [DTC 3] [record]": "snapshot records of a DTC (1..16 the n-th newest, 0xFF all) -> 59 04 [DTC 3] [status] ([record] 01 FD 2F [49 bytes nv_fault_t])*",
            "0x0A": "supported DTCs -> 59 0A 7F ([DTC 3] [status])*" },
          notes: "DTC number = 0xD10000 | id (format 0x01); status availability mask 0x7F (FW-40)." },
        { sid: "0x22", name: "ReadDataByIdentifier", requests: { "[DID BE16] (up to 8)": "62 ([DID] [data])* — telemetry 0xF200-0xF208, identity 0xFD20-0xFD22, root of trust 0xFD23 (kind: 0 none / 1 TEST key / 2 build key / 3 OTP-HSE, then the key id = SHA-256(public key)[0..3] BE), boot record 0xFD24 (FW-38: state, last, target, anti-rollback counter BE32, lkg_valid, last_err; state 0xFF = no record), motor data 0xFD25 (psi uWb BE32, pole pairs, L_d nH BE32, L_q nH BE32, R_s uOhm BE32, n_max rpm BE16, i_d,demag 0.1 A BE16 — the record's scalars, for the tool's ripple-map import over CAN), inductance maps 0xFD26 (the active record: 6 x L_d nH BE32, 6 x L_q nH BE32, i_map 0.1 A BE16 — FW-45's committed map after the key cycle), capture 0xFD40 status / 0xFD41 block, FW-46 ripple table 0xFD46 (36 x int16 BE, 0.01 A: the staged table, else the active record's)" },
          notes: "Unsupported DIDs are left out; none supported: NRC 0x31. Layouts: contract section 10h (FW-40), 10i (FW-41)." },
        { sid: "0x2E", name: "WriteDataByIdentifier", requests: { "FD 41 [block BE16]": "FW-41 seek the capture's block cursor -> 6E FD 41",
            "FD 46 [36 x int16 BE, 0.01 A]": "FW-46 stage the torque-ripple table (75 bytes: a segmented request) for the FW-39 commit -> 6E FD 46" },
          notes: "Nothing else is writable. FD 46: the unlock (NRC 0x33; one write per unlock), no routine running and not in torque (NRC 0x22), exactly 75 bytes (NRC 0x13), each value within cal_ripple_ff_max_a (NRC 0x31)." },
        { sid: "0x2A", name: "ReadDataByPeriodicIdentifier", requests: { "[mode] [pDID]*": "mode 01 every 100 ms, 02 every 10 ms, 03 every 1 ms, 04 stop; pDID = the low byte of 0xF2xx (up to 4) -> 6A" },
          notes: "Frames [pDID, data] on 0x6E9, at most one per 1 ms task, none in a task that sent a response frame; a busy mailbox drops one (FW-40)." },
        { sid: "0x14", name: "ClearDiagnosticInformation", requests: { "[group 3]": "0xFFFFFF all, or one DTC number -> 54" },
          notes: "Gated as the FW-32 routine: unlocked (one clear per unlock, else NRC 0x33), the bridge disarmed, HV absent (else NRC 0x22); the DESAT class, the service lock and every arming-forbidding DTC are kept (a clear releases no latch)." },
        { sid: "0x10", name: "DiagnosticSessionControl", requests: { "0x02": "FW-38 programmingSession -> 50 02 [P2 BE16] [P2* BE16] (arming forbidden until the next power-up: DTC_FW_UPDATE)", "0x01": "defaultSession (in the programming session) -> 50 01 ..." } },
        { sid: "0x34", name: "RequestDownload", requests: { "00 [0xSA] [address] [size]": "FW-38 -> 74 20 [maxNumberOfBlockLength BE16 = 4095]" }, notes: "Needs the unlock." },
        { sid: "0x36", name: "TransferData", requests: { "[counter] [data]": "FW-38, up to 4093 data bytes per block (a segmented request) -> 76 [counter]" }, notes: "Counter from 1, wrapping 0xFF -> 0x00; the previous counter again is a repeat (acknowledged, not written); NRC 0x73 wrong counter, 0x71 more than announced, 0x21 busy." },
        { sid: "0x37", name: "RequestTransferExit", requests: { "": "FW-38 -> 77" } },
        { sid: "0x11", name: "ECUReset", requests: { "0x01": "FW-38 hardReset in the programming session -> 51 01" } },
      ],
      nrc: { "0x11": "serviceNotSupported", "0x12": "subFunctionNotSupported", "0x13": "incorrectMessageLength", "0x21": "busyRepeatRequest", "0x22": "conditionsNotCorrect",
        "0x24": "requestSequenceError", "0x31": "requestOutOfRange", "0x33": "securityAccessDenied", "0x35": "invalidKey", "0x36": "exceededNumberOfAttempts", "0x71": "transferDataSuspended",
        "0x72": "generalProgrammingFailure", "0x73": "wrongBlockSequenceCounter", "0x7F": "serviceNotSupportedInActiveSession" },
      negative_response: "03 7F <sid> <nrc> AA AA AA AA",
    },
    vectors,
  };
}

// ---------------------------------------------------------------- main
const lines = bridge(["--time", "0", "--rate", "1"]);
const hello = lines.find((l) => l.type === "hello");
if (!hello) problem("bridge: no hello line (sim_bridge did not start)");
const golden = bridge(["--golden"]).filter((l) => l.type === "golden");
const out = {
  "params.json": await buildParams(hello),
  "dtcs.json": buildDtcs(hello),
  "can-frames.json": buildCan(golden),
};
const stamp = new Date().toISOString();
let stale = 0;
for (const [name, obj] of Object.entries(out)) {
  const body = JSON.stringify(obj, null, 2);
  const file = join(HERE, name);
  if (CHECK_ONLY) {
    let prev = "";
    try { prev = readFileSync(file, "utf8"); } catch { /* missing */ }
    const strip = (s) => s.replace(/"generated_at": "[^"]*",?\n\s*/g, "");
    if (strip(prev) !== strip(`${JSON.stringify({ generated_at: "", ...obj }, null, 2)}\n`)) {
      problem(`${name} on disk is not what the sources give now (run: node tool/protocol/generate.mjs)`);
      stale++;
    }
  } else {
    writeFileSync(file, `${JSON.stringify({ generated_at: stamp, ...obj }, null, 2)}\n`);
  }
  void body;
}
const p = out["params.json"], d = out["dtcs.json"], c = out["can-frames.json"];
console.log(`params.json      ${p.parameters.length} writable cal_* parameters, ${p.constants.length} constants, ${p.cross_field_rules.rules.length} cross-field rules`);
console.log(`dtcs.json        ${d.dtcs.length} DTCs (${d.never_set.length} declared but never set: ${d.never_set.join(", ")})`);
console.log(`can-frames.json  ${c.messages.length} messages + UDS, ${c.vectors.length} firmware vectors re-encoded from the tables`);
console.log(`firmware         ${p.firmware.fw_id}, ${p.firmware.git_head}${p.firmware.firmware_files_modified_since_head.length ? ` + ${p.firmware.firmware_files_modified_since_head.length} modified file(s)` : ""}`);
if (problems.length) {
  console.error(`\n${problems.length} problem(s):\n  - ${problems.join("\n  - ")}`);
  process.exit(1);
}
console.log(CHECK_ONLY ? "check: the exports are current and verified" : "written and verified");
