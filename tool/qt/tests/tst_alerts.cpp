// The alert engine: raise after the hold time, clear with hysteresis, missing/NaN readings change nothing, every
// operator, persistence as JSON; and the DTC tracker's raise/clear/occurrence events.
#include "AlertEngine.h"
#include "DtcTracker.h"

#include <QtTest>

namespace {
TelemetryFrame fr(double t, const char *ch, double v)
{
    TelemetryFrame f;
    f.tMs = t;
    f.num.insert(QLatin1String(ch), v);
    return f;
}
} // namespace

class TstAlerts : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void raiseAfterHoldClearWithHysteresis();
    void missingValuesChangeNothing();
    void operators();
    void persistence();
    void disabledRulesSleep();
    void dtcTracker();
};

void TstAlerts::raiseAfterHoldClearWithHysteresis()
{
    AlertEngine e;
    AlertRule r;
    r.id = QStringLiteral("hot");
    r.channel = QStringLiteral("temps.mod_max_c");
    r.op = AlertRule::Op::Gt;
    r.threshold = 90.0;
    r.hysteresis = 5.0;
    r.holdMs = 100.0;
    r.severity = QStringLiteral("crit");
    e.addRule(r);
    QVERIFY(e.evaluate(fr(0, "temps.mod_max_c", 91)).isEmpty());   // pending
    QVERIFY(e.evaluate(fr(50, "temps.mod_max_c", 92)).isEmpty());  // still inside the hold
    const auto up = e.evaluate(fr(100, "temps.mod_max_c", 93));
    QCOMPARE(up.size(), 1);
    QVERIFY(up[0].raised);
    QCOMPARE(up[0].severity, QStringLiteral("crit"));
    QVERIFY(e.isActive(QStringLiteral("hot")));
    QVERIFY(e.evaluate(fr(150, "temps.mod_max_c", 88)).isEmpty()); // inside the hysteresis band: still active
    const auto down = e.evaluate(fr(200, "temps.mod_max_c", 84));
    QCOMPARE(down.size(), 1);
    QVERIFY(!down[0].raised);
    QVERIFY(!e.isActive(QStringLiteral("hot")));
    // a dip below the threshold restarts the hold
    QVERIFY(e.evaluate(fr(300, "temps.mod_max_c", 95)).isEmpty());
    QVERIFY(e.evaluate(fr(350, "temps.mod_max_c", 80)).isEmpty());
    QVERIFY(e.evaluate(fr(420, "temps.mod_max_c", 95)).isEmpty());
    QCOMPARE(e.evaluate(fr(520, "temps.mod_max_c", 95)).size(), 1);
    QCOMPARE(e.log().size(), 3);
}

void TstAlerts::missingValuesChangeNothing()
{
    AlertEngine e;
    AlertRule r;
    r.id = QStringLiteral("v");
    r.channel = QStringLiteral("link.vdc_v");
    r.op = AlertRule::Op::Lt;
    r.threshold = 500.0;
    e.addRule(r);
    QCOMPARE(e.evaluate(fr(0, "link.vdc_v", 100)).size(), 1);
    QVERIFY(e.evaluate(fr(10, "other", 1)).isEmpty());                 // channel absent
    QVERIFY(e.evaluate(fr(20, "link.vdc_v", std::nan(""))).isEmpty()); // invalid reading
    QVERIFY(e.isActive(QStringLiteral("v")));                          // never cleared by a missing value
}

void TstAlerts::operators()
{
    AlertRule r;
    r.threshold = 10.0;
    r.op = AlertRule::Op::Ge;
    QVERIFY(r.holds(10.0) && !r.holds(9.9));
    r.op = AlertRule::Op::Le;
    QVERIFY(r.holds(10.0) && !r.holds(10.1));
    r.op = AlertRule::Op::Eq;
    QVERIFY(r.holds(10.0) && !r.holds(10.5));
    r.op = AlertRule::Op::Ne;
    QVERIFY(!r.holds(10.0) && r.holds(3.0));
    r.op = AlertRule::Op::AbsGt;
    QVERIFY(r.holds(-11.0) && r.holds(11.0) && !r.holds(-9.0));
    for (AlertRule::Op op : {AlertRule::Op::Gt, AlertRule::Op::Ge, AlertRule::Op::Lt, AlertRule::Op::Le,
                             AlertRule::Op::Eq, AlertRule::Op::Ne, AlertRule::Op::AbsGt}) {
        QCOMPARE(AlertRule::opFromText(AlertRule::opText(op)), op);
    }
}

