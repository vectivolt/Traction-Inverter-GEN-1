// FirmwareUpdater — the FW-38 field update from this tool (docs/firmware-contract.md §10f): the logic of the Firmware
// page. The protocol is firmware/src/boot/uds_update.h, the container firmware/src/boot/image.h.
//  - The container (.tifw, built and signed by firmware/tools/sign-image.mjs) is parsed and checked as far as a tool
//    can: its structure and the payload's SHA-256 against the header. The Ed25519 signature is the firmware's to verify
//    (with the card's public key): the release key's private half never enters this tool, which signs nothing.
//  - The running image (DID 0xFD20 identity, 0xF208 uptime and key cycle, 0xFD24 the FW-38 boot record with the
//    anti-rollback counter — on both transports; on the simulator also the hello's power-up counter) is compared with the
//    image before anything is sent: another target SKU, the same or an older TI_FW_ID, a security version below the
//    anti-rollback counter. Over CAN the last is a refusal: start() reads the DIDs again and ends the update before the
//    unlock when the image's security version is below the counter (a doomed transfer would still cost a programming
//    session). On the simulator it stays a warning, and the firmware's own verification refuses it (ROLLBACK).
//  - The sequence, one request in flight: SecurityAccess (27 01/02, the bench key), 10 02 programmingSession (arming is
//    forbidden until the next power-up: DTC_FW_UPDATE), 34 RequestDownload (the ECU grants the block length), 36
//    TransferData in blocks of that length (the transport frames each per ISO 15765-2), 37 RequestTransferExit, 31 01
//    FF01 verify and its results polled (31 03 FF01), 31 01 F038 activate, 11 01 ECUReset, then the identity read again
//    until the ECU has restarted.
// A refusal ends the update with the NRC and what this firmware means by it. Nothing is retried except what the ECU
// asks to be repeated — NRC 0x21 busyRepeatRequest on 36 and 11, counted, logged and bounded.
#pragma once

#include "Session.h"

#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>

#include <functional>

class FirmwareUpdater : public QObject
{
    Q_OBJECT
public:
    static constexpr int HDR_LEN = 128; // image.h IMG_HDR_LEN
    struct Image {
        QString path, error; // error empty: structurally valid, the payload matches its SHA-256
        QByteArray bytes;    // the whole container, header first
        quint32 target = 0, length = 0, fwId = 0, secVer = 0;
        bool hashOk = false;
        bool signature = false; // 64 bytes present (not all zero); verified by the firmware only
        bool ok() const { return error.isEmpty(); }
    };
    static Image parse(const QByteArray &file);
    static Image load(const QString &path);

    struct Identity {
        bool valid = false; // DID 0xFD20 read
        quint32 fwId = 0;
        int sku = 0, hwSku = 0, calSku = 0; // the parameter set's, the HW_ID resistor's, the calibration record's
        QString serial, error;
        bool haveUptime = false; // DID 0xF208 read
        quint32 uptimeMs = 0, keyCycle = 0;
        int boot = -1;       // hello.boot (simulator)
        QJsonObject bootRec; // DID 0xFD24 (hello.boot_rec's keys): state, last, target, sec_counter, lkg_valid, last_err; empty: none
    };
    enum class Step { Idle, Check, Unlock, Session, Download, Transfer, Exit, Verify, Activate, Reset, Restart, Done, Failed };

    explicit FirmwareUpdater(Session &s, QObject *parent = nullptr);

    // false: no live link with UDS, or an update runs. One read at a time — a UDS client has one request in flight (the
    // firmware drops a pending FW-40 response for a new request) — a caller during a read joins it.
    bool readIdentity(std::function<void()> done = {});
    bool reading() const { return m_reading; }
    const Identity &identity() const { return m_before; } // read on request, and again just before the activation
    const Identity &after() const { return m_after; }     // the first read that shows the ECU restarted (until the next read)
    QStringList warnings(const Image &img) const; // the image against the running identity
    bool start(const Image &img);                 // false: text() says why nothing was sent

    bool busy() const { return m_busy; }
    Step step() const { return m_step; }
    QString text() const { return m_text; }
    int nrc() const { return m_nrc; }
    qint64 elapsedMs() const { return m_busy ? m_clock.elapsed() : m_elapsed; }
    qint64 transferMs() const { return m_transferMs; }
    qint64 bytesSent() const { return m_bytes; }
    qint64 bytesTotal() const { return m_img.bytes.size(); }
    int blocksDone() const { return m_block; }
    int blocksTotal() const;
    int blockMax() const { return m_blockMax; } // maxNumberOfBlockLength granted by 34 (SID and counter included)
    int busyRepeats() const { return m_busyRepeats; }
    int verifyStatus() const { return m_verify; } // 0 running, 1 passed, 2 failed, -1 not read
    int verifyReason() const { return m_reason; } // img_result_t
    bool activated() const { return m_activated; }
    bool restarted() const { return m_restarted; }
    const QJsonObject &transport() const { return m_tx; } // the last segmented block's: tx_frames, tx_dl, fc_bs, fc_stmin, fc_wait

    static QString stepName(Step s);
    static QString skuName(int sku);
    static QString imgResultText(int r); // "ROLLBACK: security version below the card's anti-rollback counter"
    static QString bootStateName(int st);
    static QString bootLastName(int last);
    static QString nrcText(quint8 sid, int nrc);

Q_SIGNALS:
    void changed();

private:
    using Reply = std::function<void(const QByteArray &rsp, const QString &err)>;
    void request(const QByteArray &msg, Reply next, int timeoutMs = 5000);
    void later(int ms, std::function<void()> f); // unless the sequence ends first
    // true: r begins with the positive response `want`; else the update ends with the transport's error, the NRC and
    // its meaning here, or the unexpected bytes
    bool expect(const QByteArray &r, const QString &err, const QByteArray &want, const QString &what);
    // true: NRC 0x21 — counted, logged, the request again shortly (bounded)
    bool busyRepeat(const QByteArray &r, const QString &err, quint8 sid, const QString &what, std::function<void()> again);
    void readInto(Identity *out, std::function<void()> done);
    void setStep(Step s);
    void unlock();
    void session();
    void download();
    void transfer();
    void transferExit();
    void verifyPoll(int polls);
    void activate();
    void reset();
    void restart(int polls);
    void finish(bool ok, const QString &text);

    Session &m_s;
    Image m_img;
    Identity m_before, m_after;
    Step m_step = Step::Idle;
    bool m_busy = false, m_activated = false, m_restarted = false, m_reading = false;
    QVector<std::function<void()>> m_joined; // readIdentity callers waiting for the read in flight
    QString m_text;
    int m_run = 0; // the sequence a callback belongs to
    int m_nrc = -1, m_verify = -1, m_reason = -1, m_blockMax = 0, m_block = 0, m_busyRepeats = 0;
    qint64 m_bytes = 0, m_elapsed = -1, m_transferMs = -1;
    QElapsedTimer m_clock, m_transferClock;
    QJsonObject m_tx;
};
