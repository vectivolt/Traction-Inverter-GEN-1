// The CAN-FD codec against the firmware's own encoder output: every vector of tool/protocol/can-frames.json (21 at round 23)
// (embedded as :/protocol/can-frames.json), the CRC check value, the alive-counter rule, and CanTransport end to end
// on a MockCanDevice (E2E rejection, the frozen-sender rule, the service lock, VCU emulation, the torque deadman, the
// ISO 15765-2 transmit of a long UDS request under every kind of flow control); and the Session's UDS queue on it (one
// request on the bus at a time, each answered with its own response, in order).
#include "CanCodec.h"
#include "CanTransport.h"
#include "MockCanDevice.h"
#include "ProtocolInfo.h"
#include "Session.h"

#include <QCanBusFrame>
#include <QJsonArray>
#include <QSignalSpy>
#include <QtTest>

using namespace CanCodec;

namespace {
Contactors contactorsOf(const QString &s)
{
    return (s == QLatin1String("open")) ? Contactors::Open
         : (s == QLatin1String("precharge")) ? Contactors::Precharge
         : (s == QLatin1String("closed")) ? Contactors::Closed : Contactors::Invalid;
}
QString contactorsName(Contactors c)
{
    static const char *const N[] = {"invalid", "open", "precharge", "closed"};
    return QLatin1String(N[static_cast<int>(c)]);
}
QString gearName(Gear g)
{
    static const char *const N[] = {"N", "D", "R", "P"};
    return QLatin1String(N[static_cast<int>(g)]);
}
Gear gearOf(const QString &g)
{
    return (g == QLatin1String("D")) ? Gear::D : (g == QLatin1String("R")) ? Gear::R : (g == QLatin1String("P")) ? Gear::P : Gear::N;
}
QString hex(const QByteArray &b) { return QString::fromLatin1(b.toHex().toUpper()); }
bool near(double a, double b) { return std::abs(a - b) <= 1e-3 * std::max(1.0, std::abs(b)); }
InvStatus statusOf(const QJsonObject &in)
{
    InvStatus s;
    s.ctr = static_cast<quint8>(in[QLatin1String("ctr")].toInt());
    s.state = static_cast<quint8>(in[QLatin1String("state")].toInt());
    s.bridge = static_cast<quint8>(in[QLatin1String("bridge")].toInt());
    s.hv = static_cast<quint8>(in[QLatin1String("hv")].toInt());
    s.selfTestDone = in[QLatin1String("self_test_done")].toBool();
    s.keepHv = in[QLatin1String("keep_hv")].toBool();
    s.derate = in[QLatin1String("derate")].toBool();
    s.fault = in[QLatin1String("fault")].toBool();
    s.zeroTorque = in[QLatin1String("zero_torque")].toBool();
    s.discharging = in[QLatin1String("discharging")].toBool();
    s.prechargeRefused = in[QLatin1String("precharge_refused")].toBool();
    s.speedValid = in[QLatin1String("speed_valid")].toBool();
    s.torqueAppliedNm = static_cast<float>(in[QLatin1String("torque_nm")].toDouble());
    s.torqueCmdNm = static_cast<float>(in[QLatin1String("torque_cmd_nm")].toDouble());
    s.speedRpm = static_cast<float>(in[QLatin1String("speed_rpm")].toDouble());
    s.vdcV = static_cast<float>(in[QLatin1String("vdc_v")].toDouble());
    s.vdcValid = in[QLatin1String("vdc_valid")].toBool();
    s.tModuleC = static_cast<float>(in[QLatin1String("t_module_c")].toDouble());
    s.nDtc = static_cast<quint8>(std::min(255, in[QLatin1String("n_dtc")].toInt()));
    s.firstDtc = static_cast<quint16>(in[QLatin1String("first_dtc")].toInt());
    s.noSafeState = in[QLatin1String("no_safe_state")].toBool();
    s.serviceRequired = in[QLatin1String("service_required")].toBool();
    s.openContactorsReq = in[QLatin1String("open_contactors_req")].toBool();
    s.speedLimitReq = in[QLatin1String("speed_limit_req")].toBool();
    s.evidenceMissing = static_cast<quint8>(in[QLatin1String("evidence_missing")].toInt());
    return s;
}
QCanBusFrame fdFrame(quint32 id, const QByteArray &p)
{
    QCanBusFrame f(id, p);
    f.setFlexibleDataRateFormat(p.size() > 8);
    return f;
}
} // namespace

