# Review round 23 — three rechecks of `452de85` (rev A.22)

**Inputs.** Three independent rechecks of the A.21 push `452de853…` (26 September 2026): (1) a recheck that carried five
findings from a report we had never received — A20-F01 (SDADC reference-to-supply tracking), A20-F02 (the R2R reference not
isolated), A20-F03 (torque over-delivery in field weakening, reproduced with a standalone C copy of `torque.c` at −O2 and
under ASan/UBSan in eight four-quadrant cases), A20-F04 (the eight-step MTPA fallback), A20-F05 (the BOM intro hard-coded to
the SiC 8XX); (2) a recheck closing A20-N01 and confirming the round-22 bindings, with one CAD-compatibility finding
(A21-R01: the KiCad 5 sets shipped KiCad-10-format footprint files) plus the JSWD finding; (3) a recheck with the JSWD
finding (A21-R01, an SMT connector on a through-hole land), the supported-version gap (A21-R02) and a stale handoff paragraph
(A21-N01), with the 56-case resolver fixture rerun. All three keep the round-22 corrections.

**Method.** Every claim to the source and the archived data sheets before any edit: the MCU_PINS table and the S32K39 data
sheet Rev. 3 (operating-conditions notes 4, 6, 7, 8 and the SAR accuracy condition) for the two reference items; the FS26
data sheet for the VREF and LDO-matching figures; `torque.c` read in full for the two control items; the Samtec FTSH product
print (fetched and archived) for the SWD header; the copied footprint files' version fields for the compatibility item.
Firmware by an Opus agent with fail-before/pass-after tests; hardware and documents by hand; every change proved by the
generators, the extended KiCad verifier and the firmware suite.

## Verdict

Five of the eight items are real: the R2R reference was on the shared reference net against the data sheet's note 8 (F214),
the SWD header is surface-mount and sat on a through-hole land (F215), the footprint library is a KiCad-10-format library and
was wrongly shipped inside the KiCad 5 sets (F216), the torque solver could return a current vector representing about twice
the requested torque in field weakening (F217) and the no-LUT MTPA fallback did not converge (F218); the BOM intro (F219)
and the stale handoff paragraph (F220) are documentation. One item is not supported by the archived data sheet: the review's
"VREFP = VAVDD ± 25 mV" is not in the S32K39 data sheet we hold, whose window is VDD_HV_A + 0.1 V ≥ VREFH ≥ VDD_HV_A − 1.5 V
and which the FS26's ±1 % LDO-to-VREF matching satisfies with a 50 mV differential; a verifier row and VR-34 (the DC-bound
wording) close it, and the reviewers' routing ECO is the named fallback (F220). **One hardware change: three passives for
the R2R reference filter (≈ ₹0.6/unit). Firmware changed (F217/F218): TI_FW_ID → 0x0A0F0014, then → 0x0A0F0015 with the gap closure and the simulator fixes (FW-38…FW-44, §10k).**

**"Already Fixed" for a gate means the gate exists and is open** — never that the physical qualification is complete.

## Review 1 — five carried-over findings (A20-F01…F05)

