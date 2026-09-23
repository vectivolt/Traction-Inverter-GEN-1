// loss-model.mjs — ONE source for the silicon constants, the platform SKU table and the
// semiconductor loss model, shared by design-verify.mjs (steady-state bounds) and
// sim-verify.mjs (transients). Review A.6: the two scripts had drifted apart once already.
export const OP = { coolant: 65, rthPlate: 0.045, pf: 0.85, mres: 0.95 };
export const MOD = {
  // HCS600FH120D3C1 (rev X.0.1): RDS(on) 5.2 mΩ chip @+15 V/175 °C + 0.5 mΩ terminals; Eon/Eoff
  // 17.85/23.12 mJ and Err 1.99 mJ @600 V/600 A/150 °C, Rg 3.3/3.3 Ω (= the drawn gate network);
  // tf 13/22 ns (25/150 °C) @600 A; VSD 5.6 V @600 A −5 V; Rth(j-c) 0.066 + 0.015 grease
  // Drawn gate network RG_ON 3.3 Ω (the DS point) / RG_OFF 6.8 Ω: DS Fig.14 Eoff(6.8 Ω, 150 °C) ≈ 38.5 mJ.
  // The DS tf (13/22 ns @3.3 Ω) scaled by the total turn-off resistance (6.8+0.3+1.1)/(3.3+0.3+1.1).
  sic: { rdsHot: 5.7e-3, eswPerA600: (17.85e-3 + 38.5e-3) / 600, errPerA600: 1.99e-3 / 600, vsd: 4.5, tdead: 1e-6,
    tfHot: 22e-9 * 8.2 / 4.7, tfCold: 13e-9 * 8.2 / 4.7, rthJC: 0.066, rthCH: 0.015, tjMax: 175, vds: 1200, src: "HCS600 datasheet" },
  // HCG600FH120D3E1EA: VCEsat 1.82 V / VF 1.70 V @600 A hot; Eon(Rg) from DS Fig.5 at the drawn
  // 1.0 Ω ≈ 60 mJ, Eoff ≈ 104 mJ, Erec ≈ 40 mJ (Fig.11) @600 V/600 A/175 °C; SC tP ≤ 6 µs @800 V/
  // 175 °C/15 V (Table 5); Rth 0.07 IGBT / 0.10 diode; Qg 4.36 µC (−15→+15 V); Tvjop 150 °C
  // Energies read at the EQUIVALENT external Rg: our 1.0 Ω plus the NSI6611's ≈1.4 Ω source path
  // (15 V/11 A) minus a ≈0.5 Ω vendor test driver ⇒ ≈1.9 Ω: Eon ≈ 100 mJ (Fig.5), Erec ≈ 32 mJ (Fig.11).
  igbt: { v0: 0.9, r: (1.82 - 0.9) / 600, v0d: 0.9, rd: (1.70 - 0.9) / 600, eonA: 100e-3 / 600, eoffA: 104e-3 / 600, erecA: 32e-3 / 600,
    rthJC: 0.07, rthJCd: 0.10, rthCHd: 0.015, tvjop: 150, tsc: 6e-6, qg: 4.36e-6, tdead: 2.5e-6, src: "HCG600 datasheet" },
};
// ---------------- platform SKUs (review A.6): same PCBs, per-SKU module / cans / discharge / limits ----
// 4XX current limits are the platform's thermal/sensor ceiling (±900 A LEM, 600 A module), not a
// 220 kW-at-400 V promise — that needs a ~560 A rms frame (see docs/variants.md).
export const SKU = {
  sic8:  { name: "8XX SiC",  sil: "sic",  vMin: 500, vNom: 700, vMax: 850, ovTrip: 880, iPk: 340, iCont: 185, fsw: 10e3,
    can: { c: 20e-6, vr85: 1000, irms: 15.4 }, ra: 4 * 470, rp: 66e3 / 1, rpPer: 22e3, rpN: 6, pTarget: [220, 120] },
  igbt8: { name: "8XX IGBT", sil: "igbt", vMin: 500, vNom: 700, vMax: 850, ovTrip: 880, iPk: 340, iCont: 185, fsw: 5e3,
    can: { c: 20e-6, vr85: 1000, irms: 15.4 }, ra: 4 * 470, rp: 66e3, rpPer: 22e3, rpN: 6, pTarget: [220, 120] },
  igbt4: { name: "4XX IGBT", sil: "igbt", vMin: 250, vNom: 400, vMax: 500, ovTrip: 530, iPk: 400, iCont: 250, fsw: 5e3,
    can: { c: 50e-6, vr85: 600, irms: 18 }, ra: 4 * 220, rp: 45e3, rpPer: 15e3, rpN: 6, pTarget: [150, 90] },
  sic4:  { name: "4XX SiC",  sil: "sic",  vMin: 250, vNom: 400, vMax: 500, ovTrip: 530, iPk: 400, iCont: 250, fsw: 10e3,
    can: { c: 50e-6, vr85: 600, irms: 18 }, ra: 4 * 220, rp: 45e3, rpPer: 15e3, rpN: 6, pTarget: [150, 90] },
};

