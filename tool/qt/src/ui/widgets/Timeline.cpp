#include "Timeline.h"

#include "ProtocolInfo.h"
#include "Theme.h"

#include <QHelpEvent>
#include <QPainter>
#include <QToolTip>

#include <cmath>

namespace {
constexpr int LANE_H = 22;
constexpr int LABEL_W = 170;
constexpr int AXIS_H = 22;

Theme::Sev sevOfClass(const QString &cls)
{
    if (cls == QLatin1String("fault") || cls == QLatin1String("service")) {
        return Theme::Sev::Crit;
    }
    if (cls == QLatin1String("no_arm") || cls == QLatin1String("degrade")) {
        return Theme::Sev::Warn;
    }
    return Theme::Sev::Info;
}
} // namespace

Timeline::Timeline(QWidget *parent) : QWidget(parent)
{
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    setAccessibleName(QStringLiteral("Fault history timeline"));
}

QSize Timeline::sizeHint() const { return {700, AXIS_H + 8 + std::max<int>(2, static_cast<int>(m_lanes.size())) * LANE_H}; }

void Timeline::setData(const QVector<DtcEvent> &dtc, const QVector<AlertEvent> &alerts, double tFirst, double tNow)
{
    const ProtocolInfo &pi = ProtocolInfo::instance();
    m_lanes.clear();
    QHash<int, int> laneOf;
    for (const DtcEvent &e : dtc) {
        if (!laneOf.contains(e.id)) {
            Lane l;
            l.id = e.id;
            l.name = pi.dtcName(e.id);
            const DtcInfo *d = pi.dtc(e.id);
            l.cls = d ? d->cls : QString();
            laneOf.insert(e.id, static_cast<int>(m_lanes.size()));
            m_lanes.push_back(l);
        }
        Lane &l = m_lanes[laneOf.value(e.id)];
        switch (e.kind) {
        case DtcEvent::Kind::Raised: l.spans.push_back({e.tMs, std::nan("")}); break;
        case DtcEvent::Kind::Cleared:
            if (!l.spans.isEmpty() && std::isnan(l.spans.last().t1)) {
                l.spans.last().t1 = e.tMs;
            }
            break;
        case DtcEvent::Kind::Occurrence: l.ticks.push_back(e.tMs); break;
        }
    }
    if (!alerts.isEmpty()) {
        Lane l;
        l.id = -1;
        l.name = QStringLiteral("alerts");
        for (const AlertEvent &a : alerts) {
            if (a.raised) {
                l.ticks.push_back(a.tMs);
            }
        }
        m_lanes.push_back(l);
    }
    m_t0 = tFirst;
    m_t1 = std::max(tNow, tFirst + 1.0);
    updateGeometry();
    update();
}

double Timeline::xOf(double t, double left, double width) const
{
    return left + (t - m_t0) / (m_t1 - m_t0) * width;
}

void Timeline::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const double left = LABEL_W, width = std::max(10, this->width() - LABEL_W - 12);
    QFont f = font();
    f.setPixelSize(11);
    p.setFont(f);
    if (m_lanes.isEmpty()) {
        p.setPen(Theme::color("dim"));
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("No DTC has occurred in this session"));
        return;
    }
    for (int i = 0; i < m_lanes.size(); i++) {
        const Lane &l = m_lanes[i];
        const double y = i * LANE_H + 4;
        p.setPen(Theme::color("muted"));
        p.drawText(QRectF(0, y, LABEL_W - 8, LANE_H), Qt::AlignRight | Qt::AlignVCenter, l.name);
        p.setPen(QPen(Theme::color("border"), 1));
        p.drawLine(QPointF(left, y + LANE_H / 2.0), QPointF(left + width, y + LANE_H / 2.0));
        const QColor c = (l.id < 0) ? Theme::color("accent") : Theme::sevColor(sevOfClass(l.cls));
        for (const Span &s : l.spans) {
            const double x0 = xOf(s.t0, left, width);
            const double x1 = std::isnan(s.t1) ? left + width : xOf(s.t1, left, width);
            p.setPen(Qt::NoPen);
            QColor fill = c;
            fill.setAlpha(std::isnan(s.t1) ? 200 : 130);
            p.setBrush(fill);
            p.drawRoundedRect(QRectF(x0, y + 5, std::max(3.0, x1 - x0), LANE_H - 10), 3, 3);
        }
        p.setPen(QPen(c, 2));
        for (double t : l.ticks) {
            const double x = xOf(t, left, width);
            p.drawLine(QPointF(x, y + 3), QPointF(x, y + LANE_H - 3));
        }
    }
    // axis: seconds before now
    const double yAxis = m_lanes.size() * LANE_H + 8;
    p.setPen(Theme::color("dim"));
    const double span = (m_t1 - m_t0) / 1000.0;
    const double step = span > 600 ? 120 : span > 120 ? 30 : span > 30 ? 10 : span > 10 ? 2 : 1;
    for (double s = 0; s <= span + 1e-9; s += step) {
        const double x = xOf(m_t1 - s * 1000.0, left, width);
        p.drawLine(QPointF(x, yAxis - 4), QPointF(x, yAxis));
        p.drawText(QRectF(x - 40, yAxis, 80, 14), Qt::AlignCenter, s == 0 ? QStringLiteral("now") : QStringLiteral("−%1 s").arg(s));
    }
}

bool Timeline::event(QEvent *e)
{
    if (e->type() == QEvent::ToolTip) {
        auto *he = static_cast<QHelpEvent *>(e);
        const int lane = (he->pos().y() - 4) / LANE_H;
        if (lane >= 0 && lane < m_lanes.size()) {
            const Lane &l = m_lanes[lane];
            QStringList spans;
            for (const Span &s : l.spans) {
                spans << (std::isnan(s.t1) ? QStringLiteral("since %1 s").arg(s.t0 / 1000.0, 0, 'f', 2)
                                           : QStringLiteral("%1 → %2 s").arg(s.t0 / 1000.0, 0, 'f', 2).arg(s.t1 / 1000.0, 0, 'f', 2));
            }
            const DtcInfo *d = ProtocolInfo::instance().dtc(l.id);
            QToolTip::showText(he->globalPos(), QStringLiteral("<b>%1</b>%2<br/>%3")
                                                    .arg(l.name, d ? QStringLiteral(" (%1)").arg(d->code) : QString(),
                                                         spans.isEmpty() ? QStringLiteral("%1 event(s)").arg(l.ticks.size())
                                                                         : spans.join(QStringLiteral("<br/>"))), this);
        }
        return true;
    }
    return QWidget::event(e);
}
