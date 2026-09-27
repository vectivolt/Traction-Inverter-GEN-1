#include "PlotWidgets.h"

#include "Dsp.h"
#include "Theme.h"
#include "Widgets.h"

#include <QEvent>
#include <QMouseEvent>
#include <QPen>
#include <qwt_legend.h>
#include <qwt_plot_canvas.h>
#include <qwt_plot_curve.h>
#include <qwt_plot_grid.h>
#include <qwt_plot_magnifier.h>
#include <qwt_plot_marker.h>
#include <qwt_plot_panner.h>
#include <qwt_plot_rescaler.h>
#include <qwt_plot_zoomer.h>
#include <qwt_scale_engine.h>
#include <qwt_scale_widget.h>
#include <qwt_symbol.h>
#include <qwt_text.h>

#include <algorithm>
#include <cmath>

namespace {
constexpr double kPi = 3.14159265358979323846;

QwtText titleText(const QString &t)
{
    QwtText x(t);
    QFont f = x.font();
    f.setPixelSize(11);
    x.setFont(f);
    x.setColor(Theme::color("muted"));
    return x;
}

QwtPlotGrid *makeGrid(QwtPlot *p)
{
    auto *g = new QwtPlotGrid;
    g->enableXMin(false);
    g->enableYMin(false);
    g->attach(p);
    return g;
}

QwtPlotMarker *makeCursor(QwtPlot *p, const QColor &c, const QString &label)
{
    auto *m = new QwtPlotMarker;
    m->setLineStyle(QwtPlotMarker::VLine);
    m->setLinePen(QPen(c, 1.4, Qt::DashLine));
    QwtText t(label);
    t.setColor(c);
    m->setLabel(t);
    m->setLabelAlignment(Qt::AlignTop | Qt::AlignRight);
    m->setVisible(false);
    m->attach(p);
    return m;
}
} // namespace

namespace Plot {
void style(QwtPlot *p)
{
    p->setCanvasBackground(Theme::color("surface"));
    if (auto *c = qobject_cast<QwtPlotCanvas *>(p->canvas())) {
        c->setFrameStyle(QFrame::NoFrame);
        c->setBorderRadius(0);
    }
    p->setAutoFillBackground(true);
    QPalette pal = p->palette();
    pal.setColor(QPalette::Window, Theme::color("surface"));
    pal.setColor(QPalette::WindowText, Theme::color("muted"));
    pal.setColor(QPalette::Text, Theme::color("muted"));
    p->setPalette(pal);
    for (int a = 0; a < QwtAxis::AxisPositions; a++) {
        if (QwtScaleWidget *w = p->axisWidget(a)) {
            QPalette ap = w->palette();
            ap.setColor(QPalette::WindowText, Theme::color("muted"));
            ap.setColor(QPalette::Text, Theme::color("muted"));
            w->setPalette(ap);
            QFont f = w->font();
            f.setPixelSize(11);
            w->setFont(f);
            w->setMargin(2);
        }
    }
    const auto items = p->itemList(QwtPlotItem::Rtti_PlotGrid);
    for (QwtPlotItem *it : items) {
        static_cast<QwtPlotGrid *>(it)->setMajorPen(QPen(Theme::color("grid"), 1));
    }
    p->replot();
}
} // namespace Plot

