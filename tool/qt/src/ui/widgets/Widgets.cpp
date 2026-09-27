#include "Widgets.h"

#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>
#include <QToolTip>

#include <algorithm>

namespace {
constexpr int SPARK_MAX = 150;

void pushHist(QVector<double> &h, double v)
{
    h.push_back(v);
    if (h.size() > SPARK_MAX) {
        h.remove(0, h.size() - SPARK_MAX);
    }
}

void paintCardBackground(QPainter &p, const QRectF &r)
{
    p.setPen(QPen(Theme::color("border"), 1));
    p.setBrush(Theme::color("surface"));
    p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 10, 10);
}

void sparkline(QPainter &p, const QRectF &r, const QVector<double> &h, const QColor &c, bool fill, bool dashed,
               double lo, double hi)
{
    if (h.size() < 2 || !(hi > lo)) {
        return;
    }
    QPainterPath path;
    bool started = false;
    const double dx = r.width() / (SPARK_MAX - 1);
    const double x0 = r.right() - dx * (h.size() - 1);
    double lastX = x0;
    for (int i = 0; i < h.size(); i++) {
        if (std::isnan(h[i])) {
            started = false;
            continue;
        }
        const double x = x0 + dx * i;
        const double y = r.bottom() - (h[i] - lo) / (hi - lo) * r.height();
        if (!started) {
            path.moveTo(x, y);
            started = true;
        } else {
            path.lineTo(x, y);
        }
        lastX = x;
    }
    if (fill && !path.isEmpty()) {
        QPainterPath area = path;
        area.lineTo(lastX, r.bottom());
        area.lineTo(x0, r.bottom());
        area.closeSubpath();
        QLinearGradient g(r.topLeft(), r.bottomLeft());
        QColor c1 = c;
        c1.setAlpha(70);
        QColor c2 = c;
        c2.setAlpha(0);
        g.setColorAt(0, c1);
        g.setColorAt(1, c2);
        p.fillPath(area, g);
    }
    QPen pen(c, 1.6);
    if (dashed) {
        pen.setStyle(Qt::DashLine);
    }
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
}
} // namespace

// ---------------------------------------------------------------- Card
Card::Card(const QString &title, QWidget *parent) : QFrame(parent)
{
    setObjectName(QStringLiteral("card"));
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(Theme::S4, Theme::S3, Theme::S4, Theme::S4);
    outer->setSpacing(Theme::S3);
    m_header = new QHBoxLayout;
    m_header->setSpacing(Theme::S2);
    m_title = new QLabel(title.toUpper(), this);
    m_title->setObjectName(QStringLiteral("cardTitle"));
    m_header->addWidget(m_title);
    m_header->addStretch(1);
    outer->addLayout(m_header);
    m_body = new QVBoxLayout;
    m_body->setSpacing(Theme::S2);
    outer->addLayout(m_body, 1);
}

void Card::addHeaderWidget(QWidget *w) { m_header->addWidget(w); }

QLabel *makePill(const QString &text, Theme::Sev s, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    l->setObjectName(QStringLiteral("pill"));
    l->setAlignment(Qt::AlignCenter);
    setPill(l, text, s);
    return l;
}

void setPill(QLabel *pill, const QString &text, Theme::Sev s)
{
    pill->setText(text);
    const QString sev = Theme::sevName(s);
    if (pill->property("severity").toString() != sev) {
        pill->setProperty("severity", sev);
        pill->style()->unpolish(pill);
        pill->style()->polish(pill);
    }
}

QString fmtNum(double v, int decimals)
{
    if (std::isnan(v)) {
        return QStringLiteral("—");
    }
    if (std::isinf(v)) {
        return (v > 0) ? QStringLiteral("∞") : QStringLiteral("−∞");
    }
    QString s = QString::number(v, 'f', decimals);
    if (s.startsWith(QLatin1Char('-'))) {
        s.replace(0, 1, QChar(0x2212)); // a real minus sign
        if (s.mid(1).toDouble() == 0.0) {
            s.remove(0, 1); // no "−0.0"
        }
    }
    return s;
}

