#include "PlotsPage.h"

#include "LogIo.h"
#include "PlotWidgets.h"
#include "Widgets.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QShortcut>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTreeWidget>
#include <qwt_interval.h>

#include <cmath>

namespace {
const QList<QPair<QString, QStringList>> &presets()
{
    static const QList<QPair<QString, QStringList>> p = {
        {QStringLiteral("Torque"), {QStringLiteral("motion.torque_req_nm"), QStringLiteral("motion.torque_cmd_nm"),
                                    QStringLiteral("inv.torque_applied_nm"), QStringLiteral("motion.torque_est_nm")}},
        {QStringLiteral("dq currents"), {QStringLiteral("foc.id_a"), QStringLiteral("foc.iq_a"), QStringLiteral("foc.id_ref_a"),
                                         QStringLiteral("foc.iq_ref_a")}},
        {QStringLiteral("Phase currents"), {QStringLiteral("foc.ia_a"), QStringLiteral("foc.ib_a"), QStringLiteral("foc.ic_a")}},
        {QStringLiteral("Link"), {QStringLiteral("link.vdc_v"), QStringLiteral("link.v_ch1"), QStringLiteral("link.v_ch2"),
                                  QStringLiteral("inv.vdc_v")}},
        {QStringLiteral("Temperatures"), {QStringLiteral("temps.mod_u_c"), QStringLiteral("temps.mod_v_c"),
                                          QStringLiteral("temps.mod_w_c"), QStringLiteral("temps.coolant_c")}},
        {QStringLiteral("Speed"), {QStringLiteral("motion.speed_rpm"), QStringLiteral("plant.speed_rpm"), QStringLiteral("inv.speed_rpm")}},
    };
    return p;
}

QIcon swatch(const QColor &c)
{
    QPixmap pm(24, 24);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawRoundedRect(QRectF(4, 8, 16, 8), 3, 3);
    return QIcon(pm);
}

double valueAt(const TelemetryStore &s, const QString &ch, double tMs)
{
    if (s.size() == 0) {
        return std::nan("");
    }
    int i = s.lowerBound(tMs);
    if (i >= s.size()) {
        i = s.size() - 1;
    }
    if (i > 0 && std::abs(s.t(i - 1) - tMs) < std::abs(s.t(i) - tMs)) {
        i--;
    }
    return s.value(ch, i);
}
} // namespace