| ID | Review said | Class | Verification | Action in rev A.22 |
|---|---|---|---|---|
| A20-F01 | SDADC supply on V5A, references on VREF5; NXP: V_REFP = V_AVDD ± 25 mV; FS26 LDO-to-VREF matching ±1 % ≈ ±50 mV exceeds it — an unclosed margin; candidate ECO: the four reference groups to V5A, VREF5 kept for A9 | **Not Applicable** on the archived data sheet (the ± 25 mV figure is not in it) — closed with a verifier row and a vendor question | S32K39 DS Rev. 3: VDD_SDADC "must be shorted to VDD_HV_A" (note 4 — V5A ✓); VREFH groups on one source (note 8 — VREF5 ✓); "VREFH ≤ VDD_HV_A + 0.1 V" (note 7) and the SAR accuracy condition "VDD_HV_A + 0.1 V ≥ VREFH ≥ VDD_HV_A − 1.5 V"; FS26: VREF_ACC ±0.75 %, VLDOx_VREF_match ±1 % → the differential is ≤ 50 mV | verifier row "MCU VREFH (VREF5) against VDD_HV_A (V5A)" (50 mV vs +100 mV, PASS); **VR-34** asks NXP whether the +100 mV is a DC bound (note 7's "for RF-AC only" wording) — if NXP tightens it, the reviewers' ECO (routing only) is the fallback. **F220** |
| A20-F02 | A9 VREFH_R2R directly on the same VREF5 as the ADC references; NXP requires the R2R reference separately sourced or filtered | **Confirmed** | DS note 8 verbatim: "Isolated VREFH_R2R is required to avoid SDADC performance degradation. If isolated supply cannot be used, then appropriate filtration is needed"; `MCU_PINS` had A9 → VREF5 | VREF5 → RR2R 10 Ω → VR2R (A9) with CR2R1 1 µF + CR2R2 100 nF at the ball (a resistor, not a bead: the 1 µF must stay outside the FS26 VREF output-capacitance window); the SDADC/SAR references stay on VREF5; ERC locks (one source; nothing else on VR2R); verifier row; VR-35 asks NXP for the ladder reference current and its recommended filter; QP-RX-01 measures the SWG with the filter. **F214** |
| A20-F03 | `torque.c` changes i_d in field weakening without recomputing i_q; the witness checks voltage only; eight four-quadrant cases return ≈ 2× the requested torque (e.g. 400 V / 2000 rad/s / 250 A: +195.8 N·m for +100 N·m) | **Confirmed** | lines 196–214: d = min(d, d_fw) and the circle clamp change the vector, iq never recomputed from T = 1.5·p·(ψ + (Ld − Lq)·id)·iq; witness() proves voltage feasibility only; no torque postcondition | joint solve (Opus agent): along the torque curve current, flux and voltage are convex in i_d, so every limit holds on one interval and bisection finds the boundary exactly (a 64-sample scan was tried and rejected: it missed narrow feasible windows in 3 of 3000 random cases); torque reduced by bisection only when nothing on the curve fits; a postcondition before every return (TQ_POSTCOND → DTC_TORQUE_POSTCOND, references zeroed); the status frame carries the APPLIED torque (b4–5) beside the command (b16–17; INV_STATUS 20 B, T-38 / the OEM DBC); the eight cases now TQ_OK at ±100.0000 N·m (112–118 A rms instead of 230 A); a 20 000-case random reference finds no limit violation and ≤ 1.1e-4 N·m over-delivery; 10 new tests (48 checks fail on the old code), 8 mutations caught; 300 tests / 2802 checks / 0 failed in host, −O2, ASan/UBSan; TI_FW_ID 0x0A0F0014. **F217** |
| A20-F04 | the no-LUT MTPA fallback: eight fixed-point steps, no residual; 267.4 A vs the 246.3 A optimum at 200 N·m | **Confirmed** | lines 112–120 | MTPA by bisection on i_d to a 0.01 A interval (≤ 40 steps); a LUT point used only if its recomputed torque is within 0.1 % and fits the limits; 246.306 A and 227.829 A against the bracketed optima (the reviewer's 267.4 / 246.3 A pair reproduces with Lq 1.0 mH / ψ 0.05 Wb, not the F217 motor — both are in the tests); worst-case solve ≈ 65 µs at 320 MHz in the 1 ms task (timing.md). **F218** |
| A20-F05 | the BOM generator prints the fixed "220 kW pk / 800 V SiC" intro under variant headings | **Confirmed** | `bom-gen.mjs` line 85; `docs/bom-igbt4.md` line 3 | the intro follows the SKU (`SKUS[sku].intro`). **F219** |
| md — A.21 improvements retained | UEXD pad 15 → AGND, UB15 pad 17 → DGND, 5-lead regulators, 2-pad crystal, generator refusal | Already Fixed (round 22) | — | none |
| md — "the disposition cannot close this report" | the A.21 disposition did not resolve A20-F01–F05 | Not Applicable (those findings had not been sent to this repository's review process before this round) | — | this disposition |

## Review 2 — A20-N01 closed; the library's CAD version (A21-R01)

| ID | Review said | Class | Verification | Action in rev A.22 |
|---|---|---|---|---|
| A20-N01 closed | §0.2 defines SMDJ7.0A-HRA, 0x0A0F0013, a measured VAL record; older configurations historical | Already Fixed (round 22) | — | §0.2 now names 0x0A0F0015 (the round-23 gap-closure image) |
| bindings valid | NCV4276C 5-lead lands, UEXD/UB15 pads with nets, 2-pad crystal | Already Fixed (round 22) | — | none |
| A21-R01 (review 2) | the KiCad 5 sets ship the same `(footprint … (version 20260206))` files; KiCad 5.1.12's parser accepts `kicad_pcb`/`module` roots only; opening the legacy schematic does not prove its footprints load; make KiCad 10 the supported footprint-enabled handoff | **Confirmed** | `kicad5/traction-native/traction.pretty/TO-252-5_TabPin3.kicad_mod` began "(footprint … (version 20260206)"; the READMEs claimed the shared library for KiCad 5–8 | the library ships only with the KiCad 10 project (KiCad 10.0.6 or later, stated in `kicad/traction/README.txt`, the library README, the handoff and the README); the KiCad 5 / EasyEDA sets carry footprint names and no library; no version field was rewritten. **F216** |
| pad coverage ≠ physical qualification | the manifest's confirmation items remain (module hole, Samtec details, 4XX can) | Already Fixed (gates exist) | handoff §1a | none |

## Review 3 — JSWD (A21-R01), the supported version (A21-R02), a stale paragraph (A21-N01)

| ID | Review said | Class | Verification | Action in rev A.22 |
|---|---|---|---|---|
| A21-R01 (review 3, MAJOR) | JSWD = Samtec FTSH-105-01-L-DV-K, a vertical SURFACE-MOUNT header; the bound land is the through-hole 1.27 mm pattern (1.0 × 1.0 mm pads, 0.65 mm drills, rows 1.27 mm apart) where Samtec's land is 0.74 × 2.79 mm pads on rows 4.064 mm apart; pad-number coverage cannot see it | **Confirmed** | `parts-db.mjs` JSWD → FTSH-105-01-L-DV-K, `footprints.mjs` HDR2x5-1.27 → PinHeader_2x05_P1.27mm_Vertical, the emitted F2; the Samtec print (fetched, archived as `docs/datasheets/Samtec-FTSH-DV.pdf`): -DV = double vertical SMT, pin 01 bottom-left, odd/even across the rows | the land `Samtec_FTSH-105-01-L-DV-K` drawn and bound; a mounting-technology lock in the library generator (ordering codes that state smd/THT must match the footprint's attribute). **F215** |
| A21-R02 | define the supported KiCad version; the legacy sets carry a newer-format library | **Confirmed** (the same item as review 2's A21-R01) | — | **F216** |
| A21-N01 | the handoff still quoted "MCU remains explicitly symbolic — bind at layout" beside §1a | **Confirmed** | `docs/layout-handoff.md` §2 | replaced by the current binding statement. **F220** |
| corrections retained | five-lead regulators, thermal pads, crystal, QLVS tab pad 4 | Already Fixed (round 22) | — | none |
| firmware regression | trees identical to A.20; 56 cases / 40 008 assertions | Not Applicable (confirmation) — superseded this round by F217/F218 | — | the resolver code is untouched; `torque.c` changed |

## Self-found — the closed-loop simulator bridge and the update work (F221–F232)

The simulator bridge built for Traction Tool (`tool/bridge/`: the real firmware modules compiled against a plant with dyno, link,
pack, contactors and thermal models, driven by a bench VCU/BMS) ran the firmware closed-loop for the first time and found
defects the unit tests had not; the FW-38 and FW-42 agents found two more while reading the code they touched. Every one was
fixed by an Opus agent with a test that failed on the old code and passed on the new, plus mutations. All are **Confirmed**.

| ID | Found | Class | Verification | Action in rev A.22 |
|---|---|---|---|---|
| F221 | the module-temperature rate check judged one ADC-code step per 1 ms sample against 20 °C/s: three in a row latched DTC_TEMP_MODULE in normal running above ≈ 70 °C and left derating at the continuous rating | **Confirmed** (bridge, thermal scenario) | 13 checks failed on the old code | rate judged over cal_temp_rate_win_ms (200 ms) past a deadband of cal_temp_rate_db_codes (4); derating 0.548 → 0 between 96 and 114 °C with the sensors valid |
| F222 | field weakening decided on a fixed speed (n_x): at a 750 V link between ≈ 6 900 rpm and n_x with zero torque the bridge idled while the back-EMF exceeded the link — the free-wheeling diodes braked at 50–65 N·m and pushed ≈ 40 kW into the pack uncommanded | **Confirmed** (bridge, −23 A on the plant) | scenario in `check.c` | fw_needed from the measured link voltage with cal_fw_emf_margin_frac (5 %); n_x stays the floor; the same point draws +0.5 A |
| F223 | fresh torque commands were taken as steps (only the FW-11 ramp-down existed): +200 → −150 N·m at 10 000 rpm tripped over-current and LS-ASC | **Confirmed** (bridge) | 4 mutations | commands slew at cal_torque_slew_nm_s (2 000 N·m/s) both ways; fault paths still zero torque at once |
| F224 | six DTCs declared and never set (DESAT_PENDING_BOOT, ISNS_OFFSET, TEMP_BOARD, TEMP_MOTOR, OVERTEMP, SENSOR_SELFTEST); a DESAT recorded in the previous key cycle blocked arming with nothing naming it after power-up | **Confirmed** (bridge) | 13 checks failed on the old code | each set where detected; the recorded DESAT re-raised at power-up |
| F225 | every ASC entry at speed also latched DTC_OVERCURRENT (the short-circuit transient peaks 730–900 A against 601 A) | **Confirmed** as a misclassification — the low sides stay on throughout (host safety-chain model, sample by sample) | 4 mutations | inside cal_asc_oc_window_ms (20 ms) of an ASC entry: DTC_ASC_OC_TRANSIENT, information, compare re-armed after the window |
| F226 | after a resolver fault the held speed expired after 200 ms and became "unknown", so §6 chose ASC even at 2 000 rpm | **Confirmed** (bridge) | 3 mutations | the last speed kept as a bound \|n_last\| + cal_speed_accel_max_rpm_s·t (2 500 rpm/s), unknown only past n_max; 2 000 rpm stays SPO, 7 000 rpm reaches ASC at 434 ms; §6 rule written |
| F227 | the resolver demodulator's weighting places each block's effective time 7.698 µs after the mean sample time while the angle was dated at the mean: 1.85° el at 10 000 rpm (≈ 6 % torque), on the target too | **Confirmed** (first reported as a host artefact) | 19 checks failed on the old code | each block dated at the weighting centre; T-37 states what cal_rslv_latency_us then means |
| F228 | FW-10's stated 3-frame amplitude debounce was bypassed: one bad resolver frame dropped the resolver for the key cycle | **Confirmed** (FW-42 agent) | 71 checks failed on the old code | one bad frame is one count and is kept out of the observer; latch after 3 in a row |
| F229 | the NVM slot CRC was blind to a record ending in its own CRC-32 (CRC(data ‖ CRC(data)) is constant): a torn calibration or arm-validation write read back as the record from two writes earlier (anti-rollback counter included) | **Confirmed** (FW-38 agent) | 3 000 random tears | CRC-32C over the slot header and the payload; old slots readable and replaced on the next write |
| F230 | all 32 NVM slots in use after the round's records | **Confirmed** | static assert | 64 slots (32 KiB, T-25) |
| F231 | the signed update moved 5 bytes per block on the single-frame transport (≈ 210 s/MB) | **Confirmed** | 5 mutations | programming services over ISO-TP, 4 095-byte blocks: 1 MB in 4.6 s simulated |
| F232 | FW-21 rows still called the update path out of scope; the bridge assumed single-frame UDS replies and had no vehicle speed; the tool's texts called the DTC clear a simulator hook | **Confirmed** (documents and tooling) | — | rows point to FW-38; the bridge speaks segmented ISO-TP, the periodic stream, the real 0x14 and sends vehicle speed; 80 DTCs described in the exports |

## The VESC program (on the user's instruction)

"Download the VESC firmware, check whether we have all the features, controls and everything end to end, make sure ours is
ahead, and build a GUI like VESC Tool." Dispositioned like a review: every claim of the matrix cites file and line on both
sides; the tool decision was checked on this machine rather than assumed.

| ID | Item | Class | Verification | Action in rev A.22 |
|---|---|---|---|---|
| VESC-1 | the VESC firmware (6.00 and master 7.01 dev) and VESC Tool (7.01) sources | Not Applicable as code (GPL-3.0: nothing linked or copied into the firmware or the tool) | `docs/reference/vesc/README.md`, `LICENSE-vesc_tool.txt` | archived as reference tarballs under `docs/reference/vesc/` (the unpacked trees git-ignored) |
| VESC-2 | feature and control matrix | Confirmed in part: ten ranked gaps; the rest PAR, OURS AHEAD or N/A for a traction inverter (sensorless, HFI, hobby inputs, scripting — the last must never be added) | `docs/firmware-vs-vesc.md` (884 lines, two-sided) | gaps 1–7 and 9 closed this round (below); gap 8 (saturation-dependent Ld/Lq in the solve) and gap 10 (ripple compensation) stay open — both need dyno maps the program does not have |
| VESC-3 | gap 1: no field-update path (FW-21 said "out of scope") | Improvement Recommended → implemented | FW-38, T-44…T-50 | signed image container (Ed25519, SHA-256, anti-rollback, A/B boot slots), UDS 0x34/0x36/0x37/0x31/0x11 over ISO-TP; `tools/sign-image.mjs`; the release key never in the repository (test key only); IR-43 asks the OEM for the field re-validation policy |
| VESC-4 | gap 2: motor self-commissioning | Improvement Recommended → implemented, interlocked | FW-39, T-51…T-54, `check.c` (Rs within 2 % of the plant) | Rs by two DC levels, Ld/Lq by current-reference injection on a DC bias, ψ / zero / direction dyno-driven; entry only through SecurityAccess + rig attestation + ARMED_ZERO_TORQUE + the VCU's vehicle speed valid and zero (new VCU_CMD b4[5], b6–7); results staged and sealed through FW-20 |
| VESC-5 | gaps 3 and 6: no DTC export, two diagnostic services | Improvement Recommended → implemented | FW-40, T-55/T-56 | UDS 0x19 (01/02/0A/04), 0x22 DIDs, 0x2A periodic, 0x14 gated like FW-32, ISO 15765-2 over CAN-FD; `tools/dtc-table.mjs` → 80 DTCs |
| VESC-6 | gap 4: no waveform capture | Improvement Recommended → implemented | FW-41, T-43 | 2048 × 32 B ring after the current ISR, triggered by DTC / §6 row / command / level; UDS 22 FD40/FD41, 2E FD41, 31 01 F041/F042; `tools/capture-decode.mjs` |
| VESC-7 | gap 5: no overspeed check | **Confirmed** gap → implemented | FW-42 | warn 1.00×, trip 1.05× n_max with hysteresis and a 10-sample debounce; §6 rows; DTC_OVERSPEED |
| VESC-8 | gap 7: no run-time statistics | Improvement Recommended → implemented | FW-43 | NV_REC_RUNTIME saved every 600 s and at shutdown; DID 0xFE43 |
| VESC-9 | gap 9: offset checked, never adopted | Improvement Recommended → implemented | FW-44 | key-on offset adoption ≤ 2 mV per key cycle, bound to the CAL CRC |
| VESC-10 | the tool's stack (Bun proposed, Electron, Electrobun checked, Qt named) | Improvement Recommended; decided by the user for Qt after the check | `docs/tool-stack-decision.md` | Traction Tool: Qt 6 / C++17 Widgets + Qwt + Qt SerialBus, offline, packaged (.dmg); no GPL chart module; the earlier Bun UI deleted, its C bridge and protocol exports kept |
| VESC-11 | Traction Tool itself | delivered (the image transfer of the Firmware page was added in the extension, EXT-7 — the first version of this row had claimed it ahead of the fact) | `tool/qt/` (6 QtTest suites incl. an offscreen GUI smoke test against the bridge), `tool/bridge/` (`make check`), `tool/protocol/` (`generate.mjs --check`) | pages Dashboard, Live plots, Controls (hold-to-apply torque, typed confirmations), Commissioning (FW-39), Parameters, Faults & alerts, Analysis, Terminal, Firmware (FW-38: the signed update over both transports); transports: simulator bridge, log replay, CAN (QCanBus; proved over the virtual bus against the firmware, a mock device for the edge cases); service key as an interlock, not a security boundary |
| VESC-12 | what the tool cannot claim yet | open | `tool/qt/NOTICES.md`, `tool/README.md` | a CAN adapter on the bench is the gate for the transport (codec mock-tested); two licence items before external distribution; the Kvaser plugin is absent from the Homebrew Qt SerialBus build (PEAK, SocketCAN, virtual present) |

## Extension — "why is this not done, can't we implement it?" (27 September 2026)

The user asked why the round's closing report still listed open items and whether they could be implemented. Each one was
taken as a review finding: what genuinely needs hardware stays a gate; everything software can do was done, by Opus agents
under the fail-before/pass-after rule and verified by the manager. Doing so found seven more defects (F233–F239).

| ID | The open item said | Class | Verification | Action in rev A.22 |
|---|---|---|---|---|
| EXT-1 | every firmware change is host-proved only; T-05…T-07 and the dyno remain the gates | Improvement Recommended → done as far as software can | firmware/qemu.mk, T-59; F235–F237 | the whole suite built with arm-none-eabi-gcc for the Cortex-M7 (single-precision FPU, hard ABI, -O2, newlib) and run under QEMU's mps2-an500: identical counts to the host (421 / 4937 / 0 on the final tree  1 412 s); the target compiler caught test-code defects clang had accepted (F235); the host now rounds like the target (F237); the 64-bit software divide is out of every ISR path with a link-time guard (F236); the M7 footprint known (text ≈ 90 KB, bss ≈ 128 KB before libraries, RTD and stacks). Still silicon: peripherals, the NXP RTD, timing (T-36), the lockstep core, T-05…T-07 |
| EXT-2 | the commissioning smoke test exercised the Rs routine only | **Confirmed** → done | tool/qt tst_gui_smoke | L_d/L_q (LK) within 0.2 % of the plant, ψ within 0.1 %, the zero within 1 mrad, DF/DR direction verdicts, a beyond-band re-run, the commit, the reboot, the heartbeat abort (0.43 s, DTC_MC_ABORTED, armed idle, no torque) — and the bridge defect F233 on the way |
| EXT-3 | the CAN transport is untested on hardware | Improvement Recommended → done as far as software can | tool/qt tst_can_bridge, tst_update; F234, F238, F239 | proved against the real firmware over a real QCanBusDevice (Qt's virtualcan) through a gateway onto the simulator: the transport had no ISO-TP reassembly at all (F234); torque, the periodic stream, multi-frame DTC lists, a corrupted frame, the service-key refusal; then the FW-38 update end to end over both transports (16 KB in 4 blocks, BS 4 / STmin 0, 0.4 s), which found F238 in the firmware and F239 in the bridge. Still hardware: a physical adapter (the PEAK plugin is the only difference) |
| EXT-4 | matrix gaps 8 and 10 need dyno maps | Confirmed — the maps do; the capability did not | FW-45, FW-46 (contract §10l/§10m), 41 mutations | saturation-dependent inductance tables in the record (layout 4; a flat table is bit-identical to the scalar solve over 20 000 cases; −9.8 % → −0.1 % torque error at 80 % current on the saturating host plant; gain scheduling: phase margin 12° → 61°) with FW-39's routine at six bias points; a 36-point cogging feed-forward table (−83 % ripple at 100 rpm on the cogging host plant) loaded by DID 0xFD46 under FW-39's interlocks. The dyno measurements are T-57 / T-58 |
| EXT-5 | two licence items before external distribution | **Confirmed** → done | tool/qt/scripts/licences.mjs, NOTICES.md | the corresponding source is shipped (the exact Qt submodule and Qwt archives Homebrew built from, checksums from the installed formulae, the formula recipes, a manifest → TractionTool-<version>-corresponding-source.zip beside the image; no written offer owed); Qt's 87 third-party attributions with 88 licence texts generated from those sources; the bundled-library table regenerated from the staged bundle at every packaging run (an unattributed library fails the package); Help ▸ Licences… in the application |
| EXT-6 | the release signing key must never enter the repository | Not Applicable as a gap — a rule, kept | verify.c (a TEST key on the host only, -DTI_FW38_PUBKEY or the OTP/HSE copy on the target) | what was missing was the marker: DID 0xFD23 (root kind + key id, the TEST key's id pinned in the suite), the simulator's hello, the tool's banner, QP-EOL-13 refuses kind 1. Deliberately not a DTC (an always-active information DTC fails every "no active DTC" precondition on development units) |
| EXT-7 | the tool's Firmware page (self-found while doing EXT-3: "Select image… (not available)", FW-21 text; the first version of row VESC-11 had claimed FW-38 in the tool ahead of the fact) | **Confirmed** → done | tool/qt tst_update over both transports | the .tifw container parsed and checked against the running image (SKU, TI_FW_ID, security version before anything is sent; over CAN through DID 0xFD24), a typed confirmation, unlock, 0x34 / 0x36 (ISO-TP transmit with the firmware's BS/STmin) / 0x37 / verify / activate / reset, progress, every NRC named, no silent retry; rollback and a corrupted signature refused at verification |
| EXT-8 | the tool did not use FW-45/FW-46, did not show the root of trust, and pages could send UDS at the same moment (the firmware keeps one pending response) | **Confirmed** → in the same extension | tool/qt (the follow-up agent's tests) | the L_d/L_q map sweep at six bias points (every point VALID within 0.11 % of the plant; the commit was refused by the record's strict never-rising rule on a non-saturating machine → a 2 % measurement tolerance in the firmware; the committed map read back after the key cycle through the new DID 0xFD26 within 0.1 %), the ripple-map import (CSV → 36 points → DID 0xFD46 → read-back equal → commit → the active table after a reboot; over CAN with ψ and the pole pairs from the new DID 0xFD25), the root-of-trust banner (kind 1: "TEST root of trust — development unit, not for delivery") and the DID 0xFD24 rollback pre-check over CAN (refused before any 0x34), and one outstanding UDS request per transport (a queue in the Session; two overlapping requests answered in order, the test fails with the queue disabled) |

## What stays open (numbered)

Unchanged from round 22: **QP-RX-04 step 2** (≥ 8 A clearing) and **VR-16**; **QP-RX-04 step 2b / QP-MA-11** with **IR-42**
and **VR-17**; **QP-RX-05 / VR-33**; **IR-16**; target measurements T-40 / T-41 / T-42; the pre-layout decisions of
`layout-handoff.md` §6. New this round: **VR-34** (NXP: the DC bound on VREFH − VDD_HV_A), **VR-35** (NXP: the R2R reference
current and filter), and the torque solver's silicon proof (the new tests run on the host and at −O2/ASan; the target build
runs them through `make target-check`). From the VESC program: **IR-43** (the OEM's field re-validation policy after a
signed update), the release signing key's custody (never in the repository; the test key signs the host images), FW-39's rig
attestation (an operator statement — the routine's own 1 ms checks bound what a wrong one can do, the dyno run is the proof),
the tool's CAN transport on a physical adapter (the code path is proved over the virtual bus; the PEAK plugin is the only
difference), the host card model's power-up ASC without gate supply (a host artefact, documented in the bridge), the
anti-rollback check that runs at verification rather than at RequestDownload (the staging region is written before a lower
security version is refused; the running image and the boot record are never touched), and the application image, which is
signed ad hoc and not notarised. The licence items are closed (EXT-5). The twelve self-found fixes are
proved on the host and in the sanitised build; T-05…T-07 and the dyno remain the gates for every firmware change.

## Readiness (after this round)

| Area | Level |
|---|---|
| Power-off inspection / continuity / schematic-to-component reconciliation | READY |
| Handoff to PCB layout | READY on KiCad 10.0.6+ (every symbol bound; the SWD land corrected; the mounting technology locked) |
| Torque-control release | the round-23 solver is proved on the host and in the sanitised build; the target run (T-05…T-07, `make target-check`) and the dyno remain the gates |
| Target-controlled motor operation / HV / DVT / production | per `qualification-plan.md` — unchanged |
| Firmware image 0x0A0F0015 (FW-37…FW-44, F221–F232) | host-proved: 402 tests / 4707 checks / 0 failed in three build flavours, 61 target markers, the closed-loop bridge check; T-05…T-07 and the dyno remain the gates; FW-39 needs the rig |
| Traction Tool | usable today against the simulator, log replay and any QCanBus adapter (the transport, ISO-TP both ways, the update and the commissioning are proved against the firmware over the virtual bus); a physical adapter on the bench is the remaining gate; the licence items are closed and the package carries its corresponding source |
| Firmware on the target ISA | the suite passes on the Cortex-M7 under QEMU with the target compiler (T-59); silicon items T-05…T-07, T-36 and the peripherals remain |

## Counts and deliverables

Register F214–F239 (26 findings, all fixed). Hardware: three passives (≈ ₹0.6/unit). Firmware: TI_FW_ID 0x0A0F0015, CAL layout 4
(190 fields / 91 rows), 80 DTCs, 421 tests / 4937 checks / 0 failed (host, −O2, ASan/UBSan) and the same suite on the Cortex-M7
under QEMU, `make target-check` 63 markers,
the bridge's `make check` PASS, protocol exports current (`generate.mjs --check`). Tool: Qt 6 suites all passing, a packaged
.dmg. Pipeline: ERC 980 · verify 158/18/0 · sim 23/6/0 · Marine 87/23/0 · KiCad proof PASS. See the README round-23 row and
`review-A22-disposition.csv` alongside (every table above, one row per line).
