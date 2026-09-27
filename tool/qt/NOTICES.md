# Traction Tool — third-party notices and licence obligations

Traction Tool is proprietary software of JoulePoint. It is built on the open-source components below, all of them
**dynamically linked** and shipped as separate, replaceable files inside `Traction Tool.app/Contents/Frameworks` (and
`PlugIns`). Nothing is statically linked; no source file of these components is part of this repository. The full
licence texts ship with the application (`Contents/Resources/licenses/`, and `Licenses/` on the disk image); this
directory holds them as `licenses/`.

This file records what the macOS package bundles as built here (Homebrew bottles, Apple Silicon, 2026-09-26) and the
obligations that follow. It is engineering guidance for the release, not legal advice: have it reviewed before an
external distribution.

## Qt 6.11.2 — LGPL-3.0-only (chosen from its LGPL-3.0 / GPL-2.0 / GPL-3.0 options)

Modules: Qt Core, Gui, Widgets, Network, DBus, Concurrent, OpenGL, OpenGLWidgets, PrintSupport, Svg, SerialBus,
SerialPort (the last five on dependency of Qwt and Qt SerialBus), with their plugins (cocoa, macstyle, svg icon
engine, gif/ico/jpeg/svg image formats, the canbus plugins, tls, networkinformation). Qt Charts, Qt Graphs, Qt Data
Visualization and every other GPL-only or commercial-only Qt module are **not used**.

Obligations under LGPL-3.0 (sections 4 and 6), as met by this package:
- **Notice**: this file and the About box state that Qt is used and under which licence; the LGPL-3.0 text and the
  GPL-3.0 it incorporates ship as `licenses/LGPL-3.0.txt` and `licenses/GPL-3.0.txt`.
- **Replaceability (relinking)**: Qt is linked dynamically as separate frameworks, so a user can replace them with a
  modified, interface-compatible Qt 6.11 build (drop-in replacement of the frameworks in `Contents/Frameworks`; the
  executable finds them through `@executable_path/../Frameworks`). The ad-hoc code signature must then be re-applied
  (`codesign --force --deep --sign - "Traction Tool.app"`) — this is the only step macOS adds, and it is permitted.
- **No restriction on reverse engineering** for debugging such modifications: the product terms must not forbid it.
- **Corresponding source**: the Qt frameworks are the unmodified upstream 6.11.2 sources as built by Homebrew. The
  distributor must either ship that source or give a written offer (valid three years) to provide it; the exact
  sources are `qtbase-everywhere-src-6.11.2`, `qtsvg-…`, `qtserialbus-…`, `qtserialport-…` from
  https://download.qt.io/official_releases/qt/6.11/6.11.2/submodules/ (and the Homebrew formulae's build recipes).
  **Decision: the source is shipped, not offered.** `scripts/licences.mjs sources` (run by the `package_dmg` target)
  downloads exactly those archives — url and sha256 read from the installed Homebrew formulae, so they are the sources the
  bundled frameworks were built from — verifies them, adds the formula files as the build recipes and a MANIFEST.md with
  the checksums, and the release ships them next to the image as `TractionTool-<version>-corresponding-source.zip`. No
  three-year written offer is then owed (that alternative stays available: ship only the MANIFEST and the offer text).
- Qt itself contains third-party code under permissive licences (its `qt_attribution.json` files); the list for the
  modules above is part of the Qt source and must accompany a release. **Done by the same script:** every
  `qt_attribution.json` of the four modules is read from the downloaded sources and written to `licenses/QT-THIRD-PARTY.md`
  (component, version, licence, which Qt parts it is in) with the licence texts copied into `licenses/third-party/qt/`;
  both ship inside the bundle (Contents/Resources/licenses) and beside it.

## Qwt 6.3.0 — LGPL-2.1-only WITH Qwt-exception-1.0

