#include "CommissioningSequencer.h"

#include "CanCodec.h"
#include "CanTransport.h"
#include "ProtocolInfo.h"
#include "Uds.h"

#include <QDateTime>
#include <QJsonArray>
#include <QPointer>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <numeric>

namespace {
// firmware/src/app/commission.h: the enums by value, with the header's own explanation
const char *const STATES[] = {"IDLE", "RUNNING", "DONE", "ABORTED"};
const char *const REASONS[][2] = {
    {"NONE", "no reason"},
    {"ATTEST", "no or the wrong attestation for the routine"},
    {"CAL", "a service CAL outside its range"},
    {"STATE", "not ARMED_ZERO_TORQUE with the bridge armed through the normal path"},
    {"EVIDENCE", "FW-24 evidence, FW-20 record or gains incomplete, or arming forbidden"},
    {"FAULT", "a §6 row active"},
    {"DTC", "an active DTC"},
    {"VCU", "the VCU command stale, or it asks torque"},
    {"VEHICLE_SPEED", "the VCU's vehicle speed not valid and zero"},
    {"HV", "V_DC outside the SKU window, or the battery path not proven"},
    {"SPEED", "the motor speed not what the routine needs"},
    {"MOVED", "the locked rotor moved"},
    {"HEARTBEAT", "the tool's heartbeat lapsed"},
    {"CURRENT", "the current beyond the routine's bound"},
    {"VOLTAGE", "the current loop saturated"},
    {"TIMEOUT", "the routine overran its schedule"},
    {"STOPPED", "the tool stopped it (no DTC)"},
};
const char *const VERDICTS[][2] = {
    {"NONE", "no estimate"},
    {"VALID", "valid"},
    {"NOISY", "the uncertainty above u_max_rel (zero_u_max_rad)"},
    {"CLASS", "outside the FW-20 motor class limits"},
    {"NOT_REACHED", "the loop did not reach the test current"},
    {"AXES", "the saliency axis disagrees with the zero used"},
    {"DIR_PHASES", "the voltage vector turns against the resolver"},
    {"DIR_DYNO", "the resolver turns against the attested dyno direction"},
};
struct Qty {
    const char *key, *name, *unit, *si;
    double lsb;   // MC_UNIT_*: SI per count
    double scale; // SI -> the displayed unit
    int dec;      // one count = the last digit shown
};
// MC_UNIT_*: Rs 10 µΩ, Ld/Lq 0.1 µH, psi 10 µWb, zero 0.1 mrad
const Qty QTYS[CommissioningSequencer::QTY] = {
    {"rs", "Rs", "mΩ", "ohm", 1.0e-5, 1.0e3, 2},         {"ld", "Ld", "µH", "H", 1.0e-7, 1.0e6, 1},
    {"lq", "Lq", "µH", "H", 1.0e-7, 1.0e6, 1},           {"psi", "ψ", "mWb", "Wb", 1.0e-5, 1.0e3, 2},
    {"zero", "Electrical zero", "mrad", "rad", 1.0e-4, 1.0e3, 1}};
constexpr quint8 SID_SA = 0x27, SID_RC = 0x31, SID_RDBI = 0x22, SID_WDBI = 0x2E;
constexpr int MC_RUNNING = 1, MC_DONE = 2; // mc_state_t
constexpr double HF_BIAS_A = 50.0, HF_I_A = 20.0; // MC_CAL_DEFAULT hf_bias_a, hf_i_a: the sweep's bias floor and HF amplitude
constexpr double MAX_TIME_FACTOR = 2.0;    // POLL_MS x 2 = 100 ms of firmware time between polls: half hb_timeout_ms
constexpr int MAX_MISSED = 10;             // unanswered polls in a row: the tool no longer knows the routine's state
constexpr double VSPEED_MAX_KMH = 0.5;     // MC_CAL_DEFAULT.vspeed_max_kmh

QByteArray bytes(std::initializer_list<int> b)
{
    QByteArray a;
    for (int x : b) {
        a.append(static_cast<char>(x));
    }
    return a;
}
quint8 at(const QByteArray &a, int i) { return static_cast<quint8>(a.at(i)); }
int nrcOf(const QByteArray &r, quint8 sid) { return (r.size() >= 3 && at(r, 0) == 0x7F && at(r, 1) == sid) ? at(r, 2) : -1; }
QString unexpected(const QByteArray &r) { return QStringLiteral("unexpected response %1").arg(QString::fromLatin1(r.toHex(' ').toUpper())); }

QString nrcText(int nrc)
{
    static const QHash<int, const char *> t = {
        {0x12, "sub-function not supported"}, {0x13, "incorrect message length"}, {0x22, "conditions not correct"},
        {0x24, "request sequence error"}, {0x31, "request out of range"}, {0x33, "security access denied: not unlocked"},
        {0x35, "invalid key"}, {0x36, "exceeded number of attempts"},
        {0x72, "general programming failure: the record could not be queued"}};
    return QStringLiteral("NRC 0x%1 %2").arg(QString::number(nrc, 16).toUpper().rightJustified(2, QLatin1Char('0')),
                                             QLatin1String(t.value(nrc, "")));
}
QString saText(int nrc)
{
    switch (nrc) {
    case 0x22: return nrcText(nrc) + QStringLiteral(": no SecurityAccess key in this firmware build (fail closed)");
    case 0x35: return nrcText(nrc) + QStringLiteral(": this tool sends the bench key (PROTOCOL.md A.4.1); a product build's key is the OEM's");
    case 0x36: return nrcText(nrc) + QStringLiteral(": three invalid keys lock SecurityAccess until the MCU restarts");
    default: return nrcText(nrc);
    }
}

// The UDS payload of a "uds" ack: the bridge's reassembled "msg", or the single frame a CAN adapter received ("rsp").
QByteArray payloadOf(const Ack &a)
{
    if (a.raw.contains(QLatin1String("msg"))) {
        return CanCodec::fromHex(a.raw.value(QLatin1String("msg")).toString());
    }
    const QByteArray f = CanCodec::fromHex(a.raw.value(QLatin1String("rsp")).toString());
    if (f.size() >= 2 && at(f, 0) == 0) { // CAN-FD escape: 00, SF_DL
        return (f.size() >= 2 + at(f, 1)) ? f.mid(2, at(f, 1)) : QByteArray();
    }
    if (!f.isEmpty() && (at(f, 0) & 0xF0) == 0) { // classic single frame: 0L
        return (f.size() >= 1 + at(f, 0)) ? f.mid(1, at(f, 0)) : QByteArray();
    }
    return {};
}

QString outcomeName(CommissioningSequencer::Outcome o)
{
    static const char *const N[] = {"none", "done", "aborted", "refused", "committed", "failed"};
    return QLatin1String(N[static_cast<int>(o)]);
}

QString maskNames(int mask)
{
    QStringList s;
    for (int q = 0; q < CommissioningSequencer::QTY; q++) {
        if (mask & (1 << q)) {
            s << CommissioningSequencer::qtyName(q);
        }
    }
    if (mask & 0x20) {
        s << QStringLiteral("L_d/L_q map points");
    }
    if (mask & 0x40) {
        s << QStringLiteral("the ripple table");
    }
    return s.join(QStringLiteral(", "));
}

// 2E FD 46 (commission.c mc_ripple_write, uds_diag.c ripple_rq): what this firmware means by each refusal
QString rippleNrcText(int nrc)
{
    switch (nrc) {
    case 0x13: return nrcText(nrc) + QStringLiteral(": the request must be 75 bytes (2E FD 46 + 36 × int16)");
    case 0x22: return nrcText(nrc) + QStringLiteral(": a routine is running, or the inverter is in torque (RUN/DERATE, the bridge modulating or in ASC)");
    case 0x31: return nrcText(nrc) + QStringLiteral(": a value beyond cal_ripple_ff_max_a (0 A by default: only a zero table) — raise it on the Parameters page first");
    case 0x33: return nrcText(nrc) + QStringLiteral(" (one table write per unlock)");
    default: return nrcText(nrc);
    }
}

QString hex8(int v) { return QStringLiteral("0x") + QString::number(v, 16).toUpper().rightJustified(2, QLatin1Char('0')); }

int peakIndex(const QVector<qint16> &t)
{
    return t.isEmpty() ? -1 : static_cast<int>(std::max_element(t.begin(), t.end(), [](qint16 a, qint16 b) { return std::abs(a) < std::abs(b); }) - t.begin());
}
} // namespace