// ---------------------------------------------------------------- TimePlot
TimePlot::TimePlot(QWidget *parent) : QwtPlot(parent)
{
    setAxisTitle(QwtAxis::XBottom, titleText(QStringLiteral("session time (s)")));
    setAxisAutoScale(QwtAxis::YLeft, true);
    m_grid = makeGrid(this);
    m_zoomer = new QwtPlotZoomer(canvas());
    m_zoomer->setTrackerMode(QwtPicker::AlwaysOff);
    m_zoomer->setMousePattern(QwtEventPattern::MouseSelect2, Qt::RightButton, Qt::ControlModifier);
    m_zoomer->setMousePattern(QwtEventPattern::MouseSelect3, Qt::RightButton);
    connect(m_zoomer, &QwtPlotZoomer::zoomed, this, [this](const QRectF &) {
        const bool base = m_zoomer->zoomRectIndex() == 0;
        if (base != m_follow) {
            m_follow = base;
            setAxisAutoScale(QwtAxis::YLeft, base);
            Q_EMIT followChanged(m_follow);
        }
    });
    m_panner = new QwtPlotPanner(canvas());
    m_panner->setMouseButton(Qt::MiddleButton);
    connect(m_panner, &QwtPlotPanner::panned, this, [this](int, int) {
        if (m_follow) {
            m_follow = false;
            Q_EMIT followChanged(false);
        }
    });
    m_magnifier = new QwtPlotMagnifier(canvas());
    m_magnifier->setMouseButton(Qt::NoButton);
    m_ca = makeCursor(this, Theme::color("warn"), QStringLiteral("A"));
    m_cb = makeCursor(this, Theme::color("info"), QStringLiteral("B"));
    canvas()->installEventFilter(this);
    canvas()->setFocusPolicy(Qt::StrongFocus);
    m_overlay = new QLabel(canvas());
    m_overlay->setObjectName(QStringLiteral("pill"));
    m_overlay->setProperty("severity", QStringLiteral("warn"));
    m_overlay->move(10, 8);
    m_overlay->hide();
    applyTheme();
}

void TimePlot::applyTheme()
{
    Plot::style(this);
    m_zoomer->setRubberBandPen(QPen(Theme::color("accent"), 1, Qt::DashLine));
    for (int i = 0; i < m_traces.size(); i++) {
        m_traces[i].curve->setPen(QPen(Theme::seriesColor(i), 1.6));
    }
    m_ca->setLinePen(QPen(Theme::color("warn"), 1.4, Qt::DashLine));
    m_cb->setLinePen(QPen(Theme::color("info"), 1.4, Qt::DashLine));
    replot();
}

void TimePlot::setChannels(const QStringList &channels)
{
    for (Trace &t : m_traces) {
        t.curve->detach();
        delete t.curve; // QwtPlotItems are owned by the plot only while attached
    }
    m_traces.clear();
    for (int i = 0; i < channels.size(); i++) {
        auto *c = new QwtPlotCurve(channels[i]);
        c->setPen(QPen(Theme::seriesColor(i), 1.6));
        c->setRenderHint(QwtPlotItem::RenderAntialiased, true);
        c->setPaintAttribute(QwtPlotCurve::ClipPolygons, true);
        c->attach(this);
        m_traces.push_back({channels[i], c});
    }
    replot();
}

QStringList TimePlot::channels() const
{
    QStringList l;
    for (const Trace &t : m_traces) {
        l << t.channel;
    }
    return l;
}

void TimePlot::setWindowMs(double ms) { m_windowMs = std::max(100.0, ms); }

void TimePlot::setFollow(bool on)
{
    if (on == m_follow) {
        return;
    }
    m_follow = on;
    if (on) {
        m_zoomer->zoom(0);
        setAxisAutoScale(QwtAxis::YLeft, true);
    }
    Q_EMIT followChanged(on);
}

void TimePlot::refresh(const TelemetryStore &s)
{
    double x0, x1;
    if (m_follow) {
        const double tEnd = s.size() ? s.lastT() : 0.0;
        x1 = tEnd / 1000.0;
        x0 = x1 - m_windowMs / 1000.0;
        setAxisScale(QwtAxis::XBottom, x0, x1);
    } else {
        const QwtInterval iv = axisInterval(QwtAxis::XBottom);
        x0 = iv.minValue();
        x1 = iv.maxValue();
    }
    const int maxPts = std::max(200, 2 * canvas()->width());
    QVector<double> xs, ys;
    for (Trace &t : m_traces) {
        s.series(t.channel, x0 * 1000.0 - 1.0, x1 * 1000.0 + 1.0, xs, ys, maxPts);
        for (double &x : xs) {
            x /= 1000.0;
        }
        t.curve->setSamples(xs, ys);
    }
    if (m_follow) {
        m_zoomer->setZoomBase(false);
    }
    replot();
}