QString fmtAge(qint64 ms)
{
    if (ms < 0) {
        return QStringLiteral("never");
    }
    if (ms < 1000) {
        return QStringLiteral("%1 ms").arg(ms);
    }
    if (ms < 60000) {
        return QStringLiteral("%1 s").arg(ms / 1000.0, 0, 'f', 1);
    }
    return QStringLiteral("%1 min").arg(ms / 60000.0, 0, 'f', 1);
}

// ---------------------------------------------------------------- Tile
Tile::Tile(const QString &title, const QString &unit, int decimals, QWidget *parent)
    : QWidget(parent), m_title(title.toUpper()), m_unit(unit), m_dec(decimals)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMinimumHeight(118);
    setAccessibleName(title);
}

void Tile::setValue(double v)
{
    m_value = v;
    pushHist(m_h1, v);
    if (m_hasSecond) {
        pushHist(m_h2, m_second);
    }
    setAccessibleDescription(fmtNum(v, m_dec) + QLatin1Char(' ') + m_unit);
    update();
}

void Tile::setSecond(const QString &label, double v, const QString &primaryLabel)
{
    m_hasSecond = true;
    m_label2 = label;
    m_label1 = primaryLabel;
    m_second = v;
}

void Tile::setSubText(const QString &s)
{
    if (s != m_sub) {
        m_sub = s;
        update();
    }
}

void Tile::setSeverity(Theme::Sev s)
{
    if (s != m_sev) {
        m_sev = s;
        update();
    }
}

void Tile::setStale(bool stale, qint64 ageMs)
{
    if (stale != m_stale || (stale && ageMs / 100 != m_age / 100)) {
        m_stale = stale;
        m_age = ageMs;
        update();
    }
}

void Tile::clearHistory()
{
    m_h1.clear();
    m_h2.clear();
    m_value = std::nan("");
    m_second = std::nan("");
    update();
}

void Tile::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = rect();
    paintCardBackground(p, r);
    const double pad = Theme::S4;
    // severity accent on the left edge
    if (m_sev != Theme::Sev::Neutral && !m_stale) {
        p.setPen(Qt::NoPen);
        p.setBrush(Theme::sevColor(m_sev));
        p.drawRoundedRect(QRectF(r.left() + 1, r.top() + 14, 3, r.height() - 28), 1.5, 1.5);
    }
    QFont tf = font();
    tf.setPixelSize(11);
    tf.setWeight(QFont::DemiBold);
    tf.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
    p.setFont(tf);
    p.setPen(Theme::color("muted"));
    p.drawText(QRectF(pad, 10, r.width() - 2 * pad, 16), Qt::AlignLeft | Qt::AlignVCenter, m_title);
    if (m_stale) {
        const QString badge = (m_age < 0) ? QStringLiteral("NO DATA") : QStringLiteral("STALE %1").arg(fmtAge(m_age));
        QFont bf = tf;
        bf.setPixelSize(10);
        p.setFont(bf);
        const int bw = QFontMetrics(bf).horizontalAdvance(badge) + 12;
        const QRectF br(r.right() - pad - bw, 9, bw, 18);
        p.setPen(Qt::NoPen);
        p.setBrush(Theme::color("surface2"));
        p.drawRoundedRect(br, 9, 9);
        p.setPen(Theme::color("warn"));
        p.drawText(br, Qt::AlignCenter, badge);
    }
    const QColor valColor = m_stale ? Theme::color("dim") : Theme::color("text");
    const QString v = fmtNum(m_value, m_dec);
    QFont nf = Theme::numberFont(30);
    p.setFont(nf);
    p.setPen(valColor);
    const QFontMetrics nm(nf);
    const QRectF vr(pad, 30, r.width() - 2 * pad, 38);
    p.drawText(vr, Qt::AlignLeft | Qt::AlignVCenter, v);
    const double vw = nm.horizontalAdvance(v);
    QFont uf = font();
    uf.setPixelSize(13);
    p.setFont(uf);
    p.setPen(Theme::color("muted"));
    p.drawText(QRectF(pad + vw + 6, 30, r.width(), 42), Qt::AlignLeft | Qt::AlignVCenter,
               m_label1.isEmpty() ? m_unit : QStringLiteral("%1 %2").arg(m_unit, m_label1));
    double y = 70;
    if (m_hasSecond) {
        QFont sf = Theme::numberFont(13, false);
        p.setFont(sf);
        const QString s2 = QStringLiteral("%1  %2 %3").arg(m_label2, fmtNum(m_second, m_dec), m_unit);
        p.setPen(m_stale ? Theme::color("dim") : Theme::seriesColor(1));
        p.drawText(QRectF(pad, y, r.width() - 2 * pad, 16), Qt::AlignLeft | Qt::AlignVCenter, s2);
        y += 16;
    }
    if (!m_sub.isEmpty()) {
        QFont sf = font();
        sf.setPixelSize(11);
        p.setFont(sf);
        p.setPen(Theme::color("dim"));
        p.drawText(QRectF(pad, y, r.width() - 2 * pad, 15), Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetrics(sf).elidedText(m_sub, Qt::ElideRight, static_cast<int>(r.width() - 2 * pad)));
    }
    // sparkline across the bottom
    double lo = 0, hi = 0;
    bool any = false;
    for (const QVector<double> *h : {&m_h1, &m_h2}) {
        for (double x : *h) {
            if (std::isnan(x)) {
                continue;
            }
            lo = any ? std::min(lo, x) : x;
            hi = any ? std::max(hi, x) : x;
            any = true;
        }
    }
    if (any) {
        if (hi - lo < 1e-9) {
            hi += 1.0;
            lo -= 1.0;
        }
        const QRectF sr(pad, r.bottom() - 26, r.width() - 2 * pad, 18);
        const QColor c1 = m_stale ? Theme::color("dim") : Theme::seriesColor(0);
        sparkline(p, sr, m_h1, c1, true, false, lo, hi);
        if (m_hasSecond) {
            sparkline(p, sr, m_h2, m_stale ? Theme::color("dim") : Theme::seriesColor(1), false, true, lo, hi);
        }
    }
}

