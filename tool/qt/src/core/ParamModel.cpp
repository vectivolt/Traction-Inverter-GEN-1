#include "ParamModel.h"

#include <QBrush>
#include <QColor>

#include <cmath>

namespace {
QString fmt(double v, const CalRow &r)
{
    if (std::isnan(v)) {
        return QStringLiteral("—");
    }
    if (r.isInteger()) {
        return (r.name == QLatin1String("cal_fs26_prog_id")) ? QStringLiteral("0x%1").arg(static_cast<qint64>(v), 4, 16, QLatin1Char('0')).toUpper().replace(QLatin1String("0X"), QLatin1String("0x"))
                                                           : QString::number(static_cast<qint64>(v));
    }
    return QString::number(v, 'g', 7);
}
bool same(double a, double b) { return std::abs(a - b) <= 1e-6 * std::max(1.0, std::max(std::abs(a), std::abs(b))); }
} // namespace

ParamModel::ParamModel(QObject *parent) : QAbstractTableModel(parent)
{
    for (const CalRow &c : ProtocolInfo::instance().cal) {
        Row r;
        r.def = c;
        r.device = c.def;
        m_rows.push_back(r);
    }
}

int ParamModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : static_cast<int>(m_rows.size()); }
int ParamModel::columnCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : ColumnCount; }

double ParamModel::effective(int i) const
{
    const Row &r = m_rows[i];
    return r.hasEdit ? r.edited : (r.haveDevice ? r.device : r.def.def);
}

bool ParamModel::isDirty(int i) const
{
    const Row &r = m_rows[i];
    return r.hasEdit && !(r.haveDevice && same(r.edited, r.device));
}

int ParamModel::dirtyCount() const
{
    int n = 0;
    for (int i = 0; i < m_rows.size(); i++) {
        n += isDirty(i) ? 1 : 0;
    }
    return n;
}

QString ParamModel::rowProblem(int i, double v) const
{
    const CalRow &c = m_rows[i].def;
    if (!std::isfinite(v)) {
        return QStringLiteral("%1: not a finite number").arg(c.name);
    }
    if (c.isInteger() && v != std::floor(v)) {
        return QStringLiteral("%1: %2 must be a whole number").arg(c.name, QString::number(v));
    }
    if (v < c.min || v > c.max) {
        return QStringLiteral("%1 = %2 outside [%3, %4] %5").arg(c.name, fmt(v, c), fmt(c.min, c), fmt(c.max, c), c.unit).trimmed();
    }
    return {};
}

QVariant ParamModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size()) {
        return {};
    }
    const int i = index.row();
    const Row &r = m_rows[i];
    const CalRow &c = r.def;
    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case Name: return c.name;
        case Value: return fmt(effective(i), c);
        case Device: return r.haveDevice ? fmt(r.device, c) : QStringLiteral("—");
        case Default: return fmt(c.def, c);
        case Min: return fmt(c.min, c);
        case Max: return fmt(c.max, c);
        case Unit: return c.unit;
        case Group: return c.group;
        default: return {};
        }
    case Qt::EditRole:
        return (index.column() == Value) ? QVariant(effective(i)) : QVariant();
    case Qt::ToolTipRole:
        return QStringLiteral("<b>%1</b><br/>%2<br/><br/>type %3 · range [%4, %5] %6 · default %7")
            .arg(c.name, c.description.toHtmlEscaped(), c.type, fmt(c.min, c), fmt(c.max, c), c.unit, fmt(c.def, c));
    case Qt::TextAlignmentRole:
        return (index.column() >= Value && index.column() <= Max) ? QVariant(Qt::AlignRight | Qt::AlignVCenter)
                                                                   : QVariant(Qt::AlignLeft | Qt::AlignVCenter);
    case DirtyRole: return isDirty(i);
    case DiffersRole: return !same(effective(i), c.def);
    case DeviceDiffersRole: return r.haveDevice && !same(r.device, c.def);
    case GroupRole: return c.group;
    case DescriptionRole: return c.description;
    case InvalidRole: return !rowProblem(i, effective(i)).isEmpty();
    case NameRole: return c.name;
    default: return {};
    }
}

QVariant ParamModel::headerData(int section, Qt::Orientation o, int role) const
{
    if (o != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }
    static const char *const H[] = {"Parameter", "Value", "Device", "Default", "Min", "Max", "Unit", "Group"};
    return (section >= 0 && section < ColumnCount) ? QString::fromLatin1(H[section]) : QString();
}

