#include "Session.h"

#include "CanCodec.h"
#include "CanTransport.h"
#include "ProtocolInfo.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>
#include <QtEndian>

#include <algorithm>

namespace {
// SHA-256 of the documented service key (README.md, "Service key"): an interlock, not a secret.
constexpr char SERVICE_KEY_SHA256[] = "29daee2eb08df97375fcf9a82cd12e309c2af1ff948c812bc095b269af2b33a1"; // "traction-service"
bool isUds(const QString &cmd) { return cmd == QLatin1String("uds") || cmd == QLatin1String("dtc_clear"); }
} // namespace

Session::Session(QObject *parent) : QObject(parent)
{
    m_clock.start();
    m_tick.setInterval(100);
    connect(&m_tick, &QTimer::timeout, this, [this] {
        updateLink();
        const qint64 now = nowMs();
        QVector<Pending> expired; // answered after the scan: a callback may send (and insert) again
        for (auto it = m_pending.begin(); it != m_pending.end();) {
            if (now > it->deadline) {
                expired.push_back(std::move(it.value()));
                it = m_pending.erase(it);
            } else {
                ++it;
            }
        }
        for (Pending &p : expired) {
            Ack a;
            a.id = p.sid;
            a.ok = false;
            a.message = QStringLiteral("no answer (timeout)");
            if (p.done) {
                p.done(a);
            }
            if (p.uds && p.sid == m_udsSid) {
                udsNext();
            }
        }
        Q_EMIT tick();
    });
    m_tick.start();
    QSettings s;
    const QJsonArray rules = QJsonDocument::fromJson(s.value(QStringLiteral("alerts/rules")).toByteArray()).array();
    m_alerts.fromJson(rules);
}

Session::~Session()
{
    m_rec.close();
    if (m_transport) {
        m_transport->disconnect(this);
    }
}

void Session::saveAlertRules() const
{
    QSettings s;
    s.setValue(QStringLiteral("alerts/rules"), QJsonDocument(m_alerts.toJson()).toJson(QJsonDocument::Compact));
}

QString Session::linkText(Link l)
{
    switch (l) {
    case Link::None: return QStringLiteral("Disconnected");
    case Link::Connecting: return QStringLiteral("Connecting");
    case Link::Live: return QStringLiteral("Live");
    case Link::Stale: return QStringLiteral("Stale");
    case Link::Paused: return QStringLiteral("Paused");
    case Link::Replay: return QStringLiteral("Replay");
    case Link::Failed: return QStringLiteral("Failed");
    }
    return {};
}

void Session::setTransport(std::unique_ptr<ITransport> t)
{
    closeTransport();
    m_transport = std::move(t);
    if (!m_transport) {
        return;
    }
    m_haveFrame = false;
    m_simPaused = false;
    m_lastRx = -1;
    m_last = TelemetryFrame();
    m_store.clear();
    m_dtcs.clear();
    m_info = QJsonObject();
    m_recordAsked = false;
    m_boot = -1;
    m_params.clearDevice();
    ITransport *tr = m_transport.get();
    connect(tr, &ITransport::frame, this, &Session::onFrame);
    connect(tr, &ITransport::info, this, &Session::onInfo);
    connect(tr, &ITransport::ack, this, &Session::onAck);
    connect(tr, &ITransport::stateChanged, this, &Session::onState);
    connect(tr, &ITransport::log, this, [this](const QString &level, const QString &msg) { addEvent(level, msg); });
    connect(tr, &ITransport::traffic, this, &Session::traffic);
    if (auto *can = qobject_cast<CanTransport *>(tr)) {
        can->setServiceUnlocked(m_serviceUnlocked);
    }
    addEvent(QStringLiteral("info"), QStringLiteral("connecting: %1").arg(tr->name()));
    Q_EMIT transportChanged();
    tr->open();
    updateLink();
}

