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
import { REV } from "./rev.mjs";
import { OP as LOP, MOD, SKU, lossOf, rthT, rthD, tjPos, tjLimit, pAvail } from "./loss-model.mjs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
const ROOT = join(dirname(fileURLToPath(import.meta.url)), "..");

// ---------------- operating conditions ----------------
const OP = {
  vbusNom: 700, vbusMax: 850, vbusMin: 500,
  fsw: 10e3, iphPk: 340, iphCont: 185, pf: 0.85, mres: 0.95,
  kl30: { min: 9, nom: 13.5, max: 16 }, coolant: LOP.coolant, rthPlate: LOP.rthPlate,
};

// ---------------- datasheet / design constants ----------------
const P = {
  mod: MOD.sic, igbt: MOD.igbt,
  // link cap — REAL Faratronic C3D DS: 15.4 A rms @10 kHz/70 °C, ESR 7.8 mΩ, U_N 1100 V @70 °C / 1000 V @85 °C
  cap: { c: 20e-6, n: 16, vr: 1100, vr85: 1000, irmsEach: 15.4, esr: 7.8e-3, tolC: 0.10, src: "C3D datasheet" },
  // NSI6611A-Q1 DS 1.2: UVLO2 on 9.8/11.2/12.8, off 9.0/10.4/11.8; ICC2 1/3.3/7 mA; DESAT 8.5/9.3/10 V,
  // I_CHG 350/500/650 µA, LEB 200 ns, deglitch 100/200/320 ns; soft-off I_STO 100/400/570 mA;
  // FLT latched until an RST/EN RISING edge after ≥ t_FLT_MUTE (0.55–1.3 ms) low
  drv: { ipk: 10, uvlo2On: 12.8, uvlo2Off: 11.8, uvlo2OffTyp: 10.4, vccRecMin: 13, vcc2Rec: 32, vcc2Abs: 35, icc2: 7e-3, icc2Typ: 3.3e-3,
    vth: [8.5, 9.3, 10], ichg: [350e-6, 500e-6, 650e-6], leb: 0.2e-6, deg: [0.1e-6, 0.2e-6, 0.32e-6], isto: [0.1, 0.4, 0.57], tMute: 1.3e-3, src: "NSI6611 DS 1.2" },
  // UCC28C40 (SLUS458I): VDD_ON 6.5/7.0/7.5, VDD_OFF 6.1/6.6/7.1 V, I_START 50/100 µA, IVDD 2.3/3.0 mA
  pwm: { vref: 2.5, vcs: 1.0, uvloOn: 7.0, uvloOnMax: 7.5, uvloOff: 6.6, vccAbs: 20, istart: 100e-6, irun: 3.0e-3, kOsc: 1.72, src: "SLUS458I (C40 grade)" },
  // gate charge — HCS600 DS: QG 1240 nC @800 V/360 A, +18/−5 (Fig.11: ≈1.09 µC for −5.1→+15.6 V)
  qg: { total: 1.24e-6, vswing: 20.1, src: "HCS600 datasheet" },
  // flyback magnetics — REAL VGT12EEM-200S1A4: NP:NF:NS = 1:1.6:2.9, Lp 10 uH ±20 %
  xfmr: { lp: 10e-6, nf: 1.6, ns: 2.9, isat: 4.5, src: "TDK datasheet (Isat unpublished — CS limit is the guard)" },
  // TPS55340
  boost: { vref: 1.229, ilim: 5.25, vinAbs: 45, src: "TPS55340 DS" },
  // LEM HC5FW 900-S — DS: 2.22 mV/A ratiometric @5 V, 2.5 V @0 A, BW >=40 kHz
  hall: { sens: 2.22e-3, v0: 2.5, src: "LEM datasheet" },
  // AMC1311B (the drawn DWVR grade): gain error ±0.2 % max, offset ±1.5 mV, linear 0–2 V
  amc: { vinFs: 2.0, gainErr: 0.002, vos: 1.5e-3, src: "AMC1311 DS SBAS786C" },
  // resistor tolerances
  tolR: 0.05, tolRp: 0.01,
};

// ---------------- round-7 shared models ----------------
// Gate rail from the separate FB-sense rectifier (A6-R06): the divider holds FFS at V_FB·(R1+R2)/R2,
// the aux plateau is FFS + Vf_FS, the secondary plateau is that ×NS/NF, and VCC2 is what is left
// after the secondary rectifier and the 5.1 V split zener. Corners: V_FB 2.45–2.55 V (SLUS458I),
// 1 % divider, 1N4148WS 0.45–0.65 V, US1M 0.7–1.3 V (FFS peak-detects at the secondary's peak
// current, where the US1M drop is highest — cross-check 6a), BZT52-C5V1 4.8–5.4 V; the low corner
// also carries the FB bias current (≤ 2 µA into the 11.7 k divider Thevenin → −0.19 V on VCC2).
// The 100 R FB-sense drop is the divider current over the diode's conduction fraction d (round 8;
// cross-check R8X-16): d = 1 on the low corner (least drop), 0.5 nominal, 0.2 on the high corner
// (peak detection on the flattest plateau — the fast-US1M corner; the leakage spike pulls the other way).
const vcc2Of = (vfb, r1, r2, vfFs, vfSec, vz, d = 0.5) => (vfb * (r1 + r2) / r2 + vfFs + (vfb / r2) / d * 100) * (P.xfmr.ns / P.xfmr.nf) - vfSec - vz;
const VCC2 = {
  nom: vcc2Of(2.5, 52.3e3, 15e3, 0.55, 0.85, 5.1),
  lo: vcc2Of(2.45, 52.3e3 * 0.99, 15e3 * 1.01, 0.45, 1.3, 5.4, 1) - 2e-6 * 11.7e3 * (67.3 / 15) * (P.xfmr.ns / P.xfmr.nf),
  hi: vcc2Of(2.55, 52.3e3 * 1.01, 15e3 * 0.99, 0.65, 0.7, 4.8, 0.2),
  old: vcc2Of(2.5, 56e3, 15e3, 0.8, 0.85, 5.1),   // A.6: 56 k on VDD, i.e. behind the US1M aux diode
};
// 74LVC3G17-Q100 (Nexperia DS Table 8, −40…125 °C, V_CC 4.5–5.5 V) as fractions of V_CC
const SCH = { tpLo: Math.min(1.90 / 4.5, 2.20 / 5.5), tpHi: Math.max(3.30 / 4.5, 3.80 / 5.5),
  tnLo: Math.min(1.00 / 4.5, 1.20 / 5.5), tnHi: Math.max(2.20 / 4.5, 2.50 / 5.5) };
// ASC entry (round 7, RR05). Power board: TLP152 → RASCG 2.2 k / RASCPD 10 k (1.8 k Thevenin) → CASCD 12 nF
// → NSI6611 ASC (V_ASCH 2.7–3.2 V, tASC_r 0.39–1.1 µs). The bias is a QA01C-18 (+18/−3 V; round 9, A8-N01 —
// F61 had read the QA01C base part's +20/−4 V sheet): QA18 below gives its envelope. The high sides start turning off first: FS0B drops DRV_EN
// (FS1B path) or the eFlexPWM fault forces the high-side PWM off (MCU path) — EN is NOT used for ASC ordering,
// because the NSI6611 honours DESAT over ASC only with EN high (DS §8.12, round-7 cross-check).
// Mornsun QA01C-18 (DS 2018.12.11-A/0): +18 V output envelope Fig. 1 — max +9 % → +2 %, min 0 % → −7 % over
// 10–100 % load; line regulation ±1.3 %/% (V15 = NCV4276C-ADJ, ±3 %); tempco ±0.03 %/°C at 100 % load (±2.4 %
// over −40…105 °C). Our loads are 2–8 % (opto supply, ASC 2.2 k into the 5.1 V clamp, gate divider), so the
// max line is extrapolated to 2 % load (+9.6 %) and the min line held at its 10 % value (0 %).
const QA18 = (() => { const line = 0.013 * 3, tc = 0.0003 * 80;
  return { min: 18 * (1 - line - tc), max: 18 * (1 + 0.09 + 0.07 * 8 / 90 + line + tc) }; })();
const ASC = (() => {
  const rTh = 2.2e3 * 10e3 / 12.2e3, c = 12e-9, vTh = (v) => (v - 0.3) * 10 / 12.2;
  const tRc = (r, cc, vth, vt) => r * cc * Math.log(vth / (vth - vt));
  return {
    tLsMin: tRc(rTh * 0.99, c * 0.95, vTh(QA18.max), 2.7) + 0.39e-6,
    tEntryMax: 5.5e-9 + 0.25e-6 + tRc(rTh * 1.01, c * 1.05, vTh(QA18.min), 3.2) + 1.1e-6,   // UASCG (74LVC1G08-Q100 5.5 ns, 125 °C) + TLP152 tpLH at ≥ 10.3 mA (DS 10 mA point + margin) + RC + tASC_r
    tHsEn: 5.4e-9 + 2 * 4.4e-9 + 5e-9 + 60e-9 + 130e-9,   // FS1B path: USCH, two ANDs, harness, EN deglitch, EN→OUT (≈tpHL max)
  };
})();
// FW-06 over-voltage budget (round 7, RR06/A6-R08): link crossing the trip → ASC request
const OVP = { tDiv: (2.82e6 * 6.2e3 / (2.82e6 + 6.2e3)) * 1e-9, tAmc: 2.1e-6, tRx: 0.3e-6, tSample: 1 / 200e3, tConv: 1.0e-6, tAct: 1.0e-6 };
OVP.tReq = OVP.tDiv + OVP.tAmc + OVP.tRx + OVP.tSample + OVP.tConv + OVP.tAct;
// Round-8 interface data (EXTRACTED-PARAMS §26). LVC outputs (74LVC1G08/3G17-Q100 Table 7): V_OH ≥ 3.4 V
// at V_CC 4.5 V, I_O −32 mA, −40…125 °C (≥ 3.8 V over −40…85 °C); below that current the PMOS drop is
// bounded by the same resistance (triode), so V_OH(I) ≥ V_CC − I·R_out. TLP152 V_F: 1.40–1.80 V at
// 10 mA/25 °C (guaranteed) and a −1.8 mV/°C tempco that Toshiba gives as TYPICAL only (round 9, A8-02): the
// corners use a ±28 % band, −1.3…−2.3 mV/°C, each end where it hurts — cold V_F max 1.95 V, hot V_F max 1.70 V,
// hot V_F min 1.23 V. Guaranteed: I_FLH 7.5 mA max over −40…100 °C and 20 mA abs max; the 10–15 mA I_F(ON)
// window is recommended, not guaranteed. The LVC R_out is a MOS-triode bound (V_drop/I falls with I below
// the 32 mA point), not a supplier curve. Corners are temperature-consistent (cross-check R8X-04): cold =
// −40 °C V_F with the 85 °C R_out bound, hot = 100 °C V_F with the 125 °C one.
// NSI6611 FLT V_OL ≤ 0.3 V at 5 mA; BAT46 ≤ 0.25 V at 0.1 mA/25 °C, 0.45 V taken cold at 0.45 mA.
const LVC = { vOh32: 3.4, vOh32c: 3.8 }; LVC.rOut = (4.5 - LVC.vOh32) / 32e-3; LVC.rOutC = (4.5 - LVC.vOh32c) / 32e-3;
const LED = { vfMax: 1.80 + 65 * 2.3e-3, vfMaxHot: 1.80 - 75 * 1.3e-3, vfMin: 1.40 - 75 * 2.3e-3, iFlh: 7.5e-3, iRecMin: 10e-3, iRecMax: 15e-3, iAbs: 20e-3, tpLH: 0.25e-6 };
const FLTL = { vol: 0.3, bat46: 0.45 };

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

// ============ 1. POWER STAGE (every SKU) ============
{
  for (const [id, s] of Object.entries(SKU)) {
    const sec = `Power stage — ${s.name}`;
    // envelope (F09): the rectangle "220 kW over 500–850 V" was not true at 340 A
    const vFullPk = s.pTarget[0] * 1e3 / (Math.sqrt(1.5) * OP.mres * s.iPk * OP.pf);
    const vFullCont = s.pTarget[1] * 1e3 / (Math.sqrt(1.5) * OP.mres * s.iCont * OP.pf);
    add(sec, "Current-limited envelope P_pk / P_cont", `${f(pAvail(s.vMin, s.iPk), 0)}/${f(pAvail(s.vMin, s.iCont), 0)} kW @${s.vMin} V · ${f(Math.min(s.pTarget[0], pAvail(s.vNom, s.iPk)), 0)}/${f(Math.min(s.pTarget[1], pAvail(s.vNom, s.iCont)), 0)} kW @${s.vNom} V`,
      `${s.pTarget[0]}/${s.pTarget[1]} kW from ${f(vFullPk, 0)}/${f(vFullCont, 0)} V up`, "INFO",
      `${s.iPk}/${s.iCont} A rms, PF ${OP.pf}, 5 % modulation reserve — firmware derates P(V_dc) below these voltages (review A.6 F09)`);
    for (const [tag, I, V] of [["peak 30 s", s.iPk, s.vMax], ["continuous", s.iCont, s.vNom]]) {
      const L = lossOf(s, I, V);
      const Lr = s.sil !== "sic" ? lossOf(s, I, V, -0.85) : L;
      const tj = tjPos(s, L).T;   // RR07: IGBT + diode heat share the coldplate footprint
      const detail = s.sil === "sic" ? `cond ${f(L.cond, 0)} + sw ${f(L.sw, 0)} + Qrr ${f(L.rr, 0)} + dead-time ${f(L.dt, 0)} W`
        : `IGBT ${f(L.condT, 0)}+${f(L.swT, 0)} W · diode ${f(L.condD, 0)}+${f(L.rec, 0)} W (motoring)`;
      judge(sec, `Tj steady-state bound, ${tag} (${I} A, ${V} V, ${s.fsw / 1e3} kHz)`, `${f(tj, 0)} °C (${f(L.sw_die, 0)} W/switch)`,
        `${tjLimit(s)} °C ${s.sil === "sic" ? "Tj max" : "Tvjop"}`, tj / tjLimit(s), tag === "continuous" ? 0.75 : 0.9,
        `${detail}; 65 °C coolant + 0.045 K/W coldplate (shared by the position's IGBT and diode, RR07); the 30 s transient is in S4`);
      if (s.sil !== "sic") {
        const tjd = tjPos(s, Lr).D;
        judge(sec, `Diode Tj bound, ${tag} regeneration (cosφ −0.85)`, `${f(tjd, 0)} °C (${f(Lr.d_die, 0)} W)`, `${P[s.sil].tvjop} °C`,
          tjd / P[s.sil].tvjop, 0.9, `F26 — the FWD is its own die (Rth 0.10 K/W); regen loads it hardest; RR07 — the IGBT's ${f(Lr.sw_die, 0)} W heats the shared coldplate too`);
      }
    }
    const Lc = lossOf(s, s.iCont, s.vNom);
    const pOut = Math.min(s.pTarget[1], pAvail(s.vNom, s.iCont)) * 1e3;
    const pSemi = 6 * (Lc.sw_die + Lc.d_die);
    add(sec, `Semiconductor efficiency @ continuous (${f(pOut / 1e3, 0)} kW, ${s.vNom} V)`, `${f(100 * pOut / (pOut + pSemi), 2)} % (${f(pSemi, 0)} W)`, "-", "INFO",
      "six switches, conservative 175 °C R_DS(on); caps/busbar/LV add ≈0.1–0.2 pt");
  }
  // module current and sensing headroom at the highest SKU currents
  judge("Power stage — all SKUs", "Peak switch current vs module rating", "566 A pk (4XX, 400 A rms)", "600 A DC / 1200 A 1 ms", 566 / 1200, 0.5,
    "8XX SKUs: 481 A pk");
  judge("Power stage — all SKUs", "Phase-current sensing headroom", "≈620 A (566 A pk + 10 % ripple, 4XX)", "±900 A LEM range", 620 / 900, 0.8,
    "8XX ≈530 A — the ±900 A sensor covers every SKU (review F33 assumed 600 A rms; above ≈480 A rms a 4XX-HP frame needs a larger sensor)");
  // turn-off overshoot (F01): the DS fall time at the characterized 3.3 Ω is the di/dt input
  const didtHot = 0.8 * 481 / P.mod.tfHot, didtCold = 0.8 * 481 / P.mod.tfCold;
  const vpk = OP.vbusMax + 15e-9 * didtCold;
  add("Power stage — all SKUs", "Turn-off overshoot, SiC 850 V / 481 A, cold (RG_OFF 6.8 Ω)", `${f(vpk, 0)} V at 15 nH (${f(didtCold / 1e9, 1)} kA/µs est.)`,
    "1080 V repetitive guard · 1200 V abs", vpk <= 1080 ? "PASS" : vpk <= 1200 ? "WARN" : "FAIL",
    `hot ${f(OP.vbusMax + 15e-9 * didtHot, 0)} V. DPT GATE, not closed on paper: module Ls unpublished (hiitio RFQ); at the DS 3.3 Ω tf (13 ns) the same loop would reach ${f(OP.vbusMax + 15e-9 * 0.8 * 481 / 13e-9, 0)} V. Levers: RG_OFF → 10 Ω (≈+30 mJ Eoff, +11 °C at peak) and/or firmware I_pk(V_dc) above 800 V. The old S8 0.3 V term was a 1000× unit error (F01). IGBT SKUs: tf 200–385 ns ⇒ <40 V`);
}

