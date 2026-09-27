#include "CanTransport.h"

#include <QCanBus>
#include <QCanBusFrame>
#include <QVariant>

namespace {
constexpr qint64 UDS_WAIT_MS = 100; // for the response to a request, and then for each of its consecutive frames

QString hexOf(const QByteArray &b) { return QString::fromLatin1(b.toHex(' ').toUpper()); }

CanCodec::Gear gearOf(const QString &g)
{
    const QString u = g.trimmed().toUpper();
    return u.startsWith(QLatin1Char('D')) ? CanCodec::Gear::D
         : u.startsWith(QLatin1Char('R')) ? CanCodec::Gear::R
         : u.startsWith(QLatin1Char('P')) ? CanCodec::Gear::P : CanCodec::Gear::N;
}
const char *gearName(CanCodec::Gear g)
{
    static const char *const N[] = {"N", "D", "R", "P"};
    return N[static_cast<int>(g) & 3];
}
} // namespace

QCanBusDevice *CanTransport::createDevice(const Options &o, QString *err)
{
    if (!QCanBus::instance()->plugins().contains(o.plugin)) {
        *err = QStringLiteral("CAN plugin '%1' is not available on this machine (available: %2)")
                   .arg(o.plugin, QCanBus::instance()->plugins().join(QStringLiteral(", ")));
        return nullptr;
    }
    QString e;
    QCanBusDevice *d = QCanBus::instance()->createDevice(o.plugin, o.interface, &e);
    if (d == nullptr) {
        *err = QStringLiteral("%1/%2: %3").arg(o.plugin, o.interface, e);
        return nullptr;
    }
    d->setConfigurationParameter(QCanBusDevice::BitRateKey, o.bitrate);
    d->setConfigurationParameter(QCanBusDevice::CanFdKey, o.fd);
    if (o.fd) {
        d->setConfigurationParameter(QCanBusDevice::DataBitRateKey, o.dataBitrate);
    }
    return d;
}

CanTransport::CanTransport(QCanBusDevice *device, QString description, QObject *parent)
    : ITransport(parent), m_dev(device), m_desc(std::move(description)),
      m_tx([this](const QByteArray &f, bool) { return write(CanCodec::ID_UDS_REQ, f, true); })
{
    m_dev->setParent(this);
    m_clock.start();
    connect(m_dev, &QCanBusDevice::framesReceived, this, &CanTransport::onFrames);
    connect(m_dev, &QCanBusDevice::errorOccurred, this, [this](QCanBusDevice::CanBusError e) {
        if (e == QCanBusDevice::NoError) {
            return;
        }
        Q_EMIT log(QStringLiteral("error"), QStringLiteral("CAN: %1").arg(m_dev->errorString()));
        if (e == QCanBusDevice::ConnectionError) {
            setState(State::Failed, m_dev->errorString());
        }
    });
    connect(m_dev, &QCanBusDevice::stateChanged, this, [this](QCanBusDevice::CanBusDeviceState s) {
        if (s == QCanBusDevice::ConnectedState) {
            setState(State::Open, m_desc);
        } else if (s == QCanBusDevice::UnconnectedState && state() != State::Failed) {
            setState(State::Closed);
        }
    });
    m_vcuTimer.setInterval(10); // VCU_CMD/VCU_BMS every 10 ms, as the bench VCU does (FW-11 staleness 20 ms)
    m_vcuTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_vcuTimer, &QTimer::timeout, this, &CanTransport::vcuTick);
    m_udsTimer.setInterval(20);
    connect(&m_udsTimer, &QTimer::timeout, this, [this] {
        while (!m_uds.isEmpty() && now() > m_uds.head().deadline) {
            Ack a;
            a.id = m_uds.head().id;
            a.cmd = QStringLiteral("uds");
            a.ok = true;
            a.message = m_rx.active ? QStringLiteral("segmented response incomplete (no consecutive frame within 100 ms)")
                                    : QStringLiteral("no response within 100 ms");
            a.raw = m_uds.head().extra;
            m_uds.dequeue();
            m_rx.active = false;
            Q_EMIT ack(a);
        }
        if (m_uds.isEmpty()) {
            m_udsTimer.stop();
        }
    });
    // a segmented request: every frame out, the response is awaited as a single frame's; or abandoned, with the reason
    connect(&m_tx, &IsoTpSender::sent, this, [this](const QJsonObject &info) {
        m_uds.enqueue({m_txId, now() + UDS_WAIT_MS, info});
        m_udsTimer.start();
    });
    connect(&m_tx, &IsoTpSender::failed, this, [this](const QString &why, const QJsonObject &info) {
        Ack a;
        a.id = m_txId;
        a.cmd = QStringLiteral("uds");
        a.message = why;
        a.raw = info;
        Q_EMIT ack(a);
    });
    m_cmd.contactors = CanCodec::Contactors::Open;
    m_cmd.gear = CanCodec::Gear::N;
    m_cmd.coolantC = 50.0f;
    m_bms.packV = 750.0f;
    m_bms.chgW = 100000.0f;
    m_bms.disW = 250000.0f;
}

