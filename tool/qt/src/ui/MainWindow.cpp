#include "MainWindow.h"

#include "AnalysisPage.h"
#include "BridgeCanGateway.h"
#include "BugReport.h"
#include "CommissioningPage.h"
#include "ConnectDialogs.h"
#include "ControlsPage.h"
#include "DashboardPage.h"
#include "FaultsPage.h"
#include "FirmwarePage.h"
#include "ParametersPage.h"
#include "PlotsPage.h"
#include "ProtocolInfo.h"
#include "ReplayTransport.h"
#include "TerminalPage.h"
#include "Theme.h"
#include "Widgets.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QTextBrowser>
#include <QFileInfo>
#include <QDialog>
#include <QVBoxLayout>
#include <QSettings>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QToolButton>

namespace {
const char *const NAV_ICONS[] = {"dashboard", "plots",    "controls", "commissioning", "parameters",
                                 "faults",    "analysis", "terminal", "firmware"};

Theme::Sev linkSev(Session::Link l)
{
    switch (l) {
    case Session::Link::Live: return Theme::Sev::Ok;
    case Session::Link::Replay: return Theme::Sev::Info;
    case Session::Link::Stale:
    case Session::Link::Paused: return Theme::Sev::Warn;
    case Session::Link::Failed: return Theme::Sev::Crit;
    case Session::Link::Connecting: return Theme::Sev::Accent;
    case Session::Link::None: break;
    }
    return Theme::Sev::Stale;
}
} // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Traction Tool"));
    setWindowIcon(Theme::appIcon());
    auto *central = new QWidget;
    central->setObjectName(QStringLiteral("pageHost"));
    auto *v = new QVBoxLayout(central);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    auto *bar = new QWidget;
    bar->setObjectName(QStringLiteral("connBar"));
    buildConnectionBar(bar);
    v->addWidget(bar);
    auto *h = new QHBoxLayout;
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);
    auto *navBox = new QWidget;
    navBox->setObjectName(QStringLiteral("nav"));
    navBox->setFixedWidth(212);
    auto *nl = new QVBoxLayout(navBox);
    nl->setContentsMargins(0, 0, 0, Theme::S3);
    nl->setSpacing(0);
    auto *brand = new QLabel(QStringLiteral("Traction Tool"));
    brand->setObjectName(QStringLiteral("brand"));
    auto *brandSub = new QLabel(QStringLiteral("220 kW / 800 V inverter · v%1").arg(QStringLiteral(TT_VERSION)));
    brandSub->setObjectName(QStringLiteral("brandSub"));
    nl->addWidget(brand);
    nl->addWidget(brandSub);
    m_nav = new QListWidget;
    m_nav->setObjectName(QStringLiteral("nav"));
    m_nav->setIconSize(QSize(18, 18));
    m_nav->setFrameShape(QFrame::NoFrame);
    m_nav->setFocusPolicy(Qt::StrongFocus);
    nl->addWidget(m_nav, 1);
    auto *fwLabel = new QLabel(QStringLiteral("protocol exports: TI_FW_ID %1").arg(ProtocolInfo::instance().fwId));
    fwLabel->setObjectName(QStringLiteral("brandSub"));
    fwLabel->setWordWrap(true);
    nl->addWidget(fwLabel);
    h->addWidget(navBox);
    m_stack = new QStackedWidget;
    h->addWidget(m_stack, 1);
    v->addLayout(h, 1);
    setCentralWidget(central);

    m_plots = new PlotsPage(m_s);
    m_controls = new ControlsPage(m_s);
    m_faults = new FaultsPage(m_s);
    m_pages = {new DashboardPage(m_s), m_plots, m_controls, new CommissioningPage(m_s), new ParametersPage(m_s), m_faults,
               new AnalysisPage(m_s), new TerminalPage(m_s), new FirmwarePage(m_s)};
    for (int i = 0; i < m_pages.size(); i++) {
        m_stack->addWidget(m_pages[i]);
        auto *it = new QListWidgetItem(m_pages[i]->title());
        it->setToolTip(QStringLiteral("%1 (Ctrl+%2)").arg(m_pages[i]->title()).arg(i + 1));
        m_nav->addItem(it);
    }
    connect(m_nav, &QListWidget::currentRowChanged, m_stack, &QStackedWidget::setCurrentIndex);
    m_toast = new Toast(this);
    buildMenus();
    statusBar()->showMessage(QStringLiteral("Not connected — File ▸ Start simulator (Ctrl+Shift+S), Open replay (Ctrl+O) or Connect CAN (Ctrl+Shift+C)"));

    connect(&m_s, &Session::linkChanged, this, &MainWindow::updateConnectionBar);
    connect(&m_s, &Session::transportChanged, this, &MainWindow::updateConnectionBar);
    connect(&m_s, &Session::recordingChanged, this, &MainWindow::updateConnectionBar);
    connect(&m_s, &Session::serviceChanged, this, &MainWindow::updateConnectionBar);
    connect(&m_s, &Session::tick, this, [this] {
        const qint64 age = m_s.ageMs();
        if (!m_s.transport()) {
            m_age->setText(m_s.hasFrame() ? QStringLiteral("last frame %1 ago").arg(fmtAge(age)) : QStringLiteral("not connected"));
        } else {
            m_age->setText(m_s.hasFrame() ? QStringLiteral("heartbeat %1 · %2 fps").arg(fmtAge(age)).arg(m_s.isLive() ? m_s.framesPerSecond() : 0.0, 0, 'f', 0)
                                          : QStringLiteral("waiting for frames"));
        }
    });
    connect(&m_s, &Session::eventLogged, this, [this](const LogEntry &e) {
        if (e.level != QLatin1String("tx")) {
            statusBar()->showMessage(QStringLiteral("%1  %2").arg(e.when.toString(QStringLiteral("HH:mm:ss")), e.text));
        }
    });
    connect(&m_s, &Session::alertRaised, this, [this](const AlertEvent &e) {
        if (e.raised) {
            m_toast->show(QStringLiteral("Alert"), e.text, e.severity == QLatin1String("crit") ? Theme::Sev::Crit : Theme::Sev::Warn);
        }
    });
    connect(&m_s, &Session::dtcEvents, this, [this](const QVector<DtcEvent> &events) {
        for (const DtcEvent &e : events) {
            if (e.kind == DtcEvent::Kind::Raised) {
                const DtcInfo *d = ProtocolInfo::instance().dtc(e.id);
                m_toast->show(QStringLiteral("DTC %1 %2").arg(d ? d->code : QString::number(e.id), ProtocolInfo::instance().dtcName(e.id)),
                              d ? d->response : QString(), Theme::Sev::Crit, 7000);
            }
        }
    });
    connect(&m_s, &Session::ackReceived, this, [this](const Ack &a) {
        if (!a.ok && a.cmd != QLatin1String("param_set")) {
            m_toast->show(QStringLiteral("%1 refused").arg(a.cmd), a.message, Theme::Sev::Warn);
        }
    });

    QSettings s;
    restoreGeometry(s.value(QStringLiteral("ui/geometry")).toByteArray());
    m_nav->setCurrentRow(std::clamp(s.value(QStringLiteral("ui/page"), 0).toInt(), 0, static_cast<int>(m_pages.size()) - 1));
    if (!s.contains(QStringLiteral("ui/geometry"))) {
        resize(1440, 900);
    }
    updateConnectionBar();
}

