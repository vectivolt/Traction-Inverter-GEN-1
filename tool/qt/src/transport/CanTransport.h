// CanTransport — the inverter on a real CAN-FD bus through Qt SerialBus (the adapter plugin is chosen at run time:
// socketcan, peakcan, vectorcan, systeccan, tinycan, passthrucan, virtualcan — whatever this machine has).
// Receives INV_STATUS (0x201: E2E CRC + alive counter checked as the firmware checks its inputs) and, when a real VCU
// is on the bus, decodes its VCU_CMD/VCU_BMS too. Transmits only when the service key has unlocked it: VCU emulation
// (VCU_CMD + VCU_BMS every 10 ms, the bench VCU's role), raw frames and UDS requests — "hex" one single frame as given,
// "msg" a request of any length framed per ISO 15765-2 (IsoTp.h: a single frame, or a first frame and consecutive frames
// under the ECU's flow control). The response is reassembled per ISO 15765-2: a first frame gets this tester's flow
// control, the consecutive frames are checked. A real
// bench's contactors are hardware: the contactor state the emulated VCU REPORTS is set by the operator ("contactors"),
// never assumed.
#pragma once

#include "CanCodec.h"
#include "ITransport.h"
#include "IsoTp.h"

#include <QCanBusDevice>
#include <QElapsedTimer>
#include <QQueue>
#include <QTimer>

class CanTransport : public ITransport
{
    Q_OBJECT
public:
    struct Options {
        QString plugin;
        QString interface;
        int bitrate = 500000;
        bool fd = true;
        int dataBitrate = 2000000;
    };
    struct Stats {
        quint64 rx = 0, status = 0, crcErr = 0, frozen = 0, jump = 0, badLen = 0, errorFrames = 0, tx = 0, txErr = 0;
    };

    // QCanBus::createDevice with the options applied; nullptr and *err when the plugin or interface is missing.
    static QCanBusDevice *createDevice(const Options &o, QString *err);

    // Takes ownership of device (reparented).
    CanTransport(QCanBusDevice *device, QString description, QObject *parent = nullptr);
    ~CanTransport() override;

    Kind kind() const override { return Kind::Can; }
    QString name() const override { return m_desc; }
    void open() override;
    void close() override;
    QStringList commands() const override;
    quint64 send(const QString &cmd, const QJsonObject &args = {}) override;
    double expectedPeriodMs() const override { return CanCodec::STATUS_PERIOD_MS; }

    void setServiceUnlocked(bool on);
    bool serviceUnlocked() const { return m_unlocked; }
    bool vcuEmulation() const { return m_vcuTimer.isActive(); }
    const Stats &stats() const { return m_stats; }
    const CanCodec::VcuCmd &vcuCommand() const { return m_cmd; }

private:
    void onFrames();
    void vcuTick();
    bool write(quint32 id, const QByteArray &payload, bool log);
    void handleStatus(const QCanBusFrame &f);
    void udsFrame(const QByteArray &p);
    void udsDone(const QString &err);
    void addVcuChannels(TelemetryFrame &f) const;
    qint64 now() const { return m_clock.elapsed(); }

    QCanBusDevice *m_dev;
    QString m_desc;
    QElapsedTimer m_clock;
    QTimer m_vcuTimer;
    QTimer m_udsTimer;
    bool m_unlocked = false;
    CanCodec::VcuCmd m_cmd;
    CanCodec::VcuBms m_bms;
    bool m_bmsTx = true;
    qint64 m_faultResetUntil = 0, m_retryUntil = 0, m_dischargeUntil = 0, m_holdUntil = 0;
    CanCodec::AliveCounter m_statusCtr;
    Stats m_stats;
    // the last VCU frames seen on the bus (a real VCU, or our own echo)
    std::optional<CanCodec::VcuCmd> m_seenCmd;
    std::optional<CanCodec::VcuBms> m_seenBms;
    struct UdsPending {
        quint64 id;
        qint64 deadline;
        QJsonObject extra; // a segmented request's transmission (IsoTpSender info), added to its ack
    };
    QQueue<UdsPending> m_uds;
    IsoTpSender m_tx; // a segmented request going out (one at a time)
    quint64 m_txId = 0;
    struct UdsRx {         // the response being received on 0x7E9
        QString first;     // its first frame, hex
        QByteArray msg;    // the UDS payload so far
        int len = 0;       // a segmented response's length (first frame)
        int frames = 0;
        quint8 sn = 0;     // the next consecutive frame's sequence number
        bool active = false;
    } m_rx;
};
