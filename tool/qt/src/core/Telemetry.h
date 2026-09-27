// Telemetry — one frame of named channels, whatever the transport, and the columnar ring store behind the plots.
// Channel names are the simulator bridge's JSON paths flattened with dots ("motion.speed_rpm"); decoded INV_STATUS
// fields are "inv.*" (CanCodec), so a CAN session and a simulator session share one vocabulary.
#pragma once

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cmath>
#include <initializer_list>
#include <limits>

struct DtcState {
    int id = 0;
    int status = 0; // ISO 14229-1 status byte (dtc.h DTC_TF ... DTC_TNCTOC)
    int occ = 0;
    double firstMs = 0; // firmware ms (hal_time_ms)
    double lastMs = 0;
    bool active() const { return (status & 0x01) != 0; }
    bool confirmed() const { return (status & 0x08) != 0; }
};

struct TelemetryFrame {
    double tMs = 0;   // source time (simulator session ms, replay log time, CAN receive time)
    qint64 rxMs = 0;  // host monotonic receive time (Session clock), set by the Session
    QString source;   // "sim", "can", "replay"
    QHash<QString, double> num;
    QHash<QString, QString> text;
    QVector<DtcState> dtcs;
    bool dtcListComplete = false; // true: dtcs is every DTC with an occurrence (the simulator); false: a summary (CAN)
    QJsonObject raw;

    static constexpr double nan() { return std::numeric_limits<double>::quiet_NaN(); }
    bool has(const QString &k) const { return num.contains(k); }
    double v(const QString &k, double def = nan()) const { return num.value(k, def); }
    bool b(const QString &k) const
    {
        const double x = num.value(k, 0.0);
        return !std::isnan(x) && x != 0.0;
    }
    // The first channel present, e.g. pick({"motion.speed_rpm", "inv.speed_rpm"}).
    double pick(std::initializer_list<const char *> keys, double def = nan()) const;
    QString str(const QString &k) const { return text.value(k); }
};

namespace Telemetry {
// Flattens a bridge-style "tel" object (nested objects -> dotted names, bools -> 0/1, null -> NaN, the "dtc"
// array -> DtcState). The time is "t_ms"; the source "src" (or defaultSource).
TelemetryFrame fromJson(const QJsonObject &tel, const QString &defaultSource);
// A frame as a flat JSON object ({"type":"tel","t_ms":..,"src":..,"<channel>":value,...}); raw frames keep their
// original structure. Used by the recorder for sources without a raw object (CAN).
QJsonObject toJson(const TelemetryFrame &f);
} // namespace Telemetry

// Ring store: one time column and one value column per channel, all the same length; a channel first seen later
// is back-filled with NaN. Capacity in samples (default: 200 s at 100 Hz).
class TelemetryStore
{
public:
    explicit TelemetryStore(int capacity = 20000);
    void append(const TelemetryFrame &f);
    void clear();
    int size() const { return m_count; }
    int capacity() const { return m_cap; }
    QStringList channels() const;
    bool hasChannel(const QString &ch) const { return m_index.contains(ch); }
    double firstT() const;
    double lastT() const;
    // i = 0 is the oldest sample
    double t(int i) const { return m_t[slot(i)]; }
    double value(const QString &ch, int i) const;
    // First index with t >= tMs (binary search; size() if none).
    int lowerBound(double tMs) const;
    // Samples with t in [t0, t1], min/max-decimated to at most maxPoints (0 = all).
    void series(const QString &ch, double t0, double t1, QVector<double> &xs, QVector<double> &ys,
                int maxPoints = 0) const;

private:
    int slot(int i) const { return (m_head + i) % m_cap; }
    int m_cap;
    int m_head = 0; // slot of the oldest sample
    int m_count = 0;
    QVector<double> m_t;
    QHash<QString, int> m_index;
    QStringList m_names;
    QVector<QVector<double>> m_cols;
};
