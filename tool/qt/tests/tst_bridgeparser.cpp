// The bridge's JSON-lines protocol (tool/PROTOCOL.md Part A): the line parser, the flat command encoder, acks,
// telemetry flattening (nested objects, null, the DTC array, the INV_STATUS hex), and — when the simulator bridge
// is built — a live check that every field this application reads is present in real telemetry.
#include "BridgeProtocol.h"
#include "BridgeTransport.h"
#include "TerminalPage.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QtTest>

class TstBridgeParser : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void parsesTypes();
    void rejectsGarbage();
    void encodesFlatCommands();
    void acks();
    void telemetryFlattening();
    void terminalShorthand();
    void liveBridgeFields();
};

void TstBridgeParser::parsesTypes()
{
    using BridgeProtocol::Type;
    QCOMPARE(BridgeProtocol::parse(R"({"type":"tel","v":1,"t_ms":1002.02})").type, Type::Tel);
    QCOMPARE(BridgeProtocol::parse(R"({"type":"hello","fw_id":"0x0A0F0014"})").type, Type::Hello);
    QCOMPARE(BridgeProtocol::parse(R"({"type":"ack","id":1,"cmd":"arm","ok":true})").type, Type::Ack);
    QCOMPARE(BridgeProtocol::parse(R"({"type":"log","level":"info","msg":"power-up 1"})").type, Type::Log);
    QCOMPARE(BridgeProtocol::parse(R"({"type":"params","params":[]})").type, Type::Params);
    QCOMPARE(BridgeProtocol::parse(R"({"type":"can","bus":0,"id":513,"len":20,"hex":"00"})").type, Type::Can);
    QCOMPARE(BridgeProtocol::parse(R"({"type":"something-new"})").type, Type::Other); // the protocol grows by addition
    QCOMPARE(BridgeProtocol::parse("  {\"type\":\"tel\"}\r\n").type, Type::Tel);
}

void TstBridgeParser::rejectsGarbage()
{
    QCOMPARE(BridgeProtocol::parse("").type, BridgeProtocol::Type::Invalid);
    QCOMPARE(BridgeProtocol::parse("not json").type, BridgeProtocol::Type::Invalid);
    QCOMPARE(BridgeProtocol::parse("[1,2,3]").type, BridgeProtocol::Type::Invalid);
    QCOMPARE(BridgeProtocol::parse(R"({"type":"tel",)").type, BridgeProtocol::Type::Invalid); // truncated line
}

void TstBridgeParser::encodesFlatCommands()
{
    QString err;
    const QByteArray line = BridgeProtocol::encodeCommand(
        QStringLiteral("torque"), {{QStringLiteral("nm"), 120}, {QStringLiteral("hold_ms"), 200}}, 7, &err);
    QVERIFY(err.isEmpty());
    QVERIFY(!line.contains('\n'));
    const QJsonObject o = QJsonDocument::fromJson(line).object();
    QCOMPARE(o.value(QLatin1String("cmd")).toString(), QStringLiteral("torque"));
    QCOMPARE(o.value(QLatin1String("id")).toInt(), 7);
    QCOMPARE(o.value(QLatin1String("nm")).toDouble(), 120.0);
    QCOMPARE(o.value(QLatin1String("hold_ms")).toDouble(), 200.0);
    // the bridge reads flat objects only
    QVERIFY(BridgeProtocol::encodeCommand(QStringLiteral("x"), {{QStringLiteral("a"), QJsonObject{}}}, 1, &err).isEmpty());
    QVERIFY(err.contains(QLatin1String("flat")));
    QVERIFY(BridgeProtocol::encodeCommand(QString(), {}, 1, &err).isEmpty());
    QJsonObject many;
    for (int i = 0; i < 23; i++) {
        many.insert(QStringLiteral("k%1").arg(i), i);
    }
    QVERIFY(BridgeProtocol::encodeCommand(QStringLiteral("x"), many, 1, &err).isEmpty()); // 23 + cmd + id = 25 keys
    QVERIFY(BridgeProtocol::encodeCommand(QStringLiteral("x"), {{QStringLiteral("s"), QString(5000, QLatin1Char('a'))}}, 1, &err).isEmpty());
}