CommissioningSequencer::CommissioningSequencer(Session &s, QObject *parent) : QObject(parent), m_s(s)
{
    m_timer.setInterval(POLL_MS);
    connect(&m_timer, &QTimer::timeout, this, &CommissioningSequencer::pump);
    const auto forget = [this] { // another device, or a new key cycle: the results read are not this record's
        for (Quantity &q : m_q) {
            q = Quantity();
        }
        for (auto &axis : m_map) {
            std::fill(std::begin(axis), std::end(axis), MapPoint());
        }
        m_mapStaged[0] = m_mapStaged[1] = 0;
        m_mask = 0;
        m_fwState = -1;
        m_fwReason = 0;
    };
    connect(&m_s, &Session::infoChanged, this, [this, forget](const QJsonObject &info) {
        const qint64 kc = info.value(QLatin1String("key_cycle")).toInteger(-1);
        if (kc >= 0 && m_keyCycle >= 0 && kc != m_keyCycle) { // a reboot: the staged values of the old key cycle are gone
            forget();
            Q_EMIT changed();
        }
        m_keyCycle = kc;
    });
    connect(&m_s, &Session::transportChanged, this, [this, forget] {
        forget();
        m_keyCycle = -1;
        if (m_busy) { // between two polls: no callback of the old transport will end it
            m_timer.stop();
            m_running = false;
            m_inFlight = false;
            complete(Outcome::Failed, QStringLiteral("the transport changed during the operation"));
        }
        Q_EMIT changed();
    });
}

const QVector<QPair<QString, QString>> &CommissioningSequencer::checkItems()
{
    static const QVector<QPair<QString, QString>> items = {
        {QStringLiteral("key"), QStringLiteral("Service key entered in this tool|tool interlock")},
        {QStringLiteral("link"), QStringLiteral("Transport link live|tool")},
        {QStringLiteral("pace"), QStringLiteral("Heartbeat keeps up (≤ ×2 simulated time)|hb 200 ms")},
        {QStringLiteral("armed"), QStringLiteral("ARMED_ZERO_TORQUE, bridge armed idle|§9, FW-39")},
        {QStringLiteral("enable"), QStringLiteral("VCU enable withdrawn|FW-39")},
        {QStringLiteral("vspeed"), QStringLiteral("Vehicle speed valid and zero|VCU_CMD")},
        {QStringLiteral("fault"), QStringLiteral("No active fault or DTC|§6, FW-39")},
    };
    return items;
}

QVector<QPair<CommissioningSequencer::Check, QString>> CommissioningSequencer::checks() const
{
    QVector<QPair<Check, QString>> c(ROWS, {Check::Unknown, QString()});
    auto set = [&c](int row, bool ok, const QString &detail) { c[row] = {ok ? Check::Ok : Check::Fail, ok ? QString() : detail}; };
    set(RowKey, m_s.serviceUnlocked(), QStringLiteral("Tools ▸ Service key…"));
    set(RowLink, m_s.isDeviceLive(), Session::linkText(m_s.link()));
    if (!m_s.hasFrame()) {
        return c;
    }
    const TelemetryFrame &f = m_s.last();
    const double tf = f.v(QStringLiteral("sim.time_factor"), 1.0); // absent: a real bus in real time
    set(RowPace, tf > 0.0 && tf <= MAX_TIME_FACTOR,
        tf > 0.0 ? QStringLiteral("×%1: %2 ms of firmware time between polls").arg(tf).arg(POLL_MS * tf)
                 : QStringLiteral("× max: a 50 ms poll is no heartbeat"));
    const int sm = static_cast<int>(f.pick({"state.sm", "inv.state"}, -1));
    const int br = static_cast<int>(f.pick({"state.bridge", "inv.bridge"}, -1));
    if (sm >= 0) {
        set(RowArmed, sm == 6 && (br == 1 || br == 2), ProtocolInfo::instance().stateName(sm));
    }
    if (f.has(QStringLiteral("vcu.enable"))) {
        set(RowEnable, !f.b(QStringLiteral("vcu.enable")), QStringLiteral("enable requested"));
    }
    bool known = false, valid = false;
    double kmh = 0.0;
    if (f.has(QStringLiteral("vcu.vspeed_valid"))) { // the simulator's bench VCU
        known = true;
        valid = f.b(QStringLiteral("vcu.vspeed_valid"));
        kmh = f.v(QStringLiteral("vcu.vspeed_kmh"));
    } else if (const auto *can = qobject_cast<const CanTransport *>(m_s.transport()); can && can->vcuEmulation()) {
        known = true; // this tool is the VCU on the bench bus
        valid = can->vcuCommand().vspeedValid;
        kmh = can->vcuCommand().vspeedKmh;
    }
    if (known) {
        set(RowVspeed, valid && std::abs(kmh) <= VSPEED_MAX_KMH,
            valid ? QStringLiteral("%1 km/h").arg(kmh, 0, 'f', 2) : QStringLiteral("not valid"));
    }
    QStringList bad;
    if (f.b(QStringLiteral("safety.rows")) || f.b(QStringLiteral("inv.fault"))) {
        bad << QStringLiteral("fault");
    }
    for (const DtcState &d : m_s.dtcs().active()) { // commission.c dtc_blocking(): the service records excepted
        const QString n = ProtocolInfo::instance().dtcName(d.id);
        if (n != QLatin1String("SERVICE_LOCK_CLEARED") && n != QLatin1String("MC_ABORTED") && n != QLatin1String("MC_CAL_WRITTEN")) {
            bad << n;
        }
    }
    set(RowFault, bad.isEmpty(), bad.join(QStringLiteral(", ")));
    return c;
}

