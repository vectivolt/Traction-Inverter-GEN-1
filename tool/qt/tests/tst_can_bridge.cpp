// CanTransport end to end against the real firmware over a real QCanBusDevice, without hardware: the simulator bridge
// (the firmware's host build on its plant) on Qt's virtual CAN-FD bus through BridgeCanGateway, the tool on the same bus
// as on an adapter. Locked, nothing leaves the tool. Unlocked, its emulated VCU arms the firmware — the contactor report
// (precharge, then closed) is what the plant's contactors follow — and the tool decodes ARMED from INV_STATUS; the dyno
// at 1500 rpm through the bridge; 50 N·m -> RUN with the applied torque within 10 %; the periodic stream on 0x6E9 at its
// 10 ms period; faults injected through the bridge, then 19 02 FF: a segmented response reassembled by the tool's
// ISO 15765-2, its confirmed DTCs exactly the simulator's; a corrupted INV_STATUS from another node rejected by the
// tool's E2E check; relocked, the tool refuses to transmit and the firmware sees its VCU go silent. And FW-46 over CAN
// through the Session: the record's motor data from DID 0xFD25 (read with DIDs 0xFD23 and 0xFD26 — the record's maps —
// once the service key is in: none before), the example ripple CSV imported with that ψ and pole pairs — the same table the simulator's hello gives —
// written (2E FD 46: the tool's first frame and consecutive frame under the firmware's flow control), read back equal
// (22 FD 46: the firmware's segmented response under the tool's), and read again on its own.
// The waits run the event loop (not QTest::qWait, which sleeps 10 ms between passes): the VCU's 10 ms frames keep
// their timing, and the firmware ramps the torque out after 20 ms without one (FW-11). A host too loaded to schedule
// either process for 10 ms still makes one late (the firmware's own reaction, CMD_LOST + DTC_CAN_TIMEOUT, is transient):
// the torque window below is judged on its median, and the stale events are reported.
// Skips when the bridge is not built or this Qt has no virtualcan plugin.
#include "BridgeCanGateway.h"
#include "CanTransport.h"
#include "CommissioningPage.h"

#include <QCanBus>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QTcpServer>
#include <QtTest>

#include <algorithm>
#include <functional>
#include <memory>
#include <numeric>

namespace {
bool until(const std::function<bool()> &pred, int ms)
{
    QTimer wake; // processEvents() below returns at least this often
    wake.start(5);
    const QDeadlineTimer deadline(ms);
    while (!pred()) {
        if (deadline.hasExpired()) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::WaitForMoreEvents);
    }
    return true;
}
void run(int ms) { until([] { return false; }, ms); }

int dtcId(const QJsonObject &hello, const QString &name)
{
    const QJsonArray names = hello.value(QLatin1String("dtcs")).toArray();
    for (int i = 0; i < names.size(); i++) {
        if (names[i].toString() == name) {
            return i;
        }
    }
    return -1;
}
// The virtualcan plugin runs one server per process, on the port of the first device opened: every test here uses that
// one, a free port of this process's own (never another program's bus on the default port 35468).
quint16 busPort()
{
    static const quint16 port = [] {
        QTcpServer probe;
        return probe.listen(QHostAddress::LocalHost, 0) ? probe.serverPort() : quint16(0);
    }();
    return port;
}

QSet<int> confirmed(const TelemetryFrame &sim)
{
    QSet<int> s;
    for (const DtcState &d : sim.dtcs) {
        if (d.confirmed()) {
            s.insert(d.id);
        }
    }
    return s;
}
} // namespace

class TstCanBridge : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void firmwareOnVirtualCan();
    void rippleOverCan();
};

