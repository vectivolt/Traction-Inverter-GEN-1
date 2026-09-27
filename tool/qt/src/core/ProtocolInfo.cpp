#include "ProtocolInfo.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>
#include <cmath>

namespace {
QJsonObject readJson(const QString &path, QString *err)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        *err = QStringLiteral("cannot open %1").arg(path);
        return {};
    }
    QJsonParseError pe;
    const QJsonDocument d = QJsonDocument::fromJson(f.readAll(), &pe);
    if (!d.isObject()) {
        *err = QStringLiteral("%1: %2").arg(path, pe.errorString());
    }
    return d.object();
}

QString str(const QJsonValue &o, const char *k) { return o[QLatin1String(k)].toString(); }

QStringList enumNames(const QJsonObject &values)
{
    QStringList out;
    for (auto it = values.begin(); it != values.end(); ++it) {
        const int i = it.key().toInt();
        while (out.size() <= i) {
            out << QString();
        }
        out[i] = it.value().toString();
    }
    return out;
}

// ---- a small recursive-descent evaluator for params.json cross_field_rules ----
class Expr
{
public:
    Expr(const QString &s, const std::function<double(const QString &)> &lookup) : m_s(s), m_lookup(lookup) {}
    double parse(bool *ok)
    {
        const double v = orExpr();
        skip();
        *ok = m_ok && m_i == m_s.size();
        return v;
    }

private:
    void skip()
    {
        while (m_i < m_s.size() && m_s[m_i].isSpace()) {
            m_i++;
        }
    }
    bool eat(const char *tok)
    {
        skip();
        const QLatin1String t(tok);
        if (m_s.mid(m_i, t.size()) == t) {
            m_i += t.size();
            return true;
        }
        return false;
    }
    double orExpr()
    {
        double v = andExpr();
        while (eat("||")) {
            const double r = andExpr();
            v = (v != 0.0 || r != 0.0) ? 1.0 : 0.0;
        }
        return v;
    }
    double andExpr()
    {
        double v = cmp();
        while (eat("&&")) {
            const double r = cmp();
            v = (v != 0.0 && r != 0.0) ? 1.0 : 0.0;
        }
        return v;
    }
    double cmp()
    {
        const double a = sum();
        if (eat("<=")) return (a <= sum()) ? 1.0 : 0.0;
        if (eat(">=")) return (a >= sum()) ? 1.0 : 0.0;
        if (eat("==")) return (a == sum()) ? 1.0 : 0.0;
        if (eat("!=")) return (a != sum()) ? 1.0 : 0.0;
        if (eat("<")) return (a < sum()) ? 1.0 : 0.0;
        if (eat(">")) return (a > sum()) ? 1.0 : 0.0;
        return a;
    }
    double sum()
    {
        double v = product();
        for (;;) {
            if (eat("+")) {
                v += product();
            } else if (eat("-")) {
                v -= product();
            } else {
                return v;
            }
        }
    }
    double product()
    {
        double v = unary();
        for (;;) {
            if (eat("*")) {
                v *= unary();
            } else if (eat("/")) {
                v /= unary();
            } else {
                return v;
            }
        }
    }
    double unary()
    {
        if (eat("-")) {
            return -unary();
        }
        if (eat("!")) {
            return (unary() == 0.0) ? 1.0 : 0.0;
        }
        if (eat("(")) {
            const double v = orExpr();
            if (!eat(")")) {
                m_ok = false;
            }
            return v;
        }
        skip();
        const qsizetype start = m_i;
        if (m_i < m_s.size() && (m_s[m_i].isDigit() || m_s[m_i] == QLatin1Char('.'))) {
            while (m_i < m_s.size() && (m_s[m_i].isLetterOrNumber() || m_s[m_i] == QLatin1Char('.'))) {
                m_i++;
            }
            QString num = m_s.mid(start, m_i - start);
            while (num.endsWith(QLatin1Char('u')) || num.endsWith(QLatin1Char('f'))) {
                num.chop(1); // C suffixes (2u, 0.5f)
            }
            bool ok = false;
            const double v = num.toDouble(&ok);
            m_ok = m_ok && ok;
            return v;
        }
        while (m_i < m_s.size() && (m_s[m_i].isLetterOrNumber() || m_s[m_i] == QLatin1Char('_'))) {
            m_i++;
        }
        if (m_i == start) {
            m_ok = false;
            return std::nan("");
        }
        const double v = m_lookup(m_s.mid(start, m_i - start));
        if (std::isnan(v)) {
            m_ok = false;
        }
        return v;
    }

    QString m_s;
    const std::function<double(const QString &)> &m_lookup;
    qsizetype m_i = 0;
    bool m_ok = true;
};
} // namespace