class TstCodec : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void crcCheckValue();
    void protocolVectors();
    void corruptedFramesRejected();
    void legacyStatusFrame();
    void aliveCounter();
    void transportReceivesStatus();
    void transportServiceLockAndVcuEmulation();
    void transportIsoTpTransmit();
    void sessionSerialisesUds();
};

void TstCodec::crcCheckValue()
{
    const QByteArray s("123456789");
    QCOMPARE(crc8_1d(reinterpret_cast<const quint8 *>(s.constData()), 9, 0xFF, 0xFF), quint8(0x4B)); // SAE J1850
}

void TstCodec::protocolVectors()
{
    const QJsonArray vectors = ProtocolInfo::instance().canFrames.value(QLatin1String("vectors")).toArray();
    QVERIFY2(vectors.size() >= 18, "the export lost vectors");
    const ProtocolInfo &pi = ProtocolInfo::instance();
    int checked = 0;
    for (const QJsonValue &vv : vectors) {
        const QJsonObject v = vv.toObject();
        const QString msg = v[QLatin1String("msg")].toString();
        const QJsonObject in = v[QLatin1String("input")].toObject();
        const QString want = v[QLatin1String("hex")].toString();
        const QByteArray bytes = fromHex(want);
        QCOMPARE(bytes.size(), v[QLatin1String("len")].toInt());
        if (msg == QLatin1String("VCU_CMD")) {
            VcuCmd c;
            c.ctr = static_cast<quint8>(in[QLatin1String("ctr")].toInt());
            c.gear = gearOf(in[QLatin1String("gear")].toString());
            c.enable = in[QLatin1String("enable")].toBool();
            c.faultReset = in[QLatin1String("fault_reset")].toBool();
            c.torqueNm = static_cast<float>(in[QLatin1String("torque_nm")].toDouble());
            c.contactors = contactorsOf(in[QLatin1String("contactors")].toString());
            c.retryAuth = in[QLatin1String("desat_retry_auth")].toBool();
            c.discharge = in[QLatin1String("discharge_req")].toBool();
            c.shutdown = in[QLatin1String("shutdown_req")].toBool();
            c.coolantC = static_cast<float>(in[QLatin1String("coolant_c")].toDouble());
            c.vspeedValid = in[QLatin1String("vspeed_valid")].toBool();
            c.vspeedKmh = static_cast<float>(in[QLatin1String("vspeed_kmh")].toDouble());
            QCOMPARE(hex(encodeVcuCmd(c)), want);
            const auto d = decodeVcuCmd(bytes);
            QVERIFY(d.has_value());
            const QJsonObject fw = v[QLatin1String("firmware_decoded")].toObject();
            QCOMPARE(gearName(d->gear), fw[QLatin1String("gear")].toString());
            QCOMPARE(d->enable, fw[QLatin1String("enable")].toBool());
            QCOMPARE(d->faultReset, fw[QLatin1String("fault_reset")].toBool());
            QVERIFY(near(d->torqueNm, fw[QLatin1String("torque_nm")].toDouble()));
            QCOMPARE(contactorsName(d->contactors), fw[QLatin1String("contactors")].toString());
            QCOMPARE(d->retryAuth, fw[QLatin1String("desat_retry_auth")].toBool());
            QCOMPARE(d->discharge, fw[QLatin1String("discharge_req")].toBool());
            QCOMPARE(d->shutdown, fw[QLatin1String("shutdown_req")].toBool());
            QCOMPARE(d->coolantValid, fw[QLatin1String("coolant_valid")].toBool());
            QVERIFY(near(d->coolantC, fw[QLatin1String("coolant_c")].toDouble()));
            QCOMPARE(d->vspeedValid, fw[QLatin1String("vspeed_valid")].toBool());
            QVERIFY(near(d->vspeedKmh, fw[QLatin1String("vspeed_kmh")].toDouble()));
        } else if (msg == QLatin1String("VCU_BMS")) {
            VcuBms b;
            b.ctr = static_cast<quint8>(in[QLatin1String("ctr")].toInt());
            b.packV = static_cast<float>(in[QLatin1String("v_pack_v")].toDouble());
            b.chgW = static_cast<float>(in[QLatin1String("p_chg_w")].toDouble());
            b.disW = static_cast<float>(in[QLatin1String("p_dis_w")].toDouble());
            QCOMPARE(hex(encodeVcuBms(b)), want);
            const auto d = decodeVcuBms(bytes);
            QVERIFY(d.has_value());
            const QJsonObject fw = v[QLatin1String("firmware_decoded")].toObject();
            QVERIFY(near(d->packV, fw[QLatin1String("v_pack_v")].toDouble()));
            QVERIFY(near(d->chgW, fw[QLatin1String("p_chg_w")].toDouble()));
            QVERIFY(near(d->disW, fw[QLatin1String("p_dis_w")].toDouble()));
        } else if (msg == QLatin1String("INV_STATUS")) {
            QCOMPARE(hex(encodeInvStatus(statusOf(in))), want);
            const auto d = decodeInvStatus(bytes);
            QVERIFY(d.has_value());
            const QJsonObject t = v[QLatin1String("table_decoded")].toObject();
            QCOMPARE(int(d->ctr), t[QLatin1String("ctr")].toInt());
            QCOMPARE(pi.stateName(d->state), t[QLatin1String("state")].toString());
            QCOMPARE(pi.bridgeModes.value(d->bridge), t[QLatin1String("bridge")].toString());
            QCOMPARE(pi.hvStates.value(d->hv), t[QLatin1String("hv")].toString());
            for (const char *k : {"self_test_done", "keep_hv", "derate", "fault", "zero_torque", "discharging",
                                  "precharge_refused", "speed_valid", "no_safe_state", "service_required",
                                  "open_contactors_req", "speed_limit_req"}) {
                TelemetryFrame f;
                addChannels(*d, f);
                QCOMPARE(f.b(QStringLiteral("inv.") + QLatin1String(k)), t[QLatin1String(k)].toBool());
            }
            QVERIFY(near(d->torqueAppliedNm, t[QLatin1String("torque_nm")].toDouble()));
            QVERIFY(near(d->torqueCmdNm, t[QLatin1String("torque_cmd_nm")].toDouble()));
            QVERIFY(near(d->speedRpm, t[QLatin1String("speed_rpm")].toDouble()));
            QCOMPARE(d->vdcValid, !t[QLatin1String("vdc_v")].isNull());
            if (d->vdcValid) {
                QVERIFY(near(d->vdcV, t[QLatin1String("vdc_v")].toDouble()));
            }
            QVERIFY(near(d->tModuleC, t[QLatin1String("t_module_c")].toDouble()));
            QCOMPARE(int(d->nDtc), t[QLatin1String("n_dtc")].toInt());
            QCOMPARE(int(d->firstDtc), t[QLatin1String("first_dtc")].toInt());
            QCOMPARE(int(d->evidenceMissing), t[QLatin1String("evidence_missing")].toInt());
        } else if (msg == QLatin1String("UDS_REQ")) {
            QByteArray req = fromHex(in[QLatin1String("uds_request")].toString());
            req.append(QByteArray(8 - req.size(), '\0'));
            QCOMPARE(hex(req), want);
        } else {
            QFAIL(qPrintable(QStringLiteral("unknown vector message %1").arg(msg)));
        }
        checked++;
    }
    QCOMPARE(checked, vectors.size()); // every vector went through one of the branches above
}