CanTransport::~CanTransport()
{
    m_vcuTimer.stop();
    if (m_dev->state() == QCanBusDevice::ConnectedState) {
        m_dev->disconnectDevice();
    }
}

void CanTransport::open()
{
    setState(State::Opening, m_desc);
    if (!m_dev->connectDevice()) {
        setState(State::Failed, QStringLiteral("cannot connect: %1").arg(m_dev->errorString()));
    }
}

void CanTransport::close()
{
    m_tx.abort();
    m_vcuTimer.stop();
    m_dev->disconnectDevice();
    setState(State::Closed);
}

void CanTransport::setServiceUnlocked(bool on)
{
    m_unlocked = on;
    if (!on) {
        m_tx.cancel(QStringLiteral("service lock: the segmented request was abandoned"));
    }
    if (!on && m_vcuTimer.isActive()) {
        m_vcuTimer.stop(); // the emulated VCU goes silent: the inverter sees a stale command (FW-11 ramp to zero)
        Q_EMIT log(QStringLiteral("warn"), QStringLiteral("service lock: VCU emulation stopped"));
    }
}

QStringList CanTransport::commands() const
{
    return {QStringLiteral("status"), QStringLiteral("vcu"), QStringLiteral("arm"), QStringLiteral("disarm"),
            QStringLiteral("enable"), QStringLiteral("torque"), QStringLiteral("gear"), QStringLiteral("contactors"),
            QStringLiteral("fault_reset"), QStringLiteral("retry_auth"), QStringLiteral("discharge"),
            QStringLiteral("shutdown"), QStringLiteral("coolant"), QStringLiteral("vspeed"), QStringLiteral("bms"), QStringLiteral("send"),
            QStringLiteral("uds")};
}

bool CanTransport::write(quint32 id, const QByteArray &payload, bool logIt)
{
    QCanBusFrame f(id, payload);
    f.setFlexibleDataRateFormat(payload.size() > 8);
    const bool ok = m_dev->writeFrame(f);
    ok ? m_stats.tx++ : m_stats.txErr++;
    if (logIt) {
        Q_EMIT traffic(QStringLiteral("tx"), QStringLiteral("0x%1 [%2] %3%4")
                                                 .arg(id, 3, 16, QLatin1Char('0')).arg(payload.size())
                                                 .arg(hexOf(payload), ok ? QString() : QStringLiteral("  (write failed)")));
    }
    return ok;
}

