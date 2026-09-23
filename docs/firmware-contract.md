# Hardware → firmware contract (rev A.8)

The hardware protects what software cannot react to in time; firmware owns every operating
limit. This file is the contract between the two for **every SKU of the platform**. Each
review finding dispositioned as *Firmware Handled* (round 6:
[`review-A6-disposition.md`](review-A6-disposition.md), round 7:
[`review-A7-disposition.md`](review-A7-disposition.md), round 8:
[`review-A8-disposition.md`](review-A8-disposition.md)) points at a numbered requirement here
(`FW-xx`). The traction application itself is a separate deliverable (review R-F08); nothing in
this repository claims it exists. Verification of each requirement is HIL first, then bench.

## 1. Division of labour

| Fault class | Fast enough only in hardware | Owned by firmware |
|---|---|---|
| Short circuit / shoot-through | NSI6611 DESAT + soft turn-off per switch; global fault latch → DRV_EN (delayed 22–53 µs so it cannot cut a soft turn-off short) | diagnosis, retry limits, FLT_HS vs FLT_LS → safe-state choice (§6); FLT on the eFlexPWM fault inputs (FW-15) |
| Watchdog / MCU hang / reset | FS26 Q&A watchdog → FS0B → DRV_EN low (three-phase open); FS1B → LS ASC latch, entered break-before-make in hardware (§4c); every harness enable/PWM line default-OFF | FS26 configuration and the FS1B policy (FW-12) |
| Gate-power loss | NSI6611 VCC2 UVLO parks the gate low; RDY → DRV_EN | — |
| Phase overcurrent (operating) | — (DESAT sits at ≈2.8× the rated peak) | ADC hardware compare → PWM fault input, ≤ 2 PWM periods (FW-05) |
| DC-link overvoltage (regen / contactor open) | ASC entry once requested: ≤ 7.0 µs, break-before-make (§4c) | ADC hardware compare on both V_DC channels → ASC request ≤ 15.6 µs after the trip crossing (FW-06, round 7) |
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
| Current-loop crossover ceiling (S6, 45° PM) | ≤ 1.2 kHz at 10 kHz · **≤ 1.1 kHz at 8 kHz** | ≤ 0.7 kHz | ≤ 0.7 kHz | ≤ 1.2 kHz at 10 kHz · **≤ 1.1 kHz at 8 kHz** |
| Module NTC | B25/50 3375 (both modules — same curve) | | | |

