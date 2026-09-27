#!/usr/bin/env node
// dtc-table.mjs — the DTC number/description table (FW-40, round 23), generated from src/comms/dtc.h.
//   node tools/dtc-table.mjs           the table as JSON on stdout (the service tool's)
//   node tools/dtc-table.mjs --c       rewrite src/comms/dtc_table.h (the C table the unit test checks)
//   node tools/dtc-table.mjs --check   exit 1 when src/comms/dtc_table.h is not what --c would write
//   node tools/dtc-table.mjs --md      the table as Markdown rows (docs/firmware-contract.md §10h)
// Number: the enumerator's position p gives the 3-byte DTC 0xD10000 | p — dtc_code() in src/comms/dtc.c, reported by UDS
// 0x19 with DTCFormatIdentifier 0x01 (ISO_14229-1_DTCFormat). The list is append-only: an entry inserted in the middle
// renumbers every DTC after it, so explicit values are refused (DTC_NONE = 0 aside). Description: the text below, else
// the enumerator's comment in dtc.h; a DTC with neither is an error.

import { readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const ROOT = join(dirname(fileURLToPath(import.meta.url)), "..");
const SRC = join(ROOT, "src", "comms", "dtc.h");
const OUT_C = join(ROOT, "src", "comms", "dtc_table.h");

const DESC = {
  DTC_FS26_PROGID: "Safety SBC (FS26) OTP variant: M_PROGID unbound or not the procured one - no arming (FW-12)",
  DTC_FS26_OTP_CORRUPT: "Safety SBC (FS26) OTP CRC monitor reports corruption - no arming (FW-12)",
  DTC_FS26_DEBUG_MODE: "Safety SBC (FS26) in debug mode at boot - no arming (FW-12)",
  DTC_FS26_INIT_READBACK: "Safety SBC (FS26) INIT registers or their complements read back wrong - no arming (FW-12)",
  DTC_FS26_SPI: "Safety SBC (FS26) SPI communication failed at boot - no arming (FW-12)",
  DTC_FS26_WD: "Safety SBC (FS26) watchdog answer refused or not sent (FW-12)",
  DTC_FS26_RELEASE: "Safety SBC (FS26) did not release FS0B/FS1B - no arming (start-up step 4)",
  DTC_FS1B_SHORT_HIGH: "FS1B / FAULT_OUT line shorted to KL30 - no arming until repaired",
  DTC_FS26_GPIO1_OTP: "Gate power up before start-up step 6: FS_GPIO1 slotted in the FS26 OTP - no arming",
  DTC_HWID_OPEN: "HW_ID input open (power-board identity resistor missing) - no arming (FW-01)",
  DTC_HWID_SHORT: "HW_ID input shorted to ground - no arming (FW-01)",
  DTC_HWID_UNKNOWN: "HW_ID reading outside every SKU window or unstable - no arming (FW-01)",
  DTC_SKU_MISMATCH: "HW_ID, parameter set and calibration record disagree on the SKU - no gate enable (FW-02)",
  DTC_CALIB_INVALID: "Calibration record missing or wrong (layout, CRC, range, SKU, serial, motor ID) - no torque (FW-20)",
  DTC_PARAMS_INVALID: "Parameter set fails its range or consistency check - no arming",
  DTC_PWM_LOCK: "PWM fault configuration not read back, or its register lock not set - no arming (FW-24)",
  DTC_DESAT_HS: "High-side gate driver desaturation (short circuit) (FW-15)",
  DTC_DESAT_LS: "Low-side gate driver desaturation (short circuit) (FW-15)",
  DTC_DESAT_REPEAT: "Second desaturation in the key cycle - latched, no further retry (FW-15)",
  DTC_DESAT_PENDING_BOOT: "Gate driver fault line low at boot: a desaturation from before the reset",
  DTC_FLT_RECOVERY_FAIL: "Gate driver fault latch not reset by the FW-15 recovery sequence",
  DTC_OVERCURRENT: "Phase overcurrent: hardware compare or software backstop, outside the ASC-entry window (FW-05)",
  DTC_OVERVOLTAGE: "DC-link overvoltage: hardware compare, active short circuit requested (FW-06)",
  DTC_ISNS_OPEN: "Phase current sensor output open (below its valid window) (FW-05)",
  DTC_ISNS_RANGE: "Phase current sensor output outside its valid window (FW-05)",
  DTC_ISNS_SUM: "Phase currents do not sum to zero (FW-05)",
  DTC_ISNS_OFFSET: "Phase current zero-current offset outside its tolerance at key-on",
  DTC_ISNS_STALE: "Phase current samples missing or stale: lost triplets or a stopped current loop (FW-27, FW-31)",
  DTC_VDC_DISAGREE: "DC-link voltage channels disagree beyond 5 % (FW-07)",
  DTC_VOFS: "DC-link sense offset reference (VOFS) out of range (FW-07)",
  DTC_V5GD: "Gate-drive side 5 V supply (V5GD) out of range - pulses off, short circuit cleared",
  DTC_VDC_FAILSAFE: "DC-link voltage channel in its isolated amplifier's fail-safe state (FW-07)",
  DTC_VDC_STALE: "DC-link voltage samples stale (FW-07)",
  DTC_VDC_BMS: "DC-link voltage leaving the BMS pack voltage with the contactors closed (battery path)",
  DTC_RSLV_AMPLITUDE: "Resolver sin/cos amplitude outside its window (FW-10)",
  DTC_RSLV_EXCITATION: "Resolver excitation outside its band at the monitor or the winding (FW-10, FW-30)",
  DTC_RSLV_TRACKING: "Resolver tracking error above its limit (FW-10)",
  DTC_RSLV_ACCEL: "Resolver acceleration implausible (FW-10)",
  DTC_RSLV_RATE: "Resolver angle rate disagrees with the voltage-model speed (FW-10)",
  DTC_TEMP_MODULE: "Power-module temperature sensor invalid: open, short or rate (FW-13)",
  DTC_TEMP_BOARD: "Board temperature sensor invalid (FW-13)",
  DTC_TEMP_MOTOR: "Motor temperature sensor invalid (FW-13)",
  DTC_OVERTEMP: "Hottest module temperature at the end of its derating band - no torque left (FW-04, FW-13)",
  DTC_HVIL_OPEN: "HVIL loop open (FW-09)",
  DTC_HVIL_SHORT: "HVIL loop shorted to ground or a supply, or implausible (FW-09)",
  DTC_CAN_TIMEOUT: "Vehicle command (VCU_CMD) missing or older than 20 ms (FW-11)",
  DTC_BMS_TIMEOUT: "BMS limits (VCU_BMS) missing or stale - no regeneration (FW-11)",
  DTC_CAN_E2E: "Vehicle CAN frame refused by its E2E check: CRC, alive counter or length (FW-11)",
  DTC_QDIS_STUCK_OFF: "Active discharge commanded but the link does not discharge (FW-17, FW-18)",
  DTC_QDIS_STUCK_ON: "Active discharge conducting without a command - service lock, do not re-energise (FW-26)",
  DTC_QDIS_RATE_LIMIT: "Active discharge requests beyond the rate limit (FW-17)",
  DTC_TAU_MISMATCH: "Key-off discharge time constant more than 20 % off the SKU's capacitor bank (FW-02)",
  DTC_PRECHARGE_PLATEAU: "Precharge plateau below 97.5 % of the pack - arming refused (FW-19)",
  DTC_PRECHARGE_TAU: "Precharge time constant implausible - arming refused (FW-19)",
  DTC_PRECHARGE_TIMEOUT: "Precharge not complete in time - arming refused (FW-19)",
  DTC_SELFTEST_FAIL: "Gate shutdown-path self-test step mismatch - no arming (FW-16)",
  DTC_SELFTEST_NO_PASS: "Gate self-test skipped with no stored pass - no arming (FW-16)",
  DTC_GATE_POWER: "Gate-drive supply not ready in time, or lost (RDY) (FW-14)",
  DTC_SPO_ENERGY: "Pulses held off with neither energy rule proven: no safe state proven (FW-08b)",
  DTC_CTRL_NONFINITE: "Non-finite value caught in the control path before the PWM",
  DTC_NVM: "Data-flash (NVM) write failed",
  DTC_SENSOR_SELFTEST: "Sensor self-test at start-up failed (start-up step 3)",
  DTC_GAINS: "No current-loop gain set within the crossover ceiling - no arming",
  DTC_ARM_EVIDENCE: "Arming evidence missing: fault route unbound or no valid EOL/HIL validation record - no arming (FW-24)",
  DTC_TORQUE_INFEASIBLE: "No voltage-feasible current even at zero torque: zero torque, speed limit requested (FW-25, FW-37)",
  DTC_ISNS_STUCK: "Phase current channel shows no current where its reference asks for it: stuck sensor (FW-05)",
  DTC_RSLV_STALE: "No coherent resolver frame within the hold time - angle withdrawn (FW-28)",
  DTC_RSLV_SWG_SAT: "Resolver excitation trim at its top code with the monitor still below the setpoint (FW-30)",
  DTC_SERVICE_LOCK_CLEARED: "Service lock cleared by the UDS routine 0xF010 - a record, not a failure (FW-32)",
  DTC_LV_OVERVOLTAGE: "LV supply (VSUP) above 20 V - information; first and last time give the duration (FW-33)",
  DTC_LV_OV_SUSTAINED: "LV overvoltage longer than its band allows - orderly ramp to zero torque (FW-33)",
  DTC_LV_VSUP_UNKNOWN: "No LV supply reading from the FS26 AMUX - LV supervision off (information, FW-33)",
  DTC_RSLV_REACQUIRED: "Resolver frame ring re-acquired, or its producer restarted - information (FW-35)",
  DTC_TORQUE_POSTCOND: "Torque-to-current solver refused its own result: no current reference, control lost (FW-37)",
  DTC_OVERSPEED: "Measured speed in the overspeed warning or trip band (FW-42)",
  DTC_FW_UPDATE: "Programming session entered (firmware update) - no arming until the next power-up; a record (FW-38)",
  DTC_FW_FALLBACK: "Bootloader refused or abandoned the last activation, or restored the last good image (FW-38)",
  DTC_MC_ABORTED: "Commissioning routine aborted - its reason is in the routine's results; a record (FW-39)",
  DTC_MC_CAL_WRITTEN: "Commissioning wrote a new calibration record version - effective at the next key cycle (FW-39)",
  DTC_ASC_OC_TRANSIENT: "Phase overcurrent inside the ASC-entry window: the short-circuit transient - information (FW-05)",
};

const STATUS_BITS = [
  ["testFailed", "the monitor reported a failure and no pass since (most monitors report failures only)"],
  ["testFailedThisOperationCycle", "failed since this key cycle started"],
  ["pendingDTC", "failed since the last clear or power-up (not aged by passing cycles)"],
  ["confirmedDTC", "confirmed at the first failure (every monitor debounces before it reports)"],
  ["testNotCompletedSinceLastClear", "neither failed nor passed since the last clear or power-up"],
  ["testFailedSinceLastClear", "failed since the last clear or power-up"],
  ["testNotCompletedThisOperationCycle", "neither failed nor passed in this key cycle"],
];

function parse(text) {
  const body = text.match(/typedef enum \{([\s\S]*?)\} dtc_id_t;/);
  if (!body) throw new Error("dtc.h: no `typedef enum { ... } dtc_id_t;`");
  const rows = [];
  let open = null; // the row whose comment continues on the next line
  for (const line of body[1].split("\n")) {
    const m = line.match(/^\s*(DTC_[A-Z0-9_]+)\s*(?:=\s*([^,/]+?))?\s*,?\s*(.*)$/);
    if (m && !open) {
      if (m[1] === "DTC_COUNT") break;
      if (m[2] !== undefined && !(m[1] === "DTC_NONE" && m[2].trim() === "0")) {
        throw new Error(`dtc.h: ${m[1]} = ${m[2]}: explicit values renumber the DTCs (the list is append-only)`);
      }
      const row = { name: m[1], comment: "" };
      rows.push(row);
      const c = m[3].match(/^\/\*(.*)$/);
      if (c) {
        const end = c[1].indexOf("*/");
        row.comment = end >= 0 ? c[1].slice(0, end) : c[1];
        open = end >= 0 ? null : row;
      }
      continue;
    }
    if (open) {
      const end = line.indexOf("*/");
      open.comment += " " + (end >= 0 ? line.slice(0, end) : line);
      if (end >= 0) open = null;
    }
  }
  if (rows.length < 2 || rows[0].name !== "DTC_NONE") throw new Error("dtc.h: DTC_NONE must come first");
  return rows.slice(1).map((r, i) => {
    const note = r.comment.replace(/\s+/g, " ").trim().replace(/^round \d+(?: \([^)]*\))?:\s*/i, "");
    const text = DESC[r.name] ?? note;
    if (!text) throw new Error(`dtc.h: ${r.name} has no description (add it to DESC in tools/dtc-table.mjs or comment it)`);
    const id = i + 1;
    const number = 0xd10000 | id;
    return { id, name: r.name, number, hex: number.toString(16).toUpperCase().padStart(6, "0"), description: text };
  });
}

