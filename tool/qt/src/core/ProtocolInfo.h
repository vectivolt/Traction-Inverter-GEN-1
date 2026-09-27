// ProtocolInfo — what the tool knows about the firmware, from the machine-readable exports of tool/protocol/
// (the single source of truth, generated from the firmware sources by tool/protocol/generate.mjs and embedded in
// the application at build time as ":/protocol/*.json"):
//   params.json      the writable cal_* rows, the contract constants per SKU, the cross-field rules of
//                    ti_params_validate(), the SKUs and the firmware identity (TI_FW_ID);
//   dtcs.json        every DTC: code, name, class, description, the firmware's response, requirement;
//   can-frames.json  the vehicle CAN-FD/UDS frame layouts (enumeration names) and the codec test vectors.
// The enumerations that tool/PROTOCOL.md §A.7 documents but no export carries (the calibration error bits, HVIL and
// FS26 states, the eFlexPWM mode) are defined in ProtocolInfo.cpp, citing A.7. At run time the bridge's "hello"
// names (states, ss_rows, ss_actions, dtcs) take precedence (PROTOCOL.md A.7: "prefer those at run time").
#pragma once

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

struct CalRow {
    QString name, type, unit, group, description;
    QStringList refs;
    double def = 0, min = 0, max = 0;
    bool isInteger() const { return type != QLatin1String("f32"); }
};

struct ConstRow {
    QString name, unit, group, description;
    bool perSku = false;
    double value = 0;                 // when !perSku
    QHash<QString, double> skuValues; // sku key -> value, when perSku (numeric values only)
    double valueFor(const QString &skuKey) const { return perSku ? skuValues.value(skuKey, qQNaN()) : value; }
};

struct SkuInfo {
    int index = 0; // ti_sku_t value (hello.sku)
    QString key, id, name;
};

struct DtcInfo {
    int id = 0;
    QString name, code, cls, description, response, requirement;
    bool neverSet = false; // declared in dtc.h, raised nowhere in this image
    QString shortName() const { return name.startsWith(QLatin1String("DTC_")) ? name.mid(4) : name; }
};

struct CrossRule {
    QString expr;
    QStringList calFields; // the cal_* rows the rule involves (empty: constants only)
};

struct BitName {
    int bit = 0;
    QString name;
};

class ProtocolInfo
{
public:
    static ProtocolInfo &instance(); // loaded from ":/protocol/" once
    bool load(const QString &dir, QString *err);

    QString fwId;
    QVector<CalRow> cal;
    QVector<ConstRow> constants;
    QVector<SkuInfo> skus;
    QVector<DtcInfo> dtcs; // index = id; [0] = DTC_NONE
    QVector<CrossRule> rules;
    QStringList states, bridgeModes, hvStates, ssRows, ssActions;
    QVector<BitName> evidenceBits, calErrBits;
    QStringList hvilStates;
    QJsonObject canFrames; // the whole can-frames.json (layouts, vectors)

    const CalRow *calRow(const QString &name) const;
    const ConstRow *constRow(const QString &name) const;
    const DtcInfo *dtc(int id) const;
    const SkuInfo *sku(int index) const;
    const SkuInfo *skuByKey(const QString &key) const;
    QString stateName(int sm) const;
    QString dtcName(int id) const;
    static QString bitsText(int value, const QVector<BitName> &names, const QString &none = QStringLiteral("none"));

    // Run-time names from the bridge's hello (states, ss_rows, ss_actions, dtcs).
    void applyHello(const QJsonObject &hello);

    // Evaluates one cross-field rule: identifiers resolve through lookup (NaN = unknown); operators + - * / < <= > >=
    // == != && || and parentheses. *ok is false when the expression cannot be parsed or an identifier is unknown.
    static bool evalRule(const QString &expr, const std::function<double(const QString &)> &lookup, bool *ok);
};
