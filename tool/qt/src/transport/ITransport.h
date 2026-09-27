// ITransport — what the application talks to: a simulator bridge process, a recorded log, or a CAN adapter.
// Frames come out as TelemetryFrame (one vocabulary for all three); commands go in as a name plus flat arguments
// (the bridge's command set, tool/PROTOCOL.md); every command is answered by exactly one Ack with its id — a
// transport that cannot do something refuses it with ok = false and a reason, it never ignores it.
#pragma once

#include "Telemetry.h"

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

struct Ack {
    quint64 id = 0;
    QString cmd;
    bool ok = false;
    QString message; // "info" when ok, "err" otherwise
    QJsonObject raw;
};

class ITransport : public QObject
{
    Q_OBJECT
public:
    enum class Kind { Bridge, Replay, Can };
    enum class State { Closed, Opening, Open, Failed };
    Q_ENUM(State)

    using QObject::QObject;
    ~ITransport() override = default;

    virtual Kind kind() const = 0;
    virtual QString name() const = 0; // one line for the status bar
    virtual void open() = 0;
    virtual void close() = 0;
    virtual bool isSimulator() const { return false; } // fault injection and plant commands exist
    virtual bool isReplay() const { return false; }
    virtual QStringList commands() const = 0;
    // Queues a command; returns its id. The Ack arrives through ack() (possibly before this returns for a refusal).
    virtual quint64 send(const QString &cmd, const QJsonObject &args = {}) = 0;
    // Wall-clock period at which frames are expected (the staleness judgement); 0 = unknown.
    virtual double expectedPeriodMs() const { return 0.0; }

    State state() const { return m_state; }

Q_SIGNALS:
    void stateChanged(ITransport::State state, const QString &detail);
    void frame(const TelemetryFrame &f);
    void info(const QJsonObject &info); // device description: the bridge "hello"/"params", CAN adapter details
    void ack(const Ack &a);
    void log(const QString &level, const QString &message);
    void traffic(const QString &direction, const QString &line); // raw lines/frames for the terminal ("tx", "rx", "can")

protected:
    void setState(State s, const QString &detail = {})
    {
        m_state = s;
        Q_EMIT stateChanged(s, detail);
    }
    quint64 nextId() { return ++m_lastId; }
    void refuse(quint64 id, const QString &cmd, const QString &why)
    {
        Ack a;
        a.id = id;
        a.cmd = cmd;
        a.ok = false;
        a.message = why;
        Q_EMIT ack(a);
    }
    void accept(quint64 id, const QString &cmd, const QString &info = {})
    {
        Ack a;
        a.id = id;
        a.cmd = cmd;
        a.ok = true;
        a.message = info;
        Q_EMIT ack(a);
    }

private:
    State m_state = State::Closed;
    quint64 m_lastId = 0;
};
