#include "AnalysisPage.h"

#include "Dsp.h"
#include "LogIo.h"
#include "PlotWidgets.h"
#include "ProtocolInfo.h"
#include "Widgets.h"

#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <qwt_plot_curve.h>
#include <qwt_plot_grid.h>
#include <qwt_symbol.h>

#include <cmath>

namespace {
void pickDefault(QComboBox *c, const QStringList &prefs)
{
    for (const QString &p : prefs) {
        const int i = c->findText(p);
        if (i >= 0) {
            c->setCurrentIndex(i);
            return;
        }
    }
}
} // namespace

AnalysisPage::AnalysisPage(Session &s, QWidget *parent)
    : Page(s, QStringLiteral("Analysis"),
           QStringLiteral("Load a recorded log (the recorder writes JSONL; CSV also reads) or take the session buffer, "
                          "then look at statistics, events, energy and thermal behaviour."),
           parent)
{
    auto *bar = new QHBoxLayout;
    auto *open = new QPushButton(QStringLiteral("Open log…"));
    open->setObjectName(QStringLiteral("primary"));
    auto *useBuf = new QPushButton(QStringLiteral("Use session buffer"));
    m_source = new QLabel(QStringLiteral("Nothing loaded"));
    m_source->setObjectName(QStringLiteral("muted"));
    auto *csv = new QPushButton(QStringLiteral("CSV…"));
    auto *png = new QPushButton(QStringLiteral("PNG…"));
    bar->addWidget(open);
    bar->addWidget(useBuf);
    bar->addWidget(m_source, 1);
    bar->addWidget(csv);
    bar->addWidget(png);
    content()->addLayout(bar);

    m_tabs = new QTabWidget;
    content()->addWidget(m_tabs, 1);
    // ---- channels
    auto *chTab = new QSplitter(Qt::Vertical);
    auto *top = new QSplitter(Qt::Horizontal);
    auto *statsBox = new QWidget;
    auto *sl = new QVBoxLayout(statsBox);
    sl->setContentsMargins(0, Theme::S2, 0, 0);
    m_filter = new QLineEdit;
    m_filter->setPlaceholderText(QStringLiteral("Filter channels… (select rows to plot them)"));
    m_filter->setClearButtonEnabled(true);
    sl->addWidget(m_filter);
    m_stats = new QTableWidget(0, 7);
    m_stats->setHorizontalHeaderLabels({QStringLiteral("Channel"), QStringLiteral("Samples"), QStringLiteral("Min"),
                                        QStringLiteral("Max"), QStringLiteral("Mean"), QStringLiteral("RMS"), QStringLiteral("Std")});
    m_stats->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_stats->verticalHeader()->hide();
    m_stats->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_stats->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_stats->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_stats->setSortingEnabled(true);
    m_stats->setShowGrid(false);
    sl->addWidget(m_stats, 1);
    top->addWidget(statsBox);
    m_events = new QListWidget;
    m_events->setToolTip(QStringLiteral("State changes and DTC raise/clear found in the log"));
    top->addWidget(m_events);
    top->setStretchFactor(0, 3);
    top->setStretchFactor(1, 1);
    chTab->addWidget(top);
    m_plot = new TimePlot;
    m_plot->setFollow(false);
    chTab->addWidget(m_plot);
    m_tabs->addTab(chTab, QStringLiteral("Channels && events"));

    // ---- energy
    auto *enTab = new QWidget;
    auto *el = new QVBoxLayout(enTab);
    auto *eform = new QHBoxLayout;
    m_pDc = new QComboBox;
    m_pMech = new QComboBox;
    auto *calc = new QPushButton(QStringLiteral("Integrate"));
    eform->addWidget(new QLabel(QStringLiteral("Electrical power")));
    eform->addWidget(m_pDc, 1);
    eform->addWidget(new QLabel(QStringLiteral("Mechanical power")));
    eform->addWidget(m_pMech, 1);
    eform->addWidget(calc);
    el->addLayout(eform);
    auto *ecard = new Card(QStringLiteral("Energy (trapezoidal, gaps not bridged)"));
    m_energy = new KvGrid;
    for (const auto &r : {std::pair<const char *, const char *>{"dur", "Duration"}, {"edp", "Electrical in (motoring)"},
                          {"edn", "Electrical back (regen)"}, {"emp", "Mechanical out (motoring)"},
                          {"emn", "Mechanical in (regen)"}, {"etam", "Efficiency, motoring"}, {"etar", "Efficiency, regen"},
                          {"pavg", "Mean electrical power"}}) {
        m_energy->addRow(QLatin1String(r.first), QString::fromUtf8(r.second));
    }
    m_energy->setStale(false);
    ecard->body()->addWidget(m_energy);
    auto *ehint = new QLabel(QStringLiteral("Sign convention of the simulator plant: p_dc_w + = drawn from the link, p_mech_w + = "
                                            "motoring. Efficiency is energy out over energy in over the whole log."));
    ehint->setObjectName(QStringLiteral("hint"));
    ehint->setWordWrap(true);
    ecard->body()->addWidget(ehint);
    el->addWidget(ecard);
    el->addStretch(1);
    m_tabs->addTab(enTab, QStringLiteral("Energy && efficiency"));

    // ---- temperature vs load
    auto *scTab = new QWidget;
    auto *scl = new QVBoxLayout(scTab);
    auto *sform = new QHBoxLayout;
    m_scX = new QComboBox;
    m_scY = new QComboBox;
    auto *draw = new QPushButton(QStringLiteral("Plot"));
    sform->addWidget(new QLabel(QStringLiteral("Load (x)")));
    sform->addWidget(m_scX, 1);
    sform->addWidget(new QLabel(QStringLiteral("Temperature (y)")));
    sform->addWidget(m_scY, 1);
    sform->addWidget(draw);
    scl->addLayout(sform);
    m_scNote = new QLabel;
    m_scNote->setObjectName(QStringLiteral("muted"));
    scl->addWidget(m_scNote);
    m_scatter = new QwtPlot;
    auto *grid = new QwtPlotGrid;
    grid->attach(m_scatter);
    m_scatterCurve = new QwtPlotCurve(QStringLiteral("samples"));
    m_scatterCurve->setStyle(QwtPlotCurve::NoCurve);
    m_scatterCurve->attach(m_scatter);
    m_fitCurve = new QwtPlotCurve(QStringLiteral("fit"));
    m_fitCurve->attach(m_scatter);
    scl->addWidget(m_scatter, 1);
    m_tabs->addTab(scTab, QStringLiteral("Temperature vs load"));

    connect(open, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Open log"), QString(),
                                                          QStringLiteral("Telemetry logs (*.jsonl *.json *.csv);;All files (*)"));
        QString err;
        if (!path.isEmpty() && !loadFile(path, &err)) {
            QMessageBox::warning(this, QStringLiteral("Open log"), err);
        }
    });
    connect(useBuf, &QPushButton::clicked, this, [this] {
        const TelemetryStore &st = m_s.store();
        QVector<TelemetryFrame> frames;
        frames.reserve(st.size());
        const QStringList chs = st.channels();
        for (int i = 0; i < st.size(); i++) {
            TelemetryFrame f;
            f.tMs = st.t(i);
            for (const QString &c : chs) {
                const double v = st.value(c, i);
                if (!std::isnan(v)) {
                    f.num.insert(c, v);
                }
            }
            frames.push_back(std::move(f));
        }
        useFrames(frames, QStringLiteral("session buffer (%1 frames)").arg(frames.size()));
    });
    connect(m_filter, &QLineEdit::textChanged, this, [this](const QString &t) {
        for (int r = 0; r < m_stats->rowCount(); r++) {
            m_stats->setRowHidden(r, !t.isEmpty() && !m_stats->item(r, 0)->text().contains(t, Qt::CaseInsensitive));
        }
    });
    connect(m_stats, &QTableWidget::itemSelectionChanged, this, &AnalysisPage::plotSelection);
    connect(calc, &QPushButton::clicked, this, &AnalysisPage::computeEnergy);
    connect(draw, &QPushButton::clicked, this, &AnalysisPage::computeScatter);
    connect(csv, &QPushButton::clicked, this, &AnalysisPage::exportCsv);
    connect(png, &QPushButton::clicked, this, &AnalysisPage::exportPng);
    applyTheme();
}