ProtocolInfo &ProtocolInfo::instance()
{
    static ProtocolInfo s = [] {
        ProtocolInfo p;
        QString err;
        if (!p.load(QStringLiteral(":/protocol"), &err)) {
            qFatal("ProtocolInfo: %s", qPrintable(err)); // a build defect: the exports are embedded at build time
        }
        return p;
    }();
    return s;
}

bool ProtocolInfo::load(const QString &dir, QString *err)
{
    const QJsonObject params = readJson(dir + QStringLiteral("/params.json"), err);
    const QJsonObject dtcDoc = readJson(dir + QStringLiteral("/dtcs.json"), err);
    canFrames = readJson(dir + QStringLiteral("/can-frames.json"), err);
    if (params.isEmpty() || dtcDoc.isEmpty() || canFrames.isEmpty()) {
        return false;
    }
    fwId = params[QLatin1String("firmware")][QLatin1String("fw_id")].toString();
    cal.clear();
    for (const QJsonValue &r : params[QLatin1String("parameters")].toArray()) {
        CalRow c;
        c.name = str(r, "name");
        c.type = str(r, "type");
        c.unit = str(r, "unit");
        c.group = str(r, "group");
        c.description = str(r, "description");
        for (const QJsonValue &x : r[QLatin1String("refs")].toArray()) {
            c.refs << x.toString();
        }
        c.def = r[QLatin1String("default")].toDouble();
        c.min = r[QLatin1String("min")].toDouble();
        c.max = r[QLatin1String("max")].toDouble();
        cal.push_back(c);
    }
    constants.clear();
    for (const QJsonValue &r : params[QLatin1String("constants")].toArray()) {
        ConstRow c;
        c.name = str(r, "name");
        c.unit = str(r, "unit");
        c.group = str(r, "group");
        c.description = str(r, "description");
        c.perSku = r[QLatin1String("per_sku")].toBool();
        const auto num = [](const QJsonValue &v) {
            return v.isBool() ? (v.toBool() ? 1.0 : 0.0) : (v.isDouble() ? v.toDouble() : std::nan(""));
        };
        if (c.perSku) {
            const QJsonObject vals = r[QLatin1String("values")].toObject();
            for (auto it = vals.begin(); it != vals.end(); ++it) {
                c.skuValues.insert(it.key(), num(it.value()));
            }
        } else {
            c.value = num(r[QLatin1String("value")]);
        }
        constants.push_back(c);
    }
    skus.clear();
    for (const QJsonValue &s : params[QLatin1String("skus")].toArray()) {
        skus.push_back({s[QLatin1String("index")].toInt(), str(s, "key"), str(s, "id"), str(s, "name")});
    }
    rules.clear();
    for (const QJsonValue &r : params[QLatin1String("cross_field_rules")][QLatin1String("rules")].toArray()) {
        CrossRule c;
        c.expr = str(r, "expr");
        for (const QJsonValue &f : r[QLatin1String("cal_fields")].toArray()) {
            c.calFields << f.toString();
        }
        rules.push_back(c);
    }
    dtcs.clear();
    DtcInfo none;
    none.name = QStringLiteral("DTC_NONE");
    none.code = QStringLiteral("0xD10000");
    dtcs.push_back(none);
    for (const QJsonValue &r : dtcDoc[QLatin1String("dtcs")].toArray()) {
        DtcInfo d;
        d.id = r[QLatin1String("id")].toInt();
        d.code = str(r, "code");
        d.name = str(r, "name");
        d.cls = str(r, "class");
        d.description = str(r, "description");
        d.response = str(r, "response");
        d.requirement = str(r, "requirement");
        d.neverSet = r[QLatin1String("never_set")].toBool();
        if (d.id != dtcs.size()) {
            *err = QStringLiteral("dtcs.json: ids are not dense at %1").arg(d.name);
            return false;
        }
        dtcs.push_back(d);
    }
    for (const QJsonValue &m : canFrames[QLatin1String("messages")].toArray()) {
        if (str(m, "name") != QLatin1String("INV_STATUS")) {
            continue;
        }
        for (const QJsonValue &s : m[QLatin1String("signals")].toArray()) {
            const QString n = str(s, "name");
            const QJsonObject values = s[QLatin1String("values")].toObject();
            if (n == QLatin1String("state")) {
                states = enumNames(values);
            } else if (n == QLatin1String("bridge")) {
                bridgeModes = enumNames(values);
            } else if (n == QLatin1String("hv")) {
                hvStates = enumNames(values);
            } else if (n == QLatin1String("evidence_missing")) {
                evidenceBits.clear();
                for (auto it = values.begin(); it != values.end(); ++it) {
                    evidenceBits.push_back({it.key().toInt(), it.value().toString()});
                }
                std::sort(evidenceBits.begin(), evidenceBits.end(),
                          [](const BitName &a, const BitName &b) { return a.bit < b.bit; });
            }
        }
    }
    // PROTOCOL.md A.7 (no export carries these)
    calErrBits = {{0x001, QStringLiteral("MISSING")}, {0x002, QStringLiteral("VERSION")},  {0x004, QStringLiteral("CRC")},
                  {0x008, QStringLiteral("RANGE")},   {0x010, QStringLiteral("SKU")},      {0x020, QStringLiteral("SERIAL")},
                  {0x040, QStringLiteral("MOTOR_ID")}, {0x080, QStringLiteral("FSW")}};
    hvilStates = {QStringLiteral("unknown"), QStringLiteral("closed"), QStringLiteral("open"),
                  QStringLiteral("short to ground"), QStringLiteral("short to supply"), QStringLiteral("implausible")};
    ssRows = {QStringLiteral("CMD_LOST"),    QStringLiteral("BATTERY_LOST"), QStringLiteral("BMS_LIMIT_ZERO"),
              QStringLiteral("RESOLVER_INVALID"), QStringLiteral("FLT_HS"), QStringLiteral("FLT_LS"),
              QStringLiteral("V5GD_LOSS"),   QStringLiteral("OVERVOLTAGE"),  QStringLiteral("OVERCURRENT"),
              QStringLiteral("VDC_INVALID")};
    ssActions = {QStringLiteral("NONE"), QStringLiteral("RAMP_KEEP_CC"), QStringLiteral("RAMP_THEN_SPO"),
                 QStringLiteral("ZERO_CURRENT"), QStringLiteral("SPO"), QStringLiteral("LS_ASC"),
                 QStringLiteral("SPO_THEN_PWM_ASC")};
    if (cal.isEmpty() || dtcs.size() < 2 || states.isEmpty() || fwId.isEmpty() || evidenceBits.isEmpty()) {
        *err = QStringLiteral("%1: incomplete protocol exports").arg(dir);
        return false;
    }
    return true;
}