void TstBridgeParser::acks()
{
    const auto ok = BridgeProtocol::toAck(
        BridgeProtocol::parse(R"({"type":"ack","id":5,"cmd":"param_set","name":"cal_ign_off_v","ok":false,"violations":1,"err":"ti_params_validate() refused the set","value":4})").obj);
    QCOMPARE(ok.id, quint64(5));
    QCOMPARE(ok.cmd, QStringLiteral("param_set"));
    QVERIFY(!ok.ok);
    QVERIFY(ok.message.contains(QLatin1String("refused")));
    QCOMPARE(ok.raw.value(QLatin1String("value")).toDouble(), 4.0);
    const auto uds = BridgeProtocol::toAck(
        BridgeProtocol::parse(R"({"type":"ack","id":9,"cmd":"uds","ok":true,"rsp":"03 7F 27 22 AA AA AA AA","rsp_id":2025})").obj);
    QVERIFY(uds.ok);
    QCOMPARE(uds.message, QStringLiteral("03 7F 27 22 AA AA AA AA"));
}

void TstBridgeParser::telemetryFlattening()
{
    // the "can" field carries the first INV_STATUS vector of can-frames.json (RUN, 150 N·m applied, 152.3 commanded)
    const QByteArray line =
        R"({"type":"tel","v":1,"seq":3,"t_ms":2410.5,"src":"sim","state":{"sm":7,"gate_en":true},)"
        R"("motion":{"speed_rpm":2999.98,"gear":"D"},"temps":{"coolant_c":null},"plant":{"inject":["overtemp","can_loss"]},)"
        R"("dtc":[[17,47,1,4431,4431],[40,40,2,100,900]],"can":"9A13078ADC05B80B3E1D5F0128000000F3050000"})";
    const BridgeProtocol::Message m = BridgeProtocol::parse(line);
    QCOMPARE(m.type, BridgeProtocol::Type::Tel);
    const TelemetryFrame f = BridgeProtocol::toFrame(m.obj);
    QCOMPARE(f.tMs, 2410.5);
    QCOMPARE(f.source, QStringLiteral("sim"));
    QCOMPARE(f.v(QStringLiteral("state.sm")), 7.0);
    QVERIFY(f.b(QStringLiteral("state.gate_en")));
    QCOMPARE(f.str(QStringLiteral("motion.gear")), QStringLiteral("D"));
    QVERIFY(f.has(QStringLiteral("temps.coolant_c")) && std::isnan(f.v(QStringLiteral("temps.coolant_c"))));
    QCOMPARE(f.str(QStringLiteral("plant.inject")), QStringLiteral("overtemp,can_loss"));
    QVERIFY(f.dtcListComplete);
    QCOMPARE(f.dtcs.size(), 2);
    QCOMPARE(f.dtcs[0].id, 17);
    QVERIFY(f.dtcs[0].active());  // 47 = 0x2F: testFailed set
    QVERIFY(!f.dtcs[1].active()); // 40 = 0x28: confirmed, not failing now
    QCOMPARE(f.v(QStringLiteral("inv.torque_applied_nm")), 150.0);
    QVERIFY(std::abs(f.v(QStringLiteral("inv.torque_cmd_nm")) - 152.3) < 1e-4);
    QCOMPARE(f.v(QStringLiteral("inv.state")), 7.0);
    // a corrupted status hex adds nothing
    QJsonObject o = m.obj;
    o.insert(QLatin1String("can"), QStringLiteral("9B13078ADC05B80B3E1D5F0128000000F3050000"));
    QVERIFY(!BridgeProtocol::toFrame(o).has(QStringLiteral("inv.state")));
}

