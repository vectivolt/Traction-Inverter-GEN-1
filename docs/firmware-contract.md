# Hardware → firmware contract (rev A.6)

The hardware protects what software cannot react to in time; firmware owns every operating
limit. This file is the contract between the two for **every SKU of the platform**. Each
review A.6 finding dispositioned as *Firmware Handled* points at a numbered requirement here
(`FW-xx`). The traction application itself is a separate deliverable (review R-F08); nothing in
this repository claims it exists. Verification of each requirement is HIL first, then bench.

## 1. Division of labour

| Fault class | Fast enough only in hardware | Owned by firmware |
|---|---|---|
| Short circuit / shoot-through | NSI6611 DESAT + soft turn-off per switch; global fault latch → DRV_EN (delayed 12–40 µs so it cannot cut a soft turn-off short) | diagnosis, retry limits, FLT_HS vs FLT_LS → safe-state choice (§6) |
| Watchdog / MCU hang / reset | FS26 Q&A watchdog → FS0B → DRV_EN low (three-phase open); FS1B → LS ASC latch; every harness enable/PWM line default-OFF | FS26 configuration and the FS1B policy (FW-12) |
| Gate-power loss | NSI6611 VCC2 UVLO parks the gate low; RDY → DRV_EN | — |
| Phase overcurrent (operating) | — (DESAT sits at ≈2.8× the rated peak) | ADC hardware compare → PWM fault input, ≤ 2 PWM periods (FW-05) |
| DC-link overvoltage (regen / contactor open) | — | ADC hardware compare on both V_DC channels → ASC request, ≤ 20 µs (FW-06, rev A.6 N9) |
| Everything else in §5 | — | firmware |

## 2. SKU identity and parameter sets

- **FW-01** Read `HW_ID` (harness pin 40; card 10 k pull-up to VREF5 against the power board's
  `RHWID`) before arming. Windows at VREF5 = 5.0 V (1 % parts, ±4 % window):

  | SKU | RHWID | V_ID nominal |
  |---|---|---|
  | 4XX IGBT | 2.2 k | 0.90 V |
  | 8XX IGBT | 4.7 k | 1.60 V |
  | 8XX SiC | 10 k | 2.50 V |
  | 4XX SiC | 22 k | 3.44 V |
  | open / short | — | > 4.6 V / < 0.2 V → **fault, no DRV_EN** |

- **FW-02** The loaded parameter set carries its SKU; a mismatch with `HW_ID` refuses
  `MCU_GATE_EN` (an 8XX OV trip on a 600 V 4XX cap bank destroys it). Cross-check at every
  key-off: the active-discharge time constant τ = R·C gives the fitted link capacitance
  (8XX ≈ 0.60 s with 1.88 kΩ, 4XX ≈ 0.71 s with 0.88 kΩ); > 20 % off ⇒ DTC.

| Parameter set | 8XX SiC | 8XX IGBT | 4XX IGBT | 4XX SiC |
|---|---|---|---|---|
| V_DC normal range | 500–850 V | 500–850 V | 250–500 V | 250–500 V |
| OV trip (both channels) | 880 V | 880 V | 530 V | 530 V |
| Phase current pk / cont (A rms) | 340 / 185 | 340 / 185 | 400 / 250 | 400 / 250 |
| f_sw | 8–10 kHz | 5 kHz | 5 kHz | 8–10 kHz |
| Dead time (commissioning) | 1.0 µs | 2.5 µs | 2.5 µs | 1.0 µs |
| Current-loop crossover ceiling (S6) | ≤ 1.2 kHz | ≤ 0.7 kHz | ≤ 0.7 kHz | ≤ 1.2 kHz |
| Module NTC | B25/50 3375 (both modules — same curve) | | | |

## 3. Power envelope (review R-F09)

- **FW-03** Torque/power limits follow V_DC: P_max(V) = min(P_rated, √(3/2)·0.95·V·I_limit·0.85),
  with the current limits of §2. Full ratings exist only above these voltages:

| SKU | Peak 30 s | Continuous | At the low end of the range |
|---|---|---|---|
| 8XX (SiC, IGBT) | 220 kW ≥ 654 V | 120 kW ≥ 656 V | 168 / 91 kW at 500 V |
| 4XX (IGBT, SiC) | 150 kW ≥ 379 V | 90 kW ≥ 364 V | 99 / 62 kW at 250 V |

- **FW-04** Thermal derating from the module NTCs and coolant temperature; the rated 30 s peak
  assumes 65 °C coolant and the (to-be-measured) 0.045 K/W coldplate. Peak is re-allowed only
  after the continuous state has recovered (S4 time constants).

## 4. Fast protection paths

- **FW-05** Phase overcurrent: ADC channel thresholds with hardware compare (S32K396 ADC
  watchdog / BCTU → eTPU fault input) at 1.25 × the SKU peak, response ≤ 2 PWM periods,
  independent of the FOC ISR. Σ(Ia+Ib+Ic) plausibility every sample.
