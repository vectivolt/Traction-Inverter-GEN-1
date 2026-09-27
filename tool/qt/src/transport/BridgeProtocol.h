// BridgeProtocol — the simulator bridge's newline-delimited JSON (tool/PROTOCOL.md): stdout lines are objects with a
// "type" (hello, tel, ack, log, params, can, golden); stdin takes one FLAT object per line {"cmd": ..., "id": ...,
// <scalar args>} (the bridge's reader accepts no nesting).
#pragma once

#include "ITransport.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace BridgeProtocol {

enum class Type { Invalid, Hello, Tel, Ack, Log, Params, Can, Golden, Other };

struct Message {
    Type type = Type::Invalid;
    QJsonObject obj;
    QString error; // for Invalid
};

Message parse(const QByteArray &line);
// A command line (no trailing newline); empty with *err set when an argument is not a scalar, cmd is empty, there
// are more than 24 keys or the line exceeds 4095 bytes (PROTOCOL.md A.2/A.4).
QByteArray encodeCommand(const QString &cmd, const QJsonObject &args, quint64 id, QString *err);
Ack toAck(const QJsonObject &o);
// A "tel" object as a frame; the INV_STATUS bytes in "can" (when present and valid) add the "inv.*" channels.
TelemetryFrame toFrame(const QJsonObject &tel);

} // namespace BridgeProtocol
