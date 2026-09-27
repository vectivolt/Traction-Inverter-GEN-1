#include "Theme.h"

#include <QApplication>
#include <QFile>
#include <QHash>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QStyleFactory>
#include <QSvgRenderer>

namespace Theme {
namespace {
Variant s_variant = Variant::Dark;

const QHash<QString, QString> &tokens()
{
    static const QHash<QString, QString> dark = {
        {QStringLiteral("bg"), QStringLiteral("#0E1116")},        {QStringLiteral("navbg"), QStringLiteral("#0B0E12")},
        {QStringLiteral("surface"), QStringLiteral("#151A21")},   {QStringLiteral("surfaceAlt"), QStringLiteral("#181D25")},
        {QStringLiteral("surface2"), QStringLiteral("#1D232C")},  {QStringLiteral("field"), QStringLiteral("#10141A")},
        {QStringLiteral("border"), QStringLiteral("#262D38")},    {QStringLiteral("borderStrong"), QStringLiteral("#3A4351")},
        {QStringLiteral("scroll"), QStringLiteral("#2E3642")},
        {QStringLiteral("text"), QStringLiteral("#E7EAF0")},      {QStringLiteral("muted"), QStringLiteral("#98A2B3")},
        {QStringLiteral("dim"), QStringLiteral("#5D6778")},
        {QStringLiteral("accent"), QStringLiteral("#4CB8FF")},    {QStringLiteral("accentHover"), QStringLiteral("#72C7FF")},
        {QStringLiteral("accentSoft"), QStringLiteral("#15324A")}, {QStringLiteral("onAccent"), QStringLiteral("#061019")},
        {QStringLiteral("ok"), QStringLiteral("#3FD69A")},        {QStringLiteral("okSoft"), QStringLiteral("#123528")},
        {QStringLiteral("warn"), QStringLiteral("#FFB547")},      {QStringLiteral("warnSoft"), QStringLiteral("#3A2C12")},
        {QStringLiteral("crit"), QStringLiteral("#FF5D6C")},      {QStringLiteral("critSoft"), QStringLiteral("#3D1A20")},
        {QStringLiteral("info"), QStringLiteral("#A28BFF")},      {QStringLiteral("infoSoft"), QStringLiteral("#271F45")},
        {QStringLiteral("grid"), QStringLiteral("#212833")},
    };
    static const QHash<QString, QString> light = {
        {QStringLiteral("bg"), QStringLiteral("#F3F5F8")},        {QStringLiteral("navbg"), QStringLiteral("#ECEFF4")},
        {QStringLiteral("surface"), QStringLiteral("#FFFFFF")},   {QStringLiteral("surfaceAlt"), QStringLiteral("#F8F9FB")},
        {QStringLiteral("surface2"), QStringLiteral("#EDF0F4")},  {QStringLiteral("field"), QStringLiteral("#FFFFFF")},
        {QStringLiteral("border"), QStringLiteral("#D8DEE6")},    {QStringLiteral("borderStrong"), QStringLiteral("#AEB7C4")},
        {QStringLiteral("scroll"), QStringLiteral("#C9D0DA")},
        {QStringLiteral("text"), QStringLiteral("#131820")},      {QStringLiteral("muted"), QStringLiteral("#566174")},
        {QStringLiteral("dim"), QStringLiteral("#8A94A4")},
        {QStringLiteral("accent"), QStringLiteral("#0A78C2")},    {QStringLiteral("accentHover"), QStringLiteral("#0C88DB")},
        {QStringLiteral("accentSoft"), QStringLiteral("#DCEEFB")}, {QStringLiteral("onAccent"), QStringLiteral("#FFFFFF")},
        {QStringLiteral("ok"), QStringLiteral("#0F8A5C")},        {QStringLiteral("okSoft"), QStringLiteral("#DDF4EA")},
        {QStringLiteral("warn"), QStringLiteral("#A86200")},      {QStringLiteral("warnSoft"), QStringLiteral("#FBEFD9")},
        {QStringLiteral("crit"), QStringLiteral("#CC2F3F")},      {QStringLiteral("critSoft"), QStringLiteral("#FBE3E5")},
        {QStringLiteral("info"), QStringLiteral("#5A4BD1")},      {QStringLiteral("infoSoft"), QStringLiteral("#E9E6FB")},
        {QStringLiteral("grid"), QStringLiteral("#E6EAF0")},
    };
    return (s_variant == Variant::Dark) ? dark : light;
}
} // namespace

Variant current() { return s_variant; }

QColor color(const char *token) { return QColor(tokens().value(QLatin1String(token), QStringLiteral("#FF00FF"))); }

void apply(QApplication &app, Variant v)
{
    s_variant = v;
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QPalette p;
    p.setColor(QPalette::Window, color("bg"));
    p.setColor(QPalette::WindowText, color("text"));
    p.setColor(QPalette::Base, color("field"));
    p.setColor(QPalette::AlternateBase, color("surfaceAlt"));
    p.setColor(QPalette::Text, color("text"));
    p.setColor(QPalette::PlaceholderText, color("dim"));
    p.setColor(QPalette::Button, color("surface2"));
    p.setColor(QPalette::ButtonText, color("text"));
    p.setColor(QPalette::Highlight, color("accent"));
    p.setColor(QPalette::HighlightedText, color("onAccent"));
    p.setColor(QPalette::ToolTipBase, color("surface2"));
    p.setColor(QPalette::ToolTipText, color("text"));
    p.setColor(QPalette::Link, color("accent"));
    p.setColor(QPalette::Mid, color("border"));
    p.setColor(QPalette::Dark, color("border"));
    p.setColor(QPalette::Light, color("surface2"));
    for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
        p.setColor(QPalette::Disabled, role, color("dim"));
    }
    app.setPalette(p);
    QFile f(QStringLiteral(":/theme.qss"));
    QString qss;
    if (f.open(QIODevice::ReadOnly)) {
        qss = QString::fromUtf8(f.readAll());
    }
    // longest token first, so @accentSoft is not eaten by @accent
    QStringList keys = tokens().keys();
    std::sort(keys.begin(), keys.end(), [](const QString &a, const QString &b) { return a.size() > b.size(); });
    for (const QString &k : keys) {
        qss.replace(QLatin1Char('@') + k, tokens().value(k));
    }
    app.setStyleSheet(qss);
}

