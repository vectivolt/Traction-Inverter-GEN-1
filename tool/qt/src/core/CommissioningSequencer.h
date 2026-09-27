// CommissioningSequencer — the logic of the Commissioning page: FW-39 motor self-commissioning through the firmware's
// interlocked service mode (firmware/src/app/commission.h is the contract; docs/firmware-contract.md §10g). It shows
// the service mode's preconditions as the session sees them, unlocks with SecurityAccess (27 01 / 27 02, the bench key
// of tool/PROTOCOL.md A.4.1), runs one routine at a time through RoutineControl 0x31 on 0x7E1 / 0x7E9 — the start
// carries the operator's rig attestation, the results poll every 50 ms is the tool's heartbeat (hb_timeout_ms 200),
// stop on request — reads and decodes the results and the staged mask, and commits the staged values (a fresh unlock).
// Round 23: FW-45's saturation map — the Ld/Lq routine at the six bias indices one after the other (each a routine of
// its own: unlock, LK, heartbeat; a point beyond the band of the record's map gets one confirming run), the twelve
// differential inductances (results 0x40+k, 0x50+k) with their verdicts and the staged points (index 0x02) — and
// FW-46's torque-ripple table: a CSV made into the 36-point i_q feed-forward, written with 2E FD 46 (one segmented
// request, a fresh unlock), read back with 22 FD 46; both are sealed by the same commit.
// One UDS request is in flight at a time. A refusal ends the operation with the firmware's reason: nothing is retried,
// and no torque is ever sent from here.
#pragma once

#include "Session.h"

#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QVector>

#include <functional>

class CommissioningSequencer : public QObject
{
    Q_OBJECT
public:
    // commission.h: mc_routine_t values, the MC_ATTEST_* codes, MC_Q_COUNT
    enum Routine { RoutineRs = 1, RoutineLdq = 2, RoutinePsiZero = 3 };
    static constexpr quint16 ATTEST_LOCKED = 0x4C4B;   // "LK": the rotor held by the rig's brake (Rs, Ld/Lq)
    static constexpr quint16 ATTEST_DYNO_FWD = 0x4446; // "DF": a dyno drives the rotor forward (psi/zero)
    static constexpr quint16 ATTEST_DYNO_REV = 0x4452; // "DR": ... backward
    static constexpr int QTY = 5;                      // Rs, Ld, Lq, psi, zero (mc_qty_t)
    static constexpr int POLL_MS = 50;                 // the results poll: the tool's heartbeat
    static constexpr int MAP_N = 6;                    // motor.h MOTOR_MAP_N: the saturation map's breakpoints
    static constexpr int RIPPLE_N = 36;                // torque.h TQ_RIPPLE_N: the ripple table, 10° el apart
    enum class Outcome { None, Done, Aborted, Refused, Committed, Failed };
    enum class Op { Routine, Map, Commit, RippleWrite, RippleRead };
    enum class Check { Unknown, Ok, Fail };
    enum Row { RowKey, RowLink, RowPace, RowArmed, RowEnable, RowVspeed, RowFault, ROWS };

    struct Quantity {
        int verdict = 0;   // mc_verdict_t; 0 (NONE): the firmware holds no estimate
        int flags = 0;     // bit 0 pending, 1 staged, 2 beyond the band
        quint16 value = 0; // BE16 in MC_UNIT_* (0xFFFF: at or beyond the field's range)
        quint16 u = 0;     // its standard uncertainty, same unit
        int op = 0;        // the operation of this session that produced it (0: none)
    };
    struct MapPoint {   // FW-45: the differential inductance of the L_d (axis 0) or L_q (axis 1) map at a bias index
        int verdict = 0;   // mc_verdict_t of the last run at it (0: none read)
        int flags = 0;     // the point's: bit 0 pending, 1 staged, 2 beyond the band of the record's map
        quint16 value = 0; // MC_UNIT_L (0.1 µH)
        quint16 u = 0;
    };
    struct Ripple {                 // FW-46: a CSV of (electrical angle in °, measured torque ripple in N·m) as the table
        QVector<qint16> table;      // RIPPLE_N × 0.01 A: the i_q feed-forward at 0, 10, … 350° el — the ripple's opposite
        int rows = 0;
        double meanNm = 0.0;        // the torque's mean, removed (a ripple has none)
        double peakA = 0.0, peakDeg = 0.0;
        double nmPerA = 0.0;        // 1.5 pp ψ: torque.c's T = 1.5 pp (ψ + (Ld − Lq) i_d) i_q at i_d = 0
        QString error;              // empty: usable
    };
    struct Entry { // one operation of the session: a routine, a map sweep, a commit, a ripple table write or read
        int n = 0;
        Op op = Op::Routine;
        int routine = 0; // mc_routine_t (Routine, Map)
        quint16 attest = 0;
        Outcome outcome = Outcome::None;
        int nrc = -1;    // the negative response, if any
        int reason = 0;  // mc_reason_t as read at its end
        qint64 elapsedMs = -1;
        int polls = 0;
        double tMs = 0;  // session time of the last frame at its end
        QString text;
        QVector<int> qs; // the quantities it produced
        Quantity values[QTY];
    };

    explicit CommissioningSequencer(Session &s, QObject *parent = nullptr);