void TimePlot::setOverlay(const QString &text)
{
    if (text.isEmpty()) {
        m_overlay->hide();
        return;
    }
    m_overlay->setText(text);
    m_overlay->adjustSize();
    m_overlay->show();
}

void TimePlot::setCursorMode(bool on)
{
    m_cursorMode = on;
    m_zoomer->setEnabled(!on);
    canvas()->setCursor(on ? Qt::CrossCursor : Qt::ArrowCursor);
}

void TimePlot::clearCursors()
{
    m_haveA = m_haveB = false;
    m_ca->setVisible(false);
    m_cb->setVisible(false);
    replot();
    Q_EMIT cursorsChanged();
}

bool TimePlot::cursorA(double *tS) const
{
    *tS = m_ca->xValue();
    return m_haveA;
}

bool TimePlot::cursorB(double *tS) const
{
    *tS = m_cb->xValue();
    return m_haveB;
}

void TimePlot::setCursor(bool b, double tS)
{
    QwtPlotMarker *m = b ? m_cb : m_ca;
    m->setXValue(tS);
    m->setVisible(true);
    (b ? m_haveB : m_haveA) = true;
    replot();
    Q_EMIT cursorsChanged();
}

void TimePlot::setMarkers(const QVector<QPair<double, QString>> &markers)
{
    for (QwtPlotMarker *m : m_events) {
        m->detach();
        delete m;
    }
    m_events.clear();
    for (const auto &mk : markers) {
        auto *m = new QwtPlotMarker;
        m->setLineStyle(QwtPlotMarker::VLine);
        m->setLinePen(QPen(Theme::color("borderStrong"), 1, Qt::DotLine));
        m->setXValue(mk.first / 1000.0);
        QwtText t(mk.second);
        t.setColor(Theme::color("muted"));
        QFont f = t.font();
        f.setPixelSize(10);
        t.setFont(f);
        m->setLabel(t);
        m->setLabelAlignment(Qt::AlignBottom | Qt::AlignRight);
        m->setLabelOrientation(Qt::Vertical);
        m->attach(this);
        m_events.push_back(m);
    }
    replot();
}

bool TimePlot::eventFilter(QObject *o, QEvent *e)
{
    if (o == canvas() && m_cursorMode && e->type() == QEvent::MouseButtonPress) {
        auto *me = static_cast<QMouseEvent *>(e);
        const double t = invTransform(QwtAxis::XBottom, me->position().x());
        const bool b = me->button() == Qt::RightButton || (me->modifiers() & Qt::ShiftModifier);
        setCursor(b, t);
        return true;
    }
    return QwtPlot::eventFilter(o, e);
}

// ---------------------------------------------------------------- DqPlot
DqPlot::DqPlot(QWidget *parent) : QwtPlot(parent)
{
    setAxisTitle(QwtAxis::XBottom, titleText(QStringLiteral("id (A)")));
    setAxisTitle(QwtAxis::YLeft, titleText(QStringLiteral("iq (A)")));
    m_grid = makeGrid(this);
    m_traj = new QwtPlotCurve(QStringLiteral("trajectory"));
    m_traj->setStyle(QwtPlotCurve::Lines);
    m_traj->setRenderHint(QwtPlotItem::RenderAntialiased, true);
    m_traj->attach(this);
    m_circle = new QwtPlotCurve(QStringLiteral("current limit"));
    m_circle->setRenderHint(QwtPlotItem::RenderAntialiased, true);
    m_circle->attach(this);
    m_ellipse = new QwtPlotCurve(QStringLiteral("voltage limit"));
    m_ellipse->setRenderHint(QwtPlotItem::RenderAntialiased, true);
    m_ellipse->attach(this);
    m_op = new QwtPlotMarker;
    m_op->attach(this);
    m_ref = new QwtPlotMarker;
    m_ref->attach(this);
    m_rescaler = new QwtPlotRescaler(canvas(), QwtAxis::XBottom, QwtPlotRescaler::Fitting);
    m_rescaler->setAspectRatio(QwtAxis::YLeft, 1.0);
    m_rescaler->setEnabled(true);
    auto *mag = new QwtPlotMagnifier(canvas());
    mag->setMouseButton(Qt::NoButton);
    new QwtPlotPanner(canvas());
    applyTheme();
}