// ---------------------------------------------------------------- ArcGauge
ArcGauge::ArcGauge(const QString &title, const QString &unit, double min, double max, int decimals, QWidget *parent)
    : QWidget(parent), m_title(title.toUpper()), m_unit(unit), m_min(min), m_max(max), m_dec(decimals)
{
    setMinimumSize(170, 150);
    setAccessibleName(title);
}

void ArcGauge::setValue(double v)
{
    m_value = v;
    setAccessibleDescription(fmtNum(v, m_dec) + QLatin1Char(' ') + m_unit);
    update();
}

void ArcGauge::setRange(double min, double max)
{
    if (max > min && (min != m_min || max != m_max)) {
        m_min = min;
        m_max = max;
        update();
    }
}

void ArcGauge::setBands(const QVector<Band> &b)
{
    m_bands = b;
    update();
}

void ArcGauge::setStale(bool s)
{
    if (s != m_stale) {
        m_stale = s;
        update();
    }
}

void ArcGauge::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = rect();
    paintCardBackground(p, r);
    QFont tf = font();
    tf.setPixelSize(11);
    tf.setWeight(QFont::DemiBold);
    tf.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
    p.setFont(tf);
    p.setPen(Theme::color("muted"));
    p.drawText(QRectF(Theme::S4, 10, r.width() - 2 * Theme::S4, 16), Qt::AlignLeft | Qt::AlignVCenter, m_title);
    const double side = std::min(r.width() - 40, r.height() - 44);
    const QRectF arc(r.center().x() - side / 2, 34, side, side);
    const double start = 210.0, span = -240.0; // degrees, Qt: counter-clockwise positive
    auto frac = [this](double v) { return std::clamp((v - m_min) / (m_max - m_min), 0.0, 1.0); };
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(Theme::color("surface2"), 10, Qt::SolidLine, Qt::RoundCap));
    p.drawArc(arc, static_cast<int>(start * 16), static_cast<int>(span * 16));
    for (const Band &b : m_bands) {
        const double f0 = frac(b.from), f1 = frac(b.to);
        QColor c = Theme::sevColor(b.sev);
        c.setAlpha(m_stale ? 60 : 150);
        p.setPen(QPen(c, 3, Qt::SolidLine, Qt::FlatCap));
        const QRectF inner = arc.adjusted(-9, -9, 9, 9);
        p.drawArc(inner, static_cast<int>((start + span * f0) * 16), static_cast<int>(span * (f1 - f0) * 16));
    }
    if (!std::isnan(m_value)) {
        Theme::Sev sev = Theme::Sev::Accent;
        for (const Band &b : m_bands) {
            if (m_value >= b.from && m_value <= b.to) {
                sev = b.sev;
            }
        }
        const QColor c = m_stale ? Theme::color("dim") : Theme::sevColor(sev);
        p.setPen(QPen(c, 10, Qt::SolidLine, Qt::RoundCap));
        const double f0 = (m_min < 0.0 && m_max > 0.0) ? frac(0.0) : 0.0; // a signed scale fills from zero
        p.drawArc(arc, static_cast<int>((start + span * f0) * 16), static_cast<int>(span * (frac(m_value) - f0) * 16));
    }
    QFont nf = Theme::numberFont(24);
    p.setFont(nf);
    p.setPen(m_stale ? Theme::color("dim") : Theme::color("text"));
    p.drawText(QRectF(arc.left(), arc.center().y() - 18, arc.width(), 30), Qt::AlignCenter, fmtNum(m_value, m_dec));
    QFont uf = font();
    uf.setPixelSize(11);
    p.setFont(uf);
    p.setPen(Theme::color("muted"));
    p.drawText(QRectF(arc.left(), arc.center().y() + 12, arc.width(), 16), Qt::AlignCenter, m_unit);
    p.setPen(Theme::color("dim"));
    const double yb = arc.bottom() - side * 0.08;
    p.drawText(QRectF(arc.left() - 10, yb, 60, 14), Qt::AlignLeft, fmtNum(m_min, 0));
    p.drawText(QRectF(arc.right() - 50, yb, 60, 14), Qt::AlignRight, fmtNum(m_max, 0));
}

