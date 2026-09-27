#include "CommissioningPage.h"

#include "BugReport.h"
#include "ControlsPage.h"
#include "LogIo.h"
#include "Widgets.h"

#include <QComboBox>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>

#include <algorithm>
#include <cmath>
#include <iterator>

namespace {
using Seq = CommissioningSequencer;

struct Choice {
    int routine;
    quint16 attest;
    const char *label, *word, *what, *rig;
};
const char *const PSI_WHAT = "No current (i_d = i_q = 0) while the dyno turns the rotor: ψ from the back-EMF, the electrical zero "
                             "from its phase against the resolver, the resolver's direction against the phase sequence and the "
                             "attested dyno direction.";
// commission.h's routines and the rig each needs (amplitudes and windows: the default service CALs, contract §10g)
const Choice CHOICES[] = {
    {Seq::RoutineRs, Seq::ATTEST_LOCKED, "Rs — stator resistance · rotor locked (LK)", "LOCKED",
     "Two DC current levels (30 A, then 60 A) along phase U's axis into the stator, about 0.5 s: Rs = ΔV / ΔI.",
     "LK — the rotor is held by the rig's brake."},
    {Seq::RoutineLdq, Seq::ATTEST_LOCKED, "Ld, Lq — inductances · rotor locked (LK)", "LOCKED",
     "A 20 A sinusoidal current on a 50 A DC bias, along d and then along q, at standstill, about 0.5 s.",
     "LK — the rotor is held by the rig's brake."},
    {Seq::RoutinePsiZero, Seq::ATTEST_DYNO_FWD, "ψ, electrical zero, direction · dyno forward (DF)", "FORWARD", PSI_WHAT,
     "DF — a dyno drives the rotor forward at a constant 150–450 rpm (at most 0.25 × n_x)."},
    {Seq::RoutinePsiZero, Seq::ATTEST_DYNO_REV, "ψ, electrical zero, direction · dyno reverse (DR)", "REVERSE", PSI_WHAT,
     "DR — a dyno drives the rotor backward at a constant 150–450 rpm (at most 0.25 × n_x)."},
};
const QString STATEMENT = QStringLiteral(
    "The firmware cannot see the brake or the dyno: the attestation is your safety statement about the rig, sent to the "
    "firmware with the start. It still watches the rotor (locked: 2° el and 5 rpm from its start; dyno: within 5 % of its "
    "start speed) and aborts on any deviation, on any lost precondition and when the heartbeat stops.");

QPushButton *button(const QString &text, const QString &name, const QString &tip, const char *style)
{
    auto *b = new QPushButton(text);
    b->setObjectName(QLatin1String(style));
    b->setAccessibleName(name);
    b->setToolTip(tip);
    return b;
}

void setCell(QTableWidget *t, int r, int c, const QString &text, const QColor &fg, const QString &tip)
{
    QTableWidgetItem *it = t->item(r, c);
    if (!it) {
        it = new QTableWidgetItem;
        t->setItem(r, c, it);
    }
    it->setText(text);
    it->setToolTip(tip);
    it->setForeground(fg.isValid() ? QBrush(fg) : QBrush());
}

QTableWidget *table(int rows, const QStringList &header, const QString &name)
{
    auto *t = new QTableWidget(rows, static_cast<int>(header.size()));
    t->setObjectName(name);
    t->setHorizontalHeaderLabels(header);
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    t->horizontalHeader()->setSectionResizeMode(static_cast<int>(header.size()) - 1, QHeaderView::Stretch);
    t->verticalHeader()->hide();
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setSelectionMode(QAbstractItemView::NoSelection);
    t->setShowGrid(false);
    return t;
}

QString amps(qint16 v) { return QString::number(v / 100.0, 'f', 2); }
QString hex8(int v) { return QStringLiteral("0x") + QString::number(v, 16).toUpper().rightJustified(2, QLatin1Char('0')); }

const QString RIPPLE_FORMAT = QStringLiteral(
    "CSV, one row per sample: the electrical angle in ° (the firmware's θ_e, 0 on the d axis) and the torque ripple the "
    "shaft showed there in N·m, measured with no table applied, in the firmware's sign (positive motors forward) — e.g. "
    "\"0,1.25\". Comma, semicolon, tab or space separated; # comments and one header line allowed; at least one sample every "
    "10° el over the period. The tool resamples it to the firmware's 36 points (0, 10 … 350° el, periodic linear "
    "interpolation), removes the mean and writes the i_q that cancels it: i_q = −(T − T̄) / (1.5·pp·ψ) with the record's ψ "
    "and pole pairs (the simulator's hello; over CAN DID 0xFD25), in 0.01 A (±30 A at most). Example: resources/ripple-example.csv. The firmware refuses a value beyond "
    "cal_ripple_ff_max_a (default 0 A: only a zero table) and applies the table from the next key cycle after the commit.");
} // namespace

