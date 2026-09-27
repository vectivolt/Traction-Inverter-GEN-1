// BugReport — one JSON file with everything needed to reproduce a problem: the tool and firmware identity, the
// transport, the device description ("hello"), the latest frame, the last N seconds of every telemetry channel
// (columnar), the DTC list and history, the parameters (device, edited, default) with the change log, the alert
// rules and their log, and the session's event log.
#pragma once

#include <QJsonObject>
#include <QString>

class Session;

namespace BugReport {
QJsonObject build(Session &s, double lastSeconds, const QString &note);
bool save(const QString &path, const QJsonObject &report, QString *err);
} // namespace BugReport
