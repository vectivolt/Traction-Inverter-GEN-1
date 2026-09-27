#include "ParametersPage.h"

#include "ProtocolInfo.h"
#include "Widgets.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QShortcut>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QTextBrowser>

#include <cmath>

namespace {
// Filters by group, free text (name, group, description) and "changed" (dirty or different from the device/default).
class ParamFilter : public QSortFilterProxyModel
{
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    QString group, text;
    int diff = 0; // 0 all, 1 edited = differs from the device, 2 device differs from the default, 3 value differs from it
    // the filter fields change first, then the view is told (the Qt 6.10+ protocol)
    void refilter()
    {
        beginFilterChange();
        endFilterChange(QSortFilterProxyModel::Direction::Rows);
    }

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override
    {
        const QModelIndex i = sourceModel()->index(row, 0, parent);
        if (!group.isEmpty() && i.data(ParamModel::GroupRole).toString() != group) {
            return false;
        }
        const int role = (diff == 1) ? ParamModel::DirtyRole : (diff == 2) ? ParamModel::DeviceDiffersRole : ParamModel::DiffersRole;
        if (diff != 0 && !i.data(role).toBool()) {
            return false;
        }
        if (text.isEmpty()) {
            return true;
        }
        return i.data(ParamModel::NameRole).toString().contains(text, Qt::CaseInsensitive) ||
               i.data(ParamModel::GroupRole).toString().contains(text, Qt::CaseInsensitive) ||
               i.data(ParamModel::DescriptionRole).toString().contains(text, Qt::CaseInsensitive);
    }
};

// Paints the dirty/invalid state (a left bar and a bold value) and edits with a range-limited spin box.
class ParamDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const override
    {
        QStyleOptionViewItem o = opt;
        const bool dirty = idx.data(ParamModel::DirtyRole).toBool();
        const bool invalid = idx.data(ParamModel::InvalidRole).toBool();
        if (idx.column() == ParamModel::Value && (dirty || invalid)) {
            o.font.setWeight(QFont::DemiBold);
            o.palette.setColor(QPalette::Text, invalid ? Theme::color("crit") : Theme::color("accent"));
        } else if (idx.column() >= ParamModel::Device && idx.column() <= ParamModel::Max) {
            o.palette.setColor(QPalette::Text, Theme::color("muted"));
        }
        if (idx.column() == ParamModel::Value || idx.column() == ParamModel::Device || idx.column() == ParamModel::Default) {
            o.font.setFeature(QFont::Tag("tnum"), 1);
        }
        QStyledItemDelegate::paint(p, o, idx);
        if (idx.column() == 0 && (dirty || invalid)) {
            p->fillRect(QRect(o.rect.left(), o.rect.top() + 3, 3, o.rect.height() - 6),
                        invalid ? Theme::color("crit") : Theme::color("accent"));
        }
    }
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &, const QModelIndex &idx) const override
    {
        const auto *m = qobject_cast<const ParamModel *>(static_cast<const QSortFilterProxyModel *>(idx.model())->sourceModel());
        const QModelIndex src = static_cast<const QSortFilterProxyModel *>(idx.model())->mapToSource(idx);
        const CalRow &r = m->row(src.row());
        auto *e = new QDoubleSpinBox(parent);
        e->setRange(r.min, r.max); // the editor cannot leave the range; imports can, and validation says so
        e->setDecimals(r.isInteger() ? 0 : 6);
        e->setSingleStep(r.isInteger() ? 1.0 : std::max(1e-6, (r.max - r.min) / 100.0));
        e->setSuffix(r.unit.isEmpty() ? QString() : QLatin1Char(' ') + r.unit);
        e->setFrame(false);
        return e;
    }
    void setEditorData(QWidget *e, const QModelIndex &idx) const override
    {
        static_cast<QDoubleSpinBox *>(e)->setValue(idx.data(Qt::EditRole).toDouble());
    }
    void setModelData(QWidget *e, QAbstractItemModel *m, const QModelIndex &idx) const override
    {
        m->setData(idx, static_cast<QDoubleSpinBox *>(e)->value(), Qt::EditRole);
    }
};
} // namespace

