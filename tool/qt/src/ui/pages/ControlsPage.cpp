#include "ControlsPage.h"

#include "CanTransport.h"
#include "ProtocolInfo.h"
#include "Widgets.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QShortcut>
#include <QSlider>
#include <QSpinBox>

#include <cmath>

namespace {
constexpr int DEADMAN_PERIOD_MS = 100; // PROTOCOL.md A.4.3: re-send every <= 100 ms
constexpr int DEADMAN_HOLD_MS = 300;   // the bridge drops the torque 300 ms after the last refresh

QPushButton *button(const QString &text, const QString &name, const QString &tip = QString(), const char *style = nullptr)
{
    auto *b = new QPushButton(text);
    b->setObjectName(style ? QLatin1String(style) : QLatin1String(""));
    b->setProperty("id", name);
    b->setAccessibleName(text);
    b->setToolTip(tip);
    return b;
}

QDoubleSpinBox *dspin(double lo, double hi, double v, int dec, const QString &suffix)
{
    auto *s = new QDoubleSpinBox;
    s->setRange(lo, hi);
    s->setDecimals(dec);
    s->setValue(v);
    s->setSuffix(suffix);
    s->setKeyboardTracking(false);
    return s;
}

const QVector<QPair<QString, QString>> &armItems()
{
    // tool/PROTOCOL.md A.5.2 "arm" (the bridge's checklist from the firmware's state)
    static const QVector<QPair<QString, QString>> items = {
        {QStringLiteral("cal"), QStringLiteral("Calibration record valid|FW-20")},
        {QStringLiteral("val"), QStringLiteral("EOL/HIL validation record|FW-24")},
        {QStringLiteral("otp"), QStringLiteral("FS26 OTP / PROG_ID bound|FW-12")},
        {QStringLiteral("platform"), QStringLiteral("PWM fault route, config, lock|FW-24")},
        {QStringLiteral("identity"), QStringLiteral("HW_ID and SKU|FW-01, FW-02")},
        {QStringLiteral("params"), QStringLiteral("Parameter set and gains|FW-20, §2")},
        {QStringLiteral("hvil"), QStringLiteral("HV interlock closed|FW-09")},
        {QStringLiteral("no_fault"), QStringLiteral("No active fault|§6")},
        {QStringLiteral("self_test"), QStringLiteral("Gate self-test passed|FW-16")},
        {QStringLiteral("gate_power"), QStringLiteral("Gate power (RDY)|FW-14")},
        {QStringLiteral("sensors"), QStringLiteral("Resolver, V_DC, currents valid|§9 step 3")},
        {QStringLiteral("vcu"), QStringLiteral("VCU command fresh|FW-11")},
        {QStringLiteral("precharge"), QStringLiteral("Precharge verdict|FW-19")},
        {QStringLiteral("contactors"), QStringLiteral("Contactors reported closed|§9")},
        {QStringLiteral("armed"), QStringLiteral("Armed: MCU_GATE_EN high|§9 step 8")},
    };
    return items;
}
// The static preconditions the operator's Arm needs; the others are what the arm sequence itself achieves.
const QStringList PRE_ARM = {QStringLiteral("cal"), QStringLiteral("val"), QStringLiteral("otp"), QStringLiteral("platform"),
                             QStringLiteral("identity"), QStringLiteral("params"), QStringLiteral("hvil"),
                             QStringLiteral("no_fault")};

struct Fault {
    const char *id, *label, *tip;
    bool persistent;
};
// tool/PROTOCOL.md A.4.6
const Fault FAULTS[] = {
    {"desat_hs", "DESAT high side", "A high-side driver latches DESAT: hardware SPO, FW-15, FAULT", false},
    {"desat_ls", "DESAT low side", "A low-side driver latches DESAT: SPO at every speed, FAULT", false},
    {"overcurrent", "Over-current", "Two phase-U samples beyond 1.3 × the FW-05 trip", false},
    {"battery_loss", "Battery loss", "The main contactor opens under load (FW-08)", false},
    {"overtemp", "Over-temperature", "Coolant-flow loss: module temperatures rise by ΔT at 2 K/s (FW-04)", true},
    {"vdc_sense_loss", "V_DC sensing loss", "Channel 1 at the AMC1311 fail-safe level (FW-07)", true},
    {"resolver_loss", "Resolver loss", "Resolver output at 20 % amplitude (FW-10)", true},
    {"can_loss", "CAN loss", "The VCU and BMS frames stop (FW-11)", true},
    {"hvil_open", "HVIL open", "The HVIL signature reads open (FW-09)", true},
    {"v5gd_loss", "V5GD loss", "Gate-logic supply hovering at 4.5 V (§4c)", true},
    {"lv_overvoltage", "LV overvoltage", "KL30 at the FS26 VSUP = 35 V (FW-33)", true},
};
} // namespace