void ProtocolInfo::applyHello(const QJsonObject &hello)
{
    auto take = [&hello](const char *k, QStringList &into) {
        const QJsonArray a = hello.value(QLatin1String(k)).toArray();
        if (!a.isEmpty()) {
            into.clear();
            for (const QJsonValue &v : a) {
                into << v.toString();
            }
        }
    };
    take("states", states);
    take("ss_rows", ssRows);
    take("ss_actions", ssActions);
    const QJsonArray names = hello.value(QLatin1String("dtcs")).toArray();
    for (int i = 1; i < names.size() && i < dtcs.size(); i++) {
        if (dtcs[i].name != names[i].toString()) {
            dtcs[i].name = names[i].toString(); // the running image's own order wins; its description may not match
            dtcs[i].description = QStringLiteral("(the connected image names this DTC differently from dtcs.json)");
        }
    }
}

const CalRow *ProtocolInfo::calRow(const QString &name) const
{
    for (const CalRow &c : cal) {
        if (c.name == name) {
            return &c;
        }
    }
    return nullptr;
}

const ConstRow *ProtocolInfo::constRow(const QString &name) const
{
    for (const ConstRow &c : constants) {
        if (c.name == name) {
            return &c;
        }
    }
    return nullptr;
}

const DtcInfo *ProtocolInfo::dtc(int id) const { return (id > 0 && id < dtcs.size()) ? &dtcs[id] : nullptr; }

const SkuInfo *ProtocolInfo::sku(int index) const
{
    for (const SkuInfo &s : skus) {
        if (s.index == index) {
            return &s;
        }
    }
    return nullptr;
}

const SkuInfo *ProtocolInfo::skuByKey(const QString &key) const
{
    for (const SkuInfo &s : skus) {
        if (s.key == key) {
            return &s;
        }
    }
    return nullptr;
}

QString ProtocolInfo::stateName(int sm) const
{
    return (sm >= 0 && sm < states.size() && !states[sm].isEmpty()) ? states[sm] : QStringLiteral("state %1").arg(sm);
}

QString ProtocolInfo::dtcName(int id) const
{
    const DtcInfo *d = dtc(id);
    return d ? d->shortName() : QStringLiteral("DTC %1").arg(id);
}

QString ProtocolInfo::bitsText(int value, const QVector<BitName> &names, const QString &none)
{
    QStringList s;
    for (const BitName &b : names) {
        if (value & b.bit) {
            s << b.name;
        }
    }
    return s.isEmpty() ? none : s.join(QStringLiteral(", "));
}

bool ProtocolInfo::evalRule(const QString &expr, const std::function<double(const QString &)> &lookup, bool *ok)
{
    Expr e(expr, lookup);
    return e.parse(ok) != 0.0;
}