bool CommissioningSequencer::readyToStart() const
{
    const auto c = checks();
    for (int r = 0; r < ROWS; r++) {
        const bool tool = (r == RowKey || r == RowLink || r == RowPace); // the others: unknown lets the firmware judge
        if (c[r].first == Check::Fail || (tool && c[r].first != Check::Ok)) {
            return false;
        }
    }
    return !m_busy;
}

bool CommissioningSequencer::begin(Op op, int routine, quint16 attest, bool heartbeat)
{
    QString why = m_busy ? QStringLiteral("an operation is in progress") : QString();
    const auto c = checks();
    for (int r : {int(RowKey), int(RowLink), int(RowPace)}) {
        if (why.isEmpty() && c[r].first != Check::Ok && (heartbeat || r != RowPace)) {
            why = QStringLiteral("not started — %1%2").arg(checkItems()[r].second.section(QLatin1Char('|'), 0, 0),
                                                         c[r].second.isEmpty() ? QString() : QStringLiteral(": ") + c[r].second);
        }
    }
    if (!why.isEmpty()) {
        m_text = why;
        Q_EMIT changed();
        return false;
    }
    m_busy = true;
    m_nrc = -1;
    m_polls = 0;
    m_missed = 0;
    m_runs = 0;
    m_elapsed = -1;
    m_stopWanted = false;
    m_outcome = Outcome::None;
    m_text.clear();
    Entry e;
    e.n = static_cast<int>(m_history.size()) + 1;
    e.op = op;
    e.routine = routine;
    e.attest = attest;
    m_history.push_back(e);
    Q_EMIT changed();
    return true;
}

void CommissioningSequencer::request(const QByteArray &payload, Reply next)
{
    // "msg": the transport frames it per ISO 15765-2 — a classic single frame up to 7 bytes, FW-45's 8-byte biased start
    // an escape single frame, FW-46's 75-byte table write a first frame and a consecutive frame
    QPointer<CommissioningSequencer> self(this);
    m_inFlight = true;
    m_s.command(QStringLiteral("uds"), {{QStringLiteral("msg"), QString::fromLatin1(payload.toHex(' ').toUpper())}},
                [self, next](const Ack &a) {
                    if (!self) {
                        return;
                    }
                    self->m_inFlight = false;
                    const QByteArray r = a.ok ? payloadOf(a) : QByteArray();
                    next(r, !a.ok ? a.message : (r.isEmpty() ? QStringLiteral("no response from the firmware") : QString()));
                },
                2000);
}

void CommissioningSequencer::unlock(std::function<void(const QString &err)> next)
{
    request(bytes({SID_SA, 0x01}), [this, next](const QByteArray &r, const QString &err) {
        const int nrc = nrcOf(r, SID_SA);
        if (!err.isEmpty() || r.size() < 6 || !r.startsWith(bytes({0x67, 0x01}))) {
            m_nrc = nrc;
            next(QStringLiteral("SecurityAccess seed: %1").arg(!err.isEmpty() ? err : (nrc >= 0 ? saText(nrc) : unexpected(r))));
            return;
        }
        const QByteArray seed = r.mid(2, 4);
        if (seed == QByteArray(4, '\0')) { // a zero seed: already unlocked (a refused start keeps the unlock)
            next(QString());
            return;
        }
        request(bytes({SID_SA, 0x02}) + Uds::benchKey(seed), [this, next](const QByteArray &k, const QString &kerr) {
            const int knrc = nrcOf(k, SID_SA);
            if (!kerr.isEmpty() || !k.startsWith(bytes({0x67, 0x02}))) {
                m_nrc = knrc;
                next(QStringLiteral("SecurityAccess key: %1").arg(!kerr.isEmpty() ? kerr : (knrc >= 0 ? saText(knrc) : unexpected(k))));
                return;
            }
            next(QString());
        });
    });
}

bool CommissioningSequencer::start(int routine, quint16 attest)
{
    if (!begin(Op::Routine, routine, attest, true)) {
        return false;
    }
    launch(routine, attest, -1);
    return true;
}

// FW-45: bias index 0 … 5, each run a routine of its own (unlock, LK, the heartbeat); a point beyond the band of the
// record's map gets one confirming run (commission.c judge_r), as the firmware's own sweep does.
bool CommissioningSequencer::startMap()
{
    if (!begin(Op::Map, RoutineLdq, ATTEST_LOCKED, true)) {
        return false;
    }
    for (auto &axis : m_map) {
        std::fill(std::begin(axis), std::end(axis), MapPoint());
    }
    m_mapK = 0;
    m_mapRuns = 1;
    launch(RoutineLdq, ATTEST_LOCKED, 0);
    return true;
}

bool CommissioningSequencer::mapNext()
{
    const bool pending = ((m_map[0][m_mapK].flags | m_map[1][m_mapK].flags) & 1) != 0;
    if (pending && m_mapRuns < 2) {
        m_mapRuns++;
    } else if (m_mapK + 1 < MAP_N) {
        m_mapK++;
        m_mapRuns = 1;
    } else {
        return false;
    }
    Q_EMIT changed();
    launch(RoutineLdq, ATTEST_LOCKED, m_mapK);
    return true;
}

void CommissioningSequencer::launch(int routine, quint16 attest, int bias)
{
    unlock([this, routine, attest, bias](const QString &err) {
        if (!err.isEmpty()) {
            finish(m_nrc >= 0 ? Outcome::Refused : Outcome::Failed, err);
            return;
        }
        QByteArray rq = bytes({SID_RC, 0x01, 0xF0, 0x20, routine, attest >> 8, attest & 0xFF});
        if (bias >= 0) {
            rq += static_cast<char>(bias); // FW-45: 31 01 F0 20 02 4C 4B k
        }
        request(rq, [this, routine](const QByteArray &r, const QString &err2) {
            const int nrc = nrcOf(r, SID_RC);
            if (nrc >= 0) {
                m_nrc = nrc;
                finish(Outcome::Refused, QStringLiteral("refused: %1").arg(nrcText(nrc)));
                return;
            }
            if (!err2.isEmpty() || !r.startsWith(bytes({0x71, 0x01, 0xF0, 0x20, routine}))) {
                finish(Outcome::Failed, QStringLiteral("start: %1").arg(!err2.isEmpty() ? err2 : unexpected(r)));
                return;
            }
            m_running = true; // the start was the first heartbeat; the poll keeps it
            m_fwState = MC_RUNNING;
            m_fwReason = 0;
            m_runs++;
            if (m_elapsed < 0) { // the operation's first run: a sweep's elapsed time runs over all of them
                m_clock.start();
            }
            m_timer.start();
            Q_EMIT changed();
        });
    });
}

