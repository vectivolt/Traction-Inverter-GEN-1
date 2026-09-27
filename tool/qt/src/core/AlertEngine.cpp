#include "AlertEngine.h"

#include <QUuid>

#include <algorithm>
#include <cmath>

namespace {
const char *const OPS[] = {">", ">=", "<", "<=", "==", "!=", "|x|>"};
}

QString AlertRule::opText(Op op) { return QLatin1String(OPS[static_cast<int>(op)]); }

AlertRule::Op AlertRule::opFromText(const QString &s)
{
    for (int i = 0; i < 7; i++) {
        if (s == QLatin1String(OPS[i])) {
            return static_cast<Op>(i);
        }
    }
    return Op::Gt;
}

bool AlertRule::holds(double v) const
{
    switch (op) {
    case Op::Gt: return v > threshold;
    case Op::Ge: return v >= threshold;
    case Op::Lt: return v < threshold;
    case Op::Le: return v <= threshold;
    case Op::Eq: return std::abs(v - threshold) <= std::max(hysteresis, 1e-9);
    case Op::Ne: return std::abs(v - threshold) > std::max(hysteresis, 1e-9);
    case Op::AbsGt: return std::abs(v) > threshold;
    }
    return false;
}

bool AlertRule::clears(double v) const
{
    const double h = std::abs(hysteresis);
    switch (op) {
    case Op::Gt:
    case Op::Ge: return v < threshold - h || (h == 0.0 && !holds(v));
    case Op::Lt:
    case Op::Le: return v > threshold + h || (h == 0.0 && !holds(v));
    case Op::AbsGt: return std::abs(v) < threshold - h || (h == 0.0 && !holds(v));
    case Op::Eq:
    case Op::Ne: return !holds(v);
    }
    return true;
}

QString AlertRule::describe() const
{
    QString s = QStringLiteral("%1 %2 %3").arg(channel, opText(op), QString::number(threshold));
    if (holdMs > 0.0) {
        s += QStringLiteral(" for %1 ms").arg(holdMs);
    }
    return s;
}

QJsonObject AlertRule::toJson() const
{
    return {{QStringLiteral("id"), id},
            {QStringLiteral("channel"), channel},
            {QStringLiteral("op"), opText(op)},
            {QStringLiteral("threshold"), threshold},
            {QStringLiteral("hysteresis"), hysteresis},
            {QStringLiteral("hold_ms"), holdMs},
            {QStringLiteral("severity"), severity},
            {QStringLiteral("enabled"), enabled},
            {QStringLiteral("note"), note}};
}

AlertRule AlertRule::fromJson(const QJsonObject &o)
{
    AlertRule r;
    r.id = o.value(QLatin1String("id")).toString();
    if (r.id.isEmpty()) {
        r.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    r.channel = o.value(QLatin1String("channel")).toString();
    r.op = opFromText(o.value(QLatin1String("op")).toString());
    r.threshold = o.value(QLatin1String("threshold")).toDouble();
    r.hysteresis = o.value(QLatin1String("hysteresis")).toDouble();
    r.holdMs = o.value(QLatin1String("hold_ms")).toDouble();
    r.severity = o.value(QLatin1String("severity")).toString(QStringLiteral("warn"));
    r.enabled = o.value(QLatin1String("enabled")).toBool(true);
    r.note = o.value(QLatin1String("note")).toString();
    return r;
}

void AlertEngine::setRules(const QVector<AlertRule> &r)
{
    m_rules = r;
    m_state.clear();
}

void AlertEngine::addRule(const AlertRule &r)
{
    AlertRule x = r;
    if (x.id.isEmpty()) {
        x.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    m_rules.push_back(x);
}

void AlertEngine::removeRule(const QString &id)
{
    m_rules.erase(std::remove_if(m_rules.begin(), m_rules.end(), [&](const AlertRule &r) { return r.id == id; }),
                  m_rules.end());
    m_state.remove(id);
}

bool AlertEngine::isActive(const QString &id) const { return m_state.value(id).active; }

QVector<AlertEvent> AlertEngine::evaluate(const TelemetryFrame &f)
{
    QVector<AlertEvent> out;
    for (const AlertRule &r : m_rules) {
        if (!r.enabled) {
            continue;
        }
        const auto it = f.num.constFind(r.channel);
        if (it == f.num.constEnd() || std::isnan(it.value())) {
            continue;
        }
        const double v = it.value();
        RuleState &st = m_state[r.id];
        auto event = [&](bool raised) {
            AlertEvent e;
            e.ruleId = r.id;
            e.channel = r.channel;
            e.severity = r.severity;
            e.value = v;
            e.tMs = f.tMs;
            e.rxMs = f.rxMs;
            e.raised = raised;
            e.text = raised ? QStringLiteral("%1 (value %2)").arg(r.describe(), QString::number(v, 'g', 6))
                            : QStringLiteral("cleared: %1 (value %2)").arg(r.describe(), QString::number(v, 'g', 6));
            if (!r.note.isEmpty()) {
                e.text += QStringLiteral(" — ") + r.note;
            }
            out.push_back(e);
        };
        if (!st.active) {
            if (r.holds(v)) {
                if (!st.pending) {
                    st.pending = true;
                    st.since = f.tMs;
                }
                if (f.tMs - st.since >= r.holdMs) {
                    st.active = true;
                    st.pending = false;
                    event(true);
                }
            } else {
                st.pending = false;
            }
        } else if (r.clears(v)) {
            st.active = false;
            event(false);
        }
    }
    m_log += out;
    if (m_log.size() > 2000) {
        m_log.remove(0, m_log.size() - 2000);
    }
    return out;
}

QJsonArray AlertEngine::toJson() const
{
    QJsonArray a;
    for (const AlertRule &r : m_rules) {
        a.append(r.toJson());
    }
    return a;
}

void AlertEngine::fromJson(const QJsonArray &a)
{
    QVector<AlertRule> r;
    for (const QJsonValue &v : a) {
        r.push_back(AlertRule::fromJson(v.toObject()));
    }
    setRules(r);
}