void TstBridgeParser::terminalShorthand()
{
    QString cmd, err;
    QJsonObject a;
    QVERIFY(TerminalPage::parseLine(QStringLiteral("torque 50 hold_ms=500"), &cmd, &a, &err));
    QCOMPARE(cmd, QStringLiteral("torque"));
    QCOMPARE(a.value(QLatin1String("nm")).toDouble(), 50.0);
    QCOMPARE(a.value(QLatin1String("hold_ms")).toDouble(), 500.0);
    QVERIFY(TerminalPage::parseLine(QStringLiteral("uds 02 10 03"), &cmd, &a, &err));
    QCOMPARE(a.value(QLatin1String("hex")).toString(), QStringLiteral("02 10 03"));
    QVERIFY(TerminalPage::parseLine(QStringLiteral("inject desat_hs"), &cmd, &a, &err));
    QCOMPARE(a.value(QLatin1String("fault")).toString(), QStringLiteral("desat_hs"));
    QVERIFY(TerminalPage::parseLine(QStringLiteral("gear D"), &cmd, &a, &err));
    QCOMPARE(a.value(QLatin1String("gear")).toString(), QStringLiteral("D"));
    QVERIFY(TerminalPage::parseLine(QStringLiteral("pause on"), &cmd, &a, &err));
    QCOMPARE(a.value(QLatin1String("on")).toBool(), true);
    QVERIFY(TerminalPage::parseLine(QStringLiteral("param_set cal_torque_max_nm 300"), &cmd, &a, &err));
    QCOMPARE(a.value(QLatin1String("name")).toString(), QStringLiteral("cal_torque_max_nm"));
    QCOMPARE(a.value(QLatin1String("value")).toDouble(), 300.0);
    QVERIFY(TerminalPage::parseLine(QStringLiteral("can_rx 0x101 7050E803035A0000"), &cmd, &a, &err));
    QCOMPARE(a.value(QLatin1String("can_id")).toDouble(), 257.0);
    QCOMPARE(a.value(QLatin1String("hex")).toString(), QStringLiteral("7050E803035A0000"));
    QVERIFY(TerminalPage::parseLine(QStringLiteral(R"(bms chg_kw=0 note="a b")"), &cmd, &a, &err));
    QCOMPARE(a.value(QLatin1String("chg_kw")).toDouble(), 0.0);
    QCOMPARE(a.value(QLatin1String("note")).toString(), QStringLiteral("a b"));
    QVERIFY(TerminalPage::parseLine(QStringLiteral(R"({"cmd":"speed","rpm":3000,"id":9})"), &cmd, &a, &err));
    QCOMPARE(cmd, QStringLiteral("speed"));
    QCOMPARE(a.value(QLatin1String("rpm")).toDouble(), 3000.0);
    QVERIFY(!a.contains(QLatin1String("id"))); // the session assigns ids
    QVERIFY(!TerminalPage::parseLine(QStringLiteral(R"({"rpm":3000})"), &cmd, &a, &err));
    QVERIFY(!TerminalPage::parseLine(QStringLiteral("arm now please"), &cmd, &a, &err));
    QVERIFY(!TerminalPage::parseLine(QStringLiteral("   "), &cmd, &a, &err));
}

void TstBridgeParser::liveBridgeFields()
{
    const QString bridge = BridgeTransport::locate();
    if (bridge.isEmpty()) {
        QSKIP("simulator bridge not built (make -C tool/bridge); the live protocol check did not run");
    }
    BridgeTransport::Options o;
    o.program = bridge;
    o.rateHz = 100;
    o.timeFactor = 5;
    BridgeTransport t(o);
    QSignalSpy frames(&t, &ITransport::frame);
    QSignalSpy infos(&t, &ITransport::info);
    t.open();
    QTRY_VERIFY_WITH_TIMEOUT(infos.size() >= 1 && frames.size() >= 20, 15000);
    const QJsonObject hello = infos.at(0).at(0).toJsonObject();
    for (const char *k : {"fw_id", "sku", "states", "ss_rows", "ss_actions", "dtcs", "limits", "motor", "calib",
                          "validation", "params", "n_x_rpm"}) {
        QVERIFY2(hello.contains(QLatin1String(k)), k);
    }
    const TelemetryFrame f = frames.last().at(0).value<TelemetryFrame>();
    for (const char *k : {"state.sm", "state.bridge", "state.hv", "state.evidence", "state.fw_ms", "motion.speed_rpm",
                          "motion.torque_cmd_nm", "motion.torque_req_nm", "foc.id_a", "foc.iq_a", "foc.ia_a",
                          "foc.vmax_v", "link.vdc_v", "link.v5gd_v", "temps.mod_max_c", "temps.derate_start_c",
                          "temps.derate_end_c", "limits.t_motor_nm", "limits.t_regen_nm", "limits.derate",
                          "safety.rows", "safety.action", "wd.isr_age_us", "wd.cmd_fresh", "wd.fs26_wd_err",
                          "arm.cal", "arm.val", "arm.otp", "arm.hvil", "arm.no_fault", "arm.armed", "plant.p_dc_w",
                          "plant.p_mech_w", "sim.rate_hz", "sim.time_factor", "inv.torque_applied_nm",
                          "inv.torque_cmd_nm", "inv.state"}) {
        QVERIFY2(f.has(QLatin1String(k)), k);
    }
    QVERIFY(f.dtcListComplete);
}

QTEST_MAIN(TstBridgeParser)
#include "tst_bridgeparser.moc"