PlotsPage::PlotsPage(Session &s, QWidget *parent)
    : Page(s, QStringLiteral("Live plots"),
           QStringLiteral("Traces from the telemetry buffer. Drag to zoom (right-click steps back), wheel to magnify, "
                          "middle-drag to pan; in cursor mode click for A, right-click for B."),
           parent)
{
    m_tabs = new QTabWidget;
    content()->addWidget(m_tabs, 1);

    // ---------------- time plot
    auto *timeTab = new QWidget;
    auto *tl = new QVBoxLayout(timeTab);
    tl->setContentsMargins(0, Theme::S2, 0, 0);
    auto *bar = new QHBoxLayout;
    bar->setSpacing(Theme::S2);
    m_pause = new QPushButton(QStringLiteral("Pause"));
    m_pause->setCheckable(true);
    m_pause->setToolTip(QStringLiteral("Freeze the view (P). The buffer keeps filling."));
    m_follow = new QPushButton(QStringLiteral("Follow"));
    m_follow->setCheckable(true);
    m_follow->setChecked(true);
    m_follow->setToolTip(QStringLiteral("Keep the newest samples in view (F); zooming turns it off"));
    m_cursors = new QPushButton(QStringLiteral("Cursors"));
    m_cursors->setCheckable(true);
    m_cursors->setToolTip(QStringLiteral("Cursor mode (C): click sets A, right-click sets B"));
    auto *clearCur = new QPushButton(QStringLiteral("Clear cursors"));
    m_window = new QComboBox;
    for (int sec : {2, 5, 10, 30, 60, 120, 200}) {
        m_window->addItem(QStringLiteral("%1 s window").arg(sec), sec * 1000);
    }
    m_window->setCurrentIndex(2);
    m_rate = new QComboBox;
    for (int hz : {20, 50, 100, 200, 500}) {
        m_rate->addItem(QStringLiteral("%1 Hz telemetry").arg(hz), hz);
    }
    m_rate->setCurrentIndex(2);
    m_rate->setToolTip(QStringLiteral("The simulator bridge's telemetry rate (simulated time)"));
    auto *png = new QPushButton(QStringLiteral("PNG…"));
    auto *csv = new QPushButton(QStringLiteral("CSV…"));
    for (QWidget *w : std::initializer_list<QWidget *>{m_pause, m_follow, m_cursors, clearCur, m_window, m_rate}) {
        bar->addWidget(w);
    }
    bar->addStretch(1);
    bar->addWidget(png);
    bar->addWidget(csv);
    tl->addLayout(bar);

    auto *split = new QSplitter(Qt::Horizontal);
    auto *left = new QWidget;
    auto *ll = new QVBoxLayout(left);
    ll->setContentsMargins(0, 0, Theme::S2, 0);
    m_filter = new QLineEdit;
    m_filter->setPlaceholderText(QStringLiteral("Filter channels…"));
    m_filter->setClearButtonEnabled(true);
    ll->addWidget(m_filter);
    auto *presetRow = new QGridLayout;
    presetRow->setSpacing(Theme::S1);
    int k = 0;
    for (const auto &p : presets()) {
        auto *b = new QPushButton(p.first);
        const QStringList chs = p.second;
        connect(b, &QPushButton::clicked, this, [this, chs] { selectChannels(chs); });
        presetRow->addWidget(b, k / 2, k % 2);
        k++;
    }
    ll->addLayout(presetRow);
    m_tree = new QTreeWidget;
    m_tree->setHeaderHidden(true);
    m_tree->setObjectName(QStringLiteral("channelTree"));
    ll->addWidget(m_tree, 1);
    split->addWidget(left);
    auto *right = new QWidget;
    auto *rl = new QVBoxLayout(right);
    rl->setContentsMargins(Theme::S2, 0, 0, 0);
    m_plot = new TimePlot;
    m_plot->setMinimumHeight(260);
    rl->addWidget(m_plot, 3);
    m_cursorHead = new QLabel(QStringLiteral("Cursors: none"));
    m_cursorHead->setObjectName(QStringLiteral("muted"));
    rl->addWidget(m_cursorHead);
    m_cursorTable = new QTableWidget(0, 4);
    m_cursorTable->setHorizontalHeaderLabels({QStringLiteral("Channel"), QStringLiteral("A"), QStringLiteral("B"),
                                              QStringLiteral("B − A")});
    m_cursorTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_cursorTable->verticalHeader()->hide();
    m_cursorTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_cursorTable->setMaximumHeight(150);
    rl->addWidget(m_cursorTable, 1);
    split->addWidget(right);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setSizes({260, 900});
    tl->addWidget(split, 1);
    m_tabs->addTab(timeTab, QStringLiteral("Time"));

    // ---------------- id/iq plane
    auto *dqTab = new QWidget;
    auto *dl = new QVBoxLayout(dqTab);
    dl->setContentsMargins(0, Theme::S2, 0, 0);
    auto *dbar = new QHBoxLayout;
    dbar->addWidget(new QLabel(QStringLiteral("Trajectory")));
    m_dqSeconds = new QSpinBox;
    m_dqSeconds->setRange(1, 120);
    m_dqSeconds->setValue(5);
    m_dqSeconds->setSuffix(QStringLiteral(" s"));
    dbar->addWidget(m_dqSeconds);
    m_dqNote = new QLabel;
    m_dqNote->setObjectName(QStringLiteral("muted"));
    dbar->addWidget(m_dqNote, 1);
    dl->addLayout(dbar);
    auto *legend = new QLabel(QStringLiteral("● operating point   ✚ reference   – – current limit (allowance in force)   – – voltage ellipse at the present speed"));
    legend->setObjectName(QStringLiteral("hint"));
    dl->addWidget(legend);
    m_dq = new DqPlot;
    dl->addWidget(m_dq, 1);
    m_tabs->addTab(dqTab, QStringLiteral("id / iq plane"));

    // ---------------- spectrum
    auto *fftTab = new QWidget;
    auto *fl = new QVBoxLayout(fftTab);
    fl->setContentsMargins(0, Theme::S2, 0, 0);
    auto *fbar = new QHBoxLayout;
    m_fftChannel = new QComboBox;
    m_fftChannel->setEditable(true);
    m_fftChannel->setMinimumWidth(240);
    m_fftChannel->setCurrentText(QStringLiteral("foc.ia_a"));
    m_fftSize = new QComboBox;
    for (int n : {256, 512, 1024, 2048, 4096, 8192, 16384}) {
        m_fftSize->addItem(QStringLiteral("%1 samples").arg(n), n);
    }
    m_fftSize->setCurrentIndex(3);
    m_fftLog = new QCheckBox(QStringLiteral("dB"));
    m_fftLog->setChecked(true);
    m_fftAuto = new QCheckBox(QStringLiteral("Auto (1 Hz)"));
    auto *go = new QPushButton(QStringLiteral("Compute"));
    go->setObjectName(QStringLiteral("primary"));
    fbar->addWidget(new QLabel(QStringLiteral("Channel")));
    fbar->addWidget(m_fftChannel);
    fbar->addWidget(m_fftSize);
    fbar->addWidget(m_fftLog);
    fbar->addWidget(m_fftAuto);
    fbar->addWidget(go);
    fbar->addStretch(1);
    fl->addLayout(fbar);
    m_fftNote = new QLabel(QStringLiteral("The spectrum of the newest samples of the buffer; the sample rate is the telemetry rate."));
    m_fftNote->setObjectName(QStringLiteral("muted"));
    fl->addWidget(m_fftNote);
    m_fft = new SpectrumPlot;
    fl->addWidget(m_fft, 1);
    m_tabs->addTab(fftTab, QStringLiteral("Spectrum"));

    // ---------------- wiring
    connect(m_pause, &QPushButton::toggled, this, [this](bool on) { m_pause->setText(on ? QStringLiteral("Resume") : QStringLiteral("Pause")); });
    connect(m_follow, &QPushButton::toggled, m_plot, &TimePlot::setFollow);
    connect(m_plot, &TimePlot::followChanged, m_follow, &QPushButton::setChecked);
    connect(m_cursors, &QPushButton::toggled, m_plot, &TimePlot::setCursorMode);
    connect(clearCur, &QPushButton::clicked, m_plot, &TimePlot::clearCursors);
    connect(m_plot, &TimePlot::cursorsChanged, this, &PlotsPage::updateCursorTable);
    connect(m_window, &QComboBox::currentIndexChanged, this, [this] { m_plot->setWindowMs(m_window->currentData().toDouble()); });
    connect(m_rate, &QComboBox::activated, this, [this] {
        m_s.command(QStringLiteral("rate"), {{QStringLiteral("hz"), m_rate->currentData().toInt()}});
    });
    connect(png, &QPushButton::clicked, this, &PlotsPage::exportPng);
    connect(csv, &QPushButton::clicked, this, &PlotsPage::exportCsv);
    connect(m_filter, &QLineEdit::textChanged, this, [this](const QString &t) {
        for (int i = 0; i < m_tree->topLevelItemCount(); i++) {
            QTreeWidgetItem *g = m_tree->topLevelItem(i);
            int shown = 0;
            for (int j = 0; j < g->childCount(); j++) {
                const bool vis = t.isEmpty() || g->child(j)->data(0, Qt::UserRole).toString().contains(t, Qt::CaseInsensitive);
                g->child(j)->setHidden(!vis);
                shown += vis ? 1 : 0;
            }
            g->setHidden(shown == 0);
            g->setExpanded(!t.isEmpty() || g->isExpanded());
        }
    });
    connect(m_tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *it) {
        const QString ch = it->data(0, Qt::UserRole).toString();
        if (ch.isEmpty()) {
            return;
        }
        const bool on = it->checkState(0) == Qt::Checked;
        if (on && !m_selected.contains(ch)) {
            m_selected << ch;
        } else if (!on) {
            m_selected.removeAll(ch);
        }
        applySelection();
    });
    connect(go, &QPushButton::clicked, this, [this] {
        m_fftNote->setText(m_fft->compute(m_s.store(), m_fftChannel->currentText(), m_fftSize->currentData().toInt(), m_fftLog->isChecked()));
    });
    connect(&m_s, &Session::transportChanged, this, [this] { m_knownChannels = -1; });
    for (auto [key, fn] : std::initializer_list<std::pair<QKeySequence, std::function<void()>>>{
             {QKeySequence(Qt::Key_P), [this] { m_pause->toggle(); }},
             {QKeySequence(Qt::Key_F), [this] { m_follow->setChecked(true); }},
             {QKeySequence(Qt::Key_C), [this] { m_cursors->toggle(); }}}) {
        auto *sc = new QShortcut(key, this, fn);
        sc->setContext(Qt::WidgetWithChildrenShortcut);
    }
    m_timer.setInterval(50);
    connect(&m_timer, &QTimer::timeout, this, &PlotsPage::refresh);
    selectChannels(presets().first().second);
}