CommissioningPage::CommissioningPage(Session &s, QWidget *parent)
    : Page(s, QStringLiteral("Commissioning"),
           QStringLiteral("FW-39 motor self-commissioning in the firmware's interlocked service mode: Rs, Ld/Lq, ψ with the "
                          "electrical zero and the resolver direction — one routine at a time, behind SecurityAccess and your "
                          "attestation of the rig. The firmware decides: it re-checks every precondition each millisecond, "
                          "aborts on any loss and never uses a result live."),
           parent),
      m_seq(s)
{
    QVBoxLayout *root = scrollContent();
    auto *grid = new QGridLayout;
    grid->setSpacing(Theme::S3);

    // ---------------- preconditions
    auto *pre = new Card(QStringLiteral("Service mode — the preconditions"));
    m_checks = new CheckList;
    m_checks->setObjectName(QStringLiteral("commissionChecklist"));
    m_checks->setItems(Seq::checkItems());
    pre->body()->addWidget(m_checks);
    auto *preRow = new QHBoxLayout;
    m_arm = button(QStringLiteral("Arm"), QStringLiteral("commissionArm"),
                   QStringLiteral("The arm sequence the Controls page sends (enable, precharge, contactors): the firmware arms to ARMED_ZERO_TORQUE"), "primary");
    m_enableOff = button(QStringLiteral("Withdraw enable"), QStringLiteral("commissionEnableOff"),
                         QStringLiteral("The VCU's enable request off (enable on=false): the service mode takes no torque request"), "");
    m_vspeed0 = button(QStringLiteral("Vehicle speed 0 km/h"), QStringLiteral("commissionVspeedZero"),
                       QStringLiteral("VCU_CMD vehicle speed valid and 0 km/h (vspeed kmh=0)"), "");
    preRow->addWidget(m_arm, 1);
    preRow->addWidget(m_enableOff, 1);
    preRow->addWidget(m_vspeed0, 1);
    pre->body()->addLayout(preRow);
    auto *preHint = new QLabel(QStringLiteral("At the start the firmware also needs HV inside the SKU's window on a proven battery "
                                              "path and the rotor speed the routine needs. It checks everything again every 1 ms "
                                              "while a routine runs; any loss aborts it to the normal safe state (DTC_MC_ABORTED)."));
    preHint->setObjectName(QStringLiteral("hint"));
    preHint->setWordWrap(true);
    pre->body()->addWidget(preHint);
    grid->addWidget(pre, 0, 0);

    // ---------------- the routine
    auto *rc = new Card(QStringLiteral("Routine — one at a time"));
    m_pill = makePill(QStringLiteral("IDLE"), Theme::Sev::Stale);
    rc->addHeaderWidget(m_pill);
    m_routine = new QComboBox;
    m_routine->setAccessibleName(QStringLiteral("commissionRoutine"));
    for (const Choice &c : CHOICES) {
        m_routine->addItem(QString::fromUtf8(c.label));
    }
    rc->body()->addWidget(m_routine);
    m_attest = new QLabel;
    m_attest->setObjectName(QStringLiteral("muted"));
    m_attest->setWordWrap(true);
    rc->body()->addWidget(m_attest);
    auto *runRow = new QHBoxLayout;
    m_start = button(QStringLiteral("Start routine…"), QStringLiteral("commissionStart"),
                     QStringLiteral("SecurityAccess, then RoutineControl 0xF020 with your attestation (typed confirmation)"), "primary");
    m_stop = button(QStringLiteral("Stop"), QStringLiteral("commissionStop"),
                    QStringLiteral("RoutineControl stop (31 02 F0 20): the routine ends at the firmware's next 1 ms task, without a DTC"), "");
    runRow->addWidget(m_start, 1);
    runRow->addWidget(m_stop, 1);
    rc->body()->addLayout(runRow);
    m_map = button(QStringLiteral("Map L_d/L_q (6 bias points)…"), QStringLiteral("commissionMap"),
                   QStringLiteral("FW-45: the Ld/Lq routine on the locked rotor (LK) at the six bias currents of the saturation "
                                  "map, one after the other, each behind its own unlock and heartbeat (typed confirmation)"), "");
    rc->body()->addWidget(m_map);
    m_progress = new KvGrid;
    m_progress->addRow(QStringLiteral("state"), QStringLiteral("Service mode (firmware, results index 0)"));
    m_progress->addRow(QStringLiteral("reason"), QStringLiteral("Last refusal or abort"));
    m_progress->addRow(QStringLiteral("elapsed"), QStringLiteral("Elapsed since the start"), QString(), true);
    m_progress->addRow(QStringLiteral("hb"), QStringLiteral("Heartbeat: results polls every 50 ms"), QString(), true);
    m_progress->setStale(false);
    rc->body()->addWidget(m_progress);
    m_outcome = new QLabel;
    m_outcome->setObjectName(QStringLiteral("muted"));
    m_outcome->setWordWrap(true);
    m_outcome->setTextInteractionFlags(Qt::TextSelectableByMouse);
    rc->body()->addWidget(m_outcome);
    grid->addWidget(rc, 0, 1);

    // ---------------- results
    auto *res = new Card(QStringLiteral("Results — never used live"));
    m_table = new QTableWidget(Seq::QTY + 1, 6);
    m_table->setObjectName(QStringLiteral("commissionResults"));
    m_table->setHorizontalHeaderLabels({QStringLiteral("Quantity"), QStringLiteral("Value"), QStringLiteral("± u (1 σ)"),
                                        QStringLiteral("Verdict"), QStringLiteral("Band and staging"), QStringLiteral("From")});
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_table->verticalHeader()->hide();
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setShowGrid(false);
    m_table->setMinimumHeight(240);
    res->body()->addWidget(m_table);
    m_staged = new QLabel;
    m_staged->setObjectName(QStringLiteral("muted"));
    m_staged->setWordWrap(true);
    res->body()->addWidget(m_staged);
    auto *resRow = new QHBoxLayout;
    m_commit = button(QStringLiteral("Commit to calibration record…"), QStringLiteral("commissionCommit"),
                      QStringLiteral("RoutineControl 0xF021 after a fresh unlock: the staged values into a new FW-20 record version, "
                                     "used from the next key cycle (typed confirmation)"), "danger");
    auto *json = button(QStringLiteral("Export JSON…"), QStringLiteral("commissionExportJson"),
                        QStringLiteral("Every operation of this session and the results, with the firmware identity"), "");
    auto *csv = button(QStringLiteral("Export CSV…"), QStringLiteral("commissionExportCsv"),
                       QStringLiteral("The results table, one row per quantity, SI units"), "");
    resRow->addWidget(m_commit);
    resRow->addStretch(1);
    resRow->addWidget(json);
    resRow->addWidget(csv);
    res->body()->addLayout(resRow);
    auto *resHint = new QLabel(QStringLiteral("A VALID value inside the FW-20 class limits and within its band of the active record "
                                              "(Rs 10 %, Ld/Lq 10 %, ψ 5 %, the zero 2° el with the default CALs) is staged; one "
                                              "beyond the band waits for a second run that agrees within 3 combined standard "
                                              "uncertainties, then their mean is staged. The commit seals the staged values into "
                                              "a new record version; the next key cycle validates it before anything arms."));
    resHint->setObjectName(QStringLiteral("hint"));
    resHint->setWordWrap(true);
    res->body()->addWidget(resHint);
    grid->addWidget(res, 1, 0, 1, 2);

    // ---------------- FW-45: the saturation map
    auto *mc = new Card(QStringLiteral("Saturation map (FW-45) — measured at the bias, and the active record's"));
    auto *maps = new QHBoxLayout;
    for (int ax = 0; ax < 2; ax++) {
        m_mapTable[ax] = table(Seq::MAP_N, {QStringLiteral("k"), ax ? QStringLiteral("i_q bias") : QStringLiteral("i_d bias"),
                                            ax ? QStringLiteral("L_q ± u (run)") : QStringLiteral("L_d ± u (run)"), QStringLiteral("Record"),
                                            QStringLiteral("Verdict · staging")},
                               ax ? QStringLiteral("commissionMapQ") : QStringLiteral("commissionMapD"));
        m_mapTable[ax]->setMinimumHeight(240); // the six points without a scroll bar
        maps->addWidget(m_mapTable[ax], 1);
    }
    mc->body()->addLayout(maps);
    m_mapStaged = new QLabel;
    m_mapStaged->setObjectName(QStringLiteral("muted"));
    m_mapStaged->setWordWrap(true);
    mc->body()->addWidget(m_mapStaged);
    auto *mapHint = new QLabel(QStringLiteral(
        "Bias index k runs the d axis at i_d = −b and the q axis at i_q = +b: b = k/5 of the SKU's current limit, at least "
        "50 A, at most the limit (the d run also the record's demagnetisation limit) less the 20 A injection. Each point is "
        "judged against the record's map at that current (10 % band; beyond it a second, agreeing run). The commit turns a "
        "whole staged axis — all six points — into the record's apparent-inductance map (trapezoids from the differential "
        "values) and refuses a partial one; calib_check refuses a map that rises with the current."));
    mapHint->setObjectName(QStringLiteral("hint"));
    mapHint->setWordWrap(true);
    mc->body()->addWidget(mapHint);
    grid->addWidget(mc, 2, 0, 1, 2);

    // ---------------- FW-46: the torque-ripple table
    auto *rp = new Card(QStringLiteral("Torque-ripple feed-forward (FW-46, DID 0xFD46)"));
    auto *ripRow = new QHBoxLayout;
    m_ripImport = button(QStringLiteral("Import ripple map…"), QStringLiteral("commissionRippleImport"),
                         QStringLiteral("A CSV of (electrical angle °, torque ripple N·m) made into the 36-point i_q table"), "");
    m_ripWrite = button(QStringLiteral("Write + read back"), QStringLiteral("commissionRippleWrite"),
                        QStringLiteral("SecurityAccess, 2E FD 46 (one segmented request), then 22 FD 46: staged for the next commit"), "primary");
    m_ripRead = button(QStringLiteral("Read table"), QStringLiteral("commissionRippleRead"),
                       QStringLiteral("22 FD 46: the table the next commit writes — the staged one, else the active record's"), "");
    ripRow->addWidget(m_ripImport, 1);
    ripRow->addWidget(m_ripWrite, 1);
    ripRow->addWidget(m_ripRead, 1);
    rp->body()->addLayout(ripRow);
    m_ripInfo = new QLabel;
    m_ripInfo->setObjectName(QStringLiteral("muted"));
    m_ripInfo->setWordWrap(true);
    m_ripInfo->setTextInteractionFlags(Qt::TextSelectableByMouse);
    rp->body()->addWidget(m_ripInfo);
    m_ripTable = table(Seq::RIPPLE_N, {QStringLiteral("θ_e"), QStringLiteral("Imported i_q (A)"), QStringLiteral("Read back (A)"),
                                       QStringLiteral("")},
                       QStringLiteral("commissionRippleTable"));
    m_ripTable->setMinimumHeight(200);
    rp->body()->addWidget(m_ripTable);
    auto *ripHint = new QLabel(RIPPLE_FORMAT);
    ripHint->setObjectName(QStringLiteral("hint"));
    ripHint->setWordWrap(true);
    rp->body()->addWidget(ripHint);
    grid->addWidget(rp, 3, 0, 1, 2);

    // ---------------- history
    auto *hc = new Card(QStringLiteral("This session's operations"));
    m_history = new QListWidget;
    m_history->setMinimumHeight(110);
    hc->body()->addWidget(m_history);
    grid->addWidget(hc, 4, 0, 1, 2);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    root->addLayout(grid);
    root->addStretch(1);

    // ---------------- behaviour (no torque command anywhere on this page)
    connect(m_arm, &QPushButton::clicked, this, [this] { m_s.command(QStringLiteral("arm")); });
    connect(m_enableOff, &QPushButton::clicked, this, [this] { m_s.command(QStringLiteral("enable"), {{QStringLiteral("on"), false}}); });
    connect(m_vspeed0, &QPushButton::clicked, this, [this] { m_s.command(QStringLiteral("vspeed"), {{QStringLiteral("kmh"), 0.0}}); });
    connect(m_start, &QPushButton::clicked, this, &CommissioningPage::startRoutine);
    connect(m_map, &QPushButton::clicked, this, &CommissioningPage::startMap);
    connect(m_ripImport, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Import a torque-ripple map"), m_ripFile,
                                                          QStringLiteral("CSV (*.csv *.txt);;All files (*)"));
        if (!path.isEmpty()) {
            importRipple(path);
        }
    });
    connect(m_ripWrite, &QPushButton::clicked, this, [this] { m_seq.writeRipple(m_ripple.table); });
    connect(m_ripRead, &QPushButton::clicked, &m_seq, &CommissioningSequencer::readRipple);
    connect(&m_s, &Session::infoChanged, this, [this] { // ψ, pp, cal_ripple_ff_max_a, the active record's maps
        renderMap();
        renderRipple();
    });
    connect(m_stop, &QPushButton::clicked, &m_seq, &CommissioningSequencer::stop);
    connect(m_commit, &QPushButton::clicked, this, &CommissioningPage::commit);
    connect(json, &QPushButton::clicked, this, &CommissioningPage::exportJson);
    connect(csv, &QPushButton::clicked, this, &CommissioningPage::exportCsv);
    connect(m_routine, &QComboBox::currentIndexChanged, this, [this](int i) {
        const Choice &c = CHOICES[std::clamp(i, 0, static_cast<int>(std::size(CHOICES)) - 1)];
        m_attest->setText(QStringLiteral("Attestation %1 %2").arg(QString::fromUtf8(c.rig), STATEMENT));
    });
    connect(&m_seq, &CommissioningSequencer::changed, this, [this] {
        renderLive();
        renderResults();
    });
    connect(&m_s, &Session::tick, this, &CommissioningPage::renderLive);
    connect(&m_s, &Session::serviceChanged, this, &CommissioningPage::renderLive);
    m_attest->setText(QStringLiteral("Attestation %1 %2").arg(QString::fromUtf8(CHOICES[0].rig), STATEMENT));
    renderLive();
    renderResults();
}