void CommissioningSequencer::stop()
{
    if (m_running) {
        m_stopWanted = true;
        pump();
    }
}

void CommissioningSequencer::pump()
{
    if (!m_running || m_inFlight) {
        return;
    }
    if (m_stopWanted) { // any request of RID 0xF020 is a heartbeat: the stop takes the poll's slot
        m_stopWanted = false;
        request(bytes({SID_RC, 0x02, 0xF0, 0x20}), [this](const QByteArray &r, const QString &err) {
            const int nrc = nrcOf(r, SID_RC);
            m_text = !err.isEmpty() ? QStringLiteral("stop: %1").arg(err)
                   : (nrc == 0x24) ? QStringLiteral("stop: nothing was running any more (NRC 0x24)")
                   : (nrc >= 0)    ? QStringLiteral("stop refused: %1").arg(nrcText(nrc))
                                   : QStringLiteral("stop accepted");
            Q_EMIT changed();
        });
        return;
    }
    request(bytes({SID_RC, 0x03, 0xF0, 0x20, 0x00}), [this](const QByteArray &r, const QString &err) {
        if (!m_running) {
            return;
        }
        if (!err.isEmpty() || r.size() < 7 || !r.startsWith(bytes({0x71, 0x03, 0xF0, 0x20, 0x00}))) {
            if (++m_missed >= MAX_MISSED) {
                m_running = false;
                m_timer.stop();
                m_elapsed = m_clock.elapsed();
                finish(Outcome::Failed, QStringLiteral("%1 heartbeat polls in a row unanswered (%2): the routine's outcome is "
                                                       "unknown; without the heartbeat the firmware aborts it (HEARTBEAT)")
                                            .arg(m_missed).arg(!err.isEmpty() ? err : unexpected(r)));
            }
            Q_EMIT changed();
            return;
        }
        m_missed = 0;
        m_polls++;
        m_fwState = at(r, 5);
        m_fwReason = at(r, 6);
        if (m_fwState == MC_RUNNING) {
            Q_EMIT changed();
            return;
        }
        m_running = false;
        m_timer.stop();
        m_elapsed = m_clock.elapsed();
        finish(m_fwState == MC_DONE ? Outcome::Done : Outcome::Aborted, QString());
    });
}

bool CommissioningSequencer::commit()
{
    if (!begin(Op::Commit, 0, 0, false)) {
        return false;
    }
    const int staged = m_mask & 0x7F;
    unlock([this, staged](const QString &err) {
        if (!err.isEmpty()) {
            finish(m_nrc >= 0 ? Outcome::Refused : Outcome::Failed, err);
            return;
        }
        request(bytes({SID_RC, 0x01, 0xF0, 0x21}), [this, staged](const QByteArray &r, const QString &err2) {
            const int nrc = nrcOf(r, SID_RC);
            if (nrc >= 0) {
                m_nrc = nrc;
                finish(Outcome::Refused, QStringLiteral("commit refused: %1").arg(nrcText(nrc)));
                return;
            }
            if (!err2.isEmpty() || !r.startsWith(bytes({0x71, 0x01, 0xF0, 0x21}))) {
                finish(Outcome::Failed, QStringLiteral("commit: %1").arg(!err2.isEmpty() ? err2 : unexpected(r)));
                return;
            }
            finish(Outcome::Committed,
                   QStringLiteral("committed%1 into a new calibration record version (sealed and checked by FW-20, queued "
                                  "to NVM, DTC_MC_CAL_WRITTEN). This key cycle keeps its record; the next key cycle's "
                                  "init validates the new one before anything arms.")
                       .arg(staged ? QStringLiteral(" (%1)").arg(maskNames(staged)) : QString()));
        });
    });
    return true;
}

// FW-46 (commission.c mc_ripple_write): one table write per unlock, refused while a routine runs or in torque, each value
// within cal_ripple_ff_max_a; staged in RAM for the next commit. Read back at once: the table the next commit writes.
bool CommissioningSequencer::writeRipple(const QVector<qint16> &table)
{
    if (table.size() != RIPPLE_N) {
        m_text = QStringLiteral("not written — the table has %1 values, the firmware takes %2").arg(table.size()).arg(RIPPLE_N);
        Q_EMIT changed();
        return false;
    }
    if (!begin(Op::RippleWrite, 0, 0, false)) {
        return false;
    }
    QByteArray rq = bytes({SID_WDBI, 0xFD, 0x46});
    for (const qint16 v : table) {
        rq += static_cast<char>(static_cast<quint16>(v) >> 8);
        rq += static_cast<char>(static_cast<quint16>(v) & 0xFF);
    }
    m_rippleSent = table;
    unlock([this, rq](const QString &err) {
        if (!err.isEmpty()) {
            finish(m_nrc >= 0 ? Outcome::Refused : Outcome::Failed, err);
            return;
        }
        request(rq, [this](const QByteArray &r, const QString &err2) {
            const int nrc = nrcOf(r, SID_WDBI);
            if (nrc >= 0) {
                m_nrc = nrc;
                finish(Outcome::Refused, QStringLiteral("ripple table refused: %1").arg(rippleNrcText(nrc)));
                return;
            }
            if (!err2.isEmpty() || !r.startsWith(bytes({0x6E, 0xFD, 0x46}))) {
                finish(Outcome::Failed, QStringLiteral("ripple table write: %1").arg(!err2.isEmpty() ? err2 : unexpected(r)));
                return;
            }
            readTable([this](const QString &rerr) {
                int differ = 0;
                for (int k = 0; rerr.isEmpty() && k < RIPPLE_N; k++) {
                    differ += (m_rippleRead[k] != m_rippleSent[k]) ? 1 : 0;
                }
                finish(rerr.isEmpty() && differ == 0 ? Outcome::Done : Outcome::Failed,
                       !rerr.isEmpty() ? QStringLiteral("written (6E FD 46), but the read-back failed: %1").arg(rerr)
                       : differ        ? QStringLiteral("written (6E FD 46), but %1 of %2 values read back differ (22 FD 46)").arg(differ).arg(RIPPLE_N)
                                       : QStringLiteral("written (6E FD 46) and read back equal (22 FD 46), all %1 values: staged for the next commit").arg(RIPPLE_N));
            });
        });
    });
    return true;
}