void AnalysisPage::applyTheme()
{
    m_plot->applyTheme();
    Plot::style(m_scatter);
    m_scatterCurve->setSymbol(new QwtSymbol(QwtSymbol::Ellipse, Theme::seriesColor(0), Qt::NoPen, QSize(4, 4)));
    m_fitCurve->setPen(QPen(Theme::color("warn"), 1.6, Qt::DashLine));
    m_scatter->replot();
}

int AnalysisPage::channelCount() const { return m_stats->rowCount(); }

bool AnalysisPage::loadFile(const QString &path, QString *err)
{
    QVector<TelemetryFrame> frames;
    if (!LogIo::load(path, frames, err)) {
        return false;
    }
    useFrames(frames, QStringLiteral("%1 (%2 frames)").arg(path).arg(frames.size()));
    return true;
}

void AnalysisPage::useFrames(QVector<TelemetryFrame> frames, const QString &source)
{
    m_frames = std::move(frames);
    m_store = TelemetryStore(std::max(16, static_cast<int>(m_frames.size())));
    for (const TelemetryFrame &f : m_frames) {
        m_store.append(f);
    }
    const double dur = m_frames.isEmpty() ? 0.0 : (m_frames.last().tMs - m_frames.first().tMs) / 1000.0;
    m_source->setText(QStringLiteral("%1 · %2 s").arg(source).arg(dur, 0, 'f', 2));
    rebuildStats();
    const QStringList chs = m_store.channels();
    for (QComboBox *c : {m_pDc, m_pMech, m_scX, m_scY}) {
        c->clear();
        c->addItems(chs);
    }
    pickDefault(m_pDc, {QStringLiteral("plant.p_dc_w"), QStringLiteral("foc.p_elec_w")});
    pickDefault(m_pMech, {QStringLiteral("plant.p_mech_w")});
    pickDefault(m_scX, {QStringLiteral("foc.i_rms_a"), QStringLiteral("inv.torque_applied_nm")});
    pickDefault(m_scY, {QStringLiteral("temps.mod_max_c"), QStringLiteral("inv.t_module_c")});
    // events: state changes and DTC transitions
    m_events->clear();
    QVector<QPair<double, QString>> marks;
    const ProtocolInfo &pi = ProtocolInfo::instance();
    int lastSm = -1;
    DtcTracker dt;
    for (const TelemetryFrame &f : m_frames) {
        const int sm = static_cast<int>(f.pick({"state.sm", "inv.state"}, -1));
        if (sm >= 0 && sm != lastSm) {
            if (lastSm >= 0) {
                const QString text = QStringLiteral("%1 → %2").arg(pi.stateName(lastSm), pi.stateName(sm));
                marks.push_back({f.tMs, pi.stateName(sm)});
                m_events->addItem(QStringLiteral("%1 s  state %2").arg(f.tMs / 1000.0, 0, 'f', 3).arg(text));
            }
            lastSm = sm;
        }
        for (const DtcEvent &e : dt.update(f)) {
            const QString what = (e.kind == DtcEvent::Kind::Raised) ? QStringLiteral("raised")
                               : (e.kind == DtcEvent::Kind::Cleared) ? QStringLiteral("cleared") : QStringLiteral("again");
            auto *it = new QListWidgetItem(QStringLiteral("%1 s  %2 %3").arg(f.tMs / 1000.0, 0, 'f', 3).arg(pi.dtcName(e.id), what));
            it->setForeground(e.kind == DtcEvent::Kind::Raised ? Theme::color("crit") : Theme::color("muted"));
            m_events->addItem(it);
            if (e.kind == DtcEvent::Kind::Raised) {
                marks.push_back({f.tMs, pi.dtcName(e.id)});
            }
        }
    }
    if (m_events->count() == 0) {
        m_events->addItem(QStringLiteral("No state change or DTC in this log"));
    }
    m_plot->setMarkers(marks);
    if (!m_frames.isEmpty()) {
        m_plot->setAxisScale(QwtAxis::XBottom, m_frames.first().tMs / 1000.0, m_frames.last().tMs / 1000.0);
    }
    if (m_stats->rowCount() > 0) {
        for (int r = 0; r < m_stats->rowCount(); r++) {
            if (m_stats->item(r, 0)->text() == QLatin1String("motion.speed_rpm")) {
                m_stats->selectRow(r);
            }
        }
    }
    plotSelection();
}