void CommissioningPage::applyTheme() { renderResults(); }

void CommissioningPage::renderLive()
{
    const auto c = m_seq.checks();
    const auto &items = Seq::checkItems();
    QString why;
    for (int i = 0; i < c.size(); i++) {
        const CheckList::State st = (c[i].first == Seq::Check::Ok)     ? CheckList::State::Ok
                                  : (c[i].first == Seq::Check::Fail) ? CheckList::State::Fail
                                                                      : CheckList::State::Unknown;
        m_checks->set(items[i].first, st, c[i].second);
        if (why.isEmpty() && st == CheckList::State::Fail) {
            why = items[i].second.section(QLatin1Char('|'), 0, 0);
        }
    }
    const bool live = m_s.isDeviceLive(), allowed = m_s.destructiveAllowed(), busy = m_seq.busy();
    m_checks->setStale(!live);
    const int sm = m_s.hasFrame() ? static_cast<int>(m_s.last().pick({"state.sm", "inv.state"}, -1)) : -1;
    m_arm->setEnabled(live && allowed && !busy && (sm == 3 || sm == 4)); // the firmware waits for the vehicle's arm
    m_enableOff->setEnabled(live && allowed && !busy);
    m_vspeed0->setEnabled(live && allowed && !busy);
    m_start->setEnabled(m_seq.readyToStart());
    m_start->setToolTip(why.isEmpty() ? QStringLiteral("SecurityAccess, then RoutineControl 0xF020 with your attestation (typed confirmation)")
                                      : QStringLiteral("Not met: %1").arg(why));
    m_stop->setEnabled(m_seq.running());
    m_commit->setEnabled(live && !busy && m_s.serviceUnlocked());
    m_map->setEnabled(m_seq.readyToStart());
    m_map->setToolTip(why.isEmpty() ? QStringLiteral("FW-45: the Ld/Lq routine (LK) at the six bias currents of the saturation map (typed confirmation)")
                                    : QStringLiteral("Not met: %1").arg(why));
    m_ripImport->setEnabled(!busy);
    m_ripWrite->setEnabled(live && !busy && m_s.serviceUnlocked() && m_ripple.table.size() == Seq::RIPPLE_N);
    m_ripRead->setEnabled(live && !busy && m_s.serviceUnlocked());
    m_routine->setEnabled(!busy);
    const qint64 el = m_seq.elapsedMs();
    m_progress->setValue(QStringLiteral("elapsed"), el < 0 ? QStringLiteral("—") : QStringLiteral("%1 s").arg(el / 1000.0, 0, 'f', 2));
    const int missed = m_seq.missedPolls();
    m_progress->setValue(QStringLiteral("hb"), missed ? QStringLiteral("%1 (%2 unanswered in a row)").arg(m_seq.polls()).arg(missed)
                                                     : QString::number(m_seq.polls()),
                         missed ? Theme::Sev::Warn : Theme::Sev::Neutral);
}