MainWindow::~MainWindow() = default;

void MainWindow::closeEvent(QCloseEvent *e)
{
    QSettings s;
    s.setValue(QStringLiteral("ui/geometry"), saveGeometry());
    s.setValue(QStringLiteral("ui/page"), m_nav->currentRow());
    m_s.closeTransport();
    e->accept();
}

void MainWindow::buildConnectionBar(QWidget *host)
{
    auto *h = new QHBoxLayout(host);
    h->setContentsMargins(Theme::S4, Theme::S2, Theme::S4, Theme::S2);
    h->setSpacing(Theme::S3);
    m_linkPill = makePill(QStringLiteral("DISCONNECTED"), Theme::Sev::Stale);
    m_linkPill->setMinimumWidth(110);
    m_linkPill->setAccessibleName(QStringLiteral("Link state"));
    m_linkName = new QLabel(QStringLiteral("No transport"));
    m_linkName->setObjectName(QStringLiteral("connName"));
    m_age = new QLabel(QStringLiteral("no frames"));
    m_age->setObjectName(QStringLiteral("connAge"));
    m_age->setAccessibleName(QStringLiteral("Heartbeat age"));
    m_recPill = makePill(QStringLiteral("● REC"), Theme::Sev::Crit);
    m_recPill->hide();
    m_connectBtn = new QToolButton;
    m_connectBtn->setText(QStringLiteral("Connect"));
    m_connectBtn->setPopupMode(QToolButton::InstantPopup);
    m_connectBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_disconnectBtn = new QToolButton;
    m_disconnectBtn->setText(QStringLiteral("Disconnect"));
    m_recordBtn = new QToolButton;
    m_recordBtn->setText(QStringLiteral("Record"));
    m_recordBtn->setCheckable(true);
    m_recordBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_lockBtn = new QToolButton;
    m_lockBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    // simulation / replay pacing (the bridge's "time" and "pause"; the replay takes the same commands)
    m_speed = new QComboBox;
    m_speed->setToolTip(QStringLiteral("Time factor: simulated (or logged) seconds per wall second"));
    for (double f : {0.1, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0}) {
        m_speed->addItem(QStringLiteral("× %1").arg(f), f);
    }
    m_speed->addItem(QStringLiteral("× max"), 0.0);
    m_speed->setCurrentIndex(3);
    m_pauseBtn = new QToolButton;
    m_pauseBtn->setText(QStringLiteral("Pause"));
    m_pauseBtn->setCheckable(true);
    m_pauseBtn->setToolTip(QStringLiteral("Pause the simulation or the replay"));
    h->addWidget(m_linkPill);
    h->addWidget(m_linkName, 1);
    h->addWidget(m_age);
    h->addWidget(m_recPill);
    h->addWidget(m_speed);
    h->addWidget(m_pauseBtn);
    h->addWidget(m_lockBtn);
    h->addWidget(m_recordBtn);
    h->addWidget(m_disconnectBtn);
    h->addWidget(m_connectBtn);
    connect(m_disconnectBtn, &QToolButton::clicked, this, [this] { m_s.closeTransport(); });
    connect(m_recordBtn, &QToolButton::clicked, this, &MainWindow::toggleRecording);
    connect(m_lockBtn, &QToolButton::clicked, this, &MainWindow::serviceKey);
    connect(m_speed, &QComboBox::activated, this, [this] {
        m_s.command(QStringLiteral("time"), {{QStringLiteral("factor"), m_speed->currentData().toDouble()}},
                    [this](const Ack &) { updateConnectionBar(); });
    });
    connect(m_pauseBtn, &QToolButton::clicked, this, [this](bool on) {
        m_s.command(QStringLiteral("pause"), {{QStringLiteral("on"), on}});
    });
}