quint64 CanTransport::send(const QString &cmd, const QJsonObject &args)
{
    const quint64 id = nextId();
    if (cmd == QLatin1String("status")) {
        accept(id, cmd, QStringLiteral("rx %1 (INV_STATUS %2), crc %3, frozen %4, jump %5, length %6, error frames %7; "
                                       "tx %8, tx errors %9; VCU emulation %10")
                            .arg(m_stats.rx).arg(m_stats.status).arg(m_stats.crcErr).arg(m_stats.frozen)
                            .arg(m_stats.jump).arg(m_stats.badLen).arg(m_stats.errorFrames).arg(m_stats.tx)
                            .arg(m_stats.txErr).arg(m_vcuTimer.isActive() ? QStringLiteral("on") : QStringLiteral("off")));
        return id;
    }
    if (!commands().contains(cmd)) {
        refuse(id, cmd, QStringLiteral("'%1' exists on the simulator only").arg(cmd));
        return id;
    }
    if (state() != State::Open) {
        refuse(id, cmd, QStringLiteral("the CAN adapter is not connected"));
        return id;
    }
    if (!m_unlocked) {
        refuse(id, cmd, QStringLiteral("transmitting on a real bus needs the service key (Tools ▸ Service key)"));
        return id;
    }
    const qint64 t = now();
    const bool needsVcu = cmd != QLatin1String("vcu") && cmd != QLatin1String("send") && cmd != QLatin1String("uds") &&
                          cmd != QLatin1String("bms") && cmd != QLatin1String("coolant") && cmd != QLatin1String("vspeed") && cmd != QLatin1String("gear") &&
                          cmd != QLatin1String("contactors");
    if (needsVcu && !m_vcuTimer.isActive()) {
        refuse(id, cmd, QStringLiteral("start the VCU emulation first (command 'vcu on=true')"));
        return id;
    }
    if (cmd == QLatin1String("vcu")) {
        const bool on = args.value(QLatin1String("on")).toBool(true);
        if (on && !m_vcuTimer.isActive()) {
            m_cmd.enable = false;
            m_cmd.torqueNm = 0.0f;
            m_vcuTimer.start();
        } else if (!on) {
            m_vcuTimer.stop();
        }
        accept(id, cmd, on ? QStringLiteral("VCU_CMD/VCU_BMS every 10 ms") : QStringLiteral("VCU emulation stopped"));
    } else if (cmd == QLatin1String("arm")) {
        m_cmd.enable = true;
        accept(id, cmd, QStringLiteral("enable set; the contactor report follows the bench ('contactors')"));
    } else if (cmd == QLatin1String("disarm")) {
        m_cmd.torqueNm = 0.0f;
        m_cmd.enable = false;
        accept(id, cmd);
    } else if (cmd == QLatin1String("enable")) {
        m_cmd.enable = args.value(QLatin1String("on")).toBool(m_cmd.enable);
        accept(id, cmd);
    } else if (cmd == QLatin1String("torque")) {
        const double nm = args.value(QLatin1String("nm")).toDouble(qQNaN());
        if (!(std::abs(nm) <= 3276.7)) {
            refuse(id, cmd, QStringLiteral("nm required, |nm| <= 3276.7 (the frame's range)"));
            return id;
        }
        m_cmd.torqueNm = static_cast<float>(nm);
        const double hold = args.value(QLatin1String("hold_ms")).toDouble(0.0);
        m_holdUntil = (hold > 0.0) ? t + static_cast<qint64>(std::min(hold, 60000.0)) : 0;
        accept(id, cmd);
    } else if (cmd == QLatin1String("gear")) {
        m_cmd.gear = gearOf(args.value(QLatin1String("gear")).toString());
        accept(id, cmd);
    } else if (cmd == QLatin1String("contactors")) {
        const QString s = args.value(QLatin1String("state")).toString().toLower();
        m_cmd.contactors = (s == QLatin1String("closed")) ? CanCodec::Contactors::Closed
                         : (s == QLatin1String("precharge")) ? CanCodec::Contactors::Precharge
                         : (s == QLatin1String("open")) ? CanCodec::Contactors::Open : CanCodec::Contactors::Invalid;
        accept(id, cmd, QStringLiteral("reporting %1").arg(s.isEmpty() ? QStringLiteral("invalid") : s));
    } else if (cmd == QLatin1String("fault_reset")) {
        m_faultResetUntil = t + 100;
        accept(id, cmd);
    } else if (cmd == QLatin1String("retry_auth")) {
        m_retryUntil = t + 3000;
        accept(id, cmd);
    } else if (cmd == QLatin1String("discharge")) {
        m_dischargeUntil = t + 6000;
        accept(id, cmd, (m_cmd.contactors == CanCodec::Contactors::Open)
                            ? QString()
                            : QStringLiteral("the firmware fires QDIS only with the contactors reported open (FW-17)"));
    } else if (cmd == QLatin1String("shutdown")) {
        m_cmd.shutdown = args.value(QLatin1String("on")).toBool(true);
        accept(id, cmd);
    } else if (cmd == QLatin1String("coolant")) {
        const double c = args.value(QLatin1String("c")).toDouble(qQNaN());
        m_cmd.coolantValid = !std::isnan(c);
        m_cmd.coolantC = m_cmd.coolantValid ? static_cast<float>(c) : 0.0f;
        accept(id, cmd);
    } else if (cmd == QLatin1String("vspeed")) { // round 23 (FW-39): the bench vehicle's speed, valid unless said otherwise
        const double kmh = args.value(QLatin1String("kmh")).toDouble(qQNaN());
        if (!(kmh >= 0.0 && kmh <= 655.35)) {
            refuse(id, cmd, QStringLiteral("kmh must be 0..655.35"));
            return id;
        }
        m_cmd.vspeedKmh = static_cast<float>(kmh);
        m_cmd.vspeedValid = args.value(QLatin1String("valid")).toBool(true);
        accept(id, cmd);
    } else if (cmd == QLatin1String("bms")) {
        if (args.contains(QLatin1String("chg_kw"))) {
            m_bms.chgW = static_cast<float>(std::max(0.0, args.value(QLatin1String("chg_kw")).toDouble() * 1000.0));
        }
        if (args.contains(QLatin1String("dis_kw"))) {
            m_bms.disW = static_cast<float>(std::max(0.0, args.value(QLatin1String("dis_kw")).toDouble() * 1000.0));
        }
        if (args.contains(QLatin1String("pack_v"))) {
            m_bms.packV = static_cast<float>(args.value(QLatin1String("pack_v")).toDouble());
        }
        m_bmsTx = args.value(QLatin1String("on")).toBool(m_bmsTx);
        accept(id, cmd);
    } else if (cmd == QLatin1String("send")) {
        const QByteArray p = CanCodec::fromHex(args.value(QLatin1String("hex")).toString());
        const int cid = args.value(QLatin1String("can_id")).toInt(-1);
        if (cid < 0 || cid > 0x7FF || p.isEmpty() || p.size() > 64) {
            refuse(id, cmd, QStringLiteral("can_id (11-bit) and hex (1..64 bytes) required"));
            return id;
        }
        write(static_cast<quint32>(cid), p, true) ? accept(id, cmd) : refuse(id, cmd, m_dev->errorString());
    } else if (cmd == QLatin1String("uds") && m_tx.active()) {
        refuse(id, cmd, QStringLiteral("a segmented UDS request is in flight"));
    } else if (cmd == QLatin1String("uds") && args.contains(QLatin1String("msg"))) { // any length: ISO 15765-2 (IsoTp.h)
        const QVector<QByteArray> fr = IsoTp::frames(CanCodec::fromHex(args.value(QLatin1String("msg")).toString()));
        QString err;
        if (fr.isEmpty()) {
            refuse(id, cmd, QStringLiteral("msg: the UDS request, hex, SID first"));
        } else if (fr.size() > 1 && !m_uds.isEmpty()) {
            refuse(id, cmd, QStringLiteral("a segmented request waits until the pending responses are in"));
        } else if (fr.size() > 1) {
            m_txId = id;
            if (!m_tx.start(fr, &err)) {
                refuse(id, cmd, err);
            }
        } else if (!write(CanCodec::ID_UDS_REQ, fr[0], true)) {
            refuse(id, cmd, m_dev->errorString());
        } else {
            m_uds.enqueue({id, t + UDS_WAIT_MS, {}});
            m_udsTimer.start();
        }
    } else if (cmd == QLatin1String("uds")) {
        QByteArray p = CanCodec::fromHex(args.value(QLatin1String("hex")).toString());
        if (p.isEmpty() || p.size() > 8) {
            refuse(id, cmd, QStringLiteral("hex: an ISO 15765-2 single frame (PCI + payload, <= 8 bytes)"));
            return id;
        }
        p.append(QByteArray(8 - p.size(), '\0'));
        if (!write(CanCodec::ID_UDS_REQ, p, true)) {
            refuse(id, cmd, m_dev->errorString());
            return id;
        }
        m_uds.enqueue({id, t + UDS_WAIT_MS, {}});
        m_udsTimer.start();
    }
    return id;
}