bool ControlsPage::confirmTyped(QWidget *parent, const QString &title, const QString &text, const QString &word)
{
    QDialog d(parent);
    d.setWindowTitle(title);
    auto *l = new QVBoxLayout(&d);
    l->setContentsMargins(Theme::S5, Theme::S5, Theme::S5, Theme::S4);
    l->setSpacing(Theme::S3);
    auto *t = new QLabel(QStringLiteral("<b>%1</b>").arg(title));
    auto *x = new QLabel(text);
    x->setWordWrap(true);
    x->setObjectName(QStringLiteral("muted"));
    auto *ask = new QLabel(QStringLiteral("Type <b>%1</b> to confirm:").arg(word));
    auto *edit = new QLineEdit;
    edit->setObjectName(QStringLiteral("confirmEdit"));
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QPushButton *ok = bb->button(QDialogButtonBox::Ok);
    ok->setObjectName(QStringLiteral("danger"));
    ok->setEnabled(false);
    ok->setText(title);
    QObject::connect(edit, &QLineEdit::textChanged, ok, [ok, word](const QString &s) { ok->setEnabled(s.trimmed() == word); });
    QObject::connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    for (QWidget *w : std::initializer_list<QWidget *>{t, x, ask, edit}) {
        l->addWidget(w);
    }
    l->addWidget(bb);
    d.setMinimumWidth(460);
    edit->setFocus();
    return d.exec() == QDialog::Accepted;
}

