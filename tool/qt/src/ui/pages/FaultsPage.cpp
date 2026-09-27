#include "FaultsPage.h"

#include "BugReport.h"
#include "ProtocolInfo.h"
#include "Timeline.h"
#include "Widgets.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTextBrowser>

namespace {
Theme::Sev sevOfClass(const QString &cls)
{
    if (cls == QLatin1String("fault") || cls == QLatin1String("service")) {
        return Theme::Sev::Crit;
    }
    if (cls == QLatin1String("no_arm") || cls == QLatin1String("degrade")) {
        return Theme::Sev::Warn;
    }
    return Theme::Sev::Info;
}

QString statusText(int st)
{
    QStringList s;
    if (st & 0x01) s << QStringLiteral("active");
    if (st & 0x08) s << QStringLiteral("confirmed");
    if ((st & 0x04) && !(st & 0x08)) s << QStringLiteral("pending");
    if (!(st & 0x01) && (st & 0x20)) s << QStringLiteral("failed since clear");
    return s.isEmpty() ? QStringLiteral("0x%1").arg(st, 2, 16, QLatin1Char('0')) : s.join(QStringLiteral(" · "));
}
} // namespace

FaultsPage::FaultsPage(Session &s, QWidget *parent)
    : Page(s, QStringLiteral("Faults & alerts"),
           QStringLiteral("DTCs as the firmware reports them (UDS code 0xD10000 | id), what the firmware does about each "
                          "one, the history of this session, and your own alert rules."),
           parent)
{
    QVBoxLayout *root = scrollContent();
    auto *dtcCard = new Card(QStringLiteral("Diagnostic trouble codes"));
    m_dtcSummary = new QLabel(QStringLiteral("No data"));
    m_dtcSummary->setObjectName(QStringLiteral("muted"));
    dtcCard->addHeaderWidget(m_dtcSummary);
    m_clear = new QPushButton(QStringLiteral("Clear DTCs (simulator)"));
    m_clear->setToolTip(QStringLiteral("Clears the clearable DTCs through the firmware's UDS 0x14 (SecurityAccess, HV absent, bridge disarmed); latched DESAT-class and arm-forbidding DTCs are kept"));
    auto *bug = new QPushButton(QStringLiteral("Bug report…"));
    bug->setObjectName(QStringLiteral("primary"));
    bug->setIcon(Theme::icon(QStringLiteral("bug"), Theme::color("onAccent")));
    bug->setToolTip(QStringLiteral("One JSON file: state, the last N seconds of telemetry, parameters, firmware ID, DTCs, alerts (Ctrl+B)"));
    dtcCard->addHeaderWidget(m_clear);
    dtcCard->addHeaderWidget(bug);
    auto *split = new QSplitter(Qt::Horizontal);
    m_dtcs = new QTableWidget(0, 7);
    m_dtcs->setObjectName(QStringLiteral("dtcTable"));
    m_dtcs->setHorizontalHeaderLabels({QStringLiteral("Code"), QStringLiteral("DTC"), QStringLiteral("Class"), QStringLiteral("Status"),
                                       QStringLiteral("Occ."), QStringLiteral("First (s)"), QStringLiteral("Last (s)")});
    m_dtcs->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_dtcs->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_dtcs->verticalHeader()->hide();
    m_dtcs->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_dtcs->setSelectionMode(QAbstractItemView::SingleSelection);
    m_dtcs->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_dtcs->setMinimumHeight(220);
    m_dtcs->setShowGrid(false);
    m_dtcDetail = new QTextBrowser;
    m_dtcDetail->setMinimumWidth(320);
    split->addWidget(m_dtcs);
    split->addWidget(m_dtcDetail);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    split->setSizes({700, 460});
    dtcCard->body()->addWidget(split);
    root->addWidget(dtcCard);

    auto *tlCard = new Card(QStringLiteral("History (this session)"));
    m_timeline = new Timeline;
    tlCard->body()->addWidget(m_timeline);
    root->addWidget(tlCard);

    auto *alCard = new Card(QStringLiteral("Alert rules"));
    auto *form = new QHBoxLayout;
    form->setSpacing(Theme::S2);
    m_ruleChannel = new QComboBox;
    m_ruleChannel->setEditable(true);
    m_ruleChannel->setMinimumWidth(220);
    m_ruleChannel->setCurrentText(QStringLiteral("temps.mod_max_c"));
    m_ruleChannel->completer()->setCompletionMode(QCompleter::PopupCompletion);
    m_ruleChannel->completer()->setFilterMode(Qt::MatchContains);
    m_ruleOp = new QComboBox;
    for (const char *op : {">", ">=", "<", "<=", "==", "!=", "|x|>"}) {
        m_ruleOp->addItem(QLatin1String(op));
    }
    m_ruleThr = new QDoubleSpinBox;
    m_ruleThr->setRange(-1e9, 1e9);
    m_ruleThr->setDecimals(3);
    m_ruleThr->setValue(100);
    m_ruleHyst = new QDoubleSpinBox;
    m_ruleHyst->setRange(0, 1e9);
    m_ruleHyst->setDecimals(3);
    m_ruleHyst->setPrefix(QStringLiteral("hyst "));
    m_ruleHold = new QDoubleSpinBox;
    m_ruleHold->setRange(0, 600000);
    m_ruleHold->setDecimals(0);
    m_ruleHold->setSuffix(QStringLiteral(" ms"));
    m_ruleHold->setPrefix(QStringLiteral("hold "));
    m_ruleSev = new QComboBox;
    m_ruleSev->addItems({QStringLiteral("warn"), QStringLiteral("crit"), QStringLiteral("info")});
    m_ruleNote = new QLineEdit;
    m_ruleNote->setPlaceholderText(QStringLiteral("note"));
    auto *add = new QPushButton(QStringLiteral("Add rule"));
    for (QWidget *w : std::initializer_list<QWidget *>{m_ruleChannel, m_ruleOp, m_ruleThr, m_ruleHyst, m_ruleHold, m_ruleSev, m_ruleNote, add}) {
        form->addWidget(w);
    }
    alCard->body()->addLayout(form);
    auto *alSplit = new QSplitter(Qt::Horizontal);
    m_rules = new QTableWidget(0, 4);
    m_rules->setHorizontalHeaderLabels({QStringLiteral("On"), QStringLiteral("Rule"), QStringLiteral("Severity"), QString()});
    m_rules->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_rules->verticalHeader()->hide();
    m_rules->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_rules->setMinimumHeight(160);
    m_rules->setShowGrid(false);
    m_alertLog = new QListWidget;
    m_alertLog->setWordWrap(true);
    alSplit->addWidget(m_rules);
    alSplit->addWidget(m_alertLog);
    alCard->body()->addWidget(alSplit);
    root->addWidget(alCard);
    root->addStretch(1);

    connect(m_dtcs, &QTableWidget::currentCellChanged, this, &FaultsPage::showDtc);
    connect(m_clear, &QPushButton::clicked, this, [this] { m_s.command(QStringLiteral("dtc_clear")); });
    connect(bug, &QPushButton::clicked, this, &FaultsPage::saveBugReport);
    connect(add, &QPushButton::clicked, this, &FaultsPage::addRule);
    onFrames([this](const TelemetryFrame &) { rebuildDtcs(); });
    connect(&m_s, &Session::dtcEvents, this, &FaultsPage::refreshTimeline);
    connect(&m_s, &Session::alertRaised, this, &FaultsPage::addAlertLog);
    connect(&m_s, &Session::tick, this, [this] {
        static int n = 0;
        if (++n % 10 == 0) {
            refreshTimeline();
            if (m_ruleChannel->count() != m_s.store().channels().size()) {
                const QString cur = m_ruleChannel->currentText();
                m_ruleChannel->clear();
                m_ruleChannel->addItems(m_s.store().channels());
                m_ruleChannel->setCurrentText(cur);
            }
        }
        m_clear->setEnabled(m_s.transport() && m_s.transport()->isSimulator() && m_s.isDeviceLive());
    });
    connect(&m_s, &Session::transportChanged, this, [this] {
        m_signature.clear();
        rebuildDtcs();
        refreshTimeline();
    });
    rebuildRules();
    rebuildDtcs();
    refreshTimeline();
}