void MainWindow::buildMenus()
{
    QMenu *file = menuBar()->addMenu(QStringLiteral("&File"));
    QAction *sim = file->addAction(QStringLiteral("Start simulator…"), this, [this] {
        SimulatorDialog d(this);
        if (d.exec() == QDialog::Accepted) {
            connectSimulator(d.options());
        }
    });
    sim->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+S")));
    QAction *replay = file->addAction(QStringLiteral("Open replay…"), this, [this] {
        const QString p = QFileDialog::getOpenFileName(this, QStringLiteral("Replay a log"), QString(),
                                                       QStringLiteral("Telemetry logs (*.jsonl *.json *.csv);;All files (*)"));
        if (!p.isEmpty()) {
            openReplay(p);
        }
    });
    replay->setShortcut(QKeySequence::Open);
    QAction *can = file->addAction(QStringLiteral("Connect CAN adapter…"), this, [this] {
        CanDialog d(this);
        if (d.exec() == QDialog::Accepted) {
            QString err;
            if (!connectCan(d.options(), &err)) {
                QMessageBox::warning(this, QStringLiteral("CAN"), err);
            }
        }
    });
    can->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+C")));
    QAction *disc = file->addAction(QStringLiteral("Disconnect"), this, [this] { m_s.closeTransport(); });
    disc->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+D")));
    file->addSeparator();
    QAction *rec = file->addAction(QStringLiteral("Record / stop recording"), this, &MainWindow::toggleRecording);
    rec->setShortcut(QKeySequence(QStringLiteral("Ctrl+R")));
    QAction *bug = file->addAction(QStringLiteral("Bug report…"), this, [this] { m_faults->saveBugReport(); });
    bug->setShortcut(QKeySequence(QStringLiteral("Ctrl+B")));
    file->addSeparator();
    QAction *quit = file->addAction(QStringLiteral("Quit"), this, &QWidget::close);
    quit->setShortcut(QKeySequence::Quit);
    m_connectBtn->addActions({sim, replay, can});
    auto *connectMenu = new QMenu(m_connectBtn);
    connectMenu->addActions({sim, replay, can});
    m_connectBtn->setMenu(connectMenu);

    QMenu *view = menuBar()->addMenu(QStringLiteral("&View"));
    for (int i = 0; i < m_pages.size(); i++) {
        QString title = m_pages[i]->title();
        QAction *a = view->addAction(title.replace(QLatin1Char('&'), QStringLiteral("&&")), this, [this, i] { showPage(i); });
        a->setShortcut(QKeySequence(QStringLiteral("Ctrl+%1").arg(i + 1)));
    }
    view->addSeparator();
    m_lightAction = view->addAction(QStringLiteral("Light theme"));
    m_lightAction->setCheckable(true);
    m_lightAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+L")));
    m_lightAction->setChecked(Theme::current() == Theme::Variant::Light);
    connect(m_lightAction, &QAction::toggled, this, &MainWindow::setTheme);

    QMenu *tools = menuBar()->addMenu(QStringLiteral("&Tools"));
    QAction *key = tools->addAction(QStringLiteral("Service key…"), this, &MainWindow::serviceKey);
    key->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+K")));
    tools->addAction(QStringLiteral("Lock service controls"), this, [this] { m_s.lockService(); });
    QAction *zero = tools->addAction(QStringLiteral("Zero torque now"), this, [this] {
        if (m_s.transport()) {
            m_s.command(QStringLiteral("torque"), {{QStringLiteral("nm"), 0.0}});
        }
    });
    zero->setShortcut(QKeySequence(QStringLiteral("Ctrl+0")));
    // the firmware (sim_bridge) on Qt's virtual CAN-FD bus, this session a CAN adapter on it (BridgeCanGateway)
    tools->addAction(QStringLiteral("Simulator on virtual CAN…"), this, [this] {
        BridgeTransport::Options b;
        b.program = BridgeTransport::locate();
        b.rateHz = 10; // nothing shows the simulator's own telemetry here
        CanTransport::Options o;
        o.plugin = QStringLiteral("virtualcan");
        o.interface = QStringLiteral("can0");
        QString err = QStringLiteral("simulator bridge not found (make -C tool/bridge, or set TT_SIM_BRIDGE)");
        QCanBusDevice *dev = b.program.isEmpty() ? nullptr : CanTransport::createDevice(o, &err);
        if (!dev || !connectCan(o, &err)) {
            delete dev;
            QMessageBox::warning(this, QStringLiteral("Simulator on virtual CAN"), err);
            return;
        }
        auto *gw = new BridgeCanGateway(b, dev, m_s.transport()); // lives and ends with the session's transport
        connect(gw, &BridgeCanGateway::failed, this, [this](const QString &why) { m_s.addEvent(QStringLiteral("error"), why); });
        gw->start();
        m_s.addEvent(QStringLiteral("info"), QStringLiteral("the firmware on virtualcan/can0: service key, VCU on, Arm, then "
                                                            "report the contactors precharge, closed once V_DC is up"));
    });
    QMenu *help = menuBar()->addMenu(QStringLiteral("&Help"));
    help->addAction(QStringLiteral("About Traction Tool"), this, [this] {
        QMessageBox::about(this, QStringLiteral("About Traction Tool"),
                           QStringLiteral("<b>Traction Tool %1</b><p>Bench and service tool for the 220 kW / 800 V traction inverter.</p>"
                                          "<p>Protocol exports: TI_FW_ID %2. Qt %3 (LGPL-3.0, dynamically linked); Qwt 6.3 "
                                          "(LGPL-2.1 with the Qwt exception). See NOTICES.md.</p>")
                               .arg(QStringLiteral(TT_VERSION), ProtocolInfo::instance().fwId, QString::fromLatin1(qVersion())));
    });
    help->addAction(QStringLiteral("About Qt"), qApp, &QApplication::aboutQt);
    help->addAction(QStringLiteral("Licences…"), this, [this] { // NOTICES.md: Qt/Qwt LGPL, the bundled libraries, Qt's attributions
        QString path;
        for (const QString &c : {QCoreApplication::applicationDirPath() + QStringLiteral("/../Resources/NOTICES.md"),
                                 QStringLiteral(TT_SOURCE_DIR "/NOTICES.md")}) {
            if (QFileInfo::exists(c)) {
                path = c;
                break;
            }
        }
        QFile f(path);
        auto *d = new QDialog(this);
        d->setAttribute(Qt::WA_DeleteOnClose);
        d->setWindowTitle(QStringLiteral("Licences — Traction Tool"));
        d->resize(760, 620);
        auto *v = new QVBoxLayout(d);
        auto *b = new QTextBrowser(d);
        b->setOpenExternalLinks(true);
        b->setMarkdown(f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll())
                                                   : QStringLiteral("NOTICES.md was not found next to the application."));
        v->addWidget(b);
        d->show();
    });
}