void TstCanBridge::firmwareOnVirtualCan()
{
    BridgeTransport::Options bo;
    bo.program = BridgeTransport::locate();
    if (bo.program.isEmpty()) {
        QSKIP("simulator bridge not built (make -C tool/bridge): the CAN bridge test did not run");
    }
    if (!QCanBus::instance()->plugins().contains(QStringLiteral("virtualcan"))) {
        QSKIP("this Qt SerialBus has no virtualcan plugin: the CAN bridge test did not run");
    }
    bo.rateHz = 20; // the simulator's truth (DTCs, VCU freshness): 20 Hz is plenty
    QLoggingCategory::setFilterRules(QStringLiteral("qt.canbus.plugins.virtualcan.info=false")); // its connect chatter
    QVERIFY(busPort() != 0);
    CanTransport::Options co;
    co.plugin = QStringLiteral("virtualcan");
    co.interface = QStringLiteral("tcp://127.0.0.1:%1/can0").arg(busPort());

    // what the signals below fill (declared before the objects that emit into them)
    TelemetryFrame inv, sim;
    int simFrames = 0, rogueShown = 0;
    QJsonObject hello;
    QHash<quint64, Ack> acks, bridgeAcks;
    QStringList txLines;
    QVector<qint64> periodicNs;
    QByteArray periodicPdids;
    QString gwFailure;
    QElapsedTimer clock;
    clock.start();

    QString err;
    QCanBusDevice *gwDev = CanTransport::createDevice(co, &err);
    QVERIFY2(gwDev, qPrintable(err));
    BridgeCanGateway gw(bo, gwDev);
    QCanBusDevice *toolDev = CanTransport::createDevice(co, &err);
    QVERIFY2(toolDev, qPrintable(err));
    CanTransport t(toolDev, QStringLiteral("virtual CAN"));

    connect(&gw.bridge(), &ITransport::frame, this, [&](const TelemetryFrame &f) {
        sim = f;
        simFrames++;
    });
    connect(&gw.bridge(), &ITransport::info, this, [&](const QJsonObject &o) {
        if (o.value(QLatin1String("type")).toString() == QLatin1String("hello")) {
            hello = o;
        }
    });
    connect(&gw.bridge(), &ITransport::ack, this, [&](const Ack &a) { bridgeAcks.insert(a.id, a); });
    connect(&t, &ITransport::frame, this, [&](const TelemetryFrame &f) {
        inv = f;
        rogueShown += (f.v(QStringLiteral("inv.speed_rpm")) == 4321.0) ? 1 : 0;
    });
    connect(&t, &ITransport::ack, this, [&](const Ack &a) { acks.insert(a.id, a); });
    connect(&t, &ITransport::traffic, this, [&](const QString &dir, const QString &line) {
        if (dir == QLatin1String("tx")) {
            txLines << line;
        } else if (dir == QLatin1String("can") && line.startsWith(QLatin1String("0x6e9 ["))) {
            periodicNs << clock.nsecsElapsed();
            periodicPdids += CanCodec::fromHex(line.section(QLatin1String("] "), 1)).left(1);
        }
    });
    connect(&gw, &BridgeCanGateway::failed, this, [&](const QString &why) { gwFailure = why; });

    const auto cmd = [&](const QString &c, const QJsonObject &args = {}) {
        const quint64 id = t.send(c, args);
        until([&] { return acks.contains(id); }, 2000);
        Ack none;
        none.message = QStringLiteral("no ack");
        return acks.value(id, none);
    };
    const auto bridgeCmd = [&](const QString &c, const QJsonObject &args = {}) {
        const quint64 id = gw.bridge().send(c, args);
        until([&] { return bridgeAcks.contains(id); }, 2000);
        Ack none;
        none.message = QStringLiteral("no ack");
        return bridgeAcks.value(id, none);
    };
    const auto state = [&] { return static_cast<int>(inv.v(QStringLiteral("inv.state"), -1.0)); };
    const auto diag = [&] {
        QStringList dtcs;
        for (const DtcState &d : sim.dtcs) {
            dtcs << QStringLiteral("%1/0x%2").arg(d.id).arg(d.status, 2, 16, QLatin1Char('0'));
        }
        return QStringLiteral("gateway '%1' | tool: state %2 bridge %3 vdc %4 rpm %5 torque %6, rx %7 status %8 crc %9 "
                              "tx %10 | sim: sm %11 contactor %12 cmd_fresh %13 rows %14 dtc [%15] | gw: bus %16 fw %17 "
                              "dropped %18 refused %19")
            .arg(gwFailure).arg(state()).arg(inv.v(QStringLiteral("inv.bridge")))
            .arg(inv.v(QStringLiteral("inv.vdc_v"))).arg(inv.v(QStringLiteral("inv.speed_rpm")))
            .arg(inv.v(QStringLiteral("inv.torque_applied_nm"))).arg(t.stats().rx).arg(t.stats().status)
            .arg(t.stats().crcErr).arg(t.stats().tx).arg(sim.v(QStringLiteral("state.sm")))
            .arg(sim.str(QStringLiteral("plant.contactor"))).arg(sim.v(QStringLiteral("wd.cmd_fresh")))
            .arg(sim.v(QStringLiteral("safety.rows"))).arg(dtcs.join(QLatin1Char(' ')))
            .arg(gw.stats().toBus).arg(gw.stats().toFirmware).arg(gw.stats().dropped).arg(gw.stats().refused);
    };

    // ---- the firmware on the virtual bus; the tool decodes its INV_STATUS (E2E and alive counter checked)
    gw.start();
    t.open();
    QVERIFY2(until([&] { return t.state() == ITransport::State::Open; }, 5000), qPrintable(diag()));
    QVERIFY2(until([&] { return t.stats().status >= 10 && !hello.isEmpty(); }, 5000), qPrintable(diag()));
    QCOMPARE(t.stats().crcErr, quint64(0));

    // ---- locked: nothing leaves the tool
    const Ack locked = cmd(QStringLiteral("vcu"), {{QStringLiteral("on"), true}});
    QVERIFY(!locked.ok);
    QVERIFY2(locked.message.contains(QLatin1String("service key")), qPrintable(locked.message));
    QCOMPARE(t.stats().tx, quint64(0));

    // ---- unlocked: the tool is the VCU. PRECHARGE_WAIT (self-test done), enable, the contactor report precharge (the
    //      plant closes its precharge path), closed once the link holds 98 % of the pack the BMS reports (750 V, the
    //      8XX default) after >= 300 ms — the bench VCU's sequence (PROTOCOL.md A.6) — then ARMED_ZERO_TORQUE
    t.setServiceUnlocked(true);
    QVERIFY(cmd(QStringLiteral("vcu"), {{QStringLiteral("on"), true}}).ok);
    QVERIFY(cmd(QStringLiteral("gear"), {{QStringLiteral("gear"), QStringLiteral("D")}}).ok);
    QVERIFY2(until([&] { return state() == 4; }, 5000), qPrintable(diag()));
    QVERIFY(cmd(QStringLiteral("arm")).ok);
    QVERIFY(cmd(QStringLiteral("contactors"), {{QStringLiteral("state"), QStringLiteral("precharge")}}).ok);
    const qint64 precharge = clock.elapsed();
    QVERIFY2(until([&] { return inv.v(QStringLiteral("inv.vdc_v")) >= 0.98 * 750.0 && clock.elapsed() - precharge >= 300; },
                   5000),
             qPrintable(diag()));
    QVERIFY(cmd(QStringLiteral("contactors"), {{QStringLiteral("state"), QStringLiteral("closed")}}).ok);
    QVERIFY2(until([&] { return state() == 6; }, 5000), qPrintable(diag())); // ARMED_ZERO_TORQUE
    QCOMPARE(static_cast<int>(inv.v(QStringLiteral("inv.bridge"))), 1);  // idle: drivers enabled, no PWM
    QVERIFY2(until([&] { return sim.str(QStringLiteral("plant.contactor")) == QLatin1String("closed"); }, 1000),
             qPrintable(diag())); // the plant followed the report

    // ---- the dyno through the bridge, then 50 N·m: RUN, the applied torque (INV_STATUS b4-5) within 10 %
    QVERIFY(bridgeCmd(QStringLiteral("speed"), {{QStringLiteral("rpm"), 1500}}).ok);
    QVERIFY2(until([&] { return std::abs(inv.v(QStringLiteral("inv.speed_rpm")) - 1500.0) < 5.0; }, 5000),
             qPrintable(diag()));
    QVERIFY(cmd(QStringLiteral("torque"), {{QStringLiteral("nm"), 50.0}}).ok);
    QVERIFY2(until([&] { return state() == 7 && std::abs(inv.v(QStringLiteral("inv.torque_applied_nm")) - 50.0) <= 5.0; },
                   3000),
             qPrintable(diag()));
    QVector<double> applied;
    QVector<int> states;
    const auto sampling = connect(&t, &ITransport::frame, this, [&](const TelemetryFrame &f) {
        applied << f.v(QStringLiteral("inv.torque_applied_nm"));
        states << static_cast<int>(f.v(QStringLiteral("inv.state")));
    });
    run(300);
    disconnect(sampling);
    QVERIFY2(applied.size() >= 20, qPrintable(diag()));
    const double mean = std::accumulate(applied.begin(), applied.end(), 0.0) / static_cast<double>(applied.size());
    const double lo = *std::min_element(applied.begin(), applied.end());
    const double hi = *std::max_element(applied.begin(), applied.end());
    std::nth_element(applied.begin(), applied.begin() + applied.size() / 2, applied.end());
    const double median = applied[applied.size() / 2];
    std::nth_element(states.begin(), states.begin() + states.size() / 2, states.end());
    QCOMPARE(states[states.size() / 2], 7);
    QVERIFY2(std::abs(median - 50.0) <= 5.0, qPrintable(diag()));
    qInfo("applied torque: %d INV_STATUS frames in 300 ms, median %.2f, mean %.2f N·m (min %.1f, max %.1f); plant %.2f N·m",
          static_cast<int>(applied.size()), median, mean, lo, hi, sim.v(QStringLiteral("plant.torque_nm")));

    // ---- the periodic stream: 2A 02 00 (DID 0xF200 every 10 ms) -> 6A, frames on 0x6E9; 2A 04 00 stops it
    periodicNs.clear();
    periodicPdids.clear();
    Ack a = cmd(QStringLiteral("uds"), {{QStringLiteral("hex"), QStringLiteral("03 2A 02 00")}});
    QVERIFY2(a.ok && a.raw.value(QLatin1String("msg")).toString() == QLatin1String("6A"), qPrintable(a.message));
    run(1000);
    a = cmd(QStringLiteral("uds"), {{QStringLiteral("hex"), QStringLiteral("03 2A 04 00")}});
    QVERIFY2(a.ok && a.raw.value(QLatin1String("msg")).toString() == QLatin1String("6A"), qPrintable(a.message));
    const qsizetype nPeriodic = periodicNs.size();
    run(200);
    QCOMPARE(periodicNs.size(), nPeriodic); // stopped
    QVERIFY2(nPeriodic >= 80, qPrintable(QString::number(nPeriodic)));
    QVERIFY(std::all_of(periodicPdids.begin(), periodicPdids.end(), [](char p) { return p == 0; }));
    qint64 maxGap = 0;
    for (qsizetype i = 1; i < nPeriodic; i++) {
        maxGap = std::max(maxGap, periodicNs[i] - periodicNs[i - 1]);
    }
    const double periodMs = 1e-6 * static_cast<double>(periodicNs.last() - periodicNs.first()) / static_cast<double>(nPeriodic - 1);
    QVERIFY2(std::abs(periodMs - 10.0) <= 1.0, qPrintable(QString::number(periodMs)));
    qInfo("0x6E9: %d frames, mean period %.3f ms (longest gap %.1f ms)", static_cast<int>(nPeriodic), periodMs,
          1e-6 * static_cast<double>(maxGap));

    // ---- faults through the bridge, then 19 02 FF: a first frame, flow control from the tool, consecutive frames
    //      reassembled; its confirmed DTCs are the simulator's (before the request ⊆ response ⊆ after it)
    const int hvil = dtcId(hello, QStringLiteral("DTC_HVIL_OPEN")), desat = dtcId(hello, QStringLiteral("DTC_DESAT_HS"));
    QVERIFY(hvil > 0 && desat > 0);
    QVERIFY(bridgeCmd(QStringLiteral("inject"), {{QStringLiteral("fault"), QStringLiteral("hvil_open")}}).ok);
    QVERIFY2(until([&] { return confirmed(sim).contains(hvil); }, 3000), qPrintable(diag())); // FW-09, while running
    QVERIFY(bridgeCmd(QStringLiteral("inject"), {{QStringLiteral("fault"), QStringLiteral("desat_hs")}}).ok);
    QVERIFY2(until([&] { return confirmed(sim).contains(desat); }, 3000), qPrintable(diag()));
    const QSet<int> before = confirmed(sim);
    const qsizetype tx0 = txLines.size();
    a = cmd(QStringLiteral("uds"), {{QStringLiteral("hex"), QStringLiteral("03 19 02 FF")}});
    QVERIFY2(a.ok, qPrintable(a.message));
    const int frames0 = simFrames;
    QVERIFY(until([&] { return simFrames >= frames0 + 2; }, 1000));
    const QSet<int> after = confirmed(sim);
    const QByteArray ff = CanCodec::fromHex(a.raw.value(QLatin1String("rsp")).toString());
    const QByteArray msg = CanCodec::fromHex(a.raw.value(QLatin1String("msg")).toString());
    QCOMPARE(ff.size(), 64);
    QCOMPARE(static_cast<quint8>(ff[0]) >> 4, 1); // a first frame
    const int ffLen = ((static_cast<quint8>(ff[0]) & 0x0F) << 8) | static_cast<quint8>(ff[1]);
    QCOMPARE(static_cast<int>(msg.size()), ffLen);
    QCOMPARE(a.raw.value(QLatin1String("frames")).toInt(), 1 + (ffLen - 62 + 62) / 63); // 62 bytes, then 63 per frame
    QVERIFY2(txLines.mid(tx0).contains(QStringLiteral("0x7e1 [8] 30 00 00 00 00 00 00 00")), qPrintable(txLines.mid(tx0).join(QLatin1String(" | "))));
    QCOMPARE(msg.left(3), QByteArray::fromHex("59027F"));
    QCOMPARE((msg.size() - 3) % 4, 0);
    QSet<int> inUds;
    for (qsizetype i = 3; i + 3 < msg.size(); i += 4) {
        QCOMPARE(static_cast<quint8>(msg[i]), quint8(0xD1)); // 0xD10000 | id
        if (static_cast<quint8>(msg[i + 3]) & 0x08) {
            inUds.insert((static_cast<quint8>(msg[i + 1]) << 8) | static_cast<quint8>(msg[i + 2]));
        }
    }
    for (int id : before) {
        QVERIFY2(inUds.contains(id), qPrintable(QStringLiteral("DTC %1 confirmed in the simulator, not in 19 02").arg(id)));
    }
    for (int id : inUds) {
        QVERIFY2(after.contains(id), qPrintable(QStringLiteral("DTC %1 confirmed in 19 02, not in the simulator").arg(id)));
    }
    qInfo("19 02 FF: %d-byte response in %d frames (%d DTC records, %d confirmed: the simulator's)", ffLen,
          a.raw.value(QLatin1String("frames")).toInt(), (ffLen - 3) / 4, static_cast<int>(inUds.size()));

    // ---- a corrupted INV_STATUS from another node on the bus: rejected by the tool's E2E check, never shown
    std::unique_ptr<QCanBusDevice> rogue(CanTransport::createDevice(co, &err));
    QVERIFY2(rogue, qPrintable(err));
    QVERIFY(rogue->connectDevice());
    QVERIFY(until([&] { return rogue->state() == QCanBusDevice::ConnectedState; }, 2000));
    CanCodec::InvStatus s;
    s.state = 7;
    s.speedRpm = 4321.0f;
    s.ctr = static_cast<quint8>((static_cast<int>(inv.v(QStringLiteral("inv.ctr"))) + 1) & 0x0F);
    QByteArray bad = CanCodec::encodeInvStatus(s);
    bad[0] = static_cast<char>(bad[0] ^ 0x5A);
    QCanBusFrame rf(CanCodec::ID_INV_STATUS, bad);
    rf.setFlexibleDataRateFormat(true);
    const quint64 crc0 = t.stats().crcErr;
    QVERIFY(rogue->writeFrame(rf));
    QVERIFY2(until([&] { return t.stats().crcErr == crc0 + 1; }, 2000), qPrintable(diag()));
    run(50);
    QCOMPARE(t.stats().crcErr, crc0 + 1);
    QCOMPARE(rogueShown, 0);

    const int canTimeout = dtcId(hello, QStringLiteral("DTC_CAN_TIMEOUT"));
    int staleEvents = 0; // before the relock below silences the VCU on purpose
    for (const DtcState &d : sim.dtcs) {
        staleEvents += (d.id == canTimeout) ? d.occ : 0;
    }

    // ---- relocked: the emulated VCU stops, every transmitting command is refused, nothing leaves the tool, and the
    //      firmware sees its VCU go silent (FW-11)
    t.setServiceUnlocked(false);
    QVERIFY(!t.vcuEmulation());
    const quint64 txLocked = t.stats().tx;
    const Ack r1 = cmd(QStringLiteral("torque"), {{QStringLiteral("nm"), 10.0}});
    QVERIFY(!r1.ok);
    QVERIFY2(r1.message.contains(QLatin1String("service key")), qPrintable(r1.message));
    QVERIFY(!cmd(QStringLiteral("uds"), {{QStringLiteral("hex"), QStringLiteral("03 22 F2 00")}}).ok);
    QVERIFY(!cmd(QStringLiteral("send"), {{QStringLiteral("can_id"), 0x101}, {QStringLiteral("hex"), QStringLiteral("00")}}).ok);
    QVERIFY2(until([&] { return sim.has(QStringLiteral("wd.cmd_fresh")) && !sim.b(QStringLiteral("wd.cmd_fresh")); }, 2000),
             qPrintable(diag()));
    QCOMPARE(t.stats().tx, txLocked);

    // ---- the gateway lost nothing either way, and the bridge took every frame the bus gave it
    QCOMPARE(gw.stats().dropped, quint64(0));
    QCOMPARE(gw.stats().refused, quint64(0));
    QVERIFY(gwFailure.isEmpty());
    qInfo("VCU_CMD late beyond the firmware's 20 ms while armed (DTC_CAN_TIMEOUT occurrences, host scheduling): %d",
          staleEvents);
    qInfo("gateway: %llu frames firmware -> bus, %llu bus -> firmware; tool: %llu rx, %llu tx; %.1f s",
          static_cast<unsigned long long>(gw.stats().toBus), static_cast<unsigned long long>(gw.stats().toFirmware),
          static_cast<unsigned long long>(t.stats().rx), static_cast<unsigned long long>(t.stats().tx),
          1e-3 * static_cast<double>(clock.elapsed()));
}