void TstCodec::corruptedFramesRejected()
{
    VcuCmd c;
    c.torqueNm = 42.0f;
    QByteArray f = encodeVcuCmd(c);
    QVERIFY(decodeVcuCmd(f).has_value());
    f[3] = static_cast<char>(f[3] ^ 0x01);
    QVERIFY(!decodeVcuCmd(f).has_value());               // one bit: the CRC catches it
    QVERIFY(!decodeVcuCmd(encodeVcuCmd(c).left(7)).has_value()); // short
    // the DataID is part of the CRC: a VCU_BMS payload is not a valid VCU_CMD
    VcuBms b;
    b.packV = 400.0f;
    QVERIFY(!decodeVcuCmd(encodeVcuBms(b)).has_value());
    InvStatus s;
    s.state = 7;
    QByteArray st = encodeInvStatus(s);
    st[17] = static_cast<char>(st[17] ^ 0x80);
    QVERIFY(!decodeInvStatus(st).has_value());
    QVERIFY(!decodeInvStatus(encodeInvStatus(s).left(19)).has_value()); // only 20 (or legacy 16) bytes
}

void TstCodec::legacyStatusFrame()
{
    InvStatus s;
    s.state = 7;
    s.speedRpm = 1234.0f;
    s.torqueAppliedNm = -12.5f;
    QByteArray f = encodeInvStatus(s).left(LEN_STATUS_LEGACY);
    f[0] = static_cast<char>(e2eCrc(ID_INV_STATUS, f)); // a pre-round-23 image seals 16 bytes
    const auto d = decodeInvStatus(f);
    QVERIFY(d.has_value());
    QVERIFY(!d->hasTorqueCmd);
    QVERIFY(near(d->speedRpm, 1234.0));
    QVERIFY(near(d->torqueAppliedNm, -12.5));
    TelemetryFrame fr;
    addChannels(*d, fr);
    QVERIFY(!fr.has(QStringLiteral("inv.torque_cmd_nm")));
}

