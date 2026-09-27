#include "DashboardPage.h"

#include "ProtocolInfo.h"
#include "Widgets.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonObject>

#include <cmath>

namespace {
QString onOff(double v) { return std::isnan(v) ? QStringLiteral("—") : (v != 0.0 ? QStringLiteral("yes") : QStringLiteral("no")); }
Theme::Sev good(double v, bool wantTrue = true)
{
    if (std::isnan(v)) {
        return Theme::Sev::Neutral;
    }
    return ((v != 0.0) == wantTrue) ? Theme::Sev::Ok : Theme::Sev::Crit;
}
} // namespace

DashboardPage::DashboardPage(Session &s, QWidget *parent)
    : Page(s, QStringLiteral("Dashboard"),
           QStringLiteral("The inverter's own view of itself — state, arming, link, motion, currents, temperatures, "
                          "limits and the §6 safety decision."),
           parent)
{
    addRootBanner();
    QVBoxLayout *root = scrollContent();

    // ---- state + arming
    auto *top = new QHBoxLayout;
    top->setSpacing(Theme::S3);
    auto *stateCard = new Card(QStringLiteral("Operating state (§9)"));
    m_stateName = makePill(QStringLiteral("—"), Theme::Sev::Stale);
    stateCard->addHeaderWidget(m_stateName);
    m_track = new StateTrack;
    stateCard->body()->addWidget(m_track);
    top->addWidget(stateCard, 3);
    auto *armCard = new Card(QStringLiteral("Arming"));
    auto *pills = new QHBoxLayout;
    m_armPill = makePill(QStringLiteral("NO DATA"), Theme::Sev::Stale);
    m_hvPill = makePill(QStringLiteral("HV ?"), Theme::Sev::Stale);
    m_bridgePill = makePill(QStringLiteral("BRIDGE ?"), Theme::Sev::Stale);
    pills->addWidget(m_armPill);
    pills->addWidget(m_hvPill);
    pills->addWidget(m_bridgePill);
    pills->addStretch(1);
    armCard->body()->addLayout(pills);
    m_armDetail = new QLabel(QStringLiteral("—"));
    m_armDetail->setObjectName(QStringLiteral("muted"));
    m_armDetail->setWordWrap(true);
    armCard->body()->addWidget(m_armDetail);
    top->addWidget(armCard, 2);
    root->addLayout(top);

    // ---- gauges and tiles
    auto *mid = new QGridLayout;
    mid->setSpacing(Theme::S3);
    m_speed = new ArcGauge(QStringLiteral("Speed"), QStringLiteral("rpm"), -16000, 16000, 0);
    m_vdc = new ArcGauge(QStringLiteral("Link voltage"), QStringLiteral("V"), 0, 1000, 1);
    m_torque = new Tile(QStringLiteral("Torque applied"), QStringLiteral("N·m"), 1);
    m_torque->setToolTip(QStringLiteral("INV_STATUS b4–5: the torque the issued current references represent (FW-37), "
                                        "beside the command after limits, ramps and trims (b16–17)."));
    m_power = new Tile(QStringLiteral("Electrical power"), QStringLiteral("kW"), 1);
    m_dq = new Tile(QStringLiteral("Current id / iq"), QStringLiteral("A"), 1);
    m_iph = new Tile(QStringLiteral("Phase current"), QStringLiteral("A rms"), 1);
    mid->addWidget(m_speed, 0, 0, 2, 1);
    mid->addWidget(m_vdc, 0, 1, 2, 1);
    mid->addWidget(m_torque, 0, 2);
    mid->addWidget(m_power, 0, 3);
    mid->addWidget(m_dq, 1, 2);
    mid->addWidget(m_iph, 1, 3);
    for (int c = 0; c < 4; c++) {
        mid->setColumnStretch(c, 1);
    }
    root->addLayout(mid);

    // ---- temperatures, limits, watchdog, safety
    auto *bottom = new QGridLayout;
    bottom->setSpacing(Theme::S3);
    auto *temps = new Card(QStringLiteral("Temperatures (FW-04 derating band)"));
    const struct {
        const char *label;
    } T[] = {{"Module U"}, {"Module V"}, {"Module W"}, {"Board H"}, {"Board A"}, {"Motor 1"}, {"Motor 2"}, {"Coolant"}};
    for (const auto &t : T) {
        auto *b = new BandBar(QLatin1String(t.label), QStringLiteral("°C"), -40, 160);
        temps->body()->addWidget(b);
        m_temps.push_back(b);
    }
    temps->body()->addStretch(1);
    auto *lim = new Card(QStringLiteral("Limits in force"));
    m_limits = new KvGrid;
    m_limits->addRow(QStringLiteral("tm"), QStringLiteral("Motoring torque"), QStringLiteral("FW-03 envelope, BMS limits, derating"));
    m_limits->addRow(QStringLiteral("tr"), QStringLiteral("Regen torque"));
    m_limits->addRow(QStringLiteral("il"), QStringLiteral("Current allowance"));
    m_limits->addRow(QStringLiteral("der"), QStringLiteral("Thermal derating"), QStringLiteral("FW-04: 1 = none"));
    m_limits->addRow(QStringLiteral("cf"), QStringLiteral("Coolant peak factor"));
    m_limits->addRow(QStringLiteral("pk"), QStringLiteral("Peak budget used"), QStringLiteral("FW-04: 30 s peak, refills over cal_peak_recovery_s"));
    m_limits->addRow(QStringLiteral("bms"), QStringLiteral("BMS charge / discharge"));
    m_limits->addRow(QStringLiteral("tmax"), QStringLiteral("cal_torque_max_nm"));
    m_limits->addRow(QStringLiteral("sl"), QStringLiteral("Speed limit requested"), QStringLiteral("FW-25/37: no voltage-feasible current"));
    lim->body()->addWidget(m_limits);
    lim->body()->addStretch(1);
    auto *wd = new Card(QStringLiteral("Watchdog & heartbeat"));
    m_wd = new KvGrid;
    m_wd->addRow(QStringLiteral("age"), QStringLiteral("Last frame"), QStringLiteral("host clock, since the last telemetry frame"), true);
    m_wd->addRow(QStringLiteral("fps"), QStringLiteral("Frames per second"), QString(), true);
    m_wd->addRow(QStringLiteral("isr"), QStringLiteral("Current-loop ISR age"), QStringLiteral("FW-31: compared with cal_isns_stale_us"));
    m_wd->addRow(QStringLiteral("fs26"), QStringLiteral("FS26 state / WD errors"), QStringLiteral("FW-12: FS0B at WD_ERR_LIMIT 2"));
    m_wd->addRow(QStringLiteral("fsb"), QStringLiteral("FS0B / FS1B asserted"));
    m_wd->addRow(QStringLiteral("cmd"), QStringLiteral("VCU command fresh"), QStringLiteral("FW-11: 20 ms"));
    m_wd->addRow(QStringLiteral("bms"), QStringLiteral("BMS limits fresh"), QStringLiteral("FW-11: cal_bms_timeout_ms"));
    m_wd->addRow(QStringLiteral("rej"), QStringLiteral("CAN rejects crc/frozen/jump"));
    m_wd->addRow(QStringLiteral("rslv"), QStringLiteral("Resolver valid / amplitude"));
    wd->body()->addWidget(m_wd);
    wd->body()->addStretch(1);
    auto *saf = new Card(QStringLiteral("Safety decision (§6)"));
    m_safety = new KvGrid;
    m_safety->addRow(QStringLiteral("rows"), QStringLiteral("Active rows"));
    m_safety->addRow(QStringLiteral("act"), QStringLiteral("Action"));
    m_safety->addRow(QStringLiteral("rule"), QStringLiteral("Rule (a) / (b)"), QStringLiteral("§6 SPO energy rules for the deciding row"));
    m_safety->addRow(QStringLiteral("asc"), QStringLiteral("ASC permitted"));
    m_safety->addRow(QStringLiteral("keep"), QStringLiteral("Keep HV connected"), QStringLiteral("FW-08b, INV_STATUS b1.5"));
    m_safety->addRow(QStringLiteral("nss"), QStringLiteral("No safe state proven"), QStringLiteral("INV_STATUS b14.0"));
    m_safety->addRow(QStringLiteral("svc"), QStringLiteral("Service required"), QStringLiteral("FW-26: stuck-on QDIS, b14.1"));
    m_safety->addRow(QStringLiteral("hvil"), QStringLiteral("HVIL"), QStringLiteral("FW-09"));
    m_safety->addRow(QStringLiteral("dtc"), QStringLiteral("Confirmed DTCs / first"));
    saf->body()->addWidget(m_safety);
    saf->body()->addStretch(1);
    bottom->addWidget(temps, 0, 0);
    bottom->addWidget(lim, 0, 1);
    bottom->addWidget(wd, 0, 2);
    bottom->addWidget(saf, 0, 3);
    for (int c = 0; c < 4; c++) {
        bottom->setColumnStretch(c, 1);
    }
    root->addLayout(bottom);
    root->addStretch(1);

    onFrames([this](const TelemetryFrame &f) { onFrame(f); });
    connect(&m_s, &Session::tick, this, &DashboardPage::onTick);
    connect(&m_s, &Session::transportChanged, this, [this] {
        for (Tile *t : {m_torque, m_power, m_dq, m_iph}) {
            t->clearHistory();
        }
        m_haveLimits = false;
    });
}