void TstCanBridge::rippleOverCan()
{
    using Seq = CommissioningSequencer;
    BridgeTransport::Options bo;
    bo.program = BridgeTransport::locate();
    if (bo.program.isEmpty()) {
        QSKIP("simulator bridge not built (make -C tool/bridge): the ripple table over CAN did not run");
    }
    if (!QCanBus::instance()->plugins().contains(QStringLiteral("virtualcan"))) {
        QSKIP("this Qt SerialBus has no virtualcan plugin: the ripple table over CAN did not run");
    }
    bo.rateHz = 10;
    QLoggingCategory::setFilterRules(QStringLiteral("qt.canbus.plugins.virtualcan.info=false"));
    QVERIFY(busPort() != 0);
    CanTransport::Options co;
    co.plugin = QStringLiteral("virtualcan");
    co.interface = QStringLiteral("tcp://127.0.0.1:%1/can1").arg(busPort()); // the server of the first test; a bus of its own
    QString err;
    QCanBusDevice *gwDev = CanTransport::createDevice(co, &err);
    QVERIFY2(gwDev, qPrintable(err));
    BridgeCanGateway gw(bo, gwDev);
    QCanBusDevice *toolDev = CanTransport::createDevice(co, &err);
    QVERIFY2(toolDev, qPrintable(err));
    QJsonObject hello;
    QHash<quint64, Ack> bridgeAcks;
    connect(&gw.bridge(), &ITransport::info, this, [&](const QJsonObject &o) {
        if (o.value(QLatin1String("type")).toString() == QLatin1String("hello")) {
            hello = o;
        }
    });
    connect(&gw.bridge(), &ITransport::ack, this, [&](const Ack &a) { bridgeAcks.insert(a.id, a); });
    Session s;
    s.setTransport(std::make_unique<CanTransport>(toolDev, QStringLiteral("virtual CAN")));
    gw.start();
    QVERIFY(until([&] { return s.isDeviceLive() && !hello.isEmpty(); }, 10000));
    CommissioningPage page(s);
    Seq &seq = page.sequencer();
    const QString csv = QStringLiteral(TT_SOURCE_DIR "/resources/ripple-example.csv");

    // ---- over CAN the record's ψ and pole pairs come from DID 0xFD25 only: none before the service key lets the tool ask
    QVERIFY(!s.deviceInfo().contains(QLatin1String("motor")));
    QVERIFY(!page.importRipple(csv));
    QVERIFY2(page.ripple().error.contains(QLatin1String("DID 0xFD25")), qPrintable(page.ripple().error));
    QVERIFY(s.unlockService(QStringLiteral("traction-service")));
    QVERIFY(until([&] { return s.deviceInfo().contains(QLatin1String("motor")); }, 5000));
    const QJsonObject motor = s.deviceInfo().value(QLatin1String("motor")).toObject();
    const QJsonObject truth = hello.value(QLatin1String("motor")).toObject(); // the bridge's hello: the same record
    qInfo("DID 0xFD25 over CAN: %s; the bridge's hello.motor: %s", QJsonDocument(motor).toJson(QJsonDocument::Compact).constData(),
          QJsonDocument(truth).toJson(QJsonDocument::Compact).constData());
    QCOMPARE(motor.value(QLatin1String("source")).toString(), QStringLiteral("DID 0xFD25"));
    QCOMPARE(motor.value(QLatin1String("pp")).toInt(), truth.value(QLatin1String("pp")).toInt());
    for (const auto &[key, lsb] : {std::pair<const char *, double>{"psi_wb", 1e-6}, {"ld_h", 1e-9}, {"lq_h", 1e-9}, {"rs_ohm", 1e-6},
                                   {"n_max_rpm", 1.0}, {"id_demag_a", 0.1}}) { // within the DID's resolution
        QVERIFY2(std::abs(motor.value(QLatin1String(key)).toDouble() - truth.value(QLatin1String(key)).toDouble()) <= 0.5 * lsb + 1e-12, key);
    }
    QCOMPARE(s.deviceInfo().value(QLatin1String("root")).toObject().value(QLatin1String("kind")).toString(), QStringLiteral("test"));
    // the same request carried DID 0xFD26 (83 bytes: the firmware's segmented response): the record's maps — the bench
    // record's flat ones, each point its scalar — for the Commissioning page's Record column
    const QJsonObject maps = s.deviceInfo().value(QLatin1String("maps")).toObject();
    QCOMPARE(maps.value(QLatin1String("source")).toString(), QStringLiteral("DID 0xFD26"));
    for (const char *ax : {"ld_h", "lq_h"}) {
        const QJsonArray a = maps.value(QLatin1String(ax)).toArray();
        QCOMPARE(a.size(), 6);
        for (const QJsonValue &v : a) {
            QVERIFY(std::abs(v.toDouble() - truth.value(QLatin1String(ax)).toDouble()) <= 0.5e-9 + 1e-12);
        }
    }
    const double crest = std::sqrt(2.0) * hello.value(QLatin1String("limits")).toObject().value(QLatin1String("i_pk_rms_a")).toDouble();
    QVERIFY(std::abs(maps.value(QLatin1String("i_map_a")).toDouble() - crest) <= 0.05 + 1e-9);

    // ---- the import with the DID's ψ and pp: the table the simulator's hello gives
    QVERIFY2(page.importRipple(csv), qPrintable(page.ripple().error));
    const QVector<qint16> table = page.ripple().table;
    QFile f(csv);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(table, Seq::rippleFromCsv(f.readAll(), truth.value(QLatin1String("psi_wb")).toDouble(), truth.value(QLatin1String("pp")).toInt()).table);
    QVERIFY(std::abs(page.ripple().nmPerA - 1.5 * truth.value(QLatin1String("pp")).toInt() * truth.value(QLatin1String("psi_wb")).toDouble()) < 1e-9);

    // ---- the CAL through the bridge (a CAN bus carries no param_set), then the write and its read-back over CAN
    const quint64 id = gw.bridge().send(QStringLiteral("param_set"), {{QStringLiteral("name"), QStringLiteral("cal_ripple_ff_max_a")},
                                                                     {QStringLiteral("value"), 10.0}});
    QVERIFY(until([&] { return bridgeAcks.contains(id); }, 2000) && bridgeAcks.value(id).ok);
    QVERIFY(seq.writeRipple(table));
    QVERIFY(until([&] { return !seq.busy(); }, 10000));
    qInfo("CAN ripple write: %s; peak %.2f A at %.0f° el, 1.5·pp·ψ = %.4f N·m/A (DID 0xFD25)", qPrintable(seq.text()),
          page.ripple().peakA, page.ripple().peakDeg, page.ripple().nmPerA);
    QVERIFY2(seq.outcome() == Seq::Outcome::Done, qPrintable(seq.text()));
    QCOMPARE(seq.rippleRead(), table);
    QVERIFY(seq.stagedMask() & 0x40); // results index 1 after the write: the table staged for the next commit
    QVERIFY(seq.readRipple()); // on its own: the staged table
    QVERIFY(until([&] { return !seq.busy(); }, 10000));
    QVERIFY2(seq.outcome() == Seq::Outcome::Done, qPrintable(seq.text()));
    QCOMPARE(seq.rippleRead(), table);
    QCOMPARE(gw.stats().dropped, quint64(0));
    QCOMPARE(gw.stats().refused, quint64(0));
    qInfo("CAN ripple read: %s; gateway %llu frames to the bus, %llu to the firmware", qPrintable(seq.text()),
          static_cast<unsigned long long>(gw.stats().toBus), static_cast<unsigned long long>(gw.stats().toFirmware));
}

QTEST_MAIN(TstCanBridge)
#include "tst_can_bridge.moc"