void PlotsPage::applyTheme()
{
    m_plot->applyTheme();
    m_dq->applyTheme();
    m_fft->applyTheme();
    applySelection();
}

void PlotsPage::showEvent(QShowEvent *e)
{
    m_timer.start();
    Page::showEvent(e);
}

void PlotsPage::hideEvent(QHideEvent *e)
{
    m_timer.stop();
    Page::hideEvent(e);
}

void PlotsPage::selectChannels(const QStringList &channels)
{
    m_selected = channels;
    rebuildTree();
    applySelection();
}

void PlotsPage::rebuildTree()
{
    const QStringList all = m_s.store().channels();
    m_knownChannels = static_cast<int>(all.size());
    QSignalBlocker block(m_tree);
    QSet<QString> expanded;
    for (int i = 0; i < m_tree->topLevelItemCount(); i++) {
        if (m_tree->topLevelItem(i)->isExpanded()) {
            expanded.insert(m_tree->topLevelItem(i)->text(0));
        }
    }
    m_tree->clear();
    QHash<QString, QTreeWidgetItem *> groups;
    QStringList names = all;
    for (const QString &c : m_selected) {
        if (!names.contains(c)) {
            names << c; // selected before it has arrived
        }
    }
    for (const QString &ch : names) {
        const QString g = ch.section(QLatin1Char('.'), 0, 0);
        QTreeWidgetItem *gi = groups.value(g);
        if (!gi) {
            gi = new QTreeWidgetItem(m_tree, {g});
            gi->setFlags(Qt::ItemIsEnabled);
            gi->setExpanded(expanded.contains(g));
            groups.insert(g, gi);
        }
        auto *it = new QTreeWidgetItem(gi, {ch.section(QLatin1Char('.'), 1)});
        it->setData(0, Qt::UserRole, ch);
        it->setToolTip(0, ch);
        it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
        const int idx = static_cast<int>(m_selected.indexOf(ch));
        it->setCheckState(0, idx >= 0 ? Qt::Checked : Qt::Unchecked);
        if (idx >= 0) {
            it->setIcon(0, swatch(Theme::seriesColor(idx)));
            gi->setExpanded(true);
        }
    }
    m_tree->sortItems(0, Qt::AscendingOrder);
}