// ============ 2. DC LINK (every SKU) ============
// Capacitor RMS current, 2-level SVPWM (Kolar): I_C = I·√(2M[√3/4π + cos²φ(√3/π − 9M/16)]).
// Its maximum over M and cosφ is 0.65·I (M ≈ 0.61, cosφ = 1) — the old 0.62 was the rated point.
{
  const kolar = (M, c) => Math.sqrt(2 * M * (Math.sqrt(3) / (4 * Math.PI) + c * c * (Math.sqrt(3) / Math.PI - 9 * M / 16)));
  let kMax = 0;
  for (let M = 0.05; M <= 1.15; M += 0.005) for (let c = 0; c <= 1.0001; c += 0.01) kMax = Math.max(kMax, kolar(M, c));
  for (const s of [SKU.sic8, SKU.igbt4]) {
    const sec = `DC link — ${s.name.slice(0, 3)} bank (${s.name})`;
    const perCan = kMax * s.iPk / 16, perCanC = kMax * s.iCont / 16;
    judge(sec, `Ripple per can, 30 s peak (worst M/cosφ, ${s.iPk} A)`, `${f(perCan)} A`, `${s.can.irms} A @10 kHz/70 °C`, perCan / s.can.irms, 0.95,
      `bank ${f(kMax * s.iPk, 0)} A = ${f(kMax, 3)}·I (Kolar max); 30 s is far inside the can's thermal τ`);
    judge(sec, `Ripple per can, continuous (${s.iCont} A)`, `${f(perCanC)} A`, `${s.can.irms} A`, perCanC / s.can.irms, 0.8);
    judge(sec, `Voltage vs U_N at 85 °C, OV trip ${s.ovTrip} V`, `${s.ovTrip} V`, `${s.can.vr85} V`, s.ovTrip / s.can.vr85, 0.9,
      `normal max ${s.vMax} V = ${f(100 * s.vMax / s.can.vr85, 0)} %`);
    const cTot = 16 * s.can.c;
    add(sec, "Stored energy at V_max (C +10 % + 3 µF local)", `${f(0.5 * (cTot * 1.1 + 3.3e-6) * s.vMax ** 2)} J`, "-", "INFO", `${f(cTot * 1e6, 0)} µF nominal`);
  }
  const pEsr = (kMax * SKU.sic8.iCont / 16) ** 2 * P.cap.esr;
  judge("DC link — 8XX bank (8XX SiC)", "ESR heating per can, continuous", `${f(pEsr, 2)} W`, "1.85 W (15.4 A²·7.8 mΩ = the 15 K rise)", pEsr / 1.85, 0.6);
  add("DC link — 4XX bank (4XX IGBT)", "4XX can binding", "50 µF / 600 V (85 °C) in the same 37.5 mm positions", "≥18 A rms @10 kHz/70 °C", "WARN",
    "CLASS part until the Faratronic RFQ returns the exact MPN + ripple/ESR/life data (review F20/F21) — the busbar drawing is unchanged");
}

// ============ 2b. REGENERATION WITH THE BATTERY PATH LOST (FW-06/FW-08, rev A.6 N9) ============
// Constant regen power into the link after the contactor opens: C·V·dV/dt = P ⇒ V(t)² = V0² + 2·P·t/C,
// from V_max to the OV trip, then for the FW-06 response time; C at −10 % tolerance.
{
  for (const s of [SKU.sic8, SKU.igbt4]) {
    const sec = `Regeneration, battery path lost — ${s.name.slice(0, 3)} bus`;
    const C = 16 * s.can.c * 0.9 + 2.7e-6, P = s.pTarget[0] * 1e3;
    const tTrip = C * (s.ovTrip ** 2 - s.vMax ** 2) / (2 * P), vp = (t) => Math.sqrt(s.ovTrip ** 2 + 2 * P * t / C);
    add(sec, `Link charging at ${s.pTarget[0]} kW regen (C_min ${f(C * 1e6, 0)} µF)`, `${f(P / (C * s.vMax) / 1e6, 2)} V/µs; ${s.vMax}→${s.ovTrip} V trip in ${f(tTrip * 1e6, 0)} µs`, "-", "INFO",
      `a 100 µs response would end at ${f(vp(100e-6), 0)} V and a once-per-PWM-period sample at 5 kHz (200 µs) at ${f(vp(200e-6), 0)} V — why FW-06 is 20 µs on a free-running V_DC slot`);
    // regen power until the high-side-off/ASC request, then all six switches off: the phase current (up to
    // I_pk) rectifies into the link until the LS are on (cross-check item 9)
    const vPk = vp(OVP.tReq) + s.iPk * Math.SQRT2 * ASC.tEntryMax / C;
    judge(sec, `Link peak with the FW-06 response (${f(OVP.tReq * 1e6, 1)} µs to HS-off + ASC request, then ${f(ASC.tEntryMax * 1e6, 1)} µs all-off at ${f(s.iPk * Math.SQRT2, 0)} A)`, `${f(vPk, 0)} V`, `${s.can.vr85} V can U_N at 85 °C`, vPk / s.can.vr85, 0.92,
      "round 7: the whole chain, not a written 20 µs — the motor's stored magnetic energy adds a motor-dependent step on the non-ASC path (below n_x); HIL event-to-ASC measurement + dyno contactor opening under full regen are the gates");
  }
}

add("Regeneration, battery path lost — budget", "FW-06 latency to the ASC request (RR06/A6-R08)",
  `${f(OVP.tReq * 1e6, 1)} µs = divider lag ${f(OVP.tDiv * 1e6, 2)} + AMC1311B ${f(OVP.tAmc * 1e6, 1)} + receiver ${f(OVP.tRx * 1e6, 1)} + sample wait ${f(OVP.tSample * 1e6, 1)} (≥ 200 kS/s per channel) + conversion ${f(OVP.tConv * 1e6, 1)} + compare→fault→ASC_REQ ${f(OVP.tAct * 1e6, 1)}`,
  "allocated in FW-06", "INFO", "the divider's 6.2 µs is the lag of a first-order filter behind a ramp; the route (ADC analog watchdog → eFlexPWM fault + ASC_REQ) is a firmware deliverable measured on HIL — no comparator is added unless that measurement misses the budget");

// ============ 3. DISCHARGE (every bus class; worst-case corners, both paths applied ONCE) ============
{
  for (const s of [SKU.sic8, SKU.igbt4]) {
    const sec = `Discharge — ${s.vMax === 850 ? "8XX" : "4XX"} values`;
    const cN = 16 * s.can.c + 3e-6, cMax = 16 * s.can.c * 1.1 + 3.3e-6, ln = Math.log(s.vMax / 60);
    const rp = s.rp, ra = s.ra;
    judge(sec, `Passive ${s.vMax}→60 V, worst (R+5 %, C+10 %)`, `${f(rp * 1.05 * cMax * ln)} s`, "120 s service rule",
      rp * 1.05 * cMax * ln / 120, 0.8, `nominal ${f(rp * cN * ln)} s (${s.rpN * 2} × ${s.rpPer / 1e3} k, 2 strings)`);
    const vRes = s.vMax * 1.05 / (1.05 + (s.rpN - 1) * 0.95);        // one +5 % part among −5 % parts
    judge(sec, `Bleeder V on the worst-tolerance resistor @${s.vMax} V`, `${f(vRes)} V`, "200 V working (plain 2512)", vRes / 200, 0.8,
      `review A.6 F24: 5 × 27 k put 184 V (92 %) on it at 850 V`);
    const pRes = (s.vMax / (s.rpN * s.rpPer * 0.95)) ** 2 * s.rpPer * 0.95;
    judge(sec, `Bleeder W/resistor @${s.vMax} V (R−5 %)`, `${f(pRes, 2)} W`, "2 W", pRes / 2, 0.65);
    const rc = (x) => 1 / (1 / x + 1 / (rp * 1.05));
    const t60a = rc(ra * 1.05) * cMax * ln + 2.5e-3;
    judge(sec, `Active + passive ${s.vMax}→60 V, worst corner`, `${f(t60a, 2)} s`, "2 s crash target", t60a / 2.0, 0.95,
      `nominal ${f(1 / (1 / ra + 1 / rp) * cN * ln, 2)} s; paths combined ONCE + 2.5 ms bias delay (F25: S5 counted the bleeder twice)`);
    const eRes = 0.5 * cMax * s.vMax ** 2 / 4;
    judge(sec, "Energy per 10 W wirewound (C+10 %)", `${f(eRes)} J`, "100 J single-pulse", eRes / 100, 0.5,
      `peak ${f(s.vMax ** 2 / (ra * 0.95) / 4, 0)} W/resistor decaying τ = ${f(ra * cMax, 2)} s; firmware ≤ 3 discharges/5 min (thermal recovery)`);
    judge(sec, "V per wirewound", `${f(s.vMax / 4)} V`, "≥350 V axial class", (s.vMax / 4) / 350, 0.8);
    const pStuck = s.vMax ** 2 / ra;
    add(sec, "QDIS stuck ON with the battery connected", `${f(pStuck, 0)} W continuous (${f(pStuck / 4, 0)} W/resistor)`,
      "not survivable by 10 W parts", "WARN",
      "F23: bounded, not survived — firmware fires QDIS only with contactors reported OPEN + auto-timeout; a pre-existing FET short is caught at the next precharge (link plateaus ≈5 % low, abnormal τ); the fail-open flameproof wirewound class opens the string. Never demonstrated on a live battery");
  }
  add("Discharge — 8XX values", "QDIS stress", `${f(850 / 1880, 2)} A pk (4XX ${f(500 / 880, 2)} A)`, "1200 V / 42 A part", "PASS", "fully-enhanced switch, no linear region");
}

