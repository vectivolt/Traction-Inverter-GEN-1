// FW-38 from the tool, against the real firmware in the simulator bridge. The images are built and signed at test time
// by the firmware's own tool (node firmware/tools/sign-image.mjs --key test: the host build's TEST key, derived from a
// public string — never a release key). The container parsed (every header field, every structural refusal); then the
// update through BridgeTransport: a security version below the card's anti-rollback counter warned about before
// anything is sent, transferred, refused at verification (ROLLBACK) and nothing activated (NRC 0x24); a corrupted
// signature refused at verification (SIGNATURE); the good image — every block acknowledged, verified, activated
// (the simulator's boot record IDLE -> ACTIVATE), ECUReset, the card restarted (a new power-up and key cycle); the same
// update through CanTransport on Qt's virtual CAN-FD bus (BridgeCanGateway): the tool's ISO 15765-2 transmit under the
// firmware's own flow control, relayed by the gateway — there, first, the root of trust read (DID 0xFD23) and the lower
// security version refused by the pre-check against the counter of DID 0xFD24 before any request but that read
// (no 0x27, 0x10 or 0x34); and the Firmware page with an image loaded (offscreen).
// Skips without the bridge, Node or (the CAN case) the virtualcan plugin, and says so.
#include "BridgeCanGateway.h"
#include "CanTransport.h"
#include "FirmwarePage.h"
#include "FirmwareUpdater.h"
#include "IsoTp.h"
#include "Widgets.h"

#include <QCanBus>
#include <QFile>
#include <QLoggingCategory>
#include <QProcess>
#include <QPushButton>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QtTest>

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

constexpr quint32 RUNNING_FW_ID = 0x0A0F0015; // the bridge's image (hello fw_id)
constexpr quint32 NEW_FW_ID = 0x0A0F0016;
constexpr int PAYLOAD = 16000; // + the 128-byte header: 4093 + 4093 + 4093 + 3849 bytes in 4 TransferData blocks

// A UDS request through the session; the response payload ("" when none came).
QByteArray uds(Session &s, const QByteArray &msg, QString *info = nullptr)
{
    QByteArray r;
    bool done = false;
    s.command(QStringLiteral("uds"), {{QStringLiteral("msg"), QString::fromLatin1(msg.toHex(' '))}}, [&](const Ack &a) {
        r = QByteArray::fromHex(a.raw.value(QLatin1String("msg")).toString().toLatin1());
        if (info) {
            *info = a.message;
        }
        done = true;
    });
    until([&] { return done; }, 6000);
    return r;
}

QString summary(const FirmwareUpdater &u)
{
    return QStringLiteral("step %1, %2/%3 blocks, %4 bytes, block %5, busy %6, verify %7/%8, nrc %9: %10")
        .arg(FirmwareUpdater::stepName(u.step())).arg(u.blocksDone()).arg(u.blocksTotal()).arg(u.bytesSent())
        .arg(u.blockMax()).arg(u.busyRepeats()).arg(u.verifyStatus()).arg(u.verifyReason()).arg(u.nrc()).arg(u.text());
}
} // namespace

class TstUpdate : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void container();
    void overBridge();
    void overCan();

private:
    // node firmware/tools/sign-image.mjs --key test; the path, or empty with m_skip saying why
    QString sign(const QString &name, const QByteArray &payload, quint32 fwId, int secVer);
    QTemporaryDir m_dir;
    QString m_skip;
    FirmwareUpdater::Image m_good, m_old, m_badSig;
};

QString TstUpdate::sign(const QString &name, const QByteArray &payload, quint32 fwId, int secVer)
{
    const QString bin = m_dir.filePath(name + QStringLiteral(".bin")), out = m_dir.filePath(name + QStringLiteral(".tifw"));
    QFile f(bin);
    if (!f.open(QIODevice::WriteOnly) || f.write(payload) != payload.size()) {
        m_skip = QStringLiteral("cannot write %1").arg(bin);
        return {};
    }
    f.close();
    QProcess node;
    node.start(QStringLiteral("node"), {QStringLiteral(TT_SOURCE_DIR "/../../firmware/tools/sign-image.mjs"), QStringLiteral("--in"), bin,
                                        QStringLiteral("--out"), out, QStringLiteral("--key"), QStringLiteral("test"),
                                        QStringLiteral("--target"), QStringLiteral("8xx-sic"), QStringLiteral("--fw-id"),
                                        QStringLiteral("0x%1").arg(fwId, 8, 16, QLatin1Char('0')), QStringLiteral("--sec-ver"),
                                        QString::number(secVer)});
    if (!node.waitForFinished(20000) || node.exitStatus() != QProcess::NormalExit || node.exitCode() != 0) {
        m_skip = QStringLiteral("node firmware/tools/sign-image.mjs did not run (%1): %2")
                     .arg(node.errorString(), QString::fromUtf8(node.readAllStandardError()).trimmed());
        return {};
    }
    return out;
}