const cstr = (s) =>
  '"' +
  [...s]
    .map((ch) => {
      const cp = ch.codePointAt(0);
      if (ch === '"' || ch === "\\") return "\\" + ch;
      if (cp < 0x20) return " ";
      if (cp < 0x80) return ch;
      return cp <= 0xffff ? "\\u" + cp.toString(16).padStart(4, "0") : "\\U" + cp.toString(16).padStart(8, "0");
    })
    .join("") +
  '"';

function header(dtcs) {
  const lines = dtcs.map((d) => `    [${d.name}] = {"${d.name}", 0x${d.hex}u, ${cstr(d.description)}},`);
  return `/* dtc_table.h — GENERATED by tools/dtc-table.mjs from src/comms/dtc.h (FW-40): do not edit. After any change of the
 * DTC list run \`node tools/dtc-table.mjs --c\`; until then the unit test uds_diag: every_dtc_has_a_number_and_a_description
 * fails. One row per DTC, keyed by its enumerator: the UDS DTC number (dtc_code(): 0xD10000 | id, ISO 14229-1 DTC
 * format 0x01) and the service tool's description — the table \`node tools/dtc-table.mjs\` prints as JSON. Test-only:
 * the image carries the numbers (dtc_code), not the texts. */
#ifndef DTC_TABLE_H
#define DTC_TABLE_H

#include "dtc.h"

typedef struct {
    const char *name;
    uint32_t number;
    const char *text;
} dtc_table_row_t;

#define DTC_TABLE_N ${dtcs.length + 1}u /* DTC_COUNT when generated */

static const dtc_table_row_t DTC_TABLE[DTC_COUNT] = {
${lines.join("\n")}
};

#endif /* DTC_TABLE_H */
`;
}