void TstCodec::aliveCounter()
{
    AliveCounter a(2);
    QCOMPARE(a.check(5), AliveCounter::Result::First);
    QCOMPARE(a.check(6), AliveCounter::Result::Ok);
    QCOMPARE(a.check(6), AliveCounter::Result::Frozen); // a repeated counter: frozen sender
    QCOMPARE(a.check(8), AliveCounter::Result::Ok);     // one frame lost: tolerated (jump 2)
    QCOMPARE(a.check(11), AliveCounter::Result::Jump);  // jump 3: rejected, resyncs
    QCOMPARE(a.check(12), AliveCounter::Result::Ok);
    QCOMPARE(a.check(15), AliveCounter::Result::Jump);
    QCOMPARE(a.check(0), AliveCounter::Result::Ok);     // wraps modulo 16
}

void TstCodec::transportReceivesStatus()
{
    auto *dev = new MockCanDevice; // CanTransport takes ownership
    CanTransport t(dev, QStringLiteral("mock"));
    QSignalSpy frames(&t, &ITransport::frame);
    t.open();
    QCOMPARE(t.state(), ITransport::State::Open);
    InvStatus s;
    s.state = 7;
    s.bridge = 2;
    s.speedRpm = 2500.0f;
    s.torqueAppliedNm = 99.9f;
    s.torqueCmdNm = 120.0f;
    s.vdcValid = true;
    s.vdcV = 748.0f;
    s.firstDtc = 17;
    s.nDtc = 1;
    s.ctr = 1;
    dev->inject({fdFrame(ID_INV_STATUS, encodeInvStatus(s))});
    QTRY_COMPARE(frames.size(), 1);
    const TelemetryFrame f = frames.at(0).at(0).value<TelemetryFrame>();
    QCOMPARE(f.source, QStringLiteral("can"));
    QVERIFY(near(f.v(QStringLiteral("inv.torque_applied_nm")), 99.9));
    QVERIFY(near(f.v(QStringLiteral("inv.torque_cmd_nm")), 120.0));
    QVERIFY(near(f.v(QStringLiteral("inv.speed_rpm")), 2500.0));
    QCOMPARE(f.dtcs.size(), 1);
    QCOMPARE(f.dtcs[0].id, 17);
    // the same counter again: a frozen sender, never forwarded as fresh data
    dev->inject({fdFrame(ID_INV_STATUS, encodeInvStatus(s))});
    // a corrupted frame: counted, dropped
    QByteArray bad = encodeInvStatus(s);
    bad[6] = static_cast<char>(bad[6] ^ 0x10);
    dev->inject({fdFrame(ID_INV_STATUS, bad)});
    s.ctr = 2;
    dev->inject({fdFrame(ID_INV_STATUS, encodeInvStatus(s))});
    QTRY_COMPARE(frames.size(), 2);
    QCOMPARE(t.stats().frozen, quint64(1));
    QCOMPARE(t.stats().crcErr, quint64(1));
    QCOMPARE(t.stats().status, quint64(2));
}