ParametersPage::ParametersPage(Session &s, QWidget *parent)
    : Page(s, QStringLiteral("Parameters"),
           QStringLiteral("The calibration table of firmware/tools/gen-params.mjs (tool/protocol/params.json). Edits are "
                          "local until written; the device runs its own ti_params_validate() on every write and refuses "
                          "a set that fails it."),
           parent)
{
    auto *bar = new QHBoxLayout;
    bar->setSpacing(Theme::S2);
    m_search = new QLineEdit;
    m_search->setPlaceholderText(QStringLiteral("Search name, group, basis…  (Ctrl+F)"));
    m_search->setClearButtonEnabled(true);
    m_group = new QComboBox;
    m_group->addItem(QStringLiteral("All groups"), QString());
    QStringList groups;
    for (const CalRow &c : ProtocolInfo::instance().cal) {
        if (!groups.contains(c.group)) {
            groups << c.group;
        }
    }
    for (const QString &g : groups) {
        m_group->addItem(g, g);
    }
    m_diff = new QComboBox;
    m_diff->addItems({QStringLiteral("All rows"), QStringLiteral("Edited: differs from the device"),
                      QStringLiteral("Device differs from the default"), QStringLiteral("Value differs from the default")});
    m_diff->setToolTip(QStringLiteral("Diff: the edited set against the device's table, or the device against gen-params.mjs"));
    m_read = new QPushButton(QStringLiteral("Read"));
    m_read->setToolTip(QStringLiteral("Ask the device for its table (params)"));
    m_write = new QPushButton(QStringLiteral("Write…"));
    m_write->setObjectName(QStringLiteral("primary"));
    m_revert = new QPushButton(QStringLiteral("Revert"));
    m_defaults = new QPushButton(QStringLiteral("Defaults"));
    m_defaults->setToolTip(QStringLiteral("Edit every row to its gen-params.mjs default (not written until Write)"));
    auto *imp = new QPushButton(QStringLiteral("Import…"));
    auto *exp = new QPushButton(QStringLiteral("Export…"));
    bar->addWidget(m_search, 2);
    bar->addWidget(m_group, 1);
    bar->addWidget(m_diff);
    bar->addStretch(0);
    for (QPushButton *b : {m_read, m_revert, m_defaults, imp, exp, m_write}) {
        bar->addWidget(b);
    }
    content()->addLayout(bar);
    m_status = new QLabel;
    m_status->setObjectName(QStringLiteral("muted"));
    content()->addWidget(m_status);

    auto *split = new QSplitter(Qt::Horizontal);
    m_proxy = new ParamFilter(this);
    m_proxy->setSourceModel(&m_s.params());
    m_table = new QTableView;
    m_table->setObjectName(QStringLiteral("paramTable"));
    m_table->setModel(m_proxy);
    m_table->setItemDelegate(new ParamDelegate(m_table));
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setAlternatingRowColors(true);
    m_table->setSortingEnabled(true);
    m_table->sortByColumn(ParamModel::Group, Qt::AscendingOrder);
    m_table->verticalHeader()->hide();
    m_table->verticalHeader()->setDefaultSectionSize(28);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(ParamModel::Group, QHeaderView::Stretch);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed | QAbstractItemView::AnyKeyPressed);
    m_table->setShowGrid(false);
    split->addWidget(m_table);
    auto *side = new QWidget;
    auto *sl = new QVBoxLayout(side);
    sl->setContentsMargins(Theme::S3, 0, 0, 0);
    auto *dTitle = new QLabel(QStringLiteral("PARAMETER"));
    dTitle->setObjectName(QStringLiteral("cardTitle"));
    m_detail = new QTextBrowser;
    m_detail->setOpenLinks(false);
    auto *pTitle = new QLabel(QStringLiteral("VALIDATION"));
    pTitle->setObjectName(QStringLiteral("cardTitle"));
    m_problems = new QListWidget;
    m_problems->setObjectName(QStringLiteral("paramProblems"));
    m_problems->setWordWrap(true);
    auto *lTitle = new QLabel(QStringLiteral("CHANGE LOG"));
    lTitle->setObjectName(QStringLiteral("cardTitle"));
    m_log = new QListWidget;
    m_log->setWordWrap(true);
    sl->addWidget(dTitle);
    sl->addWidget(m_detail, 3);
    sl->addWidget(pTitle);
    sl->addWidget(m_problems, 2);
    sl->addWidget(lTitle);
    sl->addWidget(m_log, 2);
    split->addWidget(side);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 1);
    split->setSizes({900, 380});
    content()->addWidget(split, 1);

    auto *f = static_cast<ParamFilter *>(m_proxy);
    connect(m_search, &QLineEdit::textChanged, this, [f](const QString &t) {
        f->text = t;
        f->refilter();
    });
    connect(m_group, &QComboBox::currentIndexChanged, this, [this, f] {
        f->group = m_group->currentData().toString();
        f->refilter();
    });
    connect(m_diff, &QComboBox::currentIndexChanged, this, [f](int i) {
        f->diff = i;
        f->refilter();
    });
    connect(m_table->selectionModel(), &QItemSelectionModel::currentRowChanged, this, &ParametersPage::updateDetail);
    connect(&m_s.params(), &ParamModel::dataChanged, this, [this] {
        updateDetail();
        updateState();
    });
    connect(&m_s.params(), &ParamModel::dirtyChanged, this, &ParametersPage::updateState);
    connect(&m_s, &Session::paramsWritten, this, [this](int ok, int refused) {
        m_status->setText(QStringLiteral("Write finished: %1 accepted, %2 refused by the device").arg(ok).arg(refused));
        refreshLog();
    });
    connect(&m_s, &Session::transportChanged, this, &ParametersPage::updateState);
    connect(&m_s, &Session::linkChanged, this, &ParametersPage::updateState);
    connect(m_read, &QPushButton::clicked, this, [this] { m_s.command(QStringLiteral("params")); });
    connect(m_write, &QPushButton::clicked, this, &ParametersPage::write);
    connect(m_revert, &QPushButton::clicked, this, [this] { m_s.params().revertAll(); refreshLog(); });
    connect(m_defaults, &QPushButton::clicked, this, [this] { m_s.params().editAllToDefaults(); refreshLog(); });
    connect(imp, &QPushButton::clicked, this, &ParametersPage::importFile);
    connect(exp, &QPushButton::clicked, this, &ParametersPage::exportFile);
    auto *find = new QShortcut(QKeySequence::Find, this, [this] { m_search->setFocus(); m_search->selectAll(); });
    find->setContext(Qt::WidgetWithChildrenShortcut);
    updateState();
}