void MainWindow::updateConnectionBar()
{
    const Session::Link l = m_s.link();
    QString text = Session::linkText(l).toUpper();
    setPill(m_linkPill, text, linkSev(l));
    ITransport *t = m_s.transport();
    m_linkName->setText(t ? t->name() : QStringLiteral("No transport"));
    m_disconnectBtn->setEnabled(t != nullptr);
    const bool paced = t && (t->isSimulator() || t->isReplay());
    m_speed->setVisible(paced);
    m_pauseBtn->setVisible(paced);
    m_pauseBtn->setChecked(l == Session::Link::Paused);
    m_pauseBtn->setText(l == Session::Link::Paused ? QStringLiteral("Resume") : QStringLiteral("Pause"));
    if (t != m_shown) { // a new transport: show its own pacing
        m_shown = t;
        m_pauseBtn->setChecked(false);
        const auto *b = qobject_cast<const BridgeTransport *>(t);
        const double f = b ? b->options().timeFactor : 1.0;
        const int i = m_speed->findData(f);
        m_speed->setCurrentIndex(i >= 0 ? i : 3);
        m_pauseBtn->setChecked(b && b->options().paused);
    }
    m_recordBtn->setEnabled(t != nullptr && !t->isReplay());
    m_recordBtn->setChecked(m_s.isRecording());
    m_recordBtn->setIcon(Theme::icon(QStringLiteral("record"), m_s.isRecording() ? Theme::color("crit") : Theme::color("muted")));
    m_recPill->setVisible(m_s.isRecording());
    const bool unlocked = m_s.serviceUnlocked();
    m_lockBtn->setText(unlocked ? QStringLiteral("Service unlocked") : QStringLiteral("Service locked"));
    m_lockBtn->setIcon(Theme::icon(unlocked ? QStringLiteral("unlock") : QStringLiteral("lock"),
                                   unlocked ? Theme::color("warn") : Theme::color("muted")));
    m_lockBtn->setToolTip(QStringLiteral("On a real CAN bus the transmitting controls need the service key (Ctrl+Shift+K)"));
    m_connectBtn->setIcon(Theme::icon(QStringLiteral("plug"), Theme::color("accent")));
    for (int i = 0; i < m_nav->count(); i++) {
        m_nav->item(i)->setIcon(Theme::icon(QLatin1String(NAV_ICONS[i]), Theme::color("muted")));
    }
    setWindowTitle(t ? QStringLiteral("Traction Tool — %1").arg(t->name()) : QStringLiteral("Traction Tool"));
}