void AnalysisPage::rebuildStats()
{
    const QStringList chs = m_store.channels();
    m_stats->setSortingEnabled(false);
    m_stats->setRowCount(static_cast<int>(chs.size()));
    for (int r = 0; r < chs.size(); r++) {
        QVector<double> v;
        v.reserve(m_store.size());
        for (int i = 0; i < m_store.size(); i++) {
            v.push_back(m_store.value(chs[r], i));
        }
        const Dsp::Stats s = Dsp::stats(v);
        m_stats->setItem(r, 0, new QTableWidgetItem(chs[r]));
        const double vals[] = {static_cast<double>(s.n), s.min, s.max, s.mean, s.rms, s.std};
        for (int c = 0; c < 6; c++) {
            auto *it = new QTableWidgetItem;
            it->setData(Qt::DisplayRole, (c == 0) ? QVariant(s.n) : (s.n ? QVariant(vals[c]) : QVariant(QStringLiteral("—"))));
            it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_stats->setItem(r, c + 1, it);
        }
    }
    m_stats->setSortingEnabled(true);
}

void AnalysisPage::plotSelection()
{
    QStringList chs;
    for (const QModelIndex &i : m_stats->selectionModel()->selectedRows()) {
        chs << m_stats->item(i.row(), 0)->text();
    }
    m_plot->setChannels(chs.mid(0, 8));
    m_plot->refresh(m_store);
}

