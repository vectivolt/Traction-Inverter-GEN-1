EASYEDA-IMPORT VARIANT (round 15, A13-R05). The symbol library is pre-mirrored about Y because EasyEDA's
KiCad-legacy importer places pins at (ux+px, uy+py) and ignores the "1 0 0 -1" orientation matrix.
Do NOT open this folder in native KiCad: use ../traction-native/ (same sheets, un-mirrored library).
Both variants are verified pin-by-pin with their consumer's placement rule (calculations/kicad5-verify.mjs).

FOOTPRINTS (round 22, F210; round 23): every symbol's F2 field names its footprint as "traction:<name>". The library itself
(kicad/traction/traction.pretty — patterns copied verbatim from the KiCad 10.0.6 libraries or drawn from the archived datasheets,
see its README.md, SOURCES.json and MANIFEST.md) is in the KiCad 10 file format, which KiCad 5–9 and EasyEDA cannot read, so it is
NOT shipped in this set: this set is the schematic and the footprint NAMES; the footprint-enabled layout handoff is the KiCad 10
project in kicad/ (KiCad 10.0.6 or later). Off-board parts (the LEM sensors USNSU/V/W on the busbar) carry no footprint on purpose.
