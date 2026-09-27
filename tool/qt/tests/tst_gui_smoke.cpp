// GUI smoke test, offscreen (QT_QPA_PLATFORM=offscreen), against the real simulator bridge: start it, see live
// telemetry, arm through the Controls page's Arm button (the firmware's checklist must allow it), hold the torque
// deadman and see the command and the APPLIED torque follow, release it and see them return, inject a DESAT and see
// DTC_DESAT_HS in the Faults page's table, disarm; then stop the bridge and see the data go stale. Then, on a fresh
// bridge in real time, the Commissioning page's logic end to end (FW-39): arm, the VCU's enable withdrawn and the
// vehicle speed 0 through the page's buttons, SecurityAccess, the Rs routine on the locked rotor (LK) polled to DONE
// under the tool's heartbeat, Rs against the plant, the JSON/CSV export read back, the commit, and a start refused by
// the firmware at 5 km/h (NRC 0x22, reason 8); then Ld/Lq (LK) and psi/zero/direction on the dyno (DF, DF against
// the rotation, DR) against the plant with the firmware host test's tolerances, Rs again, all five staged and
// committed, a reboot and the record the next key cycle validated (DID 0xFD21), and the heartbeat abort of a frozen
// tool (DTC_MC_ABORTED, armed idle, no torque). Round 23, on a third bridge in real time: the root-of-trust banner (the
// host build's TEST key) on the Dashboard and the Firmware page; FW-46's ripple table — the example CSV imported through
// the page, refused at the default cal_ripple_ff_max_a (NRC 0x31), written (2E FD 46, one segmented request) and read back
// equal, committed, the card rebooted and the active table read back; FW-45's sweep — the Ld/Lq routine at the six bias
// indices, every point VALID and within 5 % of the plant's inductance (the plant does not saturate: each equals the
// scalar), all twelve staged, the commit accepted, the card rebooted and the record's maps read back (DID 0xFD26: the
// staged points within 0.2 %, i_map the SKU's limit) and shown in the page's Record column.
// TT_SCREENSHOTS=<dir> also saves a PNG of every page (dark and light).
#include "AnalysisPage.h"
#include "BugReport.h"
#include "CommissioningPage.h"
#include "ConnectDialogs.h"
#include "ControlsPage.h"
#include "FaultsPage.h"
#include "LogIo.h"
#include "MainWindow.h"
#include "Page.h"
#include "ProtocolInfo.h"
#include "Theme.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace {
QPushButton *buttonNamed(QWidget *root, const QString &accessible)
{
    for (QPushButton *b : root->findChildren<QPushButton *>()) {
        if (b->accessibleName() == accessible) {
            return b;
        }
    }
    return nullptr;
}

QString diag(Session &s)
{
    const TelemetryFrame &f = s.last();
    QStringList parts;
    parts << QStringLiteral("sm=%1 vcu.seq=%2 note=%3").arg(f.v(QStringLiteral("state.sm"))).arg(f.str(QStringLiteral("vcu.seq")), f.str(QStringLiteral("vcu.note")));
    for (auto it = f.num.begin(); it != f.num.end(); ++it) {
        if (it.key().startsWith(QLatin1String("arm.")) && it.value() == 0.0) {
            parts << it.key();
        }
    }
    for (const LogEntry &e : s.events()) {
        if (e.level != QLatin1String("tx")) {
            parts << e.level + QLatin1Char(':') + e.text;
        }
    }
    return parts.join(QStringLiteral(" | "));
}

QString checksText(const CommissioningSequencer &seq)
{
    QStringList p;
    const auto c = seq.checks();
    for (int i = 0; i < c.size(); i++) {
        p << QStringLiteral("%1=%2 %3").arg(CommissioningSequencer::checkItems()[i].first).arg(static_cast<int>(c[i].first)).arg(c[i].second);
    }
    return p.join(QStringLiteral(" | "));
}

bool tableHas(QTableWidget *t, const QString &text)
{
    for (int r = 0; r < t->rowCount(); r++) {
        if (t->item(r, 1) && t->item(r, 1)->text() == text) {
            return true;
        }
    }
    return false;
}

void shots(MainWindow &w, const QString &tag)
{
    const QString dir = qEnvironmentVariable("TT_SCREENSHOTS");
    if (dir.isEmpty()) {
        return;
    }
    QDir().mkpath(dir);
    for (int i = 0; i < w.pageCount(); i++) {
        w.showPage(i);
        QTest::qWait(250);
        w.grab().save(QStringLiteral("%1/%2-%3-%4.png").arg(dir, tag).arg(i).arg(w.page(i)->title().toLower().replace(QLatin1Char(' '), QLatin1Char('_'))));
    }
}
} // namespace

class TstGuiSmoke : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void armTorqueFaultDisarm();
    void commissioning();
    void mapAndRipple();
};