void DqPlot::applyTheme()
{
    Plot::style(this);
    m_traj->setPen(QPen(Theme::seriesColor(0), 1.2));
    m_circle->setPen(QPen(Theme::color("warn"), 1.6, Qt::DashLine));
    m_ellipse->setPen(QPen(Theme::color("info"), 1.6, Qt::DashLine));
    m_op->setSymbol(new QwtSymbol(QwtSymbol::Ellipse, Theme::color("accent"), QPen(Theme::color("text"), 1.5), QSize(11, 11)));
    m_ref->setSymbol(new QwtSymbol(QwtSymbol::Cross, Qt::NoBrush, QPen(Theme::color("ok"), 2), QSize(13, 13)));
    replot();
}

void DqPlot::refresh(const TelemetryStore &s, const TelemetryFrame &last, const QJsonObject &hello, double seconds)
{
    QVector<double> tId, id, tIq, iq;
    const double t1 = s.lastT();
    s.series(QStringLiteral("foc.id_a"), t1 - seconds * 1000.0, t1, tId, id, 4000);
    s.series(QStringLiteral("foc.iq_a"), t1 - seconds * 1000.0, t1, tIq, iq, 4000);
    const int n = std::min(id.size(), iq.size());
    m_traj->setSamples(id.mid(0, n), iq.mid(0, n));
    const QJsonObject motor = hello.value(QLatin1String("motor")).toObject();
    const QJsonObject limits = hello.value(QLatin1String("limits")).toObject();
    // the current allowance in force (limits.i_limit_rms_a), else the SKU's peak rating; dq amperes are phase peak
    double iLimRms = last.v(QStringLiteral("limits.i_limit_rms_a"));
    if (!(iLimRms > 0)) {
        iLimRms = limits.value(QLatin1String("i_pk_rms_a")).toDouble(std::nan(""));
    }
    QVector<double> cx, cy, ex, ey;
    const double r = iLimRms * std::sqrt(2.0);
    if (r > 0) {
        for (int k = 0; k <= 180; k++) {
            const double a = 2.0 * kPi * k / 180.0;
            cx.push_back(r * std::cos(a));
            cy.push_back(r * std::sin(a));
        }
    }
    m_circle->setSamples(cx, cy);
    const double ld = motor.value(QLatin1String("ld_h")).toDouble(std::nan(""));
    const double lq = motor.value(QLatin1String("lq_h")).toDouble(std::nan(""));
    const double psi = motor.value(QLatin1String("psi_wb")).toDouble(std::nan(""));
    const double pp = motor.value(QLatin1String("pp")).toDouble(std::nan(""));
    const double rpm = last.pick({"motion.speed_rpm", "inv.speed_rpm"});
    const double vmax = last.v(QStringLiteral("foc.vmax_v"));
    const double we = std::abs(rpm) * 2.0 * kPi / 60.0 * pp;
    m_note.clear();
    if (!(ld > 0 && lq > 0 && psi > 0)) {
        m_note = QStringLiteral("motor data unknown (no hello): no voltage ellipse");
    } else if (!(vmax > 0)) {
        m_note = QStringLiteral("no V_DC / voltage limit: no voltage ellipse");
    } else if (we < 1.0) {
        m_note = QStringLiteral("standstill: the voltage limit does not bind");
    } else {
        const double rho = vmax / we; // flux-linkage radius
        for (int k = 0; k <= 360; k++) {
            const double a = 2.0 * kPi * k / 360.0;
            ex.push_back((-psi + rho * std::cos(a)) / ld);
            ey.push_back(rho * std::sin(a) / lq);
        }
        m_note = QStringLiteral("ellipse at %1 rpm, Vmax %2 V (centre id = −ψ/Ld = %3 A)")
                     .arg(rpm, 0, 'f', 0).arg(vmax, 0, 'f', 0).arg(-psi / ld, 0, 'f', 0);
    }
    m_ellipse->setSamples(ex, ey);
    m_op->setValue(last.v(QStringLiteral("foc.id_a"), 0.0), last.v(QStringLiteral("foc.iq_a"), 0.0));
    m_ref->setValue(last.v(QStringLiteral("foc.id_ref_a"), 0.0), last.v(QStringLiteral("foc.iq_ref_a"), 0.0));
    const double span = std::max({r > 0 ? r * 1.15 : 100.0, 50.0});
    setAxisScale(QwtAxis::XBottom, -span, span);
    setAxisScale(QwtAxis::YLeft, -span, span);
    m_rescaler->rescale();
    replot();
}