void TstUpdate::initTestCase()
{
    QVERIFY(m_dir.isValid());
    QByteArray payload(PAYLOAD, '\0');
    quint32 lcg = 38;
    for (char &c : payload) { // a stand-in payload: pseudo-random bytes
        lcg = lcg * 1664525u + 1013904223u;
        c = static_cast<char>(lcg >> 24);
    }
    const QString good = sign(QStringLiteral("good"), payload, NEW_FW_ID, 2);
    const QString old = good.isEmpty() ? QString() : sign(QStringLiteral("old"), payload, NEW_FW_ID, 0);
    if (old.isEmpty()) {
        return; // m_skip says why; the cases skip
    }
    m_good = FirmwareUpdater::load(good);
    m_old = FirmwareUpdater::load(old);
    m_badSig = FirmwareUpdater::parse(m_good.bytes);
    m_badSig.bytes[64 + 10] = static_cast<char>(m_badSig.bytes[64 + 10] ^ 0x01); // one bit of the signature
    m_badSig.path = QStringLiteral("good.tifw with one signature bit flipped");
}

// The header as image.h defines it; every structural refusal before anything is sent.
void TstUpdate::container()
{
    if (!m_skip.isEmpty()) {
        QSKIP(qPrintable(m_skip));
    }
    QVERIFY2(m_good.ok(), qPrintable(m_good.error));
    QCOMPARE(m_good.bytes.size(), FirmwareUpdater::HDR_LEN + PAYLOAD);
    QCOMPARE(m_good.target, 1u); // 8XX SiC
    QCOMPARE(m_good.length, quint32(PAYLOAD));
    QCOMPARE(m_good.fwId, NEW_FW_ID);
    QCOMPARE(m_good.secVer, 2u);
    QVERIFY(m_good.hashOk && m_good.signature);
    QVERIFY2(m_badSig.ok(), qPrintable(m_badSig.error)); // only the firmware can tell a signature bit
    QVERIFY(m_old.ok() && m_old.secVer == 0);
    const auto broken = [this](int at, char x) {
        QByteArray b = m_good.bytes;
        b[at] = static_cast<char>(b[at] ^ x);
        return FirmwareUpdater::parse(b).error;
    };
    QVERIFY(broken(1, 0x01).contains(QLatin1String("magic")));
    QVERIFY(broken(4, 0x02).contains(QLatin1String("format")));
    QVERIFY(broken(8, 0x04).contains(QLatin1String("target 5")));
    QVERIFY(broken(25, 0x01).contains(QLatin1String("reserved")));
    QVERIFY(broken(12, 0x01).contains(QLatin1String("payload length")));
    QVERIFY(broken(FirmwareUpdater::HDR_LEN + 999, 0x10).contains(QLatin1String("SHA-256"))); // one payload bit
    QVERIFY(FirmwareUpdater::parse(m_good.bytes.left(100)).error.contains(QLatin1String("header")));
    QVERIFY(FirmwareUpdater::parse(m_good.bytes + '\0').error.contains(QLatin1String("payload length")));
    QByteArray unsig = m_good.bytes;
    unsig.replace(64, 64, QByteArray(64, '\0'));
    QVERIFY(FirmwareUpdater::parse(unsig).error.contains(QLatin1String("unsigned")));
    // the transport's framing of the blocks (TX_DL 64): 4095 bytes = a first frame and 65 consecutive frames
    const QVector<QByteArray> fr = IsoTp::frames(QByteArray(4095, '\x5A'));
    QCOMPARE(fr.size(), 66);
    QCOMPARE(fr[0].left(2), QByteArray::fromHex("1FFF"));
    QCOMPARE(fr[15].at(0), '\x2F');
    QCOMPARE(fr[16].at(0), '\x20');
    QCOMPARE(fr.last(), QByteArray::fromHex("215AAAAAAAAAAAAA")); // 4095 - 62 - 64 x 63 = 1 byte, padded 0xAA to 8
}