// ---------------------------------------------------------------- BandBar
BandBar::BandBar(const QString &label, const QString &unit, double min, double max, QWidget *parent)
    : QWidget(parent), m_label(label), m_unit(unit), m_min(min), m_max(max)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(30);
    setAccessibleName(label);
}

void BandBar::setValue(double v)
{
    m_value = v;
    setAccessibleDescription(fmtNum(v, 1) + QLatin1Char(' ') + m_unit);
    update();
}

void BandBar::setZones(double warnFrom, double critFrom)
{
    m_warn = warnFrom;
    m_crit = critFrom;
    update();
}

void BandBar::setStale(bool s)
{
    if (s != m_stale) {
        m_stale = s;
        update();
    }
}

void BandBar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = rect();
    QFont lf = font();
    lf.setPixelSize(12);
    p.setFont(lf);
    p.setPen(Theme::color("muted"));
    const double labelW = 92, valueW = 72;
    p.drawText(QRectF(0, 0, labelW, r.height()), Qt::AlignLeft | Qt::AlignVCenter, m_label);
    const QRectF track(labelW, r.center().y() - 3, r.width() - labelW - valueW - 8, 6);
    auto x = [&](double v) { return track.left() + std::clamp((v - m_min) / (m_max - m_min), 0.0, 1.0) * track.width(); };
    p.setPen(Qt::NoPen);
    p.setBrush(Theme::color("surface2"));
    p.drawRoundedRect(track, 3, 3);
    if (!std::isnan(m_warn)) {
        QColor w = Theme::color("warn");
        w.setAlpha(m_stale ? 40 : 90);
        p.setBrush(w);
        p.drawRect(QRectF(x(m_warn), track.top(), x(m_crit) - x(m_warn), track.height()));
        QColor c = Theme::color("crit");
        c.setAlpha(m_stale ? 40 : 110);
        p.setBrush(c);
        p.drawRect(QRectF(x(m_crit), track.top(), track.right() - x(m_crit), track.height()));
    }
    Theme::Sev sev = Theme::Sev::Ok;
    if (!std::isnan(m_value) && !std::isnan(m_warn)) {
        sev = (m_value >= m_crit) ? Theme::Sev::Crit : (m_value >= m_warn) ? Theme::Sev::Warn : Theme::Sev::Ok;
    }
    const QColor mc = m_stale ? Theme::color("dim") : Theme::sevColor(sev);
    if (!std::isnan(m_value)) {
        p.setBrush(mc);
        p.drawRoundedRect(QRectF(track.left(), track.top(), x(m_value) - track.left(), track.height()), 3, 3);
        p.setBrush(Theme::color("text"));
        p.drawEllipse(QPointF(x(m_value), track.center().y()), 5, 5);
    }
    QFont nf = Theme::numberFont(13);
    p.setFont(nf);
    p.setPen(m_stale ? Theme::color("dim") : Theme::color("text"));
    p.drawText(QRectF(r.width() - valueW, 0, valueW, r.height()), Qt::AlignRight | Qt::AlignVCenter,
               QStringLiteral("%1 %2").arg(fmtNum(m_value, 1), m_unit));
}