const dtcs = parse(readFileSync(SRC, "utf8"));
const arg = process.argv[2] ?? "";
if (arg === "--c") {
  writeFileSync(OUT_C, header(dtcs));
  console.error(`dtc-table: ${dtcs.length} DTCs -> src/comms/dtc_table.h`);
} else if (arg === "--check") {
  let have = "";
  try {
    have = readFileSync(OUT_C, "utf8");
  } catch {
    have = "";
  }
  if (have !== header(dtcs)) {
    console.error("dtc-table: src/comms/dtc_table.h is not generated from the present dtc.h: node tools/dtc-table.mjs --c");
    process.exit(1);
  }
  console.error(`dtc-table: src/comms/dtc_table.h matches dtc.h (${dtcs.length} DTCs)`);
} else if (arg === "--md") {
  const rows = dtcs.map((d) => `| ${d.id} | 0x${d.hex} | \`${d.name}\` | ${d.description} |`);
  process.stdout.write(["| Id | DTC number | Name | Description |", "|---|---|---|---|", ...rows].join("\n") + "\n");
} else if (arg === "") {
  const out = {
    generated_by: "firmware/tools/dtc-table.mjs",
    source: "firmware/src/comms/dtc.h",
    dtc_format_identifier: 1,
    number_rule: "0xD10000 | position in dtc_id_t (dtc_code())",
    status_availability_mask: 0x7f,
    status_bits: STATUS_BITS.map(([name, meaning], bit) => ({ bit, mask: 1 << bit, name, meaning })),
    dtcs,
  };
  process.stdout.write(JSON.stringify(out, null, 2) + "\n");
} else {
  console.error("usage: node tools/dtc-table.mjs [--c | --check | --md]");
  process.exit(2);
}