ControlsPage::ControlsPage(Session &s, QWidget *parent)
    : Page(s, QStringLiteral("Controls"),
           QStringLiteral("Arm, torque and protective actions. The firmware decides: every command is a request it may "
                          "refuse, and the acknowledgement says why."),
           parent)
{
    QVBoxLayout *root = scrollContent();
    m_lockBanner = new QLabel;
    m_lockBanner->setObjectName(QStringLiteral("pill"));
    m_lockBanner->setProperty("severity", QStringLiteral("warn"));
    m_lockBanner->setWordWrap(true);
    m_lockBanner->hide();
    root->addWidget(m_lockBanner);

    auto *grid = new QGridLayout;
    grid->setSpacing(Theme::S3);

    // ---------------- arming
    auto *armCard = new Card(QStringLiteral("Arming — the firmware's preconditions"));
    m_checks = new CheckList;
    m_checks->setObjectName(QStringLiteral("armChecklist"));
    m_checks->setItems(armItems());
    armCard->body()->addWidget(m_checks);
    m_armNote = new QLabel(QStringLiteral("No data"));
    m_armNote->setObjectName(QStringLiteral("muted"));
    m_armNote->setWordWrap(true);
    armCard->body()->addWidget(m_armNote);
    auto *armRow = new QHBoxLayout;
    m_arm = button(QStringLiteral("Arm"), QStringLiteral("arm"), QStringLiteral("Starts the arm sequence (enable, precharge, contactors). Needs CAL + VAL + OTP, the platform evidence, HVIL closed and no active fault."), "primary");
    m_arm->setObjectName(QStringLiteral("primary"));
    m_arm->setAccessibleName(QStringLiteral("armButton"));
    m_disarm = button(QStringLiteral("Disarm"), QStringLiteral("disarm"), QStringLiteral("Torque to zero, enable off; the contactors open below n_x (FW-08)"));
    m_disarm->setAccessibleName(QStringLiteral("disarmButton"));
    armRow->addWidget(m_arm, 1);
    armRow->addWidget(m_disarm, 1);
    armCard->body()->addLayout(armRow);
    grid->addWidget(armCard, 0, 0, 2, 1);

    // ---------------- torque
    auto *tqCard = new Card(QStringLiteral("Torque — hold to apply"));
    auto *tqRow = new QHBoxLayout;
    m_tqSlider = new QSlider(Qt::Horizontal);
    m_tqSlider->setRange(-450, 450);
    m_tqSlider->setAccessibleName(QStringLiteral("Torque setpoint"));
    m_tq = dspin(-450, 450, 0, 1, QStringLiteral(" N·m"));
    m_tq->setAccessibleName(QStringLiteral("Torque setpoint (N·m)"));
    tqRow->addWidget(m_tqSlider, 1);
    tqRow->addWidget(m_tq);
    tqCard->body()->addLayout(tqRow);
    auto *gearRow = new QHBoxLayout;
    gearRow->setSpacing(0);
    m_gear = new QButtonGroup(this);
    m_gear->setExclusive(true);
    int gi = 0;
    for (const char *g : {"P", "R", "N", "D"}) {
        auto *b = new QPushButton(QLatin1String(g));
        b->setObjectName(QStringLiteral("segment"));
        b->setCheckable(true);
        b->setToolTip(QStringLiteral("Gear %1 (VCU_CMD): D never drives backwards, R never forwards, N/P zero torque; changes only near standstill").arg(QLatin1String(g)));
        m_gear->addButton(b, gi++);
        gearRow->addWidget(b);
        m_driveWidgets << b;
    }
    m_gear->button(3)->setChecked(true);
    gearRow->addSpacing(Theme::S4);
    gearRow->addWidget(new QLabel(QStringLiteral("Slew")));
    m_slew = dspin(0, 100000, 3000, 0, QStringLiteral(" N·m/s"));
    m_slew->setToolTip(QStringLiteral("The VCU's rate limit on the torque in its frames; 0 = steps"));
    gearRow->addWidget(m_slew);
    gearRow->addStretch(1);
    tqCard->body()->addLayout(gearRow);
    m_deadman = button(QStringLiteral("HOLD TO APPLY TORQUE   (Space)"), QStringLiteral("deadman"),
                       QStringLiteral("Press and hold (mouse or Space): the setpoint is re-sent every 100 ms with a 300 ms deadman. Release = zero torque."), "deadman");
    m_deadman->setObjectName(QStringLiteral("deadman"));
    m_deadman->setAccessibleName(QStringLiteral("deadmanButton"));
    m_deadman->setAutoRepeat(false);
    tqCard->body()->addWidget(m_deadman);
    m_zero = button(QStringLiteral("Zero torque (Esc)"), QStringLiteral("zero"));
    tqCard->body()->addWidget(m_zero);
    m_tqLive = new KvGrid;
    m_tqLive->addRow(QStringLiteral("req"), QStringLiteral("Request received"));
    m_tqLive->addRow(QStringLiteral("cmd"), QStringLiteral("Command (after limits)"));
    m_tqLive->addRow(QStringLiteral("act"), QStringLiteral("Applied (INV_STATUS b4–5)"));
    m_tqLive->addRow(QStringLiteral("lim"), QStringLiteral("Limits motoring / regen"));
    tqCard->body()->addWidget(m_tqLive);
    grid->addWidget(tqCard, 0, 1);

    // ---------------- speed (dyno)
    auto *spCard = new Card(QStringLiteral("Dyno speed (simulator)"));
    auto *spRow = new QHBoxLayout;
    m_rpm = new QSpinBox;
    m_rpm->setRange(-16000, 16000);
    m_rpm->setSingleStep(100);
    m_rpm->setSuffix(QStringLiteral(" rpm"));
    m_ramp = new QSpinBox;
    m_ramp->setRange(1, 20000);
    m_ramp->setValue(2000);
    m_ramp->setSuffix(QStringLiteral(" rpm/s"));
    m_speedApply = button(QStringLiteral("Set speed"), QStringLiteral("speed"), QStringLiteral("The load machine holds the shaft speed; the firmware has no speed command"));
    spRow->addWidget(m_rpm);
    spRow->addWidget(m_ramp);
    spRow->addWidget(m_speedApply);
    spCard->body()->addLayout(spRow);
    auto *spHint = new QLabel(QStringLiteral("VCU_CMD carries torque only; speed is the dyno's. On a real bench this card does nothing."));
    spHint->setObjectName(QStringLiteral("hint"));
    spHint->setWordWrap(true);
    spCard->body()->addWidget(spHint);
    m_simButtons << m_speedApply;
    grid->addWidget(spCard, 1, 1);

    // ---------------- protective actions
    auto *protCard = new Card(QStringLiteral("Protective actions"));
    m_ascOn = button(QStringLiteral("Enter PWM-ASC…"), QStringLiteral("asc_on"), QStringLiteral("Simulator test hook: the firmware's own §4c entry, armed only"), "danger");
    m_ascOn->setObjectName(QStringLiteral("danger"));
    m_ascOff = button(QStringLiteral("Exit ASC…"), QStringLiteral("asc_off"), QStringLiteral("FW-06a: the MCU-commanded exit, allowed only below n_x"));
    m_discharge = button(QStringLiteral("Request discharge…"), QStringLiteral("discharge"), QStringLiteral("FW-17: QDIS fires only with the contactors reported open, 5 s, at most 3 per 5 min"), "danger");
    m_discharge->setObjectName(QStringLiteral("danger"));
    auto *pRow = new QHBoxLayout;
    pRow->addWidget(m_ascOn);
    pRow->addWidget(m_ascOff);
    pRow->addWidget(m_discharge);
    protCard->body()->addLayout(pRow);
    auto *pHint = new QLabel(QStringLiteral("The vehicle interface has no ASC command: on the real inverter ASC is only a §6 decision."));
    pHint->setObjectName(QStringLiteral("hint"));
    pHint->setWordWrap(true);
    protCard->body()->addWidget(pHint);
    m_simButtons << m_ascOn << m_ascOff;
    grid->addWidget(protCard, 2, 0);

    // ---------------- vehicle signals
    auto *vehCard = new Card(QStringLiteral("Vehicle signals"));
    auto *vg = new QGridLayout;
    vg->setSpacing(Theme::S2);
    auto *reset = button(QStringLiteral("Fault reset"), QStringLiteral("fault_reset"), QStringLiteral("VCU fault-reset bit for 100 ms; latched rows reset only below n_x"));
    auto *retry = button(QStringLiteral("DESAT retry auth"), QStringLiteral("retry_auth"), QStringLiteral("FW-15: one retry per key cycle, ≥ 1 s after the DESAT, below n_x, at reduced torque"));
    auto *keyOff = button(QStringLiteral("Key off"), QStringLiteral("key_off"));
    auto *keyOn = button(QStringLiteral("Key on"), QStringLiteral("key_on"));
    auto *reboot = button(QStringLiteral("Power cycle"), QStringLiteral("reboot"), QStringLiteral("Simulator: the whole card model resets (NVM kept)"));
    m_coolant = dspin(-40, 120, 50, 0, QStringLiteral(" °C"));
    auto *coolApply = button(QStringLiteral("Set coolant"), QStringLiteral("coolant"));
    vg->addWidget(reset, 0, 0);
    vg->addWidget(retry, 0, 1);
    vg->addWidget(keyOff, 1, 0);
    vg->addWidget(keyOn, 1, 1);
    vg->addWidget(reboot, 2, 0);
    vg->addWidget(m_coolant, 3, 0);
    vg->addWidget(coolApply, 3, 1);
    vehCard->body()->addLayout(vg);
    m_driveWidgets << reset << retry << coolApply;
    m_simButtons << keyOff << keyOn << reboot;
    grid->addWidget(vehCard, 2, 1);

    // ---------------- fault injection + BMS (simulator)
    m_injCard = new Card(QStringLiteral("Fault injection (simulator only)"));
    auto *ig = new QGridLayout;
    ig->setSpacing(Theme::S2);
    int k = 0;
    for (const Fault &f : FAULTS) {
        auto *b = button(QString::fromUtf8(f.label), QLatin1String(f.id), QString::fromUtf8(f.tip));
        b->setAccessibleName(QStringLiteral("inject_%1").arg(QLatin1String(f.id)));
        b->setCheckable(f.persistent);
        const QString id = QLatin1String(f.id);
        const bool persistent = f.persistent;
        connect(b, &QPushButton::clicked, this, [this, id, persistent, b] { inject(id, persistent ? b->isChecked() : true); });
        ig->addWidget(b, k / 3, k % 3);
        m_injectButtons << b;
        k++;
    }
    auto *clearAll = button(QStringLiteral("Clear all injections"), QStringLiteral("clear_all"));
    connect(clearAll, &QPushButton::clicked, this, [this] { m_s.command(QStringLiteral("clear"), {{QStringLiteral("fault"), QStringLiteral("all")}}); });
    ig->addWidget(clearAll, k / 3, k % 3);
    m_injectButtons << clearAll;
    m_injCard->body()->addLayout(ig);
    auto *dtRow = new QHBoxLayout;
    dtRow->addWidget(new QLabel(QStringLiteral("Over-temperature ΔT")));
    m_dt = dspin(1, 120, 60, 0, QStringLiteral(" K"));
    dtRow->addWidget(m_dt);
    dtRow->addStretch(1);
    m_injCard->body()->addLayout(dtRow);
    auto *bmsTitle = new QLabel(QStringLiteral("BMS limits (VCU_BMS)"));
    bmsTitle->setObjectName(QStringLiteral("cardTitle"));
    m_injCard->body()->addWidget(bmsTitle);
    auto *bmsRow = new QHBoxLayout;
    m_chg = dspin(0, 6553, 100, 0, QStringLiteral(" kW chg"));
    m_dis = dspin(0, 6553, 250, 0, QStringLiteral(" kW dis"));
    m_pack = dspin(1, 999, 750, 0, QStringLiteral(" V pack"));
    auto *bmsApply = button(QStringLiteral("Apply"), QStringLiteral("bms"), QStringLiteral("FW-11: a charge limit of 0 is the §6 BMS_LIMIT_ZERO row (zero regen)"));
    for (QWidget *w : std::initializer_list<QWidget *>{m_chg, m_dis, m_pack, bmsApply}) {
        bmsRow->addWidget(w);
    }
    m_injCard->body()->addLayout(bmsRow);
    m_driveWidgets << bmsApply;
    auto *provTitle = new QLabel(QStringLiteral("EOL/HIL provisioning (power-cycles; withdraw one to see the fail-closed arming)"));
    provTitle->setObjectName(QStringLiteral("cardTitle"));
    provTitle->setWordWrap(true);
    m_injCard->body()->addWidget(provTitle);
    auto *provRow = new QHBoxLayout;
    const char *const PROV[] = {"val", "route", "otp", "cal"};
    const char *const PROVL[] = {"Validation record (FW-24)", "Fault route (FW-24)", "PROG_ID (FW-12)", "Motor ID (FW-20)"};
    for (int i = 0; i < 4; i++) {
        m_prov[i] = new QCheckBox(QLatin1String(PROVL[i]));
        m_prov[i]->setChecked(true);
        m_prov[i]->setProperty("key", QLatin1String(PROV[i]));
        provRow->addWidget(m_prov[i]);
    }
    auto *provApply = button(QStringLiteral("Apply"), QStringLiteral("provision"));
    provRow->addWidget(provApply);
    m_injCard->body()->addLayout(provRow);
    m_simButtons << provApply;
    grid->addWidget(m_injCard, 3, 0, 1, 2);

    // ---------------- CAN bench (real bus)
    m_canCard = new Card(QStringLiteral("CAN bench — this tool as the VCU (service key)"));
    auto *cRow = new QHBoxLayout;
    m_vcuOn = button(QStringLiteral("VCU emulation"), QStringLiteral("vcu"), QStringLiteral("VCU_CMD + VCU_BMS every 10 ms from this tool"));
    m_vcuOn->setCheckable(true);
    m_contactors = new QComboBox;
    for (const char *c : {"open", "precharge", "closed", "invalid"}) {
        m_contactors->addItem(QLatin1String(c));
    }
    auto *contApply = button(QStringLiteral("Report contactors"), QStringLiteral("contactors"));
    cRow->addWidget(m_vcuOn);
    cRow->addWidget(new QLabel(QStringLiteral("Contactor report")));
    cRow->addWidget(m_contactors);
    cRow->addWidget(contApply);
    cRow->addStretch(1);
    m_canCard->body()->addLayout(cRow);
    auto *cHint = new QLabel(QStringLiteral("The contactors of a real bench are hardware: report what they are, never what you wish. "
                                            "Stopping the emulation makes the command stale — the firmware ramps to zero (FW-11)."));
    cHint->setObjectName(QStringLiteral("hint"));
    cHint->setWordWrap(true);
    m_canCard->body()->addWidget(cHint);
    m_canWidgets << m_vcuOn << m_contactors << contApply;
    grid->addWidget(m_canCard, 4, 0, 1, 2);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    root->addLayout(grid);
    root->addStretch(1);

    // ---------------- behaviour
    connect(m_arm, &QPushButton::clicked, this, [this] { m_s.command(QStringLiteral("arm")); });
    connect(m_disarm, &QPushButton::clicked, this, [this] {
        stopDeadman();
        m_s.command(QStringLiteral("disarm"));
    });
    connect(m_tqSlider, &QSlider::valueChanged, this, [this](int v) {
        if (std::lround(m_tq->value()) != v) {
            m_tq->setValue(v);
        }
    });
    connect(m_tq, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        QSignalBlocker b(m_tqSlider);
        m_tqSlider->setValue(static_cast<int>(std::lround(v)));
        if (m_holding) {
            sendTorque(true); // the new setpoint at once, not at the next refresh
        }
    });
    connect(m_deadman, &QPushButton::pressed, this, &ControlsPage::startDeadman);
    connect(m_deadman, &QPushButton::released, this, &ControlsPage::stopDeadman);
    connect(m_zero, &QPushButton::clicked, this, [this] {
        stopDeadman();
        m_tq->setValue(0);
        m_s.command(QStringLiteral("torque"), {{QStringLiteral("nm"), 0.0}});
    });
    auto *esc = new QShortcut(QKeySequence(Qt::Key_Escape), this, [this] { m_zero->click(); });
    esc->setContext(Qt::WidgetWithChildrenShortcut);
    m_deadmanTimer.setInterval(DEADMAN_PERIOD_MS);
    connect(&m_deadmanTimer, &QTimer::timeout, this, [this] {
        if (!m_deadman->isDown() || !m_s.isDeviceLive()) {
            stopDeadman(); // released outside the button, focus lost, or the link went stale
            return;
        }
        sendTorque(true);
    });
    connect(m_gear, &QButtonGroup::idClicked, this, [this](int id) {
        static const char *const G[] = {"P", "R", "N", "D"};
        m_s.command(QStringLiteral("gear"), {{QStringLiteral("gear"), QLatin1String(G[id])}});
    });
    connect(m_speedApply, &QPushButton::clicked, this, [this] {
        m_s.command(QStringLiteral("speed"), {{QStringLiteral("rpm"), m_rpm->value()}, {QStringLiteral("ramp_rpm_s"), m_ramp->value()}});
    });
    connect(m_ascOn, &QPushButton::clicked, this, [this] {
        if (confirmTyped(this, QStringLiteral("Enter PWM-ASC"),
                         QStringLiteral("All three low sides on (PWM-ASC through the firmware's §4c sequence). The winding current "
                                        "circulates and brakes the machine. Simulator test hook — the vehicle interface has no ASC command."),
                         QStringLiteral("ASC"))) {
            m_s.command(QStringLiteral("asc"), {{QStringLiteral("on"), true}});
        }
    });
    connect(m_ascOff, &QPushButton::clicked, this, [this] {
        if (confirmTyped(this, QStringLiteral("Exit ASC"),
                         QStringLiteral("The firmware's MCU-commanded exit (FW-06a); it refuses at or above n_x."), QStringLiteral("EXIT"))) {
            m_s.command(QStringLiteral("asc"), {{QStringLiteral("on"), false}});
        }
    });
    connect(m_discharge, &QPushButton::clicked, this, [this] {
        if (confirmTyped(this, QStringLiteral("Request discharge"),
                         QStringLiteral("Sets the VCU discharge-request bit for 6 s. The firmware fires QDIS only with the contactors "
                                        "reported open, for at most 5 s, at most 3 times in 5 minutes (FW-17)."),
                         QStringLiteral("DISCHARGE"))) {
            m_s.command(QStringLiteral("discharge"));
        }
    });
    connect(reset, &QPushButton::clicked, this, [this] { m_s.command(QStringLiteral("fault_reset")); });
    connect(retry, &QPushButton::clicked, this, [this] { m_s.command(QStringLiteral("retry_auth")); });
    connect(keyOff, &QPushButton::clicked, this, [this] { m_s.command(QStringLiteral("key"), {{QStringLiteral("on"), false}}); });
    connect(keyOn, &QPushButton::clicked, this, [this] { m_s.command(QStringLiteral("key"), {{QStringLiteral("on"), true}}); });
    connect(reboot, &QPushButton::clicked, this, [this] { m_s.command(QStringLiteral("reboot")); });
    connect(coolApply, &QPushButton::clicked, this, [this] { m_s.command(QStringLiteral("coolant"), {{QStringLiteral("c"), m_coolant->value()}}); });
    connect(bmsApply, &QPushButton::clicked, this, [this] {
        m_s.command(QStringLiteral("bms"), {{QStringLiteral("chg_kw"), m_chg->value()}, {QStringLiteral("dis_kw"), m_dis->value()},
                                           {QStringLiteral("pack_v"), m_pack->value()}});
    });
    connect(provApply, &QPushButton::clicked, this, [this] {
        QJsonObject a;
        for (QCheckBox *c : m_prov) {
            a.insert(c->property("key").toString(), c->isChecked());
        }
        m_s.command(QStringLiteral("provision"), a);
    });
    connect(m_vcuOn, &QPushButton::toggled, this, [this](bool on) { m_s.command(QStringLiteral("vcu"), {{QStringLiteral("on"), on}}); });
    connect(contApply, &QPushButton::clicked, this, [this] {
        m_s.command(QStringLiteral("contactors"), {{QStringLiteral("state"), m_contactors->currentText()}});
    });
    onFrames([this](const TelemetryFrame &f) { onFrame(f); });
    connect(&m_s, &Session::tick, this, &ControlsPage::updateEnables);
    connect(&m_s, &Session::serviceChanged, this, &ControlsPage::updateEnables);
    connect(&m_s, &Session::infoChanged, this, [this](const QJsonObject &info) {
        // the dyno may not exceed the calibration record's motor n_max (the bridge refuses beyond it)
        const double nmax = info.value(QLatin1String("motor")).toObject().value(QLatin1String("n_max_rpm")).toDouble(16000.0);
        m_rpm->setRange(-static_cast<int>(nmax), static_cast<int>(nmax));
        m_rpm->setToolTip(QStringLiteral("Dyno speed setpoint, |n| ≤ n_max = %1 rpm (n_x = %2 rpm)")
                              .arg(nmax, 0, 'f', 0).arg(info.value(QLatin1String("n_x_rpm")).toDouble(), 0, 'f', 0));
    });
    connect(&m_s, &Session::transportChanged, this, [this] {
        stopDeadman();
        m_checks->setAll(CheckList::State::Unknown);
        updateEnables();
    });
    updateEnables();
}