void Session::closeTransport()
{
    if (!m_transport) {
        return;
    }
    stopRecording();
    m_transport->disconnect(this);
    m_transport->close();
    addEvent(QStringLiteral("info"), QStringLiteral("disconnected: %1").arg(m_transport->name()));
    m_transport.reset();
    const QHash<quint64, Pending> pending = std::exchange(m_pending, {});
    const QVector<Queued> queued = std::exchange(m_udsQueue, {});
    m_udsSid = 0;
    for (const Pending &p : pending) {
        if (p.done) {
            Ack a;
            a.id = p.sid;
            a.message = QStringLiteral("disconnected");
            p.done(a);
        }
    }
    for (const Queued &q : queued) {
        if (q.done) {
            Ack a;
            a.id = q.sid;
            a.cmd = q.cmd;
            a.message = QStringLiteral("disconnected");
            q.done(a);
        }
    }
    m_writeQueue.clear();
    m_fps = 0.0;
    m_fpsCount = 0;
    Q_EMIT transportChanged();
    updateLink();
}

quint64 Session::command(const QString &cmd, const QJsonObject &args, std::function<void(const Ack &)> done,
                         int timeoutMs)
{
    if (!m_transport) {
        Ack a;
        a.cmd = cmd;
        a.message = QStringLiteral("not connected");
        if (done) {
            done(a);
        }
        return 0;
    }
    const quint64 sid = ++m_lastSid;
    if (isUds(cmd) && m_udsSid != 0) { // the transport's one UDS slot is taken: wait for its answer
        m_udsQueue.push_back({sid, cmd, args, std::move(done), timeoutMs});
        return sid;
    }
    dispatch(sid, cmd, args, std::move(done), timeoutMs);
    return sid;
}

void Session::dispatch(quint64 sid, const QString &cmd, const QJsonObject &args, std::function<void(const Ack &)> done,
                       int timeoutMs)
{
    QJsonObject shown = args;
    shown.insert(QLatin1String("cmd"), cmd);
    addEvent(QStringLiteral("tx"), QString::fromUtf8(QJsonDocument(shown).toJson(QJsonDocument::Compact)));
    if (cmd == QLatin1String("pause")) { // a paused simulator sends no telemetry: say "paused", not "stale"
        const bool on = args.contains(QLatin1String("on")) ? args.value(QLatin1String("on")).toBool() : !m_simPaused;
        done = [this, on, inner = std::move(done)](const Ack &a) {
            if (a.ok) {
                m_simPaused = on;
                updateLink();
            }
            if (inner) {
                inner(a);
            }
        };
    }
    const bool uds = isUds(cmd);
    if (uds) {
        m_udsSid = sid;
    }
    m_sending = {std::move(done), nowMs() + timeoutMs, sid, uds};
    m_inSend = true; // a transport may refuse synchronously, from inside send()
    const quint64 id = m_transport->send(cmd, args);
    if (m_inSend) { // not answered synchronously: wait for the ack
        m_inSend = false;
        m_pending.insert(id, std::move(m_sending));
    }
}

void Session::udsNext()
{
    m_udsSid = 0;
    if (m_transport && !m_udsQueue.isEmpty()) {
        Queued q = m_udsQueue.takeFirst();
        dispatch(q.sid, q.cmd, q.args, std::move(q.done), q.timeoutMs);
    }
}

void Session::onAck(const Ack &in)
{
    if (!in.ok) {
        addEvent(QStringLiteral("warn"), QStringLiteral("%1 refused: %2").arg(in.cmd, in.message));
    } else if (!in.message.isEmpty()) {
        addEvent(QStringLiteral("info"), QStringLiteral("%1: %2").arg(in.cmd, in.message));
    }
    Pending p; // an ack of no command of ours (a late one, the bridge's unpaired flow control) keeps sid 0
    if (m_inSend) {
        m_inSend = false; // answered from inside send()
        p = std::move(m_sending);
    } else if (m_pending.contains(in.id)) {
        p = m_pending.take(in.id);
    }
    Ack a = in;
    a.id = p.sid;
    Q_EMIT ackReceived(a);
    if (p.done) {
        p.done(a);
    }
    if (p.uds && p.sid == m_udsSid) { // after the callback: a request it sends queues behind the waiting ones
        udsNext();
    }
}

qint64 Session::ageMs() const { return (m_lastRx < 0) ? -1 : nowMs() - m_lastRx; }

qint64 Session::staleAfterMs() const
{
    const double p = m_transport ? m_transport->expectedPeriodMs() : 0.0;
    return std::max<qint64>(250, static_cast<qint64>(4.0 * p));
}