void FaultsPage::rebuildDtcs()
{
    const ProtocolInfo &pi = ProtocolInfo::instance();
    QVector<DtcState> list = m_s.dtcs().current();
    std::stable_sort(list.begin(), list.end(), [](const DtcState &a, const DtcState &b) { return a.active() > b.active(); });
    QString sig;
    for (const DtcState &d : list) {
        sig += QStringLiteral("%1:%2:%3;").arg(d.id).arg(d.status).arg(d.occ);
    }
    if (sig == m_signature && m_dtcs->rowCount() == list.size()) {
        return;
    }
    m_signature = sig;
    const int activeN = static_cast<int>(m_s.dtcs().active().size());
    m_dtcSummary->setText(m_s.hasFrame() ? QStringLiteral("%1 active · %2 confirmed%3").arg(activeN).arg(m_s.dtcs().confirmedCount())
                                               .arg(m_s.dtcs().complete() ? QString() : QStringLiteral(" · CAN reports the first active DTC only"))
                                         : QStringLiteral("No data"));
    const int keep = m_dtcs->currentRow();
    m_dtcs->setRowCount(static_cast<int>(list.size()));
    for (int r = 0; r < list.size(); r++) {
        const DtcState &d = list[r];
        const DtcInfo *info = pi.dtc(d.id);
        const QStringList cells = {info ? info->code : QString::number(d.id), pi.dtcName(d.id), info ? info->cls : QString(),
                                   statusText(d.status), QString::number(d.occ),
                                   m_s.dtcs().complete() ? QString::number(d.firstMs / 1000.0, 'f', 3) : QStringLiteral("—"),
                                   m_s.dtcs().complete() ? QString::number(d.lastMs / 1000.0, 'f', 3) : QStringLiteral("—")};
        for (int c = 0; c < cells.size(); c++) {
            auto *it = new QTableWidgetItem(cells[c]);
            it->setData(Qt::UserRole, d.id);
            if (c == 2 || (c == 3 && d.active())) {
                it->setForeground(Theme::sevColor(d.active() ? sevOfClass(info ? info->cls : QString()) : Theme::Sev::Neutral));
            }
            if (!d.active()) {
                it->setForeground(Theme::color("muted"));
            }
            m_dtcs->setItem(r, c, it);
        }
    }
    if (keep >= 0 && keep < m_dtcs->rowCount()) {
        m_dtcs->setCurrentCell(keep, 0);
    } else if (m_dtcs->rowCount() > 0) {
        m_dtcs->setCurrentCell(0, 0);
    }
    showDtc();
}