void PlotsPage::applySelection()
{
    m_plot->setChannels(m_selected);
    QSignalBlocker block(m_tree);
    for (int i = 0; i < m_tree->topLevelItemCount(); i++) {
        QTreeWidgetItem *g = m_tree->topLevelItem(i);
        for (int j = 0; j < g->childCount(); j++) {
            QTreeWidgetItem *it = g->child(j);
            const int idx = static_cast<int>(m_selected.indexOf(it->data(0, Qt::UserRole).toString()));
            it->setIcon(0, idx >= 0 ? swatch(Theme::seriesColor(idx)) : QIcon());
        }
    }
    updateCursorTable();
}

void PlotsPage::refresh()
{
    if (m_s.store().channels().size() != m_knownChannels) {
        rebuildTree();
        const QString cur = m_fftChannel->currentText();
        m_fftChannel->clear();
        m_fftChannel->addItems(m_s.store().channels());
        m_fftChannel->setCurrentText(cur);
    }
    const bool live = m_s.isLive();
    m_plot->setOverlay(live || !m_s.hasFrame() ? QString()
                                              : QStringLiteral("%1 — last frame %2 ago").arg(Session::linkText(m_s.link()).toUpper(), fmtAge(m_s.ageMs())));
    m_rate->setEnabled(m_s.transport() && m_s.transport()->isSimulator());
    const int tab = m_tabs->currentIndex();
    if (tab == 0 && !m_pause->isChecked()) {
        m_plot->refresh(m_s.store());
        if (m_tickCount % 4 == 0) {
            updateCursorTable();
        }
    } else if (tab == 1 && (m_tickCount % 2 == 0)) {
        m_dq->refresh(m_s.store(), m_s.last(), m_s.deviceInfo(), m_dqSeconds->value());
        m_dqNote->setText(m_dq->note());
    } else if (tab == 2 && m_fftAuto->isChecked() && (m_tickCount % 20 == 0)) {
        m_fftNote->setText(m_fft->compute(m_s.store(), m_fftChannel->currentText(), m_fftSize->currentData().toInt(), m_fftLog->isChecked()));
    }
    m_tickCount++;
}

