# Reference: VESC firmware and VESC Tool (third-party, GPL-3.0)

Archived on 2026-09-26 on the user's instruction, for the feature and control comparison of round 23 (`docs/review-A22-disposition.md`,
`docs/firmware-vs-vesc.md`) and as design reference for `tool/` (Traction Tool). **Nothing here is linked into, copied into or
required by our firmware or our tool**: both are original work under this repository's own terms; the VESC sources are
GPL-3.0 (the VESC Tool licence text is beside the archives as LICENSE-vesc_tool.txt; the firmware repository carries no root licence file — every source file's header states GPL-3.0-or-later) and are kept only as reference copies.

| Archive | Source | Version | Commit date | sha256 (first 16) |
|---|---|---|---|---|
| `bldc-6.00.tar.gz` | https://github.com/vedderb/bldc, tag `6.00` (the last tagged release) | FW 6.00 | tag | b31746e969a1a99d |
| `bldc-master-4fd8279.tar.gz` | https://github.com/vedderb/bldc, `master` at 4fd8279 | FW 7.01 (dev; `conf_general.h` FW_VERSION 7.01) | 2026-09-17 | 13d4344c87359e95 |
| `vesc_tool-master-dc53c65.tar.gz` | https://github.com/vedderb/vesc_tool, `master` at dc53c65 (the repository carries no tags) | VESC Tool 7.01 (`vesc_tool.pro` VT_VERSION) | 2026-09-04 | 7d35773a546ee1c3 |

The archives are `git archive` exports of the source trees (no history). `src/` holds the extracted working copies for reading
and is not committed (`.gitignore`); re-extract with `for f in *.tar.gz; do tar -xzf "$f" -C src; done`.