void CommissioningPage::renderResults()
{
    QString pill = QStringLiteral("IDLE");
    Theme::Sev sev = Theme::Sev::Stale;
    if (m_seq.running()) {
        pill = QStringLiteral("RUNNING");
        sev = Theme::Sev::Accent;
    } else if (m_seq.busy()) {
        pill = QStringLiteral("BUSY");
        sev = Theme::Sev::Info;
    } else {
        switch (m_seq.outcome()) {
        case Seq::Outcome::None: break;
        case Seq::Outcome::Done: pill = QStringLiteral("DONE"); sev = Theme::Sev::Ok; break;
        case Seq::Outcome::Aborted: pill = QStringLiteral("ABORTED"); sev = Theme::Sev::Warn; break;
        case Seq::Outcome::Refused: pill = QStringLiteral("REFUSED"); sev = Theme::Sev::Warn; break;
        case Seq::Outcome::Committed: pill = QStringLiteral("COMMITTED"); sev = Theme::Sev::Ok; break;
        case Seq::Outcome::Failed: pill = QStringLiteral("FAILED"); sev = Theme::Sev::Crit; break;
        }
    }
    setPill(m_pill, pill, sev);
    const bool read = m_seq.fwState() >= 0;
    m_progress->setValue(QStringLiteral("state"), Seq::stateName(m_seq.fwState()), m_seq.running() ? Theme::Sev::Accent : Theme::Sev::Neutral);
    m_progress->setValue(QStringLiteral("reason"), read ? Seq::reasonText(m_seq.fwReason()) : QStringLiteral("—"),
                         (read && m_seq.fwReason() != 0) ? Theme::Sev::Warn : Theme::Sev::Neutral);
    m_outcome->setText(m_seq.text());

    const QColor ok = Theme::color("ok"), warn = Theme::color("warn"), crit = Theme::color("crit"), dim = Theme::color("dim");
    for (int q = 0; q < Seq::QTY; q++) {
        const Seq::Quantity &v = m_seq.quantity(q);
        const bool has = v.verdict != 0;
        setCell(m_table, q, 0, Seq::qtyName(q), QColor(), QString());
        setCell(m_table, q, 1, has ? Seq::valueText(q, v.value) : QStringLiteral("—"), QColor(), QString());
        setCell(m_table, q, 2, has ? QStringLiteral("± ") + Seq::valueText(q, v.u) : QStringLiteral("—"), QColor(), QString());
        setCell(m_table, q, 3, Seq::verdictName(v.verdict), !has ? dim : (v.verdict == 1 ? ok : warn), Seq::verdictText(v.verdict));
        setCell(m_table, q, 4, Seq::flagsText(v.flags), (v.flags & 1) ? warn : ((v.flags & 2) ? ok : QColor()), QString());
        setCell(m_table, q, 5, v.op ? QStringLiteral("#%1").arg(v.op) : (has ? QStringLiteral("before this session") : QStringLiteral("—")),
                QColor(), QString());
    }
    // the direction: the psi/zero run's DIR_PHASES / DIR_DYNO verdicts (commission.h), under the attested dyno direction
    const int d = Seq::QTY;
    const Seq::Quantity &psi = m_seq.quantity(3);
    const bool dirBad = psi.verdict == 6 || psi.verdict == 7;
    const quint16 da = m_seq.lastDynoAttest();
    setCell(m_table, d, 0, QStringLiteral("Direction (resolver)"), QColor(), QString());
    setCell(m_table, d, 1, da == Seq::ATTEST_DYNO_FWD ? QStringLiteral("forward (DF)") : (da == Seq::ATTEST_DYNO_REV ? QStringLiteral("reverse (DR)") : QStringLiteral("—")),
            QColor(), QStringLiteral("The dyno direction attested for the last ψ/zero run of this session"));
    setCell(m_table, d, 2, QString(), QColor(), QString());
    setCell(m_table, d, 3, psi.verdict == 0 ? QStringLiteral("—") : (dirBad ? Seq::verdictName(psi.verdict) : QStringLiteral("consistent")),
            psi.verdict == 0 ? dim : (dirBad ? crit : ok),
            dirBad ? Seq::verdictText(psi.verdict) : QStringLiteral("The resolver turns with the phase sequence and the attested dyno direction"));
    setCell(m_table, d, 4, QString(), QColor(), QString());
    setCell(m_table, d, 5, psi.op ? QStringLiteral("#%1").arg(psi.op) : QStringLiteral("—"), QColor(), QString());

    const int mask = m_seq.stagedMask();
    QStringList staged;
    for (int q = 0; q < Seq::QTY; q++) {
        if (mask & (1 << q)) {
            staged << Seq::qtyName(q);
        }
    }
    if (mask & 0x20) {
        staged << QStringLiteral("map points (L_d %1, L_q %2)").arg(hex8(m_seq.mapStaged(0)), hex8(m_seq.mapStaged(1)));
    }
    if (mask & 0x40) {
        staged << QStringLiteral("the ripple table");
    }
    m_staged->setText(QStringLiteral("Staged for the next record: %1 · committed in this key cycle: %2 (results index 1, as last read)")
                          .arg(staged.isEmpty() ? QStringLiteral("nothing") : staged.join(QStringLiteral(", ")),
                               (mask & 0x80) ? QStringLiteral("yes") : QStringLiteral("no")));

    const QVector<Seq::Entry> &h = m_seq.history();
    while (m_history->count() < h.size()) {
        m_history->addItem(new QListWidgetItem);
    }
    for (int i = 0; i < h.size(); i++) {
        const Seq::Entry &e = h[i];
        QListWidgetItem *it = m_history->item(i);
        QString what = Seq::opName(e);
        what[0] = what[0].toUpper();
        it->setText(QStringLiteral("#%1  %2%3 — %4")
                        .arg(e.n)
                        .arg(what,
                             e.attest ? QStringLiteral(" (%1)").arg(Seq::attestName(e.attest)) : QString(),
                             e.outcome == Seq::Outcome::None ? QStringLiteral("in progress") : e.text));
        it->setForeground((e.outcome == Seq::Outcome::Failed) ? crit
                          : (e.outcome == Seq::Outcome::Aborted || e.outcome == Seq::Outcome::Refused) ? warn
                                                                                                         : Theme::color("text"));
    }
    renderMap();
    renderRipple();
}