    // The preconditions panel: CheckList items (key, "title|ref") and their states now, in Row order.
    static const QVector<QPair<QString, QString>> &checkItems();
    QVector<QPair<Check, QString>> checks() const;
    bool readyToStart() const; // idle, no row failing, the tool's own interlocks met

    bool start(int routine, quint16 attest); // false (text() says why) when the tool refuses to begin
    bool startMap();                         // FW-45: the Ld/Lq routine (LK) at bias indices 0 … 5, in sequence
    void stop();                             // 31 02 F0 20, in the heartbeat's next slot
    bool commit();                           // a fresh unlock, 31 01 F0 21
    bool writeRipple(const QVector<qint16> &table); // FW-46: a fresh unlock, 2E FD 46, then 22 FD 46 read back
    bool readRipple();                              // 22 FD 46: the table the next commit writes

    bool busy() const { return m_busy; }
    bool running() const { return m_running; }
    Outcome outcome() const { return m_outcome; }
    int lastNrc() const { return m_nrc; }
    int fwState() const { return m_fwState; }   // mc_state_t as last read (-1: not read)
    int fwReason() const { return m_fwReason; } // mc_reason_t as last read
    int stagedMask() const { return m_mask; }   // bit q staged, 5 a map point, 6 the ripple table, 7 committed (index 1)
    int mapIndex() const { return m_mapK; }     // the bias index of the sweep's run (-1: no sweep)
    int mapStaged(int axis) const { return m_mapStaged[axis]; } // results index 2: bit k staged (axis 0 d, 1 q)
    const MapPoint &mapPoint(int axis, int k) const { return m_map[axis][k]; }
    double mapBiasA(int k, bool dAxis) const;   // the bias current the firmware runs at index k (A; the d run at −it)
    const QVector<qint16> &rippleSent() const { return m_rippleSent; } // the last table written
    const QVector<qint16> &rippleRead() const { return m_rippleRead; } // the last 22 FD 46 (empty: none)
    qint64 elapsedMs() const;                   // the routine's, from its accepted start (-1: none yet)
    int polls() const { return m_polls; }
    int missedPolls() const { return m_missed; }
    QString text() const { return m_text; }
    const Quantity &quantity(int q) const { return m_q[q]; }
    const QVector<Entry> &history() const { return m_history; }
    quint16 lastDynoAttest() const; // the attestation of this session's last psi/zero run that finished (0: none)

    // commission.h names and units
    static QString stateName(int st);
    static QString reasonName(int r);
    static QString reasonText(int r); // "VEHICLE_SPEED: the VCU's vehicle speed not valid and zero"
    static QString verdictName(int v);
    static QString verdictText(int v);
    static QString routineName(int rt);
    static QString opName(const Entry &e); // "Rs", "Ld/Lq map", "commit", "ripple table write", …
    static Ripple rippleFromCsv(const QByteArray &csv, double psiWb, int pp);
    static QString attestName(quint16 a);
    static QString qtyName(int q);
    static QVector<int> quantitiesOf(int routine);
    static double siValue(int q, quint16 raw);    // ohm, H, Wb, rad
    static QString valueText(int q, quint16 raw); // "24.97 mΩ"
    static QString flagsText(int flags);
    static QString summary(int q, const Quantity &v);

    QJsonObject toJson() const;               // the export (BugReport::save writes it)
    QVector<TelemetryFrame> csvRows() const;  // the results table for LogIo::saveCsv
    static QStringList csvColumns();

Q_SIGNALS:
    void changed();

private:
    using Reply = std::function<void(const QByteArray &rsp, const QString &err)>;
    bool begin(Op op, int routine, quint16 attest, bool heartbeat); // the tool's interlocks; opens a history entry
    void request(const QByteArray &payload, Reply next);
    void unlock(std::function<void(const QString &err)> next);
    void launch(int routine, quint16 attest, int bias); // unlock, then the start (bias >= 0: FW-45's byte)
    bool mapNext();                                     // after a run of the sweep: the next one started (false: done)
    void readTable(std::function<void(const QString &err)> next); // 22 FD 46 into m_rippleRead
    void pump(); // the heartbeat slot: a stop when asked, else a results poll
    void finish(Outcome o, const QString &text);
    void complete(Outcome o, QString text);
    void readNext(QVector<quint8> left, std::function<void()> next);

    Session &m_s;
    QTimer m_timer;
    QElapsedTimer m_clock;
    bool m_busy = false, m_running = false, m_inFlight = false, m_stopWanted = false;
    Outcome m_outcome = Outcome::None;
    int m_nrc = -1, m_fwState = -1, m_fwReason = 0, m_mask = 0, m_polls = 0, m_missed = 0, m_runs = 0;
    int m_mapK = -1, m_mapRuns = 0, m_mapStaged[2] = {0, 0};
    int m_pointOf[QTY] = {-1, -1, -1, -1, -1}; // a read's 0x1q flagged a biased run: its 0x2q/0x3q are map point k's
    MapPoint m_map[2][MAP_N];
    QVector<qint16> m_rippleSent, m_rippleRead;
    qint64 m_keyCycle = -1; // the device's key cycle from its hello: a change empties the staged results
    qint64 m_elapsed = -1;
    QString m_text, m_readErr;
    double m_readTMs = 0;
    Quantity m_q[QTY];
    QVector<Entry> m_history;
};