void TstCodec::transportServiceLockAndVcuEmulation()
{
    auto *dev = new MockCanDevice;
    CanTransport t(dev, QStringLiteral("mock"));
    QSignalSpy acks(&t, &ITransport::ack);
    t.open();
    t.send(QStringLiteral("vcu"), {{QStringLiteral("on"), true}});
    QCOMPARE(acks.size(), 1);
    QVERIFY(!acks.at(0).at(0).value<Ack>().ok); // locked: a real bus is never driven without the service key
    QVERIFY(dev->written().isEmpty());
    t.setServiceUnlocked(true);
    t.send(QStringLiteral("vcu"), {{QStringLiteral("on"), true}});
    QVERIFY(acks.at(1).at(0).value<Ack>().ok);
    t.send(QStringLiteral("contactors"), {{QStringLiteral("state"), QStringLiteral("closed")}});
    t.send(QStringLiteral("arm"));
    t.send(QStringLiteral("torque"), {{QStringLiteral("nm"), 80.0}, {QStringLiteral("hold_ms"), 150.0}});
    QTRY_VERIFY_WITH_TIMEOUT(dev->written().size() >= 10, 2000);
    // every emulated frame is valid for the firmware's receiver, counters advance by one
    int lastCtr = -1;
    bool sawTorque = false;
    for (const QCanBusFrame &f : dev->written()) {
        if (f.frameId() == ID_VCU_CMD) {
            const auto c = decodeVcuCmd(f.payload());
            QVERIFY(c.has_value());
            if (lastCtr >= 0) {
                QCOMPARE(int(c->ctr), (lastCtr + 1) & 0x0F);
            }
            lastCtr = c->ctr;
            QCOMPARE(c->contactors, Contactors::Closed);
            sawTorque = sawTorque || near(c->torqueNm, 80.0);
        } else {
            QCOMPARE(f.frameId(), ID_VCU_BMS);
            QVERIFY(decodeVcuBms(f.payload()).has_value());
        }
    }
    QVERIFY(sawTorque);
    // deadman: not refreshed within hold_ms -> the torque returns to 0 by itself
    QTRY_VERIFY_WITH_TIMEOUT(std::abs(t.vcuCommand().torqueNm) < 1e-6, 1000);
    dev->clearWritten();
    t.setServiceUnlocked(false); // relocking silences the emulated VCU
    QVERIFY(!t.vcuEmulation());
    QTest::qWait(50);
    QVERIFY(dev->written().isEmpty());
}