void CommissioningPage::renderMap()
{
    const QColor ok = Theme::color("ok"), warn = Theme::color("warn"), dim = Theme::color("dim"), accent = Theme::color("accent");
    const QJsonObject maps = m_s.deviceInfo().value(QLatin1String("maps")).toObject(); // the active record's (Session: DID 0xFD26)
    for (int ax = 0; ax < 2; ax++) {
        for (int k = 0; k < Seq::MAP_N; k++) {
            const Seq::MapPoint &p = m_seq.mapPoint(ax, k);
            const bool has = p.verdict != 0, now = m_seq.mapIndex() == k;
            const double b = m_seq.mapBiasA(k, ax == 0);
            QTableWidget *t = m_mapTable[ax];
            setCell(t, k, 0, QString::number(k), now ? accent : QColor(), now ? QStringLiteral("running now") : QString());
            setCell(t, k, 1, std::isnan(b) ? QStringLiteral("—") : QStringLiteral("%1%2 A").arg(ax ? QStringLiteral("+") : QStringLiteral("−")).arg(b, 0, 'f', 1),
                    QColor(), QStringLiteral("commission.c map_bias(): the DC bias of this run"));
            setCell(t, k, 2, has ? QStringLiteral("%1 ± %2").arg(Seq::valueText(ax + 1, p.value), QString::number(p.u / 10.0, 'f', 1)) : QStringLiteral("—"), QColor(),
                    QStringLiteral("results index 0x%1: the differential inductance the last run at this bias measured, ± its standard uncertainty")
                        .arg(ax ? 0x50 + k : 0x40 + k, 2, 16));
            const QJsonArray rec = maps.value(QLatin1String(ax ? "lq_h" : "ld_h")).toArray();
            setCell(t, k, 3, k < rec.size() ? QStringLiteral("%1 µH").arg(rec[k].toDouble() * 1e6, 0, 'f', 2) : QStringLiteral("—"), QColor(),
                    QStringLiteral("The active record's map (DID 0xFD26): the apparent inductance at breakpoint %1 = %2 A, which the "
                                   "next commit replaces with the staged points (trapezoids over them)")
                        .arg(k).arg(maps.value(QLatin1String("i_map_a")).toDouble() * k / (Seq::MAP_N - 1), 0, 'f', 1));
            setCell(t, k, 4, has ? QStringLiteral("%1 · %2").arg(Seq::verdictName(p.verdict), Seq::flagsText(p.flags)) : QStringLiteral("—"),
                    !has ? dim : (p.verdict != 1 || (p.flags & 1)) ? warn : ((p.flags & 2) ? ok : QColor()), Seq::verdictText(p.verdict));
        }
    }
    m_mapStaged->setText(QStringLiteral("Staged points (results index 0x02, as last read): L_d %1, L_q %2 of 0x3F%3 · Record: the active "
                                        "record's map (%4), breakpoints k/5 of i_map %5")
                             .arg(hex8(m_seq.mapStaged(0)), hex8(m_seq.mapStaged(1)))
                             .arg(m_seq.mapIndex() >= 0 ? QStringLiteral(" · sweeping: bias index %1").arg(m_seq.mapIndex()) : QString(),
                                  maps.isEmpty() ? QStringLiteral("DID 0xFD26 not read") : maps.value(QLatin1String("source")).toString(),
                                  maps.isEmpty() ? QStringLiteral("—") : QStringLiteral("%1 A").arg(maps.value(QLatin1String("i_map_a")).toDouble(), 0, 'f', 1)));
}

