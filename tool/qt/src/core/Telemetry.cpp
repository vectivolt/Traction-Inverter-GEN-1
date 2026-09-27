#include "Telemetry.h"

#include <QJsonArray>

#include <algorithm>

double TelemetryFrame::pick(std::initializer_list<const char *> keys, double def) const
{
    for (const char *k : keys) {
        const auto it = num.constFind(QLatin1String(k));
        if (it != num.constEnd()) {
            return it.value();
        }
    }
    return def;
}

namespace {
void flatten(const QJsonObject &o, const QString &prefix, TelemetryFrame &f)
{
    for (auto it = o.begin(); it != o.end(); ++it) {
        const QString key = prefix.isEmpty() ? it.key() : prefix + QLatin1Char('.') + it.key();
        const QJsonValue v = it.value();
        switch (v.type()) {
        case QJsonValue::Double: f.num.insert(key, v.toDouble()); break;
        case QJsonValue::Bool: f.num.insert(key, v.toBool() ? 1.0 : 0.0); break;
        case QJsonValue::Null: f.num.insert(key, TelemetryFrame::nan()); break;
        case QJsonValue::String: f.text.insert(key, v.toString()); break;
        case QJsonValue::Object: flatten(v.toObject(), key, f); break;
        case QJsonValue::Array: {
            QStringList parts; // arrays of strings (e.g. plant.inject) become one text channel
            for (const QJsonValue &e : v.toArray()) {
                if (e.isString()) {
                    parts << e.toString();
                }
            }
            f.text.insert(key, parts.join(QLatin1Char(',')));
            break;
        }
        default: break;
        }
    }
}
} // namespace

namespace Telemetry {
TelemetryFrame fromJson(const QJsonObject &tel, const QString &defaultSource)
{
    TelemetryFrame f;
    f.raw = tel;
    f.tMs = tel.value(QLatin1String("t_ms")).toDouble();
    f.source = tel.value(QLatin1String("src")).toString(defaultSource);
    QJsonObject rest = tel;
    for (const char *k : {"type", "t_ms", "src", "dtc", "v"}) {
        rest.remove(QLatin1String(k));
    }
    flatten(rest, QString(), f);
    const QJsonValue d = tel.value(QLatin1String("dtc"));
    if (d.isArray()) {
        f.dtcListComplete = true;
        for (const QJsonValue &e : d.toArray()) {
            const QJsonArray a = e.toArray(); // [id, status, occurrences, first_ms, last_ms]
            if (a.size() >= 5) {
                f.dtcs.push_back({a[0].toInt(), a[1].toInt(), a[2].toInt(), a[3].toDouble(), a[4].toDouble()});
            }
        }
    }
    return f;
}

QJsonObject toJson(const TelemetryFrame &f)
{
    if (!f.raw.isEmpty()) {
        return f.raw;
    }
    QJsonObject o;
    o.insert(QLatin1String("type"), QLatin1String("tel"));
    o.insert(QLatin1String("t_ms"), f.tMs);
    o.insert(QLatin1String("src"), f.source);
    for (auto it = f.num.begin(); it != f.num.end(); ++it) {
        o.insert(it.key(), std::isnan(it.value()) ? QJsonValue() : QJsonValue(it.value()));
    }
    for (auto it = f.text.begin(); it != f.text.end(); ++it) {
        o.insert(it.key(), it.value());
    }
    if (!f.dtcs.isEmpty()) {
        QJsonArray a;
        for (const DtcState &d : f.dtcs) {
            a.append(QJsonArray{d.id, d.status, d.occ, d.firstMs, d.lastMs});
        }
        o.insert(QLatin1String("dtc"), a);
    }
    return o;
}
} // namespace Telemetry

TelemetryStore::TelemetryStore(int capacity) : m_cap(std::max(16, capacity)), m_t(m_cap, 0.0) {}

void TelemetryStore::clear()
{
    m_head = 0;
    m_count = 0;
    m_index.clear();
    m_names.clear();
    m_cols.clear();
}

void TelemetryStore::append(const TelemetryFrame &f)
{
    int s;
    if (m_count < m_cap) {
        s = slot(m_count);
        m_count++;
    } else {
        s = m_head; // overwrite the oldest
        m_head = (m_head + 1) % m_cap;
    }
    m_t[s] = f.tMs;
    for (QVector<double> &c : m_cols) {
        c[s] = TelemetryFrame::nan();
    }
    for (auto it = f.num.begin(); it != f.num.end(); ++it) {
        auto idx = m_index.constFind(it.key());
        if (idx == m_index.constEnd()) {
            idx = m_index.insert(it.key(), static_cast<int>(m_cols.size()));
            m_names << it.key();
            m_cols.push_back(QVector<double>(m_cap, TelemetryFrame::nan()));
        }
        m_cols[idx.value()][s] = it.value();
    }
}

QStringList TelemetryStore::channels() const
{
    QStringList n = m_names;
    n.sort();
    return n;
}

double TelemetryStore::firstT() const { return m_count ? t(0) : TelemetryFrame::nan(); }
double TelemetryStore::lastT() const { return m_count ? t(m_count - 1) : TelemetryFrame::nan(); }

double TelemetryStore::value(const QString &ch, int i) const
{
    const auto idx = m_index.constFind(ch);
    return (idx == m_index.constEnd() || i < 0 || i >= m_count) ? TelemetryFrame::nan() : m_cols[idx.value()][slot(i)];
}

int TelemetryStore::lowerBound(double tMs) const
{
    int lo = 0;
    int hi = m_count;
    while (lo < hi) {
        const int mid = (lo + hi) / 2;
        if (t(mid) < tMs) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

void TelemetryStore::series(const QString &ch, double t0, double t1, QVector<double> &xs, QVector<double> &ys,
                            int maxPoints) const
{
    xs.clear();
    ys.clear();
    const auto idx = m_index.constFind(ch);
    if (idx == m_index.constEnd() || m_count == 0) {
        return;
    }
    const QVector<double> &col = m_cols[idx.value()];
    const int i0 = lowerBound(t0);
    int i1 = i0;
    while (i1 < m_count && t(i1) <= t1) {
        i1++;
    }
    const int n = i1 - i0;
    if (maxPoints <= 0 || n <= maxPoints) {
        xs.reserve(n);
        ys.reserve(n);
        for (int i = i0; i < i1; i++) {
            xs.push_back(t(i));
            ys.push_back(col[slot(i)]);
        }
        return;
    }
    // min/max per bucket keeps every peak visible (ponytail: fixed buckets, an LTTB pass if curves ever need it)
    const int buckets = std::max(1, maxPoints / 2);
    xs.reserve(2 * buckets);
    ys.reserve(2 * buckets);
    for (int b = 0; b < buckets; b++) {
        const int a = i0 + static_cast<int>(static_cast<qint64>(n) * b / buckets);
        const int e = i0 + static_cast<int>(static_cast<qint64>(n) * (b + 1) / buckets);
        int iMin = -1;
        int iMax = -1;
        for (int i = a; i < e; i++) {
            const double y = col[slot(i)];
            if (std::isnan(y)) {
                continue;
            }
            if (iMin < 0 || y < col[slot(iMin)]) {
                iMin = i;
            }
            if (iMax < 0 || y > col[slot(iMax)]) {
                iMax = i;
            }
        }
        if (iMin < 0) {
            continue;
        }
        const int first = std::min(iMin, iMax);
        const int second = std::max(iMin, iMax);
        xs.push_back(t(first));
        ys.push_back(col[slot(first)]);
        if (second != first) {
            xs.push_back(t(second));
            ys.push_back(col[slot(second)]);
        }
    }
}