Plotting library (`qwt.framework`), dynamically linked. The Qwt exception permits static linking and states that
subclassing Qwt widgets does not create a derivative work; this package links dynamically anyway. The exception asks
that programs identify their use of Qwt: **Traction Tool is based in part on the work of the Qwt project
(https://qwt.sourceforge.io).** Licence text: `licenses/Qwt-COPYING.txt` (the Qwt exception followed by LGPL-2.1).
Source: https://sourceforge.net/projects/qwt/files/qwt/6.3.0/ (unmodified).

## Libraries bundled because Homebrew's Qt links them

All dynamically linked, unmodified Homebrew builds; licence texts in `licenses/third-party/` (copied from each library's
Homebrew keg by `scripts/licences.mjs table`, which regenerates this table from the staged bundle at every packaging run and
refuses to package a library it cannot attribute), and `licenses/LGPL-2.1.txt` for the LGPL ones.

<!-- bundled-libraries:begin -->
| Library (files in `Contents/Frameworks`) | Version | Licence (SPDX) | Text |
|---|---|---|---|
| BLAKE2 (`libb2`) | 0.98.1 | CC0-1.0 | `third-party/libb2-COPYING` |
| Brotli (`libbrotlicommon`, `libbrotlidec`) | 1.2.0 | MIT | `third-party/brotli-LICENSE` |
| D-Bus (`libdbus-1`) | 1.16.2 | AFL-2.1 OR GPL-2.0-or-later (AFL-2.1 chosen) | `third-party/dbus-COPYING` |
| double-conversion | 3.4.0 | BSD-3-Clause | `third-party/double-conversion-LICENSE` |
| FreeType (`libfreetype`) | 2.14.3 | FTL (chosen) OR GPL-2.0 | `third-party/freetype-LICENSE.TXT` |
| gettext runtime (`libintl`) | 1.0 | LGPL-2.1-or-later (the library) | `LGPL-2.1.txt` |
| GLib (`libglib`, `libgio`, `libgobject`, `libgmodule`, `libgthread`) | 2.90.0 | LGPL-2.1-or-later | `LGPL-2.1.txt` |
| Graphite2 (`libgraphite2`) | 1.3.15 | MIT OR MPL-2.0 OR LGPL-2.1+ OR GPL-2.0+ (MIT chosen) | `third-party/graphite2-LICENSE` |
| HarfBuzz (`libharfbuzz`) | 14.5.0 | MIT | `third-party/harfbuzz-COPYING` |
| ICU (`libicuuc`, `libicui18n`, `libicudata`) | 78.3 | Unicode-3.0 | `third-party/icu4c-LICENSE` |
| libjpeg-turbo (`libjpeg`) | 3.2.0 | IJG AND BSD-3-Clause AND Zlib | `third-party/jpeg-turbo-LICENSE.md` |
| libpng (`libpng16`) | 1.6.58 | libpng-2.0 | `third-party/libpng-LICENSE` |
| MD4C (`libmd4c`) | 0.6.0 | MIT | `third-party/md4c-LICENSE.md` |
| OpenSSL (`libssl`, `libcrypto`) | 3.6.4 | Apache-2.0 | `third-party/openssl-LICENSE.txt` |
| PCRE2 (`libpcre2-8`, `libpcre2-16`) | 10.48 | BSD-3-Clause | `third-party/pcre2-LICENCE.md` |
| Zstandard (`libzstd`) | 1.5.7 | BSD-3-Clause (chosen) OR GPL-2.0 | `third-party/zstd-LICENSE` |

Table generated by `scripts/licences.mjs table` from the staged bundle (25 dylibs, 13 frameworks: QtConcurrent, QtCore, QtDBus, QtGui, QtNetwork, QtOpenGL, QtOpenGLWidgets, QtPrintSupport, QtSerialBus, QtSerialPort, QtSvg, QtWidgets, qwt).
<!-- bundled-libraries:end -->

The LGPL obligations above (notice, replaceability, source or offer) apply to GLib and libintl as they do to Qt; their
sources are the upstream releases named in the table.

## Not bundled, not used

No network service is contacted at run time (no update check, no telemetry, no downloaded fonts or assets); Qt Network
is present only as a dependency of Qt SerialBus. The CAN adapter vendor drivers (PEAK, Vector, …) are not shipped:
Qt SerialBus loads them at run time if the user has installed them, under their own licences.

## Windows and Linux packages

`windeployqt` and `linuxdeploy` bundle a different set of libraries (Windows: Qt, Qwt, the MSVC runtime, ICU as
applicable; Linux: Qt, Qwt and whatever the build host links — often none of the above if the system provides them).
Run `scripts/licences.mjs table --app <bundle>` against that package before distributing either (extend its LIBS map for
libraries it does not know); the Qt and Qwt obligations are the same, and `scripts/licences.mjs sources` is platform-neutral.