Qt::ItemFlags ParamModel::flags(const QModelIndex &index) const
{
    Qt::ItemFlags f = QAbstractTableModel::flags(index);
    if (index.column() == Value) {
        f |= Qt::ItemIsEditable;
    }
    return f;
}

bool ParamModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (role != Qt::EditRole || index.column() != Value) {
        return false;
    }
    bool ok = false;
    QString s = value.toString().trimmed();
    double v = s.startsWith(QLatin1String("0x"), Qt::CaseInsensitive) ? static_cast<double>(s.mid(2).toLongLong(&ok, 16))
                                                                       : s.toDouble(&ok);
    if (value.typeId() == QMetaType::Double) {
        v = value.toDouble();
        ok = true;
    }
    return ok && setEdited(m_rows[index.row()].def.name, v);
}

bool ParamModel::setEdited(const QString &name, double v, const QString &why)
{
    const int i = indexOf(name);
    if (i < 0) {
        return false;
    }
    Row &r = m_rows[i];
    const double before = effective(i);
    if (r.hasEdit && same(r.edited, v)) {
        return true;
    }
    r.edited = v;
    r.hasEdit = true;
    logChange(name, before, v, why);
    Q_EMIT dataChanged(index(i, 0), index(i, ColumnCount - 1));
    Q_EMIT dirtyChanged(dirtyCount());
    return true;
}

void ParamModel::revertParam(const QString &name)
{
    const int i = indexOf(name);
    if (i < 0 || !m_rows[i].hasEdit) {
        return;
    }
    const double before = effective(i);
    m_rows[i].hasEdit = false;
    logChange(name, before, effective(i), QStringLiteral("reverted"));
    Q_EMIT dataChanged(index(i, 0), index(i, ColumnCount - 1));
    Q_EMIT dirtyChanged(dirtyCount());
}

void ParamModel::revertAll()
{
    for (const Row &r : m_rows) {
        if (r.hasEdit) {
            revertParam(r.def.name);
        }
    }
}

void ParamModel::editAllToDefaults()
{
    for (int i = 0; i < m_rows.size(); i++) {
        if (!same(effective(i), m_rows[i].def.def)) {
            setEdited(m_rows[i].def.name, m_rows[i].def.def, QStringLiteral("default"));
        }
    }
}

int ParamModel::indexOf(const QString &name) const
{
    for (int i = 0; i < m_rows.size(); i++) {
        if (m_rows[i].def.name == name) {
            return i;
        }
    }
    return -1;
}

QStringList ParamModel::setDeviceTable(const QJsonArray &params)
{
    QStringList mismatch;
    for (const QJsonValue &p : params) {
        const QJsonObject o = p.toObject();
        const QString name = o.value(QLatin1String("name")).toString();
        const int i = indexOf(name);
        if (i < 0) {
            mismatch << QStringLiteral("%1: on the device, not in this build's gen-params.mjs export").arg(name);
            continue;
        }
        Row &r = m_rows[i];
        r.device = o.value(QLatin1String("value")).toDouble();
        r.haveDevice = true;
        const double mn = o.value(QLatin1String("min")).toDouble(r.def.min);
        const double mx = o.value(QLatin1String("max")).toDouble(r.def.max);
        if (!same(mn, r.def.min) || !same(mx, r.def.max)) {
            mismatch << QStringLiteral("%1: device range [%2, %3], this build [%4, %5]")
                            .arg(name).arg(mn).arg(mx).arg(r.def.min).arg(r.def.max);
        }
        if (r.hasEdit && same(r.edited, r.device)) {
            r.hasEdit = false; // the device now holds the edit
        }
    }
    m_haveDevice = true;
    Q_EMIT dataChanged(index(0, 0), index(rowCount() - 1, ColumnCount - 1));
    Q_EMIT dirtyChanged(dirtyCount());
    return mismatch;
}

void ParamModel::clearDevice()
{
    for (Row &r : m_rows) {
        r.haveDevice = false;
    }
    m_haveDevice = false;
    Q_EMIT dataChanged(index(0, 0), index(rowCount() - 1, ColumnCount - 1));
    Q_EMIT dirtyChanged(dirtyCount());
}