void CommissioningPage::renderRipple()
{
    const QVector<qint16> &rd = m_seq.rippleRead();
    const QColor ok = Theme::color("ok"), crit = Theme::color("crit");
    for (int k = 0; k < Seq::RIPPLE_N; k++) {
        const bool have = m_ripple.table.size() == Seq::RIPPLE_N, back = rd.size() == Seq::RIPPLE_N;
        setCell(m_ripTable, k, 0, QStringLiteral("%1°").arg(k * 360 / Seq::RIPPLE_N), QColor(), QString());
        setCell(m_ripTable, k, 1, have ? amps(m_ripple.table[k]) : QStringLiteral("—"), QColor(), QString());
        setCell(m_ripTable, k, 2, back ? amps(rd[k]) : QStringLiteral("—"), QColor(), QString());
        const bool cmp = have && back;
        setCell(m_ripTable, k, 3, !cmp ? QString() : (rd[k] == m_ripple.table[k] ? QStringLiteral("=") : QStringLiteral("≠")),
                !cmp ? QColor() : (rd[k] == m_ripple.table[k] ? ok : crit), QString());
    }
    QStringList l;
    if (!m_ripple.error.isEmpty()) {
        l << QStringLiteral("%1: %2").arg(QFileInfo(m_ripFile).fileName(), m_ripple.error);
    } else if (!m_ripple.table.isEmpty()) {
        l << QStringLiteral("%1: %2 samples; mean %3 N·m removed; 1.5·pp·ψ = %4 N·m/A; peak %5 A at %6° el")
                 .arg(QFileInfo(m_ripFile).fileName()).arg(m_ripple.rows).arg(m_ripple.meanNm, 0, 'f', 3)
                 .arg(m_ripple.nmPerA, 0, 'f', 4).arg(m_ripple.peakA, 0, 'f', 2).arg(m_ripple.peakDeg, 0, 'f', 0);
    } else {
        l << QStringLiteral("No table imported.");
    }
    const ParamModel &pm = m_s.params();
    const int i = pm.indexOf(QStringLiteral("cal_ripple_ff_max_a"));
    if (i >= 0 && pm.haveDevice()) {
        const double lim = pm.device(i);
        l << QStringLiteral("cal_ripple_ff_max_a on the device: %1 A%2").arg(lim, 0, 'f', 2)
                 .arg(!m_ripple.table.isEmpty() && std::abs(m_ripple.peakA) > lim
                          ? QStringLiteral(" — below this table's peak: the firmware refuses it (NRC 0x31); raise it on the Parameters page first")
                          : QString());
    }
    if (rd.size() == Seq::RIPPLE_N && m_ripple.table.size() == Seq::RIPPLE_N) {
        const auto same = std::equal(rd.begin(), rd.end(), m_ripple.table.begin());
        l << (same ? QStringLiteral("Read back: equal to the imported table.") : QStringLiteral("Read back: differs from the imported table."));
    }
    m_ripInfo->setText(l.join(QStringLiteral(" · ")));
}