void TstUpdate::overBridge()
{
    if (!m_skip.isEmpty()) {
        QSKIP(qPrintable(m_skip));
    }
    BridgeTransport::Options o;
    o.program = BridgeTransport::locate();
    if (o.program.isEmpty()) {
        QSKIP("simulator bridge not built (make -C tool/bridge): the update over the bridge did not run");
    }
    o.rateHz = 20;
    o.timeFactor = 1; // real time, as the GUI runs it
    Session s;
    s.setTransport(std::make_unique<BridgeTransport>(o));
    QVERIFY(until([&] { return s.isDeviceLive() && s.last().v(QStringLiteral("state.sm")) == 4.0; }, 10000)); // PRECHARGE_WAIT
    FirmwarePage page(s); // the page's own updater runs every case: its rendering follows a real update
    page.resize(1280, 2400);
    page.show();
    FirmwareUpdater &up = page.updater();
    QVERIFY(up.readIdentity());
    QVERIFY(until([&] { return up.identity().valid || !up.identity().error.isEmpty(); }, 5000));
    const FirmwareUpdater::Identity id0 = up.identity();
    QVERIFY2(id0.valid, qPrintable(id0.error));
    QCOMPARE(id0.fwId, RUNNING_FW_ID);
    QCOMPARE(id0.sku, 1);
    // the bench card's FW-38 boot record (PROTOCOL.md A.1: provisioned at the first power-up): IDLE, counter 1
    QCOMPARE(id0.bootRec.value(QLatin1String("state")).toInt(-1), 0);
    QCOMPARE(id0.bootRec.value(QLatin1String("sec_counter")).toInt(-1), 1);
    QCOMPARE(id0.bootRec.value(QLatin1String("target")).toInt(-1), 1);
    qInfo("running: TI_FW_ID 0x%08X, SKU %d/%d/%d, card %s, key cycle %u, power-up %d, boot record %s", id0.fwId, id0.sku, id0.hwSku,
          id0.calSku, qPrintable(id0.serial), id0.keyCycle, id0.boot, QJsonDocument(id0.bootRec).toJson(QJsonDocument::Compact).constData());

    // ---- before anything is sent: the lower security version is named, the good image draws no warning
    QVERIFY2(up.warnings(m_old).join(QLatin1Char('|')).contains(QLatin1String("anti-rollback counter 1")), qPrintable(up.warnings(m_old).join(QLatin1Char('|'))));
    QVERIFY2(up.warnings(m_good).isEmpty(), qPrintable(up.warnings(m_good).join(QLatin1Char('|'))));
    // ... and the Firmware page shows it: a usable image enables Update…, a truncated one does not
    QVERIFY(page.loadImage(m_good.path));
    QPushButton *update = nullptr;
    for (QPushButton *b : page.findChildren<QPushButton *>()) {
        update = b->accessibleName() == QLatin1String("firmwareUpdate") ? b : update;
    }
    QVERIFY(update);
    QVERIFY(until([&] { return page.updater().identity().valid && update->isEnabled(); }, 5000));
    QFile cut(m_dir.filePath(QStringLiteral("cut.tifw")));
    QVERIFY(cut.open(QIODevice::WriteOnly) && cut.write(m_good.bytes.left(1000)) == 1000);
    cut.close();
    QVERIFY(!page.loadImage(cut.fileName()));
    QVERIFY(!update->isEnabled());
    QVERIFY(page.loadImage(m_good.path)); // the page reads the running image again: one read, the second call joins it
    QVERIFY(!up.start(m_good));          // nothing starts while it is in flight
    QVERIFY2(up.text().contains(QLatin1String("being read")), qPrintable(up.text()));
    QVERIFY(until([&] { return !up.reading() && update->isEnabled(); }, 5000));

    const auto run = [&](const FirmwareUpdater::Image &img) { return up.start(img) && until([&] { return !up.busy(); }, 60000); };
    const auto rec = [&s] { // the simulator's boot record, as the hello prints it now
        bool done = false;
        s.command(QStringLiteral("info"), {}, [&done](const Ack &) { done = true; });
        until([&done] { return done; }, 5000);
        return s.deviceInfo().value(QLatin1String("boot_rec")).toObject();
    };

    // ---- a security version below the counter: every block acknowledged, refused at verification (ROLLBACK), and the
    //      activation refused (NRC 0x24): the boot record stays IDLE, nothing is installed
    QVERIFY(run(m_old));
    qInfo("security version 0: %s", qPrintable(summary(up)));
    QCOMPARE(up.step(), FirmwareUpdater::Step::Failed);
    QCOMPARE(up.blocksDone(), 4);
    QCOMPARE(up.blocksTotal(), 4);
    QCOMPARE(up.bytesSent(), m_old.bytes.size());
    QCOMPARE(up.verifyStatus(), 2);
    QCOMPARE(up.verifyReason(), 9); // IMG_ERR_ROLLBACK
    QVERIFY(!up.activated());
    QString info;
    QCOMPARE(uds(s, QByteArray::fromHex("3101F038"), &info), QByteArray::fromHex("7F3124"));
    QCOMPARE(rec().value(QLatin1String("state")).toInt(-1), 0);

    // ---- one signature bit: refused at verification (SIGNATURE)
    QVERIFY(run(m_badSig));
    qInfo("signature bit: %s", qPrintable(summary(up)));
    QCOMPARE(up.step(), FirmwareUpdater::Step::Failed);
    QCOMPARE(up.blocksDone(), 4);
    QCOMPARE(up.verifyStatus(), 2);
    QCOMPARE(up.verifyReason(), 7); // IMG_ERR_SIGNATURE
    QCOMPARE(rec().value(QLatin1String("state")).toInt(-1), 0);

    // ---- the good image: verified, activated (the boot record IDLE -> ACTIVATE), ECUReset, the card restarted
    QVERIFY(run(m_good));
    qInfo("good image: %s", qPrintable(summary(up)));
    QVERIFY2(up.step() == FirmwareUpdater::Step::Done, qPrintable(summary(up)));
    QCOMPARE(up.blocksDone(), 4);
    QCOMPARE(up.bytesSent(), m_good.bytes.size());
    QCOMPARE(up.blockMax(), 4095);
    QCOMPARE(up.verifyStatus(), 1);
    QVERIFY(up.activated() && up.restarted());
    const FirmwareUpdater::Identity &b = up.identity(), &a = up.after(); // just before the activation; after the restart
    QCOMPARE(b.bootRec.value(QLatin1String("state")).toInt(-1), 0); // IDLE ...
    QCOMPARE(a.boot, b.boot + 1);           // the bridge restarted the card: a power-up ...
    QCOMPARE(a.keyCycle, b.keyCycle + 1);   // ... a new key cycle
    QVERIFY(a.uptimeMs < b.uptimeMs);
    QCOMPARE(a.fwId, RUNNING_FW_ID);        // the host build runs its own code: no bootloader installs the payload
    QCOMPARE(a.bootRec.value(QLatin1String("state")).toInt(-1), 1); // ... ACTIVATE, and it stays so: no bootloader runs
    QCOMPARE(up.transport().value(QLatin1String("fc_bs")).toInt(), 4);
    QCOMPARE(up.transport().value(QLatin1String("fc_stmin")).toInt(), 0);
    QCOMPARE(up.transport().value(QLatin1String("tx_dl")).toInt(), 64);
    qInfo("bridge: %lld bytes in %d blocks of <= %d bytes (%d data), TransferData %.3f s (%.1f kB/s), whole update %.2f s; the "
          "firmware's flow control BS %d STmin %d, TX_DL %d; busy repeats %d",
          static_cast<long long>(up.bytesSent()), up.blocksDone(), up.blockMax(), up.blockMax() - 2, up.transferMs() / 1000.0,
          static_cast<double>(up.bytesSent()) / std::max<qint64>(1, up.transferMs()), up.elapsedMs() / 1000.0, up.transport().value(QLatin1String("fc_bs")).toInt(),
          up.transport().value(QLatin1String("fc_stmin")).toInt(), up.transport().value(QLatin1String("tx_dl")).toInt(), up.busyRepeats());
    qInfo("after the reset: %s", qPrintable(up.text()));
    if (!qEnvironmentVariableIsEmpty("TT_SCREENSHOTS")) {
        QTest::qWait(200);
        page.grab().save(qEnvironmentVariable("TT_SCREENSHOTS") + QStringLiteral("/firmware-update-done.png"));
    }

    // ---- the firmware's transport gap (reported, not worked around): a programming request of 8-62 bytes travels as an
    //      ISO 15765-2 escape single frame, which uds_handle() drops (uds.c: n == 0) — TransferData outside the session
    //      should answer 7F 36 7F as its classic single frame does
    QCOMPARE(uds(s, QByteArray::fromHex("360101020304")), QByteArray::fromHex("7F367F"));
    const QByteArray esc = uds(s, QByteArray::fromHex("3601") + QByteArray(10, '\x5A'), &info);
    QCOMPARE(esc, QByteArray::fromHex("7F367F"));
    qInfo("36 01 + 10 bytes as an escape single frame: '%s' (%s)", esc.toHex(' ').constData(), qPrintable(info));
}