double ControlsPage::torqueMax() const
{
    const int i = m_s.params().indexOf(QStringLiteral("cal_torque_max_nm"));
    const double t = (i >= 0) ? m_s.params().effective(i) : 450.0;
    return std::clamp(t, 1.0, 3000.0);
}

void ControlsPage::setTorqueSetpoint(double nm) { m_tq->setValue(nm); }

void ControlsPage::sendTorque(bool withHold)
{
    QJsonObject a{{QStringLiteral("nm"), m_tq->value()}, {QStringLiteral("slew_nm_s"), m_slew->value()}};
    if (withHold) {
        a.insert(QStringLiteral("hold_ms"), DEADMAN_HOLD_MS);
    }
    m_s.command(QStringLiteral("torque"), a);
}

void ControlsPage::startDeadman()
{
    if (!m_deadman->isEnabled()) {
        return;
    }
    m_holding = true;
    sendTorque(true);
    m_deadmanTimer.start();
}

void ControlsPage::stopDeadman()
{
    const bool was = m_holding;
    m_holding = false;
    m_deadmanTimer.stop();
    if (was && m_s.transport()) {
        m_s.command(QStringLiteral("torque"), {{QStringLiteral("nm"), 0.0}}); // no hold_ms: 0 N·m is held
    }
}

