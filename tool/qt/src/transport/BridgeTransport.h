// BridgeTransport — runs the simulator bridge (tool/bridge/sim_bridge: the firmware's host build, unmodified, with a
// plant, a VCU/BMS model and fault injection) as a child process and speaks its JSON lines over stdio.
// "uds" with "msg" (a UDS request of any length, SID first) is framed here per ISO 15765-2 (IsoTp.h), because the
// bridge's own "uds" carries one frame (at most 64 bytes, PROTOCOL.md A.4.5): a single frame goes to the bridge as its
// "uds"; a longer request is segmented here — the first frame and each block's last consecutive frame as the bridge's
// "uds" (its ack is the ECU's next flow control, or the response), the other consecutive frames as "can_rx" onto the
// diagnostic bus — under the firmware's flow control; the tool's ack is the response's.
#pragma once

#include "ITransport.h"
#include "IsoTp.h"

#include <QProcess>
#include <QSet>

class BridgeTransport : public ITransport
{
    Q_OBJECT
public:
    struct Options {
        QString program;              // path to sim_bridge
        QString sku = QStringLiteral("8xx_sic");
        double rateHz = 100.0;        // telemetry rate (bridge "rate")
        double timeFactor = 1.0;      // simulated/wall time (bridge "time"; 0 = as fast as possible)
        bool paused = false;
    };

    explicit BridgeTransport(Options o, QObject *parent = nullptr);
    ~BridgeTransport() override;

    // The first existing of: $TT_SIM_BRIDGE, next to the executable (the packaged app), the build's dev path.
    static QString locate();

    Kind kind() const override { return Kind::Bridge; }
    QString name() const override;
    void open() override;
    void close() override;
    bool isSimulator() const override { return true; }
    QStringList commands() const override;
    quint64 send(const QString &cmd, const QJsonObject &args = {}) override;
    double expectedPeriodMs() const override;
    const Options &options() const { return m_opt; }

private:
    void onStdout();
    void handleLine(const QByteArray &line);
    bool frameOut(const QByteArray &frame, bool replyDue); // one frame of a segmented request, as an internal command
    void internalAck(const Ack &a);
    void endSegmented();

    Options m_opt;
    QProcess m_proc;
    QByteArray m_buf;
    bool m_closing = false;
    IsoTpSender m_tx;
    quint64 m_txId = 0;          // the tool's segmented request
    quint64 m_txUds = 0;         // the internal "uds" whose ack is due (a flow control, or the response)
    bool m_txResponse = false;   // every frame is out: m_txUds's ack is the response
    QJsonObject m_txInfo;
    QSet<quint64> m_internal;    // internal commands: their acks are taken here
    QSet<quint64> m_udsOut;      // bridge "uds" commands awaiting their ack (it pairs ECU frames with them in order)
};
