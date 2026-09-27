// Timeline — the fault history over session time: one lane per DTC that occurred, a bar from each raise to its
// clear (open while still active), a tick per repeated occurrence, and the alert events on their own lane.
#pragma once

#include "AlertEngine.h"
#include "DtcTracker.h"

#include <QWidget>

class Timeline : public QWidget
{
    Q_OBJECT
public:
    explicit Timeline(QWidget *parent = nullptr);
    void setData(const QVector<DtcEvent> &dtc, const QVector<AlertEvent> &alerts, double tFirst, double tNow);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return {400, 90}; }

protected:
    void paintEvent(QPaintEvent *) override;
    bool event(QEvent *e) override;

private:
    struct Span {
        double t0, t1; // t1 NaN: still active
    };
    struct Lane {
        int id; // -1: alerts
        QString name;
        QString cls;
        QVector<Span> spans;
        QVector<double> ticks;
    };
    QVector<Lane> m_lanes;
    double m_t0 = 0, m_t1 = 1;
    double xOf(double t, double left, double width) const;
};