void Session::onFrame(const TelemetryFrame &in)
{
    TelemetryFrame f = in;
    f.rxMs = nowMs();
    m_last = f;
    m_haveFrame = true;
    m_lastRx = f.rxMs;
    m_fpsCount++;
    if (f.rxMs - m_fpsWindowStart >= 1000) {
        m_fps = 1000.0 * m_fpsCount / static_cast<double>(f.rxMs - m_fpsWindowStart);
        m_fpsWindowStart = f.rxMs;
        m_fpsCount = 0;
    }
    m_store.append(f);
    if (m_rec.isOpen()) {
        m_rec.write(f);
    }
    const QVector<DtcEvent> dev = m_dtcs.update(f);
    const ProtocolInfo &pi = ProtocolInfo::instance();
    for (const DtcEvent &e : dev) {
        const QString what = (e.kind == DtcEvent::Kind::Raised) ? QStringLiteral("raised")
                           : (e.kind == DtcEvent::Kind::Cleared) ? QStringLiteral("cleared") : QStringLiteral("again");
        const DtcInfo *d = pi.dtc(e.id);
        addEvent(QStringLiteral("dtc"), QStringLiteral("%1 %2 %3").arg(d ? d->code : QString::number(e.id),
                                                                         pi.dtcName(e.id), what));
    }
    if (!dev.isEmpty()) {
        Q_EMIT dtcEvents(dev);
    }
    for (const AlertEvent &a : m_alerts.evaluate(f)) {
        addEvent(QStringLiteral("alert"), a.text);
        Q_EMIT alertRaised(a);
    }
    updateLink();
    Q_EMIT frame(f);
}

void Session::onInfo(const QJsonObject &info)
{
    for (auto it = info.begin(); it != info.end(); ++it) {
        if (it.key() != QLatin1String("type")) {
            m_info.insert(it.key(), it.value());
        }
    }
    const QString type = info.value(QLatin1String("type")).toString();
    if (type == QLatin1String("hello")) {
        ProtocolInfo::instance().applyHello(info);
        const SkuInfo *sku = ProtocolInfo::instance().sku(info.value(QLatin1String("sku")).toInt());
        if (sku) {
            m_params.setSku(sku->key);
        }
        const QString fw = info.value(QLatin1String("fw_id")).toString();
        addEvent(QStringLiteral("info"), QStringLiteral("device: %1, TI_FW_ID %2%3")
                                             .arg(info.value(QLatin1String("sku_name")).toString(), fw,
                                                  (fw == ProtocolInfo::instance().fwId)
                                                      ? QString()
                                                      : QStringLiteral(" (this build's exports: %1)")
                                                            .arg(ProtocolInfo::instance().fwId)));
    }
    if (info.contains(QLatin1String("params"))) {
        const QStringList mismatch = m_params.setDeviceTable(info.value(QLatin1String("params")).toArray());
        for (const QString &m : mismatch) {
            addEvent(QStringLiteral("warn"), QStringLiteral("parameter table: %1").arg(m));
        }
    }
    const int boot = info.value(QLatin1String("boot")).toInt(-1);
    const bool powerUp = type == QLatin1String("hello") && boot != m_boot; // the key cycle's record: its maps read again
    if (powerUp) {
        m_boot = boot;
        m_info.remove(QStringLiteral("maps"));
        m_recordAsked = false;
    }
    Q_EMIT infoChanged(m_info);
    if (powerUp) {
        readRecord();
    }
}

void Session::onState(ITransport::State s, const QString &detail)
{
    if (s == ITransport::State::Failed) {
        addEvent(QStringLiteral("error"), detail);
    } else if (s == ITransport::State::Open) {
        addEvent(QStringLiteral("info"), QStringLiteral("open: %1").arg(detail));
    }
    updateLink();
}

void Session::updateLink()
{
    Link l = Link::None;
    if (m_transport) {
        switch (m_transport->state()) {
        case ITransport::State::Closed: l = Link::None; break;
        case ITransport::State::Opening: l = Link::Connecting; break;
        case ITransport::State::Failed: l = Link::Failed; break;
        case ITransport::State::Open:
            if (!m_haveFrame) {
                l = Link::Connecting;
            } else if (m_simPaused || m_last.b(QStringLiteral("sim.paused"))) {
                l = Link::Paused;
            } else if (ageMs() > staleAfterMs()) {
                l = Link::Stale;
            } else {
                l = m_transport->isReplay() ? Link::Replay : Link::Live;
            }
            break;
        }
    }
    if (l != m_link) {
        m_link = l;
        m_recordAsked = m_recordAsked && l == Link::Live; // a link that comes back may be a new key cycle's record
        Q_EMIT linkChanged(l);
    }
    readRecord();
}