// A UDS request longer than a single frame (IsoTp.h, TX_DL 64): the first frame alone, then the consecutive frames the
// receiver's flow control allows — its block size, STmin between frames, Wait, Overflow — and N_Bs without one.
void TstCodec::transportIsoTpTransmit()
{
    auto *dev = new MockCanDevice;
    CanTransport t(dev, QStringLiteral("mock"));
    QHash<quint64, Ack> acks;
    connect(&t, &ITransport::ack, this, [&acks](const Ack &a) { acks.insert(a.id, a); });
    QElapsedTimer clock;
    clock.start();
    QVector<qint64> at; // when each frame went out
    connect(dev, &QCanBusDevice::framesWritten, this, [&at, &clock] { at << clock.elapsed(); });
    t.open();
    t.setServiceUnlocked(true);
    const auto fc = [dev](const char *hex) { dev->inject({fdFrame(ID_UDS_RSP, QByteArray::fromHex(hex))}); };
    QByteArray msg(200, '\0');
    for (int i = 0; i < msg.size(); i++) {
        msg[i] = static_cast<char>(i);
    }
    msg[0] = 0x36;
    const quint64 id = t.send(QStringLiteral("uds"), {{QStringLiteral("msg"), QString::fromLatin1(msg.toHex())}});
    QCOMPARE(dev->written().size(), 1); // the first frame, then nothing until the flow control
    QCOMPARE(dev->written()[0].payload(), QByteArray::fromHex("10C8") + msg.left(62));
    QCOMPARE(dev->written()[0].frameId(), ID_UDS_REQ);
    const quint64 busy = t.send(QStringLiteral("uds"), {{QStringLiteral("hex"), QStringLiteral("02 3E 00")}});
    QVERIFY(acks.contains(busy) && !acks.value(busy).ok); // refused at once: one request at a time while one is segmented
    fc("300205");                    // ContinueToSend, block size 2, STmin 5 ms
    QTRY_COMPARE(dev->written().size(), 3);
    QTest::qWait(30);
    QCOMPARE(dev->written().size(), 3); // the block is sent: the next flow control first
    QVERIFY2(at[2] - at[1] >= 5, qPrintable(QString::number(at[2] - at[1])));
    QCOMPARE(dev->written()[1].payload(), QByteArray(1, '\x21') + msg.mid(62, 63));
    QCOMPARE(dev->written()[2].payload(), QByteArray(1, '\x22') + msg.mid(125, 63));
    fc("310000"); // Wait: N_Bs again, nothing sent
    QTest::qWait(20);
    QCOMPARE(dev->written().size(), 3);
    fc("300000"); // ContinueToSend, no further flow control
    QTRY_COMPARE(dev->written().size(), 4);
    QCOMPARE(dev->written()[3].payload(), QByteArray(1, '\x23') + msg.mid(188) + QByteArray(3, '\xAA')); // 13 bytes -> 16
    QVERIFY(dev->written()[3].hasFlexibleDataRateFormat());
    dev->inject({fdFrame(ID_UDS_RSP, QByteArray::fromHex("027601AAAAAAAAAA"))});
    QTRY_VERIFY(acks.contains(id));
    const Ack ok = acks.value(id);
    QVERIFY2(ok.ok, qPrintable(ok.message));
    QCOMPARE(ok.raw.value(QLatin1String("msg")).toString(), QStringLiteral("76 01"));
    QCOMPARE(ok.raw.value(QLatin1String("tx_frames")).toInt(), 4);
    QCOMPARE(ok.raw.value(QLatin1String("fc")).toInt(), 3);
    QCOMPARE(ok.raw.value(QLatin1String("fc_wait")).toInt(), 1);
    QCOMPARE(ok.raw.value(QLatin1String("tx_dl")).toInt(), 64);
    // Overflow: abandoned with the reason
    const quint64 id2 = t.send(QStringLiteral("uds"), {{QStringLiteral("msg"), QString::fromLatin1(msg.left(100).toHex())}});
    fc("320000");
    QTRY_VERIFY(acks.contains(id2));
    QVERIFY(!acks.value(id2).ok && acks.value(id2).message.contains(QLatin1String("Overflow")));
    // no flow control at all: N_Bs (1 s)
    const quint64 id3 = t.send(QStringLiteral("uds"), {{QStringLiteral("msg"), QString::fromLatin1(msg.left(100).toHex())}});
    QTRY_VERIFY_WITH_TIMEOUT(acks.contains(id3), 2000);
    QVERIFY(!acks.value(id3).ok && acks.value(id3).message.contains(QLatin1String("N_Bs")));
    // a single frame's worth: classic (8 bytes) and escape (00 SF_DL, the smallest CAN-FD length), padded 0xAA
    dev->clearWritten();
    t.send(QStringLiteral("uds"), {{QStringLiteral("msg"), QStringLiteral("22 FD 20")}});
    t.send(QStringLiteral("uds"), {{QStringLiteral("msg"), QString::fromLatin1(msg.left(12).toHex())}});
    QCOMPARE(dev->written().size(), 2);
    QCOMPARE(dev->written()[0].payload(), QByteArray::fromHex("0322FD20AAAAAAAA"));
    QCOMPARE(dev->written()[1].payload(), QByteArray::fromHex("000C") + msg.left(12) + QByteArray(2, '\xAA'));
}