void TstAlerts::persistence()
{
    AlertEngine a;
    AlertRule r;
    r.channel = QStringLiteral("wd.isr_age_us");
    r.op = AlertRule::Op::Gt;
    r.threshold = 200;
    r.holdMs = 5;
    r.note = QStringLiteral("FW-31 liveness");
    a.addRule(r);
    AlertEngine b;
    b.fromJson(a.toJson());
    QCOMPARE(b.rules().size(), 1);
    QCOMPARE(b.rules()[0].channel, r.channel);
    QCOMPARE(b.rules()[0].threshold, 200.0);
    QCOMPARE(b.rules()[0].holdMs, 5.0);
    QCOMPARE(b.rules()[0].note, r.note);
    QCOMPARE(b.rules()[0].id, a.rules()[0].id);
    b.removeRule(b.rules()[0].id);
    QVERIFY(b.rules().isEmpty());
}

void TstAlerts::disabledRulesSleep()
{
    AlertEngine e;
    AlertRule r;
    r.channel = QStringLiteral("x");
    r.threshold = 0;
    r.enabled = false;
    e.addRule(r);
    QVERIFY(e.evaluate(fr(0, "x", 5)).isEmpty());
}

void TstAlerts::dtcTracker()
{
    DtcTracker t;
    TelemetryFrame f;
    f.dtcListComplete = true;
    f.tMs = 1;
    f.dtcs = {{17, 0x2F, 1, 100, 100}};
    auto ev = t.update(f);
    QCOMPARE(ev.size(), 1);
    QCOMPARE(ev[0].kind, DtcEvent::Kind::Raised);
    QCOMPARE(t.active().size(), 1);
    QCOMPARE(t.confirmedCount(), 1);
    f.tMs = 2;
    f.dtcs = {{17, 0x2F, 2, 100, 200}}; // failed again between frames
    ev = t.update(f);
    QCOMPARE(ev.size(), 1);
    QCOMPARE(ev[0].kind, DtcEvent::Kind::Occurrence);
    f.tMs = 3;
    f.dtcs = {{17, 0x28, 2, 100, 200}}; // passed: testFailed cleared, confirmed kept
    ev = t.update(f);
    QCOMPARE(ev.size(), 1);
    QCOMPARE(ev[0].kind, DtcEvent::Kind::Cleared);
    QVERIFY(t.active().isEmpty());
    f.tMs = 4;
    f.dtcs.clear(); // dtc_clear
    QVERIFY(t.update(f).isEmpty());
    QCOMPARE(t.history().size(), 3);
    // a frame that says nothing about DTCs changes nothing
    TelemetryFrame quiet;
    quiet.num.insert(QStringLiteral("x"), 1);
    QVERIFY(t.update(quiet).isEmpty());
    // CAN summary: the first active DTC and the confirmed count
    DtcTracker c;
    TelemetryFrame s;
    s.num.insert(QStringLiteral("inv.n_dtc"), 2);
    s.dtcs = {{40, 0x09, 1, 0, 0}};
    QCOMPARE(c.update(s).size(), 1);
    QCOMPARE(c.confirmedCount(), 2);
    QVERIFY(!c.complete());
    s.dtcs = {{17, 0x09, 1, 0, 0}};
    QCOMPARE(c.update(s).size(), 2); // 40 no longer first (cleared from the summary), 17 raised
}

QTEST_MAIN(TstAlerts)
#include "tst_alerts.moc"
