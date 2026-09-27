#include "BridgeTransport.h"

#include "BridgeProtocol.h"
#include "CanCodec.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#include <algorithm>

BridgeTransport::BridgeTransport(Options o, QObject *parent)
    : ITransport(parent), m_opt(std::move(o)), m_tx([this](const QByteArray &f, bool replyDue) { return frameOut(f, replyDue); })
{
    connect(&m_tx, &IsoTpSender::sent, this, [this](const QJsonObject &info) {
        m_txResponse = true;
        m_txInfo = info;
    });
    connect(&m_tx, &IsoTpSender::failed, this, [this](const QString &why, const QJsonObject &info) {
        Ack a;
        a.id = m_txId;
        a.cmd = QStringLiteral("uds");
        a.message = why;
        a.raw = info;
        endSegmented();
        Q_EMIT ack(a);
    });
    connect(&m_proc, &QProcess::readyReadStandardOutput, this, &BridgeTransport::onStdout);
    connect(&m_proc, &QProcess::readyReadStandardError, this, [this] {
        const QString err = QString::fromUtf8(m_proc.readAllStandardError()).trimmed();
        if (!err.isEmpty()) {
            Q_EMIT log(QStringLiteral("warn"), QStringLiteral("bridge stderr: %1").arg(err));
        }
    });
    connect(&m_proc, &QProcess::started, this, [this] { setState(State::Open, name()); });
    connect(&m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (!m_closing && e == QProcess::FailedToStart) {
            setState(State::Failed, QStringLiteral("cannot start %1: %2").arg(m_opt.program, m_proc.errorString()));
        }
    });
    connect(&m_proc, &QProcess::finished, this, [this](int code, QProcess::ExitStatus st) {
        m_tx.abort();
        endSegmented();
        m_udsOut.clear();
        if (m_closing) {
            setState(State::Closed);
        } else {
            setState(State::Failed, QStringLiteral("the bridge exited (%1, code %2)")
                                        .arg(st == QProcess::CrashExit ? QStringLiteral("crashed") : QStringLiteral("normal"))
                                        .arg(code));
        }
    });
}

BridgeTransport::~BridgeTransport()
{
    m_closing = true;
    if (m_proc.state() != QProcess::NotRunning) {
        m_proc.closeWriteChannel(); // EOF on stdin: the bridge exits
        if (!m_proc.waitForFinished(1000)) {
            m_proc.kill();
            m_proc.waitForFinished(1000);
        }
    }
}

QString BridgeTransport::locate()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {qEnvironmentVariable("TT_SIM_BRIDGE"), appDir + QStringLiteral("/sim_bridge"),
                                    appDir + QStringLiteral("/../Resources/sim_bridge"),
                                    QStringLiteral(TT_DEV_SIM_BRIDGE)};
    for (const QString &c : candidates) {
        if (!c.isEmpty() && QFileInfo(c).isExecutable() && QFileInfo(c).isFile()) {
            return QDir::cleanPath(c);
        }
    }
    return {};
}

QString BridgeTransport::name() const
{
    return QStringLiteral("Simulator bridge · %1 · %2 Hz · ×%3")
        .arg(m_opt.sku.toUpper(), QString::number(m_opt.rateHz), QString::number(m_opt.timeFactor));
}

void BridgeTransport::open()
{
    if (m_proc.state() != QProcess::NotRunning) {
        return;
    }
    if (m_opt.program.isEmpty() || !QFileInfo(m_opt.program).isExecutable()) {
        setState(State::Failed, QStringLiteral("simulator bridge not found (%1) — build tool/bridge (make) or set "
                                               "TT_SIM_BRIDGE").arg(m_opt.program.isEmpty() ? QStringLiteral("no path")
                                                                                            : m_opt.program));
        return;
    }
    QStringList args = {QStringLiteral("--sku"), m_opt.sku, QStringLiteral("--rate"), QString::number(m_opt.rateHz),
                        QStringLiteral("--time"), QString::number(m_opt.timeFactor)};
    if (m_opt.paused) {
        args << QStringLiteral("--paused");
    }
    m_closing = false;
    m_buf.clear();
    setState(State::Opening, QStringLiteral("starting %1").arg(QFileInfo(m_opt.program).fileName()));
    m_proc.start(m_opt.program, args, QIODevice::ReadWrite);
}

void BridgeTransport::close()
{
    if (m_proc.state() == QProcess::NotRunning) {
        setState(State::Closed);
        return;
    }
    m_closing = true;
    m_proc.closeWriteChannel();
    if (!m_proc.waitForFinished(1000)) {
        m_proc.kill();
        m_proc.waitForFinished(1000);
    }
}