bool CommissioningSequencer::readRipple()
{
    if (!begin(Op::RippleRead, 0, 0, false)) {
        return false;
    }
    readTable([this](const QString &err) {
        const int k = peakIndex(m_rippleRead);
        finish(err.isEmpty() ? Outcome::Done : Outcome::Failed,
               !err.isEmpty() ? QStringLiteral("ripple table read: %1").arg(err)
                              : QStringLiteral("read (22 FD 46) — the table the next commit writes: the staged one, else the active "
                                               "record's; peak %1 A at %2° el").arg(m_rippleRead[k] / 100.0, 0, 'f', 2).arg(k * 360 / RIPPLE_N));
    });
    return true;
}

void CommissioningSequencer::readTable(std::function<void(const QString &err)> next)
{
    request(bytes({SID_RDBI, 0xFD, 0x46}), [this, next](const QByteArray &r, const QString &err) {
        if (!err.isEmpty() || r.size() != 3 + 2 * RIPPLE_N || !r.startsWith(bytes({0x62, 0xFD, 0x46}))) {
            const int nrc = nrcOf(r, SID_RDBI);
            next(!err.isEmpty() ? err : nrc >= 0 ? nrcText(nrc) : unexpected(r.left(8)));
            return;
        }
        m_rippleRead.resize(RIPPLE_N);
        for (int k = 0; k < RIPPLE_N; k++) {
            m_rippleRead[k] = static_cast<qint16>((at(r, 3 + 2 * k) << 8) | at(r, 4 + 2 * k));
        }
        next(QString());
    });
}

void CommissioningSequencer::finish(Outcome o, const QString &text)
{
    if (o == Outcome::Failed) { // no answer: a read-back would get none either
        complete(o, text);
        return;
    }
    const Op op = m_history.last().op;
    QVector<quint8> ix = {0x00, 0x01};
    if (op == Op::Map && o == Outcome::Done) { // this point's: the flags of 0x11/0x12 carry the bias bit and k
        ix << 0x02 << 0x11 << 0x12 << 0x31 << 0x32 << static_cast<quint8>(0x40 + m_mapK) << static_cast<quint8>(0x50 + m_mapK);
    } else if (op == Op::Map) {
        ix << 0x02;
    } else if (op == Op::Routine || op == Op::Commit) {
        for (int q = 0; q < QTY; q++) {
            ix << static_cast<quint8>(0x10 + q) << static_cast<quint8>(0x20 + q) << static_cast<quint8>(0x30 + q);
        }
    }
    m_readErr.clear();
    readNext(ix, [this, op, o, text] {
        if (op == Op::Map && o == Outcome::Done && m_readErr.isEmpty() && mapNext()) {
            return;
        }
        complete(o, text);
    });
}

void CommissioningSequencer::readNext(QVector<quint8> left, std::function<void()> next)
{
    if (left.isEmpty()) {
        next();
        return;
    }
    const quint8 ix = left.takeFirst();
    request(bytes({SID_RC, 0x03, 0xF0, 0x20, ix}), [this, ix, left, next](const QByteArray &r, const QString &err) {
        if (!err.isEmpty() || r.size() < 7 || !r.startsWith(bytes({0x71, 0x03, 0xF0, 0x20, ix}))) {
            m_readErr = QStringLiteral("results index 0x%1: %2").arg(ix, 2, 16, QLatin1Char('0')).arg(!err.isEmpty() ? err : unexpected(r));
            next();
            return;
        }
        const int b1 = at(r, 5), b2 = at(r, 6), i = ix & 0x0F;
        const auto be = static_cast<quint16>((b1 << 8) | b2);
        Quantity &q = m_q[i];
        MapPoint *pt = ((i == 1 || i == 2) && m_pointOf[i] >= 0) ? &m_map[i - 1][m_pointOf[i]] : nullptr;
        switch (ix & 0xF0) {
        case 0x00:
            if (ix == 0x00) {
                m_fwState = b1;
                m_fwReason = b2;
            } else if (ix == 0x01) {
                m_mask = b2; // b1: the routine
                if (!(b2 & 0x20)) {
                    m_mapStaged[0] = m_mapStaged[1] = 0; // no map point staged (committed, or a new key cycle)
                }
            } else {
                m_mapStaged[0] = b1; // FW-45: the staged points of the L_d map, of the L_q map
                m_mapStaged[1] = b2;
            }
            break;
        case 0x10:
            m_pointOf[i] = -1;
            if ((i == 1 || i == 2) && (b2 & 0x08)) { // FW-45: a biased run's — the flags are map point k's (bits 4-6)
                m_pointOf[i] = std::min((b2 >> 4) & 0x07, MAP_N - 1);
                m_map[i - 1][m_pointOf[i]].verdict = b1;
                m_map[i - 1][m_pointOf[i]].flags = b2 & 0x07;
                break;
            }
            q.verdict = b1;
            q.flags = b2;
            q.op = (b1 == 0) ? 0 : q.op; // a start clears its quantities' estimates
            break;
        case 0x20: (pt ? pt->value : q.value) = be; break;
        case 0x30: (pt ? pt->u : q.u) = be; break;
        case 0x40: // FW-45: the L_d point k's, the L_q point k's
        case 0x50: m_map[(ix >> 4) - 4][std::min(i, MAP_N - 1)].value = be; break;
        default: break;
        }
        readNext(left, next);
    });
}