void TstGuiSmoke::armTorqueFaultDisarm()
{
    const QString bridge = BridgeTransport::locate();
    if (bridge.isEmpty()) {
        QSKIP("simulator bridge not built (make -C tool/bridge): the GUI smoke test did not run");
    }
    Theme::apply(*qApp, Theme::Variant::Dark);
    MainWindow w;
    w.resize(1440, 960);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    Session &s = w.session();
    BridgeTransport::Options o;
    o.program = bridge;
    o.rateHz = 100;
    o.timeFactor = 2;
    w.connectSimulator(o);
    QTRY_VERIFY_WITH_TIMEOUT(s.isDeviceLive(), 10000);
    QTemporaryDir tmp;
    const QString log = tmp.filePath(QStringLiteral("session.jsonl"));
    QString err;
    QVERIFY2(s.startRecording(log, &err), qPrintable(err));
    QVERIFY(s.deviceInfo().contains(QLatin1String("fw_id")));
    QTRY_VERIFY_WITH_TIMEOUT(s.params().haveDevice(), 5000);

    // ---- arm through the page's button, once the firmware's preconditions allow it
    w.showPage(2);
    ControlsPage *c = w.controls();
    QPushButton *arm = buttonNamed(c, QStringLiteral("armButton"));
    QVERIFY(arm);
    QTRY_COMPARE_WITH_TIMEOUT(static_cast<int>(s.last().v(QStringLiteral("state.sm"))), 4, 10000); // PRECHARGE_WAIT: self-test done
    QTRY_VERIFY_WITH_TIMEOUT(arm->isEnabled(), 10000); // the page re-evaluates the checklist on its 100 ms tick
    QTest::mouseClick(arm, Qt::LeftButton);
    QTRY_VERIFY2_WITH_TIMEOUT(s.last().b(QStringLiteral("arm.armed")), qPrintable(diag(s)), 15000);
    QCOMPARE(static_cast<int>(s.last().v(QStringLiteral("state.sm"))), 6); // ARMED_ZERO_TORQUE

    // ---- hold-to-apply torque: the command and the applied torque follow, then return on release
    s.command(QStringLiteral("speed"), {{QStringLiteral("rpm"), 1000}, {QStringLiteral("ramp_rpm_s"), 5000}});
    QTRY_VERIFY_WITH_TIMEOUT(s.last().v(QStringLiteral("motion.speed_rpm")) > 900, 10000);
    c->setTorqueSetpoint(60.0);
    QPushButton *deadman = buttonNamed(c, QStringLiteral("deadmanButton"));
    QVERIFY(deadman && deadman->isEnabled());
    QTest::mousePress(deadman, Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(s.last().v(QStringLiteral("motion.torque_cmd_nm")) > 50.0, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(s.last().v(QStringLiteral("inv.torque_applied_nm")) > 50.0, 10000);
    QCOMPARE(static_cast<int>(s.last().v(QStringLiteral("state.sm"))), 7); // RUN
    QTest::mouseRelease(deadman, Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(std::abs(s.last().v(QStringLiteral("motion.torque_cmd_nm"))) < 1.0, 10000);
    // leaving the page releases the deadman (focus loss): for the screenshots, a timed torque instead
    if (!qEnvironmentVariableIsEmpty("TT_SCREENSHOTS")) {
        s.command(QStringLiteral("torque"), {{QStringLiteral("nm"), 120.0}, {QStringLiteral("hold_ms"), 6000.0}});
        QTest::qWait(1500);
        shots(w, QStringLiteral("running"));
        w.showPage(2);
        s.command(QStringLiteral("torque"), {{QStringLiteral("nm"), 0.0}});
    }
    QTRY_VERIFY_WITH_TIMEOUT(std::abs(s.last().v(QStringLiteral("motion.torque_cmd_nm"))) < 1.0, 10000);

    // ---- a DESAT on a high side: the Faults page lists DTC_DESAT_HS, the firmware goes to FAULT
    QPushButton *desat = buttonNamed(c, QStringLiteral("inject_desat_hs"));
    QVERIFY(desat && desat->isEnabled());
    QTest::mouseClick(desat, Qt::LeftButton);
    QTRY_COMPARE_WITH_TIMEOUT(static_cast<int>(s.last().v(QStringLiteral("state.sm"))), 9, 10000); // FAULT
    w.showPage(5);
    QTRY_VERIFY_WITH_TIMEOUT(tableHas(w.faults()->dtcTable(), QStringLiteral("DESAT_HS")), 5000);
    shots(w, QStringLiteral("fault"));

    // ---- disarm through the page's button
    w.showPage(2);
    QPushButton *disarm = buttonNamed(c, QStringLiteral("disarmButton"));
    QVERIFY(disarm && disarm->isEnabled());
    QTest::mouseClick(disarm, Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(!s.last().b(QStringLiteral("vcu.enable")) && !s.last().b(QStringLiteral("state.gate_en")), 10000);

    // ---- light theme renders, then the link goes away: the data turn stale, never "live"
    w.setTheme(true);
    shots(w, QStringLiteral("light"));
    w.setTheme(false);
    s.command(QStringLiteral("pause"), {{QStringLiteral("on"), true}});
    QTRY_COMPARE_WITH_TIMEOUT(s.link(), Session::Link::Paused, 5000);
    QVERIFY(!s.isLive());
    QTRY_VERIFY_WITH_TIMEOUT(!arm->isEnabled() && !deadman->isEnabled(), 2000);
    shots(w, QStringLiteral("paused"));
    const int recorded = static_cast<int>(s.recorder().frames());
    s.closeTransport(); // stops the recording too
    QCOMPARE(s.link(), Session::Link::None);
    QVERIFY(recorded > 100);

    // ---- the recording analyses and replays
    auto *analysis = qobject_cast<AnalysisPage *>(w.page(6));
    QVERIFY(analysis);
    QVERIFY2(analysis->loadFile(log, &err), qPrintable(err));
    QVERIFY(analysis->channelCount() > 100);
    if (!qEnvironmentVariableIsEmpty("TT_SCREENSHOTS")) {
        const QString dir = qEnvironmentVariable("TT_SCREENSHOTS");
        w.showPage(6);
        QTest::qWait(300);
        w.grab().save(dir + QStringLiteral("/analysis-loaded.png"));
        CanDialog cd(&w);
        cd.show();
        QTest::qWait(200);
        cd.grab().save(dir + QStringLiteral("/dialog-can.png"));
        SimulatorDialog sd(&w);
        sd.show();
        QTest::qWait(200);
        sd.grab().save(dir + QStringLiteral("/dialog-sim.png"));
    }
    QVERIFY(w.openReplay(log));
    QTRY_COMPARE_WITH_TIMEOUT(s.link(), Session::Link::Replay, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(s.store().size() > 50, 5000);
    QVERIFY(!arm->isEnabled()); // a replay commands nothing
    s.closeTransport();
}

void TstGuiSmoke::commissioning()
{
    using Seq = CommissioningSequencer;
    const QString bridge = BridgeTransport::locate();
    if (bridge.isEmpty()) {
        QSKIP("simulator bridge not built (make -C tool/bridge): the commissioning scenario did not run");
    }
    Theme::apply(*qApp, Theme::Variant::Dark);
    MainWindow w;
    w.resize(1440, 960);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    Session &s = w.session();
    BridgeTransport::Options o;
    o.program = bridge;
    o.rateHz = 100;
    o.timeFactor = 1; // real time, as the GUI runs: the 50 ms results poll is the firmware's heartbeat (200 ms)
    w.connectSimulator(o);
    QTRY_VERIFY_WITH_TIMEOUT(s.isDeviceLive(), 10000);
    w.showPage(3);
    auto *page = qobject_cast<CommissioningPage *>(w.page(3));
    QVERIFY(page);
    Seq &seq = page->sequencer();

    // ---- the preconditions through the page's own buttons: arm, the VCU's enable withdrawn, the vehicle speed 0
    QTRY_COMPARE_WITH_TIMEOUT(static_cast<int>(s.last().v(QStringLiteral("state.sm"))), 4, 10000); // PRECHARGE_WAIT
    QPushButton *arm = buttonNamed(page, QStringLiteral("commissionArm"));
    QPushButton *start = buttonNamed(page, QStringLiteral("commissionStart"));
    QVERIFY(arm && start);
    QTRY_VERIFY_WITH_TIMEOUT(arm->isEnabled(), 5000);
    QTest::mouseClick(arm, Qt::LeftButton);
    QTRY_VERIFY2_WITH_TIMEOUT(s.last().b(QStringLiteral("arm.armed")), qPrintable(diag(s)), 15000);
    QTest::mouseClick(buttonNamed(page, QStringLiteral("commissionEnableOff")), Qt::LeftButton);
    QTest::mouseClick(buttonNamed(page, QStringLiteral("commissionVspeedZero")), Qt::LeftButton);
    QVERIFY(seq.checks()[Seq::RowKey].first == Seq::Check::Fail); // the service key: a precondition on every transport
    QVERIFY(s.unlockService(QStringLiteral("traction-service")));
    QTRY_VERIFY2_WITH_TIMEOUT(seq.readyToStart(), qPrintable(checksText(seq)), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(start->isEnabled(), 2000);

    // ---- Rs on the locked rotor: SecurityAccess, the start (LK), the results poll as the heartbeat, DONE, the decode
    QVERIFY(seq.start(Seq::RoutineRs, Seq::ATTEST_LOCKED));
    QTRY_VERIFY_WITH_TIMEOUT(!seq.busy(), 10000);
    QVERIFY2(seq.outcome() == Seq::Outcome::Done, qPrintable(seq.text()));
    const Seq::Quantity rsq = seq.quantity(0);
    const double rs = Seq::siValue(0, rsq.value);
    const double plant = s.deviceInfo().value(QLatin1String("motor")).toObject().value(QLatin1String("rs_ohm")).toDouble();
    qInfo("Rs identified %.2f mOhm (raw %u x 10 uOhm) +- %.2f mOhm, verdict %s; the plant's %.2f mOhm (%+.2f %%); "
          "%d heartbeat polls, %lld ms",
          rs * 1e3, static_cast<unsigned>(rsq.value), Seq::siValue(0, rsq.u) * 1e3, qPrintable(Seq::verdictName(rsq.verdict)),
          plant * 1e3, 100.0 * (rs - plant) / plant, seq.polls(), static_cast<long long>(seq.elapsedMs()));
    QVERIFY(plant > 0.0);
    QVERIFY2(std::abs(rs - plant) <= 0.02 * plant, qPrintable(seq.text()));
    QCOMPARE(rsq.verdict, 1); // MC_V_VALID
    QVERIFY(seq.stagedMask() & 0x01); // inside the record's band: staged
    QVERIFY(seq.polls() >= 3);        // the routine outlived several 50 ms polls: they kept it alive
    if (!qEnvironmentVariableIsEmpty("TT_SCREENSHOTS")) {
        QTest::qWait(300);
        w.grab().save(qEnvironmentVariable("TT_SCREENSHOTS") + QStringLiteral("/commissioning-done.png"));
    }

    // ---- the export: JSON through BugReport::save, CSV through LogIo::saveCsv, both read back
    QTemporaryDir tmp;
    QString err;
    const QString csvPath = tmp.filePath(QStringLiteral("commissioning.csv"));
    QVERIFY2(LogIo::saveCsv(csvPath, seq.csvRows(), Seq::csvColumns(), &err), qPrintable(err));
    QVector<TelemetryFrame> rows;
    QVERIFY2(LogIo::load(csvPath, rows, &err), qPrintable(err));
    QCOMPARE(rows.size(), Seq::QTY);
    QCOMPARE(rows[0].str(QStringLiteral("quantity")), QStringLiteral("rs"));
    QCOMPARE(rows[0].str(QStringLiteral("verdict")), QStringLiteral("VALID"));
    QVERIFY(std::abs(rows[0].v(QStringLiteral("value")) - rs) < 1e-9);
    const QString jsonPath = tmp.filePath(QStringLiteral("commissioning.json"));
    QVERIFY2(BugReport::save(jsonPath, seq.toJson(), &err), qPrintable(err));
    QFile jf(jsonPath);
    QVERIFY(jf.open(QIODevice::ReadOnly));
    const QJsonObject j = QJsonDocument::fromJson(jf.readAll()).object();
    QCOMPARE(j.value(QLatin1String("operations")).toArray().at(0).toObject().value(QLatin1String("outcome")).toString(), QStringLiteral("done"));
    QCOMPARE(j.value(QLatin1String("quantities")).toArray().at(0).toObject().value(QLatin1String("verdict")).toString(), QStringLiteral("VALID"));

    // ---- the commit: a fresh unlock, the staged Rs sealed into a new record version for the next key cycle
    QVERIFY(seq.commit());
    QTRY_VERIFY_WITH_TIMEOUT(!seq.busy(), 10000);
    QVERIFY2(seq.outcome() == Seq::Outcome::Committed, qPrintable(seq.text()));
    QCOMPARE(seq.stagedMask(), 0x80); // nothing staged any more, committed in this key cycle

    // ---- the vehicle at 5 km/h: the page shows it, and the firmware refuses a start (NRC 0x22, reason 8)
    s.command(QStringLiteral("vspeed"), {{QStringLiteral("kmh"), 5.0}});
    QTRY_VERIFY_WITH_TIMEOUT(seq.checks()[Seq::RowVspeed].first == Seq::Check::Fail, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!start->isEnabled(), 2000);
    QTest::qWait(50); // a few VCU_CMD frames at 5 km/h reach the firmware
    QVERIFY(seq.start(Seq::RoutineRs, Seq::ATTEST_LOCKED)); // the tool's own interlocks pass: the firmware judges
    QTRY_VERIFY_WITH_TIMEOUT(!seq.busy(), 10000);
    QVERIFY2(seq.outcome() == Seq::Outcome::Refused, qPrintable(seq.text()));
    QCOMPARE(seq.lastNrc(), 0x22);
    QCOMPARE(seq.fwReason(), 8); // MC_R_VEHICLE_SPEED
    QVERIFY2(seq.text().contains(QLatin1String("VEHICLE_SPEED")), qPrintable(seq.text()));
    qInfo("refused as expected: %s", qPrintable(seq.text()));
    QCOMPARE(seq.history().size(), 3);

    // ---- the other routines against the plant, with the firmware host test's tolerances (test_commission.c:217-243)
    const QJsonObject motor = s.deviceInfo().value(QLatin1String("motor")).toObject(); // the plant is the record's motor
    auto run = [&seq](int routine, quint16 attest) {
        return seq.start(routine, attest) && QTest::qWaitFor([&seq] { return !seq.busy(); }, 10000) &&
               seq.outcome() == Seq::Outcome::Done;
    };
    auto dyno = [&s](double rpm) { // the bench's load machine (PROTOCOL.md A.4.4): the test drives it, never the sequencer
        s.command(QStringLiteral("speed"), {{QStringLiteral("rpm"), rpm}});
        return QTest::qWaitFor([&s, rpm] { return std::abs(s.last().v(QStringLiteral("motion.speed_rpm")) - rpm) < 1.0; }, 5000);
    };
    s.command(QStringLiteral("vspeed"), {{QStringLiteral("kmh"), 0.0}});
    QTRY_VERIFY2_WITH_TIMEOUT(seq.readyToStart(), qPrintable(checksText(seq)), 5000);
    QTest::qWait(50); // a few VCU_CMD frames at 0 km/h reach the firmware

    // Ld/Lq on the locked rotor (LK): test_commission.c:242-243, rel < 1 %
    QVERIFY2(run(Seq::RoutineLdq, Seq::ATTEST_LOCKED), qPrintable(seq.text()));
    for (int q : {1, 2}) {
        const double l = Seq::siValue(q, seq.quantity(q).value);
        const double lp = motor.value(QLatin1String(q == 1 ? "ld_h" : "lq_h")).toDouble();
        qInfo("%s identified %.1f uH +- %.1f uH, verdict %s; the plant's %.1f uH (%+.3f %%)", qPrintable(Seq::qtyName(q)), l * 1e6,
              Seq::siValue(q, seq.quantity(q).u) * 1e6, qPrintable(Seq::verdictName(seq.quantity(q).verdict)), lp * 1e6,
              100.0 * (l - lp) / lp);
        QCOMPARE(seq.quantity(q).verdict, 1); // MC_V_VALID
        QVERIFY2(lp > 0.0 && std::abs(l - lp) < 0.01 * lp, qPrintable(seq.text()));
    }

    // psi / zero / direction on the dyno, 300 rpm as test_commission.c's DYNO_RPM: inside MC_CAL_DEFAULT's window
    // (150-450 rpm, <= 0.25 n_x). psi rel < 0.5 % (:236), the zero within 3.5 mrad (:237) of the plant's: 0 — the bridge's
    // plant_theta_e() is pp x the shaft angle its resolver reads (tool/bridge/plant.c:60, bridge.c:379).
    const double psiPlant = motor.value(QLatin1String("psi_wb")).toDouble();
    auto psiZero = [&](const char *what, int verdict) { // both quantities carry the direction verdicts
        const double psi = Seq::siValue(3, seq.quantity(3).value), zero = Seq::siValue(4, seq.quantity(4).value);
        const double dz = std::atan2(std::sin(zero), std::cos(zero)); // reported in [0, 2 pi): its distance from 0
        qInfo("%s: psi %.2f mWb (%+.3f %%), zero %.1f mrad (%+.2f mrad from the plant's), verdicts %s/%s", what, psi * 1e3,
              100.0 * (psi - psiPlant) / psiPlant, zero * 1e3, dz * 1e3, qPrintable(Seq::verdictName(seq.quantity(3).verdict)),
              qPrintable(Seq::verdictName(seq.quantity(4).verdict)));
        return seq.quantity(3).verdict == verdict && seq.quantity(4).verdict == verdict &&
               (verdict != 1 || (std::abs(psi - psiPlant) < 0.005 * psiPlant && std::abs(dz) <= 3.5e-3));
    };
    QVERIFY(dyno(300.0));
    QVERIFY2(run(Seq::RoutinePsiZero, Seq::ATTEST_DYNO_FWD), qPrintable(seq.text()));
    QVERIFY2(psiZero("DF at +300 rpm", 1), qPrintable(seq.text()));
    QVERIFY(dyno(-300.0));
    QVERIFY2(run(Seq::RoutinePsiZero, Seq::ATTEST_DYNO_FWD), qPrintable(seq.text())); // attested against the rotation
    QVERIFY2(psiZero("DF at -300 rpm", 7), qPrintable(seq.text()));                  // MC_V_DIR_DYNO
    QVERIFY2(run(Seq::RoutinePsiZero, Seq::ATTEST_DYNO_REV), qPrintable(seq.text()));
    QVERIFY2(psiZero("DR at -300 rpm", 1), qPrintable(seq.text()));
    QCOMPARE(seq.lastDynoAttest(), Seq::ATTEST_DYNO_REV);

    // ---- Rs again (the commit above cleared its staging), then all five staged; bit 7: committed in this key cycle
    QVERIFY(dyno(0.0));
    QVERIFY2(run(Seq::RoutineRs, Seq::ATTEST_LOCKED), qPrintable(seq.text()));
    QCOMPARE(seq.stagedMask(), 0x9F);
    // DID 0xFD21 (uds_diag.c did_calibration): the active record's layout, the image's, cal_err, motor ID, CRC-32, ...
    auto fd21 = [&s] {
        QByteArray m;
        bool done = false;
        s.command(QStringLiteral("uds"), {{QStringLiteral("hex"), QStringLiteral("03 22 FD 21")}}, [&m, &done](const Ack &a) {
            m = QByteArray::fromHex(a.raw.value(QLatin1String("msg")).toString().toLatin1());
            done = true;
        });
        const bool ok = QTest::qWaitFor([&done] { return done; }, 6000); // the session answers every command by 5 s
        return (ok && m.size() == 24 && m.startsWith(QByteArray::fromHex("62FD21"))) ? m : QByteArray();
    };
    auto be32 = [](const QByteArray &m, int at) { return qFromBigEndian<quint32>(m.constData() + at); };
    const QByteArray recBefore = fd21();
    QVERIFY(!recBefore.isEmpty());
    QVERIFY(seq.commit());
    QTRY_VERIFY_WITH_TIMEOUT(!seq.busy(), 10000);
    QVERIFY2(seq.outcome() == Seq::Outcome::Committed, qPrintable(seq.text()));
    QCOMPARE(seq.stagedMask(), 0x80);

    // ---- the next key cycle (reboot, NVM kept): its init validates the record it runs with
    const int boot = s.deviceInfo().value(QLatin1String("boot")).toInt();
    s.command(QStringLiteral("reboot"));
    QTRY_COMPARE_WITH_TIMEOUT(s.deviceInfo().value(QLatin1String("boot")).toInt(), boot + 1, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(static_cast<int>(s.last().v(QStringLiteral("state.sm"))), 4, 10000); // PRECHARGE_WAIT
    const QByteArray recAfter = fd21();
    QVERIFY(!recAfter.isEmpty());
    qInfo("DID 0xFD21 CRC-32 before the commit 0x%08X, after the reboot 0x%08X; cal_err 0x%X; layout %u (image %u)",
          be32(recBefore, 15), be32(recAfter, 15), be32(recAfter, 7), qFromBigEndian<quint16>(recAfter.constData() + 3),
          qFromBigEndian<quint16>(recAfter.constData() + 5));
    QCOMPARE(be32(recAfter, 7), 0u); // cal_err: calib_check passed (FW-20)
    QCOMPARE(qFromBigEndian<quint16>(recAfter.constData() + 3), qFromBigEndian<quint16>(recAfter.constData() + 5)); // layouts
    QCOMPARE(static_cast<qint64>(be32(recAfter, 11)),
             s.deviceInfo().value(QLatin1String("calib")).toObject().value(QLatin1String("motor_id")).toInteger());
    QVERIFY(be32(recAfter, 15) != be32(recBefore, 15)); // the committed record is the one loaded at the next key cycle (its CRC differs)

    // ---- the heartbeat lapses: armed again, a routine started, then the tool freezes (no poll) past hb_timeout_ms
    QTRY_VERIFY_WITH_TIMEOUT(arm->isEnabled(), 5000);
    QTest::mouseClick(arm, Qt::LeftButton);
    QTRY_VERIFY2_WITH_TIMEOUT(s.last().b(QStringLiteral("arm.armed")), qPrintable(diag(s)), 15000);
    QTest::mouseClick(buttonNamed(page, QStringLiteral("commissionEnableOff")), Qt::LeftButton);
    QTRY_VERIFY2_WITH_TIMEOUT(seq.readyToStart(), qPrintable(checksText(seq)), 5000);
    QCOMPARE(s.last().v(QStringLiteral("sim.time_factor")), 1.0); // the freeze is firmware time too
    // a frozen tool reads nothing: at 100 Hz the ~2.6 kB frames fill the 64 kB pipe in ~240 ms, then the bridge — and
    // the firmware's clock — stall, which leaves the 200 ms lapse ahead by tens of ms only; 5 Hz keeps the bridge running
    // through the freeze, as a real ECU runs on regardless
    s.command(QStringLiteral("rate"), {{QStringLiteral("hz"), 5}});
    QVERIFY(seq.start(Seq::RoutineRs, Seq::ATTEST_LOCKED));
    QTRY_VERIFY_WITH_TIMEOUT(seq.running(), 2000);
    QTest::qSleep(400); // no event processing: no heartbeat leaves the tool for 400 ms (hb_timeout_ms 200)
    QTRY_VERIFY_WITH_TIMEOUT(!seq.busy(), 10000);
    QVERIFY2(seq.outcome() == Seq::Outcome::Aborted, qPrintable(seq.text()));
    QCOMPARE(seq.fwState(), 3);   // MC_ABORTED
    QCOMPARE(seq.fwReason(), 12); // MC_R_HEARTBEAT
    qInfo("aborted as expected: %s", qPrintable(seq.text()));
    auto active = [&s](const QString &name) {
        const auto a = s.dtcs().active();
        return std::any_of(a.begin(), a.end(), [&name](const DtcState &d) { return ProtocolInfo::instance().dtcName(d.id) == name; });
    };
    QTRY_VERIFY_WITH_TIMEOUT(active(QStringLiteral("MC_ABORTED")), 5000);
    w.showPage(5);
    QTRY_VERIFY_WITH_TIMEOUT(tableHas(w.faults()->dtcTable(), QStringLiteral("MC_ABORTED")), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(std::abs(s.last().v(QStringLiteral("plant.torque_nm"))) < 0.5, 5000);
    QCOMPARE(static_cast<int>(s.last().v(QStringLiteral("state.sm"))), 6);     // ARMED_ZERO_TORQUE
    QCOMPARE(static_cast<int>(s.last().v(QStringLiteral("state.bridge"))), 1); // armed idle
    QCOMPARE(static_cast<int>(s.last().v(QStringLiteral("state.pwm"))), 0);    // the PWM off
    QCOMPARE(s.last().v(QStringLiteral("motion.torque_cmd_nm")), 0.0);
    QCOMPARE(seq.stagedMask(), 0); // a new key cycle: nothing staged, nothing committed
    s.closeTransport();
    QVERIFY(!seq.busy());
}

void TstGuiSmoke::mapAndRipple()
{
    using Seq = CommissioningSequencer;
    const QString bridge = BridgeTransport::locate();
    if (bridge.isEmpty()) {
        QSKIP("simulator bridge not built (make -C tool/bridge): the FW-45/FW-46 scenario did not run");
    }
    Theme::apply(*qApp, Theme::Variant::Dark);
    MainWindow w;
    w.resize(1440, 960);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    Session &s = w.session();
    BridgeTransport::Options o;
    o.program = bridge;
    o.rateHz = 100;
    o.timeFactor = 1; // real time: the results poll is the heartbeat
    w.connectSimulator(o);
    QTRY_VERIFY_WITH_TIMEOUT(s.isDeviceLive(), 10000);

    // ---- the root of trust: the host build verifies with the TEST key (hello "root"; DID 0xFD23 over CAN: tst_update)
    const QJsonObject root = s.deviceInfo().value(QLatin1String("root")).toObject();
    QCOMPARE(root.value(QLatin1String("kind")).toString(), QStringLiteral("test"));
    for (int p : {0, 8}) { // the Dashboard, the Firmware page
        QLabel *banner = nullptr;
        for (QLabel *l : w.page(p)->findChildren<QLabel *>()) {
            banner = l->accessibleName() == QLatin1String("rootBanner") ? l : banner;
        }
        QVERIFY(banner && !banner->isHidden());
        QCOMPARE(banner->text().trimmed(), QStringLiteral("TEST root of trust — development unit, not for delivery"));
    }
    qInfo("root of trust: kind %s, key id %s — banner \"TEST root of trust — development unit, not for delivery\"",
          qPrintable(root.value(QLatin1String("kind")).toString()), qPrintable(root.value(QLatin1String("key_id")).toString()));

    w.showPage(3);
    auto *page = qobject_cast<CommissioningPage *>(w.page(3));
    QVERIFY(page);
    Seq &seq = page->sequencer();
    QVERIFY(s.unlockService(QStringLiteral("traction-service")));
    QTRY_COMPARE_WITH_TIMEOUT(static_cast<int>(s.last().v(QStringLiteral("state.sm"))), 4, 10000); // PRECHARGE_WAIT
    const QJsonObject motor = s.deviceInfo().value(QLatin1String("motor")).toObject();
    auto finished = [&seq] { return QTest::qWaitFor([&seq] { return !seq.busy(); }, 20000); };

    // ---- FW-46: the example CSV through the page: 36 points, the mean removed, the i_q that cancels the ripple
    QVERIFY2(page->importRipple(QStringLiteral(TT_SOURCE_DIR "/resources/ripple-example.csv")), qPrintable(page->ripple().error));
    const QVector<qint16> table = page->ripple().table;
    QCOMPARE(table.size(), Seq::RIPPLE_N);
    const double kt = 1.5 * motor.value(QLatin1String("pp")).toInt() * motor.value(QLatin1String("psi_wb")).toDouble();
    QVERIFY(std::abs(page->ripple().nmPerA - kt) < 1e-9);
    // the example: T = 0.5 + 2.0 sin(6θ + 0.3) + 0.8 cos(12θ) N·m, sampled every 5°: at 0° el i_q = −(T − 0.5) / (1.5 pp ψ)
    QVERIFY(std::abs(page->ripple().meanNm - 0.5) < 1e-3);
    QVERIFY(std::abs(table[0] - (-100.0 * (2.0 * std::sin(0.3) + 0.8) / kt)) <= 1.0);
    QVERIFY(std::abs(std::accumulate(table.begin(), table.end(), 0)) <= 18); // the mean removed (± the 0.01 A rounding)
    qInfo("ripple table from the example CSV: %d samples, mean %.3f N·m removed, 1.5·pp·ψ = %.3f N·m/A, peak %.2f A at %.0f° el, "
          "i_q(0°) %.2f A", page->ripple().rows, page->ripple().meanNm, kt, page->ripple().peakA, page->ripple().peakDeg, table[0] / 100.0);
    // the default cal_ripple_ff_max_a (0 A) takes only a zero table: the firmware refuses it (NRC 0x31), nothing is staged
    QVERIFY(seq.writeRipple(table));
    QVERIFY(finished());
    QVERIFY2(seq.outcome() == Seq::Outcome::Refused && seq.lastNrc() == 0x31, qPrintable(seq.text()));
    QCOMPARE(seq.stagedMask() & 0x40, 0);
    qInfo("refused as expected: %s", qPrintable(seq.text()));
    bool set = false;
    s.command(QStringLiteral("param_set"), {{QStringLiteral("name"), QStringLiteral("cal_ripple_ff_max_a")}, {QStringLiteral("value"), 10.0}},
              [&set](const Ack &a) { set = a.ok; });
    QTRY_VERIFY_WITH_TIMEOUT(set, 5000);
    // written (2E FD 46: 75 bytes, a first frame and a consecutive frame) and read back (22 FD 46) equal, staged (bit 6)
    QVERIFY(seq.writeRipple(table));
    QVERIFY(finished());
    QVERIFY2(seq.outcome() == Seq::Outcome::Done, qPrintable(seq.text()));
    QCOMPARE(seq.rippleRead(), table);
    QVERIFY(seq.stagedMask() & 0x40);
    qInfo("ripple: %s", qPrintable(seq.text()));
    QVERIFY(seq.commit());
    QVERIFY(finished());
    QVERIFY2(seq.outcome() == Seq::Outcome::Committed, qPrintable(seq.text()));
    QCOMPARE(seq.stagedMask(), 0x80);
    // the next key cycle: 22 FD 46 reads the active record's table — the one written
    const int boot = s.deviceInfo().value(QLatin1String("boot")).toInt();
    s.command(QStringLiteral("reboot"));
    QTRY_COMPARE_WITH_TIMEOUT(s.deviceInfo().value(QLatin1String("boot")).toInt(), boot + 1, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(static_cast<int>(s.last().v(QStringLiteral("state.sm"))), 4, 10000); // PRECHARGE_WAIT: the record valid
    QVERIFY(seq.readRipple());
    QVERIFY(finished());
    QVERIFY2(seq.outcome() == Seq::Outcome::Done, qPrintable(seq.text()));
    QCOMPARE(seq.rippleRead(), table);
    qInfo("after the reboot: %s", qPrintable(seq.text()));

    // ---- FW-45: the map sweep on the locked rotor, armed through the page
    QPushButton *arm = buttonNamed(page, QStringLiteral("commissionArm"));
    QVERIFY(arm);
    QTRY_VERIFY_WITH_TIMEOUT(arm->isEnabled(), 5000);
    QTest::mouseClick(arm, Qt::LeftButton);
    QTRY_VERIFY2_WITH_TIMEOUT(s.last().b(QStringLiteral("arm.armed")), qPrintable(diag(s)), 15000);
    QTest::mouseClick(buttonNamed(page, QStringLiteral("commissionEnableOff")), Qt::LeftButton);
    QTest::mouseClick(buttonNamed(page, QStringLiteral("commissionVspeedZero")), Qt::LeftButton);
    QTRY_VERIFY2_WITH_TIMEOUT(seq.readyToStart(), qPrintable(checksText(seq)), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(buttonNamed(page, QStringLiteral("commissionMap"))->isEnabled(), 2000);
    QTest::qWait(50); // a few VCU_CMD frames without the enable reach the firmware
    QVERIFY(seq.startMap());
    QVERIFY(QTest::qWaitFor([&seq] { return !seq.busy(); }, 60000));
    QVERIFY2(seq.outcome() == Seq::Outcome::Done, qPrintable(seq.text()));
    qInfo("%s", qPrintable(seq.text()));
    double worst = 0.0;
    for (int ax = 0; ax < 2; ax++) {
        const double plant = motor.value(QLatin1String(ax ? "lq_h" : "ld_h")).toDouble();
        for (int k = 0; k < Seq::MAP_N; k++) {
            const Seq::MapPoint &p = seq.mapPoint(ax, k);
            const double l = p.value * 1e-7, e = (l - plant) / plant; // MC_UNIT_L
            worst = std::max(worst, std::abs(e));
            qInfo("  %s k=%d at %s%.1f A: %.1f uH +- %.2f uH, %s, %s; the plant's %.1f uH (%+.3f %%)", ax ? "Lq" : "Ld", k, ax ? "+" : "-",
                  seq.mapBiasA(k, ax == 0), l * 1e6, p.u * 1e-1, qPrintable(Seq::verdictName(p.verdict)), qPrintable(Seq::flagsText(p.flags)),
                  plant * 1e6, 100.0 * e);
            QCOMPARE(p.verdict, 1); // MC_V_VALID
            QVERIFY2(plant > 0.0 && std::abs(e) <= 0.05, qPrintable(QStringLiteral("%1 point %2").arg(ax).arg(k)));
            QVERIFY(p.flags & 2); // staged: inside the band of the record's (flat) map
        }
    }
    QCOMPARE(seq.mapStaged(0), 0x3F);
    QCOMPARE(seq.mapStaged(1), 0x3F);
    QVERIFY(seq.stagedMask() & 0x20);
    qInfo("the twelve points within %.3f %% of the plant's inductance", 100.0 * worst);
    if (!qEnvironmentVariableIsEmpty("TT_SCREENSHOTS")) {
        QTest::qWait(300);
        w.grab().save(qEnvironmentVariable("TT_SCREENSHOTS") + QStringLiteral("/commissioning-map.png"));
        if (auto *area = page->findChild<QScrollArea *>()) { // the whole page, the map and ripple cards below the fold
            area->widget()->grab().save(qEnvironmentVariable("TT_SCREENSHOTS") + QStringLiteral("/commissioning-map-page.png"));
        }
    }
    // ---- the commit: each staged axis becomes its apparent-inductance map (commission.c commit(): trapezoids over the
    //      differential points), which calib_check accepts rising by no more than MOTOR_MAP_RISE_TOL (2 %) between
    //      neighbouring points (calib.c map_ok): this plant's 0.1 % scatter passes
    double staged[2][Seq::MAP_N]; // what was staged (0.1 µH, as read): the next key cycle's init clears the tool's copy
    for (int ax = 0; ax < 2; ax++) {
        for (int k = 0; k < Seq::MAP_N; k++) {
            staged[ax][k] = seq.mapPoint(ax, k).value * 1e-7;
        }
    }
    QVERIFY(seq.commit());
    QVERIFY(finished());
    qInfo("commit of the swept maps: %s", qPrintable(seq.text()));
    QVERIFY2(seq.outcome() == Seq::Outcome::Committed, qPrintable(seq.text()));
    QCOMPARE(seq.stagedMask(), 0x80); // nothing staged any more (bit 5 gone), committed in this key cycle
    QCOMPARE(seq.mapStaged(0), 0);
    QCOMPARE(seq.mapStaged(1), 0);

    // ---- the next key cycle runs the committed record: its maps read back (DID 0xFD26: 6 x L_d, 6 x L_q in nH BE32, i_map
    //      in 0.1 A BE16), each point the staged one within 0.2 % — exactly, the firmware's construction from the staged
    //      points (trapezoids; L_0 = D_0) within the 0.1 µH they were read with — and i_map the SKU's current limit
    const int boot2 = s.deviceInfo().value(QLatin1String("boot")).toInt();
    s.command(QStringLiteral("reboot"));
    QTRY_COMPARE_WITH_TIMEOUT(s.deviceInfo().value(QLatin1String("boot")).toInt(), boot2 + 1, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(static_cast<int>(s.last().v(QStringLiteral("state.sm"))), 4, 10000); // PRECHARGE_WAIT: the record valid
    QByteArray m;
    bool done = false;
    s.command(QStringLiteral("uds"), {{QStringLiteral("msg"), QStringLiteral("22 FD 26")}}, [&m, &done](const Ack &a) {
        m = QByteArray::fromHex(a.raw.value(QLatin1String("msg")).toString().toLatin1());
        done = true;
    });
    QVERIFY(QTest::qWaitFor([&done] { return done; }, 6000)); // the session answers every command by 5 s
    QVERIFY2(m.size() == 53 && m.startsWith(QByteArray::fromHex("62FD26")), m.toHex(' ').constData());
    const auto *d = reinterpret_cast<const uchar *>(m.constData()) + 3;
    const double iMap = qFromBigEndian<quint16>(d + 48) * 0.1;
    const double crest = std::sqrt(2.0) * s.deviceInfo().value(QLatin1String("limits")).toObject().value(QLatin1String("i_pk_rms_a")).toDouble();
    qInfo("DID 0xFD26 i_map %.1f A; the SKU's limit sqrt(2) x i_pk_rms_a (hello.limits) %.3f A", iMap, crest);
    QVERIFY(std::abs(iMap - crest) <= 0.05 + 1e-9); // 0.1 A resolution
    for (int ax = 0; ax < 2; ax++) {
        QStringList line;
        double lam = 0.0, worstStaged = 0.0, worstBuilt = 0.0;
        for (int k = 0; k < Seq::MAP_N; k++) {
            const double rec = qFromBigEndian<quint32>(d + 24 * ax + 4 * k) * 1e-9;
            lam += k ? 0.5 * (staged[ax][k - 1] + staged[ax][k]) : 0.0;
            const double built = k ? lam / k : staged[ax][0]; // commission.c commit(): L_k = λ_k / k, L_0 = D_0
            worstStaged = std::max(worstStaged, std::abs(rec - staged[ax][k]) / staged[ax][k]);
            worstBuilt = std::max(worstBuilt, std::abs(rec - built));
            line << QStringLiteral("%1 (staged %2)").arg(rec * 1e6, 0, 'f', 3).arg(staged[ax][k] * 1e6, 0, 'f', 1);
            QVERIFY2(std::abs(rec - staged[ax][k]) <= 0.002 * staged[ax][k], qPrintable(QStringLiteral("%1 point %2").arg(ax).arg(k)));
            QVERIFY2(std::abs(rec - built) <= 0.06e-6, qPrintable(QStringLiteral("%1 point %2").arg(ax).arg(k)));
        }
        qInfo("the next key cycle's %s map (DID 0xFD26), uH: %s; within %.3f %% of the staged points, %.4f uH of their trapezoids",
              ax ? "Lq" : "Ld", qPrintable(line.join(QStringLiteral(", "))), 100.0 * worstStaged, worstBuilt * 1e6);
    }
    // the page's Record column: the Session reads DID 0xFD26 itself at the power-up (both transports)
    QTRY_VERIFY_WITH_TIMEOUT(s.deviceInfo().value(QLatin1String("maps")).toObject().value(QLatin1String("ld_h")).toArray().size() == Seq::MAP_N, 5000);
    for (int ax = 0; ax < 2; ax++) {
        const QJsonArray a = s.deviceInfo().value(QLatin1String("maps")).toObject().value(QLatin1String(ax ? "lq_h" : "ld_h")).toArray();
        auto *t = page->findChild<QTableWidget *>(ax ? QStringLiteral("commissionMapQ") : QStringLiteral("commissionMapD"));
        QVERIFY(t);
        for (int k = 0; k < Seq::MAP_N; k++) {
            QCOMPARE(qRound64(a[k].toDouble() * 1e9), static_cast<qint64>(qFromBigEndian<quint32>(d + 24 * ax + 4 * k)));
            QTRY_COMPARE(t->item(k, 3)->text(), QStringLiteral("%1 µH").arg(a[k].toDouble() * 1e6, 0, 'f', 2));
        }
    }
    if (!qEnvironmentVariableIsEmpty("TT_SCREENSHOTS")) {
        QTest::qWait(300);
        if (auto *area = page->findChild<QScrollArea *>()) {
            area->widget()->grab().save(qEnvironmentVariable("TT_SCREENSHOTS") + QStringLiteral("/commissioning-map-record.png"));
        }
    }
    s.closeTransport();
}

QTEST_MAIN(TstGuiSmoke)
#include "tst_gui_smoke.moc"