// ---------------- loss models (review A.6 F02/F26 — replaces the IGBT-only formula) ----------------
// SiC synchronous bridge: each switch carries the phase current for its duty share in BOTH
// directions (reverse channel conduction), so ∫d·i² = ½·I² exactly — P_cond = ½·I_rms²·R per
// switch, 3·I²·R per bridge. The old I_rms·√(1/8 + m·cosφ/3π) is the IGBT transistor-only
// formula and under-stated SiC conduction 2.4×. Switching: DS energies at the drawn 3.3/6.8 Ω,
// linear in current (averaged over the sinusoid: Î/π) and voltage. Plus body-diode Qrr loss
// and dead-time body-diode conduction (2 events/period/leg, V_SD at −5 V gate).
export function sicLoss(Irms, V, fsw) {
  const Ipk = Irms * Math.SQRT2, m = MOD.sic;
  const cond = 0.5 * Irms * Irms * m.rdsHot;
  const sw = m.eswPerA600 * (Ipk / Math.PI) * (V / 600) * fsw;
  const rr = m.errPerA600 * (Ipk / Math.PI) * (V / 600) * fsw;
  const dt = 2 * m.tdead * fsw * m.vsd * Ipk / Math.PI;
  return { cond, sw, rr, dt, sw_die: cond + sw + rr + dt, d_die: 0 };
}
// IGBT: transistor and diode are separate dies (separate Rth). Standard sinusoidal-PWM
// averages with m·cosφ (motoring > 0, regeneration < 0), linearized VCE/VF, DS energies at the
// drawn 1.0 Ω gate resistor referred to our driver (Eon ≈ 100 mJ vs 39.7 mJ at the DS 0.51 Ω test).
export function igbtLoss(Irms, V, fsw, mcos, g = MOD.igbt) {
  const Ipk = Irms * Math.SQRT2, k = (Ipk / Math.PI) * (V / 600) * fsw;
  const condT = g.v0 * Ipk * (1 / (2 * Math.PI) + mcos / 8) + g.r * Ipk * Ipk * (1 / 8 + mcos / (3 * Math.PI));
  const condD = g.v0d * Ipk * (1 / (2 * Math.PI) - mcos / 8) + g.rd * Ipk * Ipk * (1 / 8 - mcos / (3 * Math.PI));
  const swT = (g.eonA + g.eoffA) * k, rec = g.erecA * k;
  return { condT, swT, condD, rec, sw_die: condT + swT, d_die: condD + rec };
}
export const lossOf = (s, Irms, V, mcos = 0.85) => s.sil === "sic" ? sicLoss(Irms, V, s.fsw) : igbtLoss(Irms, V, s.fsw, mcos, MOD[s.sil]);
export const rthT = (s) => (s.sil === "sic" ? MOD.sic.rthJC : MOD[s.sil].rthJC) + MOD.sic.rthCH + OP.rthPlate;
export const rthD = (s) => MOD[s.sil].rthJCd + MOD[s.sil].rthCHd + OP.rthPlate;
// Junction temperatures of ONE switch position (round 7, RR07): the IGBT and its diode sit on
// the same coldplate footprint, so the plate term carries BOTH losses; each die keeps its own
// j-c and c-h (datasheet Rth are per die). SiC: d_die = 0, so this equals the old per-die sum.
export const tjPos = (s, L) => {
  const g = s.sil === "sic" ? MOD.sic : MOD[s.sil], plate = OP.coolant + (L.sw_die + L.d_die) * OP.rthPlate;
  return { T: plate + L.sw_die * (g.rthJC + MOD.sic.rthCH), D: plate + L.d_die * ((g.rthJCd ?? 0) + (g.rthCHd ?? 0)), plate };
};
export const tjLimit = (s) => s.sil === "sic" ? MOD.sic.tjMax : MOD[s.sil].tvjop;
// current-limited AC power, linear SVPWM with modulation reserve
export const pAvail = (V, I) => Math.sqrt(1.5) * OP.mres * V * I * OP.pf / 1e3;

