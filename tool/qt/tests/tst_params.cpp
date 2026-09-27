// The calibration model against tool/protocol/params.json: every row, validation (range, type and every
// cross_field_rule of ti_params_validate(), evaluated by ProtocolInfo::evalRule), the device table, dirty state,
// writes, import/export and the change log; and the DTC catalogue of dtcs.json.
#include "ParamModel.h"
#include "ProtocolInfo.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QtTest>

class TstParams : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void exportsLoaded();
    void defaultsAreValid();
    void ruleEvaluator();
    void rangeAndTypeChecks();
    void crossFieldRules();
    void deviceTableAndWrites();
    void importExport();
    void dtcCatalogue();
};

namespace {
QJsonObject resource(const char *name)
{
    QFile f(QStringLiteral(":/protocol/") + QLatin1String(name));
    return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject();
}
} // namespace

void TstParams::exportsLoaded()
{
    const ProtocolInfo &pi = ProtocolInfo::instance();
    // every row of the export loads (the counts follow the firmware: nothing here is hard-coded)
    QCOMPARE(pi.cal.size(), resource("params.json").value(QLatin1String("parameters")).toArray().size());
    QVERIFY(!pi.cal.isEmpty());
    QCOMPARE(pi.dtcs.size() - 1, resource("dtcs.json").value(QLatin1String("dtcs")).toArray().size());
    QVERIFY(pi.fwId.startsWith(QLatin1String("0x")));
    QCOMPARE(pi.skus.size(), 4);
    QVERIFY(pi.rules.size() >= 18);
    QCOMPARE(pi.states.value(7), QStringLiteral("RUN"));
    QCOMPARE(pi.evidenceBits.size(), 5);
    for (const CalRow &c : pi.cal) {
        QVERIFY2(c.min <= c.def && c.def <= c.max, qPrintable(c.name));
        QVERIFY2(!c.group.isEmpty(), qPrintable(c.name));
        QVERIFY2(c.name.startsWith(QLatin1String("cal_")), qPrintable(c.name));
    }
}

void TstParams::defaultsAreValid()
{
    ParamModel m;
    for (const SkuInfo &s : ProtocolInfo::instance().skus) {
        m.setSku(s.key);
        QVERIFY2(m.validate().isEmpty(), qPrintable(s.key + QStringLiteral(": ") + m.validate().join(QLatin1Char('\n'))));
    }
    QVERIFY(m.pendingWrites().isEmpty());
}

void TstParams::ruleEvaluator()
{
    const auto lookup = [](const QString &n) {
        if (n == QLatin1String("a")) return 2.0;
        if (n == QLatin1String("b")) return 3.0;
        if (n == QLatin1String("c_min_f")) return 1e-4;
        return std::nan("");
    };
    bool ok = false;
    QVERIFY(ProtocolInfo::evalRule(QStringLiteral("a < b"), lookup, &ok) && ok);
    QVERIFY(!ProtocolInfo::evalRule(QStringLiteral("a >= b"), lookup, &ok) && ok);
    QVERIFY(ProtocolInfo::evalRule(QStringLiteral("((a * b) / 2) <= 3"), lookup, &ok) && ok);
    QVERIFY(ProtocolInfo::evalRule(QStringLiteral("a + b * 2 == 8"), lookup, &ok) && ok); // precedence
    QVERIFY(ProtocolInfo::evalRule(QStringLiteral("(a >= 1) && (a <= 2)"), lookup, &ok) && ok);
    QVERIFY(ProtocolInfo::evalRule(QStringLiteral("a == 2u || b == 0.5f"), lookup, &ok) && ok); // C suffixes
    QVERIFY(ProtocolInfo::evalRule(QStringLiteral("-a < 0"), lookup, &ok) && ok);
    QVERIFY(ProtocolInfo::evalRule(QStringLiteral("c_min_f > 0"), lookup, &ok) && ok);
    ProtocolInfo::evalRule(QStringLiteral("a < unknown_x"), lookup, &ok);
    QVERIFY(!ok); // an unknown identifier is never "true"
    ProtocolInfo::evalRule(QStringLiteral("(a < b"), lookup, &ok);
    QVERIFY(!ok);
    ProtocolInfo::evalRule(QStringLiteral("a < b junk"), lookup, &ok);
    QVERIFY(!ok);
}

