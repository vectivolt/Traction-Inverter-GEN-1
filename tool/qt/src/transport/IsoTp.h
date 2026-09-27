// IsoTp — the transmit side of ISO 15765-2:2016 for UDS requests on the diagnostic CAN-FD bus, framed as the firmware
// takes them and sends its own (firmware/src/comms/uds_diag.c, docs/firmware-contract.md §10h): TX_DL 64, padding 0xAA.
// A request of up to 7 bytes is a classic single frame (8 bytes), up to 62 an escape single frame (00 SF_DL) in the
// smallest CAN-FD length that holds it, longer a first frame (FF_DL in 12 bits up to 4095, the 32-bit escape form
// beyond) with 62 data bytes, then consecutive frames of 63 with the 4-bit sequence number 1 … 15, 0, 1 …, the last one
// padded to its CAN-FD length.
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QVector>

#include <functional>

namespace IsoTp {
constexpr int TX_DL = 64;
constexpr quint8 PAD = 0xAA;  // uds_diag.c PAD
constexpr int N_BS_MS = 1000; // a flow control's timeout (the firmware's own N_Bs)
constexpr int N_WFT_MAX = 16; // Wait flow controls accepted in a row (the firmware's N_WFT_MAX)
int fdLen(int n);             // the smallest CAN-FD data length (8, 12, 16, 20, 24, 32, 48, 64) holding n bytes
QVector<QByteArray> frames(const QByteArray &msg); // one single frame, or a first frame and consecutive frames
int stminUs(quint8 raw); // 0x00-0x7F ms, 0xF1-0xF9 100-900 µs, a reserved value as 127 ms (uds_diag.c stmin_us)
} // namespace IsoTp

// One segmented request at a time under the receiver's flow control: ContinueToSend (its block size — 0: no further
// flow control — and STmin before each further consecutive frame, waited in whole milliseconds, never less), Wait
// (N_Bs again, at most N_WFT_MAX), Overflow or an invalid flow status (abandoned); N_Bs without a flow control abandons
// it. The transport writes the frames (`replyDue`: a flow control or the response follows this one) and hands every
// frame from the receiver to flowControl().
class IsoTpSender : public QObject
{
    Q_OBJECT
public:
    using Out = std::function<bool(const QByteArray &frame, bool replyDue)>;
    explicit IsoTpSender(Out out, QObject *parent = nullptr);
    bool active() const { return !m_frames.isEmpty(); }
    bool start(const QVector<QByteArray> &frames, QString *err); // writes the first frame; false: busy or not written
    bool flowControl(const QByteArray &frame); // true: the flow control this request waits for, taken
    void cancel(const QString &why);           // failed(why) if active
    void abort();                              // silently

Q_SIGNALS:
    void sent(const QJsonObject &info); // the last consecutive frame is out: the response comes next
    void failed(const QString &why, const QJsonObject &info);

private:
    void next();
    QJsonObject info() const; // tx_frames (sent), tx_dl, fc (flow controls), fc_bs, fc_stmin, fc_wait

    Out m_out;
    QVector<QByteArray> m_frames;
    int m_next = 0, m_blockLeft = 0, m_fcs = 0, m_waits = 0, m_waitRow = 0, m_bs = -1, m_stmin = -1;
    bool m_waitFc = false;
    QTimer m_nbs, m_pace;
};