// The image and its record, from the DIDs (uds_diag.c), in the hello's shape: 0xFD26 the active record's FW-45 inductance
// maps (did_maps: 6 x L_d, 6 x L_q in nH BE32, i_map in 0.1 A BE16) as "maps" on both transports — no hello carries them;
// over CAN also 0xFD23 the image verifier's root of trust (did_root: kind 0 none, 1 TEST, 2 build, 3 OTP/HSE, then the
// key id BE32) as "root" and 0xFD25 the record's motor data (did_motor: ψ µWb BE32, pole pairs, L_d and L_q nH BE32,
// R_s µΩ BE32, n_max rpm BE16, i_d,demag 0.1 A BE16) as "motor", which the bridge's hello carries. One request, once the
// link is live (over CAN once the service key lets the tool transmit), and again when the link comes back or the
// bridge powers up (a new key cycle runs its record); an image without a DID leaves its key alone.
void Session::readRecord()
{
    const bool can = m_transport && m_transport->kind() == ITransport::Kind::Can;
    if (m_recordAsked || m_link != Link::Live || !m_transport->commands().contains(QStringLiteral("uds")) || (can && !m_serviceUnlocked)) {
        return;
    }
    m_recordAsked = true;
    command(QStringLiteral("uds"), {{QStringLiteral("msg"), can ? QStringLiteral("22 FD 23 FD 25 FD 26") : QStringLiteral("22 FD 26")}},
            [this](const Ack &a) {
        const QByteArray r = CanCodec::fromHex(a.raw.value(QLatin1String("msg")).toString());
        if (r.isEmpty() || static_cast<quint8>(r[0]) != 0x62) {
            addEvent(QStringLiteral("warn"), QStringLiteral("the record's DIDs (0xFD23, 0xFD25, 0xFD26) not read: %1")
                                                 .arg(r.isEmpty() ? a.message : QString::fromLatin1(r.toHex(' ').toUpper())));
            return;
        }
        for (int i = 1; i + 2 <= r.size();) {
            const int did = (static_cast<quint8>(r[i]) << 8) | static_cast<quint8>(r[i + 1]);
            const int len = did == 0xFD23 ? 5 : did == 0xFD25 ? 21 : did == 0xFD26 ? 50 : -1;
            if (len < 0 || i + 2 + len > r.size()) {
                break;
            }
            const auto *d = reinterpret_cast<const uchar *>(r.constData()) + i + 2;
            if (did == 0xFD23) {
                static const char *const KIND[] = {"none", "test", "build", "otp"};
                const quint32 id = qFromBigEndian<quint32>(d + 1);
                m_info.insert(QStringLiteral("root"),
                              QJsonObject{{QStringLiteral("kind"), d[0] < 4 ? QLatin1String(KIND[d[0]]) : QString::number(d[0])},
                                          {QStringLiteral("key_id"), QStringLiteral("0x%1").arg(id, 8, 16, QLatin1Char('0')).toUpper().replace(QLatin1String("0X"), QLatin1String("0x"))},
                                          {QStringLiteral("source"), QStringLiteral("DID 0xFD23")}});
            } else if (did == 0xFD25) {
                m_info.insert(QStringLiteral("motor"),
                              QJsonObject{{QStringLiteral("psi_wb"), qFromBigEndian<quint32>(d) * 1e-6},
                                          {QStringLiteral("pp"), d[4]},
                                          {QStringLiteral("ld_h"), qFromBigEndian<quint32>(d + 5) * 1e-9},
                                          {QStringLiteral("lq_h"), qFromBigEndian<quint32>(d + 9) * 1e-9},
                                          {QStringLiteral("rs_ohm"), qFromBigEndian<quint32>(d + 13) * 1e-6},
                                          {QStringLiteral("n_max_rpm"), qFromBigEndian<quint16>(d + 17)},
                                          {QStringLiteral("id_demag_a"), qFromBigEndian<quint16>(d + 19) * 0.1},
                                          {QStringLiteral("source"), QStringLiteral("DID 0xFD25")}});
            } else {
                QJsonArray ld, lq;
                for (int k = 0; k < 6; k++) { // motor.h MOTOR_MAP_N
                    ld.append(qFromBigEndian<quint32>(d + 4 * k) * 1e-9);
                    lq.append(qFromBigEndian<quint32>(d + 24 + 4 * k) * 1e-9);
                }
                m_info.insert(QStringLiteral("maps"), QJsonObject{{QStringLiteral("ld_h"), ld},
                                                                 {QStringLiteral("lq_h"), lq},
                                                                 {QStringLiteral("i_map_a"), qFromBigEndian<quint16>(d + 48) * 0.1},
                                                                 {QStringLiteral("source"), QStringLiteral("DID 0xFD26")}});
            }
            i += 2 + len;
        }
        Q_EMIT infoChanged(m_info);
    });
}