QStringList BridgeTransport::commands() const
{
    // bridge.c exec(); the argument names are tool/PROTOCOL.md's
    return {QStringLiteral("ping"),        QStringLiteral("info"),      QStringLiteral("rate"),
            QStringLiteral("time"),        QStringLiteral("pause"),     QStringLiteral("step"),
            QStringLiteral("arm"),         QStringLiteral("disarm"),    QStringLiteral("torque"),
            QStringLiteral("speed"),       QStringLiteral("gear"),      QStringLiteral("enable"),
            QStringLiteral("coolant"),     QStringLiteral("vspeed"),    QStringLiteral("bms"),       QStringLiteral("fault_reset"),
            QStringLiteral("retry_auth"),  QStringLiteral("discharge"), QStringLiteral("asc"),
            QStringLiteral("inject"),      QStringLiteral("clear"),     QStringLiteral("key"),
            QStringLiteral("reboot"),      QStringLiteral("provision"), QStringLiteral("param_set"),
            QStringLiteral("param_reset"), QStringLiteral("params"),    QStringLiteral("dtc_clear"),
            QStringLiteral("uds"),         QStringLiteral("can_rx"),    QStringLiteral("vcu_model"),
            QStringLiteral("can_tap")};
}

quint64 BridgeTransport::send(const QString &cmd, const QJsonObject &args)
{
    const quint64 id = nextId();
    if (m_proc.state() != QProcess::Running) {
        refuse(id, cmd, QStringLiteral("the simulator bridge is not running"));
        return id;
    }
    QJsonObject a = args;
    if (cmd == QLatin1String("uds") && (m_tx.active() || m_txResponse)) {
        refuse(id, cmd, QStringLiteral("a segmented UDS request is in flight"));
        return id;
    }
    if (cmd == QLatin1String("uds") && args.contains(QLatin1String("msg"))) {
        const QVector<QByteArray> fr = IsoTp::frames(CanCodec::fromHex(args.value(QLatin1String("msg")).toString()));
        QString err;
        if (fr.isEmpty()) {
            refuse(id, cmd, QStringLiteral("msg: the UDS request, hex, SID first"));
            return id;
        }
        if (fr.size() > 1 && !m_udsOut.isEmpty()) {
            refuse(id, cmd, QStringLiteral("a segmented request waits until the pending responses are in"));
            return id;
        }
        if (fr.size() > 1) {
            m_txId = id;
            if (!m_tx.start(fr, &err)) {
                refuse(id, cmd, err);
            }
            return id;
        }
        a = QJsonObject{{QStringLiteral("hex"), QString::fromLatin1(fr[0].toHex(' ').toUpper())}}; // one frame: as it is
    }
    QString err;
    const QByteArray line = BridgeProtocol::encodeCommand(cmd, a, id, &err);
    if (line.isEmpty()) {
        refuse(id, cmd, err);
        return id;
    }
    if (cmd == QLatin1String("rate") && args.contains(QLatin1String("hz"))) {
        m_opt.rateHz = args.value(QLatin1String("hz")).toDouble(m_opt.rateHz);
    } else if (cmd == QLatin1String("time") && args.contains(QLatin1String("factor"))) {
        m_opt.timeFactor = args.value(QLatin1String("factor")).toDouble(m_opt.timeFactor);
    }
    m_proc.write(line + '\n');
    Q_EMIT traffic(QStringLiteral("tx"), QString::fromUtf8(line));
    if (cmd == QLatin1String("uds")) {
        m_udsOut.insert(id);
    }
    return id;
}

bool BridgeTransport::frameOut(const QByteArray &frame, bool replyDue)
{
    const quint64 id = nextId();
    QString err;
    const QByteArray line =
        replyDue ? BridgeProtocol::encodeCommand(QStringLiteral("uds"), {{QStringLiteral("hex"), QString::fromLatin1(frame.toHex(' '))}}, id, &err)
                 : BridgeProtocol::encodeCommand(QStringLiteral("can_rx"), {{QStringLiteral("can_id"), static_cast<int>(CanCodec::ID_UDS_REQ)},
                                                                              {QStringLiteral("bus"), 1},
                                                                              {QStringLiteral("hex"), QString::fromLatin1(frame.toHex())}},
                                                  id, &err);
    if (line.isEmpty() || m_proc.state() != QProcess::Running) {
        return false;
    }
    m_internal.insert(id);
    if (replyDue) {
        m_txUds = id;
        m_udsOut.insert(id);
    }
    m_proc.write(line + '\n');
    Q_EMIT traffic(QStringLiteral("tx"), QString::fromUtf8(line));
    return true;
}