QColor sevColor(Sev s)
{
    switch (s) {
    case Sev::Ok: return color("ok");
    case Sev::Warn: return color("warn");
    case Sev::Crit: return color("crit");
    case Sev::Info: return color("info");
    case Sev::Accent: return color("accent");
    case Sev::Stale: return color("dim");
    case Sev::Neutral: break;
    }
    return color("muted");
}

QString sevName(Sev s)
{
    static const char *const N[] = {"neutral", "ok", "warn", "crit", "info", "accent", "stale"};
    return QLatin1String(N[static_cast<int>(s)]);
}

QColor seriesColor(int i)
{
    static const char *const dark[] = {"#4CB8FF", "#FFB547", "#3FD69A", "#FF6FA8", "#A28BFF", "#7BE0E0", "#FF8C5A", "#C6D66B"};
    static const char *const light[] = {"#0A78C2", "#B86A00", "#0F8A5C", "#C2185B", "#5A4BD1", "#00838F", "#D84315", "#6B7A00"};
    return QColor(QLatin1String((s_variant == Variant::Dark ? dark : light)[((i % 8) + 8) % 8]));
}

QFont numberFont(int pixelSize, bool bold)
{
    QFont f = QApplication::font();
    f.setPixelSize(pixelSize);
    f.setWeight(bold ? QFont::DemiBold : QFont::Normal);
    f.setFeature(QFont::Tag("tnum"), 1); // tabular figures: numbers do not jitter as they change
    return f;
}

QFont monoFont(int pixelSize)
{
    QFont f(QStringLiteral("Menlo"));
    f.setStyleHint(QFont::Monospace);
    f.setFamilies({QStringLiteral("SF Mono"), QStringLiteral("Menlo"), QStringLiteral("Consolas"),
                   QStringLiteral("DejaVu Sans Mono")});
    f.setPixelSize(pixelSize);
    return f;
}

QIcon appIcon()
{
    QSvgRenderer r(QStringLiteral(":/app-icon.svg"));
    QIcon ic;
    for (int sz : {32, 64, 128, 256}) {
        QPixmap pm(sz, sz);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        r.render(&p);
        p.end();
        ic.addPixmap(pm);
    }
    return ic;
}

QIcon icon(const QString &name, const QColor &c)
{
    QFile f(QStringLiteral(":/icons/%1.svg").arg(name));
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    QByteArray svg = f.readAll();
    svg.replace("currentColor", c.name().toLatin1());
    QSvgRenderer r(svg);
    QIcon ic;
    for (int sz : {16, 20, 24, 32, 48}) {
        QPixmap pm(sz * 2, sz * 2);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        r.render(&p);
        p.end();
        pm.setDevicePixelRatio(2.0);
        ic.addPixmap(pm);
    }
    return ic;
}
} // namespace Theme