void MainWindow::setTheme(bool light)
{
    Theme::apply(*qApp, light ? Theme::Variant::Light : Theme::Variant::Dark);
    QSettings().setValue(QStringLiteral("ui/light"), light);
    for (Page *p : m_pages) {
        p->applyTheme();
        p->update();
    }
    updateConnectionBar();
}

void MainWindow::connectSimulator(const BridgeTransport::Options &o)
{
    m_s.setTransport(std::make_unique<BridgeTransport>(o));
}

bool MainWindow::openReplay(const QString &path)
{
    m_s.setTransport(std::make_unique<ReplayTransport>(path));
    return m_s.transport() && m_s.transport()->state() == ITransport::State::Open;
}

bool MainWindow::connectCan(const CanTransport::Options &o, QString *err)
{
    QCanBusDevice *dev = CanTransport::createDevice(o, err);
    if (!dev) {
        return false;
    }
    m_s.setTransport(std::make_unique<CanTransport>(dev, QStringLiteral("CAN · %1/%2 · %3 kbit/s%4").arg(o.plugin, o.interface)
                                                             .arg(o.bitrate / 1000).arg(o.fd ? QStringLiteral(" · FD %1 Mbit/s").arg(o.dataBitrate / 1000000) : QString())));
    return true;
}

void MainWindow::showPage(int index)
{
    if (index >= 0 && index < m_pages.size()) {
        m_nav->setCurrentRow(index);
    }
}

