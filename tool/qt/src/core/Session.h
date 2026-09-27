// Session — the one place the application's state lives: the transport, the latest frame and whether it is live,
// the telemetry ring store, the DTC picture, the alert rules, the calibration table, the event log and the
// recorder. Pages read from it and send commands through it; nothing else talks to a transport.
//
// Liveness: a frame is live only while the transport is open and the last frame arrived within the stale time
// (4 expected frame periods, at least 250 ms, by the host's monotonic clock). Otherwise every page shows the last
// values as stale, with their age — a stale frame never looks live.
#pragma once

#include "AlertEngine.h"
#include "DtcTracker.h"
#include "ITransport.h"
#include "LogIo.h"
#include "ParamModel.h"
#include "Telemetry.h"

#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QTimer>

#include <functional>
#include <memory>

struct LogEntry {
    qint64 rxMs = 0;       // host monotonic
    QDateTime when;
    QString level;         // info, warn, error, alert, dtc, tx
    QString text;
};

class Session : public QObject
{
    Q_OBJECT
public:
    enum class Link { None, Connecting, Live, Stale, Paused, Replay, Failed };
    Q_ENUM(Link)

    explicit Session(QObject *parent = nullptr);
    ~Session() override;

    void setTransport(std::unique_ptr<ITransport> t); // closes the previous one, opens this one
    void closeTransport();
    ITransport *transport() const { return m_transport.get(); }

    // Sends a command; done (optional) gets its Ack, or a synthesized refusal after timeoutMs. The returned number is
    // the one the Ack carries (ackReceived too). UDS requests ("uds", and the bridge's "dtc_clear", which runs the
    // image's own UDS) go one at a time per transport: the firmware drops a pending diagnostic response when a new
    // request arrives, so a second request waits in a queue until the first is answered or times out (timeoutMs counts
    // from its sending) — the pages' requests never pair responses crosswise.
    quint64 command(const QString &cmd, const QJsonObject &args = {}, std::function<void(const Ack &)> done = {},
                    int timeoutMs = 5000);
    int udsQueued() const { return static_cast<int>(m_udsQueue.size()); } // waiting behind the outstanding one

    Link link() const { return m_link; }
    static QString linkText(Link l);
    bool isLive() const { return m_link == Link::Live || m_link == Link::Replay; }
    bool isDeviceLive() const { return m_link == Link::Live; } // live data from a device that takes commands
    bool hasFrame() const { return m_haveFrame; }
    const TelemetryFrame &last() const { return m_last; }
    qint64 ageMs() const; // since the last frame (-1: none)
    qint64 staleAfterMs() const;
    qint64 nowMs() const { return m_clock.elapsed(); }
    double framesPerSecond() const { return m_fps; }

    TelemetryStore &store() { return m_store; }
    DtcTracker &dtcs() { return m_dtcs; }
    AlertEngine &alerts() { return m_alerts; }
    ParamModel &params() { return m_params; }
    const QJsonObject &deviceInfo() const { return m_info; }
    const QVector<LogEntry> &events() const { return m_events; }
    void addEvent(const QString &level, const QString &text);

    // Destructive controls on a real CAN bus are locked until the service key is entered (a UI interlock against a
    // slip on a live bench, not a security boundary: the key is documented in README.md).
    bool serviceUnlocked() const { return m_serviceUnlocked; }
    bool unlockService(const QString &key);
    void lockService();
    bool destructiveAllowed() const; // simulator: always; CAN: service unlocked; replay: never

    bool startRecording(const QString &path, QString *err);
    void stopRecording();
    bool isRecording() const { return m_rec.isOpen(); }
    const Recorder &recorder() const { return m_rec; }

    // Writes every dirty parameter (one param_set each, in order); progress through paramsWritten().
    bool writeParams(QString *err);
    void saveAlertRules() const;

Q_SIGNALS:
    void transportChanged();
    void linkChanged(Session::Link link);
    void frame(const TelemetryFrame &f);
    void infoChanged(const QJsonObject &info);
    void ackReceived(const Ack &a);
    void eventLogged(const LogEntry &e);
    void traffic(const QString &direction, const QString &line);
    void alertRaised(const AlertEvent &e);
    void dtcEvents(const QVector<DtcEvent> &events);
    void tick(); // 10 Hz: ages, staleness
    void serviceChanged(bool unlocked);
    void recordingChanged(bool on);
    void paramsWritten(int ok, int refused);

private:
    void onFrame(const TelemetryFrame &f);
    void onInfo(const QJsonObject &info);
    void onAck(const Ack &a);
    void onState(ITransport::State s, const QString &detail);
    void updateLink();
    void writeNextParam();
    void dispatch(quint64 sid, const QString &cmd, const QJsonObject &args, std::function<void(const Ack &)> done, int timeoutMs);
    void udsNext(); // the UDS slot is free: the next queued request goes out
    void readRecord(); // DID 0xFD26 into info "maps" (both transports); over CAN also 0xFD23, 0xFD25 ("root", "motor")

    std::unique_ptr<ITransport> m_transport;
    QElapsedTimer m_clock;
    QTimer m_tick;
    TelemetryFrame m_last;
    bool m_haveFrame = false;
    qint64 m_lastRx = -1;
    double m_fps = 0.0;
    qint64 m_fpsWindowStart = 0;
    int m_fpsCount = 0;
    Link m_link = Link::None;
    TelemetryStore m_store;
    DtcTracker m_dtcs;
    AlertEngine m_alerts;
    ParamModel m_params;
    QJsonObject m_info;
    QVector<LogEntry> m_events;
    Recorder m_rec;
    bool m_serviceUnlocked = false;
    bool m_simPaused = false; // the bridge acknowledged "pause": no telemetry is expected (not a stale link)
    struct Pending {
        std::function<void(const Ack &)> done;
        qint64 deadline = 0;
        quint64 sid = 0; // command()'s number: the Ack carries it
        bool uds = false;
    };
    struct Queued {
        quint64 sid;
        QString cmd;
        QJsonObject args;
        std::function<void(const Ack &)> done;
        int timeoutMs;
    };
    QHash<quint64, Pending> m_pending; // by the transport's id
    Pending m_sending;                 // the command inside send(): a transport may answer from inside it
    bool m_inSend = false;
    quint64 m_lastSid = 0;
    quint64 m_udsSid = 0;              // the UDS request holding the transport's one slot (0: free)
    QVector<Queued> m_udsQueue;
    bool m_recordAsked = false;
    int m_boot = -1; // the bridge's power-up counter (hello): a new one runs its record, read again
    QVector<QPair<QString, double>> m_writeQueue;
    int m_writeOk = 0, m_writeRefused = 0;
};