void CommissioningSequencer::complete(Outcome o, QString text)
{
    Entry &e = m_history.last(); // begin() opened it
    QVector<int> qs;
    if (o == Outcome::Done && !m_readErr.isEmpty()) { // the table would show the values of before: say so instead
        o = Outcome::Failed;
        text = QStringLiteral("the routine finished (DONE) but its results were not read back");
    } else if (o == Outcome::Done && e.op == Op::Map) {
        QStringList pending;
        for (int k = 0; k < MAP_N; k++) {
            for (int ax = 0; ax < 2; ax++) {
                if (m_map[ax][k].flags & 1) {
                    pending << QStringLiteral("%1 point %2").arg(ax ? QStringLiteral("L_q") : QStringLiteral("L_d")).arg(k);
                }
            }
        }
        text = QStringLiteral("map swept in %1 s: %2 runs, %3 heartbeat polls; staged points L_d %4, L_q %5 of 0x3F%6")
                   .arg(m_elapsed / 1000.0, 0, 'f', 2).arg(m_runs).arg(m_polls)
                   .arg(hex8(m_mapStaged[0]), hex8(m_mapStaged[1]))
                   .arg(pending.isEmpty() ? QString()
                                          : QStringLiteral("; pending (beyond the band, the confirming run disagreed): %1 — a map is committed whole").arg(pending.join(QStringLiteral(", "))));
    } else if (o == Outcome::Done && e.op == Op::Routine) {
        QStringList parts;
        qs = quantitiesOf(e.routine);
        for (int q : qs) {
            m_q[q].op = e.n;
            e.values[q] = m_q[q];
            parts << summary(q, m_q[q]);
        }
        text = QStringLiteral("done in %1 s, %2 heartbeat polls: %3").arg(m_elapsed / 1000.0, 0, 'f', 2).arg(m_polls).arg(parts.join(QStringLiteral("; ")));
    } else if (o == Outcome::Aborted) {
        text = QStringLiteral("aborted after %1 s: %2 — %3").arg(m_elapsed / 1000.0, 0, 'f', 2).arg(stateName(m_fwState), reasonText(m_fwReason));
    } else if (o == Outcome::Refused && m_nrc == 0x22 && text.startsWith(QLatin1String("refused")) && m_readErr.isEmpty()) {
        text += QStringLiteral(" — ") + reasonText(m_fwReason); // the start's refusal: its reason is results index 0
    } else if (o == Outcome::Refused && m_nrc == 0x22 && text.startsWith(QLatin1String("commit")) && m_readErr.isEmpty()) {
        // commission.c commit() leaves index 0's reason alone: what it checks, as far as the read-back shows it
        const bool partMap = (m_mask & 0x20) && ((m_mapStaged[0] != 0 && m_mapStaged[0] != 0x3F) || (m_mapStaged[1] != 0 && m_mapStaged[1] != 0x3F));
        text += (m_fwState == MC_RUNNING) ? QStringLiteral(" — a routine is running")
              : ((m_mask & 0x7F) == 0)    ? QStringLiteral(" — nothing is staged")
              : partMap                   ? QStringLiteral(" — a map has only some of its six points staged (a map is committed whole)")
                                          : QStringLiteral(" — the bridge is switching (modulating or ASC), or the sealed record failed "
                                                           "calib_check (layout, CRC, ranges — a map point may rise at most 2 % over its neighbour and the flux slope just below every breakpoint must stay ≥ 0.25 × the unsaturated inductance (round 24) —, SKU, "
                                                           "serial, motor ID)");
    }
    if (e.op == Op::Map && o != Outcome::Done && m_mapK >= 0) {
        text = QStringLiteral("bias index %1: %2").arg(m_mapK).arg(text);
    }
    m_mapK = -1;
    if (!m_readErr.isEmpty()) {
        text += QStringLiteral(" (results not read back: %1)").arg(m_readErr);
    }
    e.outcome = o;
    e.nrc = m_nrc;
    e.reason = m_fwReason;
    e.elapsedMs = m_elapsed;
    e.polls = m_polls;
    e.tMs = m_s.hasFrame() ? m_s.last().tMs : 0.0;
    e.text = text;
    e.qs = qs;
    m_readTMs = e.tMs;
    m_outcome = o;
    m_text = text;
    m_busy = false;
    m_s.addEvent((o == Outcome::Done || o == Outcome::Committed) ? QStringLiteral("info") : QStringLiteral("warn"),
                 QStringLiteral("commissioning #%1 %2: %3").arg(e.n).arg(opName(e), text));
    Q_EMIT changed();
}

qint64 CommissioningSequencer::elapsedMs() const { return m_running ? m_clock.elapsed() : m_elapsed; }

quint16 CommissioningSequencer::lastDynoAttest() const
{
    for (auto it = m_history.crbegin(); it != m_history.crend(); ++it) {
        if (it->routine == RoutinePsiZero && it->outcome == Outcome::Done) {
            return it->attest;
        }
    }
    return 0;
}

QString CommissioningSequencer::stateName(int st)
{
    return (st >= 0 && st < static_cast<int>(std::size(STATES))) ? QLatin1String(STATES[st]) : QStringLiteral("—");
}

QString CommissioningSequencer::reasonName(int r)
{
    return (r >= 0 && r < static_cast<int>(std::size(REASONS))) ? QLatin1String(REASONS[r][0]) : QStringLiteral("reason %1").arg(r);
}

QString CommissioningSequencer::reasonText(int r)
{
    return (r >= 0 && r < static_cast<int>(std::size(REASONS))) ? QStringLiteral("%1: %2").arg(QLatin1String(REASONS[r][0]), QString::fromUtf8(REASONS[r][1]))
                                                                 : reasonName(r);
}

QString CommissioningSequencer::verdictName(int v)
{
    return (v >= 0 && v < static_cast<int>(std::size(VERDICTS))) ? QLatin1String(VERDICTS[v][0]) : QStringLiteral("verdict %1").arg(v);
}

QString CommissioningSequencer::verdictText(int v)
{
    return (v >= 0 && v < static_cast<int>(std::size(VERDICTS))) ? QString::fromUtf8(VERDICTS[v][1]) : verdictName(v);
}

QString CommissioningSequencer::routineName(int rt)
{
    switch (rt) {
    case RoutineRs: return QStringLiteral("Rs");
    case RoutineLdq: return QStringLiteral("Ld/Lq");
    case RoutinePsiZero: return QStringLiteral("ψ/zero/direction");
    default: return QStringLiteral("routine %1").arg(rt);
    }
}

QString CommissioningSequencer::opName(const Entry &e)
{
    switch (e.op) {
    case Op::Routine: return routineName(e.routine);
    case Op::Map: return QStringLiteral("Ld/Lq map");
    case Op::Commit: return QStringLiteral("commit");
    case Op::RippleWrite: return QStringLiteral("ripple table write");
    case Op::RippleRead: return QStringLiteral("ripple table read");
    }
    return {};
}

// commission.c map_bias() with MC_CAL_DEFAULT: breakpoint k's current, k/5 of the SKU's limit i_crest_a (the record's
// i_map_a, which calib_check binds to it), at least hf_bias_a 50 A, at most the limit — the d run also the record's
// demagnetisation limit — less hf_i_a 20 A. NaN when the limit is unknown.
double CommissioningSequencer::mapBiasA(int k, bool dAxis) const
{
    const QJsonObject &info = m_s.deviceInfo();
    const double pk = info.value(QLatin1String("limits")).toObject().value(QLatin1String("i_pk_rms_a")).toDouble(qQNaN());
    const ConstRow *c = ProtocolInfo::instance().constRow(QStringLiteral("i_crest_a")); // the SKU's √2·I_pk,rms
    const double crest = !std::isnan(pk) ? std::sqrt(2.0) * pk : (c ? c->valueFor(m_s.params().sku()) : qQNaN());
    const double top = dAxis ? std::min(crest, info.value(QLatin1String("motor")).toObject().value(QLatin1String("id_demag_a")).toDouble(qInf()))
                             : crest;
    return std::max(std::min(std::max(crest * k / (MAP_N - 1), HF_BIAS_A), top - HF_I_A), 0.0);
}