double DashboardPage::limit(const char *name, double def) const
{
    const QJsonObject l = m_s.deviceInfo().value(QLatin1String("limits")).toObject();
    if (l.contains(QLatin1String(name))) {
        return l.value(QLatin1String(name)).toDouble(def);
    }
    const ConstRow *c = ProtocolInfo::instance().constRow(QLatin1String(name));
    const double v = c ? c->valueFor(m_s.params().sku()) : def;
    return std::isnan(v) ? def : v;
}

void DashboardPage::onFrame(const TelemetryFrame &f)
{
    const ProtocolInfo &pi = ProtocolInfo::instance();
    if (!m_haveLimits) {
        const double nmax = m_s.deviceInfo().value(QLatin1String("motor")).toObject().value(QLatin1String("n_max_rpm")).toDouble(16000);
        const double nx = m_s.deviceInfo().value(QLatin1String("n_x_rpm")).toDouble(std::nan(""));
        m_speed->setRange(-nmax, nmax);
        if (!std::isnan(nx)) {
            m_speed->setBands({{-nmax, -nx, Theme::Sev::Warn}, {nx, nmax, Theme::Sev::Warn}});
        }
        const double vmin = limit("vdc_min_v", 500), vmax = limit("vdc_max_v", 850), ov = limit("ov_trip_v", 880);
        const double un = limit("cap_un_v", ov * 1.14);
        m_vdc->setRange(0, un);
        m_vdc->setBands({{vmin, vmax, Theme::Sev::Ok}, {vmax, ov, Theme::Sev::Warn}, {ov, un, Theme::Sev::Crit}});
        m_haveLimits = true;
    }
    const int sm = static_cast<int>(f.pick({"state.sm", "inv.state"}, -1));
    m_track->setState(sm);
    const Theme::Sev smSev = (sm == 9) ? Theme::Sev::Crit : (sm == 8) ? Theme::Sev::Warn
                           : (sm == 7 || sm == 6) ? Theme::Sev::Ok : Theme::Sev::Accent;
    setPill(m_stateName, pi.stateName(sm), smSev);
    const bool armed = f.b(QStringLiteral("state.gate_en")) || f.b(QStringLiteral("arm.armed")) || sm == 6 || sm == 7 || sm == 8;
    setPill(m_armPill, (sm == 9) ? QStringLiteral("FAULT") : armed ? QStringLiteral("ARMED") : QStringLiteral("DISARMED"),
            (sm == 9) ? Theme::Sev::Crit : armed ? Theme::Sev::Ok : Theme::Sev::Neutral);
    const int hv = static_cast<int>(f.pick({"state.hv", "inv.hv"}, -1));
    setPill(m_hvPill, QStringLiteral("HV %1").arg(pi.hvStates.value(hv, QStringLiteral("?")).section(QLatin1Char(' '), 0, 0).toUpper()),
            hv == 2 ? Theme::Sev::Warn : hv == 1 ? Theme::Sev::Ok : Theme::Sev::Crit);
    const int br = static_cast<int>(f.pick({"state.bridge", "inv.bridge"}, -1));
    static const char *const BR[] = {"SPO", "IDLE", "MODULATING", "PWM-ASC"};
    setPill(m_bridgePill, (br >= 0 && br < 4) ? QLatin1String(BR[br]) : QStringLiteral("BRIDGE ?"),
            br == 2 ? Theme::Sev::Accent : br == 3 ? Theme::Sev::Warn : Theme::Sev::Neutral);
    int present = f.has(QStringLiteral("state.evidence")) ? static_cast<int>(f.v(QStringLiteral("state.evidence")))
                                                          : (0x1F & ~static_cast<int>(f.v(QStringLiteral("inv.evidence_missing"), 0)));
    const QString missing = ProtocolInfo::bitsText(0x1F & ~present, pi.evidenceBits);
    QStringList detail;
    detail << QStringLiteral("Evidence missing: %1").arg(missing);
    if (f.has(QStringLiteral("state.gate_en"))) {
        detail << QStringLiteral("MCU_GATE_EN %1 · DRV_EN %2 · ASC latch %3").arg(onOff(f.v(QStringLiteral("state.gate_en"))),
                   onOff(f.v(QStringLiteral("state.drv_en"))), onOff(f.v(QStringLiteral("state.asc_latch"))));
    }
    if (f.b(QStringLiteral("state.no_arm"))) {
        detail << QStringLiteral("A failure forbids arming for this key cycle");
    }
    const QString note = f.str(QStringLiteral("vcu.note"));
    if (!note.isEmpty()) {
        detail << QStringLiteral("VCU: %1").arg(note);
    }
    m_armDetail->setText(detail.join(QLatin1Char('\n')));

    m_speed->setValue(f.pick({"motion.speed_rpm", "inv.speed_rpm"}));
    m_vdc->setValue(f.pick({"link.vdc_v", "inv.vdc_v"}));
    const double tq = f.v(QStringLiteral("inv.torque_applied_nm"));
    m_torque->setSecond(QStringLiteral("command"), f.pick({"motion.torque_cmd_nm", "inv.torque_cmd_nm"}));
    m_torque->setValue(tq);
    const double req = f.v(QStringLiteral("motion.torque_req_nm"));
    m_torque->setSubText(QStringLiteral("request %1 · limits +%2 / −%3 N·m")
                             .arg(fmtNum(req, 1), fmtNum(f.v(QStringLiteral("limits.t_motor_nm")), 0),
                                  fmtNum(f.v(QStringLiteral("limits.t_regen_nm")), 0)));
    const double pe = f.v(QStringLiteral("foc.p_elec_w"));
    m_power->setValue(pe / 1000.0);
    m_power->setSubText(f.has(QStringLiteral("plant.p_dc_w"))
                            ? QStringLiteral("DC side %1 kW · shaft %2 kW (plant)")
                                  .arg(fmtNum(f.v(QStringLiteral("plant.p_dc_w")) / 1000.0, 1),
                                       fmtNum(f.v(QStringLiteral("plant.p_mech_w")) / 1000.0, 1))
                            : QString());
    m_dq->setSecond(QStringLiteral("iq"), f.v(QStringLiteral("foc.iq_a")), QStringLiteral("id"));
    m_dq->setValue(f.v(QStringLiteral("foc.id_a")));
    m_dq->setSubText(QStringLiteral("refs id %1 · iq %2 A").arg(fmtNum(f.v(QStringLiteral("foc.id_ref_a")), 1),
                                                                fmtNum(f.v(QStringLiteral("foc.iq_ref_a")), 1)));
    m_iph->setValue(f.v(QStringLiteral("foc.i_rms_a")));
    m_iph->setSubText(QStringLiteral("ia %1 · ib %2 · ic %3 A%4").arg(fmtNum(f.v(QStringLiteral("foc.ia_a")), 0),
                        fmtNum(f.v(QStringLiteral("foc.ib_a")), 0), fmtNum(f.v(QStringLiteral("foc.ic_a")), 0),
                        f.has(QStringLiteral("foc.isns_valid")) && !f.b(QStringLiteral("foc.isns_valid")) ? QStringLiteral(" · INVALID") : QString()));
    m_iph->setSeverity(f.has(QStringLiteral("foc.isns_valid")) && !f.b(QStringLiteral("foc.isns_valid")) ? Theme::Sev::Crit : Theme::Sev::Neutral);

    // temperatures against the FW-04 band in force
    const double ds = f.v(QStringLiteral("temps.derate_start_c"), 90), de = f.v(QStringLiteral("temps.derate_end_c"), 115);
    const double cs = m_s.params().effective(m_s.params().indexOf(QStringLiteral("cal_coolant_derate_start_c")));
    const double ce = m_s.params().effective(m_s.params().indexOf(QStringLiteral("cal_coolant_derate_end_c")));
    const char *const ch[] = {"temps.mod_u_c", "temps.mod_v_c", "temps.mod_w_c", "temps.board_h_c", "temps.board_a_c",
                              "temps.motor1_c", "temps.motor2_c", "temps.coolant_c"};
    for (int i = 0; i < m_temps.size(); i++) {
        double v = f.v(QLatin1String(ch[i]));
        if (i == 0 && std::isnan(v)) {
            v = f.v(QStringLiteral("inv.t_module_c")); // CAN: the hottest module NTC only
        }
        m_temps[i]->setValue(v);
        if (i < 3) {
            m_temps[i]->setZones(ds, de);
        } else if (i == 7) {
            m_temps[i]->setZones(cs, ce);
        }
    }

    const double tm = f.v(QStringLiteral("limits.t_motor_nm")), tr = f.v(QStringLiteral("limits.t_regen_nm"));
    m_limits->setValue(QStringLiteral("tm"), QStringLiteral("%1 N·m").arg(fmtNum(tm, 0)));
    m_limits->setValue(QStringLiteral("tr"), QStringLiteral("%1 N·m").arg(fmtNum(tr, 0)));
    m_limits->setValue(QStringLiteral("il"), QStringLiteral("%1 A rms").arg(fmtNum(f.v(QStringLiteral("limits.i_limit_rms_a")), 0)));
    const double der = f.v(QStringLiteral("limits.derate"));
    m_limits->setValue(QStringLiteral("der"), fmtNum(der, 2), (der < 0.999) ? Theme::Sev::Warn : Theme::Sev::Neutral);
    m_limits->setValue(QStringLiteral("cf"), fmtNum(f.v(QStringLiteral("limits.coolant_factor")), 2));
    m_limits->setValue(QStringLiteral("pk"), QStringLiteral("%1 s%2").arg(fmtNum(f.v(QStringLiteral("limits.peak_used_s")), 1),
                         f.b(QStringLiteral("limits.peak_exhausted")) ? QStringLiteral(" · exhausted") : QString()),
                       f.b(QStringLiteral("limits.peak_exhausted")) ? Theme::Sev::Warn : Theme::Sev::Neutral);
    m_limits->setValue(QStringLiteral("bms"), QStringLiteral("%1 / %2 kW").arg(fmtNum(f.v(QStringLiteral("limits.p_chg_w")) / 1000.0, 0),
                                                                             fmtNum(f.v(QStringLiteral("limits.p_dis_w")) / 1000.0, 0)));
    m_limits->setValue(QStringLiteral("tmax"), QStringLiteral("%1 N·m").arg(fmtNum(f.v(QStringLiteral("limits.torque_max_nm")), 0)));
    const double sl = f.pick({"state.speed_limit_req", "inv.speed_limit_req"});
    m_limits->setValue(QStringLiteral("sl"), onOff(sl), (sl > 0) ? Theme::Sev::Warn : Theme::Sev::Neutral);

    const double isr = f.v(QStringLiteral("wd.isr_age_us"));
    const double isrMax = m_s.params().effective(m_s.params().indexOf(QStringLiteral("cal_isns_stale_us")));
    m_wd->setValue(QStringLiteral("isr"), QStringLiteral("%1 µs").arg(fmtNum(isr, 0)), (isr > isrMax) ? Theme::Sev::Crit : Theme::Sev::Neutral);
    m_wd->setValue(QStringLiteral("fs26"), QStringLiteral("%1 / %2").arg(fmtNum(f.v(QStringLiteral("wd.fs26_state")), 0),
                                                                      fmtNum(f.v(QStringLiteral("wd.fs26_wd_err")), 0)),
                   (f.v(QStringLiteral("wd.fs26_wd_err"), 0) > 0) ? Theme::Sev::Warn : Theme::Sev::Neutral);
    m_wd->setValue(QStringLiteral("fsb"), QStringLiteral("%1 / %2").arg(onOff(f.v(QStringLiteral("wd.fs0b"))), onOff(f.v(QStringLiteral("wd.fs1b")))),
                   (f.b(QStringLiteral("wd.fs0b")) || f.b(QStringLiteral("wd.fs1b"))) ? Theme::Sev::Warn : Theme::Sev::Neutral);
    m_wd->setValue(QStringLiteral("cmd"), onOff(f.v(QStringLiteral("wd.cmd_fresh"))), good(f.v(QStringLiteral("wd.cmd_fresh"))));
    m_wd->setValue(QStringLiteral("bms"), onOff(f.v(QStringLiteral("wd.bms_fresh"))), good(f.v(QStringLiteral("wd.bms_fresh"))));
    const double crc = f.pick({"wd.can_crc", "can.crc_err"}), frz = f.pick({"wd.can_frozen", "can.frozen"}), jmp = f.pick({"wd.can_jump", "can.jump"});
    m_wd->setValue(QStringLiteral("rej"), QStringLiteral("%1 / %2 / %3").arg(fmtNum(crc, 0), fmtNum(frz, 0), fmtNum(jmp, 0)),
                   (crc + frz + jmp > 0) ? Theme::Sev::Warn : Theme::Sev::Neutral);
    m_wd->setValue(QStringLiteral("rslv"), QStringLiteral("%1 / %2").arg(onOff(f.pick({"wd.rslv_valid", "inv.speed_valid"})),
                                                                      fmtNum(f.v(QStringLiteral("wd.rslv_amp")), 2)),
                   good(f.pick({"wd.rslv_valid", "inv.speed_valid"})));

    const int rows = static_cast<int>(f.v(QStringLiteral("safety.rows"), 0));
    QStringList active;
    for (int b = 0; b < pi.ssRows.size(); b++) {
        if (rows & (1 << b)) {
            active << pi.ssRows[b];
        }
    }
    m_safety->setValue(QStringLiteral("rows"), f.has(QStringLiteral("safety.rows")) ? (active.isEmpty() ? QStringLiteral("none") : active.join(QLatin1Char('\n'))) : QStringLiteral("—"),
                       active.isEmpty() ? Theme::Sev::Neutral : Theme::Sev::Crit);
    const int act = static_cast<int>(f.v(QStringLiteral("safety.action"), -1));
    m_safety->setValue(QStringLiteral("act"), pi.ssActions.value(act, QStringLiteral("—")), act > 0 ? Theme::Sev::Warn : Theme::Sev::Neutral);
    m_safety->setValue(QStringLiteral("rule"), QStringLiteral("%1 / %2").arg(onOff(f.v(QStringLiteral("safety.rule_a"))), onOff(f.v(QStringLiteral("safety.rule_b")))));
    m_safety->setValue(QStringLiteral("asc"), onOff(f.v(QStringLiteral("safety.asc_permitted"))));
    const double keep = f.pick({"state.keep_hv", "inv.keep_hv"}), nss = f.pick({"state.no_safe_state", "inv.no_safe_state"});
    const double svc = f.pick({"state.service_required", "inv.service_required"});
    m_safety->setValue(QStringLiteral("keep"), onOff(keep), keep > 0 ? Theme::Sev::Warn : Theme::Sev::Neutral);
    m_safety->setValue(QStringLiteral("nss"), onOff(nss), nss > 0 ? Theme::Sev::Crit : Theme::Sev::Neutral);
    m_safety->setValue(QStringLiteral("svc"), onOff(svc), svc > 0 ? Theme::Sev::Crit : Theme::Sev::Neutral);
    const int hvil = static_cast<int>(f.v(QStringLiteral("safety.hvil"), -1));
    m_safety->setValue(QStringLiteral("hvil"), pi.hvilStates.value(hvil, QStringLiteral("—")), hvil == 1 ? Theme::Sev::Ok : (hvil < 0 ? Theme::Sev::Neutral : Theme::Sev::Crit));
    const QVector<DtcState> actv = m_s.dtcs().active();
    m_safety->setValue(QStringLiteral("dtc"), QStringLiteral("%1 / %2").arg(m_s.dtcs().confirmedCount())
                                                  .arg(actv.isEmpty() ? QStringLiteral("none") : pi.dtcName(actv.first().id)),
                       actv.isEmpty() ? Theme::Sev::Neutral : Theme::Sev::Crit);
}

void DashboardPage::onTick()
{
    const bool stale = !m_s.isLive();
    const qint64 age = m_s.ageMs();
    m_track->setStale(stale);
    m_speed->setStale(stale);
    m_vdc->setStale(stale);
    for (Tile *t : {m_torque, m_power, m_dq, m_iph}) {
        t->setStale(stale, age);
    }
    for (BandBar *b : m_temps) {
        b->setStale(stale);
    }
    for (KvGrid *g : {m_limits, m_wd, m_safety}) {
        g->setStale(stale);
    }
    if (stale) {
        setPill(m_armPill, m_s.hasFrame() ? QStringLiteral("STALE") : QStringLiteral("NO DATA"), Theme::Sev::Stale);
        setPill(m_stateName, m_s.hasFrame() ? QStringLiteral("STALE · %1").arg(fmtAge(age)) : QStringLiteral("—"), Theme::Sev::Stale);
    }
    // the heartbeat rows are the host's own: they stay coloured even when stale
    m_wd->setValue(QStringLiteral("age"), fmtAge(age), stale ? Theme::Sev::Crit : Theme::Sev::Ok);
    m_wd->setValue(QStringLiteral("fps"), QString::number(m_s.framesPerSecond(), 'f', 0));
}