void FaultsPage::showDtc()
{
    const int r = m_dtcs->currentRow();
    if (r < 0 || !m_dtcs->item(r, 0)) {
        m_dtcDetail->setHtml(QStringLiteral("<p style='color:%1'>%2</p>").arg(Theme::color("dim").name(),
                                                                          m_s.hasFrame() ? QStringLiteral("No DTC has occurred since the last clear.")
                                                                                         : QStringLiteral("No data.")));
        return;
    }
    const int id = m_dtcs->item(r, 0)->data(Qt::UserRole).toInt();
    const DtcInfo *d = ProtocolInfo::instance().dtc(id);
    const QString muted = Theme::color("muted").name();
    if (!d) {
        m_dtcDetail->setHtml(QStringLiteral("<b>DTC %1</b><p>Not in tool/protocol/dtcs.json: the device runs another image.</p>").arg(id));
        return;
    }
    m_dtcDetail->setHtml(QStringLiteral("<h3 style='margin:0'>%1</h3><p style='color:%2;margin-top:2px'>%3 · id %4 · class %5 · %6</p>"
                                        "<p><b>What it means.</b> %7</p><p><b>What the firmware does.</b> %8</p>%9")
                             .arg(d->shortName(), muted, d->code).arg(id).arg(d->cls, d->requirement.toHtmlEscaped(),
                                  d->description.toHtmlEscaped(), d->response.toHtmlEscaped(),
                                  d->neverSet ? QStringLiteral("<p style='color:%1'>Declared in dtc.h but raised nowhere in this image.</p>").arg(muted)
                                              : QString()));
}

void FaultsPage::refreshTimeline()
{
    const TelemetryStore &st = m_s.store();
    const double t0 = st.size() ? st.firstT() : 0.0;
    const double t1 = st.size() ? st.lastT() : 1.0;
    QVector<DtcEvent> ev;
    for (const DtcEvent &e : m_s.dtcs().history()) {
        if (e.tMs >= t0) {
            ev.push_back(e);
        }
    }
    QVector<AlertEvent> al;
    for (const AlertEvent &a : m_s.alerts().log()) {
        if (a.tMs >= t0) {
            al.push_back(a);
        }
    }
    m_timeline->setData(ev, al, t0, t1);
}