void TstParams::rangeAndTypeChecks()
{
    ParamModel m;
    QVERIFY(m.setEdited(QStringLiteral("cal_vdc_disagree_floor_v"), 41.0)); // max 40
    QStringList p = m.validate();
    QCOMPARE(p.size(), 1);
    QVERIFY(p[0].contains(QLatin1String("cal_vdc_disagree_floor_v")));
    m.setEdited(QStringLiteral("cal_vdc_disagree_floor_v"), 40.0);
    QVERIFY(m.validate().isEmpty());
    m.setEdited(QStringLiteral("cal_isum_debounce"), 2.5); // u8: whole numbers only
    p = m.validate();
    QCOMPARE(p.size(), 1);
    QVERIFY(p[0].contains(QLatin1String("whole number")));
    m.setEdited(QStringLiteral("cal_isum_debounce"), std::nan(""));
    QVERIFY(m.validate().join(QString()).contains(QLatin1String("finite")));
    QVERIFY(!m.setEdited(QStringLiteral("cal_does_not_exist"), 1.0));
}

void TstParams::crossFieldRules()
{
    ParamModel m;
    m.setEdited(QStringLiteral("cal_ign_off_v"), 6.0); // == cal_ign_on_v (6): ign_off < ign_on fails
    QStringList p = m.validate();
    QCOMPARE(p.size(), 1);
    QVERIFY(p[0].contains(QLatin1String("cal_ign_off_v < cal_ign_on_v")));
    m.revertParam(QStringLiteral("cal_ign_off_v"));
    // FW-30: the winding plane — a 6.5 V pp monitor setpoint leaves the cold winding below its 6.5 V pp floor
    m.setEdited(QStringLiteral("cal_rslv_exc_target_vpp"), 6.5);
    p = m.validate();
    QCOMPARE(p.size(), 1);
    QVERIFY(p[0].contains(QLatin1String("rslv_floor_vpp")));
    // and 8.3 V pp is beyond the amplifier's slew ceiling and the SWG's low corner
    m.setEdited(QStringLiteral("cal_rslv_exc_target_vpp"), 8.3);
    p = m.validate();
    QCOMPARE(p.size(), 2);
    m.revertAll();
    QVERIFY(m.validate().isEmpty());
}