int MainWindow::pageCount() const { return static_cast<int>(m_pages.size()); }
Page *MainWindow::page(int index) const { return m_pages.value(index); }

void MainWindow::toggleRecording()
{
    if (m_s.isRecording()) {
        m_s.stopRecording();
        return;
    }
    if (!m_s.transport()) {
        return;
    }
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Record telemetry"),
        dir + QStringLiteral("/traction-%1.jsonl").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))),
        QStringLiteral("JSON lines (*.jsonl)"));
    if (path.isEmpty()) {
        updateConnectionBar();
        return;
    }
    QString err;
    if (!m_s.startRecording(path, &err)) {
        QMessageBox::warning(this, QStringLiteral("Record"), err);
    }
    updateConnectionBar();
}

void MainWindow::serviceKey()
{
    if (m_s.serviceUnlocked()) {
        m_s.lockService();
        return;
    }
    bool ok = false;
    const QString key = QInputDialog::getText(this, QStringLiteral("Service key"),
                                              QStringLiteral("Unlocks the transmitting controls on a real CAN bus for this session.\n"
                                                             "It is an interlock against a slip on a live bench, not a security boundary."),
                                              QLineEdit::Password, QString(), &ok);
    if (ok && !m_s.unlockService(key)) {
        QMessageBox::warning(this, QStringLiteral("Service key"), QStringLiteral("That is not the service key."));
    }
}
