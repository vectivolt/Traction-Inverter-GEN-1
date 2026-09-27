// AlertEngine — user rules on any telemetry channel: "<channel> <op> <threshold>" held for hold_ms (frame time)
// raises an alert; it clears once the condition is false beyond the hysteresis. A channel without a value (absent
// or NaN) changes nothing: a stale or missing reading never clears an alarm.
#pragma once

#include "Telemetry.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

struct AlertRule {
    enum class Op { Gt, Ge, Lt, Le, Eq, Ne, AbsGt };
    QString id; // stable key
    QString channel;
    Op op = Op::Gt;
    double threshold = 0.0;
    double hysteresis = 0.0;
    double holdMs = 0.0;
    QString severity = QStringLiteral("warn"); // info, warn, crit
    bool enabled = true;
    QString note;

    bool holds(double v) const;   // the raise condition
    bool clears(double v) const;  // the clear condition (with hysteresis)
    QString describe() const;     // "motion.speed_rpm > 9000 for 200 ms"
    QJsonObject toJson() const;
    static AlertRule fromJson(const QJsonObject &o);
    static QString opText(Op op);
    static Op opFromText(const QString &s);
};

struct AlertEvent {
    QString ruleId;
    QString channel;
    QString severity;
    QString text;
    double value = 0.0;
    double tMs = 0.0;
    qint64 rxMs = 0;
    bool raised = true; // false: cleared
};

class AlertEngine
{
public:
    const QVector<AlertRule> &rules() const { return m_rules; }
    void setRules(const QVector<AlertRule> &r);
    void addRule(const AlertRule &r);
    void removeRule(const QString &id);
    bool isActive(const QString &id) const;
    QVector<AlertEvent> evaluate(const TelemetryFrame &f);
    QJsonArray toJson() const;
    void fromJson(const QJsonArray &a);
    const QVector<AlertEvent> &log() const { return m_log; }

private:
    struct RuleState {
        bool active = false;
        bool pending = false;
        double since = 0.0;
    };
    QVector<AlertRule> m_rules;
    QHash<QString, RuleState> m_state;
    QVector<AlertEvent> m_log;
};