void AnalysisPage::computeEnergy()
{
    QVector<double> t, pd, pm;
    for (int i = 0; i < m_store.size(); i++) {
        t.push_back(m_store.t(i));
        pd.push_back(m_store.value(m_pDc->currentText(), i));
        pm.push_back(m_store.value(m_pMech->currentText(), i));
    }
    const double wh = 1.0 / 3600.0;
    const double edp = Dsp::integratePart(t, pd, true) * wh, edn = -Dsp::integratePart(t, pd, false) * wh;
    const double emp = Dsp::integratePart(t, pm, true) * wh, emn = -Dsp::integratePart(t, pm, false) * wh;
    const double dur = t.isEmpty() ? 0.0 : (t.last() - t.first()) / 1000.0;
    m_energy->setValue(QStringLiteral("dur"), QStringLiteral("%1 s").arg(dur, 0, 'f', 2));
    m_energy->setValue(QStringLiteral("edp"), QStringLiteral("%1 Wh").arg(edp, 0, 'f', 3));
    m_energy->setValue(QStringLiteral("edn"), QStringLiteral("%1 Wh").arg(edn, 0, 'f', 3));
    m_energy->setValue(QStringLiteral("emp"), QStringLiteral("%1 Wh").arg(emp, 0, 'f', 3));
    m_energy->setValue(QStringLiteral("emn"), QStringLiteral("%1 Wh").arg(emn, 0, 'f', 3));
    m_energy->setValue(QStringLiteral("etam"), edp > 1e-9 ? QStringLiteral("%1 %").arg(100.0 * emp / edp, 0, 'f', 2) : QStringLiteral("—"));
    m_energy->setValue(QStringLiteral("etar"), emn > 1e-9 ? QStringLiteral("%1 %").arg(100.0 * edn / emn, 0, 'f', 2) : QStringLiteral("—"));
    m_energy->setValue(QStringLiteral("pavg"), dur > 0 ? QStringLiteral("%1 kW").arg((edp - edn) / wh / dur / 1000.0, 0, 'f', 2) : QStringLiteral("—"));
}

void AnalysisPage::computeScatter()
{
    QVector<double> x, y;
    const QString cx = m_scX->currentText(), cy = m_scY->currentText();
    for (int i = 0; i < m_store.size(); i++) {
        const double a = m_store.value(cx, i), b = m_store.value(cy, i);
        if (std::isfinite(a) && std::isfinite(b)) {
            x.push_back(a);
            y.push_back(b);
        }
    }
    m_scatterCurve->setSamples(x, y);
    m_scatter->setAxisTitle(QwtAxis::XBottom, cx);
    m_scatter->setAxisTitle(QwtAxis::YLeft, cy);
    if (x.size() >= 2) {
        const Dsp::Stats sx = Dsp::stats(x), sy = Dsp::stats(y);
        double sxy = 0;
        for (int i = 0; i < x.size(); i++) {
            sxy += (x[i] - sx.mean) * (y[i] - sy.mean);
        }
        const double varx = sx.std * sx.std * x.size();
        const double slope = varx > 1e-12 ? sxy / varx : 0.0;
        const double icpt = sy.mean - slope * sx.mean;
        m_fitCurve->setSamples(QVector<double>{sx.min, sx.max}, QVector<double>{icpt + slope * sx.min, icpt + slope * sx.max});
        m_scNote->setText(QStringLiteral("%1 points · least-squares fit: %2 = %3 + %4 × %5 (a first look, not a thermal model)")
                              .arg(x.size()).arg(cy).arg(icpt, 0, 'g', 4).arg(slope, 0, 'g', 4).arg(cx));
    } else {
        m_fitCurve->setSamples(QVector<double>(), QVector<double>());
        m_scNote->setText(QStringLiteral("Not enough paired samples"));
    }
    m_scatter->replot();
}

void AnalysisPage::exportCsv()
{
    if (m_frames.isEmpty()) {
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export CSV"), QStringLiteral("analysis.csv"), QStringLiteral("CSV (*.csv)"));
    if (path.isEmpty()) {
        return;
    }
    QStringList chs = m_plot->channels();
    if (chs.isEmpty()) {
        chs = m_store.channels();
    }
    QString err;
    if (!LogIo::saveCsv(path, m_frames, chs, &err)) {
        QMessageBox::warning(this, QStringLiteral("Export CSV"), err);
    }
}

void AnalysisPage::exportPng()
{
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export PNG"), QStringLiteral("analysis.png"), QStringLiteral("PNG (*.png)"));
    if (!path.isEmpty() && !m_tabs->currentWidget()->grab().save(path)) {
        QMessageBox::warning(this, QStringLiteral("Export PNG"), QStringLiteral("Could not write %1").arg(path));
    }
}