// ============ 4. GATE DRIVE ============
{
  const span = VCC2.nom + 5.1;   // regulated secondary (§5 model, round 7), +15/−5 combo (F30)
  judge("Gate drive", "VCC2 low corner vs UVLO-rising MAX", `${f(VCC2.lo, 2)} V`, `${P.drv.uvlo2On} V`, P.drv.uvlo2On / VCC2.lo, 0.95, "§5 corner stack: V_FB, divider, both rectifiers, split zener");
  judge("Gate drive", "VCC2 low corner vs recommended-min", `${f(VCC2.lo, 2)} V`, `${P.drv.vccRecMin} V rec-min`, P.drv.vccRecMin / VCC2.lo, 0.95);
  const spanHi = VCC2.hi + 4.8;
  judge("Gate drive", "VCC2−VEE2 span (high corner)", `${f(spanHi)} V`, `${P.drv.vcc2Rec} V recommended (35 abs)`, spanHi / P.drv.vcc2Rec, 0.8);
  // NSI6611 DS §9.6: I = min[(VCC2−VEE)/(R_G + R_OH|OL + R_Gint), 10 A]; R_OH 2.2 Ω, R_OL 0.3 Ω typ
  const ig = (rg, rdrv, rint) => Math.min(span / (rg + rdrv + rint), 10);
  add("Gate drive", "Peak gate current on/off (DS §9.6 formula)", `SiC ${f(ig(3.3, 2.2, 1.1))}/${f(ig(6.8, 0.3, 1.1))} A · IGBT ${f(ig(1, 2.2, 0.5))}/${f(ig(1, 0.3, 0.5))} A`,
    `${P.drv.ipk} A driver`, "PASS", "SiC 3.3/6.8 Ω, IGBT 1.0/1.0 Ω (SKU BOM); the IGBT sink sits at the driver's own 10 A limit");
  const qSic = 1.09e-6;   // HCS600 Fig.11: −5.1 → +15.6 V
  // bank capacity at 100 % of the CS limit, 92 % transfer efficiency: typ (1.0 V, 3.33 µH) and
  // worst (0.9 V CS, Lp −20 %) — the old "3.87 W" was the typical limit before losses
  const cap = (fosc, vcs = 1.0, lp = 10e-6 / 3) => 0.5 * lp * (vcs / 0.33) ** 2 * fosc * 0.92;
  for (const [tag, q, fs, fosc] of [["SiC @10 kHz", qSic, 10e3, 253e3], ["SiC @20 kHz option", qSic, 20e3, 253e3],
    ["IGBT @5 kHz (full 4.36 µC, RT 8.2 k)", P.igbt.qg, 5e3, 308e3]]) {
    const pBank = 3 * (q * span * fs + span * P.drv.icc2Typ + span ** 2 / 5.1e3) + 0.2;
    const cW = cap(fosc, 0.9, 8e-6 / 3);
    judge("Gate drive", `Gate-power demand per bank, ${tag}`, `${f(pBank, 2)} W`, `${f(cW, 2)} W worst-part capacity (typ ${f(cap(fosc), 2)} W) at ${f(fosc / 1e3, 0)} kHz`, pBank / cW, 0.8,
      "Qg·ΔV·f + ICC2 + 5.1 k bleeder per domain; F27: IGBT uses the full ±15 V Qg (no scaling); IGBT SKUs fit RT 8.2 k (~308 kHz)");
  }
  judge("Gate drive", "Positive gate clamp (18 V zener + Vf)", "18.8 V", "+22 V abs Vgs (SiC) / ±20 V (IGBT)", 18.8 / 20, 0.95);
  judge("Gate drive", "Negative gate clamp (5.1 V zener + Vf)", "−5.9 V", "−10 V abs Vgs", 5.9 / 10, 0.85);
  add("Gate drive", "HS DESAT sense point", "module aux drain pin 9 (DSH)", "-", "PASS", "F29 — real HCS600 pin map; kelvin sensing, no busbar drop in the trip level");
  add("Gate drive", "ASC drive level", "5.1 V clamp at ganged pins", "GND2+6 V abs", "PASS", "F28 — 2.2 k + zener from the +20 V opto rail");
  // DESAT trip level (collector/drain) and worst-case DETECTION time (review A.6 F03/F05)
  const d = P.drv, vf = 0.6;
  const trip = (r, i) => [d.vth[0] - 2 * vf - d.ichg[2] * r, d.vth[2] - 2 * vf - d.ichg[0] * r];
  const [tsMin, tsMax] = trip(100, 0), [tiMin, tiMax] = trip(4.7e3, 0);
  add("Gate drive", "DESAT trip at the switch (corners)", `SiC ${f(tsMin, 1)}–${f(tsMax, 1)} V ≈ ${f(tsMin / P.mod.rdsHot / 1e3, 1)}+ kA · IGBT ${f(tiMin, 1)}–${f(tiMax, 1)} V`,
    "-", "INFO", "short-circuit detection, not overload — halls + firmware own the operating current limit (F46)");
  for (const [tag, c, lim, soft] of [["SiC 47 pF", 47e-12, null, 0.46e-6 / 0.4], ["IGBT 82 pF", 82e-12, P.igbt.tsc, 106e-9 * (VCC2.hi - 10) / 0.4]]) {
    const tMin = c * 0.95 * d.vth[0] / d.ichg[2] + d.leb;
    const tDet = c * 1.05 * d.vth[2] / d.ichg[0] + d.leb + d.deg[2];
    // round 7, RR04/A6-R04: judged at BOTH soft-off corners; the 100 mA DS minimum does not close
    // against the 6 µs rating, and the 850 V derating is not documented — an OPEN release gate, not PASS
    if (lim) add("Gate drive", `DESAT worst detection + soft-off, ${tag}`, `${f((tDet + soft) * 1e6, 2)} µs @400 mA typ · ${f((tDet + soft * 4) * 1e6, 2)} µs @100 mA DS min (detect ${f(tDet * 1e6, 2)} µs)`,
      "tP ≤ 6 µs @800 V/15 V/175 °C (DS Table 5)", "WARN",
      `RELEASE GATE: typ closes (${f(100 * (tDet + soft) / 6e-6, 0)} % of 6 µs), the 100 mA corner does not — NOVOSENSE I_STO distribution + hiitio SC envelope at 850 V and the actual gate bias (§5: ${f(VCC2.nom, 1)} V nom, soft-off here from the ${f(VCC2.hi, 1)} V high corner) + contained SC test with integrated energy. Min blank ${f(tMin * 1e6, 2)} µs vs the turn-on tail (DPT)`);
    else add("Gate drive", `DESAT worst detection + soft-off, ${tag}`, `${f((tDet + soft) * 1e6, 2)} µs (detect ${f(tDet * 1e6, 2)} + STO ${f(soft * 1e6, 2)} @400 mA)`,
      "SiC tSC NOT published — vendor letter", "WARN", `min blank ${f(tMin * 1e6, 2)} µs; release gate: hiitio SC envelope at 850 V/150 °C/+${f(VCC2.hi, 1)} V (high corner) or a contained SC test (F04)`);
  }
  add("Gate drive", "Shoot-through lockout", "IN+/IN− complementary pairing", "-", "PASS", "verified structurally in erc-audit (12 checks)");
  // round 7 (RR01/RR02): both RC nodes now end in the 74LVC3G17-Q100 Schmitt buffer (no Δt/ΔV limit);
  // delays use its V_T−/V_T+ windows as fractions of V_CC, R ±1 %, C0G ±5 %, X7R −20/+15 %
  const tauDlo = 9.9e3 * 3.3e-9 * 0.95, tauDhi = 10.1e3 * 3.3e-9 * 1.05;
  const dLo = tauDlo * Math.log(1 / SCH.tnHi), dHi = tauDhi * Math.log(1 / SCH.tnLo), dRise = tauDhi * Math.log(1 / (1 - SCH.tpHi));
  const tSoft = 106e-9 * (VCC2.hi - 5.6) / P.drv.isto[0];   // IGBT Cies from the high-corner rail to below V_th at the DS-minimum I_STO
  judge("Gate drive", "Global DRV_EN drop after a DESAT vs the faulted driver's soft turn-off", `${f(dLo * 1e6, 0)}–${f(dHi * 1e6, 0)} µs RC delay (+0.4–0.8 µs FLT)`,
    `IGBT soft-off ${f(tSoft * 1e6, 1)} µs at the DS-minimum 100 mA`, tSoft / dLo, 1.0,
    "F71: NSI6611 DS is silent on RST/EN during soft turn-off — 10 k/3.3 nF to the USCH Schmitt threshold (V_T− 0.22–0.49 V_CC) makes the design independent of it; FS0B/MCU paths stay undelayed");
  const tOsLo = 9.9e3 * 15e-9 * 0.95 * Math.log(1 / (1 - SCH.tpLo)), tOsHi = 10.1e3 * 15e-9 * 1.05 * Math.log(1 / (1 - SCH.tpHi));   // CCLR C0G ±5 % (round 7 cross-check)
  const tReset = dRise + 0.8e-6 + 1.5e-6;   // FLT_OKD to V_T+ max, NSI6611 t_RST_FIL 0.8 µs, FLT/FLT_CMB pull-up rise
  judge("Gate drive", "Fault-latch CLEAR one-shot (15 nF into 10 k, at the USCH output)", `${f(tOsLo * 1e6, 0)}–${f(tOsHi * 1e6, 0)} µs low per falling edge`, `≥ ${f(tReset * 1e6, 0)} µs to deliver the drivers' reset edge through the delay`,
    tReset / tOsLo, 0.9,
    "review F06 disposition: PRE=CLR=L (both outputs high) is the ONLY way to give the NSI6611s their RST/EN rising edge while FLT is still asserted — a fault-dominant latch would deadlock recovery. The one-shot bounds one stuck-low pin in hardware; a re-pulsing pin is RR03 → FW-15 (eFlexPWM fault lock), the drivers' own latch and the FS26 watchdog");
  add("Gate drive", "Slow edges at LVC inputs (Δt/ΔV 5–10 ns/V)", "RC nodes and FS0B via USCH, FLT diode-OR and FS1B strap via USCH2, RDY_HS/LS via USCH3 (no limit) — none left", "every open-drain or RC edge on the safety chain", "PASS",
    "round 7 RR01/RR02: the two RC nodes were 14,000–63,500 ns/V — buffered. The remaining open-drain release edges only return a latch or AND input to its idle level with no output change (PRE release with CLR high holds; RDY releases while MCU_GATE_EN is low per §9 sequencing and FW-14)");
}

// ============ 5. GATE-POWER FLYBACKS (real VGT winding: NP:NF:NS = 1:1.6:2.9, Lp 10 µH) ============
{
  // round 7 (A6-R06/R07): the divider senses its own aux rectifier (FFS), so VDD is the aux plateau
  // minus the US1M drop and the start feed can no longer hold FB above the reference
  const vFfs = P.pwm.vref * (52.3 + 15) / 15, vdd = vFfs + 0.55 - 0.85;
  const vcc2 = VCC2.nom;
  judge("Flyback", "VDD in regulation (FFS 52.3k/15k, aux plateau − US1M)", `${f(vdd, 1)} V (FFS ${f(vFfs, 2)} V)`, `${P.pwm.vccAbs} V abs`, vdd / P.pwm.vccAbs, 0.75, "F33 — a 15 V target through NF would push the secondaries to ~27 V");
  const inWin = VCC2.lo >= 13.5 && VCC2.hi <= 17.0;
  add("Flyback", "Derived gate rail VCC2 (both diodes, all corners)", `${f(VCC2.nom, 1)} V nom · ${f(VCC2.lo, 2)}–${f(VCC2.hi, 1)} V (VEE −4.8…−5.4 V)`, "13.5–17.0 V bias window", inWin ? "WARN" : "FAIL",
    `A6-R06: VCC2 = (V_FFS + Vf_FS)·NS/NF − Vf_sec − Vz. The A.6 model dropped the aux diode (15.6 V claimed, ${f(VCC2.old, 1)} V real). Window: NSI6611 rec-min 13 V + margin; ≤ 17 V keeps the SC current near the 15 V DS data. The low corner sits at the window edge (US1M at peak current, FB bias) — BENCH GATE: six-domain VCC2 at start, full gate load, ASC and no-load, KL30 9–16 V, 24 V and 33 V`);
  const iqLow = 1.5e-3, vStopOld = (16 - 2.2e3 * iqLow) / (1 + 2.2e3 / 71e3);
  add("Flyback", "Restart after a stopped interval (start feed vs FB)", "FB from the aux-only FFS node", "the start feed must not hold FB ≥ 2.5 V", "PASS",
    `A6-R07: with the A.6 VDD sense a stopped converter sat at ${f(vStopOld, 1)} V (> the 11.83 V target) on a 16 V rail for a ${iqLow * 1e3} mA part (DS: 2.3 typ, no min), and at the 18 V clamp at a 24 V jump start — no restart, gate power lost. FFS decays through the 67 k divider (τ 6.7 ms) and the controller restarts`);
  const fsw = P.pwm.kOsc / (10e3 * 680e-12);
  add("Flyback", "Switching frequency (10k/680p)", `${f(fsw / 1e3)} kHz`, "-", "INFO", "F32 — Lp 10 µH demands small per-cycle energy; osc anchors per SLUS458I curves");
  const pOut = 3 * (P.qg.total * P.qg.vswing * OP.fsw + vcc2 * P.drv.icc2) + 0.3;
  const pin = pOut / 0.78;
  const ipkOp = Math.sqrt(2 * pin / ((P.xfmr.lp / 3) * fsw));  // DCM peak of the BANK: three 10 µH primaries in parallel (A6-R14)
  const ilim = P.pwm.vcs / 0.33;
  judge("Flyback", "DCM peak current vs CS limit (bank)", `${f(ipkOp, 2)} A op`, `${f(ilim, 2)} A limit (0.33 Ω)`, ipkOp / ilim, 0.75, "A6-R14 — the common switch/shunt carries all three primaries: bank Lp = 10 µH/3 (the old check used one transformer's 10 µH: 1.18 A)");
  judge("Flyback", "CS limit as the saturation guard", `${f(ilim, 2)} A`, `${P.xfmr.isat} A (Isat unpublished — guard band)`, ilim / P.xfmr.isat, 1.0, "bench-verify core at current limit");
  // start condition (review A.6 F18): the rail must push I_START AND the 71 k divider current into VDD at VDD_ON(max)
  const vNeed = (r) => P.pwm.uvloOnMax + r * P.pwm.istart;   // round 7: the 67 k divider hangs on FFS, not VDD
  const v12at9 = OP.kl30.min - 0.95;                     // two reverse Schottkys + 3 polyfuses at ~1 A
  judge("Flyback", "Start threshold at the 12 V node, worst (2.2 k)", `${f(vNeed(2.2e3), 2)} V needed`, `${f(v12at9, 2)} V at KL30 = 9 V`, vNeed(2.2e3) / v12at9, 0.99,
    `A.6 needed 7.95 V (divider on VDD). Burst-to-takeover energy is S1's job`);
  judge("Flyback", "Start resistor dissipation @24 V jump start", `${f((24 - vdd) ** 2 / 2.2e3, 3)} W`, "0.25 W (1206)", (24 - vdd) ** 2 / 2.2e3 / 0.25, 0.5,
    `${f((16 - vdd) ** 2 / 2.2e3 * 1e3, 0)} mW at 16 V; VDD sits at the aux-derived ${f(vdd, 1)} V`);
  // DCM shunt RMS = Ipk·√(D_on/3), D_on = t_on·f with t_on = L_bank·Ipk/V_in — worst at the 9 V-crank node
  const dOn = (P.xfmr.lp / 3) * ipkOp / (OP.kl30.min - 0.95) * fsw, pRcs = ipkOp ** 2 * (dOn / 3) * 0.33;
  judge("Flyback", "CS resistor power (bank, DCM)", `${f(pRcs, 3)} W`, "0.75 W (1210)", pRcs / 0.75, 0.6, `D_on ${f(dOn, 3)} at 8.05 V in; the old (Ipk/√3)² assumed a 100 % duty triangle`);
}

