#include "BugReport.h"

#include "ProtocolInfo.h"
#include "Session.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSysInfo>

#include <cmath>

namespace BugReport {

QJsonObject build(Session &s, double lastSeconds, const QString &note)
{
    const ProtocolInfo &pi = ProtocolInfo::instance();
    QJsonObject r;
    r.insert(QLatin1String("format"), QLatin1String("traction-tool-bug-report"));
    r.insert(QLatin1String("version"), 1);
    r.insert(QLatin1String("created"), QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
    r.insert(QLatin1String("note"), note);
    r.insert(QLatin1String("tool"), QJsonObject{{QStringLiteral("version"), QStringLiteral(TT_VERSION)},
                                                {QStringLiteral("qt"), QString::fromLatin1(qVersion())},
                                                {QStringLiteral("os"), QSysInfo::prettyProductName()},
                                                {QStringLiteral("protocol_fw_id"), pi.fwId}});
    QJsonObject link{{QStringLiteral("state"), Session::linkText(s.link())},
                     {QStringLiteral("age_ms"), static_cast<double>(s.ageMs())},
                     {QStringLiteral("stale_after_ms"), static_cast<double>(s.staleAfterMs())}};
    if (ITransport *t = s.transport()) {
        link.insert(QLatin1String("transport"), t->name());
    }
    r.insert(QLatin1String("link"), link);
    r.insert(QLatin1String("fw_id"), s.deviceInfo().value(QLatin1String("fw_id")).toString(QStringLiteral("unknown")));
    r.insert(QLatin1String("device"), s.deviceInfo());
    if (s.hasFrame()) {
        r.insert(QLatin1String("state"), Telemetry::toJson(s.last()));
    }
    // telemetry: columnar, the last N seconds of the store
    TelemetryStore &st = s.store();
    QJsonObject tel;
    if (st.size() > 0) {
        const double t1 = st.lastT();
        const int i0 = st.lowerBound(t1 - lastSeconds * 1000.0);
        QJsonArray t;
        for (int i = i0; i < st.size(); i++) {
            t.append(st.t(i));
        }
        QJsonObject cols;
        for (const QString &ch : st.channels()) {
            QJsonArray c;
            for (int i = i0; i < st.size(); i++) {
                const double v = st.value(ch, i);
                c.append(std::isnan(v) ? QJsonValue() : QJsonValue(v));
            }
            cols.insert(ch, c);
        }
        tel.insert(QLatin1String("seconds"), lastSeconds);
        tel.insert(QLatin1String("t_ms"), t);
        tel.insert(QLatin1String("channels"), cols);
    }
    r.insert(QLatin1String("telemetry"), tel);
    QJsonArray active;
    for (const DtcState &d : s.dtcs().current()) {
        const DtcInfo *info = pi.dtc(d.id);
        active.append(QJsonObject{{QStringLiteral("id"), d.id},
                                  {QStringLiteral("name"), pi.dtcName(d.id)},
                                  {QStringLiteral("code"), info ? info->code : QString()},
                                  {QStringLiteral("status"), d.status},
                                  {QStringLiteral("active"), d.active()},
                                  {QStringLiteral("occurrences"), d.occ},
                                  {QStringLiteral("first_ms"), d.firstMs},
                                  {QStringLiteral("last_ms"), d.lastMs}});
    }
    QJsonArray hist;
    for (const DtcEvent &e : s.dtcs().history()) {
        static const char *const K[] = {"raised", "cleared", "again"};
        hist.append(QJsonObject{{QStringLiteral("t_ms"), e.tMs},
                                {QStringLiteral("id"), e.id},
                                {QStringLiteral("name"), pi.dtcName(e.id)},
                                {QStringLiteral("event"), QLatin1String(K[static_cast<int>(e.kind)])}});
    }
    r.insert(QLatin1String("dtcs"), QJsonObject{{QStringLiteral("list"), active},
                                               {QStringLiteral("complete"), s.dtcs().complete()},
                                               {QStringLiteral("history"), hist}});
    ParamModel &pm = s.params();
    QJsonArray params;
    for (int i = 0; i < pm.rowCount(); i++) {
        const CalRow &c = pm.row(i);
        params.append(QJsonObject{{QStringLiteral("name"), c.name},
                                  {QStringLiteral("value"), pm.effective(i)},
                                  {QStringLiteral("device"), pm.haveDevice() ? QJsonValue(pm.device(i)) : QJsonValue()},
                                  {QStringLiteral("default"), c.def},
                                  {QStringLiteral("dirty"), pm.isDirty(i)}});
    }
    r.insert(QLatin1String("parameters"), QJsonObject{{QStringLiteral("rows"), params},
                                                     {QStringLiteral("problems"), QJsonArray::fromStringList(pm.validate())},
                                                     {QStringLiteral("change_log"), pm.changeLogJson()}});
    QJsonArray alog;
    for (const AlertEvent &a : s.alerts().log()) {
        alog.append(QJsonObject{{QStringLiteral("t_ms"), a.tMs},
                                {QStringLiteral("raised"), a.raised},
                                {QStringLiteral("severity"), a.severity},
                                {QStringLiteral("text"), a.text}});
    }
    r.insert(QLatin1String("alerts"), QJsonObject{{QStringLiteral("rules"), s.alerts().toJson()},
                                                 {QStringLiteral("log"), alog}});
    QJsonArray ev;
    for (const LogEntry &e : s.events()) {
        ev.append(QJsonObject{{QStringLiteral("when"), e.when.toString(Qt::ISODateWithMs)},
                              {QStringLiteral("level"), e.level},
                              {QStringLiteral("text"), e.text}});
    }
    r.insert(QLatin1String("events"), ev);
    return r;
}

bool save(const QString &path, const QJsonObject &report, QString *err)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        *err = f.errorString();
        return false;
    }
    f.write(QJsonDocument(report).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        *err = f.errorString();
        return false;
    }
    return true;
}

} // namespace BugReport