// Two pages' UDS requests at the same moment (a DID read and the terminal's): the firmware drops a pending response when a
// new request arrives and CanTransport pairs responses with requests in order, so both on the bus at once would give the
// one response to the wrong request. The Session keeps the second in its queue until the first is answered: one frame on
// the bus, then the other; each callback gets its own response, in order, and the ack carries the id command() returned.
// A request nobody answers holds the slot only until its answer times out (the transport's 100 ms), then the next goes.
void TstCodec::sessionSerialisesUds()
{
    auto *dev = new MockCanDevice;
    Session s;
    s.setTransport(std::make_unique<CanTransport>(dev, QStringLiteral("mock")));
    QVERIFY(s.unlockService(QStringLiteral("traction-service"))); // CanTransport transmits only then
    QStringList order;
    QHash<quint64, QByteArray> got;
    QVector<quint64> acked;
    connect(&s, &Session::ackReceived, this, [&acked](const Ack &a) { acked << a.id; });
    auto request = [&](const char *msg) {
        const QString m = QLatin1String(msg);
        return s.command(QStringLiteral("uds"), {{QStringLiteral("msg"), m}}, [&order, &got, m](const Ack &a) {
            order << m;
            got.insert(a.id, QByteArray::fromHex(a.raw.value(QLatin1String("msg")).toString().toLatin1()));
        });
    };
    const quint64 a = request("22 FD 20"), b = request("22 FD 21");
    QVERIFY(a != 0 && b != 0 && a != b);
    QCOMPARE(dev->written().size(), 1); // only the first is on the bus
    QCOMPARE(s.udsQueued(), 1);
    QCOMPARE(dev->written()[0].payload(), QByteArray::fromHex("0322FD20AAAAAAAA"));
    dev->inject({fdFrame(ID_UDS_RSP, QByteArray::fromHex("0562FD20A1B2AAAA"))}); // the ECU answers the request it holds
    QTRY_COMPARE(order.size(), 1);
    QTRY_COMPARE(dev->written().size(), 2); // then the queued one goes out
    QCOMPARE(s.udsQueued(), 0);
    QCOMPARE(dev->written()[1].payload(), QByteArray::fromHex("0322FD21AAAAAAAA"));
    dev->inject({fdFrame(ID_UDS_RSP, QByteArray::fromHex("0562FD21C3D4AAAA"))});
    QTRY_COMPARE(order.size(), 2);
    QCOMPARE(order, (QStringList{QStringLiteral("22 FD 20"), QStringLiteral("22 FD 21")}));
    QCOMPARE(got.value(a), QByteArray::fromHex("62FD20A1B2"));
    QCOMPARE(got.value(b), QByteArray::fromHex("62FD21C3D4"));
    QCOMPARE(acked, (QVector<quint64>{a, b}));
    // unanswered: the slot is held until the transport's timeout answers it, then the waiting request goes
    const quint64 c = request("22 F2 00"), d = request("22 F2 01");
    QCOMPARE(dev->written().size(), 3);
    QTest::qWait(50);
    QCOMPARE(dev->written().size(), 3); // still waiting behind the unanswered one
    QTRY_COMPARE_WITH_TIMEOUT(dev->written().size(), 4, 1000);
    QVERIFY(got.contains(c) && got.value(c).isEmpty()); // "no response within 100 ms"
    QCOMPARE(dev->written()[3].payload(), QByteArray::fromHex("0322F201AAAAAAAA"));
    dev->inject({fdFrame(ID_UDS_RSP, QByteArray::fromHex("0462F20109AAAAAA"))});
    QTRY_VERIFY(got.contains(d));
    QCOMPARE(got.value(d), QByteArray::fromHex("62F20109"));
    QCOMPARE(order.size(), 4);
}

QTEST_MAIN(TstCodec)
#include "tst_codec.moc"