// ---------- rev A.4 corrections (external design review F37–F46) ----------
{
  // F37 transformer phasing is topological (locked by erc-audit); the numbers it protects:
  const vRefl = 21.4 / 2.9;             // secondary total reflected through NS -> NP
  judge("Flyback A.4", "Reflected voltage vs clamp-TVS standoff", `${f(vRefl, 1)} V`, "13 V SMAJ13A standoff",
    vRefl / 13, 0.75, "F38 — TVS must stay dark in normal OFF; dots per TDK p.3/9");
  const vDrainLD = 39 + 21.5 + 0.7;     // clamped load-dump rail + TVS clamp + blocking Vf
  judge("Flyback A.4", "Drain worst case (clamped load dump)", `${f(vDrainLD, 1)} V`, "80 V BUK7Y14-80E",
    vDrainLD / 80, 0.85, "F38 — replaces SMBJ85A (94.4 V min breakdown, forward path in OFF)");
  // F39 DESAT clamp direction is topological (erc-audit); F40 ASC latch levels:
  // round 7 (A6-R01): FS1B holds V_OL ≤ 0.4 V only up to 2 mA and may current-limit at 4 mA. Budget at
  // V5A 5.1 V, 1 % parts. Round 8 (A7-N04): FS1B has no own pull-up — the RFS1/RFS2 strap is its pull-up.
  const vOL = 0.4, v5 = 5.1;
  const iRenp = (v5 - vOL) / (5.1e3 * 0.99), iStrap = (v5 - vOL) / (11e3 * 0.99), iFout = (v5 - vOL - 0.3) / (9.9e3 + 0.99e3), iFs1b = iStrap + iFout;   // FAULT_OUT sinks through DFO (BAT46 ≈0.3 V)
  judge("Safety A.8", "FS1B load at its V_OL point (strap + FAULT_OUT)", `${f(iFs1b * 1e3, 2)} mA`, "2 mA (V_OL ≤ 0.4 V; current limit ≥ 4 mA)", iFs1b / 2e-3, 0.95,
    `A6-R01: the 1 k pulls took ${f((5 / 1e3 + 5 / 11e3) * 1e3, 2)} mA — a 4 mA-limit part sat at 1.33 V, ASC_SET_N at 1.67 V (> VIL); the old row compared with the 22 mA maximum and divided by 1000 again`);
  const vSetLow = vOL + (v5 - vOL) * 1.01e3 / (1.01e3 + 9.9e3), vTnMin = SCH.tnLo * 4.9;
  judge("Safety A.8", "ASC latch preset low level (FS1B at V_OL, 1k into 10k) vs USCH2 V_T−", `${f(vSetLow, 2)} V`, `${f(vTnMin, 2)} V V_T− minimum at V5A 4.9 V`,
    vSetLow / vTnMin, 0.85, "F40/A6-R01; round 8 buffers the preset (R7-02), so the Schmitt threshold, not the LVC VIL, is the limit");
  const vFltCmb = FLTL.vol + FLTL.bat46;
  judge("Safety A.8", "FLT diode-OR low level (V_OL + BAT46, cold) vs USCH2 V_T−", `${f(vFltCmb, 2)} V`, `${f(vTnMin, 2)} V V_T− minimum`,
    vFltCmb / vTnMin, 0.85, "round 8: DFLT1/2 Schottky — a 1N4148 (≈0.7 V cold) would put the node at the threshold");
  judge("Safety A.8", "FS0B load at its V_OL point (5.1 k into the USCH input)", `${f(iRenp * 1e3, 2)} mA`, "2 mA (V_OL ≤ 0.4 V)", iRenp / 2e-3, 0.6,
    "pin ≤ 0.4 V: under the SBC's own 0.7 V read-back threshold and the buffer's 1.0 V V_T− minimum");
  const vFoutLo = vOL + 0.3 + iFout * 1.01e3;
  judge("Safety A.8", "FAULT_OUT asserted level at the VCU (10 k to 5 V)", `${f(vFoutLo, 2)} V`, "1.5 V (5 V CMOS V_IL)", vFoutLo / 1.5, 0.85,
    "sink-only through DFO: the VCU must pull up (firmware-contract §9)");
  // KL30 short on FAULT_OUT with FS1B released: DFO + RFS4 + RFS1 (1.98 k low) into ZSET; RFS2 back to V5A.
  // ZSET BZT52-B5V6 (Nexperia): 5.71 V max at 5 mA, S_Z ≤ +2.5 mV/K (→ 125 °C), r_dif ≤ 40 Ω; the current out
  // through RFS2 is ignored (more zener current = higher clamp). 16 V KL30 max, 24 V jump start, 35 V suppressed
  // load dump — each with the wire already shorted (cross-check R8X-05: a C5V6 reached 6.62 V at 35 V).
  const ZS = { v5: 5.71 + 100 * 2.5e-3, rz: 40, r: 1.98e3 };
  const vzAt = (vk) => (ZS.v5 - 5e-3 * ZS.rz + ZS.rz * (vk - 0.3) / ZS.r) / (1 + ZS.rz / ZS.r);
  const [vz16, vz24, vz35] = [16, 24, 35].map(vzAt), iZ35 = (35 - 0.3 - vz35) / ZS.r;
  const iBackLive = (vz35 - 4.9) / 10e3, iBackDead = vz35 / 10e3;
  judge("Safety A.8", "FAULT_OUT shorted to KL30 (FS1B released): ASC_SET_N clamp vs USCH2 V_I abs max",
    `${f(vz16, 2)} / ${f(vz24, 2)} / ${f(vz35, 2)} V at 16 / 24 / 35 V (125 °C)`, "6.5 V abs max (74LVC3G17)", vz35 / 6.5, 0.99,
    `to ground: blocked by DFO. Into V5A ≤ ${f(iBackLive * 1e3, 2)} mA live / ${f(iBackDead * 1e3, 2)} mA with V5A off (disabled LDO2 discharges it through 20–60 Ω: ≤ 40 mV). RFS1/RFS4 ${f(iZ35 * 1e3, 1)} mA for the ≤ 0.4 s pulse, ${f((24 - 0.3 - vz24) / ZS.r * 1e3, 1)} mA at a jump start (short-time overload of the 0603s — accepted for a shorted wire). A7-N04: the round-7 BAT46 into V5A back-fed a sleeping rail with ≈8 mA. Vishay alt BZT52B5V6 (+6·10⁻⁴/K): ≤ 6.43 V`);
  judge("Safety A.8", "ASC break-before-make: HS off before LS on", `LS starts ≥ ${f(ASC.tLsMin * 1e6, 2)} µs after the latch sets`,
    `HS off by ${f((ASC.tHsEn + 2.5e-6) * 1e6, 2)} µs (${f(ASC.tHsEn * 1e6, 2)} µs to EN + 2.5 µs IGBT dead time)`, (ASC.tHsEn + 2.5e-6) / ASC.tLsMin, 0.9,
    "RR05, FS1B path shown (FS0B → USCH → ANDs → EN); the MCU path is faster (eFlexPWM fault on the high-side outputs → IN+ low, tpHL ≤ 0.13 µs), and its low sides come on by PWM after the dead time. EN stays high on the MCU path so LS DESAT keeps priority (DS §8.12). SiC dead time is 1.0 µs — more margin");
  add("Safety A.8", "ASC entry, latch set → LS gates on (worst)", `${f(ASC.tEntryMax * 1e6, 2)} µs`, "counted in the FW-06 budget (§2b)", "INFO",
    "release ≤ 0.75 µs (TLP152 tpHL 0.19 µs + DASCR discharge + tASC_f 0.48 µs); exit is MCU-sequenced (FW-06a)");
  // the mask must release the healthy low sides' ASC pins before the fault latch drops DRV_EN, or they sit in
  // EN-low ASC, where the LS DESAT is not documented (cross-check R8X-08: this row used to be a fixed PASS)
  const tMask = 5.4e-9 + 5.5e-9 + 0.75e-6, tDropMin = 9.9e3 * 3.3e-9 * 0.95 * Math.log(1 / SCH.tnHi);
  judge("Safety A.8", "Latched driver FLT masks ASC on every path (UASCG) before DRV_EN drops",
    `ASC_CMD low ≤ ${f((tMask - 0.75e-6) * 1e9, 0)} ns, LS ASC pins released ≤ ${f(tMask * 1e6, 2)} µs`, `DRV_EN drop ≥ ${f(tDropMin * 1e6, 0)} µs after FLT`, tMask / tDropMin, 0.5,
    "round 8 R7-01/A7-N01: the faulted NSI6611 holds its own gate off through IN-low and EN-low with ASC high (DS Fig. 8.11); the gate removes ASC from the HEALTHY low sides and the eFlexPWM fault forces IN low, so the bridge reaches SPO (FS1B-ASC included). The MCU re-enters ASC only through §4c after the FW-15 reset. Wiring locked in erc-audit");
  // both TLP152 LEDs, from buffered 5 V logic: I = (V_CC − I_pd·R_out − V_F)/(R + R_out), R 261 R ±1 %, the
  // far-end 10 k pulldown (RPD8/RPD9) loading the same output; temperature-consistent corners (R8X-04)
  const rLed = 261;   // RASCL = RQDL (ERC-locked)
  const iLedAt = (rOut, vf) => (4.9 - 0.5e-3 * rOut - vf) / (rLed * 1.01 + rOut);
  const iLedCold = iLedAt(LVC.rOutC, LED.vfMax), iLedHot = iLedAt(LVC.rOut, LED.vfMaxHot);
  const iLed = Math.min(iLedCold, iLedHot), iLedMax = (5.1 - LED.vfMin) / (rLed * 0.99);
  const ledSt = iLed < 1.25 * LED.iFlh || iLedMax > LED.iAbs ? "FAIL" : iLed >= LED.iRecMin && iLedMax <= LED.iRecMax ? "PASS" : "WARN";
  for (const [tag, drv] of [["ASC opto (RASCL, from UASCG)", "74LVC1G08-Q100"], ["discharge opto (RQDL, from USCH2 ch3)", "74LVC3G17-Q100"]])
    add("Safety A.8", `TLP152 LED current — ${tag}`, `${f(iLedCold * 1e3, 2)} mA cold · ${f(iLedHot * 1e3, 2)} mA hot · ${f(iLedMax * 1e3, 2)} mA max`,
      "guaranteed: ≥ 1.25 × I_FLH 7.5 mA, ≤ 20 mA abs · recommended 10–15 mA", ledSt,
      `${drv}: guaranteed margins ${f(iLed / LED.iFlh, 2)}× over I_FLH and ${f(LED.iAbs / iLedMax, 2)}× under the abs max; the 10–15 mA window closes only with the ±28 % V_F tempco band (typical-derived — T7-04 bench). Cold = −40 °C V_F ${f(LED.vfMax, 2)} V with R_out ≤ ${f(LVC.rOutC, 1)} Ω (V_OH ≥ 3.8 V at −32 mA, −40…85 °C); hot = 100 °C V_F ${f(LED.vfMaxHot, 2)} V with R_out ≤ ${f(LVC.rOut, 1)} Ω (125 °C); max at V5A 5.1 V, V_F ${f(LED.vfMin, 3)} V, R_out 0. R7-03/A7-N03: 470 R from the MCU pin gave 6.4–6.8 mA; cross-check R8X-04: 270 R mixed temperatures and dipped to 9.9 mA cold`);
  // ---- round 10 (S9-01): RASCG carries the opto-to-clamp current for as long as ASC is held (minutes at speed):
  // (QA01C-18 top − ZASC low end)² / R at −1 %, against the ESR03 rating derated to 85 °C
  {
    const pAscg = (QA18.max - 4.8) ** 2 / (2.2e3 * 0.99), pRat = 0.33 * (155 - 85) / (155 - 70);
    judge("Safety A.9", "RASCG continuous dissipation while ASC is held (QA01C-18 top into the ZASC clamp)", `${f(pAscg * 1e3, 0)} mW`,
      `${f(pRat * 1e3, 0)} mW at 85 °C (ESR03EZPF2201, 0.33 W at 70 °C)`, pAscg / pRat, 0.8,
      `S9-01: a generic 0603 rated 0.1 W at 70 °C allows only 82 mW at 85 °C. 2.2 k is kept: it sets the ASC break-before-make RC with CASCD`);
  }
  // ---- round 9 (A8-03): RFS4 with FAULT_OUT shorted to KL30 WHILE FS1B is asserted (it sinks at 0.4 V up to its
  // 4–22 mA limit; the 22 mA end is the stress case). Above the limit the node rises, and past the ZSET clamp RFS1
  // shares the current. The 16 V case is the continuous one: FS1B held with no release (MCU never boots, or DFS).
  // BACKUP_SAFETY_PATH_FS1B = 0 (FW-12) removes the RSTB repetition, so a pulse is ≤ 0.1 s (TDUR) or the ≤ 0.3 s
  // boot hold. Part: ROHM ESR03EZPF1001, 0.33 W at 70 °C → 0.27 W at 85 °C, overload 2 × U_R for 5 s ≈ 1.3 W.
  {
    const iLim = 22e-3, vzC = vzAt(16) - 0.3;   // clamp near 3 mA, a little under its 5 mA value
    const pAt = (vw) => { const i0 = (vw - 0.3 - 0.4) / 1e3; if (i0 <= iLim) return i0 ** 2 * 1e3;
      const vn1 = vw - 0.3 - iLim * 1e3, vn = vn1 > vzC ? (vn1 + vzC) / 2 : vn1; return ((vw - 0.3 - vn) / 1e3) ** 2 * 1e3; };
    const [p16, p24, p35] = [16, 24, 35].map(pAt), pCont = 0.33 * (155 - 85) / (155 - 70);
    judge("Safety A.9", "RFS4 with FAULT_OUT shorted to KL30 and FS1B asserted (22 mA limit end)", `${f(p16, 3)} / ${f(p24, 3)} / ${f(p35, 3)} W at 16 / 24 / 35 V`,
      `${f(pCont, 2)} W continuous at 85 °C (ESR03, 0.33 W at 70 °C); ≈1.3 W for 5 s overload`, p16 / pCont, 0.9,
      `A8-03: 0.23 W was only the 16 V case. Pulses ≤ ${f(p35, 2)} W last ≤ 0.1 s (FS1B_TDUR) or the ≤ 0.3 s boot hold — ≤ ${f(p35 * 0.3, 2)} J against the 5 s overload rating; the 16 V row is the continuous case (no release). BACKUP_SAFETY_PATH_FS1B = 0 stops RSTB loops. A 0.1 W 0603 is 0.08 W at 85 °C and fails the continuous case`);
  }
  // R9X-14: FS1B HELD for a whole key-on — a high-limit FS1B current-limits at 24 V (node ≈1.7 V reads high:
  // ABIST refuses the release). 18 V/60 min (ISO 16750-2, T_max − 20 °C ≈ 65 °C) and a 24 V/60 s jump start at 25 °C.
  {
    const p18 = ((18 - 0.7) / 1e3) ** 2 * 1e3, p24 = (22e-3) ** 2 * 1e3;
    const rth = (155 - 70) / 0.33, tEl24 = 25 + p24 * rth;   // element temperature from the rating's own slope
    add("Safety A.9", "RFS4 with FS1B held a whole key-on (18 V/60 min at 65 °C · 24 V/60 s at 25 °C)", `${f(p18, 2)} W · ${f(p24, 2)} W (element ≈${f(tEl24, 0)} °C)`,
      "0.33 W continuous at ≤ 70 °C (ESR03); 155 °C element", p18 > 0.33 || tEl24 > 155 ? "FAIL" : p24 > 0.33 ? "WARN" : "PASS",
      `R9X-14: 18 V fits. The 24 V jump start runs the resistor at ${f(p24 / 0.33, 2)}× its nameplate for 60 s — under its 155 °C element limit at 25 °C but outside the rating, in a triple condition (FAULT_OUT shorted to KL30, a high-limit FS1B part, a jump start). Drift or an open only disconnects FAULT_OUT from an already-shorted wire, and the FS1B preset keeps working through the strap. Accepted; bench item`);
  }
  // ---- round 9 (A8-N03 + cross-check R9X-07): FW-16 energy eligibility. The low-voltage V_DC reading is not
  // trusted: the chain is ≈ ±9 V uncalibrated at this level (MCU ADC alone ±3.3 V of bus). Either both channels
  // read < 3 V (≤ 12 V true), or QDIS runs for 2 τ from a < 60 V reading (≤ 69 V true → ≤ 9.3 V).
  for (const id of ["sic8", "sic4"]) {
    const sk = SKU[id], cMax = 16 * sk.can.c * 1.1 + 3.3e-6, ra = sk.vMax === 850 ? 1880 : 880, tau = ra * 1.05 * cMax;
    const eRead = 0.5 * cMax * 12 ** 2, eTop = 0.5 * cMax * (69 * Math.exp(-2)) ** 2, e = Math.max(eRead, eTop);
    judge("Safety A.9", `FW-16 self-test residual energy, ${sk.vMax === 850 ? "8XX" : "4XX"} bank (read < 3 V, or QDIS 2 τ from < 60 V)`,
      `≤ ${f(eRead * 1e3, 0)} mJ (read) · ≤ ${f(eTop * 1e3, 0)} mJ (QDIS 2 τ = ${f(2 * tau, 2)} s)`, "0.1 J design limit", e / 0.1, 0.8,
      `A8-N03: "< 60 V" allowed ${f(0.5 * cMax * 59 ** 2, 2)} J at C_max ${f(cMax * 1e6, 1)} µF. R9X-07: the first "< 12 V read" rule assumed a 1 V error — it can be 9 V. n_ss from E_LL,pk(n_ss) ≤ 12 V. Fixture-qualified, not a destructive test`);
  }
  // ---- round 9, self-found N17 + cross-check R9X-03/04/10: power-board LV feed switch QLVS (DMP6023LEQ: −60 V,
  // 28 mΩ at −10 V, 35 mΩ at −4.5 V, V_GS(th) −1…−3 V, ±20 V, I_DSS ≤ 1 µA; DS39935). Gate from NRC (KL30 −
  // ≈0.5 V: reverse Schottky + polyfuse): 4.7 k to the V5A-driven 2N7002, 10 k gate-source, BZT52-C15 clamp,
  // and 1 k + 100 nF drain-gate for a controlled turn-on slew. Sleep: V5A off → gate at source → off.
  {
    const nrc = (vk) => vk - 0.5, k = 10 / 14.7, rTh = 10e3 * 4.7e3 / 14.7e3;
    const vgs = (vk) => Math.min(nrc(vk) * k, 15.6), [g6, g9, g16, g39] = [6, 9, 16, 39].map(vgs);
    judge("LV A.9", "LV feed switch V_GS from NRC: KL30 6 / 9 / 16 / 39 V (clamped)", `−${f(g6, 1)} / −${f(g9, 1)} / −${f(g16, 1)} / −${f(g39, 1)} V`,
      "±20 V V_GS max; ≥ 4.5 V for the 35 mΩ point", Math.max(g39 / 20, 4.5 / g9), 0.8,
      `at the 9 V crank floor the gate sits ${f(g9, 1)} V below the source (≤ 35 mΩ; 0.6 A → ≤ 13 mW); even at 6 V (${f(g6, 1)} V) it stays past V_GS(th) max 3 V. R9X-10: taken from NRC, not KL30`);
    const iLeak = 50e-6;   // 2N7002 off-state drain leakage at 85 °C, V_DS ≈ 12 V (vendors give ≤ 1 µA at 25 °C, up to 500 µA at 125 °C)
    judge("LV A.9", "LV feed switch held off by a hot 2N7002 (leakage × 10 k gate-source)", `${f(iLeak * 10e3, 2)} V at ${iLeak * 1e6} µA`,
      "1.0 V V_GS(th) min", iLeak * 10e3 / 1.0, 0.6, "R9X-10: with the first 100 k the same leakage made 5 V — the switch could half-close in a hot parked car");
    const vPl = 3.0, cL = 45e-6, cM = 100e-9;   // Miller plateau (low end → largest gate current), power-board input capacitance
    const [i12, i16] = [12, 16].map((vk) => cL * ((nrc(vk) * k - vPl) / rTh) / cM);
    judge("LV A.9", "LV feed switch inrush into the power board at each wake (drain-gate 100 nF slew)", `${f(i12, 2)} A at 12 V · ${f(i16, 2)} A at 16 V`,
      "3 A (polyfuses, harness, VBATC dip)", i16 / 3, 0.6,
      `R9X-04: slew = gate current / 100 nF ≈ ${f(((nrc(12) * k - vPl) / rTh) / cM / 1e3, 0)} V/ms at 12 V; without it the gate-drain charge alone let 9–31 A through (2.5–4.4 mJ) and dipped VBATC ~2 V at every wake`);
    // whole-inverter sleep current with KL30 present (FS26 in LPOFF): FS26 30 µA typ (60 max at 85 °C), ULDOEX
    // inhibited ≤ 10 µA, QLVS I_DSS ≤ 1 µA, 2N7002 ≤ 1 µA, TVS ≤ 1 µA at 25 °C; the power board is unpowered
    const iSleep25 = 30e-6 + 10e-6 + 1e-6 + 1e-6 + 1e-6, iSleep85 = 60e-6 + 10e-6 + 1e-6 + iLeak + 5e-6;
    const iQa = 2 * 16e-3 * 15 / 0.85 / 12;
    judge("LV A.9", "Parking drain, whole inverter (KL30 present, FS26 in LPOFF)", `≤ ${f(iSleep25 * 1e6, 0)} µA at 25 °C · ≤ ${f(iSleep85 * 1e6, 0)} µA at 85 °C`,
      "0.1 mA at 25 °C (OEM sleep budgets ≤ 0.1–1 mA per ECU)", iSleep25 / 0.1e-3, 0.8,
      `N17 + R9X-03: before, the power board's LV side drew ≥ ${f(iQa * 1e3, 0)} mA from the QA01C-18 no-load inputs alone (≈150 mA in all) and ULDOEX kept the exciter at ≈0.9 mA; both now follow V5A. S1 start-up already starts VDD at 0 V, so a switched feed costs no start time`);
  }
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
  judge("LV A.7", "CB15O1/2 (V15B) at clamped load dump", "≈33 V", "50 V MLCC rating",
    33 / 50, 0.8, "round 7 RR10 — V15B follows V12L−Vf in pass-through; the 25 V parts were overstressed");
  judge("LV A.4", "ULDO15 dissipation @24 V sustained", `${f((24 - 0.5 - 15) * 0.33, 1)} W`, "TSD-protected (survival case, not an operating mode)",
    0.5, 0.9, "jump start is stationary service — brief V15 brown-out via TSD is acceptable; passive bleeder unaffected");

  // ---- rev A.4.2 (third review round, F52–F57) ----
  // FS26 buck passives: effective capacitance at bias/temperature/tolerance corners
  const effV15S = 44 * 0.8 * 0.9;      // 2x22 µF at 1.5 V bias (negligible) · -20 % tol · temp
  judge("LV A.4", "VCORE COUT effective (2×22 µF @1.5 V)", `${f(effV15S, 0)} µF`, "20–100 µF eff (Table 106)",
    20 / effV15S, 0.9, "F52 — was 10 µF nominal; inductor now 2.2 µH per CORE_LSEL_OTP");
  const effVPRE = 44 * 0.8 * 0.75;     // 2x22 µF at ~6 V bias on 16 V dielectric
  judge("LV A.4", "VPRE COUT effective (2×22 µF @6 V)", `${f(effVPRE, 0)} µF`, "≥22 µF test condition",
    22 / effVPRE, 0.95, "F52");
  const effVIN = 22 * 0.8 * 0.65;      // 22 µF 25 V 1210 at 14 V bias
  judge("LV A.4", "VPRE input effective (22 µF @14 V)", `${f(effVIN, 1)} µF`, "≥10 µF eff",
    10 / effVIN, 0.95, "F52 — CLVC2 4.7 µF was under the input spec");
  // CSB5 1 µF + CMA1 1 µF + CMA2 0.1 µF (round 12, R2-F11: 2.2 µF put the nominal rail AT the 3.3 µF upper limit)
  // CSB5 (0805) at −15 % bias, CMA1 (0603) at −30 %, CMA2 0.1; −10 % tolerance, −15 % at −40 °C, −5 % aging;
  // max: +10 % tolerance, +5 % after reflow, +15 % X7R over temperature, no bias credit
  const minVREF = (1.0 * 0.85 + 1.0 * 0.70 + 0.1) * 0.9 * 0.85 * 0.95, maxVREF = 2.1 * 1.1 * 1.05 * 1.15;
  add("LV A.4", "VREF5 rail effective (whole rail: 2.1 µF nom)", `${f(minVREF, 2)}–${f(maxVREF, 2)} µF`, "1.1–3.3 µF eff window (FS26 COUT_VREF)",
    minVREF >= 1.1 && maxVREF <= 3.3 ? "PASS" : "FAIL", "round 12 (R2-F11): 2.2 µF put the nominal rail AT the 3.3 µF limit and the A.4 row never looked at the +tolerance corner; CMA1 stays 1 µF as the reservoir at the MCU VREFH pins (AN5032)");
  // ULDO15 / ULDOEX compensation zeros vs onsemi 11–18 kHz guidance
  judge("LV A.4", "ULDO15 feed-forward zero (49.9k·220pF)", `${f(1 / (2 * Math.PI * 49.9e3 * 220e-12) / 1e3, 1)} kHz`,
    "11–18 kHz (onsemi Cb guidance)", 14.5 / 18, 0.95, "F54 — COUT 22 µF ceramic");
  judge("LV A.4", "ULDOEX feed-forward zero (38.3k·270pF)", `${f(1 / (2 * Math.PI * 38.3e3 * 270e-12) / 1e3, 1)} kHz`,
    "11–18 kHz (onsemi Cb guidance)", 15.4 / 18, 0.95, "F55");
  judge("LV A.4", "VEXD target for ALM2402", `${f(2.5 * (1 + 38.3 / 10), 2)} V`, "16 V recommended max (18 V abs)",
    (2.5 * (1 + 38.3 / 10)) / 16, 0.85, "F55 — was raw VBATC: 24 V jump start exceeded abs max");
  // Gate-power switch drive legality + clamp loading
  judge("Flyback A.4", "QF gate drive vs BUK7Y14-80E VGS abs", "11.8 V", "±20 V DC (was BUK9Y: ±10 V)",
    11.8 / 20, 0.8, "F56 — logic-level part was outside abs max at the VDD drive");
  add("Flyback A.4", "Gate zener standing load", "0 W (BZT52-C15 dark at the ≈11 V VDD)", "was ~0.4 W/zener all ON-time", "PASS",
    "F56 — the 5.6 V clamp conducted ~0.29 A through every ON interval (historical estimate, not carried into the new budget)");

  // ---- rev A.5 documentation-audit findings (F60–F61) ----
  // QDIS gate (round 9, A8-N02): PSQD QA01C-18 → TLP152 (V_OH ≤ V_CC; ≥ V_CC − 1.5 V at the ≈1.5 mA static load,
  // DS 6.0 V min at 10 V/−100 mA) → RQDG 1.5 k / RQDPD 10 k divider. The old row read the wrong module (+20 V).
  const vgsMax = QA18.max * 10.1e3 / (10.1e3 + 1.485e3), vgsMin = (QA18.min - 1.5) * 9.9e3 / (9.9e3 + 1.515e3);   // 1 % divider corners
  judge("Discharge", "QDIS gate V_GS (QA01C-18 envelope through the 1.5 k/10 k divider) vs HCM75S12T4K3 +22 V abs",
    `${f(vgsMin, 1)}–${f(vgsMax, 1)} V (rail ${f(QA18.min, 1)}–${f(QA18.max, 1)} V)`, "+22 V abs max (+18 V recommended)", vgsMax / 22, 0.85,
    `round 9 A8-N02: 47 Ω passed the rail straight to the gate — up to ${f(QA18.max, 1)} V at this 2–4 % load, above the +18 V recommended level with the < 10 % load region extrapolated. The divider keeps the top at the recommended level and ${f(22 - vgsMax, 1)} V under the abs max; the low end fully enhances a 0.45 A discharge. BENCH: V18Q, QDVO and V_GS at start-up, no load and ON`);

  // ---------- IGBT SKUs: HCG600FH120D3E1EA (same D3 pads + pin map) ----------
  // Losses/thermal/DESAT timing now live in the per-SKU blocks above (review A.6 F03/F26/F27);
  // what stays here is what the silicon swap itself must satisfy.
  judge("IGBT SKUs", "Gate rails legality (+15.6/−5.1)", "on 15.6 V · off −5.1 V", "±20 V abs; VGE(th) min 5.0 V",
    15.6 / 20, 0.85, "DS characterizes at ±15; high Vth + Miller clamp justify −5.1 off-bias — dv/dt shoot-through is a DPT row");
  add("IGBT SKUs", "Pin map / footprint", "IDENTICAL to HCS600FH120D3C1 (DS p.8: 1=G_L 2=E_L 3=DC− 4=DC+ 5/6=NTC 7=G_H 8=E_H 9=C-sense 10/11=AC)", "-", "PASS",
    "zero layout change; MODx pinLabels carry over (KS labels = Kelvin emitter)");
  add("IGBT SKUs", "Short-circuit rating used for DESAT timing", "tP ≤ 6 µs @800 V, 175 °C, VGE 15 V (DS Table 5)", "-", "PASS",
    "F03: docs and S9 carried a 10 µs class; 850 V/15.6 V operation shortens it — treated as ≈5 µs");

  // ---- rev A.4.3 (fourth review round, F58–F59) ----
  const D15 = 1 - 12 / 15.4, Rld15 = 15.4 / 0.33;
  const fRHPZ = (1 - D15) ** 2 * Rld15 / (2 * Math.PI * 10e-6) / 1e3;
  judge("LV A.4", "UB15 comp zero (2k·100nF) vs output pole", "796 Hz vs ~140 Hz", "fZ slightly above fP (TI rule)",
    0.6, 0.9, "F59 — series RC replaces the lone 10 nF (screening phase margin ~0°); bench Bode gates it");
  add("LV A.4", "UB15 RHP zero @12 V/0.33 A", `${f(fRHPZ, 0)} kHz`, "far above the loop crossover", "PASS",
    "F59 — light-load boost: RHPZ not the constraint");
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
  // F37: correlated top string (one lot, one temperature) does NOT shrink by √6 — worst case is linear.
  const top = P.tolRp, bot = 0.001, amc = P.amc.gainErr + P.amc.vos / 1.87, rx = 2 * 0.001, ref = 0.005;
  const gWc = top + bot + amc + rx + ref, gRss = Math.sqrt((top / Math.sqrt(6)) ** 2 + bot ** 2 + amc ** 2 + rx ** 2 + ref ** 2);
  judge("Sensing", "VDC chain error, WORST CASE uncalibrated", `±${f(gWc * 100, 2)} % (±${f(gWc * 880, 0)} V at the 880 V OV trip)`, "OV trip below the 1000 V can rating",
    (880 * (1 + gWc)) / 1000, 0.95, `top 1 % correlated + bottom 0.1 % + AMC1311B 0.2 %+offset + receiver 0.2 % + VREF5 0.5 %; RSS would claim ±${f(gRss * 100, 2)} %`);
  add("Sensing", "VDC chain error after EOL gain/offset calibration", "≈±0.3 % (residual drift/nonlinearity)", "5 % cross-check window", "PASS",
    "calibrated values feed protection only after the stored record passes CRC + range checks (F42)");
  add("Sensing", "Shared receiver offset VOFS monitored", "UVOF output → MCU ADC (PTB1)", "±5 % of 0.5 V", "PASS",
    "F11: a failed UVOF would shift BOTH channels by up to 0.5 V (≈228 V) and pass the 5 % cross-check — now read directly; BMS pack voltage is the third witness when contactors are closed");
  const pDiv = OP.vbusMax ** 2 / (6 * 470e3 + 6.2e3);
  judge("Sensing", "Divider dissipation @850 V", `${f(pDiv * 1e3)} mW total`, "6× 1206 (250 mW ea)", (pDiv / 6) / 0.25, 0.5, `${f(OP.vbusMax / 6)} V per 200 V-rated 1206 — 71 %`);
  const vHall = P.hall.v0 + OP.iphPk * Math.SQRT2 * P.hall.sens;
  judge("Sensing", "Hall output at 480 A pk", `${f(vHall, 2)} V`, "0.3–4.7 V buffer swing", (vHall - 2.5) / 2.2, 0.75);
  add("Sensing", "Hall ratiometric ref vs ADC ref", "V5S(V5A) vs VREF5", "-", "WARN", "two 5 V sources — ~±1–2 % gain drift between them; calibrate at EOL or move VREFH to V5A (GEN3 ships the same topology)");
  // HVIL signatures
  add("Sensing", "HVIL signatures (drive hi/lo/open)", "3.0 / 2.0 / 2.5 V", "-", "PASS", "distinct at ±5 % R tolerance (worst separation 0.38 V)");
  const vmp = 4.0 * 12.1 / (12.1 + 4.99), vmn = 4.0 * 24 / (24 + 4.99);
  judge("Sensing", "Resolver monitor dividers @4 V pk", `${f(vmp, 2)} / ${f(vmn, 2)} V`, "5 V SDADC input", Math.max(vmp, vmn) / 5, 0.85);
  add("Sensing", "Resolver drive @9 V KL30", "≈6.5 V pp available vs 8 V pp target", "-", "WARN", "ALM2402 swing at cold-crank INCLUDING ULDOEX dropout (~0.3 V @ ~150 mA, A.4.3) — angle still tracks (amplitude-invariant demod); GEN3-equivalent behavior");
}