void TstParams::deviceTableAndWrites()
{
    ParamModel m;
    QSignalSpy dirty(&m, &ParamModel::dirtyChanged);
    QJsonArray dev;
    for (const CalRow &c : ProtocolInfo::instance().cal) {
        dev.append(QJsonObject{{QStringLiteral("name"), c.name}, {QStringLiteral("type"), c.type},
                               {QStringLiteral("min"), c.min}, {QStringLiteral("max"), c.max},
                               {QStringLiteral("value"), c.def}, {QStringLiteral("default"), c.def}});
    }
    dev[0] = QJsonObject{{QStringLiteral("name"), QStringLiteral("cal_vdc_disagree_floor_v")},
                         {QStringLiteral("value"), 20.0}, {QStringLiteral("min"), 5.0}, {QStringLiteral("max"), 40.0}};
    QVERIFY(m.setDeviceTable(dev).isEmpty());
    QVERIFY(m.haveDevice());
    const int i = m.indexOf(QStringLiteral("cal_vdc_disagree_floor_v"));
    QCOMPARE(m.effective(i), 20.0); // the device's value, not the default
    QVERIFY(!m.isDirty(i));
    m.setEdited(QStringLiteral("cal_vdc_disagree_floor_v"), 25.0);
    QVERIFY(m.isDirty(i));
    QCOMPARE(m.pendingWrites().size(), 1);
    QCOMPARE(m.pendingWrites()[0].second, 25.0);
    m.writeResult(QStringLiteral("cal_vdc_disagree_floor_v"), false, 20.0, QStringLiteral("refused"));
    QVERIFY(m.isDirty(i)); // still pending: the device kept its value
    m.writeResult(QStringLiteral("cal_vdc_disagree_floor_v"), true, 25.0, QString());
    QVERIFY(!m.isDirty(i));
    QCOMPARE(m.device(i), 25.0);
    QVERIFY(m.pendingWrites().isEmpty());
    QVERIFY(!dirty.isEmpty());
    const QJsonArray log = m.changeLogJson();
    QCOMPARE(log.last().toObject().value(QLatin1String("result")).toString(), QStringLiteral("written"));
    // a device with another range is reported (a bridge built from other firmware)
    QJsonArray other{QJsonObject{{QStringLiteral("name"), QStringLiteral("cal_ign_on_v")}, {QStringLiteral("value"), 6.0},
                                 {QStringLiteral("min"), 3.0}, {QStringLiteral("max"), 9.0}}};
    QCOMPARE(m.setDeviceTable(other).size(), 1);
}

void TstParams::importExport()
{
    ParamModel a;
    a.setEdited(QStringLiteral("cal_torque_max_nm"), 300.0);
    a.setEdited(QStringLiteral("cal_can_ctr_max_jump"), 1.0);
    const QJsonObject file = a.exportJson(ProtocolInfo::instance().fwId, QStringLiteral("8xx_sic"));
    ParamModel b;
    QStringList p = b.importJson(file);
    QVERIFY2(p.isEmpty(), qPrintable(p.join(QLatin1Char('\n'))));
    QCOMPARE(b.effective(b.indexOf(QStringLiteral("cal_torque_max_nm"))), 300.0);
    QCOMPARE(b.effective(b.indexOf(QStringLiteral("cal_can_ctr_max_jump"))), 1.0);
    QCOMPARE(b.pendingWrites().size(), 2);
    // a bad file is refused whole; an out-of-range value imports but blocks the write
    QVERIFY(!b.importJson(QJsonObject{{QStringLiteral("format"), QStringLiteral("x")}}).isEmpty());
    QJsonObject bad = file;
    QJsonObject vals = bad.value(QLatin1String("params")).toObject();
    vals.insert(QStringLiteral("cal_torque_max_nm"), 5000.0);
    vals.insert(QStringLiteral("cal_nonexistent"), 1.0);
    bad.insert(QStringLiteral("params"), vals);
    p = b.importJson(bad);
    QCOMPARE(p.size(), 2); // the unknown row and the range violation
}

void TstParams::dtcCatalogue()
{
    const ProtocolInfo &pi = ProtocolInfo::instance();
    const QStringList classes = resource("dtcs.json").value(QLatin1String("classes")).toObject().keys(); // incl. "unknown"
    QVERIFY(classes.contains(QStringLiteral("fault")));
    QVERIFY(pi.dtcs.size() > 70);
    for (int id = 1; id < pi.dtcs.size(); id++) {
        const DtcInfo *d = pi.dtc(id);
        QVERIFY(d);
        QCOMPARE(d->id, id);
        QCOMPARE(d->code, QStringLiteral("0x%1").arg(0xD10000 | id, 6, 16, QLatin1Char('0')).toUpper().replace(QLatin1String("0X"), QLatin1String("0x")));
        QVERIFY2(!d->description.isEmpty() && !d->response.isEmpty(), qPrintable(d->name));
        QVERIFY2(classes.contains(d->cls), qPrintable(d->name));
    }
    QCOMPARE(pi.dtcName(17), QStringLiteral("DESAT_HS"));
}

QTEST_MAIN(TstParams)
#include "tst_params.moc"