// The ack of an internal command: a refused can_rx ends the request; a "uds" ack carries the ECU's frame — the flow
// control the request waits for, or (every frame out) the response, which becomes the ack of the tool's request.
void BridgeTransport::internalAck(const Ack &a)
{
    if (a.cmd == QLatin1String("can_rx")) {
        if (!a.ok) {
            m_tx.cancel(QStringLiteral("the bridge refused a consecutive frame: %1").arg(a.message));
        }
        return;
    }
    if (a.id != m_txUds) {
        return; // an earlier frame's, late: the request has moved on or ended
    }
    if (m_txResponse) {
        Ack r = a;
        r.id = m_txId;
        for (auto it = m_txInfo.begin(); it != m_txInfo.end(); ++it) {
            r.raw.insert(it.key(), it.value());
        }
        endSegmented();
        Q_EMIT ack(r);
        return;
    }
    const QByteArray f = CanCodec::fromHex(a.raw.value(QLatin1String("rsp")).toString());
    if (!m_tx.flowControl(f)) {
        m_tx.cancel(f.isEmpty() ? QStringLiteral("no flow control from the ECU (bridge: %1)").arg(a.message)
                                : QStringLiteral("the ECU answered %1 where a flow control was due").arg(QString::fromLatin1(f.toHex(' ').toUpper())));
    }
}

void BridgeTransport::endSegmented()
{
    m_txResponse = false;
    m_txUds = 0;
    m_txInfo = QJsonObject();
}

double BridgeTransport::expectedPeriodMs() const
{
    const double sim = 1000.0 / std::max(1.0, m_opt.rateHz);
    return (m_opt.timeFactor > 0.0) ? sim / m_opt.timeFactor : sim;
}

void BridgeTransport::onStdout()
{
    m_buf += m_proc.readAllStandardOutput();
    for (qsizetype nl = m_buf.indexOf('\n'); nl >= 0; nl = m_buf.indexOf('\n')) {
        const QByteArray line = m_buf.left(nl);
        m_buf.remove(0, nl + 1);
        handleLine(line);
    }
    if (m_buf.size() > (1 << 20)) {
        m_buf.clear(); // a runaway line without a newline: drop it rather than grow without bound
        Q_EMIT log(QStringLiteral("warn"), QStringLiteral("bridge output line longer than 1 MiB dropped"));
    }
}

void BridgeTransport::handleLine(const QByteArray &line)
{
    const BridgeProtocol::Message m = BridgeProtocol::parse(line);
    switch (m.type) {
    case BridgeProtocol::Type::Tel: Q_EMIT frame(BridgeProtocol::toFrame(m.obj)); return;
    case BridgeProtocol::Type::Ack: { // the Ack itself is the record
        const Ack a = BridgeProtocol::toAck(m.obj);
        if (a.cmd == QLatin1String("uds")) {
            m_udsOut.remove(a.id);
        }
        if (m_internal.remove(a.id)) {
            internalAck(a);
        } else if (a.id == 0 && a.cmd == QLatin1String("uds") &&
                   m_tx.flowControl(CanCodec::fromHex(a.raw.value(QLatin1String("rsp")).toString()))) {
            // after a Wait, the ECU's next flow control arrives unpaired (its request already answered by the Wait)
        } else {
            Q_EMIT ack(a);
        }
        return;
    }
    case BridgeProtocol::Type::Hello:
    case BridgeProtocol::Type::Params: Q_EMIT info(m.obj); break;
    case BridgeProtocol::Type::Log:
        Q_EMIT log(m.obj.value(QLatin1String("level")).toString(QStringLiteral("info")),
                   m.obj.value(QLatin1String("msg")).toString());
        return;
    case BridgeProtocol::Type::Can: Q_EMIT traffic(QStringLiteral("can"), QString::fromUtf8(line)); return;
    case BridgeProtocol::Type::Invalid:
        Q_EMIT log(QStringLiteral("warn"), QStringLiteral("unparsable bridge line (%1): %2")
                                              .arg(m.error, QString::fromUtf8(line.left(200))));
        return;
    default: break;
    }
    Q_EMIT traffic(QStringLiteral("rx"), QString::fromUtf8(line));
}