void CanTransport::vcuTick()
{
    const qint64 t = now();
    if (m_holdUntil > 0 && t > m_holdUntil) {
        m_cmd.torqueNm = 0.0f; // deadman: the torque was not refreshed within its hold time
        m_holdUntil = 0;
    }
    CanCodec::VcuCmd c = m_cmd;
    c.faultReset = t < m_faultResetUntil;
    c.retryAuth = t < m_retryUntil;
    c.discharge = t < m_dischargeUntil;
    write(CanCodec::ID_VCU_CMD, CanCodec::encodeVcuCmd(c), false);
    m_cmd.ctr = static_cast<quint8>((m_cmd.ctr + 1) & 0x0F);
    if (m_bmsTx) {
        write(CanCodec::ID_VCU_BMS, CanCodec::encodeVcuBms(m_bms), false);
        m_bms.ctr = static_cast<quint8>((m_bms.ctr + 1) & 0x0F);
    }
}

void CanTransport::onFrames()
{
    while (m_dev->framesAvailable() > 0) {
        const QCanBusFrame f = m_dev->readFrame();
        m_stats.rx++;
        if (f.frameType() == QCanBusFrame::ErrorFrame) {
            m_stats.errorFrames++;
            Q_EMIT traffic(QStringLiteral("can"), QStringLiteral("error frame: %1").arg(m_dev->interpretErrorFrame(f)));
            continue;
        }
        const QByteArray p = f.payload();
        switch (f.frameId()) {
        case CanCodec::ID_INV_STATUS: handleStatus(f); break;
        case CanCodec::ID_VCU_CMD:
            if (const auto c = CanCodec::decodeVcuCmd(p)) {
                m_seenCmd = c;
            }
            break;
        case CanCodec::ID_VCU_BMS:
            if (const auto b = CanCodec::decodeVcuBms(p)) {
                m_seenBms = b;
            }
            break;
        case CanCodec::ID_UDS_RSP: udsFrame(p); break;
        default:
            Q_EMIT traffic(QStringLiteral("can"), QStringLiteral("0x%1 [%2] %3")
                                                      .arg(f.frameId(), 3, 16, QLatin1Char('0')).arg(p.size())
                                                      .arg(hexOf(p)));
            break;
        }
    }
}