// ---------------------------------------------------------------- StateTrack
namespace {
struct TrackStop {
    int sm;
    const char *label;
};
// docs/firmware-contract.md §9: INIT -> SENSOR_SELFTEST -> VEHICLE_HANDSHAKE -> GATE_SELFTEST -> PRECHARGE_WAIT ->
// ARMED_ZERO_TORQUE -> RUN/DERATE (enum values from the protocol, A.7)
const TrackStop TRACK[] = {{0, "OFF"}, {1, "INIT"}, {2, "SENSORS"}, {3, "HANDSHAKE"}, {5, "GATE TEST"},
                           {4, "PRECHARGE"}, {6, "ARMED"}, {7, "RUN"}};
const TrackStop SIDE[] = {{8, "DERATE"}, {9, "FAULT"}, {10, "DISCHARGE"}, {11, "POWERDOWN"}};
} // namespace

StateTrack::StateTrack(QWidget *parent) : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(96);
    setAccessibleName(QStringLiteral("Operating state"));
}

void StateTrack::setState(int sm)
{
    if (sm != m_state) {
        m_state = sm;
        setAccessibleDescription(QString::number(sm));
        update();
    }
}

void StateTrack::setStale(bool s)
{
    if (s != m_stale) {
        m_stale = s;
        update();
    }
}

void StateTrack::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = rect();
    const QRectF main(34, 0, r.width() - 68, r.height());
    const int n = static_cast<int>(sizeof(TRACK) / sizeof(TRACK[0]));
    int cur = -1;
    for (int i = 0; i < n; i++) {
        if (TRACK[i].sm == m_state || (m_state == 8 && TRACK[i].sm == 7)) {
            cur = i;
        }
    }
    const double y = 22;
    const double step = main.width() / (n - 1);
    QFont lf = font();
    lf.setPixelSize(10);
    lf.setWeight(QFont::DemiBold);
    lf.setLetterSpacing(QFont::AbsoluteSpacing, 0.6);
    p.setFont(lf);
    const QColor accent = m_stale ? Theme::color("dim") : Theme::color("accent");
    for (int i = 0; i < n; i++) {
        const double x = main.left() + step * i;
        if (i < n - 1) {
            p.setPen(QPen((cur >= 0 && i < cur) ? accent : Theme::color("border"), 2));
            p.drawLine(QPointF(x + 7, y), QPointF(x + step - 7, y));
        }
        const bool past = cur >= 0 && i < cur;
        const bool here = i == cur;
        p.setPen(QPen(here || past ? accent : Theme::color("borderStrong"), 2));
        p.setBrush(here ? accent : (past ? Theme::color("accentSoft") : Theme::color("surface")));
        p.drawEllipse(QPointF(x, y), here ? 7.5 : 5.5, here ? 7.5 : 5.5);
        p.setPen(here ? Theme::color("text") : Theme::color("muted"));
        p.drawText(QRectF(x - 50, y + 12, 100, 16), Qt::AlignHCenter | Qt::AlignTop, QLatin1String(TRACK[i].label));
    }
    // side states as pills on a second row
    double x = main.left() - 10;
    for (const TrackStop &s : SIDE) {
        const bool on = s.sm == m_state;
        const QRectF pr(x, y + 40, 90, 22);
        Theme::Sev sev = (s.sm == 9) ? Theme::Sev::Crit : (s.sm == 8) ? Theme::Sev::Warn : Theme::Sev::Info;
        QColor bg = on ? Theme::sevColor(sev) : Theme::color("surface2");
        if (on && m_stale) {
            bg = Theme::color("dim");
        }
        p.setPen(Qt::NoPen);
        p.setBrush(bg);
        p.drawRoundedRect(pr, 11, 11);
        p.setPen(on ? Theme::color("onAccent") : Theme::color("dim"));
        p.drawText(pr, Qt::AlignCenter, QLatin1String(s.label));
        x += 98;
    }
    if (m_state < 0 || m_state > 11) {
        p.setPen(Theme::color("dim"));
        p.drawText(QRectF(x + 8, y + 40, main.right() - x, 22), Qt::AlignLeft | Qt::AlignVCenter,
                   m_state < 0 ? QStringLiteral("state unknown") : QStringLiteral("state %1 (not in the protocol)").arg(m_state));
    }
}