// ============ 7b. ROUND 12 — system review of 4544715 (docs/review-A11-disposition.md) ============
{
  // R1-F01/R2-F08: SPO (all switches off) with the battery path gone — the module diodes rectify the stored
  // winding energy 1.5·L·I² into the isolated link before the current is gone. Rule (a) of §6 now needs
  // the link headroom ½·C_min·(U_N² − V_max²) to cover it. Screening motor from S6 (0.35 mH); the real
  // L_d/L_q(i), ψ_f and n_max are a commissioning input (the back-EMF work during the decay adds to this).
  const Lscr = 0.35e-3;
  for (const s of [SKU.sic8, SKU.igbt4]) {
    const cMin = 16 * s.can.c * 0.9 + 3e-6, v0 = s.ovTrip, head = 0.5 * cMin * (s.can.vr85 ** 2 - v0 ** 2);
    const wMag = 1.5 * Lscr * s.iPk ** 2, iOk = Math.sqrt(head / (1.5 * Lscr));
    const vEnd = Math.sqrt(v0 ** 2 + 2 * wMag / cMin);   // the E = 0 case of the §6 bound V_pk ≤ E + √((V₀−E)² + 2·W/C)
    add("System A.11", `SPO freewheel energy at ${s.iPk} A rms into the isolated link (${s.name}, screening motor 0.35 mH, zero back-EMF)`,
      `${f(wMag, 0)} J → ${f(vEnd, 0)} V from the ${v0} V trip`, `${f(head, 1)} J headroom to ${s.can.vr85} V U_N at C_min ${f(cMin * 1e6, 0)} µF`,
      wMag > head ? "WARN" : "PASS",
      `round 12: rule (a) of §6 covers this motor only up to ${f(iOk, 0)} A rms at zero back-EMF; with back-EMF the decay is slower and the generated work adds 19–76 J (Opus check: 1098–1301 V at 340 A for E_LL,pk 300–800 V), and at E ≥ V₀ the current does not decay at all — a diode-bridge simulation with the real L_d(i)/L_q(i), ψ_f, R_s decides; otherwise rule (b). The reviewers' 300 µH/20 mΩ ODE from 850 V ends at 1038 V (8XX) / 667 V (4XX)`);
  }
  // R2-F04: resolver exciter — textbook MFB (R1 10k in, R2 24k feedback, R3 10k, C1 1.5 nF to ground, C2 220 pF feedback)
  {
    const R1 = 13e3, R2 = 24e3, R3 = 10e3, C1 = 1.5e-9, C2 = 220e-12, w = 2 * Math.PI * 10e3;   // R1 = REXA1 + REXA2: the 100 nF coupling is a short at 10 kHz
    const H = (c1, c2) => { const a = 1 - w * w * c1 * c2 * R2 * R3, b = w * c2 * (R2 + R3 + R2 * R3 / R1); return (R2 / R1) / Math.hypot(a, b); };
    const h = H(C1, C2), hs = [0.9, 1.1].flatMap((a) => [0.9, 1.1].map((b) => H(C1 * a, C2 * b)));
    const f0 = 1 / (2 * Math.PI * Math.sqrt(C1 * C2 * R2 * R3)), Q = Math.sqrt(C1 * C2 * R2 * R3) / (C2 * (R2 + R3 + R2 * R3 / R1));
    const swg = [1.884, 2.093, 2.302];   // S32K39 Table 40 MAXAPP min/typ/max, V pk-pk
    add("Sensing A.11", "Resolver excitation at 10 kHz (SWG → MFB → ALM2402 H-bridge)", `|H| ${f(h, 2)} (${f(Math.min(...hs), 2)}–${f(Math.max(...hs), 2)} over ±10 % caps) → ${f(2 * swg[1] * h, 1)} V pp differential (${f(2 * swg[0] * h, 1)}–${f(2 * swg[2] * h, 1)} over the SWG MAXAPP range)`,
      "8 V pp target; SWG amplitude register is the firmware knob (0.39–2.30 V pp)", "PASS",
      `f0 ${f(f0 / 1e3, 1)} kHz, Q ${f(Q, 2)}, passband gain −${f(R2 / R1, 2)}. 24 k is the ceiling: the ALM2402's ≈0.13 V/µs slew at −40 °C caps each output near 2.07 V pk (8.3 V pp). Round 12 (R2-F04): the A.10 network had the MFB feedback pair swapped — 4.7 nF from the output to the summing node and 24 k to the inverting input — a first-order 2 kHz roll-off with |H(10 kHz)| 0.18, i.e. ${f(2 * swg[1] * 0.178, 2)} V pp from the SWG maximum. OPA348: 1 MHz GBW gives ≈ ${f(1e6 / 10e3 / (1 + R2 / R1), 0)}× loop gain at 10 kHz; slew needed ${f(Math.PI * 10e3 * 2 * swg[1] * h, 3)} V/µs of 0.5`);
  }
  // R1-F03: hall open-wire signature — 100 k to AGND at the card input
  {
    const tau = 100e3 * 3.3e-9, t = tau * Math.log(2.5 / 0.3);
    add("Sensing A.11", "Hall signal open wire (R⟨ph⟩B0 100 k)", `reads 0 V; 2.5 → 0.3 V in ${f(t * 1e3, 2)} ms`, "outside the HC5FW 0.2–4.8 V window (FW-05); RL ≥ 10 k", "PASS",
      "round 12 (R1-F03): the README had claimed the pull-down since A.4; an unpowered sensor reads the same 0 V. The Σi = 0 check alone could not see a stale 2.5 V (zero-current) reading at standstill");
  }
  // R2-F02/F03: VDC-channel bias — UCC12050 per channel behind its own NCV4276C from V15
  {
    const iIn = 50e-3 + 8e-3 / 0.5, pLdo = (15 - 5) * iIn, tj = 85 + pLdo * 40;
    judge("Sensing A.11", "VDC bias LDO (NCV4276C DPAK from V15, one per UCC12050)", `${f(pLdo, 2)} W → Tj ≈ ${f(tj, 0)} °C at 85 °C ambient (40 K/W on a 1 in² pad)`, "150 °C Tj max", tj / 150, 0.85,
      `UCC12050 idles at 50 mA (TI §6.9) + the AMC1311's ${f(8e-3 * 1e3, 0)} mA at ≈50 % efficiency; one shared LDO would carry ${f(2 * pLdo, 1)} W, and the V5GD LDO already runs from the 12 V node — hence one LDO per channel, which also keeps the channels independent (F4)`);
    judge("Sensing A.11", "VDC bias barrier working voltage (UCC12050 V_IOWM)", "1697 VDC / 1200 Vrms reinforced (VDE 0884-11)", "850 VDC link (same class as the AMC1311, 1.2 kVrms)", 850 / 1697, 0.9,
      "round 12 (R1-F12, R2-F02/F03): the A.4.4 bind \"MGJ2D150505SC\" does not exist and the MGJ2 family is reinforced to 150 Vrms only — every module family checked (MGJ1/2, NXE/NXJ, RECOM RxxP/R1SX, Mornsun QA, ADuM6028) fails the working-voltage criterion");
  }
  // R2-F13: a resolver wire shorted to KL30 (same vehicle connector) — pin injection through RSINF + RSINR
  {
    const rS = 10e3 + 120, iPin = [16, 24, 35].map((v) => (v - 5.7) / rS), iBuf = 2 * (16 - 2.5) / 10e3, iOld = (16 - 5.7) / 450;
    const cDiff = 47e-12 + 100e-12 + 22e-12, fc = 1 / (2 * Math.PI * 2 * rS * cDiff), ph = Math.atan(10e3 / fc) * 180 / Math.PI;
    judge("Sensing A.11", "Resolver wire shorted to KL30: SDADC pin injection (RSINF 10 k + RSINR 120)", `${f(iPin[0] * 1e3, 2)} / ${f(iPin[1] * 1e3, 2)} / ${f(iPin[2] * 1e3, 2)} mA at 16 / 24 / 35 V`,
      "3 mA per pin — the S32K39 operating AND absolute-maximum limit (no transient allowance)", iPin[2] / 3e-3, 0.98,
      `round 12: the GEN3 330 Ω + 120 Ω let ${f(iOld * 1e3, 0)} mA in. Both legs pull up through the winding, so the VMID buffer sinks ${f(iBuf * 1e3, 1)} mA through the two 10 k bias resistors (OPA348 ≈ 7 mA at 125 °C; was 20 mA through 680 Ω). Caps rescaled with the 30× source: 47 pF + 100 pF differential + 22 pF common-mode on both legs → corner ${f(fc / 1e3, 0)} kHz, −${f(ph, 0)}° at 10 kHz on both channels (a ±5 % cap mismatch is ${f(2 * (10e3 / fc) ** 2 * 5, 2)} % gain mismatch, ≈${f(2 * (10e3 / fc) ** 2 * 5 / 100 * 57.3 / 2, 2)}° electrical); source 20 k vs Z_DIFF ≥ 215 k is a ratio-cancelled ${f(20.24 / 215 * 100, 0)} % gain term`);
  }
  // R1-F14/R2-F10 (Opus check): the LV-entry coordination under ISO 16750-2 test B (35 V, 400 ms, Ri 0.5–4 Ω).
  // TVS I–V above breakdown: V ≈ V_BR + R_d·I with R_d ≈ (V_C − V_BR)/I_PP. Classic TPSMC24CA (V_BR 22.8–25.2 V, 33.2 V at
  // 45.8 A) vs the 24 V stand-off TPSMC24CA-VR now bound (V_BR 26.7–29.5 V, 38.9 V at 38.6 A). No SMC datasheet rates a
  // pulse beyond 1 ms; ≈110 W for 400 ms is an extrapolation of the 1.5 kW 10/1000 µs curve, quoted as such.
  {
    const tvs = (vbr, rd, ri) => { const i = Math.max(0, (35 - vbr) / (ri + rd)); return [i, i * (vbr + rd * i)]; };
    for (const [name, vbr, rd] of [["classic TPSMC24CA", 22.8, (33.2 - 25.2) / 45.8], ["TPSMC24CA-VR", 26.7, (38.9 - 29.5) / 38.6]]) {
      const [[i05, p05], [i2, p2], [i4, p4]] = [0.5, 2, 4].map((ri) => tvs(vbr, rd, ri));
      add("LV A.11", `Load dump test B (35 V, 400 ms) into the ${name} + MF-LSMF300/24X polyfuse`, `${f(i05, 1)} A / ${f(p05, 0)} W at Ri 0.5 Ω · ${f(i2, 1)} A / ${f(p2, 0)} W at 2 Ω · ${f(i4, 1)} A / ${f(p4, 0)} W at 4 Ω`,
        "≈ 110 W for 400 ms (1.5 kW class, extrapolated — no rating beyond 1 ms); polyfuse 3 A hold / 5.2 A trip, V_max 24 V", "WARN",
        name.startsWith("classic")
          ? "round 12, historical: the A.4 notes described this part — it conducts at the 24 V/60 s jump start (V_BR min 22.8 V) and at Ri ≤ 2 Ω takes ≥ 1.2× the extrapolated 400 ms capability; at 0.5 Ω the polyfuse trips inside the pulse (0.1–0.25 s hot), sees ≈ 30 V (V_max 24 V) and latches until a KL30 cycle — the archived sheet and the LCSC code were the -VR class, now bound explicitly"
          : "round 12: dark at 24 V (V_BR ≥ 26.7 V), clamps ≤ 33 V so the TPS55340 (34 V abs max) is inside its rating; at Ri = 4 Ω the pulse is 55 W (covered), at 2 Ω it is at the extrapolated limit, below 1 Ω the polyfuse trips and sees ≈ 30 V. The OEM's test-B source resistance is release gate ㉓: at ≤ 2 Ω either a TVS pulse test or 35 V-rated rails (a ≥ 42 V boost input in place of the TPS55340) decide. The polyfuse stays (the 33 V MF-LSMF260 holds 1.17 A hot against the 1.19 A worst chain)");
    }
  }
  // R1-F06/R2-F05: the IGBT SC rating is a test condition, not a corner guarantee
  add("Power stage — all SKUs", "IGBT short-circuit rating condition vs the design corner", "6 µs at 800 V / 15 V / 175 °C (hiitio DS)", "design corner 850 V / VCC2 up to 16.7 V", "WARN",
    "round 12 (R1-F06, R2-F05): the RR04 release gate (contained SC test) now also asks hiitio for the SC statement at 850 V and the 16.7 V gate-rail corner, or the gate rail's upper corner is tightened; the stale \"inside 6 µs\" BOM text was removed");
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
let md = `# Design Verification Report — rev ${REV} (${new Date().toISOString().slice(0, 10)})

End-to-end verification of the 220 kW / 800 V traction inverter at actual operating corners
(V_bus 500–850 V · KL30 9–16 V · 10 kHz · 65 °C coldplate), worst-case component tolerances.
Three independent layers:

1. **Geometric pin-verify** (sheets vs netlist, \`kicad5-verify.mjs\`) — the sheets ship only at 100 %
2. **Structural ERC audit** (netlist vs design intent, \`erc-audit.mjs\`) — 0 fail, incl. a lock-in per fixed finding
3. **Numeric verification** (this report, \`design-verify.mjs\`): **${counts.PASS} PASS · ${counts.WARN} WARN · ${counts.FAIL} FAIL** (+${counts.INFO} info)

A WARN is an item this analysis cannot close on paper — each names its bench or vendor gate.
Every SKU of the platform (8XX/4XX × SiC/IGBT — \`loss-model.mjs\`) is checked on the
same PCBs; losses and thermal use the shared model that \`sim-verify.mjs\` also runs.

## Findings log (F1–F36 rev A.3 campaign · F37–F46 rev A.4 · F47–F51 rev A.4.1 · F52–F57 rev A.4.2 · F58–F59 rev A.4.3 · F60–F62 rev A.5 docs audit · F63–F76 rev A.6 external review round 6 · F77–F89 rev A.7 review round 7 · F90–F97 rev A.8 review round 8 · F98–F105 its cross-check · F106–F113 rev A.9 review round 9 · F114–F119 its cross-check · F120–F122 rev A.10 schematic rechecks · F123–F134 rev A.11 system review — all fixed; review cross-reference in [\`review-A6-disposition.md\`](review-A6-disposition.md), [\`review-A7-disposition.md\`](review-A7-disposition.md) and [\`review-A8-disposition.md\`](review-A8-disposition.md))

| # | Severity | Finding | Fix |
|---|---|---|---|
| F90 | **HIGH** | A DESAT during latched ASC did not reach SPO: the faulted NSI6611 holds its own gate off (DS Fig. 8.11), but the healthy low sides stayed on through ASC, and a reset edge with the latch still set re-energised the faulted switch (R7-01/A7-N01) | UASCG: ASC_CMD = latch AND no-FLT on every path (FS1B-ASC included); FW-15 clears the ASC latch before the recovery; §4c transition table |
| F91 | MED | The FLT diode-OR reached the fault-latch preset at 111–135 ns/V (R7-02); buffering it exposed a 1N4148 low level at the Schmitt threshold (N12) | USCH2 74LVC3G17-Q100 on both latch presets; DFLT1/2 BAT46 |
| F92 | MED | Discharge TLP152 driven 470 Ω from the MCU pin: 6.4–6.8 mA vs 7.5 mA I_FLH max; the ASC opto proof used an assumed output drop (R7-03/A7-N03) | QDIS_CMD from USCH2 ch3, RQDL 270 Ω; both LEDs 9.6–14.2 mA from the guaranteed LVC V_OH point and V_F cold |
| F93 | MED | FW-16 boot test masked by RDY with gate power off; single-polarity (R7-04/A7-N02) | sensitized test with gate power up, before precharge, both polarities; coverage stated |
| F94 | MED | FAULT_OUT short to KL30 back-fed V5A through DSET/RENP2 (≈8 mA with V5A off) (A7-N04) | ZSET to ground, RENP2 removed: ≤ 0.6 mA into V5A |
| F95 | LOW | bom-gen wrote per-board CSVs before the semantic checks, so a failed run overwrote good outputs (A7-N05) | validate first, then publish atomically (temp + rename); mutation-tested |
| F96 | LOW | S4 called a 0.8 s junction pole conservative, and its case node delayed plate heating (R7-07, N14) | junction-case and case-plate static (an upper bound at every instant); +3 °C |
| F97 | LOW | Contract said the NSI6611 reset needs EN low ≥ t_FLT_MUTE; the DS (Fig. 8.8) has a mute time from the fault plus a ≥ t_RST_FIL pulse after it (N13) | §7 corrected; FW-15 unchanged |
| F98 | MED | Contract (cross-check R8X-01/02/03): FW-15 could leave the ASC latch set, so ASC returned at the reset edge with PWM held low (LS on with IN+ low — DESAT undocumented); FW-16 relied on sequence position, not measured V_DC/speed, for its HV-free step a; FW-16 d/e could not drop RDY while FS_GPIO1 held the flyback OR, and the FS_GPIO1 input was never tested | FW-15 always clears ASC (re-entry only through §4c); FW-16 measured preconditions, VCU precharge handshake, stored-pass arming; d/e drive FS_GPIO1 low, new g tests the OR; new h injects FLT (latch + mask covered in the field); FS1B_TDELAY = 0 required |
| F99 | MED | Dead-state gaps (R8X-06 + self-found): a Hi-Z USCH2 floated the fault-latch preset and the ASC mask — FW-16 passed with both gone; behind UASCG a Hi-Z ULAT floated the gate input (round 8 had moved the ASC pull-down off the latch) | RFCB 100 k on FLT_CMB_B (dead USCH2 = latched FLT: SPO, ASC masked, FW-16 b fails); RASCP moved to ASC_Q (dead ULAT = no ASC) |
| F100 | MED | QDIS_CMD sat next to V15 on the 4-way discharge header: a pin short put 15 V into USCH2 — which also carries both latch presets — and back into V5A (R8X-07) | header V15-GND-CMD-GND on both boards, locked by pin number |
| F101 | LOW | TLP152 LEDs 9.9 mA at the consistent cold corner, under the 10 mA recommended I_F; the model paired the 125 °C output resistance with the −40 °C V_F and omitted the far-end pulldown (R8X-04) | RASCL = RQDL = 261 Ω 1 %: 10.3–14.8 mA, judged against 10–15 mA |
| F102 | LOW | ZSET C5V6 "≤ 6.0 V" held only at 25 °C/5 mA: 6.62 V at a 35 V load dump on a KL30-shorted FAULT_OUT at 125 °C, over the USCH2 6.5 V abs max (R8X-05) | BZT52-B5V6 (±2 %): ≤ 6.34 V. C5V1 rejected — its soft knee through RFS2 would sag the released level toward V_T+ |
| F103 | LOW | FLT_LS_N had no glitch filter (CFLTF only on FLT_HS_N); a glitch now drops ASC and latches SPO with no automatic retry (R8X-12) | CFLTF2 100 pF |
| F104 | LOW | Lock-ins missed the FLT diode-OR, the FS1B back-feed by net and all of parts-db — a moved DFLT2 anode and an added FS1B pull-up both passed 854/0; two A.8 rows were fixed PASS (R8X-08) | ERC by net, pin number and first-match MPN per SKU; both rows computed; every new lock-in mutation-tested |
| F105 | LOW | S4 "true upper bound" held only for the assumed 60 s plate pole; VCC2 used 50 % FB-sense conduction on every corner; UASCG delay 4.4 ns (R8X-11/16) | static-plate bound reported (8XX IGBT 142 °C, WARN); per-corner conduction 1/0.5/0.2; 5.5 ns |
| F106 | MED | PSASC/PSQD are QA01C-18 (+18/−3 V), but F61 had bound the base QA01C sheet (+20/−4 V): the ASC timing assumed 18–24 V and the discharge gate ≈19.5 V (A8-N01) | QA01C-18 envelope 16.9–20.9 V modelled (Mornsun Fig. 1, line, tempco): ASC entry 7.07 → 7.52 µs, FW-06 end-point 905 → 906 V; MPN locked |
| F107 | MED | QDIS gate followed the bias rail through 47 Ω: up to 20.9 V against the +18 V recommended / +22 V abs, with the < 10 % load region extrapolated (A8-N02) | RQDG 1.5 k with RQDPD 10 k: V_GS 13.3–18.2 V with 1 % resistors (a divider, not a clamp — F122) |
| F108 | MED | FLT/RDY pulled up to V5A while the drivers' VCC1 is V5GD: 0.2 V over the FLT/RDY abs max (VCC1, no +0.3 V) at rail corners, and an undefined V5GD-loss state (A8-01; P-03, R8X-10) | pull-ups and the diode-OR pull-up on V5GD via harness pin 39 (DGND/HW_ID neighbours), RV5GP for an open pin: a lost V5GD is SPO, deterministically |
| F109 | MED | FW-16 allowed its ASC-actuating steps below 60 V: 0.62 J (8XX) / 1.54 J (4XX) at C_max into a latent high-side short (A8-N03) | eligibility < 12 V read (≤ 30/75 mJ), QDIS top-up from ≤ 60 V, n_ss from E_LL,pk |
| F110 | MED | Self-found (N17): the power board's LV side — V15 boost and four bias modules, V5GD, flyback controllers — ran on unswitched KL30, ≈150 mA while the vehicle sleeps | card-side P-FET switch (DMP6023LEQ) on V5A: ≤ 1 µA asleep |
| F111 | LOW | RFS4's 0.23 W case was only 16 V; 24/35 V reach 0.48/0.65 W and 0.23 W continues if FS1B is never released; BACKUP_SAFETY_PATH_FS1B = 1 repeated it through RSTB (A8-03) | RFS4 = ROHM ESR03EZPF1001 (0.27 W at 85 °C, ≈1.3 W/5 s); BACKUP_SAFETY_PATH_FS1B = 0 |
| F112 | LOW | The 10–15 mA LED window used Toshiba's typical V_F tempco as if guaranteed (A8-02) | ±28 % tempco band at every corner; the row judges the guaranteed I_FLH and abs-max margins first |
| F113 | LOW | "Battery opens in the SPO window = double fault" assumed independence (A8-G02) | §6 motor/vehicle release rule: energy-safe SPO, proven keep-connected, or the total-KL30-loss backup |
| F114 | MED | Cross-check R9X-01: a dead V5GD unpowers the AMC1311 LV sides, so the receivers sit at their +0.5 V offset — a valid-looking "0 V bus" (850 V could be reported as discharged); the N18 discriminator relied on it | V5GD read on PTB5 (RV5GP/RV5GS 2 × 47 k); V_DC invalid and SPO forced outside 4.75–5.25 V; also catches a back-powered, hovering V5GD (R9X-06) |
| F115 | MED | R9X-02: the drawn 2×20 harness numbers row by row, so VBAT faced ASC_CMD/QDIS_CMD/FLT_LS_N/RDY_HS and V5GD faced VDC1_P; "MICROFIT3-40" does not exist | HARNESS40 re-laid so VBAT and V5GD touch only ground or their own rail in both row-by-row and odd/even numbering (ERC checks both); class MPN until the layout picks the family |
| F116 | MED | R9X-03: ULDOEX (resolver exciter LDO) had INH on VBATC — ≈0.9 mA in FS26 LPOFF; the parking row summed the power board only | INH on V5A; the row now sums every unswitched load (≤ 43 µA at 25 °C) |
| F117 | LOW | R9X-04/10: QLVS turned on through its gate-drain charge (9–31 A into ≈45 µF at each wake), and a hot 2N7002's leakage × 100 k could half-close it while parked | 10 k/4.7 k gate network + 1 k/100 nF drain-gate: ≤ 1.1 A inrush, ≤ 0.5 V leakage drive |
| F118 | LOW | R9X-07/08: FW-16 trusted a 1 V low-voltage V_DC error (≈ ±9 V real); V5GD now follows V5A, so an FS26 restart erased a latched DESAT | read < 3 V or QDIS for 2 τ; FW-15 writes the DESAT to NVM and §9 gates arming on it |
| F119 | LOW | R9X-05/09/11/12/14: QLVS/QLVN G/S label swaps and a V5A–V5GD bridge passed the ERC; release rule (c) claimed the backup could cover the V5GD row; 25 V caps on the 20.9 V bias rails; RFS4 with FS1B held through a jump start unlisted; stale texts | pin-number and no-bridge locks; (c) limited to total KL30 loss; CASC/CQD 50 V; RFS4 held-case row (WARN, accepted); texts fixed |
| F120 | MED | Schematic recheck A9-S01: RDY_HS/RDY_LS (open-drain, 5.1 k to V5GD) drove 74LVC1G11-Q100 AND inputs directly at ≈20–100 ns/V against the 10 ns/V recommended limit; the round-7/8 "state-benign" rejection left the shutdown chain outside the datasheet | USCH3 74LVC3G17-Q100 on both RDY lines (RRDB dead-state pull-down); ERC by pin number |
| F121 | LOW | S9-01: RASCG (2.2 k) carries up to 0.12 W for as long as ASC is held, but was a generic 0603 (0.1 W at 70 °C → 82 mW at 85 °C) | ROHM ESR03EZPF2201 (0.33 W at 70 °C → 0.27 W at 85 °C); 2.2 k kept for the ASC timing |
| F122 | LOW | S9-02 and notes: discharge sheet heading "67.5k bleeder (58 s)"; "V_GS 13.4–18.1 V" read as a hard ceiling; the A9 disposition still quoted the interim pin-39 V5GD pin | 66 k (56 s nom / 65 s worst); 13.3–18.2 V with 1 % resistors ("a divider, not a clamp"); the final harness map quoted |
| F123 | **HIGH** | §6 release rule (a) accepted SPO on back-EMF alone and omitted the stored winding energy the diodes rectify into an isolated link: 1.5·L·I² = 61 J (8XX) / 84 J (4XX) with the 0.35 mH screening motor against 40 J of link headroom (R1-F01, R2-F08) | rule (a) now requires ½·C_min·(U_N² − V_max²) ≥ W_mag + W_emf from the motor's dq model; screening row (WARN): 8XX covers that motor only to 277 A rms, above it rule (b) |
| F124 | **HIGH** | Resolver exciter drawn with the MFB feedback pair swapped (4.7 nF output→summing node, 24 k output→inverting input): first-order 2 kHz roll-off, \|H(10 kHz)\| 0.18, so the S32K39 SWG (≤ 2.30 V pp) would have driven ≈ 0.7 V pp instead of 8 V pp (R2-F04) | textbook MFB 10k/24k/10k with 1.5 nF to ground and 220 pF feedback: f0 17.9 kHz, Q 0.70, \|H\| 1.85 → 7.7 V pp; SWG amplitude is the firmware knob; ERC by topology and value |
| F125 | **HIGH** | VDC-channel bias "MGJ2D150505SC" is not an existing order code, and the MGJ2 family is reinforced only to 150 Vrms (R1-F12, R2-F02/F03) | TI UCC12050 per channel (V_IOWM 1200 Vrms / 1697 VDC, VDE 0884-11 reinforced, the AMC1311's own class) behind its own NCV4276C from V15; 10 µF X7R both sides; ERC by pin |
| F126 | MED | The Hall open-wire pull-down claimed in README/design-basis since A.4 was never drawn (R1-F03) | R⟨ph⟩B0 100 k at each card input: open wire or dead sensor reads 0 V, outside the 0.2–4.8 V validity window now in FW-05 |
| F127 | MED | LEM HC5FW drawn as a 3-pin VCC/OUT/GND symbol; the device has 1 V_ref, 2 V_out, 3 Gnd, 4 U_C and E1–E4 mass pins and no connector (R1-F04) | 8-terminal symbol by DS number (V_ref open, E1–E4 to Gnd); the off-board carrier drawing derives from it; ERC by terminal number |
| F128 | MED | A resolver wire shorted to KL30 (shares the vehicle connector) injected ≈ 23 mA into an SDADC pin through 330 + 120 Ω against the S32K39's 3 mA limit (operating and absolute maximum), and both legs pulled 20 mA into the VMID buffer through the 680 Ω bias (R2-F13, R1-F20) | RSINF/RCOSF and RSIN/RCOS → 10 k: 1.0 / 1.8 / 2.9 mA at 16 / 24 / 35 V, buffer 2.7 mA; caps rescaled (47 p + 100 p differential, 22 p common-mode on both legs — the P-only caps converted CM to DM): corner 47 kHz, −12° on both channels; SWG given its 47 pF load |
| F129 | LOW | VREF5 sat at 3.3 µF nominal, the FS26 upper limit, before tolerance (R2-F11) | CSB5 2.2 µF → 1 µF (0805 X7R 16 V): 2.1 µF nominal, 1.4–2.3 µF effective over tolerance, bias, temperature and aging |
| F130 | **HIGH** | LV-entry coordination (R1-F14, R2-F10, found by the Opus check): the notes described the classic TPSMC24CA (V_BR 22.8–25.2 V), which conducts at the 24 V/60 s jump start and, in an ISO 16750-2 test B at Ri ≤ 2 Ω, takes more than the (extrapolated) 400 ms capability; at 0.5 Ω the polyfuse then trips inside the pulse, sees ≈ 30 V against its 24 V V_max and latches until a KL30 cycle; the archived datasheet was the -VR series | TPSMC24CA-VR bound explicitly (AEC-Q101, 24 V stand-off, V_BR 26.7–29.5 V, same pad and price): dark at 24 V, clamps ≤ 33 V under the boost's 34 V abs max; test B covered on paper at Ri ≥ 4 Ω, the OEM's Ri is gate ㉓ (≤ 2 Ω: TVS pulse test or 35 V-rated rails). Polyfuse kept (the 33 V part holds 1.17 A hot against the 1.19 A worst chain) |
| F131 | LOW | UEXD (ALM2402QPWPRQ1) bought as "HTSSOP16"; the PWP package is 14-pin (R1-F05, R2-F26) | footprint HTSSOP14-PWP; ERC asserts the 14-pin package against the symbol |
| F132 | LOW | Board NTCs drawn as 2-pin headers, bought as 0603 NTCs (R2-F27) | on-board RTAMB/RTHS 0603 (symbol = BOM) |
| F133 | LOW | kicad5-verify exited 0 on an empty page set (R2-F29) | fails unless 4 sheets and ≥ 1500 pins were compared |
| F134 | LOW | Contract and document defects: FW-05 threshold in A rms; §6 merged "contactor open" with "BMS limit 0"; FW-15 "NVM first" ahead of the safe action; FW-02 τ bands overlap; HW_ID "pin 40"; "LQFP-176"; stale "inside 6 µs" IGBT text; ALT field offered M7 rectifiers and a 3-lead TO-247; CAN termination fixed; FW-18 silent on invalid witnesses; S6 double-update unstated (R1-F02/F11/F18/F23/F24/F25/F27, R2-F18/F19/F24/F33/F34/F35) | all corrected: instantaneous ±601/±707 A; rows split; retained-RAM latch, queued NVM; plausibility wording + EOL measurement; pin 2; 289-MAPBGA; RR04 wording; ALT = qualify-before-use; endpoint DNP option; "unknown, never safe"; delay stated |
| F77 | **HIGH** | The A.6 RC timing nodes drove non-Schmitt LVC inputs: the clear one-shot into ULAT2 /CLR at ≈63,500 ns/V (5 ns/V allowed), the soft-off delay into UAND2 at ≈14,000 ns/V (10 ns/V) (RR01/RR02, A6-R02/R03) | 74LVC3G17-Q100 Schmitt buffer (no Δt/ΔV limit) on both nodes and on FS0B; one-shot 61–230 µs, delay 22–53 µs at its thresholds |
| F78 | **HIGH** | FS1B loaded 5.45 mA through 1 k pull-ups: V_OL ≤ 0.4 V holds only to 2 mA and the limit can be 4 mA — FS1B 1.33 V, ASC_SET_N 1.67 V (> VIL), SBC read-back (< 0.7 V) fails; the checker compared with 22 mA and divided by 1000 twice (A6-R01) | RENP1/2 5.1 k (NXP value): 1.79 mA incl. strap and a specified FAULT_OUT load, ASC_SET_N ≤ 0.84 V; checker at the V_OL point |
| F79 | **HIGH** | ASC entry had no break-before-make: FS0B/FS1B assert together on the MCU-dead path (HS turn-off raced the LS ASC), and the MCU path had no ordered entry (RR05) | CASCD 12 nF + DASCR: LS ASC ≥ 3.4 µs after the latch, entry ≤ 7.0 µs, release ≤ 0.75 µs; MCU path = eFlexPWM fault (high sides off) → ASC_REQ → PWM-ASC with EN high after the dead time (§4c). A first draft also dropped DRV_EN from the latch (DASC) — removed: with EN low the NSI6611 does not give DESAT priority over ASC (DS §8.12, cross-check) |
| F80 | **HIGH** | Flyback FB divider on VDD: the 2.2 k start feed could hold FB above 2.5 V with the converter stopped (12.3 V at a 16 V rail for a 1.5 mA controller; 18 V clamp at a 24 V jump start) → no restart, gate power lost (A6-R07); the VCC2 model omitted the aux diode — the real rail was 16.9 V, not 15.6 V (A6-R06) | FB senses its own aux rectifier (1N4148WS + 100 Ω + 100 nF), 52.3k/15k: VCC2 15.4 V nom, 14.0–16.7 V corners in a 13.5–17.0 V window |
| F81 | MED | CB15O1/2 22 µF/25 V on V15B, which follows V12L − Vf to ≈33 V in pass-through (RR10) | 22 µF/50 V 1210 (same part as CLVC2); boost PM 72–84° over 20–35 µF effective |
| F82 | MED | TLP152 ASC opto LED at 5.4–7.0 mA through 470 Ω — below its 7.5 mA guaranteed turn-on current (self-found, N10) | RASCL 270 Ω: 10.6–13.5 mA |
| F83 | MED | The one-shot comment claimed runaway code could not hold the chain permissive; repeated clear pulses do (RR03) | claim corrected; FW-15 locked eFlexPWM fault inputs (PWM forced low while FLT) + FW-12 WD_ERR_LIMIT 2 → FS0B |
| F84 | MED | DESAT timing row judged PASS at the 400 mA soft-off only; the 100 mA DS minimum needs 8.9–10.1 µs vs tP ≤ 6 µs (RR04/A6-R04) | both corners shown, WARN = release gate (I_STO distribution, SC envelope at 850 V and actual gate bias, contained SC test) |
| F85 | LOW | Thermal: S4/steady-state put only the switch die into the coldplate term; the diode heats the same plate (RR07) | tjPos: plate carries IGBT + diode; 8XX/4XX IGBT 30 s peak 129/123 °C |
| F86 | LOW | 8 kHz SiC mode allowed a 1.2 kHz current-loop crossover (43.3° PM) (RR08) | ≤ 1.1 kHz at 8 kHz (47.2°) in the SKU table |
| F87 | LOW | bom-gen exited 0 with missing inputs; JSWD 10-pin source vs a 20-pin BOM part (A6-R09/R10) | preflight aborts before any write (missing/empty/malformed/stale); Samtec FTSH-105-01-L-DV-K + contact-count check |
| F88 | LOW | Flyback peak-current check used one transformer's Lp for the bank current; README kept pre-A.6 loss numbers (A6-R14, README note) | bank Lp = 10 µH/3 (2.05 A, 68 %); README sizing regenerated from the loss model |
| F89 | LOW | ULAT/ULAT2 were TI SN74LVC1G74DCUR, a catalog part with no AEC-Q100 variant (self-found, N11) | Nexperia 74LVC1G74DC-Q100, pin-identical |
| F63 | **HIGH** | S8 turn-off overshoot used \`Ln·20e12·1e-6\` for 20 kA/µs — 1000× too small (0.3 V instead of 300 V at 15 nH), so its PASS was void; with the DS fall time (13 ns cold at 3.3 Ω ≈ 30 kA/µs at 481 A) no EconoDUAL-class loop holds 1080 V at 850 V (review R-F01) | SI units; overshoot budget from the DS tf; RG_OFF start value 6.8 Ω (Eoff booked in the loss model), RG_ON 3.3 Ω (the only characterized point, was 1.5/1.0); DPT gate at 850 V cold/hot; module Ls requested from hiitio |
| F64 | **HIGH** | SiC conduction loss used the IGBT transistor-only formula \`I·√(1/8+m·cosφ/3π)\`: synchronous SiC conducts ½·I²·R per switch — understated 2.4× (136 → 330 W/switch at 340 A) (R-F02) | exact ½·I²R + switching at the fitted Rg + Qrr + dead-time diode; thermal and efficiency restated (peak 30 s Tj 122 °C at 850 V, not 89 °C; 99.0 % semiconductor efficiency at the continuous point) |
| F65 | **HIGH** | IGBT short-circuit rating carried as "10 µs class"; HCG600 DS Table 5 says tP ≤ 6 µs at 800 V/175 °C/15 V; 150 pF blanking = 4.5 µs worst detection alone (R-F03) | IGBT blanking 82 pF C0G: 2.98 µs worst detection + ~1.5 µs soft-off < 5 µs derated; contained SC test is the release gate (the DS-minimum 100 mA soft-off current is not coverable) |
| F66 | **HIGH** | Gate-power flyback could not start at KL30 9 V: 4.7 k needed 8.47 V at the 12 V node (divider current omitted) AND the UCC28C40's 0.4 V UVLO hysteresis gave ~0.15 ms bursts on 4.7 µF (R-F17/F18, cycle-by-cycle S1) | 2.2 k 1206 start + 47 µF VDD (one-burst start in every corner) + 18 V VDD zener (the C40 has no internal clamp — the lower start resistor would lift VDD past 18 V at jump start with the flyback disabled) |
| F67 | MED | "220 kW / 120 kW over 500–850 V" is not deliverable at 340/185 A: full power needs ≥654/656 V (PF 0.85, 5 % modulation reserve) (R-F09) | published P(V_dc) envelope per SKU; firmware derates by V_dc |
| F68 | MED | Passive bleeder 5 × 27 k: the low-tolerance part carries 184 V at 850 V = 92 % of a plain 2512's 200 V working rating (R-F24 at 850 V) | 2 × 6 × 22 k = 66 k: 154 V (77 %), 56 s / 65 s to 60 V |
| F69 | MED | Global DRV_EN drop (≈0.5–0.9 µs after DESAT) could interrupt the faulted driver's soft turn-off — the NSI6611 DS does not state RST/EN priority during soft-off (R-F05) | 10 k/3.3 nF between the latch and the AND's Schmitt input: 12–40 µs; FS0B/MCU paths undelayed |
| F70 | MED | Fault-latch clear was level-sensitive: a stuck-low MCU pin held PRE=CLR=L (both outputs high) and silently disabled the global latch (residual of R-F06; the proposed "fault-dominant" fix would deadlock the NSI6611 FLT reset) | clear is a hardware one-shot (15 nF into the 10 k pull-up + BAT46 clamp): ≥54 µs per falling edge, re-arms by itself |
| F71 | MED | IGBT build thermal/efficiency omitted the FWD die, used m·cosφ = 0 and DS energies at 0.51 Ω; Qg scaled linearly (R-F26/F27) | separate IGBT/diode dies with m·cosφ (motoring + regen), energies referred to our driver, full Qg; 8XX IGBT rated at 5 kHz (127 °C end of 30 s, not 110 °C) |
| F72 | MED | Both V_DC receivers share the +0.5 V offset buffer UVOF: its failure shifts both channels by up to 228 V and passes the 5 % cross-check — OV and discharge witness blinded (R-F11) | VOFS routed to an MCU ADC (zero parts); BMS pack voltage is the third witness; the "fully independent" wording corrected |
| F73 | MED | BOM class MPNs had drifted from the netlist: RFS1–4 printed R0603-120R (re-creating F40), CLVC2 4.7 µF (re-creating F52), 10 more lines; the IGBT variant BOM printed the SiC value next to the IGBT MPN | parts-db fixed; SKU rows carry their value; bom-gen FAILS on any value/MPN disagreement |
| F74 | LOW | Simulation defects: S5 counted the bleeder twice; S10 hold-up put the VEE cap in parallel with VCC2 and ignored the bleeder/gate loads (15 ms claimed, 1.1–3.2 ms real); S4 started cold; S6 applied the 10 kHz bandwidth to the 4–6 kHz IGBT; S1 had no startup model (R-F14/F17/F25/F28/F32) | all rewritten on the shared loss model; ASC through total LV loss not credited (unchanged conclusion, corrected number) |
| F75 | LOW | Resolver cable shields terminated into AGND at the vehicle connector (R-F35) | shields on the connector ground (DGND); AGND keeps its single-point tie |
| F76 | LOW | Documentation overstated: HVIL "hardware window comparator" (it is an MCU ADC signature), RSS labelled worst case (±0.7 % → ±2.1 % worst), 0.62 ripple factor (worst 0.65), XM3 inductance reused for a D3 module, bias-bank "3.87 W" (100 % CS limit before losses; 2.3 W worst parts), and two "drop-in" module alternates that are not (HCS800FH120D4B3 has a lettered press-fit pin map; FF6MR12W2M1H is not an EconoDUAL-3 package code) (R-F12/F21/F29/F37) | wording and numbers corrected; IGBT SKUs fit RT 8.2 k (~308 kHz) for gate-power margin; alternates list limited to pin-map-verified parts |
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
| F52 | **HIGH** | FS26 VCORE buck network out of spec: LCOR 4.7 µH (Table 106 allows 1/1.5/2.2 µH by OTP), COUT 10 µF nominal vs 20–100 µF effective, bootstrap 100 nF vs 47 nF; VPRE COUT/input caps under the effective minimums | LCOR 2.2 µH (CORE_LSEL_OTP=0x02) · V15S 2×22 µF (≈32 µF eff) · CBTC 47 nF · CBTP 22 nF (typ; Rev 6.1 Table 100 allows 22–100 nF, so the prior 100 nF was legal — narrative corrected) · CSB1/2 22 µF · CLVC2 22 µF; effective-capacitance rows added |
| F53 | **HIGH** | LCAN1/LCAN2 mapped as windings 1-2/3-4 — ACT45B is physically wound 1-4 and 2-3, so transceiver CANH landed on the external CANL net (both ports) | Pin map corrected to the TDK circuit diagram; ERC asserts end-to-end pairing |
| F54 | MED | ULDO15 (ADJ + ceramic COUT) drawn without the required feed-forward capacitor; COUT 4.7 µF below the reference design | Cb 220 pF across the 49.9 k leg (f_z 14.5 kHz, in the 11–18 kHz window) · COUT 22 µF |
| F55 | **HIGH** | UEXD (ALM2402: 18 V abs, 16 V rec) fed from raw VBATC — 24 V jump start exceeds abs max and TPSMC24CA clamps far above 18 V | ULDOEX 12.1 V protective LDO (same NCV4276C-ADJ family) feeds VCC/VCC_O1/VCC_O2; crank behavior unchanged (dropout) |
| F56 | **HIGH** | Flyback switch BUK9Y14-80E is logic-level: V_GS abs ±10 V DC vs the 11.8 V VDD drive; the 5.6 V gate zener "fixed" it by conducting ~0.29 A through every ON interval (~0.4 W each, doubling the aux budget) | BUK7Y14-80E (standard-level, ±20 V, same LFPAK56/current class) + zener repurposed to a dark 15 V protective clamp |
| F57 | LOW | LDO ordering code transposed (NCV4276CADJDTRKG) | NCV4276CDTADJRKG per the DS ordering table |
| F62 | MED | The on-sheet ASIL-D + DISCHARGE review panel had silently vanished from the power sheet (its single void pick stopped fitting as the sheet grew through A.4.x) — found by the A.5 docs audit | Panel placement retries every void largest-first, and the build now FAILS if the panel cannot be placed; panel text refreshed (DRV_EN chain incl. RDY+fault-latch, 57 s bleed, "never energize w/o the DISCHARGE BOARD") |
| F60 | MED | JVEH bound to TE 776231-1 called "AMPSEAL 23" — the TE drawing in docs/datasheets shows 776231-1 is the **35-position** header (mates plug 776164) | Rebound to **770669-1**, the real 23-position AMPSEAL PCB header (mates the 770680-1 plug already cited); drawing TE-770669-1.pdf fetched |
| F61 | LOW | Rails named V18A/V18Q assume "+18 V": the QA01C DS selection row is **+20/−4 V**. ASC path unaffected (2.2 k + 5.1 V clamp); QDIS gate ≈19.5 V vs +22 abs / +18 rec | Documented + WARN row; clamps verified for +20 V; gate-divider option noted for proto |
| F58 | LOW | LCOR reconciliation: the netlist value became 2.2 µH in A.4.2 but the parts-db class MPN still printed "IND-4.7uH-2A" on the sheet/BOM — an assembler would fit the old value | MPN string now IND-2.2uH-4A with the OTP pairing in the description; value, MPN, BOM and OTP agree |
| F59 | MED | UB15 (TPS55340) COMP node had only 10 nF to ground — no compensation zero; simplified CCM screening gives ~0° phase margin | Series R3/C4 = 2 kΩ/100 nF per TI §8.2.1.2.11 + 470 pF HF pole cap; RHPZ ≈ 450 kHz (not limiting); measured Bode remains a bench gate |
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
## Open vendor/bench inputs (every WARN above names one)

hiitio: module stray inductance Ls (SiC D3), SiC short-circuit envelope at 850 V ·
NOVOSENSE: RST/EN behaviour during DESAT soft turn-off and the I_STO distribution · TDK:
VGT12EEM saturation current and working-insulation rating · Faratronic: the 4XX 50 µF/600 V
can (ripple/ESR/life) · Murata: MGJ2 reinforced certificate · coldplate Rth (thermal test) ·
motor data (flux linkage, n_max, Ld/Lq) for the safe-state decision. Datasheet values used are
in \`docs/datasheets/EXTRACTED-PARAMS.md\`.

## Method

Closed-form worst-case analysis (tolerance corners: R ±5 %, precision ±1 %, C +10 %,
KL30 9–16 V, V_bus to 850 V / 500 V) — the correct tool at schematic phase; the time-domain
companion is \`sim-verify.mjs\`. Review A.6 added an independent cross-check: every
contested number (losses, overshoot, DESAT timing, startup, hold-up, discharge, ripple,
sensing) was recomputed in a separate script by a second reviewer; the two agree within
model assumptions. SPICE adds nothing without vendor switch models; the double-pulse,
short-circuit, thermal and EMC items are bench gates listed in \`docs/firmware-contract.md\`
and \`docs/review-A6-disposition.md\`.
`;
writeFileSync(join(ROOT, "docs", "verification-report.md"), md);
console.log(`design-verify: ${counts.PASS} PASS · ${counts.WARN} WARN · ${counts.FAIL} FAIL · ${counts.INFO} info → docs/verification-report.md`);
process.exit(counts.FAIL ? 1 : 0);