// One frame on 0x7E9 (ISO 15765-2, FW-40): the flow control a segmented request of ours waits for, a single frame
// (classic 0L, CAN-FD escape 00 LL), a first frame (12-bit length; answered with ContinueToSend, BS 0, STmin 0, when the
// request is ours and the service key allows transmit — another tester flow-controls its own), or one of its
// consecutive frames (sequence number checked).
void CanTransport::udsFrame(const QByteArray &p)
{
    Q_EMIT traffic(QStringLiteral("rx"), QStringLiteral("0x7E9 [%1] %2").arg(p.size()).arg(hexOf(p)));
    if (p.isEmpty() || m_tx.flowControl(p)) {
        return;
    }
    const auto b0 = static_cast<quint8>(p[0]);
    if (m_rx.active && (b0 >> 4) == 2) {
        if ((b0 & 0x0F) != m_rx.sn) {
            udsDone(QStringLiteral("consecutive frame out of sequence"));
            return;
        }
        m_rx.msg += p.mid(1, m_rx.len - static_cast<int>(m_rx.msg.size()));
        m_rx.frames++;
        m_rx.sn = static_cast<quint8>((m_rx.sn + 1) & 0x0F);
        if (!m_uds.isEmpty()) {
            m_uds.head().deadline = now() + UDS_WAIT_MS;
        }
        if (m_rx.msg.size() >= m_rx.len) {
            udsDone({});
        }
        return;
    }
    if (m_rx.active) {
        udsDone(QStringLiteral("segmented response abandoned by the ECU"));
    }
    if ((b0 >> 4) > 1) {
        return; // a flow control nobody here waits for, or a stray consecutive frame
    }
    m_rx = {};
    m_rx.first = hexOf(p);
    m_rx.frames = 1;
    if ((b0 >> 4) == 1) {
        m_rx.len = ((b0 & 0x0F) << 8) | (p.size() > 1 ? static_cast<quint8>(p[1]) : 0);
        m_rx.msg = p.mid(2);
        if (m_rx.len <= m_rx.msg.size()) {
            udsDone(QStringLiteral("first frame length invalid"));
            return;
        }
        m_rx.active = true;
        m_rx.sn = 1;
        if (!m_uds.isEmpty() && m_unlocked) {
            write(CanCodec::ID_UDS_REQ, QByteArray::fromHex("3000000000000000"), true);
            m_uds.head().deadline = now() + UDS_WAIT_MS;
        }
        return;
    }
    const int n = (b0 != 0) ? b0 : (p.size() > 1 ? static_cast<quint8>(p[1]) : 0);
    m_rx.msg = p.mid(b0 != 0 ? 1 : 2, n);
    udsDone({});
}

