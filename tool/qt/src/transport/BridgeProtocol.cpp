#include "BridgeProtocol.h"

#include "CanCodec.h"

#include <QJsonDocument>
#include <QJsonParseError>

namespace BridgeProtocol {

Message parse(const QByteArray &line)
{
    Message m;
    const QByteArray t = line.trimmed();
    if (t.isEmpty()) {
        m.error = QStringLiteral("empty line");
        return m;
    }
    QJsonParseError pe;
    const QJsonDocument d = QJsonDocument::fromJson(t, &pe);
    if (pe.error != QJsonParseError::NoError || !d.isObject()) {
        m.error = (pe.error != QJsonParseError::NoError) ? pe.errorString() : QStringLiteral("not a JSON object");
        return m;
    }
    m.obj = d.object();
    const QString type = m.obj.value(QLatin1String("type")).toString();
    if (type == QLatin1String("tel")) {
        m.type = Type::Tel;
    } else if (type == QLatin1String("ack")) {
        m.type = Type::Ack;
    } else if (type == QLatin1String("hello")) {
        m.type = Type::Hello;
    } else if (type == QLatin1String("log")) {
        m.type = Type::Log;
    } else if (type == QLatin1String("params")) {
        m.type = Type::Params;
    } else if (type == QLatin1String("can")) {
        m.type = Type::Can;
    } else if (type == QLatin1String("golden")) {
        m.type = Type::Golden;
    } else {
        m.type = Type::Other;
    }
    return m;
}

QByteArray encodeCommand(const QString &cmd, const QJsonObject &args, quint64 id, QString *err)
{
    if (cmd.trimmed().isEmpty()) {
        *err = QStringLiteral("empty command");
        return {};
    }
    QJsonObject o;
    for (auto it = args.begin(); it != args.end(); ++it) {
        const QJsonValue v = it.value();
        if (!(v.isString() || v.isDouble() || v.isBool() || v.isNull())) {
            *err = QStringLiteral("argument '%1' is not a string, number, boolean or null (the bridge takes flat objects)")
                       .arg(it.key());
            return {};
        }
        o.insert(it.key(), v);
    }
    o.insert(QLatin1String("cmd"), cmd);
    o.insert(QLatin1String("id"), static_cast<double>(id));
    if (o.size() > 24) { // PROTOCOL.md A.4: at most 24 keys
        *err = QStringLiteral("more than 24 keys");
        return {};
    }
    const QByteArray line = QJsonDocument(o).toJson(QJsonDocument::Compact);
    if (line.size() > 4095) { // PROTOCOL.md A.2: longer lines are dropped by the bridge
        *err = QStringLiteral("the command line is longer than 4095 bytes");
        return {};
    }
    return line;
}

Ack toAck(const QJsonObject &o)
{
    Ack a;
    a.id = static_cast<quint64>(o.value(QLatin1String("id")).toDouble(0.0));
    a.cmd = o.value(QLatin1String("cmd")).toString();
    a.ok = o.value(QLatin1String("ok")).toBool(false);
    a.message = o.value(QLatin1String(a.ok ? "info" : "err")).toString();
    if (a.message.isEmpty() && o.contains(QLatin1String("rsp"))) {
        a.message = o.value(QLatin1String("rsp")).toString();
    }
    a.raw = o;
    return a;
}

TelemetryFrame toFrame(const QJsonObject &tel)
{
    TelemetryFrame f = Telemetry::fromJson(tel, QStringLiteral("sim"));
    CanCodec::addChannelsFromHexText(f);
    return f;
}

} // namespace BridgeProtocol