void PlotsPage::updateCursorTable()
{
    double ta = 0, tb = 0;
    const bool a = m_plot->cursorA(&ta), b = m_plot->cursorB(&tb);
    if (!a && !b) {
        m_cursorHead->setText(QStringLiteral("Cursors: none — toggle cursor mode (C), click for A, right-click for B"));
    } else if (a && b) {
        const double dt = tb - ta;
        m_cursorHead->setText(QStringLiteral("A %1 s · B %2 s · Δt %3 ms · 1/Δt %4 Hz")
                                  .arg(ta, 0, 'f', 4).arg(tb, 0, 'f', 4).arg(dt * 1000.0, 0, 'f', 2)
                                  .arg(std::abs(dt) > 1e-9 ? 1.0 / std::abs(dt) : 0.0, 0, 'f', 2));
    } else {
        m_cursorHead->setText(a ? QStringLiteral("A %1 s").arg(ta, 0, 'f', 4) : QStringLiteral("B %1 s").arg(tb, 0, 'f', 4));
    }
    m_cursorTable->setRowCount(static_cast<int>(m_selected.size()));
    for (int i = 0; i < m_selected.size(); i++) {
        const QString &ch = m_selected[i];
        const double va = a ? valueAt(m_s.store(), ch, ta * 1000.0) : std::nan("");
        const double vb = b ? valueAt(m_s.store(), ch, tb * 1000.0) : std::nan("");
        const QStringList cells = {ch, fmtNum(va, 3), fmtNum(vb, 3), fmtNum(vb - va, 3)};
        for (int c = 0; c < 4; c++) {
            auto *it = m_cursorTable->item(i, c);
            if (!it) {
                it = new QTableWidgetItem;
                m_cursorTable->setItem(i, c, it);
            }
            it->setText(cells[c]);
            if (c == 0) {
                it->setIcon(swatch(Theme::seriesColor(i)));
            } else {
                it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            }
        }
    }
}

void PlotsPage::exportPng()
{
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export plot"), QStringLiteral("plot.png"),
                                                      QStringLiteral("PNG image (*.png)"));
    if (path.isEmpty()) {
        return;
    }
    QWidget *w = m_tabs->currentWidget();
    if (!w->grab().save(path)) {
        QMessageBox::warning(this, QStringLiteral("Export"), QStringLiteral("Could not write %1").arg(path));
    }
}

void PlotsPage::exportCsv()
{
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export visible traces"), QStringLiteral("traces.csv"),
                                                      QStringLiteral("CSV (*.csv)"));
    if (path.isEmpty()) {
        return;
    }
    const QwtInterval iv = m_plot->axisInterval(QwtAxis::XBottom);
    const TelemetryStore &st = m_s.store();
    QVector<TelemetryFrame> frames;
    for (int i = st.lowerBound(iv.minValue() * 1000.0); i < st.size() && st.t(i) <= iv.maxValue() * 1000.0; i++) {
        TelemetryFrame f;
        f.tMs = st.t(i);
        for (const QString &ch : m_selected) {
            const double v = st.value(ch, i);
            if (!std::isnan(v)) {
                f.num.insert(ch, v);
            }
        }
        frames.push_back(f);
    }
    QString err;
    if (!LogIo::saveCsv(path, frames, m_selected, &err)) {
        QMessageBox::warning(this, QStringLiteral("Export"), err);
    }
}