void ParametersPage::updateDetail()
{
    const QModelIndex cur = m_proxy->mapToSource(m_table->currentIndex());
    if (!cur.isValid()) {
        m_detail->setHtml(QStringLiteral("<p style='color:%1'>Select a parameter.</p>").arg(Theme::color("dim").name()));
        return;
    }
    ParamModel &m = m_s.params();
    const int i = cur.row();
    const CalRow &c = m.row(i);
    const QString dim = Theme::color("muted").name();
    m_detail->setHtml(QStringLiteral("<h3 style='margin:0'>%1</h3><p style='color:%2;margin-top:2px'>%3 · %4</p>"
                                     "<p>%5</p><p style='color:%2'>References: %6</p>"
                                     "<table cellpadding='3'><tr><td style='color:%2'>Value</td><td><b>%7</b> %8%9</td></tr>"
                                     "<tr><td style='color:%2'>Device</td><td>%10</td></tr>"
                                     "<tr><td style='color:%2'>Default</td><td>%11</td></tr>"
                                     "<tr><td style='color:%2'>Range</td><td>[%12, %13]</td></tr></table>")
                          .arg(c.name, dim, c.group, c.type, c.description.toHtmlEscaped(),
                               c.refs.isEmpty() ? QStringLiteral("—") : c.refs.join(QStringLiteral(", ")),
                               m.data(m.index(i, ParamModel::Value)).toString(), c.unit,
                               m.isDirty(i) ? QStringLiteral(" (edited, not written)") : QString(),
                               m.data(m.index(i, ParamModel::Device)).toString(), m.data(m.index(i, ParamModel::Default)).toString(),
                               m.data(m.index(i, ParamModel::Min)).toString(), m.data(m.index(i, ParamModel::Max)).toString()));
}