bool CommissioningPage::importRipple(const QString &path)
{
    QFile f(path);
    m_ripFile = path;
    if (!f.open(QIODevice::ReadOnly)) {
        m_ripple = Seq::Ripple();
        m_ripple.error = f.errorString();
    } else {
        const QJsonObject motor = m_s.deviceInfo().value(QLatin1String("motor")).toObject();
        m_ripple = Seq::rippleFromCsv(f.readAll(), motor.value(QLatin1String("psi_wb")).toDouble(), motor.value(QLatin1String("pp")).toInt());
    }
    m_s.addEvent(m_ripple.error.isEmpty() ? QStringLiteral("info") : QStringLiteral("warn"),
                 m_ripple.error.isEmpty() ? QStringLiteral("ripple map imported: %1 (peak %2 A)").arg(path).arg(m_ripple.peakA, 0, 'f', 2)
                                          : QStringLiteral("ripple map not imported: %1: %2").arg(path, m_ripple.error));
    renderRipple();
    renderLive();
    return m_ripple.error.isEmpty();
}

void CommissioningPage::startRoutine()
{
    const Choice &c = CHOICES[std::clamp(m_routine->currentIndex(), 0, static_cast<int>(std::size(CHOICES)) - 1)];
    const QString text = QStringLiteral("%1\n\nYour attestation: %2\n\n%3")
                             .arg(QString::fromUtf8(c.what), QString::fromUtf8(c.rig), STATEMENT);
    if (ControlsPage::confirmTyped(this, QStringLiteral("Start the %1 routine").arg(Seq::routineName(c.routine)), text,
                                   QLatin1String(c.word))) {
        m_seq.start(c.routine, c.attest); // a refusal (the tool's or the firmware's) comes back through changed()
    }
}