- **FW-06** DC overvoltage: both V_DC channels in hardware compare at the SKU OV trip; action =
  zero torque request + ASC request (if the §6 matrix allows) **within 20 µs** of the link
  crossing the trip, AMC1311 delay included. That needs the V_DC channels on a **free-running
  conversion slot (≥ 100 kHz)**, not the PWM-synchronised list — at 5 kHz a once-per-period
  sample alone is 200 µs. Rationale (N9): with the battery path lost at the SKU's full regen
  power the link charges at ≈0.9 V/µs (8XX: 220 kW into 291 µF). A 100 µs response ends at
  962 V (96 % of the cans' 85 °C U_N) and 200 µs at 1038 V; 20 µs ends at 897 V (4XX: 538 V
  of 600 V). The earlier 100 µs budget was sized at 100 kW.
- **FW-07** V_DC plausibility: |VDC1 − VDC2| > 5 % ⇒ fault; `VOFS` (the receivers' shared
  +0.5 V offset, now on an ADC pin) outside 0.475–0.525 V ⇒ **both channels invalid** (a failed
  offset buffer shifts both by up to 228 V and passes the 5 % check); with contactors closed,
  |V_DC − V_pack(BMS)| > 3 % ⇒ fault. A channel reading < 0.25 V is the AMC1311 fail-safe
  state, not a dead bus.
- **FW-08** Regeneration with the battery path lost (contactor opens, BMS limit drops to 0):
  enter DC-link voltage control (torque → 0 at the current-loop rate) and rely on FW-06 for
  the fast part; request the VCU/BMS protocol "zero torque before opening" for every
  non-emergency opening.

## 4b. Regenerative braking — who owns what (vehicle level)

The inverter is the only device that can turn wheel torque into DC power, so it **executes**
regeneration and **protects itself** during it. It does not decide how much regen the vehicle
wants, and it has no dump resistor.

| Function | Owner | The inverter's part |
|---|---|---|
| When to regenerate and how much (lift-off coast, brake pedal) | VCU | executes the negative torque request; reports the regen torque available within its limits (FW-03) |
| Blending with the friction brakes; ABS/ESC cutting regen on wheel slip | brake system, via the VCU | torque follows within the current-loop time; a stale command ramps to zero (FW-11) |
| How much charge the battery can accept | BMS (charge current / voltage limits) | caps regen at the limit relayed by the VCU; BMS-limit timeout ⇒ zero regen (FW-11) |
| Opening the battery path during regen | BMS/VCU ask for zero torque first (FW-08) | emergency opening: FW-06 hardware compare ≤ 20 µs → zero torque + ASC/SPO per §6; DC-link voltage control (FW-08) |
| Uncontrolled regen with the inverter off at speed | inverter + motor spec | §6: LS-ASC above n_x; total LV loss is energy-safe only below the E_LL,pk limit |
| Device heating in regen | inverter | the diode carries regen current — diode Tj verified per SKU (design-verify) |
| Energy that cannot go to the battery | vehicle friction brakes | none — the discharge resistors are shutdown bleeders, not a regen dump |

## 5. Monitoring (slow paths)

| Req | Signal | Rule |
|---|---|---|
| FW-09 | HVIL (`INTRLOK_N` ADC signature 3.0 / 2.0 / 2.5 V) | open ⇒ ramp torque to zero, report to VCU; the safe state follows §6 — HVIL open is **not** a licence to open contactors at speed. Reaction ≤ 100 ms. (R-F12: there is no hardware comparator.) |
| FW-10 | Resolver (SDADC sin/cos + excitation monitor) | amplitude (sin²+cos²) window, tracking error, angle-rate plausibility vs current model, excitation-monitor level; any fault ⇒ no angle-dependent torque, §6 state |
| FW-11 | CAN torque command | counter + CRC, ≤ 20 ms staleness ⇒ ramp to zero torque (not hold last value); BMS limit timeout ⇒ zero regen |
| FW-12 | FS26 | OTP set of design-basis §8a verified by SPI readback at every boot (R-F38); FS1B policy per §6; watchdog window configured before FS0B release |
| FW-13 | Temperatures | module NTCs (open/short/rate), motor sensors, board NTCs; derate per FW-04 |
| FW-14 | Gate power | RDY_HS/RDY_LS low ⇒ no PWM; flyback enables sequenced before DRV_EN |

## 6. Safe-state decision matrix (review R-F13/F15)

The inverter provides three actuators: **SPO** (DRV_EN low — three-phase open), **LS-ASC**
(latched, overrides EN; high-side ASC is not implemented) and torque-controlled ramp-down.
Which one is safe depends on the **motor**: the crossover speed n_x where the line-line
back-EMF peak √3·ω_e·ψ_f equals the allowed link voltage (880 V 8XX / 530 V 4XX, cold
magnets). Motor data are a commissioning input (R-F16); the table is the rule.