void ControlsPage::inject(const QString &fault, bool on)
{
    QJsonObject a{{QStringLiteral("fault"), fault}};
    if (fault == QLatin1String("overtemp") && on) {
        a.insert(QStringLiteral("dt_c"), m_dt->value());
    }
    m_s.command(on ? QStringLiteral("inject") : QStringLiteral("clear"), a);
}

void ControlsPage::onFrame(const TelemetryFrame &f)
{
    const bool haveArm = f.has(QStringLiteral("arm.cal"));
    if (haveArm) {
        for (const auto &it : armItems()) {
            const QString k = QStringLiteral("arm.") + it.first;
            m_checks->set(it.first, !f.has(k) ? CheckList::State::Unknown : (f.b(k) ? CheckList::State::Ok : CheckList::State::Fail));
        }
    } else if (f.has(QStringLiteral("inv.state"))) {
        // CAN: what INV_STATUS proves; the rest stays unknown
        const int missing = static_cast<int>(f.v(QStringLiteral("inv.evidence_missing")));
        const int sm = static_cast<int>(f.v(QStringLiteral("inv.state")));
        auto st = [](bool ok) { return ok ? CheckList::State::Ok : CheckList::State::Fail; };
        m_checks->set(QStringLiteral("val"), st((missing & 0x18) == 0), ProtocolInfo::bitsText(missing & 0x18, ProtocolInfo::instance().evidenceBits));
        m_checks->set(QStringLiteral("platform"), st((missing & 0x07) == 0), ProtocolInfo::bitsText(missing & 0x07, ProtocolInfo::instance().evidenceBits));
        m_checks->set(QStringLiteral("no_fault"), st(!f.b(QStringLiteral("inv.fault"))));
        m_checks->set(QStringLiteral("self_test"), st(f.b(QStringLiteral("inv.self_test_done"))));
        m_checks->set(QStringLiteral("precharge"), st(!f.b(QStringLiteral("inv.precharge_refused"))));
        m_checks->set(QStringLiteral("armed"), st(sm == 6 || sm == 7 || sm == 8));
    }
    const QString seq = f.str(QStringLiteral("vcu.seq"));
    const QString note = f.str(QStringLiteral("vcu.note"));
    QStringList lines;
    if (!seq.isEmpty()) {
        lines << QStringLiteral("Bench VCU: %1%2").arg(seq, note.isEmpty() ? QString() : QStringLiteral(" — ") + note);
    }
    if (f.b(QStringLiteral("state.no_arm"))) {
        lines << QStringLiteral("A failure forbids arming for this key cycle (see Faults)");
    }
    m_armNote->setText(lines.isEmpty() ? QStringLiteral("State %1").arg(ProtocolInfo::instance().stateName(static_cast<int>(f.pick({"state.sm", "inv.state"}, -1))))
                                       : lines.join(QLatin1Char('\n')));
    m_tqLive->setValue(QStringLiteral("req"), QStringLiteral("%1 N·m").arg(fmtNum(f.v(QStringLiteral("motion.torque_req_nm")), 1)));
    m_tqLive->setValue(QStringLiteral("cmd"), QStringLiteral("%1 N·m").arg(fmtNum(f.pick({"motion.torque_cmd_nm", "inv.torque_cmd_nm"}), 1)));
    m_tqLive->setValue(QStringLiteral("act"), QStringLiteral("%1 N·m").arg(fmtNum(f.v(QStringLiteral("inv.torque_applied_nm")), 1)));
    m_tqLive->setValue(QStringLiteral("lim"), QStringLiteral("+%1 / −%2 N·m").arg(fmtNum(f.v(QStringLiteral("limits.t_motor_nm")), 0),
                                                                              fmtNum(f.v(QStringLiteral("limits.t_regen_nm")), 0)));
    // persistent injections as the plant reports them
    const QString inj = f.str(QStringLiteral("plant.inject"));
    if (inj != m_injected) {
        m_injected = inj;
        const QStringList on = inj.split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (QPushButton *b : m_injectButtons) {
            if (b->isCheckable()) {
                QSignalBlocker blk(b);
                b->setChecked(on.contains(b->property("id").toString()));
            }
        }
    }
    const QString gear = f.str(QStringLiteral("vcu.gear"));
    if (!gear.isEmpty()) {
        const int id = QStringLiteral("PRND").indexOf(gear.left(1));
        if (id >= 0 && m_gear->checkedId() != id) {
            QSignalBlocker blk(m_gear);
            m_gear->button(id)->setChecked(true);
        }
    }
}

