// Theme — the application's look: one stylesheet (resources/theme.qss) with @tokens, a dark and a light palette,
// the matching QPalette (Fusion base style), and the colours custom-painted widgets use.
#pragma once

#include <QColor>
#include <QFont>
#include <QIcon>
#include <QString>

class QApplication;

namespace Theme {
enum class Variant { Dark, Light };
enum class Sev { Neutral, Ok, Warn, Crit, Info, Accent, Stale };

void apply(QApplication &app, Variant v);
Variant current();
QColor color(const char *token); // "bg", "surface", "text", "muted", "accent", "ok", "warn", "crit", ...
QColor sevColor(Sev s);
QString sevName(Sev s);          // the QSS "severity" property value
QColor seriesColor(int i);       // plot/trace palette, distinguishable in both variants
QFont numberFont(int pixelSize, bool bold = true); // tabular figures
QFont monoFont(int pixelSize = 12);
// An icon from resources/icons/<name>.svg drawn in the given colour (the SVGs use currentColor).
QIcon icon(const QString &name, const QColor &c);
QIcon appIcon(); // resources/app-icon.svg
// Spacing scale
constexpr int S1 = 4, S2 = 8, S3 = 12, S4 = 16, S5 = 24, S6 = 32;
} // namespace Theme
