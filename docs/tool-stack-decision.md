# Traction Tool — the stack decision (round 23, 2026-09-26; decided for Qt by the user the same day)

The user proposed Bun as an alternative to Electron and asked for compiled software, named Qt as another good option, and asked
for a checked decision rather than an assumption. This note records what was checked and why the tool is built the way it is.

## What was checked on this machine and in this repository

| Fact | Evidence |
|---|---|
| Bun 1.3.12 is installed; `bun build --compile` produces a standalone executable; `bun:ffi` (dlopen of C libraries) works | `bun --version`; `bun build --help` lists `--compile`; `bun -e 'import { dlopen } from "bun:ffi"'` |
| The repository's tooling is already JavaScript/TypeScript (19 generator and verifier scripts under `calculations/`, tscircuit boards) plus C11 firmware | `package.json`, `calculations/*.mjs`, `firmware/` |
| No Qt SDK is installed (no `qmake`); CMake is | `which qmake cmake` |
| VESC Tool is Qt Widgets + QML with a mobile build — the reference the user has in mind | `docs/reference/vesc/src/vesc_tool-master-dc53c65/vesc_tool.pro` (`mobile/`, `widgets/`, `res_qml.qrc`) |
| The tool's hardware I/O is CAN-FD (the vehicle interface of `docs/firmware-contract.md`) through a USB adapter (PEAK PCAN-USB FD, Kvaser) or SocketCAN — all with C APIs | the contract; the adapters' vendor SDKs |

## The options, honestly

| Option | For | Against | Verdict |
|---|---|---|---|
| **Bun: server + bundled web UI, shipped as one compiled executable** | one runtime the team already uses; `bun build --compile` gives a single binary per platform with the UI embedded; the UI is web technology (fast charts, a real design system, remote/second-screen use at an EOL station for free); hardware I/O lives in the server process through `bun:ffi` (PCAN-Basic, CANlib, SocketCAN via libc) — no browser sandbox problem; `bun test` for the codec, alerts, replay and an end-to-end smoke test | the UI opens in the system browser, not a native window (a Tauri shell can wrap the same UI later if a native window is ever required); FFI bindings for each adapter are ours to write and test | **chosen** |
| Electron | mature native window; the same web UI | 150+ MB of bundled Chromium and a second runtime (Node) beside Bun for no gain; heavier installers; slower start | not chosen — Bun covers what Electron would give us here |
| Qt / QML (C++) | native, compiled, first-class serial and CAN support (`QCanBus` plugins for PEAK, Kvaser, SocketCAN), instrument-grade widgets, mobile builds | a third toolchain for this team (C++ + a 1–2 GB Qt SDK per platform); the charting modules a tool like this needs (Qt Charts, QCustomPlot) are GPL/commercial, so a proprietary product must buy a commercial licence or write its own plotting; slower iteration and harder automated GUI tests; VESC Tool chose it for Android/iOS, which we do not need | not chosen — its real advantages (adapter plugins, mobile) do not outweigh the cost for this team and product |
| Tauri (Rust core + system webview) | small native-window binary; the same web UI | a Rust toolchain and per-platform webview differences; only worth it if a native window is a requirement | kept as the optional shell for later |
| **Electrobun** (checked on the user's prompt: npm `electrobun` 2.0.1 stable, 2.0.2-beta.31 published 2026-09-25, MIT, ≈ 49 k weekly downloads; official platforms macOS 14+, Windows 11+, Ubuntu 24.04+; system webview; Zstd self-extracting bundles, code signing, a bsdiff updater) | a native desktop window with our TypeScript stack — the offline, installable "tool like VESC Tool" the user wants — without Chromium or Rust; installers and an updater built in | its main-process runtime is now its own JSC-based "Cottontail" ("Node.js and Bun-compatible where it counts"), so `bun:ffi` for CAN adapters is not guaranteed there; a native build chain (the Hutch CLI, cmake, Xcode CLT / VS Build Tools); a single maintainer ("no expectation that I will review, respond to, or merge") | **chosen as the desktop shell only**: it launches the compiled Bun core as a sidecar and hosts the UI in a native window, so hardware I/O and every Bun API stay in the core and the shell is replaceable (Tauri, or a plain browser) if the project stalls |

## The user's decision: Qt 6 (mature platform)

After the analysis above the user chose maturity: **Traction Tool is a Qt 6 / C++17 Widgets application** (`tool/qt/`,
CMake), the path VESC Tool itself took for its desktop program. What makes it work for this product, checked on this machine:

| Item | Fact |
|---|---|
| Toolchain | Apple clang 17, CMake 4.1.2 present; Qt 6.11.2 via Homebrew (`qt`, or the component formulae `qtbase` / `qtserialbus` / `qtsvg` / `qttools`) |
| Licence | Qt under **LGPL-3.0** with dynamic linking (`tool/qt/NOTICES.md` carries the obligations); **Qt Charts, Qt Graphs, Qt Data Visualization and QCustomPlot are GPL/commercial and are not used** |
| Plotting | **Qwt 6.3.0** (LGPL-2.1 with the Qwt exception; Homebrew `qwt`) — or our own QPainter plot widget if Qwt cannot be installed |
| CAN | **Qt SerialBus / QCanBus** with the socketcan, peakcan, kvaser, vectorcan and virtualcan plugins — the adapter is selected at run time; a mock device for tests |
| Simulator | the C bridge under `tool/bridge/` (JSON lines on stdio, `tool/PROTOCOL.md`, exports in `tool/protocol/`) run as a QProcess — the part of the earlier Bun work that is language-neutral and kept |
| Offline | no network at run time; packaged with `macdeployqt` (.app/.dmg), `windeployqt` / `linuxdeploy` on the other platforms |
| Tests | QtTest units (codec, parameters, alerts, replay, bridge parser) and an offscreen GUI smoke test (`QT_QPA_PLATFORM=offscreen`) |

The Bun/Electrobun analysis above stays as the record of what was weighed; nothing from the web UI was kept.

## The earlier (superseded) decision

Traction Tool is two layers. **The core** is a Bun + TypeScript application: a server that owns the transports (the firmware
host simulator, log replay, and a CAN adapter interface with `bun:ffi` bindings) and a bundled web UI, packaged with
`bun build --compile` into one executable that serves on localhost and opens the browser (headless mode for EOL use) — offline,
no network access at run time. **The desktop shell** is an Electrobun app (`tool/desktop/`) that starts the compiled core as a
sidecar and hosts the same UI in a native window with an installer and an updater — the offline desktop tool the user asked
for, on macOS 14+, Windows 11+ and Ubuntu 24.04+ as Electrobun supports them. The shell owns nothing that matters: if
Electrobun cannot be built on a platform or the project stalls, the core runs in a browser unchanged, or a Tauri shell wraps it. It is original work — VESC Tool (GPL-3.0) was read
for feature ideas only; no code, QML, images or text were taken (`docs/reference/vesc/README.md`). If a native desktop window
becomes a requirement, the same UI is wrapped in a Tauri shell with the Bun server as its sidecar; nothing in the UI or the
server changes for that.

What this decision does not claim: that the tool is the safety element (the firmware is — the tool's controls are gated by
the firmware's own arming preconditions and its commands go through the same CAN-FD contract as the vehicle's), or that the
adapter bindings are proven — they are an interface with a mock and unit-tested codecs until an adapter is on the bench.