QStringList ParamModel::validate() const
{
    QStringList p;
    for (int i = 0; i < m_rows.size(); i++) {
        const QString e = rowProblem(i, effective(i));
        if (!e.isEmpty()) {
            p << e;
        }
    }
    // params.json cross_field_rules (ti_params_validate()), on the edited set and the connected SKU's constants
    const ProtocolInfo &pi = ProtocolInfo::instance();
    const auto lookup = [this, &pi](const QString &n) {
        const int i = indexOf(n);
        if (i >= 0) {
            return effective(i);
        }
        const ConstRow *c = pi.constRow(n);
        return c ? c->valueFor(m_sku) : std::nan("");
    };
    for (const CrossRule &r : pi.rules) {
        bool ok = false;
        const bool holds = ProtocolInfo::evalRule(r.expr, lookup, &ok);
        if (!ok) {
            p << QStringLiteral("cannot evaluate the rule \"%1\" for SKU %2").arg(r.expr, m_sku);
        } else if (!holds) {
            QStringList vals;
            for (const QString &f : r.calFields) {
                vals << QStringLiteral("%1 = %2").arg(f, QString::number(lookup(f)));
            }
            p << QStringLiteral("rule %1 fails%2").arg(r.expr, vals.isEmpty() ? QString() : QStringLiteral(" (") + vals.join(QStringLiteral(", ")) + QLatin1Char(')'));
        }
    }
    return p;
}

QVector<QPair<QString, double>> ParamModel::pendingWrites() const
{
    QVector<QPair<QString, double>> w;
    for (int i = 0; i < m_rows.size(); i++) {
        if (isDirty(i)) {
            w.push_back({m_rows[i].def.name, m_rows[i].edited});
        }
    }
    return w;
}

void ParamModel::writeResult(const QString &name, bool ok, double deviceValue, const QString &message)
{
    const int i = indexOf(name);
    if (i < 0) {
        return;
    }
    Row &r = m_rows[i];
    const double before = r.haveDevice ? r.device : r.def.def;
    if (ok) {
        r.device = deviceValue;
        r.haveDevice = true;
        r.hasEdit = false;
        logChange(name, before, deviceValue, QStringLiteral("written"));
    } else {
        logChange(name, before, r.edited, QStringLiteral("refused: %1").arg(message));
    }
    Q_EMIT dataChanged(index(i, 0), index(i, ColumnCount - 1));
    Q_EMIT dirtyChanged(dirtyCount());
}

QJsonObject ParamModel::exportJson(const QString &fwId, const QString &sku) const
{
    QJsonObject values;
    for (int i = 0; i < m_rows.size(); i++) {
        values.insert(m_rows[i].def.name, effective(i));
    }
    return {{QStringLiteral("format"), QStringLiteral("traction-tool-params")},
            {QStringLiteral("version"), 1},
            {QStringLiteral("fw_id"), fwId},
            {QStringLiteral("sku"), sku},
            {QStringLiteral("exported"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
            {QStringLiteral("params"), values}};
}

QStringList ParamModel::importJson(const QJsonObject &o)
{
    QStringList problems;
    if (o.value(QLatin1String("format")).toString() != QLatin1String("traction-tool-params")) {
        return {QStringLiteral("not a Traction Tool parameter file (format \"traction-tool-params\" expected)")};
    }
    const QString fw = o.value(QLatin1String("fw_id")).toString();
    if (!fw.isEmpty() && fw != ProtocolInfo::instance().fwId) {
        problems << QStringLiteral("exported for firmware %1, this build knows %2: check every value")
                        .arg(fw, ProtocolInfo::instance().fwId);
    }
    const QJsonObject values = o.value(QLatin1String("params")).toObject();
    for (auto it = values.begin(); it != values.end(); ++it) {
        const int i = indexOf(it.key());
        if (i < 0) {
            problems << QStringLiteral("%1: unknown parameter, skipped").arg(it.key());
            continue;
        }
        if (!it.value().isDouble()) {
            problems << QStringLiteral("%1: not a number, skipped").arg(it.key());
            continue;
        }
        if (!same(effective(i), it.value().toDouble())) {
            setEdited(it.key(), it.value().toDouble(), QStringLiteral("imported"));
        }
    }
    return problems + validate();
}

void ParamModel::logChange(const QString &name, double from, double to, const QString &result)
{
    m_log.push_back({QDateTime::currentDateTime(), name, from, to, result});
}

QJsonArray ParamModel::changeLogJson() const
{
    QJsonArray a;
    for (const ParamChange &c : m_log) {
        a.append(QJsonObject{{QStringLiteral("when"), c.when.toString(Qt::ISODateWithMs)},
                             {QStringLiteral("name"), c.name},
                             {QStringLiteral("from"), c.from},
                             {QStringLiteral("to"), c.to},
                             {QStringLiteral("result"), c.result}});
    }
    return a;
}