// ---------------------------------------------------------------- CheckList
CheckList::CheckList(QWidget *parent) : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    setMouseTracking(true);
}

void CheckList::setItems(const QVector<QPair<QString, QString>> &items)
{
    m_items.clear();
    for (const auto &it : items) {
        Item i;
        i.key = it.first;
        const QStringList parts = it.second.split(QLatin1Char('|'));
        i.title = parts.value(0);
        i.ref = parts.value(1);
        m_items.push_back(i);
    }
    updateGeometry();
    update();
}

void CheckList::set(const QString &key, State s, const QString &detail)
{
    for (Item &i : m_items) {
        if (i.key == key && (i.st != s || i.detail != detail)) {
            i.st = s;
            i.detail = detail;
            update();
        }
    }
}

void CheckList::setAll(State s)
{
    for (Item &i : m_items) {
        i.st = s;
    }
    update();
}

void CheckList::setStale(bool stale)
{
    if (stale != m_stale) {
        m_stale = stale;
        update();
    }
}

CheckList::State CheckList::state(const QString &key) const
{
    for (const Item &i : m_items) {
        if (i.key == key) {
            return i.st;
        }
    }
    return State::Unknown;
}

QSize CheckList::sizeHint() const { return {300, static_cast<int>(m_items.size()) * 24 + 4}; }

int CheckList::rowAt(int y) const { return y / 24; }

bool CheckList::event(QEvent *e)
{
    if (e->type() == QEvent::ToolTip) {
        auto *he = static_cast<QHelpEvent *>(e);
        const int row = rowAt(he->pos().y());
        if (row >= 0 && row < m_items.size()) {
            const Item &i = m_items[row];
            QToolTip::showText(he->globalPos(), QStringLiteral("<b>%1</b> — %2%3").arg(i.title, i.ref,
                               i.detail.isEmpty() ? QString() : QStringLiteral("<br/>") + i.detail.toHtmlEscaped()), this);
        }
        return true;
    }
    return QWidget::event(e);
}