// FW-46's table from a dyno measurement. The CSV: one row per sample, the electrical angle in degrees (the firmware's
// θ_e: 0 on the d axis, as the FOC turns; any range, wrapped into one period) and the torque ripple the shaft showed there
// in N·m (the firmware's sign: positive motors in the positive direction), measured with the table not applied;
// comma, semicolon, tab or space separated, '#' comments and one header line allowed, at least one sample every 10° el.
// Resampled by periodic linear interpolation at 0, 10, … 350° el, the mean removed, turned into the i_q that cancels it —
// i_q = −(T − mean) / (1.5 pp ψ), torque.c's constant at i_d = 0 with the record's ψ and pole pairs (the firmware's own
// test builds its table so: test_fw45_46.c measure_table) — and rounded to 0.01 A; ±30 A at most (calib_check). The
// caller passes the record's ψ and pp: Session info "motor" (the bridge's hello, over CAN DID 0xFD25).
CommissioningSequencer::Ripple CommissioningSequencer::rippleFromCsv(const QByteArray &csv, double psiWb, int pp)
{
    Ripple out;
    if (!(psiWb > 0.0) || pp < 1) {
        out.error = QStringLiteral("the record's ψ and pole pairs are unknown: the simulator's hello carries them, over CAN DID 0xFD25 "
                                   "(read once the service key lets the tool transmit)");
        return out;
    }
    out.nmPerA = 1.5 * pp * psiWb;
    QVector<QPair<double, double>> pts;
    bool header = false;
    int line = 0;
    for (const QByteArray &raw : csv.split('\n')) {
        line++;
        const QString l = QString::fromUtf8(raw).trimmed();
        if (l.isEmpty() || l.startsWith(QLatin1Char('#'))) {
            continue;
        }
        const QStringList f = l.split(QRegularExpression(QStringLiteral("[,;\\s]+")), Qt::SkipEmptyParts);
        bool okA = false, okT = false;
        const double a = f.value(0).toDouble(&okA), t = f.value(1).toDouble(&okT);
        if (!okA || !okT || !std::isfinite(a) || !std::isfinite(t)) {
            if (pts.isEmpty() && !header) {
                header = true;
                continue;
            }
            out.error = QStringLiteral("line %1: two numbers expected — the electrical angle in °, the torque ripple in N·m: \"%2\"").arg(line).arg(l.left(60));
            return out;
        }
        pts.push_back({std::fmod(std::fmod(a, 360.0) + 360.0, 360.0), t});
    }
    std::sort(pts.begin(), pts.end());
    out.rows = static_cast<int>(pts.size());
    if (out.rows == 0) {
        out.error = QStringLiteral("no samples");
        return out;
    }
    for (int i = 0; i < out.rows; i++) {
        const double gap = (i + 1 < out.rows ? pts[i + 1].first : pts[0].first + 360.0) - pts[i].first;
        if (i + 1 < out.rows && gap <= 0.0) {
            out.error = QStringLiteral("two samples at %1° el").arg(pts[i].first);
            return out;
        }
        if (gap > 10.0 + 1e-9) {
            out.error = QStringLiteral("no sample between %1° and %2° el: the table needs one at least every 10° el (%3 rows)")
                            .arg(pts[i].first).arg(std::fmod(pts[i].first + gap, 360.0)).arg(out.rows);
            return out;
        }
    }
    QVector<double> nm(RIPPLE_N);
    for (int k = 0; k < RIPPLE_N; k++) {
        const double x = k * 360.0 / RIPPLE_N;
        int j = static_cast<int>(std::upper_bound(pts.begin(), pts.end(), qMakePair(x, qInf())) - pts.begin()); // first above x
        const QPair<double, double> hi = j < out.rows ? pts[j] : qMakePair(pts[0].first + 360.0, pts[0].second);
        const QPair<double, double> lo = j > 0 ? pts[j - 1] : qMakePair(pts.last().first - 360.0, pts.last().second);
        nm[k] = lo.second + (x - lo.first) * (hi.second - lo.second) / (hi.first - lo.first);
    }
    out.meanNm = std::accumulate(nm.begin(), nm.end(), 0.0) / RIPPLE_N;
    for (int k = 0; k < RIPPLE_N; k++) {
        const double ia = -(nm[k] - out.meanNm) / out.nmPerA;
        if (!(std::abs(ia) <= 30.0)) {
            out.error = QStringLiteral("%1 A at %2° el: the record holds ±30 A (calib_check)").arg(ia, 0, 'f', 2).arg(k * 360 / RIPPLE_N);
            out.table.clear();
            return out;
        }
        out.table.push_back(static_cast<qint16>(std::lround(ia * 100.0)));
    }
    const int pk = peakIndex(out.table);
    out.peakA = out.table[pk] / 100.0;
    out.peakDeg = pk * 360.0 / RIPPLE_N;
    return out;
}

QString CommissioningSequencer::attestName(quint16 a)
{
    return (a == 0) ? QString() : QString::fromLatin1(QByteArray(1, static_cast<char>(a >> 8)) + static_cast<char>(a & 0xFF));
}

QString CommissioningSequencer::qtyName(int q) { return QString::fromUtf8(QTYS[q].name); }

QVector<int> CommissioningSequencer::quantitiesOf(int routine)
{
    switch (routine) { // commission.h: RS -> Rs; LDQ -> Ld, Lq; PSI_ZERO -> psi, the zero (and the direction verdicts)
    case RoutineRs: return {0};
    case RoutineLdq: return {1, 2};
    case RoutinePsiZero: return {3, 4};
    default: return {};
    }
}

double CommissioningSequencer::siValue(int q, quint16 raw) { return raw * QTYS[q].lsb; }

QString CommissioningSequencer::valueText(int q, quint16 raw)
{
    const Qty &d = QTYS[q];
    return QStringLiteral("%1%2 %3").arg(raw == 0xFFFF ? QStringLiteral("≥ ") : QString(),
                                         QString::number(raw * d.lsb * d.scale, 'f', d.dec), QString::fromUtf8(d.unit));
}

QString CommissioningSequencer::flagsText(int flags)
{
    QStringList p;
    if (flags & 2) {
        p << QStringLiteral("staged");
    }
    if (flags & 4) {
        p << ((flags & 2) ? QStringLiteral("beyond the band, confirmed by a second run") : QStringLiteral("beyond the band"));
    }
    if (flags & 1) {
        p << QStringLiteral("pending: needs a second agreeing run");
    }
    return p.isEmpty() ? QStringLiteral("—") : p.join(QStringLiteral(", "));
}

QString CommissioningSequencer::summary(int q, const Quantity &v)
{
    if (v.verdict == 0) {
        return QStringLiteral("%1: no estimate").arg(qtyName(q));
    }
    return QStringLiteral("%1 %2 ± %3, %4%5").arg(qtyName(q), valueText(q, v.value), valueText(q, v.u), verdictName(v.verdict),
                                                 v.flags ? QStringLiteral(", ") + flagsText(v.flags) : QString());
}