void ParametersPage::updateState()
{
    ParamModel &m = m_s.params();
    const int dirty = static_cast<int>(m.pendingWrites().size());
    const QStringList problems = m.validate();
    m_problems->clear();
    if (problems.isEmpty()) {
        auto *it = new QListWidgetItem(QStringLiteral("The edited set passes every range and cross-field rule (SKU %1).").arg(m.sku()));
        it->setForeground(Theme::color("ok"));
        m_problems->addItem(it);
    } else {
        for (const QString &p : problems) {
            auto *it = new QListWidgetItem(p);
            it->setForeground(Theme::color("crit"));
            m_problems->addItem(it);
        }
    }
    ITransport *t = m_s.transport();
    const bool canWrite = t && t->commands().contains(QStringLiteral("param_set")) && m_s.isDeviceLive() && m_s.destructiveAllowed();
    m_write->setEnabled(canWrite && dirty > 0 && problems.isEmpty());
    m_write->setText(dirty ? QStringLiteral("Write %1…").arg(dirty) : QStringLiteral("Write…"));
    m_read->setEnabled(t && t->commands().contains(QStringLiteral("params")) && m_s.isDeviceLive());
    m_revert->setEnabled(dirty > 0);
    const QString dev = m.haveDevice() ? QStringLiteral("device table read") : QStringLiteral("no device table (showing the defaults)");
    m_status->setText(QStringLiteral("%1 parameters · %2 edited · %3%4").arg(m.rowCount()).arg(dirty).arg(dev,
                          t && !t->commands().contains(QStringLiteral("param_set"))
                              ? QStringLiteral(" · this transport cannot write parameters (the CAN interface has no parameter service)")
                              : QString()));
    refreshLog();
}

void ParametersPage::refreshLog()
{
    const auto &log = m_s.params().changeLog();
    if (m_log->count() == log.size()) {
        return;
    }
    m_log->clear();
    for (int i = static_cast<int>(log.size()) - 1; i >= 0 && m_log->count() < 300; i--) {
        const ParamChange &c = log[i];
        auto *it = new QListWidgetItem(QStringLiteral("%1  %2: %3 → %4  (%5)").arg(c.when.toString(QStringLiteral("HH:mm:ss")), c.name,
                                                                                 QString::number(c.from, 'g', 7), QString::number(c.to, 'g', 7), c.result));
        if (c.result.startsWith(QLatin1String("refused"))) {
            it->setForeground(Theme::color("crit"));
        } else if (c.result == QLatin1String("written")) {
            it->setForeground(Theme::color("ok"));
        }
        m_log->addItem(it);
    }
}

void ParametersPage::write()
{
    const auto w = m_s.params().pendingWrites();
    QStringList lines;
    for (const auto &p : w) {
        const int i = m_s.params().indexOf(p.first);
        lines << QStringLiteral("%1: %2 → %3 %4").arg(p.first, QString::number(m_s.params().device(i), 'g', 7),
                                                    QString::number(p.second, 'g', 7), m_s.params().row(i).unit);
    }
    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("Write parameters"));
    box.setText(QStringLiteral("Write %1 parameter(s) to the device?").arg(w.size()));
    box.setInformativeText(QStringLiteral("Each is one param_set; the device keeps only values its own ti_params_validate() "
                                          "accepts. Values read at initialisation (e.g. cal_fc_fraction) take effect at the next power-up."));
    box.setDetailedText(lines.join(QLatin1Char('\n')));
    box.setStandardButtons(QMessageBox::Cancel);
    QPushButton *go = box.addButton(QStringLiteral("Write"), QMessageBox::AcceptRole);
    box.setDefaultButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() != go) {
        return;
    }
    QString err;
    if (!m_s.writeParams(&err)) {
        QMessageBox::warning(this, QStringLiteral("Write parameters"), err);
        return;
    }
    m_status->setText(QStringLiteral("Writing %1 parameter(s)…").arg(w.size()));
}

void ParametersPage::importFile()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Import parameters"), QString(), QStringLiteral("JSON (*.json)"));
    if (path.isEmpty()) {
        return;
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, QStringLiteral("Import"), f.errorString());
        return;
    }
    const QJsonDocument d = QJsonDocument::fromJson(f.readAll());
    const QStringList problems = m_s.params().importJson(d.object());
    m_s.addEvent(QStringLiteral("info"), QStringLiteral("parameters imported from %1").arg(path));
    if (!problems.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Import"), QStringLiteral("Imported with remarks:\n\n%1").arg(problems.join(QLatin1Char('\n'))));
    }
    refreshLog();
}

void ParametersPage::exportFile()
{
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export parameters"), QStringLiteral("parameters.json"),
                                                      QStringLiteral("JSON (*.json)"));
    if (path.isEmpty()) {
        return;
    }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, QStringLiteral("Export"), f.errorString());
        return;
    }
    const QString fw = m_s.deviceInfo().value(QLatin1String("fw_id")).toString(ProtocolInfo::instance().fwId);
    f.write(QJsonDocument(m_s.params().exportJson(fw, m_s.params().sku())).toJson());
}