void CheckList::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QFont f = font();
    f.setPixelSize(13);
    QFont rf = f;
    rf.setPixelSize(11);
    for (int row = 0; row < m_items.size(); row++) {
        const Item &i = m_items[row];
        const double y = row * 24.0 + 12;
        const QPointF c(9, y);
        QColor col = m_stale ? Theme::color("dim")
                   : (i.st == State::Ok) ? Theme::color("ok") : (i.st == State::Fail) ? Theme::color("crit") : Theme::color("dim");
        p.setPen(Qt::NoPen);
        QColor bg = col;
        bg.setAlpha(40);
        p.setBrush(bg);
        p.drawEllipse(c, 8, 8);
        p.setPen(QPen(col, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        if (i.st == State::Ok) {
            p.drawPolyline(QPolygonF({c + QPointF(-3.5, 0.2), c + QPointF(-0.8, 2.8), c + QPointF(3.8, -2.6)}));
        } else if (i.st == State::Fail) {
            p.drawLine(c + QPointF(-3, -3), c + QPointF(3, 3));
            p.drawLine(c + QPointF(-3, 3), c + QPointF(3, -3));
        } else {
            p.drawLine(c + QPointF(-3, 0), c + QPointF(3, 0));
        }
        p.setFont(f);
        p.setPen(i.st == State::Fail ? Theme::color("text") : Theme::color(i.st == State::Ok ? "text" : "muted"));
        p.drawText(QRectF(24, y - 10, width() - 110, 20), Qt::AlignLeft | Qt::AlignVCenter,
                   i.detail.isEmpty() ? i.title : QStringLiteral("%1 — %2").arg(i.title, i.detail));
        p.setFont(rf);
        p.setPen(Theme::color("dim"));
        p.drawText(QRectF(width() - 104, y - 10, 104, 20), Qt::AlignRight | Qt::AlignVCenter, i.ref);
    }
}

// ---------------------------------------------------------------- KvGrid
KvGrid::KvGrid(QWidget *parent) : QWidget(parent)
{
    auto *g = new QGridLayout(this);
    g->setContentsMargins(0, 0, 0, 0);
    g->setHorizontalSpacing(Theme::S4);
    g->setVerticalSpacing(Theme::S1 + 2);
    g->setColumnStretch(0, 1);
}

void KvGrid::addRow(const QString &key, const QString &label, const QString &tip, bool hostSide)
{
    auto *g = static_cast<QGridLayout *>(layout());
    Row r;
    r.hostSide = hostSide;
    r.label = new QLabel(label, this);
    r.label->setObjectName(QStringLiteral("muted"));
    r.label->setToolTip(tip);
    r.value = new QLabel(QStringLiteral("—"), this);
    r.value->setObjectName(QStringLiteral("kvValue"));
    r.value->setFont(Theme::numberFont(13, false));
    r.value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    r.label->setWordWrap(true); // the descriptive side wraps; numbers never do
    r.value->setTextInteractionFlags(Qt::TextSelectableByMouse);
    r.value->setToolTip(tip);
    g->addWidget(r.label, m_n, 0);
    g->addWidget(r.value, m_n, 1);
    m_n++;
    m_rows.insert(key, r);
    paintRow(m_rows[key]);
}

void KvGrid::setValue(const QString &key, const QString &text, Theme::Sev s)
{
    auto it = m_rows.find(key);
    if (it == m_rows.end()) {
        return;
    }
    if (it->value->text() != text) {
        it->value->setText(text);
    }
    if (it->sev != s) {
        it->sev = s;
        paintRow(*it);
    }
}

void KvGrid::setStale(bool stale)
{
    if (stale == m_stale) {
        return;
    }
    m_stale = stale;
    for (Row &r : m_rows) {
        paintRow(r);
    }
}

void KvGrid::paintRow(Row &r)
{
    r.value->setProperty("severity", Theme::sevName(r.sev));
    r.value->setProperty("stale", m_stale && !r.hostSide);
    r.value->style()->unpolish(r.value);
    r.value->style()->polish(r.value);
}

QString KvGrid::valueText(const QString &key) const
{
    const auto it = m_rows.constFind(key);
    return (it == m_rows.constEnd()) ? QString() : it->value->text();
}

// ---------------------------------------------------------------- Toast
Toast::Toast(QWidget *window) : QFrame(window)
{
    setObjectName(QStringLiteral("toast"));
    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(Theme::S4, Theme::S3, Theme::S4, Theme::S3);
    l->setSpacing(2);
    m_title = new QLabel(this);
    QFont f = m_title->font();
    f.setWeight(QFont::DemiBold);
    m_title->setFont(f);
    m_text = new QLabel(this);
    m_text->setWordWrap(true);
    m_text->setObjectName(QStringLiteral("muted"));
    l->addWidget(m_title);
    l->addWidget(m_text);
    setFixedWidth(360);
    m_hide.setSingleShot(true);
    connect(&m_hide, &QTimer::timeout, this, &QWidget::hide);
    window->installEventFilter(this);
    hide();
}

void Toast::show(const QString &title, const QString &text, Theme::Sev s, int ms)
{
    m_title->setText(title);
    m_text->setText(text);
    setProperty("severity", Theme::sevName(s));
    style()->unpolish(this);
    style()->polish(this);
    adjustSize();
    place();
    QFrame::show();
    raise();
    m_hide.start(ms);
}

bool Toast::eventFilter(QObject *o, QEvent *e)
{
    if (o == parent() && (e->type() == QEvent::Resize || e->type() == QEvent::Move)) {
        place();
    }
    return QFrame::eventFilter(o, e);
}

void Toast::place()
{
    auto *w = parentWidget();
    move(w->width() - width() - Theme::S5, 64);
}