void FaultsPage::rebuildRules()
{
    const auto &rules = m_s.alerts().rules();
    m_rules->setRowCount(static_cast<int>(rules.size()));
    for (int r = 0; r < rules.size(); r++) {
        const AlertRule &rule = rules[r];
        auto *on = new QCheckBox;
        on->setChecked(rule.enabled);
        const QString id = rule.id;
        connect(on, &QCheckBox::toggled, this, [this, id](bool v) {
            QVector<AlertRule> all = m_s.alerts().rules();
            for (AlertRule &x : all) {
                if (x.id == id) {
                    x.enabled = v;
                }
            }
            m_s.alerts().setRules(all);
            m_s.saveAlertRules();
        });
        m_rules->setCellWidget(r, 0, on);
        m_rules->setItem(r, 1, new QTableWidgetItem(rule.describe() + (rule.note.isEmpty() ? QString() : QStringLiteral(" — ") + rule.note)));
        auto *sev = new QTableWidgetItem(rule.severity);
        sev->setForeground(Theme::sevColor(rule.severity == QLatin1String("crit") ? Theme::Sev::Crit
                                          : rule.severity == QLatin1String("warn") ? Theme::Sev::Warn : Theme::Sev::Info));
        m_rules->setItem(r, 2, sev);
        auto *del = new QPushButton(QStringLiteral("Remove"));
        connect(del, &QPushButton::clicked, this, [this, id] {
            m_s.alerts().removeRule(id);
            m_s.saveAlertRules();
            rebuildRules();
        });
        m_rules->setCellWidget(r, 3, del);
    }
}

void FaultsPage::addRule()
{
    AlertRule r;
    r.channel = m_ruleChannel->currentText().trimmed();
    if (r.channel.isEmpty()) {
        return;
    }
    r.op = AlertRule::opFromText(m_ruleOp->currentText());
    r.threshold = m_ruleThr->value();
    r.hysteresis = m_ruleHyst->value();
    r.holdMs = m_ruleHold->value();
    r.severity = m_ruleSev->currentText();
    r.note = m_ruleNote->text().trimmed();
    m_s.alerts().addRule(r);
    m_s.saveAlertRules();
    if (!m_s.store().hasChannel(r.channel)) {
        m_s.addEvent(QStringLiteral("warn"), QStringLiteral("alert rule on %1: no such channel in the current telemetry (yet)").arg(r.channel));
    }
    rebuildRules();
}

void FaultsPage::addAlertLog(const AlertEvent &e)
{
    auto *it = new QListWidgetItem(QStringLiteral("%1  %2").arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")), e.text));
    it->setForeground(!e.raised ? Theme::color("muted")
                                : Theme::sevColor(e.severity == QLatin1String("crit") ? Theme::Sev::Crit
                                                  : e.severity == QLatin1String("warn") ? Theme::Sev::Warn : Theme::Sev::Info));
    m_alertLog->insertItem(0, it);
    while (m_alertLog->count() > 500) {
        delete m_alertLog->takeItem(m_alertLog->count() - 1);
    }
}

void FaultsPage::saveBugReport()
{
    QDialog d(this);
    d.setWindowTitle(QStringLiteral("Bug report"));
    auto *form = new QFormLayout(&d);
    auto *note = new QPlainTextEdit;
    note->setPlaceholderText(QStringLiteral("What happened, what you expected, what you did just before…"));
    note->setMinimumSize(420, 120);
    auto *secs = new QSpinBox;
    secs->setRange(1, 200);
    secs->setValue(30);
    secs->setSuffix(QStringLiteral(" s of telemetry"));
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    form->addRow(QStringLiteral("Note"), note);
    form->addRow(QStringLiteral("Include"), secs);
    form->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    if (d.exec() != QDialog::Accepted) {
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Save bug report"),
        QStringLiteral("traction-bug-%1.json").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))),
        QStringLiteral("JSON (*.json)"));
    if (path.isEmpty()) {
        return;
    }
    QString err;
    if (!BugReport::save(path, BugReport::build(m_s, secs->value(), note->toPlainText()), &err)) {
        QMessageBox::warning(this, QStringLiteral("Bug report"), err);
        return;
    }
    m_s.addEvent(QStringLiteral("info"), QStringLiteral("bug report saved: %1").arg(path));
}