void ControlsPage::updateEnables()
{
    ITransport *t = m_s.transport();
    const bool live = m_s.isDeviceLive();
    const bool sim = t && t->isSimulator();
    const bool can = t && t->kind() == ITransport::Kind::Can;
    const bool allowed = m_s.destructiveAllowed();
    const bool drive = live && allowed;
    m_checks->setStale(!live);
    const double tmax = torqueMax();
    if (m_tq->maximum() != tmax) {
        m_tq->setRange(-tmax, tmax);
        m_tqSlider->setRange(static_cast<int>(-tmax), static_cast<int>(tmax));
    }
    bool preOk = true;
    for (const QString &k : PRE_ARM) {
        const CheckList::State st = m_checks->state(k);
        preOk = preOk && (st == CheckList::State::Ok || (can && st == CheckList::State::Unknown));
    }
    const bool armed = m_checks->state(QStringLiteral("armed")) == CheckList::State::Ok;
    m_arm->setEnabled(drive && preOk && !armed);
    m_arm->setToolTip(preOk ? QStringLiteral("Start the arm sequence")
                            : QStringLiteral("The firmware's preconditions are not met: see the checklist"));
    m_disarm->setEnabled(drive);
    m_deadman->setEnabled(drive);
    m_zero->setEnabled(live && t);
    m_tq->setEnabled(drive);
    m_tqSlider->setEnabled(drive);
    for (QWidget *w : m_driveWidgets) {
        w->setEnabled(drive);
    }
    for (QPushButton *b : m_simButtons) {
        b->setEnabled(live && sim);
    }
    for (QPushButton *b : m_injectButtons) {
        b->setEnabled(live && sim);
    }
    m_discharge->setEnabled(drive);
    m_injCard->setVisible(!can);
    m_canCard->setVisible(can);
    for (QWidget *w : m_canWidgets) {
        w->setEnabled(can && allowed && t->state() == ITransport::State::Open);
    }
    if (can && !allowed) {
        m_lockBanner->setText(QStringLiteral("Real CAN bus: every transmitting control is locked. Tools ▸ Service key… unlocks them for this session."));
        m_lockBanner->show();
    } else if (t && t->isReplay()) {
        m_lockBanner->setText(QStringLiteral("Replay: a recorded log commands nothing."));
        m_lockBanner->show();
    } else {
        m_lockBanner->hide();
    }
    if (m_holding && !drive) {
        stopDeadman();
    }
}
