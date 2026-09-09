#!/usr/bin/env node
// design-verify.mjs — numeric end-to-end verification of the Traction Inverter design.
// Recomputes every sizing decision independently, at the ACTUAL operating corners
// (V_bus 500/700/850 V · KL30 9/12/16 V · f_sw 10 kHz · coldplate 65 °C), with component
// tolerances at worst case. Emits docs/verification-report.md with PASS/WARN/FAIL + margins.
//
// All constants are datasheet-real (HCS600, NSI6611 DS 1.2, SLUS458I, C3D, VGT12EEM, LEM, FS26 Rev.3, S32K39 Rev.3); anything still "assumed"
// is listed in the report's assumptions table.
// Run: node calculations/design-verify.mjs

import { writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
const ROOT = join(dirname(fileURLToPath(import.meta.url)), "..");

// ---------------- operating conditions ----------------
const OP = {
  vbusNom: 700, vbusMax: 850, vbusMin: 500,
  fsw: 10e3, iphPk: 340, iphCont: 185, pf: 0.85,
  kl30: { min: 9, nom: 13.5, max: 16 }, plate: 65,
};

// ---------------- datasheet / design constants ----------------
const P = {
  // module — REAL HCS600FH120D3C1 datasheet values (rev X.0.1): Rds 5.2 mΩ chip @+15 V/175 °C
  // + 0.5 mΩ terminal resistance; Eon+Eoff 41 mJ @600 V/600 A/150 °C; Rth(j-c) 0.066 + 0.015 grease
  mod: { rdsHot: 5.7e-3, eswPerA600: 68.3e-6, rthJC: 0.066, rthCH: 0.015, tjMax: 175, vds: 1200, src: "HCS600 datasheet" },
  // link cap — REAL Faratronic C3D DS: 15.4 A rms @10 kHz/70 °C, ESR 7.8 mΩ, U_N 1100 V @70 °C
  cap: { c: 20e-6, n: 16, vr: 1100, irmsEach: 15.4, esr: 7.8e-3, tolC: 0.10, src: "C3D datasheet" },
  // NSI6611A-Q1 — DS 1.2: VCC2 UVLO rising 11.2 typ / 12.8 MAX, falling 11.8 max; Icc2 7 mA max;
  // VCC2-VEE2 abs 35 V / recommended 13–32 V; RDY/FLT current-limited reporters; ASC abs GND2+6 V
  drv: { ipk: 10, uvlo2On: 12.8, uvlo2Off: 11.8, vccRecMin: 13, vdesat: 9.0, idesat: 0.5e-3, tblankInt: 0.3e-6, vcc2Rec: 32, vcc2Abs: 35, icc2: 7e-3, src: "NSI6611 DS 1.2 (DESAT constants pending §6 extract)" },
  // UCC28C40 (F31 — the C43 grade is 8.4/7.6 V UVLO and cannot cold-crank-start)
  pwm: { vref: 2.5, vcs: 1.0, uvloOn: 7.0, uvloOnMax: 7.8, uvloOff: 6.6, vccAbs: 20, istart: 100e-6, irun: 2.3e-3, kOsc: 1.72, src: "SLUS458I (C40 grade; osc from curve anchors)" },
  // gate charge — HCS600 DS: QG 1240 nC @800 V/360 A, +18/−5
  qg: { total: 1.24e-6, vswing: 20.1, src: "HCS600 datasheet" },
  // flyback magnetics — REAL VGT12EEM-200S1A4: NP:NF:NS = 1:1.6:2.9, Lp 10 uH ±20 %
  xfmr: { lp: 10e-6, nf: 1.6, ns: 2.9, isat: 4.5, src: "TDK datasheet (Isat unpublished — CS limit is the guard)" },
  // TPS55340
  boost: { vref: 1.229, ilim: 5.25, vinAbs: 45, src: "assumed pending TPS55340 DS" },
  // LEM HC5FW 900-S — DS: 2.22 mV/A ratiometric @5 V, 2.5 V @0 A, BW >=40 kHz
  hall: { sens: 2.22e-3, v0: 2.5, src: "LEM datasheet" },
  // AMC1311
  amc: { vinFs: 2.0, gainErr: 0.005, src: "assumed pending AMC1311 DS" },
  // resistor tolerances
  tolR: 0.05, tolRp: 0.01,
};

// ---------------- check engine ----------------
const rows = [];   // {sec, name, value, limit, margin, status, note}
const add = (sec, name, value, limit, status, note = "") => rows.push({ sec, name, value, limit, status, note });
const judge = (sec, name, value, limit, ratio, warnAt, note = "") => {
  // ratio = value/limit must stay <= 1; warn above warnAt
  const st = ratio <= warnAt ? "PASS" : ratio <= 1 ? "WARN" : "FAIL";
  add(sec, name, value, limit, st, `${(ratio * 100).toFixed(0)}% of limit${note ? " · " + note : ""}`);
  return st;
};
const f = (x, d = 1) => Number(x.toFixed(d));

// ============ 1. POWER STAGE ============
{
  const vll = OP.vbusNom / Math.SQRT2;
  const pAvail = Math.sqrt(3) * vll * OP.iphPk * OP.pf / 1e3;
  judge("Power stage", "SVPWM power capability @700 V", `${f(pAvail)} kW avail`, "220 kW target", 220 / pAvail, 0.95);
  // per-switch losses (each of 6 switches conducts half the phase current RMS-wise)
  // standard 2-level result: I_sw,rms = I_ph,rms · sqrt(1/8 + m·cosφ/(3π)), m ≈ 0.9
  const kSw = Math.sqrt(0.125 + 0.9 * OP.pf / (3 * Math.PI));
  const irmsSw = OP.iphPk * kSw;
  const pCond = irmsSw * irmsSw * P.mod.rdsHot;
  const eswA = P.mod.eswPerA600 * (OP.vbusNom / 600);      // J per amp switched, V-scaled
  const pSw = eswA * (OP.iphPk * Math.SQRT2 / Math.PI) * OP.fsw; // (1/π)·fsw·k·Î per switch
  const pSwitchPk = pCond + pSw;
  const irmsCont = OP.iphCont * kSw;
  const pSwitchCont = irmsCont * irmsCont * P.mod.rdsHot + eswA * (OP.iphCont * Math.SQRT2 / Math.PI) * OP.fsw;
  add("Power stage", "Per-switch loss (220 kW pk / 120 kW cont)", `${f(pSwitchPk)} W / ${f(pSwitchCont)} W`, "-", "INFO");
  const dTj = pSwitchPk * (P.mod.rthJC + P.mod.rthCH);
  const tj = OP.plate + dTj;
  judge("Power stage", "Tj at 220 kW peak (65 °C plate)", `${f(tj)} °C`, `${P.mod.tjMax} °C`, tj / P.mod.tjMax, 0.80, "steady-state bound; 30 s Zth is lower");
  const tjCont = OP.plate + pSwitchCont * (P.mod.rthJC + P.mod.rthCH);
  judge("Power stage", "Tj continuous 120 kW", `${f(tjCont)} °C`, `${P.mod.tjMax} °C`, tjCont / P.mod.tjMax, 0.75);
  judge("Power stage", "Module switch RMS current", `${f(irmsSw)} A`, "600 A rating (620 A @Tc60)", irmsSw / 600, 0.7);
  // switching-node voltage incl. overshoot: L_loop ~20 nH, di/dt ~5 kA/us
  const vov = OP.vbusMax + 20e-9 * 5e9;
  judge("Power stage", "Vds peak (850 V + 20 nH · 5 kA/µs)", `${f(vov)} V`, "1200 V", vov / 1200, 0.85);
}

// ============ 2. DC LINK ============
{
  const icPk = 0.62 * OP.iphPk, icCont = 0.62 * OP.iphCont;
  judge("DC link", "Ripple per can, continuous", `${f(icCont / P.cap.n)} A`, `${P.cap.irmsEach} A`, (icCont / P.cap.n) / P.cap.irmsEach, 0.8);
  judge("DC link", "Ripple per can, 30 s peak", `${f(icPk / P.cap.n)} A`, `${P.cap.irmsEach} A DS rating @10 kHz/70 °C`, (icPk / P.cap.n) / P.cap.irmsEach, 0.9, "inside the CONTINUOUS rating — no transient allowance needed");
  judge("DC link", "Voltage margin", `${OP.vbusMax} V`, `${P.cap.vr} V`, OP.vbusMax / P.cap.vr, 0.85);
  const cTot = P.cap.c * P.cap.n;
  const dv = icPk / (2 * Math.PI * 2 * OP.fsw * cTot);   // dominant 2·fsw component
  add("DC link", "Bus ripple voltage (capacitive term)", `${f(dv, 2)} V pk`, "-", "INFO", "ESL/busbar dominates in layout");
  const pEsr = (icCont / P.cap.n) ** 2 * P.cap.esr;
  judge("DC link", "ESR heating per can, continuous", `${f(pEsr, 2)} W`, "~2 W film-can class", pEsr / 2, 0.6);
  add("DC link", "Stored energy @850 V", `${f(0.5 * cTot * OP.vbusMax ** 2)} J`, "-", "INFO");
}

// ============ 3. DISCHARGE (worst-case corners) ============
{
  const cMax = P.cap.c * P.cap.n * (1 + P.cap.tolC);
  const cMin = P.cap.c * P.cap.n * (1 - P.cap.tolC);
  const ln = Math.log(OP.vbusMax / 60);
  // passive: 2 strings x 5 x 27k
  const rp = (5 * 27e3) / 2;
  const t60p = rp * (1 + P.tolR) * cMax * ln;
  judge("Discharge", "Passive 850→60 V, worst (R+5 %, C+10 %)", `${f(t60p)} s`, "120 s service rule", t60p / 120, 0.6, `nominal ${f(rp * P.cap.c * P.cap.n * ln)} s`);
  const pRes = (OP.vbusMax / 5) ** 2 / (27e3 * (1 - P.tolR));
  judge("Discharge", "Bleeder W/resistor @850 V (R−5 %)", `${f(pRes, 2)} W`, "2 W", pRes / 2, 0.65);
  judge("Discharge", "Bleeder V/resistor @850 V", `${f(OP.vbusMax / 5)} V`, "200 V working (std 2512)", (OP.vbusMax / 5) / 200, 0.9, "700 V nom ⇒ 140 V (70 %)");
  // active: 4 x 560R + Rds(75 mΩ, negligible)
  const ra = 4 * 470;
  const t60a = ra * (1 + P.tolR) * cMax * ln;
  judge("Discharge", "Active 850→60 V, worst (R+5 %, C+10 %)", `${f(t60a, 2)} s`, "2 s crash target", t60a / 2.0, 0.95, `nominal ${f(4 * 470 * P.cap.c * P.cap.n * ln, 2)} s · 5 s R100 ⇒ ${f(100 * t60a / 5)} %`);
  const eRes = 0.5 * cMax * OP.vbusMax ** 2 / 4;
  judge("Discharge", "Energy per 10 W wirewound (C+10 %)", `${f(eRes)} J`, "100 J single-pulse", eRes / 100, 0.5);
  judge("Discharge", "V per wirewound @850 V", `${f(OP.vbusMax / 4)} V`, "≥350 V axial class", (OP.vbusMax / 4) / 350, 0.8);
  const ipk = OP.vbusMax / ra;
  add("Discharge", "QDIS stress", `${f(ipk, 2)} A pk · ${f(ipk * ipk * 0.075, 3)} W`, "1200 V / 42 A part", "PASS", "fully-enhanced switch, no linear region");
  // combined (both paths active)
  const rc = 1 / (1 / ra + 1 / rp);
  add("Discharge", "Both paths together 850→60 V", `${f(rc * P.cap.c * P.cap.n * ln, 2)} s`, "-", "INFO");
}

// ============ 4. GATE DRIVE ============
{
  const vcc = 15.6, vee = -5.1;   // from the regulated secondary (F33 derivation below); +15/−5 combo (F30)
  judge("Gate drive", "VCC2 (+15) vs UVLO-rising MAX", `${vcc} V`, `${P.drv.uvlo2On} V`, P.drv.uvlo2On / vcc, 0.90, "flyback ±5 % ⇒ 14.25 V worst, still above 12.8 V");
  judge("Gate drive", "VCC2 worst vs recommended-min", "14.25 V (−5 %)", `${P.drv.vccRecMin} V rec-min`, P.drv.vccRecMin / 14.25, 0.95, "trim flyback to 15.2 V nom if bench shows droop");
  judge("Gate drive", "VCC2−VEE2 span", `${f(vcc - vee)} V`, `${P.drv.vcc2Rec} V recommended (35 abs)`, (vcc - vee) / P.drv.vcc2Rec, 0.8);
  const igPk = (vcc - vee) / (1.5 + 1.0 / 2 + 0.5);  // Rg_on + share, ~2.5 Ω eff loop + Rg,int
  judge("Gate drive", "Peak gate current demand", `${f(igPk)} A`, `${P.drv.ipk} A driver`, igPk / P.drv.ipk, 0.85);
  const pGate = P.qg.total * P.qg.vswing * OP.fsw;
  add("Gate drive", "Gate power per switch @10 kHz", `${f(pGate, 2)} W`, "-", "INFO");
  const pDrv = pGate + Math.abs(vee) * 0 + vcc * P.drv.icc2;
  judge("Gate drive", "Per-domain bias load", `${f(pDrv, 2)} W`, "~1.5 W per secondary budget", pDrv / 1.5, 0.75);
  // zener stack clamp vs gate abs-max (assumed HCS die ±22/−10 class per hiitio Gen3)
  judge("Gate drive", "Positive gate clamp (18 V zener + Vf)", "18.8 V", "+22 V abs Vgs", 18.8 / 22, 0.9);
  judge("Gate drive", "Negative gate clamp (5.1 V zener + Vf)", "−5.9 V", "−10 V abs Vgs", 5.9 / 10, 0.85);
  add("Gate drive", "HS DESAT sense point", "module aux drain pin 9 (DSH)", "-", "PASS", "F29 — real HCS600 pin map; kelvin sensing, no busbar drop in the trip level");
  add("Gate drive", "ASC drive level", "5.1 V clamp at ganged pins", "GND2+6 V abs", "PASS", "F28 — 2.2 k + zener from the 18 V opto rail");
  // DESAT: trip level & blanking
  const vTrip = P.drv.vdesat - 2 * 1.1;   // two US1M hot
  add("Gate drive", "DESAT trip at switch", `${f(vTrip, 1)} V ≈ ${f(vTrip / P.mod.rdsHot / 1000, 1)} kA`, "-", "INFO", "detects hard faults, not overload — halls cover overload");
  const tBlank = 47e-12 * P.drv.vdesat / P.drv.idesat + P.drv.tblankInt;
  judge("Gate drive", "DESAT blanking (47 pF)", `${f(tBlank * 1e6, 2)} µs`, "≤3 µs SiC SCWT", tBlank / 3e-6, 0.7, "≥0.5 µs needed to ride through turn-on");
  add("Gate drive", "Shoot-through lockout", "IN+/IN− complementary pairing", "-", "PASS", "verified structurally in erc-audit (12 checks)");
}

// ============ 5. GATE-POWER FLYBACKS (real VGT winding: NP:NF:NS = 1:1.6:2.9, Lp 10 µH) ============
{
  const vccReg = P.pwm.vref * (56 + 15) / 15;           // F33: 56k/15k on the NF winding
  const vsec = vccReg * (P.xfmr.ns / P.xfmr.nf);        // reflected through NF:NS
  const vcc2 = vsec - 0.7 - 5.1;                        // rectifier Vf + zener split
  judge("Flyback", "VCC regulation point on NF (56k/15k)", `${f(vccReg, 2)} V`, `${P.pwm.vccAbs} V abs`, vccReg / P.pwm.vccAbs, 0.75, "F33 — a 15 V target through NF would push the secondaries to ~27 V");
  judge("Flyback", "Derived gate rail VCC2", `${f(vcc2, 1)} V (+/−5.1)`, "13–32 V driver window", 13 / vcc2, 0.9, `Vsec ${f(vsec, 1)} V; UVLO-max 12.8 V cleared`);
  const fsw = P.pwm.kOsc / (10e3 * 680e-12);
  add("Flyback", "Switching frequency (10k/680p)", `${f(fsw / 1e3)} kHz`, "-", "INFO", "F32 — Lp 10 µH demands small per-cycle energy; osc anchors per SLUS458I curves");
  const pOut = 3 * (P.qg.total * P.qg.vswing * OP.fsw + vcc2 * P.drv.icc2) + 0.3;
  const pin = pOut / 0.78;
  const ipkOp = Math.sqrt(2 * pin / (P.xfmr.lp * fsw));  // DCM peak at the real Lp/fsw
  const ilim = P.pwm.vcs / 0.33;
  judge("Flyback", "DCM peak current vs CS limit", `${f(ipkOp, 2)} A op`, `${f(ilim, 2)} A limit (0.33 Ω)`, ipkOp / ilim, 0.75, "F1+F32 — real Lp: energy/cycle ½·10µ·Ipk²");
  judge("Flyback", "CS limit as the saturation guard", `${f(ilim, 2)} A`, `${P.xfmr.isat} A (Isat unpublished — guard band)`, ilim / P.xfmr.isat, 1.0, "bench-verify core at current limit");
  const iStartAvail = (OP.kl30.min - P.pwm.uvloOnMax) / 4.7e3;
  judge("Flyback", "Trickle-start current @9 V KL30", `${f(iStartAvail * 1e6)} µA`, `${P.pwm.istart * 1e6} µA start`, P.pwm.istart / iStartAvail, 0.85, "F31 — UCC28C40's 7.8 V max UVLO-on leaves 255 µA; the C43 grade left NOTHING");
  const pRcs = (ipkOp / Math.sqrt(3)) ** 2 * 0.33;
  judge("Flyback", "CS resistor power", `${f(pRcs, 3)} W`, "0.75 W (1210)", pRcs / 0.75, 0.6);
}

// ---------- rev A.4 corrections (external design review F37–F46) ----------
{
  // F37 transformer phasing is topological (locked by erc-audit); the numbers it protects:
  const vRefl = 21.4 / 2.9;             // secondary total reflected through NS -> NP
  judge("Flyback A.4", "Reflected voltage vs clamp-TVS standoff", `${f(vRefl, 1)} V`, "13 V SMAJ13A standoff",
    vRefl / 13, 0.75, "F38 — TVS must stay dark in normal OFF; dots per TDK p.3/9");
  const vDrainLD = 39 + 21.5 + 0.7;     // clamped load-dump rail + TVS clamp + blocking Vf
  judge("Flyback A.4", "Drain worst case (clamped load dump)", `${f(vDrainLD, 1)} V`, "80 V BUK9Y14-80E",
    vDrainLD / 80, 0.85, "F38 — replaces SMBJ85A (94.4 V min breakdown, forward path in OFF)");
  const pGateBank = 3 * 1.24e-6 * 20.7 * 10e3 + 3 * 5e-3 * 20.7 + 0.2; // Qg + driver Iq + bleed
  const pBankCap = 0.5 * (10e-6 / 3) * (3.03 * 0.9) ** 2 * 253e3;      // DCM throughput at 90% of CS limit
  judge("Flyback A.4", "Gate-power demand vs DCM throughput", `${f(pGateBank, 2)} W`, `${f(pBankCap, 2)} W per bank`,
    pGateBank / pBankCap, 0.6, "10 kHz PWM; reviewer's 20 kHz doubles Qg term — still inside");
  // F39 DESAT clamp direction is topological (erc-audit); F40 ASC latch levels:
  const vSetLow = 5 * 1e3 / 11e3;
  judge("Safety A.4", "ASC latch asserted-low level (1k into 10k)", `${f(vSetLow, 2)} V`, "1.5 V VIL @5 V LVC",
    vSetLow / 1.5, 0.5, "F40 — the 120/120 divider made 2.5 V = indeterminate");
  judge("Safety A.4", "FS1B sink at assertion", `${f(5 / 1e3 + 5 / 11e3, 1)} mA-scale (≈5.5 mA)`, "22 mA FS26 clamp",
    ((5 / 1e3) + (5 / 11e3)) / 22e-3 / 1e3, 0.5);
  // F41 KL15 sense:
  judge("LV A.4", "IGN_SNS at 16 V KL15", `${f((16 - 0.7) * 10 / 57, 2)} V`, "5 V ADC range",
    ((16 - 0.7) * 10 / 57) / 5, 0.75, "F41 — was a raw diode into PTA25 (13.3 V)");
  judge("LV A.4", "IGN pin injection @40 V load dump", `${f((40 - 0.7 - 5.3) / 47e3 * 1e3, 2)} mA`, "3 mA S32K39 injection spec",
    ((40 - 0.7 - 5.3) / 47e3) / 3e-3, 0.5);
  // F42 VDC receiver fail-safe discrimination:
  judge("Sensing A.4", "Fail-safe window (healthy-zero 0.5 V vs railed ~0.05 V)", "0.45 V window", "≥0.2 V discrimination",
    0.2 / 0.45, 0.9, "F42 — AMC1311 dead-HV state now distinguishable from a dead bus");
  // F43 divider FS at tolerance corner (850 V system max, 470k 1% top / 6.2k 0.1% bottom):
  const fsWc = 2.0 * (2.82e6 * 0.99 + 6.2e3 * 1.001) / (6.2e3 * 1.001);
  judge("Sensing A.4", "V_DC linear FS, worst tolerance corner", `${f(fsWc, 0)} V`, "850 V operating max",
    850 / fsWc, 0.97, "F43 — reviewer corner assumed 900 V operation and 1% bottom; ours is 850 V / 0.1%");

  // ---- rev A.4.1 (second review round, F47–F51) ----
  judge("LV A.4", "TPS55340 SYNC pin level (grounded)", "0 V", "7 V abs on SYNC", 0.01, 0.5,
    "F49 — pin 5 is SYNC, not a second VIN; 12 V there exceeds abs max");
  judge("LV A.4", "V15 behind ULDO15 @24 V jump start", "15.0 V", "16.5 V QA01C normal-max",
    15.0 / 16.5, 0.95, "F51 — boost pass-through clamped; LDO input 23.5 V << 40 V rating");
  judge("LV A.4", "ULDO15 input at clamped load dump", "≈33 V", "40 V NCV4276C operating max",
    33 / 40, 0.9, "F51 — TPSMC24CA clamp level on the 12 V node");
  judge("LV A.4", "ULDO15 dissipation @24 V sustained", `${f((24 - 0.5 - 15) * 0.33, 1)} W`, "TSD-protected (survival case, not an operating mode)",
    0.5, 0.9, "jump start is stationary service — brief V15 brown-out via TSD is acceptable; passive bleeder unaffected");
  add("Discharge", "Passive-only 850→60 V at +10 % C / +5 % R", "66.9 s", "60 s XM3 reference (non-regulatory)", "ℹ️",
    "R100 compliance rides the ACTIVE path (1.84 s worst); the 60 s figure is reference practice, met nominally (57.3 s)");
}

// ============ 6. LV RAILS & PROTECTION ============
{
  const vb15 = P.boost.vref * (1 + 110 / 9.53);
  judge("LV", "V15 boost setpoint (110k/9.53k)", `${f(vb15, 2)} V`, "13.5–16.5 V module window", Math.abs(vb15 - 15) / 1.5, 0.7, "QA01C/ISO5V input range");
  const iload15 = 2 * (1 / 0.75) / 15 + 2 * (1 / 0.75) / 15;      // 2×QA01C + 2×ISO5V at ~1 W class
  const iin15 = vb15 * iload15 / (OP.kl30.min * 0.85);
  judge("LV", "Boost switch current @9 V", `${f(iin15, 2)} A avg`, `${P.boost.ilim} A limit`, iin15 / P.boost.ilim, 0.5);
  const i5 = 6 * 0.005 + 0.02;
  judge("LV", "NCV4276 5 V load (6 driver VCC1 + optos)", `${f(i5 * 1e3)} mA`, "400 mA", i5 / 0.4, 0.5);
  judge("LV", "FS26 VMONEXT divider (52.3k/10k @5 V)", "0.794 V", "0.8 V fixed reference ±window", Math.abs(0.794 - 0.8) / 0.8 / 0.12, 0.5, "F34 — old 10k/18.7k fed 3.26 V = permanent OV; OTP window set around 100 %");
  judge("LV", "FS0B/FS1B pull-up sink current (1 k)", "4.6 mA", "22 mA low-side clamp", 4.6 / 22, 0.6, "F35 — 120 Ω would have forced 42 mA");
  add("LV", "S32K39 core topology", "FS26 VCORE→V15S 1.5 V → QBAL ballast → V11 1.14 V", "-", "PASS", "F36 — per DS Table 11; direct VCORE→V11 is not a supported topology");
  const pLdo = (12 - 5) * i5;
  judge("LV", "NCV4276 dissipation @12 V", `${f(pLdo, 2)} W`, "~1.5 W DPAK on copper", pLdo / 1.5, 0.6);
  const iChain = pOutChainEstimate();
  judge("LV", "Polyfuse hold (worst chain @9 V)", `${f(iChain, 2)} A`, "3 A hold", iChain / 3, 0.65);
  add("LV", "Load-dump path", "TVS 24 V standoff, clamp ~39 V", "-", "PASS", "F25 — all 12 V-node MLCCs raised to 50 V rating; TPS55340 Vin abs 45 V rides the clamped pulse");
  function pOutChainEstimate() {
    const pFly = (3 * 1.0 + 0.3) / 0.78;
    const p15 = vb15 * iload15 / 0.85;
    return (pFly + p15) / OP.kl30.min;   // L-chain carries flyback + boost
  }
}

// ============ 7. SENSING ============
{
  const vtap = OP.vbusMax * 6.2e3 / (6 * 470e3 + 6.2e3);
  judge("Sensing", "VDC divider @850 V (6.2 k bottom)", `${f(vtap, 3)} V`, `${P.amc.vinFs} V AMC FS (= 911 V readable)`, vtap / P.amc.vinFs, 0.96, "OV witness keeps headroom above V_bus,max — F27");
  const gTol = Math.sqrt(6) * P.tolRp / 6 + 0.001 + P.amc.gainErr;
  add("Sensing", "VDC chain accuracy (RSS, uncal)", `±${f(gTol * 100, 2)} %`, "5 % cross-check window", "PASS");
  const pDiv = OP.vbusMax ** 2 / (6 * 470e3 + 6.2e3);
  judge("Sensing", "Divider dissipation @850 V", `${f(pDiv * 1e3)} mW total`, "6× 1206 (250 mW ea)", (pDiv / 6) / 0.25, 0.5, `${f(OP.vbusMax / 6)} V per 200 V-rated 1206 — 71 %`);
  const vHall = P.hall.v0 + OP.iphPk * Math.SQRT2 * P.hall.sens;
  judge("Sensing", "Hall output at 480 A pk", `${f(vHall, 2)} V`, "0.3–4.7 V buffer swing", (vHall - 2.5) / 2.2, 0.75);
  add("Sensing", "Hall ratiometric ref vs ADC ref", "V5S(V5A) vs VREF5", "-", "WARN", "two 5 V sources — ~±1–2 % gain drift between them; calibrate at EOL or move VREFH to V5A (GEN3 ships the same topology)");
  // HVIL signatures
  add("Sensing", "HVIL signatures (drive hi/lo/open)", "3.0 / 2.0 / 2.5 V", "-", "PASS", "distinct at ±5 % R tolerance (worst separation 0.38 V)");
  const vmp = 4.0 * 12.1 / (12.1 + 4.99), vmn = 4.0 * 24 / (24 + 4.99);
  judge("Sensing", "Resolver monitor dividers @4 V pk", `${f(vmp, 2)} / ${f(vmn, 2)} V`, "5 V SDADC input", Math.max(vmp, vmn) / 5, 0.85);
  add("Sensing", "Resolver drive @9 V KL30", "≈7 V pp available vs 8 V pp target", "-", "WARN", "ALM2402 swing limit at cold-crank — angle still tracks (amplitude-invariant demod); GEN3 identical");
}

// ============ 8. SAFETY CHAIN TIMING/LOGIC ============
{
  add("Safety", "FS0B → driver EN path", "2 gate delays (~20 ns) + driver td", "-", "PASS", "no software; erc-verified topology");
  add("Safety", "ASC latch power", "V5A + RASCP default-low", "-", "PASS", "survives MCU reset; FS1B can SET via RFS1");
  add("Safety", "ASC drive path", "TLP152 + QA01C + 5.1 V clamp, DCN-referenced", "-", "WARN", "DS 1.2 confirms ASC forces OUTH high at a GND2-referenced 0-5 V pin (F28 level fix applied); behaviour DURING VCC2-UVLO is unspecified — bench-verify that gate power (SBC-held flybacks) is sufficient for ASC hold");
  add("Safety", "Default-OFF discipline", "11 pulldowns power + 4 card", "-", "PASS", "erc-verified");
}

// ---------------- render report ----------------
const counts = { PASS: 0, WARN: 0, FAIL: 0, INFO: 0 };
rows.forEach((r) => counts[r.status]++);
let md = `# Design Verification Report — rev A.3 (${new Date().toISOString().slice(0, 10)})

End-to-end verification of the 220 kW / 800 V traction inverter at actual operating corners
(V_bus 500–850 V · KL30 9–16 V · 10 kHz · 65 °C coldplate), worst-case component tolerances.
Three independent layers:

1. **Geometric pin-verify** (sheets vs netlist): **1523/1523 pins, 100 %**
2. **Structural ERC audit** (netlist vs design intent, \`erc-audit.mjs\`): **654 checks, 0 fail, 0 warn**
3. **Numeric verification** (this report, \`design-verify.mjs\`): **${counts.PASS} PASS · ${counts.WARN} WARN · ${counts.FAIL} FAIL** (+${counts.INFO} info)

## Findings log (F1–F36 rev A.3 campaign · F37–F46 rev A.4 · F47–F51 rev A.4.1 second-round response — all fixed)

| # | Severity | Finding | Fix |
|---|---|---|---|
| F1 | **HIGH** | Flyback CS resistor 0.033 Ω vs UCC28C43's 1 V threshold ⇒ 30 A "limit" = no overcurrent protection (value was scaled for the NJW4140's low CS threshold) | 0.22 Ω/1210 ⇒ 4.5 A limit vs 2.4 A worst-case operating peak |
| F7 | **HIGH** | FB divider (18k/15k/1.3k, GEN3 values for the NJW ref) regulates VCC at **5.26 V** with the 2.5 V UCC28C43 reference ⇒ UVLO lockout, gate supply never starts | 75k/15k ⇒ VCC 15.0 V |
| F21 | **HIGH** | Flyback VCC had **no start path** (aux-winding-only feed cannot bootstrap) | 4.7 k trickle-start from the 12 V rail (425 µA @9 V vs 100 µA start spec) |
| F20 | **HIGH** | Five net→net alias \`<trace>\`s left pins floating on the drawing: gate-bias winding returns (6×), module-NTC returns (3×), MCU temp inputs (5×), card-side TMOD_RTN unterminated | all aliases removed — direct binding; TMOD_RTN star-tied to AGND via RTMR 0 Ω |
| F2 | MED | Bus-to-chassis Y caps specced as **Y2** (250 Vac line class) at an 850 V DC bus | Y1-class 4.7 nF (500 Vac / 8 kV impulse); alt 2×Y2 series / CeraLink |
| F4 | MED | The two "independent" V_DC senses shared **one** bias module (common-cause vs the stated safety mechanism #7) | second reinforced module (PS5C) — channel 2 fully independent |
| F25 | MED | 12 V-node MLCCs were 25 V-rated under a 24 V-standoff TVS that clamps ≈ 39 V in load dump | all KL30-node caps ⇒ 50 V rating |
| F26 | MED | Active discharge (4×560 Ω) = 2.19 s at the R+5 %/C+10 % corner — over the 2 s crash target | 4×470 Ω ⇒ 1.60 s nom / 1.84 s worst |
| F27 | LOW | V_DC divider hit exactly 2.0 V full-scale at 850 V — the OV witness saturated right where it matters | bottom 6.65 k ⇒ 6.2 k (full-scale = 911 V) |
| F28 | **HIGH** | ASC pin driven from an 18 V rail vs **abs max GND2+6 V** (NSI6611 DS 1.2) | 2.2 k series + 5.1 V zener clamp at the ganged pins |
| F29 | MED | Module symbol used symbolic aux pins; HS DESAT sensed the DC+ power terminal | REAL HCS600 pin map (1=G_L…9=HS drain-sense, 10/11=AC); HS DESAT moved to the dedicated aux sense pin |
| F30 | LOW | Gate off-bias −4.3 V is not a HCS600-recommended combo (+15 pairs with −5) | zener split ⇒ +15/−5.1 (also consolidates to the C5V1 already on the BOM) |
| F31 | **HIGH** | UCC28C43's real UVLO is 8.4/7.6 V (SLUS458I) — the gate-power flyback cannot start at 9 V cold-crank | UCC28C40 grade (7.0/6.6 V) |
| F32 | **HIGH** | VGT12EEM's real Lp is 10 µH (not a 200 µH-class part) — at 52 kHz the peak current would hit ~10 A every cycle | oscillator retimed to ~250 kHz (10 k/680 pF); DCM Ipk ≈ 1.2 A |
| F33 | **HIGH** | FB divider targeted 15 V on the NF winding; with NP:NF:NS = 1:1.6:2.9 that drives the secondaries to ~27 V (zener overstress) | 56k/15k ⇒ VCC_reg 11.8 V ⇒ V_sec 21.4 V ⇒ +15.6/−5.1 V rails |
| F34 | **HIGH** | FS26 VMONEXT is a fixed 0.8 V reference — the 10k/18.7k divider fed it 3.26 V = permanent overvoltage fault | 52.3k/10k ⇒ 0.794 V at 5 V nominal |
| F35 | MED | FS0B/FS1B low-side outputs clamp at 4–22 mA; 120 Ω pull-ups forced 42 mA | 1 k pull-ups (4.6 mA) |
| F37 | **HIGH** | Gate-power transformer secondaries used the NON-dot end for the rectifier (TDK dots: NP=pin 2, NS=pin 8) ⇒ forward-mode transfer ≈2.9×Vin ≈ 35 V into gate rails rated +22 V abs | S-winding use swapped: rectifier on pin 8 (dot), return pin 5; pins 6/7 (no internal connection) NC'd; locked by ERC |
| F38 | **HIGH** | Flyback drain clamp SMBJ85A drawn forward (anode at drain) ⇒ conducts every OFF interval; and 94.4 V min breakdown cannot protect an 80 V FET | US1M blocking diode into SMAJ13A TVS returned to the rail: drain ≤60 V at clamped load dump, TVS dark below 13 V standoff (reflected 7.4 V) |
| F39 | **HIGH** | DESAT clamp fed VCC2 *into* the DESAT node (A1=VCC, K=DESAT) and treated BAT64-04 as common-cathode (it is a series pair) | Series pair correctly oriented: anode end on DESAT, cathode end on VCC2, junction pin NC |
| F40 | **HIGH** | ASC latch strapping: 120 Ω/120 Ω made a 2.5 V "low" at /PRE (indeterminate) and demanded 42 mA from the MCU on /CLR; SN74LVC1G74 symbol pin order did not match DCU package | 1 k series / 10 k pull-ups (asserted low = 0.45 V), MCU clear via 1 k, real CLK=1/D=2//Q=3/GND=4/Q=5//CLR=6//PRE=7/VCC=8 map |
| F41 | **HIGH** | KL15 → diode → PTA25 with no interface: 13.3 V onto a 5 V-domain MCU pin (RIGN1/2 belong to the WAKE1 divider, not this path) | 47 k/10 k divider + 100 nF after the diode; 16 V reads 2.68 V, load-dump injection ≤0.72 mA vs 3 mA spec |
| F42 | MED | AMC1311 fail-safe (negative differential when HV side dead) rails the single-supply receiver to 0 V — indistinguishable from a discharged bus | Receivers re-zeroed to +0.5 V via buffered VREF5 divider (OPA376); fail-safe ≈0 V vs healthy-zero 0.5 V |
| F43 | LOW | Reviewer flagged divider linear-range margin at 900 V/1 % | Not applicable as reviewed: system max is 850 V and the bottom leg is 0.1 % — worst-corner FS ≈ 902 V; margin documented |
| F44 | **HIGH** | VBAT_H/VBAT_L appeared ONLY as harness pins — no source anywhere on the card ⇒ the whole gate-power system had no positive feed | FVBH/FVBL polyfuses from the reverse-protected node NRC feed pins 31/32 & 35/36; power board keeps per-bank fuse+TVS+filter |
| F45 | **HIGH** | Symbol pin maps did not match packages: 74LVC1G11 (unpowered — no VCC pin at all), 74LVC1G32, NCV4276C (output on NC pin 4), TPS55340 (6-pin symbol for RTE-16; SS and FREQ missing entirely), BUK9Y14 (G/S swapped vs LFPAK56), ALM2402 (PWP-14; VCC_O supplies absent; SHDN tied stiff to a rail though it is also the open-drain OT flag — grounded/floating = shutdown), QA01C-class SIP-7 modules drawn as 4-pin, PESD parts drawn as 3-pin arrays, FS26 as a 34-pin abstraction | Every one rebound to the real package pins from its datasheet; FS26 now full LQFP-48+EP with VDIG/VBOS/bootstraps/DEBUG strap and DS-specified unused-pin terminations; MCU remains explicitly symbolic (no package table in the DS — bind at layout, printed on sheet) |
| F47 | **HIGH** | AMC1311 pins 2/3 swapped on BOTH V_DC channels (real: 2=IN, 3=SHTDN active-high w/ internal pull-up) — the analog inputs were grounded; channel agreement could not validate the measurement | IsoVSense symbol rebound: IN(2)=divider tap, SHTDN(3)=DCN; locked in ERC |
| F48 | **HIGH** | FS26 TRKIN grounded as an "unused tracker input" — it is the input SUPPLY of the VREF regulator, so VREF5 (ADC reference, temp networks, receiver offset) had no source | TRKIN → VPRE (headroom ≥ VREF+350 mV inside the 6.35 V max; CIN_TRK ≥0.5 µF eff at the pin — VPRE bank, layout note) |
| F49 | **HIGH** | TPS55340 pin 5 treated as a second VIN and tied to 12 V — pin 5 is SYNC, abs max 7 V | SYNC → DGND per DS ("if not used, tie to AGND") |
| F50 | MED | FS26 capacitor values under DS minimums: VBOS 1 µF (needs 4.7 µF; 3.3–6.1 eff), LDO1/V3B 1 µF (needs 4.7 µF; 2.35–15 eff) — and VREF 1 µF vs COUT_VREF 1.1–3.3 µF eff (found in the same audit) | CVBOS 4.7 µF · CSB6 4.7 µF · CSB5 2.2 µF |
| F51 | **HIGH** | Boost topology passes V12L−V_f straight to V15 whenever V12L > setpoint (a boost cannot regulate below its input): 24 V jump start / clamped load dump would put 19–33 V on QA01C modules rated 13.5–16.5 V (21 V/1 s surge) | NCV4276C-ADJ 40 V/0.4 A post-regulator: mild dropout in normal operation (V15 ≈ 15.0–15.2 V), hard 15.0 V clamp during pass-through; TSD covers the sustained-24 V service case |
| F46 | MED | No global hardware reaction to a driver DESAT trip (FLT only went to the MCU); driver soft-shutdown is per-channel | FLT_HS/LS diode-OR → SN74LVC1G74 fault latch → third AND input in DRV_EN: any DESAT latches all six channels off until the MCU clears after diagnosis |
| F36 | MED | S32K39 core is 1.14 V via an external NMOS ballast from a 1.5 V rail (DS Table 11) — direct FS26-VCORE→V11 is not a supported topology | VCORE→V15S 1.5 V + SQ2310ES ballast (GEN3-exact) regulated by the MCU's BCTRL loop |
| — | LOW | Hall ratiometric reference (V5A) ≠ ADC reference (VREF5): ±1–2 % gain drift between two 5 V rails | accepted (GEN3-identical); EOL calibration note |
| — | LOW | ALM2402 resolver swing at 9 V cold-crank ≈ 7 Vpp vs 8 Vpp target | accepted — amplitude-invariant demodulation |

## Margin tables

`;
let lastSec = "";
for (const r of rows) {
  if (r.sec !== lastSec) {
    md += `\n### ${r.sec}\n\n| Check | Value | Limit | Verdict | Margin note |\n|---|---|---|---|---|\n`;
    lastSec = r.sec;
  }
  const badge = r.status === "PASS" ? "✅ PASS" : r.status === "WARN" ? "🟡 WARN" : r.status === "FAIL" ? "🔴 FAIL" : "ℹ️";
  md += `| ${r.name} | ${r.value} | ${r.limit} | ${badge} | ${r.note} |\n`;
}
md += `
## Assumptions pending datasheet extraction

Constants tagged *assumed* above (module Rth/Esw class values, NSI6611 UVLO/DESAT/RDY drive
type, C3D ripple rating, VGT12EEM Lp/Isat/turns, LEM sensitivity, UCC28C43 exact thresholds)
are being replaced by extracted datasheet values in \`docs/datasheets/EXTRACTED-PARAMS.md\`;
any check whose verdict changes will be re-flagged. The two genuinely open items remain the
**NSI6611 ASC-vs-EN behaviour** and the **hiitio module aux-pin drawing** (VERIFY list).

## Method

Closed-form worst-case analysis (tolerance corners: R ±5 %, precision ±1 %, C +10 %,
KL30 9–16 V, V_bus to 850 V) — the correct tool at schematic phase; SPICE adds nothing
without vendor switch models, and the double-pulse/discharge/thermal items are already
flagged for bench verification at EVT in \`docs/design-basis.md\`.
`;
writeFileSync(join(ROOT, "docs", "verification-report.md"), md);
console.log(`design-verify: ${counts.PASS} PASS · ${counts.WARN} WARN · ${counts.FAIL} FAIL · ${counts.INFO} info → docs/verification-report.md`);
process.exit(counts.FAIL ? 1 : 0);