// ---------------------------------------------------------------- SpectrumPlot
SpectrumPlot::SpectrumPlot(QWidget *parent) : QwtPlot(parent)
{
    setAxisTitle(QwtAxis::XBottom, titleText(QStringLiteral("frequency (Hz)")));
    setAxisTitle(QwtAxis::YLeft, titleText(QStringLiteral("amplitude")));
    m_grid = makeGrid(this);
    m_curve = new QwtPlotCurve(QStringLiteral("spectrum"));
    m_curve->setRenderHint(QwtPlotItem::RenderAntialiased, true);
    m_curve->attach(this);
    m_peak = new QwtPlotMarker;
    m_peak->setLabelAlignment(Qt::AlignTop | Qt::AlignRight);
    m_peak->attach(this);
    new QwtPlotZoomer(canvas());
    applyTheme();
}

void SpectrumPlot::applyTheme()
{
    Plot::style(this);
    m_curve->setPen(QPen(Theme::seriesColor(0), 1.4));
    m_peak->setSymbol(new QwtSymbol(QwtSymbol::Diamond, Theme::color("warn"), QPen(Theme::color("warn")), QSize(9, 9)));
    replot();
}

QString SpectrumPlot::compute(const TelemetryStore &s, const QString &channel, int samples, bool logScale)
{
    if (s.size() < 16 || !s.hasChannel(channel)) {
        m_curve->setSamples(QVector<double>(), QVector<double>());
        replot();
        return QStringLiteral("no data for %1").arg(channel);
    }
    const int i0 = std::max(0, s.size() - samples);
    QVector<double> x, dts;
    for (int i = i0; i < s.size(); i++) {
        x.push_back(s.value(channel, i));
        if (i > i0) {
            dts.push_back(s.t(i) - s.t(i - 1));
        }
    }
    std::sort(dts.begin(), dts.end());
    const double dt = dts.isEmpty() ? 0.0 : dts[dts.size() / 2];
    const double fs = (dt > 0) ? 1000.0 / dt : 0.0;
    QVector<double> f, a;
    if (!Dsp::spectrum(x, fs, f, a)) {
        return QStringLiteral("too few samples");
    }
    int pk = 1;
    for (int k = 1; k < a.size(); k++) {
        if (a[k] > a[pk]) {
            pk = k;
        }
    }
    QVector<double> y = a;
    if (logScale) {
        for (double &v : y) {
            v = 20.0 * std::log10(std::max(v, 1e-9));
        }
    }
    m_curve->setSamples(f, y);
    setAxisTitle(QwtAxis::YLeft, titleText(logScale ? QStringLiteral("amplitude (dB)") : QStringLiteral("amplitude")));
    setAxisAutoScale(QwtAxis::XBottom, true);
    setAxisAutoScale(QwtAxis::YLeft, true);
    m_peak->setValue(f.value(pk), y.value(pk));
    QwtText lbl(QStringLiteral("%1 Hz").arg(f.value(pk), 0, 'f', 2));
    lbl.setColor(Theme::color("warn"));
    m_peak->setLabel(lbl);
    replot();
    return QStringLiteral("fs %1 Hz · %2 points · resolution %3 Hz · peak %4 at %5 Hz")
        .arg(fs, 0, 'f', 1).arg((f.size() - 1) * 2).arg(f.value(1), 0, 'g', 3).arg(a.value(pk), 0, 'g', 4)
        .arg(f.value(pk), 0, 'f', 2);
}