QJsonObject CommissioningSequencer::toJson() const
{
    auto qty = [](int q, const Quantity &v) {
        const bool has = v.verdict != 0;
        return QJsonObject{{QStringLiteral("quantity"), QLatin1String(QTYS[q].key)},
                           {QStringLiteral("unit"), QLatin1String(QTYS[q].si)},
                           {QStringLiteral("value"), has ? QJsonValue(siValue(q, v.value)) : QJsonValue()},
                           {QStringLiteral("u"), has ? QJsonValue(siValue(q, v.u)) : QJsonValue()},
                           {QStringLiteral("raw_value_u"), QJsonArray{static_cast<int>(v.value), static_cast<int>(v.u)}},
                           {QStringLiteral("verdict"), verdictName(v.verdict)},
                           {QStringLiteral("pending"), (v.flags & 1) != 0},
                           {QStringLiteral("staged"), (v.flags & 2) != 0},
                           {QStringLiteral("beyond_band"), (v.flags & 4) != 0},
                           {QStringLiteral("operation"), v.op}};
    };
    QJsonArray now;
    for (int q = 0; q < QTY; q++) {
        now.append(qty(q, m_q[q]));
    }
    QJsonArray ops;
    for (const Entry &e : m_history) {
        QJsonArray eq;
        for (int q : e.qs) {
            eq.append(qty(q, e.values[q]));
        }
        ops.append(QJsonObject{{QStringLiteral("n"), e.n},
                               {QStringLiteral("operation"), opName(e)},
                               {QStringLiteral("attestation"), attestName(e.attest)},
                               {QStringLiteral("outcome"), outcomeName(e.outcome)},
                               {QStringLiteral("nrc"), e.nrc >= 0 ? QJsonValue(e.nrc) : QJsonValue()},
                               {QStringLiteral("reason"), reasonName(e.reason)},
                               {QStringLiteral("elapsed_ms"), e.elapsedMs >= 0 ? QJsonValue(static_cast<double>(e.elapsedMs)) : QJsonValue()},
                               {QStringLiteral("heartbeat_polls"), e.polls},
                               {QStringLiteral("t_ms"), e.tMs},
                               {QStringLiteral("text"), e.text},
                               {QStringLiteral("quantities"), eq}});
    }
    const QJsonObject &info = m_s.deviceInfo();
    QJsonObject r{{QStringLiteral("format"), QStringLiteral("traction-tool-commissioning")},
                  {QStringLiteral("version"), 1},
                  {QStringLiteral("created"), QDateTime::currentDateTime().toString(Qt::ISODateWithMs)},
                  {QStringLiteral("tool_version"), QStringLiteral(TT_VERSION)},
                  {QStringLiteral("fw_id"), info.value(QLatin1String("fw_id")).toString(QStringLiteral("unknown"))},
                  {QStringLiteral("units"), QStringLiteral("SI: ohm, H, Wb, rad; u = the standard uncertainty; the zero is the new electrical zero in [0, 2 pi)")},
                  {QStringLiteral("staged_mask"), m_mask},
                  {QStringLiteral("committed"), (m_mask & 0x80) != 0},
                  {QStringLiteral("quantities"), now},
                  {QStringLiteral("operations"), ops}};
    if (info.contains(QLatin1String("motor"))) {
        r.insert(QLatin1String("record_motor"), info.value(QLatin1String("motor"))); // the active record's (hello)
    }
    QJsonObject map{{QStringLiteral("unit"), QStringLiteral("H: the differential inductance at the bias current (A; the d run at -bias)")},
                    {QStringLiteral("staged_d"), m_mapStaged[0]},
                    {QStringLiteral("staged_q"), m_mapStaged[1]}};
    for (int ax = 0; ax < 2; ax++) {
        QJsonArray pts;
        for (int k = 0; k < MAP_N; k++) {
            const MapPoint &p = m_map[ax][k];
            const double bias = mapBiasA(k, ax == 0);
            pts.append(QJsonObject{{QStringLiteral("k"), k},
                                   {QStringLiteral("bias_a"), std::isnan(bias) ? QJsonValue() : QJsonValue(ax == 0 ? -bias : bias)},
                                   {QStringLiteral("value"), p.verdict ? QJsonValue(p.value * 1e-7) : QJsonValue()},
                                   {QStringLiteral("u"), p.verdict ? QJsonValue(p.u * 1e-7) : QJsonValue()},
                                   {QStringLiteral("verdict"), verdictName(p.verdict)},
                                   {QStringLiteral("pending"), (p.flags & 1) != 0},
                                   {QStringLiteral("staged"), (p.flags & 2) != 0},
                                   {QStringLiteral("beyond_band"), (p.flags & 4) != 0}});
        }
        map.insert(ax == 0 ? QStringLiteral("d") : QStringLiteral("q"), pts);
    }
    r.insert(QLatin1String("map"), map);
    auto amps = [](const QVector<qint16> &t) {
        QJsonArray a;
        for (const qint16 v : t) {
            a.append(v / 100.0);
        }
        return a;
    };
    r.insert(QLatin1String("ripple"), QJsonObject{{QStringLiteral("unit"), QStringLiteral("A: the i_q feed-forward at 0, 10, ... 350 deg el")},
                                                  {QStringLiteral("written"), amps(m_rippleSent)},
                                                  {QStringLiteral("read"), amps(m_rippleRead)}});
    if (ITransport *t = m_s.transport()) {
        r.insert(QLatin1String("transport"), t->name());
    }
    return r;
}

QStringList CommissioningSequencer::csvColumns()
{
    return {QStringLiteral("quantity"), QStringLiteral("value"),  QStringLiteral("u"),           QStringLiteral("unit"),
            QStringLiteral("verdict"),  QStringLiteral("pending"), QStringLiteral("staged"),     QStringLiteral("beyond_band"),
            QStringLiteral("operation")};
}

QVector<TelemetryFrame> CommissioningSequencer::csvRows() const
{
    QVector<TelemetryFrame> rows;
    for (int q = 0; q < QTY; q++) {
        const Quantity &v = m_q[q];
        TelemetryFrame f;
        f.tMs = m_readTMs; // session time of the read-back
        f.text.insert(QStringLiteral("quantity"), QLatin1String(QTYS[q].key));
        f.text.insert(QStringLiteral("unit"), QLatin1String(QTYS[q].si));
        f.text.insert(QStringLiteral("verdict"), verdictName(v.verdict));
        if (v.verdict != 0) {
            f.num.insert(QStringLiteral("value"), siValue(q, v.value));
            f.num.insert(QStringLiteral("u"), siValue(q, v.u));
        }
        f.num.insert(QStringLiteral("pending"), (v.flags & 1) ? 1.0 : 0.0);
        f.num.insert(QStringLiteral("staged"), (v.flags & 2) ? 1.0 : 0.0);
        f.num.insert(QStringLiteral("beyond_band"), (v.flags & 4) ? 1.0 : 0.0);
        f.num.insert(QStringLiteral("operation"), v.op);
        rows.push_back(f);
    }
    return rows;
}