// A response complete (or broken): the ack of the oldest request, as the bridge acknowledges its "uds" (PROTOCOL.md
// A.4.5): rsp = the first frame as received, msg = the UDS payload, frames, rsp_id.
void CanTransport::udsDone(const QString &err)
{
    m_rx.active = false;
    if (m_uds.isEmpty()) {
        return; // the response to another tester's request
    }
    Ack a;
    a.raw = m_uds.head().extra;
    a.id = m_uds.dequeue().id;
    a.cmd = QStringLiteral("uds");
    a.ok = err.isEmpty();
    a.message = a.ok ? m_rx.first : err;
    a.raw.insert(QLatin1String("rsp"), m_rx.first);
    a.raw.insert(QLatin1String("msg"), hexOf(m_rx.msg));
    a.raw.insert(QLatin1String("frames"), m_rx.frames);
    a.raw.insert(QLatin1String("rsp_id"), static_cast<int>(CanCodec::ID_UDS_RSP));
    Q_EMIT ack(a);
}

void CanTransport::handleStatus(const QCanBusFrame &f)
{
    const QByteArray p = f.payload();
    if (p.size() != CanCodec::LEN_STATUS && p.size() != CanCodec::LEN_STATUS_LEGACY) {
        m_stats.badLen++;
        return;
    }
    const auto s = CanCodec::decodeInvStatus(p);
    if (!s) {
        m_stats.crcErr++;
        return;
    }
    const CanCodec::AliveCounter::Result r = m_statusCtr.check(s->ctr);
    if (r == CanCodec::AliveCounter::Result::Frozen) {
        m_stats.frozen++; // a repeated counter: the inverter's status is not fresh — never shown as live
        return;
    }
    if (r == CanCodec::AliveCounter::Result::Jump) {
        m_stats.jump++; // frames lost; the value itself is still the inverter's latest
    }
    m_stats.status++;
    TelemetryFrame fr;
    fr.source = QStringLiteral("can");
    fr.tMs = static_cast<double>(now());
    CanCodec::addChannels(*s, fr);
    fr.text.insert(QStringLiteral("can"), QString::fromLatin1(p.toHex().toUpper()));
    if (s->firstDtc != 0) { // CAN carries a summary: the confirmed count and the first active DTC
        fr.dtcs.push_back({s->firstDtc, 0x09, 1, 0.0, 0.0});
    }
    fr.dtcListComplete = false;
    auto put = [&fr](const char *k, double v) { fr.num.insert(QLatin1String(k), v); };
    put("can.rx", static_cast<double>(m_stats.rx));
    put("can.crc_err", static_cast<double>(m_stats.crcErr));
    put("can.frozen", static_cast<double>(m_stats.frozen));
    put("can.jump", static_cast<double>(m_stats.jump));
    put("can.tx", static_cast<double>(m_stats.tx));
    put("can.vcu_emulation", m_vcuTimer.isActive());
    addVcuChannels(fr);
    Q_EMIT frame(fr);
}

void CanTransport::addVcuChannels(TelemetryFrame &f) const
{
    if (m_vcuTimer.isActive()) {
        f.num.insert(QStringLiteral("vcu.enable"), m_cmd.enable);
        f.num.insert(QStringLiteral("vcu.torque_tx_nm"), m_cmd.torqueNm);
        f.num.insert(QStringLiteral("vcu.contactors"), static_cast<int>(m_cmd.contactors));
        f.text.insert(QStringLiteral("vcu.gear"), QLatin1String(gearName(m_cmd.gear)));
    } else if (m_seenCmd) { // a real VCU on the bus
        f.num.insert(QStringLiteral("vcu.enable"), m_seenCmd->enable);
        f.num.insert(QStringLiteral("vcu.torque_tx_nm"), m_seenCmd->torqueNm);
        f.num.insert(QStringLiteral("vcu.contactors"), static_cast<int>(m_seenCmd->contactors));
        f.text.insert(QStringLiteral("vcu.gear"), QLatin1String(gearName(m_seenCmd->gear)));
    }
    const std::optional<CanCodec::VcuBms> b = m_vcuTimer.isActive() ? std::optional<CanCodec::VcuBms>(m_bms) : m_seenBms;
    if (b) {
        f.num.insert(QStringLiteral("link.v_pack_v"), b->packV);
        f.num.insert(QStringLiteral("limits.p_chg_w"), b->chgW);
        f.num.insert(QStringLiteral("limits.p_dis_w"), b->disW);
    }
}
