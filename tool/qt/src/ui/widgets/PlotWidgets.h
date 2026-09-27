// PlotWidgets — the Qwt plots: a multi-trace time plot over the telemetry store (follow / pause, rubber-band zoom,
// wheel magnify, middle-button pan, two measurement cursors), the id/iq plane with the current circle and the
// voltage ellipse, and an amplitude spectrum. Data are pulled from the store per refresh, min/max-decimated to the
// canvas width, so a long window never draws more points than there are pixels.
#pragma once

#include "Telemetry.h"

#include <QJsonObject>
#include <QLabel>
#include <qwt_plot.h>

class QwtPlotCurve;
class QwtPlotGrid;
class QwtPlotMagnifier;
class QwtPlotMarker;
class QwtPlotPanner;
class QwtPlotZoomer;
class QwtPlotRescaler;

namespace Plot {
void style(QwtPlot *p); // theme colours, fonts, grid
} // namespace Plot

class TimePlot : public QwtPlot
{
    Q_OBJECT
public:
    explicit TimePlot(QWidget *parent = nullptr);
    void setChannels(const QStringList &channels); // index = colour
    QStringList channels() const;
    void setWindowMs(double ms);
    void setFollow(bool on);
    bool follow() const { return m_follow; }
    void refresh(const TelemetryStore &s);
    void setOverlay(const QString &text); // empty hides it (the stale banner)
    void setCursorMode(bool on);
    void clearCursors();
    bool cursorA(double *tS) const;
    bool cursorB(double *tS) const;
    void applyTheme();
    void setCursor(bool b, double tS); // for tests and keyboard
    // Vertical event markers (session time in ms, label); replaces the previous set.
    void setMarkers(const QVector<QPair<double, QString>> &markers);

Q_SIGNALS:
    void cursorsChanged();
    void followChanged(bool follow);

protected:
    bool eventFilter(QObject *o, QEvent *e) override;

private:
    struct Trace {
        QString channel;
        QwtPlotCurve *curve;
    };
    QVector<Trace> m_traces;
    QwtPlotGrid *m_grid;
    QwtPlotZoomer *m_zoomer;
    QwtPlotPanner *m_panner;
    QwtPlotMagnifier *m_magnifier;
    QwtPlotMarker *m_ca, *m_cb;
    QVector<QwtPlotMarker *> m_events;
    bool m_haveA = false, m_haveB = false;
    bool m_cursorMode = false;
    bool m_follow = true;
    double m_windowMs = 10000.0;
    QLabel *m_overlay;
};

// id/iq plane: the trajectory of the measured dq currents, the operating and reference points, the current circle
// (the current allowance, amplitude-invariant dq = phase peak) and the voltage ellipse at the present speed:
// (Ld·id + ψ)² + (Lq·iq)² ≤ (Vmax/ωe)².
class DqPlot : public QwtPlot
{
    Q_OBJECT
public:
    explicit DqPlot(QWidget *parent = nullptr);
    void refresh(const TelemetryStore &s, const TelemetryFrame &last, const QJsonObject &hello, double seconds);
    void applyTheme();
    QString note() const { return m_note; }

private:
    QwtPlotCurve *m_traj, *m_circle, *m_ellipse;
    QwtPlotMarker *m_op, *m_ref;
    QwtPlotGrid *m_grid;
    QwtPlotRescaler *m_rescaler;
    QString m_note;
};

// Amplitude spectrum of one channel (Dsp::spectrum), with the dominant peak marked.
class SpectrumPlot : public QwtPlot
{
    Q_OBJECT
public:
    explicit SpectrumPlot(QWidget *parent = nullptr);
    // Returns a one-line summary (sample rate, resolution, peak) or the reason nothing was computed.
    QString compute(const TelemetryStore &s, const QString &channel, int samples, bool logScale);
    void applyTheme();

private:
    QwtPlotCurve *m_curve;
    QwtPlotMarker *m_peak;
    QwtPlotGrid *m_grid;
};