void TstUpdate::overCan()
{
    if (!m_skip.isEmpty()) {
        QSKIP(qPrintable(m_skip));
    }
    BridgeTransport::Options bo;
    bo.program = BridgeTransport::locate();
    if (bo.program.isEmpty()) {
        QSKIP("simulator bridge not built (make -C tool/bridge): the update over CAN did not run");
    }
    if (!QCanBus::instance()->plugins().contains(QStringLiteral("virtualcan"))) {
        QSKIP("this Qt SerialBus has no virtualcan plugin: the update over CAN did not run");
    }
    bo.rateHz = 10;
    QLoggingCategory::setFilterRules(QStringLiteral("qt.canbus.plugins.virtualcan.info=false"));
    quint16 port = 0;
    { // a port of this process's own: never another program's bus
        QTcpServer probe;
        QVERIFY(probe.listen(QHostAddress::LocalHost, 0));
        port = probe.serverPort();
    }
    CanTransport::Options co;
    co.plugin = QStringLiteral("virtualcan");
    co.interface = QStringLiteral("tcp://127.0.0.1:%1/can0").arg(port);
    QString err;
    QCanBusDevice *gwDev = CanTransport::createDevice(co, &err);
    QVERIFY2(gwDev, qPrintable(err));
    BridgeCanGateway gw(bo, gwDev);
    QCanBusDevice *toolDev = CanTransport::createDevice(co, &err);
    QVERIFY2(toolDev, qPrintable(err));
    QJsonObject hello;
    connect(&gw.bridge(), &ITransport::info, this, [&](const QJsonObject &o) {
        if (o.value(QLatin1String("type")).toString() == QLatin1String("hello")) {
            hello = o;
        }
    });
    Session s;
    s.setTransport(std::make_unique<CanTransport>(toolDev, QStringLiteral("virtual CAN")));
    gw.start();
    const auto sm = [&s] { return static_cast<int>(s.last().v(QStringLiteral("inv.state"), -1.0)); };
    // booted: the self-tests done (VEHICLE_HANDSHAKE without a VCU on the bus, or PRECHARGE_WAIT) — standstill proven
    QVERIFY(until([&] { return s.isDeviceLive() && !hello.isEmpty() && (sm() == 3 || sm() == 4); }, 10000));
    QVERIFY(s.unlockService(QStringLiteral("traction-service")));
    // the root of trust over CAN: DID 0xFD23, the same as the bridge's hello says
    QVERIFY(until([&] { return s.deviceInfo().contains(QLatin1String("root")); }, 5000));
    const QJsonObject root = s.deviceInfo().value(QLatin1String("root")).toObject();
    qInfo("root of trust over CAN (DID 0xFD23): %s; the bridge's hello: %s", QJsonDocument(root).toJson(QJsonDocument::Compact).constData(),
          QJsonDocument(hello.value(QLatin1String("root")).toObject()).toJson(QJsonDocument::Compact).constData());
    QCOMPARE(root.value(QLatin1String("kind")).toString(), QStringLiteral("test"));
    QCOMPARE(root.value(QLatin1String("key_id")).toString(), hello.value(QLatin1String("root")).toObject().value(QLatin1String("key_id")).toString());
    FirmwareUpdater up(s);
    QVERIFY(up.readIdentity());
    QVERIFY(until([&] { return up.identity().valid || !up.identity().error.isEmpty(); }, 5000));
    QVERIFY2(up.identity().valid, qPrintable(up.identity().error));
    QCOMPARE(up.identity().fwId, RUNNING_FW_ID);
    QCOMPARE(up.identity().bootRec.value(QLatin1String("sec_counter")).toInt(-1), 1); // DID 0xFD24 over CAN
    QCOMPARE(up.identity().bootRec.value(QLatin1String("state")).toInt(-1), 0);

    // ---- the pre-check over CAN: security version 0 below the counter 1 — refused before anything but the DID read
    QVector<int> sids; // the service of every UDS request the tool puts on the bus (a single or a first frame)
    const QMetaObject::Connection tap = connect(&s, &Session::traffic, this, [&sids](const QString &dir, const QString &line) {
        if (dir != QLatin1String("tx") || !line.startsWith(QLatin1String("0x7e1"))) {
            return;
        }
        const QByteArray f = QByteArray::fromHex(line.section(QLatin1Char(']'), 1).toLatin1());
        const int pci = f.isEmpty() ? 0xFF : static_cast<quint8>(f[0]);
        if (pci >> 4 <= 1 && f.size() >= 3) { // single frame (00: escape) or first frame: the SID follows the PCI
            sids << static_cast<quint8>(f[(pci == 0 || pci >> 4 == 1) ? 2 : 1]);
        }
    });
    QVERIFY(up.start(m_old)); // it begins: the fresh read of the counter
    QVERIFY(until([&] { return !up.busy(); }, 10000));
    disconnect(tap);
    qInfo("CAN pre-check: %s; requests sent: %s", qPrintable(summary(up)),
          qPrintable(std::accumulate(sids.begin(), sids.end(), QString(), [](QString a, int x) { return a + QStringLiteral(" %1").arg(x, 2, 16, QLatin1Char('0')); })));
    QCOMPARE(up.step(), FirmwareUpdater::Step::Failed);
    QVERIFY2(up.text().contains(QLatin1String("below this card's anti-rollback counter 1")), qPrintable(up.text()));
    QCOMPARE(up.nrc(), -1);
    QCOMPARE(up.blockMax(), 0); // no RequestDownload answered
    QVERIFY(!sids.isEmpty());
    for (int sid : sids) {
        QCOMPARE(sid, 0x22); // ReadDataByIdentifier only: no 0x27, no 0x10, no 0x34
    }
    const int boot = hello.value(QLatin1String("boot")).toInt();
    QVERIFY(up.start(m_good));
    QVERIFY(until([&] { return !up.busy(); }, 60000));
    qInfo("CAN: %s", qPrintable(summary(up)));
    QVERIFY2(up.step() == FirmwareUpdater::Step::Done, qPrintable(summary(up)));
    QCOMPARE(up.blocksDone(), 4);
    QCOMPARE(up.bytesSent(), m_good.bytes.size());
    QCOMPARE(up.verifyStatus(), 1);
    QVERIFY(up.activated() && up.restarted());
    QCOMPARE(up.after().keyCycle, up.identity().keyCycle + 1);
    // the tool's ISO 15765-2 transmit under the firmware's own flow control (relayed by the gateway): block size 4,
    // STmin 0; the last block (3851 bytes) a first frame and 61 consecutive frames
    QCOMPARE(up.transport().value(QLatin1String("fc_bs")).toInt(), 4);
    QCOMPARE(up.transport().value(QLatin1String("fc_stmin")).toInt(), 0);
    QCOMPARE(up.transport().value(QLatin1String("tx_dl")).toInt(), 64);
    QCOMPARE(up.transport().value(QLatin1String("tx_frames")).toInt(), 62);
    QVERIFY(until([&] { return hello.value(QLatin1String("boot")).toInt() == boot + 1; }, 5000)); // the bridge restarted the card
    QCOMPARE(hello.value(QLatin1String("boot_rec")).toObject().value(QLatin1String("state")).toInt(-1), 1); // ACTIVATE
    QCOMPARE(gw.stats().dropped, quint64(0));
    QCOMPARE(gw.stats().refused, quint64(0));
    qInfo("CAN: %lld bytes in %d blocks of <= %d bytes (%d data), TransferData %.3f s (%.1f kB/s), whole update %.2f s; the "
          "firmware's flow control BS %d STmin %d, TX_DL %d; busy repeats %d; gateway %llu frames to the bus, %llu to the firmware",
          static_cast<long long>(up.bytesSent()), up.blocksDone(), up.blockMax(), up.blockMax() - 2, up.transferMs() / 1000.0,
          static_cast<double>(up.bytesSent()) / std::max<qint64>(1, up.transferMs()), up.elapsedMs() / 1000.0, up.transport().value(QLatin1String("fc_bs")).toInt(),
          up.transport().value(QLatin1String("fc_stmin")).toInt(), up.transport().value(QLatin1String("tx_dl")).toInt(), up.busyRepeats(),
          static_cast<unsigned long long>(gw.stats().toBus), static_cast<unsigned long long>(gw.stats().toFirmware));
    qInfo("after the reset: %s", qPrintable(up.text()));
}

QTEST_MAIN(TstUpdate)
#include "tst_update.moc"