The crossover ceiling is set **per switching mode**, not per silicon (round 7, RR08): the S6 model
gives 51.4° at 10 kHz / 1.2 kHz but 43.3° at 8 kHz / 1.2 kHz, and 47.2° at 8 kHz / 1.1 kHz. The
parameter set carries one gain set per f_sw; motor L/R and sampled-data timing re-close it.

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
  high sides forced off + zero torque request + ASC request, then PWM-ASC (§4c; if the §6 matrix
  allows). Round 7 (RR06/A6-R08) replaces the written "20 µs" with a budget that must be
  **measured on HIL**, link crossing the trip → `ASC_REQ` edge:

  | Stage | Allocation |
  |---|---|
  | Divider/filter lag (2.82 MΩ ∥ 6.2 kΩ, 1 nF: a first-order filter trails a ramp by τ) | 6.2 µs |
  | AMC1311B signal delay 50–50 % (max) | 2.1 µs |
  | Card receiver | 0.3 µs |
  | Sample wait — each V_DC channel **free-running at ≥ 200 kS/s** (not the PWM-synchronised list) | ≤ 5.0 µs |
  | Conversion | 1.0 µs |
  | ADC analog watchdog → eFlexPWM fault input (high sides off in hardware) → `ASC_REQ` | 1.0 µs |
  | **To the ASC request** | **≤ 15.6 µs** |
  | Hardware ASC entry, break-before-make (§4c) | ≤ 7.0 µs |

  Route: ADC analog-watchdog (hardware threshold per channel) → TRGMUX → eFlexPWM FAULT and the
  `ASC_REQ` edge (a timer/trigger output or the first instruction of the fault ISR). A separate
  comparator is added only if this route misses the budget on HIL. Rationale (N9): with the
  battery path lost at the SKU's full regen power the link charges at ≈0.9 V/µs (8XX: 220 kW
  into 291 µF). After the PWM-off, all six switches are off until the low sides are in ASC. The
  phase current (up to 481 A) then rectifies into the link at ≈1.65 V/µs. The whole chain ends
  at 905 V (90 % of the cans' 1000 V U_N at 85 °C; 4XX: 542 V of 600 V). A 100 µs response would end at 962 V and a once-per-PWM-period sample
  at 5 kHz (200 µs) at 1038 V.
- **FW-07** V_DC plausibility: |VDC1 − VDC2| > 5 % ⇒ fault; `VOFS` (the receivers' shared
  +0.5 V offset, now on an ADC pin) outside 0.475–0.525 V ⇒ **both channels invalid** (a failed
  offset buffer shifts both by up to 228 V and passes the 5 % check); with contactors closed,
  |V_DC − V_pack(BMS)| > 3 % ⇒ fault. A channel reading < 0.25 V is the AMC1311 fail-safe
  state, not a dead bus.
- **FW-08** Regeneration with the battery path lost (contactor opens, BMS limit drops to 0):
  enter DC-link voltage control (torque → 0 at the current-loop rate) and rely on FW-06 for
  the fast part; request the VCU/BMS protocol "zero torque before opening" for every
  non-emergency opening.
- **FW-08b** After any DESAT at n ≥ n_x, report "fault recovery, keep HV connected" to the VCU/BMS
  until ASC is re-established or n < n_x (round 8, R7-06). During the mandatory ≥ 1.5 ms driver
  reset the bridge is three-phase open, and the battery absorbs the rectified current.
  After an **FLT_LS** the bridge stays in SPO until n < n_x (§6), so the rectified charge flows for
  seconds, not milliseconds. The VCU brings n below n_x with the friction brakes, and the pack must
  take that charge. Whether it can (full or cold pack, BMS short-term charge limit) is a vehicle-
  integration check from the motor data (I and E at n_max). A BMS that opens earlier creates the §6
  double fault: the link then rises toward E_LL,pk(n) (cross-check R8X-15).

## 4b. Regenerative braking — who owns what (vehicle level)

The inverter is the only device that can turn wheel torque into DC power, so it **executes**
regeneration and **protects itself** during it. It does not decide how much regen the vehicle
wants, and it has no dump resistor.

| Function | Owner | The inverter's part |
|---|---|---|
| When to regenerate and how much (lift-off coast, brake pedal) | VCU | executes the negative torque request; reports the regen torque available within its limits (FW-03) |
| Blending with the friction brakes; ABS/ESC cutting regen on wheel slip | brake system, via the VCU | torque follows within the current-loop time; a stale command ramps to zero (FW-11) |
| How much charge the battery can accept | BMS (charge current / voltage limits) | caps regen at the limit relayed by the VCU; BMS-limit timeout ⇒ zero regen (FW-11) |
| Opening the battery path during regen | BMS/VCU ask for zero torque first (FW-08) | emergency opening: FW-06 hardware compare → ASC request ≤ 15.6 µs, ASC entry ≤ 7.0 µs → zero torque + ASC/SPO per §6; DC-link voltage control (FW-08) |
| Uncontrolled regen with the inverter off at speed | inverter + motor spec | §6: LS-ASC above n_x; total LV loss is energy-safe only below the E_LL,pk limit |
| Device heating in regen | inverter | the diode carries regen current — diode Tj verified per SKU (design-verify) |
| Energy that cannot go to the battery | vehicle friction brakes | none — the discharge resistors are shutdown bleeders, not a regen dump |

## 4c. ASC entry and exit (round 7, RR05 + cross-check)

The high sides have no ASC input. ASC acts on the three low-side drivers and outranks their EN
and IN. Two datasheet facts set the design (NSI6611 DS 1.2):

- **Priority.** ASC outranks VCC1-UVLO, EN and IN, but not VCC2-UVLO.
- **DESAT over ASC.** DESAT is honoured over ASC **only with EN/RST high and the driver's own
  input commanding on** (IN+ high, IN− low; §8.12 function table, Fig. 8.11). With EN low and ASC
  high, the table marks DESAT "irrelevant" and the output stays on.

So entry is ordered **without using EN** on the path where the MCU is alive. That keeps the
low-side short-circuit protection active during a commanded ASC.

**MCU path** (FW-06 over-voltage, resolver invalid, contactor open at speed):
1. **High sides off.** The ADC-watchdog fault channel of the eFlexPWM is mapped (DISMAP) to the
   **high-side outputs only**, so hardware forces PWM_xH low. The high sides start turning off
   within 0.13 µs (IN+ low). A firmware-detected trigger does the same in software.
2. **Latch.** The fault ISR asserts `ASC_REQ`. The power board's delay (CASCD 12 nF) holds the
   low-side ASC pins below threshold ≥ 3.0 µs, plus the NSI6611's own 0.39–1.1 µs, so the ASC pins
   engage **≥ 3.4 µs** after the request and complete ≤ 7.0 µs.
3. **PWM-ASC.** No sooner than the dead time after step 1 (2.5 µs IGBT, 1.0 µs SiC), the ISR sets
   the low sides to 100 % on (PWM_xL = 1), with `MCU_GATE_EN` high:
   - the low sides are on through IN+ with EN high, so their DESAT is documented to act;
   - each high side is held off by the fault state and by its own IN− interlock.

   The latch stays set as the hold that survives an MCU reset. The DESAT fault channels (FW-15)
   still map to all six outputs.

**FS1B path** (MCU dead): FS0B drops `DRV_EN` and the high sides turn off ≈0.2 µs later. FS1B
sets the latch, and CASCD delays the low sides ≥ 3.4 µs. That is break-before-make in hardware.
In this state EN is low, so a high side that **fails short during** an FS1B-ASC is not bounded
by the low-side DESAT. That takes a double fault (MCU dead plus a new HS short). A NOVOSENSE
statement on DESAT with ASC high and EN low is a release gate. Until then, FW-12 chooses FS1B-ASC
only for motors that need it (§6).

**A DESAT while ASC is active** (round 8, R7-01/A7-N01). Example: a high side fails short during
PWM-ASC. The NSI6611 behaviour comes from DS 1.2 Fig. 8.11 (ASC high, DESAT, then IN low, then an
RST/EN pulse).

| Step | Faulted low side | Healthy low sides | High sides |
|---|---|---|---|
| PWM-ASC (EN 1, IN+ 1, IN− 0, ASC 1) | on | on | off (IN− interlock) |
| Its DESAT trips (documented: EN high, IN+ high) | soft turn-off, FLT latched | on | off |
| FLT → **UASCG masks ASC_CMD** (hardware, ≤ 0.01 µs; pins release ≤ 0.75 µs); FW-15 fault forces PWM low | **held off by its own latched fault**: it stays off through IN low, EN low and ASC | off (IN+ low, no ASC) | off |
| Fault latch drops DRV_EN 22–53 µs later | held off | off | off |
| **Result** | | **SPO**, as §6 requires for an LS DESAT | |

The faulted driver can come back on only at an RST/EN **rising edge after its mute time**, which is
the deliberate FW-15 recovery. It then follows IN and ASC, so FW-15 **always** clears the ASC latch
first (UASCG masks ASC only while FLT is latched). ASC returns only through the MCU path below,
after FW-15 step 4 (cross-check R8X-01). The mask also covers FS1B-ASC: a reset after a DESAT
(a causally related second event, review T7-02) cannot bring ASC back while the fault is latched.
That is one bounded SC event per recovery, never a sustained shoot-through.

- **FW-06a** ASC exit is MCU-commanded:
  1. `ASC_CLR` pulse while PWM-ASC still holds the low sides on.
  2. Move PWM from PWM-ASC to the next state (SPO or modulation); the eFlexPWM inserts the dead
     time.
  3. Keep the first high-side pulse ≥ 1 µs after the clear (the ASC pins release in ≤ 0.75 µs).

  An exit is never needed in a hurry. After an MCU reset, the latch is **not** cleared blindly;
  §9 decides from speed.

**Gate-logic supply loss (V5GD off, V5A on)** (cross-check R8X-10). The NSI6611 honours ASC with
VCC1 off (DS §8.11), but DS 1.2 does not say how an unpowered FLT pin behaves. If it clamps to VCC1,
the 5.1 k V5A pull-ups read as FLT, UASCG masks ASC and the bridge goes to SPO, as in §6's total-LV
row. The FLT/RDY absolute maximum is VCC1 with no +0.3 V, and V5A may sit up to 0.2 V above V5GD in
normal operation (P-03). Both are bench-matrix items: V5GD off / V5A on, and V5A high / V5GD low,
reading FLT_x_N, RDY_x and ASC_CMD.

## 5. Monitoring (slow paths)

| Req | Signal | Rule |
|---|---|---|
| FW-09 | HVIL (`INTRLOK_N` ADC signature 3.0 / 2.0 / 2.5 V) | open ⇒ ramp torque to zero, report to VCU; the safe state follows §6 — HVIL open is **not** a licence to open contactors at speed. Reaction ≤ 100 ms. (R-F12: there is no hardware comparator.) |
| FW-10 | Resolver (SDADC sin/cos + excitation monitor) | amplitude (sin²+cos²) window, tracking error, angle-rate plausibility vs current model, excitation-monitor level; any fault ⇒ no angle-dependent torque, §6 state |
| FW-11 | CAN torque command | counter + CRC, ≤ 20 ms staleness ⇒ ramp to zero torque (not hold last value); BMS limit timeout ⇒ zero regen |
| FW-12 | FS26 | OTP set of design-basis §8a verified by SPI readback at every boot (R-F38); FS1B policy per §6; Q&A watchdog configured before FS0B release with **WD_ERR_LIMIT = 2** (not the default 6), WD_FS_REACTION = RSTB + FS0B (default), window ≤ 3 ms, so runaway code asserts FS0B within about two windows (round 7, RR03). **FS1B_TDELAY = 0 (required)**: FS1B asserts with FS0B, FW-16 step a reads the ASC preset at once, and a delay would only lengthen the SPO interval after an FS0B event; the ASC entry is break-before-make in hardware (§4c). **FS1B_TDUR = 100 ms** (default): the ASC latch holds after FS1B releases, and the bounded pulse also bounds RFS4 on a FAULT_OUT wire short (§9). Keep BACKUP_SAFETY_PATH_FS0B = 1 and BACKUP_SAFETY_PATH_FS1B = 1 (both default: a short-to-high asserts RSTB; for FS1B this is reachable only with FAULT_OUT shorted to KL30, §9). FS_GPIO1 (flyback-enable OR input) stays low from POR until §9 step 6, then high, so gate power is held through an MCU reset but is never up while FS1B is still asserted at boot. The OTP must configure GPIO1 **push-pull and not slotted** (design-basis §8a): a slotted push-pull GPIO1 goes high by itself at power-up (DS Table 133). The FW-16 boot test asserts FS0B with FS0B_REQ (FS_SAFE_IOS_1 bit 6). FS1B-ASC only for motors that need it (§4c: with EN low the LS DESAT is not documented). Outside that policy, only FW-16 switches the low sides on through ASC. It does so in states without documented LS DESAT: step a (EN low, via FS1B), step f (EN high, IN+ low) and step h (both, around the FLT injection). Each runs only under its measured no-HV, standstill conditions |
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
| DESAT on a **high-side** switch (FLT_HS) | SPO | SPO first. The fault latch holds EN low, and LS-ASC with EN low has no documented LS DESAT. After the FW-15 reset (≥ 1.5 ms), LS-ASC is permitted **only as PWM-ASC** with EN high (§4c). **Assumption:** the HS DESAT came from a shorted LS device or a phase-to-DC− fault, which LS-ASC completes into a symmetric short. **If an HS device has itself failed short**, the LS DESAT (documented with EN high and IN+ high) soft-turns the LS off, FLT_LS follows and the row below applies (SPO): one bounded extra SC event, never a sustained shoot-through. The ASC latch alone must not be used for this row. **Energy during the SPO interval** (round 8, R7-06): for ≥ 1.5 ms the rectified motor current flows into the battery, so the inverter asks the VCU/BMS to keep the contactors closed until ASC is back or n < n_x (FW-08b). Whether the link stays inside its rating if the battery path is lost inside that window is a motor/pack commissioning check (E_LL, L_d, n_max, DC-link energy 32.8 J 8XX / 28.6 J 4XX to U_N). A battery lost in that window is a double fault; no brake chopper by default |
| DESAT on a **low-side** switch (FLT_LS) | SPO | **SPO only** — the cause may be a shorted HS switch; LS-ASC would short the link through the motor |
| MCU hang / reset | hardware: FS0B ⇒ SPO; FS1B ⇒ LS-ASC per FW-12 policy | same — choose the FS1B policy from the motor's n_x. Unconditional FS1B-ASC is safe only if the motor tolerates ASC braking torque at low speed. In FS1B-ASC EN is low (§4c), so a new HS short during it is not bounded by the drivers — a double fault. After the reset, §9 decides from speed whether to keep ASC |
| Total LV (KL30) loss | SPO (gates park low by UVLO) | SPO — **energy-safe only if E_LL,pk(n_max) < 1000 V (8XX cap rating at 85 °C) / 600 V (4XX)**. Motors above that need the HV-fed backup-bias option (TI TIDM-02014 pattern) before release — S10: gate reservoirs hold ASC only 0.5–3.1 ms (round 7: worst case now starts at the 13.54 V low-corner rail) and the command path ≈1 ms |

**Commissioning check (cross-check R8X-13).** A DESAT during FW-06 ASC now always ends in SPO. So the
peak phase current at ASC entry (n_max, cold magnets) must stay below the low-side DESAT minimum
with margin (SiC 7.2 V at the switch ≈ 1.3 kA hot; IGBT from its V_CE(sat) curve at the 4.2 V
minimum, design-verify "DESAT trip"). Otherwise ASC entry alone trips DESAT and FW-06 lands in SPO
exactly when the battery is gone.

## 7. Fault-latch recovery (review R-F06 — NSI6611 DS 1.2 §8.10/§9.4)

The NSI6611 releases FLT **only at an RST/EN rising edge after its mute time**. The mute time is
0.55–1.3 ms from the fault, and the driver ignores any reset during it. The DS is not consistent
about the reset itself:
- §8.10 and Fig. 8.8: any RST/EN low pulse ≥ t_RST_FIL (≤ 0.8 µs) after the mute time releases
  FLT at its rising edge;
- §9.4: the low must be held ≥ t_FLT_MUTE.

FW-15's ≥ 1.5 ms low satisfies both. Under the shorter reading, a pin re-pulsed **slowly** by runaway
code can also deliver a valid reset after the mute time (FW-15 step 3). That case therefore rests on
the eFlexPWM lock and the watchdog, not on the driver's own latch (cross-check R8X-09).
Until the reset, the driver holds its own gate off through IN, EN and ASC changes (Fig. 8.11). All six RST/EN pins
are DRV_EN, which the fault latch holds low. So:

1. **FW-15** On FLT_HS/FLT_LS: PWM inputs to zero, log which bank, apply §6. **Hardware PWM
   inhibit (round 7, RR03):** `FLT_HS_N`/`FLT_LS_N` (PTC26/PTC25) are routed through the SIUL2
   input mux to eFlexPWM FAULT inputs. They run in fail-safe, manual-clear mode, set at init and
   write-protected. While any FLT is asserted, the PWM outputs are forced low inside the MCU,
   whatever the code does. If the input mux cannot reach a FAULT input from these pins, swap to
   pins that can; that is a card pin swap at zero BOM cost, confirmed before layout.
2. Wait ≥ 1.5 ms with DRV_EN low (latch holds it). **Always clear the ASC latch** (`ASC_CLR`).
   UASCG masks ASC only while FLT is latched. A latch left set would bring ASC back at the step-3
   reset edge while PWM is still forced low: low sides on with IN+ low, where DESAT is not
   documented (Fig. 8.11, cross-check R8X-01). If §6 wants LS-ASC after the recovery, it is
   re-entered only after step 4, through the §4c MCU path (clear FFLAG, PWM-ASC, then `ASC_REQ`).
3. Keep PWM inputs low and MCU_GATE_EN high; drive `PTD9_FLTCLR` high→low once. The 15 nF
   one-shot holds CLR low for 72–210 µs (measured at the Schmitt buffer output). /Q goes high
   and DRV_EN rises 16–46 µs later (delay RC). The drivers see their rising edge and release
   FLT, and PRE returns high while CLR is still low, so the latch ends cleared. If FLT does not
   release, the latch re-sets when CLR returns (fail-safe). **What the one-shot bounds (RR03):**
   it bounds one stuck-low pin, not code that keeps re-pulsing it. That case is covered three
   ways:
   - the eFlexPWM fault lock above keeps PWM forced low while FLT is asserted;
   - a faulted driver stays off while the re-pulsing is **fast**: each falling edge inside the
     72–210 µs one-shot keeps DRV_EN high, so there is no RST/EN edge. **Slower** re-pulsing lets
     the latch re-set and drop DRV_EN between pulses, and a rising edge after the mute time is a
     valid reset (§8.10). The driver then follows IN, which the eFlexPWM lock still holds low.
     FFLAG is cleared only in FW-15 step 4, for the §6 PWM-ASC re-entry or the one VCU-authorised
     retry, and at the end of FW-16 step h;
   - the FS26 watchdog asserts FS0B (FW-12), and FS0B's release needs a token-derived SPI
     write.
4. Confirm FLT_HS/FLT_LS high, RDY high and `ASC_CMD_RB` 0 (latch cleared in step 2) before
   clearing FFLAG and re-enabling PWM. **FLT is a DESAT (short-
   circuit) event, so there is no automatic retry** (round 7, A6-R05): one VCU-authorised retry per
   key cycle, no sooner than 1 s after the event and at reduced torque; a second DESAT latches the
   DTC. Three quick retries into a hard short would be four SC events in ≈5 ms. Gate-supply
   undervoltage is signalled on RDY, not FLT, and recovers when RDY returns.
5. **FW-16** Boot self-test through the `DRV_EN_RB` and `ASC_CMD_RB` read-backs (round 8,
   R7-04/A7-N02). Each shutdown term is tested **while every other term is known permissive**,
   with both polarities, so no term can hide behind another (with gate power off, RDY holds the
   chain low and would mask FS0B).

   **Conditions (measured, cross-check R8X-02):**
   - gate power up (RDY high);
   - **no HV**: both V_DC channels valid (VOFS in its window) and below 60 V, and the VCU reports the
     main contactors open. The VCU starts precharge only after the inverter reports "self-test
     done" (§9 step 7);
   - **standstill**: resolver valid and |n| < n_ss;
   - all six PWM outputs held low, so EN high cannot turn a gate on;
   - fault latch cleared.

   If a condition fails, FW-16 is **skipped** rather than run on assumption. That happens after an
   MCU reset with the contactors closed, or at speed. The inverter then arms on the stored result of
   the last complete pass if it dates from this or the previous key cycle (NVM), and reruns FW-16 at
   the next eligible key-on. With no stored pass it does not arm and sets a DTC. A skip never delays
   PWM-ASC after a reset at speed (§9 step 5).

   | Step | Stimulus | Expected | Proves |
   |---|---|---|---|
   | a | MCU_GATE_EN 1, FS0B **asserted** (`FS0B_REQ`) | DRV_EN_RB 0; ASC_CMD_RB 1 within 20 µs (FS1B asserts with FS0B, FS1B_TDELAY = 0 per FW-12, and presets the ASC latch) | FS0B path (USCH ch3, UAND1) not stuck high; FS1B → ASC preset path works |
   | b | release FS0B/FS1B (token write), `ASC_CLR` | DRV_EN_RB **1**; ASC_CMD_RB 0 | chain and read-back not stuck low; ASC clear works; USCH2 alive (a dead one reads as FLT through RFCB) |
   | c | MCU_GATE_EN 0, then 1 | 0, then 1 | UAND1.B |
   | d | FS_GPIO1 **low** (SPI) and MCU_EN_FLYBK_HS 0 until RDY_HS low (record the time), then MCU_EN_FLYBK_HS 1 until RDY_HS high | 0, then 1 | RDY_HS path (the OR output can only fall with both inputs low, cross-check R8X-03) |
   | e | the same with MCU_EN_FLYBK_LS | 0, then 1 | RDY_LS path |
   | f | `ASC_REQ`, then `ASC_CLR` | ASC_CMD_RB 1, then 0 | ASC latch clock and clear, ASC gate with FLT healthy |
   | g | FS_GPIO1 **high**, both MCU flyback enables 0 for twice the RDY drop time recorded in d/e, then 1 | RDY_HS and RDY_LS stay high | the FS_GPIO1 inputs of UOR1/UOR2, which hold gate power for FS1B-ASC through an MCU reset |
   | h | `ASC_REQ`; drive PTC26 (FLT_HS_N) low, release, clear through the one-shot; the same with PTC25 (FLT_LS_N); `ASC_CLR` | each time: ASC_CMD_RB 0 at once, DRV_EN_RB 0 within 60 µs, eFlexPWM FFLAG set; after the clear DRV_EN_RB 1 and ASC_CMD_RB 1; finally ASC_CMD_RB 0 | FLT diode-OR (DFLT1/2, RFLTC), USCH2 ch1, fault-latch preset, UASCG mask and the eFlexPWM FAULT routing (cross-check R8X-17) |

   **Step h pad rule.** The FLT pins are pulled low only by toggling the pad's output-buffer
   enable, with its data register held at 0. The pad configuration is re-locked afterwards and
   FFLAG is cleared at the end of the step. A stuck setting can only pull FLT low, which is
   fail-safe; it can never mask a real FLT.

   Steps a, f and h switch the low sides on through ASC at standstill with no HV (measured), which
   is harmless. Any mismatch ⇒ no arming and a DTC.

   **Field coverage.** Step h now reaches the FLT → fault-latch → FLT_OKB path and the FLT → UASCG
   mask at every eligible key-on; before round 8's cross-check these waited for the EOL rig. The EOL
   rig still drives the FLT lines from the power-board side, which step h cannot reach: the harness
   and the drivers' own FLT outputs.

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

FS26 asserts **FS0B and FS1B after every POR or wake-up** (DS §22.11.3). FS1B holds the ASC latch
preset until it is released, and the latch holds ASC once gate power exists. The order therefore
is (round-7 cross-check):

1. KL30 → FS26 rails → MCU boot. DRV_EN, PWM, ASC_REQ, the discharge command and the flyback
   enables are held low by pull-downs and FS0B. The `ASC_CLR` output latch is written high before
   its pin driver is enabled.
2. FW-12 readback → FW-01/FW-02 identity → fault-latch clear through the one-shot.
3. Resolver and both V_DC channels valid (VOFS and the healthy-zero 0.5 V), so the speed n is
   known.
4. Release FS0B/FS1B, with MCU_GATE_EN still low, so DRV_EN cannot rise.
5. ASC latch decision:
   - n < n_x (or the battery present and current control ready): `ASC_CLR`.
   - n ≥ n_x: `ASC_REQ` (idempotent). ASC is kept, and PWM-ASC takes over once armed.
6. Enable the flybacks (S1: one burst, 73–240 ms to rails) and FS_GPIO1 high.
7. RDY → **FW-16** if its measured conditions hold (no HV, standstill; step a re-asserts FS0B by
   SPI), otherwise the stored pass (FW-16) → report "self-test done". The VCU precharges only after
   that → precharge monitoring (FW-19).
8. Arm with MCU_GATE_EN high: the FS0B edge has already settled, and RDY rises while MCU_GATE_EN
   is still low.
9. Zero torque.

**A driver FLT still low at boot** is a pending DESAT from before the reset (review T7-02,
cross-check R8X-14). The step-2 one-shot cannot release it while FS0B holds DRV_EN low, so the
fault latch re-sets and FW-16 cannot start. The result is no arming, and SPO with ASC masked, which
is what §6 wants. Log the DTC and apply FW-08b. Run the FW-15 recovery (ASC latch cleared) only
after step 6 and the FS0B release, raising MCU_GATE_EN just for its reset pulse, and under the
one-retry rule.

**Vehicle `FAULT_OUT` (JVEH pin FAULT), electrical interface (round 7, A6-R01 + cross-check):**
- Active-low and **sink-only**: the FS26 FS1B output through 1 kΩ and a Schottky (DFO).
- The **VCU must provide the pull-up**, ≥ 10 kΩ to ≤ 5 V, into a 5 V CMOS input with ≤ 1 nF.
  It reads ≤ 1.2 V when asserted.
- A wire shorted to ground, a sleeping VCU input or a negative spike **cannot** reach the
  ASC-latch preset.
- A short to KL30 is clamped to ground at the latch input. ZSET is a BZT52-B5V6: ≤ 5.96 / 6.12 /
  6.33 V at 16 V / a 24 V jump start / a 35 V load dump, at 125 °C, which stays under the buffer's
  6.5 V absolute maximum. No low-impedance path leads into the V5A logic rail: ≤ 0.14 mA with V5A
  up, ≤ 0.63 mA with it off (round 8, A7-N04 + cross-check R8X-05).
- While FS1B is released the short is silent: the pin reads high, as it should. At the next FS1B
  assertion the outcome depends on the part's current limit, 4–22 mA (cross-check P-01):
  - FS1B still pulls the node low. The preset works, and RFS4 carries up to ≈ 0.23 W for the
    100 ms FS1B_TDUR, or until the boot release. That is a short-time overload the 0603 takes.
  - FS1B cannot pull the node low. The SBC reports FS1B short-to-high and, with
    BACKUP_SAFETY_PATH_FS1B = 1 (FW-12), asserts RSTB. The MCU path provides ASC after the reset
    (§9 step 5). ABIST1 at the next power-up may refuse the FS0B release: no arming and a DTC until
    the wire is repaired.
- FAULT_OUT is a 100 ms low pulse per FS0B event (FS1B_TDUR), and it is also low from power-up
  until §9 step 4. The VCU latches it and reads the fault state over CAN.
- A deliberate 12 V pull-up is not allowed.
- The budget keeps FS1B inside its 2 mA V_OL point, so the SBC's own read-back (low < 0.7 V)
  stays valid.

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
[`review-A6-disposition.md`](review-A6-disposition.md) and, for round 7, in
[`review-A7-disposition.md`](review-A7-disposition.md). The round-7 gates are:
- the LV-only gate-supply bench (six-domain VCC2 across KL30/temperature, start/stop/jump start);
- the ASC entry measurement (HS gate vs LS gate at no HV);
- the HIL measurement of the FW-06 chain.