| Condition | n < n_x | n ≥ n_x |
|---|---|---|
| Healthy, command/CAN lost | ramp to zero torque, then SPO | ramp to zero torque; keep current control (field weakening) while the battery is present |
| Battery contactor opens / BMS limit 0 | zero torque, then SPO | **LS-ASC** (FW-06), release to SPO below n_x |
| Resolver invalid | SPO | LS-ASC (no angle needed) |
| DESAT on a **high-side** switch (FLT_HS) | SPO | LS-ASC permitted (a shorted LS leg completes the symmetric short) |
| DESAT on a **low-side** switch (FLT_LS) | SPO | **SPO only** — the cause may be a shorted HS switch; LS-ASC would short the link through the motor |
| MCU hang / reset | hardware: FS0B ⇒ SPO; FS1B ⇒ LS-ASC per FW-12 policy | same — choose the FS1B policy from the motor's n_x (FS1B unconditional ASC is safe only if the motor tolerates ASC braking torque at low speed) |
| Total LV (KL30) loss | SPO (gates park low by UVLO) | SPO — **energy-safe only if E_LL,pk(n_max) < 1000 V (8XX cap rating at 85 °C) / 600 V (4XX)**. Motors above that need the HV-fed backup-bias option (TI TIDM-02014 pattern) before release — S10: gate reservoirs hold ASC only 1.1–3.2 ms and the command path ≈1 ms |

## 7. Fault-latch recovery (review R-F06 — NSI6611 DS 1.2 §8.10/§9.4)

The NSI6611 releases FLT **only on an RST/EN rising edge after RST/EN has been low for
t_FLT_MUTE ≥ 0.55–1.3 ms**. All six RST/EN pins are DRV_EN, which the latch holds low. So:

1. **FW-15** On FLT_HS/FLT_LS: PWM inputs to zero, log which bank, apply §6.
2. Wait ≥ 1.5 ms with DRV_EN low (latch holds it).
3. Keep PWM inputs low and MCU_GATE_EN high; drive `PTD9_FLTCLR` high→low once. The 15 nF
   one-shot holds CLR low ≥ 54 µs: /Q goes high, DRV_EN rises 12–40 µs later (delay RC), the
   drivers see their rising edge and release FLT, PRE returns high while CLR is still low, the
   latch ends cleared. (If FLT does not release, the latch re-sets when CLR returns — fail-safe.)
4. Confirm FLT_HS/FLT_LS high and RDY high before re-enabling PWM. ≤ 3 retries per key cycle,
   then latched DTC.
5. **FW-16** Boot self-test: clear once (the flip-flop powers up in an undefined state), read
   back the MCU pin, and verify DRV_EN cannot rise with FS0B asserted (EOL test).

## 8. Discharge (review R-F22/F23)

- **FW-17** Fire QDIS only when the VCU/BMS reports the main contactors **open**, auto-release
  after 5 s, at most 3 discharges per 5 min (32 J/resistor pulses; thermal recovery).
- **FW-18** Witness on both V_DC channels: expected τ (§2); no decay within 200 ms ⇒ stuck-off
  DTC; the passive bleeder still guarantees < 60 V in 65 s (8XX) / 89 s (4XX) worst case.
- **FW-19** Precharge plausibility: a link that plateaus ≈5 % below the pack or charges with a
  short time constant indicates a shorted QDIS/string ⇒ refuse to arm. (A stuck-ON QDIS with
  the battery connected dissipates 384 W / 284 W — the fail-open flameproof wirewound class
  bounds it; it is never demonstrated on a live battery.)

## 9. Start-up sequence

KL30 → FS26 rails → MCU boot (DRV_EN, PWM, ASC_REQ, flyback EN held low by pulldowns and
FS0B) → FW-12 readback → FW-01/FW-02 identity → FW-16 latch self-test → VOFS and both V_DC
channels at their healthy-zero 0.5 V → enable flybacks (S1: one burst, 75–295 ms to rails)
→ RDY → precharge monitoring (FW-19) → arm (FS0B release, MCU_GATE_EN) → zero torque.

## 10. Calibration, updates, diagnostics (R-F42/F43, RV-09)

- **FW-20** Versioned, CRC- and range-checked calibration records (current offset/gain/sign,
  V_DC gain/offset per channel, resolver gain/phase/offset, electrical zero, pole pairs) tied to
  hardware serial, SKU and motor ID; any failure ⇒ no torque.
- **FW-21** Signed images, rollback, a no-torque update state; DRV_EN is hardware-inhibited
  through reset, erase/program and debug (pulldowns + FS0B — verified structurally).

## 11. What this contract does not close

Double-pulse (turn-off overshoot at 850 V, R_G_OFF), contained short-circuit tests per silicon,
thermal/coldplate, EMC, LV transients (ISO 16750-2/7637-2 per the OEM contract), insulation
coordination and mechanical DV are hardware gates — listed in
[`review-A6-disposition.md`](review-A6-disposition.md).