void Session::addEvent(const QString &level, const QString &text)
{
    LogEntry e{nowMs(), QDateTime::currentDateTime(), level, text};
    m_events.push_back(e);
    if (m_events.size() > 5000) {
        m_events.remove(0, m_events.size() - 5000);
    }
    Q_EMIT eventLogged(e);
}

bool Session::unlockService(const QString &key)
{
    const QByteArray h = QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256).toHex();
    const bool ok = (h == QByteArray(SERVICE_KEY_SHA256));
    if (ok) {
        m_serviceUnlocked = true;
        if (auto *can = qobject_cast<CanTransport *>(m_transport.get())) {
            can->setServiceUnlocked(true);
        }
        addEvent(QStringLiteral("warn"), QStringLiteral("service key accepted: destructive controls unlocked"));
        Q_EMIT serviceChanged(true);
        m_recordAsked = false;
        readRecord();
    } else {
        addEvent(QStringLiteral("warn"), QStringLiteral("service key refused"));
    }
    return ok;
}

void Session::lockService()
{
    m_serviceUnlocked = false;
    if (auto *can = qobject_cast<CanTransport *>(m_transport.get())) {
        can->setServiceUnlocked(false);
    }
    addEvent(QStringLiteral("info"), QStringLiteral("service lock engaged"));
    Q_EMIT serviceChanged(false);
}

bool Session::destructiveAllowed() const
{
    if (!m_transport || m_transport->isReplay()) {
        return false;
    }
    return m_transport->isSimulator() || m_serviceUnlocked;
}

bool Session::startRecording(const QString &path, QString *err)
{
    if (!m_rec.open(path, err)) {
        return false;
    }
    addEvent(QStringLiteral("info"), QStringLiteral("recording to %1").arg(path));
    Q_EMIT recordingChanged(true);
    return true;
}

void Session::stopRecording()
{
    if (!m_rec.isOpen()) {
        return;
    }
    addEvent(QStringLiteral("info"), QStringLiteral("recording stopped: %1 frames in %2").arg(m_rec.frames()).arg(m_rec.path()));
    m_rec.close();
    Q_EMIT recordingChanged(false);
}

bool Session::writeParams(QString *err)
{
    if (!m_writeQueue.isEmpty()) {
        *err = QStringLiteral("a write is already in progress");
        return false;
    }
    const QStringList problems = m_params.validate();
    if (!problems.isEmpty()) {
        *err = problems.join(QLatin1Char('\n'));
        return false;
    }
    if (!m_transport || !m_transport->commands().contains(QStringLiteral("param_set"))) {
        *err = QStringLiteral("this transport cannot write parameters");
        return false;
    }
    m_writeQueue = m_params.pendingWrites();
    if (m_writeQueue.isEmpty()) {
        *err = QStringLiteral("nothing to write");
        return false;
    }
    m_writeOk = 0;
    m_writeRefused = 0;
    writeNextParam();
    return true;
}

void Session::writeNextParam()
{
    if (m_writeQueue.isEmpty()) {
        Q_EMIT paramsWritten(m_writeOk, m_writeRefused);
        return;
    }
    const auto [name, value] = m_writeQueue.takeFirst();
    command(QStringLiteral("param_set"), {{QStringLiteral("name"), name}, {QStringLiteral("value"), value}},
            [this, name = name](const Ack &a) {
                const double inForce = a.raw.value(QLatin1String("value")).toDouble(qQNaN());
                m_params.writeResult(name, a.ok, inForce, a.message);
                a.ok ? m_writeOk++ : m_writeRefused++;
                writeNextParam();
            });
}