void CommissioningPage::startMap()
{
    QStringList b;
    for (int k = 0; k < Seq::MAP_N; k++) {
        b << QStringLiteral("%1: i_d −%2 A / i_q +%3 A").arg(k).arg(m_seq.mapBiasA(k, true), 0, 'f', 1).arg(m_seq.mapBiasA(k, false), 0, 'f', 1);
    }
    const QString text = QStringLiteral("FW-45: the Ld/Lq routine at each of the six bias indices, one after the other — a 20 A "
                                        "sinusoidal current on the DC bias, along d, then along q, at standstill, about 0.5 s each; a "
                                        "point beyond the band gets one confirming run. The q runs put torque on the shaft.\n\n%1\n\n"
                                        "Your attestation: %2\n\n%3")
                             .arg(b.join(QLatin1Char('\n')), QString::fromUtf8(CHOICES[1].rig), STATEMENT);
    if (ControlsPage::confirmTyped(this, QStringLiteral("Map L_d/L_q (6 bias points)"), text, QStringLiteral("LOCKED"))) {
        m_seq.startMap();
    }
}

void CommissioningPage::commit()
{
    QStringList staged;
    for (int q = 0; q < Seq::QTY; q++) {
        if (m_seq.stagedMask() & (1 << q)) {
            staged << Seq::qtyName(q);
        }
    }
    if (m_seq.stagedMask() & 0x20) {
        staged << QStringLiteral("the L_d/L_q map points (a staged axis becomes its apparent-inductance map)");
    }
    if (m_seq.stagedMask() & 0x40) {
        staged << QStringLiteral("the torque-ripple table");
    }
    const QString text =
        QStringLiteral("Seal %1 into a new FW-20 calibration record version: the active record is copied, the staged values put "
                       "in, sealed and checked (CRC, ranges, SKU, serial = the device UID, motor ID) and queued to NVM "
                       "(DTC_MC_CAL_WRITTEN). A stored MTPA table solved for the old Ld, Lq or ψ is dropped.\n\n"
                       "The running key cycle keeps its record: the next key cycle's init validates the new record (FW-20) "
                       "before anything arms (FW-24).")
            .arg(staged.isEmpty() ? QStringLiteral("the staged values (none known to this tool: the firmware refuses when nothing is staged)")
                                  : QStringLiteral("the staged values (%1)").arg(staged.join(QStringLiteral(", "))));
    if (ControlsPage::confirmTyped(this, QStringLiteral("Commit to calibration record"), text, QStringLiteral("COMMIT"))) {
        m_seq.commit();
    }
}

void CommissioningPage::exportJson()
{
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export commissioning results"),
        QStringLiteral("traction-commissioning-%1.json").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))),
        QStringLiteral("JSON (*.json)"));
    if (path.isEmpty()) {
        return;
    }
    QString err;
    if (!BugReport::save(path, m_seq.toJson(), &err)) {
        QMessageBox::warning(this, QStringLiteral("Export JSON"), err);
        return;
    }
    m_s.addEvent(QStringLiteral("info"), QStringLiteral("commissioning results saved: %1").arg(path));
}

void CommissioningPage::exportCsv()
{
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export commissioning results"),
        QStringLiteral("traction-commissioning-%1.csv").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))),
        QStringLiteral("CSV (*.csv)"));
    if (path.isEmpty()) {
        return;
    }
    QString err;
    if (!LogIo::saveCsv(path, m_seq.csvRows(), Seq::csvColumns(), &err)) {
        QMessageBox::warning(this, QStringLiteral("Export CSV"), err);
        return;
    }
    m_s.addEvent(QStringLiteral("info"), QStringLiteral("commissioning results saved: %1").arg(path));
}
