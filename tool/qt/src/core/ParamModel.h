// ParamModel — the calibration table (tool/protocol/params.json "parameters": the cal_* rows of
// firmware/tools/gen-params.mjs): the device's values, the operator's edits (dirty until written), defaults, ranges,
// units and groups; validation before any write (each row's range and type, plus every params.json
// cross_field_rule — the rules of ti_params_validate() — evaluated on the edited set with the connected SKU's
// constants); import/export as JSON and a change log. Writing is the Session's job (one param_set per dirty row);
// the device answers with its own ti_params_validate() verdict, which is final.
#pragma once

#include "ProtocolInfo.h"

#include <QAbstractTableModel>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QVector>

struct ParamChange {
    QDateTime when;
    QString name;
    double from = 0.0;
    double to = 0.0;
    QString result; // "written", "refused: ...", "imported", "edited", "reverted"
};

class ParamModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column { Name, Value, Device, Default, Min, Max, Unit, Group, ColumnCount };
    // DiffersRole: the value in force differs from the gen-params.mjs default; DeviceDiffersRole: the device's value
    // differs from the default (a device that has been calibrated)
    enum Role { DirtyRole = Qt::UserRole + 1, DiffersRole, DeviceDiffersRole, GroupRole, DescriptionRole, InvalidRole, NameRole };

    explicit ParamModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation o, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;

    // The device's table (bridge "params": [{name, type, min, max, value, default}]). Rows whose range or default
    // differ from this build's export are reported (a bridge built from other firmware sources).
    QStringList setDeviceTable(const QJsonArray &params);
    void clearDevice();
    bool haveDevice() const { return m_haveDevice; }

    // The SKU whose per-SKU constants the cross-field rules use (params.json sku key, e.g. "8xx_sic").
    void setSku(const QString &skuKey) { m_sku = skuKey; }
    QString sku() const { return m_sku; }

    int indexOf(const QString &name) const;
    const CalRow &row(int i) const { return m_rows[i].def; }
    double effective(int i) const; // edited, else the device's, else the default
    double device(int i) const { return m_rows[i].device; }
    bool isDirty(int i) const;
    bool setEdited(const QString &name, double v, const QString &why = QStringLiteral("edited"));
    void revertParam(const QString &name);
    void revertAll();
    void editAllToDefaults();

    // Problems that forbid a write: one line each, empty when the edited set is valid.
    QStringList validate() const;
    // name -> value of every dirty row
    QVector<QPair<QString, double>> pendingWrites() const;
    // The device's verdict on one write.
    void writeResult(const QString &name, bool ok, double deviceValue, const QString &message);

    QJsonObject exportJson(const QString &fwId, const QString &sku) const;
    // Sets the edited values; returns problems (unknown names, out of range values are still imported but invalid).
    QStringList importJson(const QJsonObject &o);

    const QVector<ParamChange> &changeLog() const { return m_log; }
    QJsonArray changeLogJson() const;

Q_SIGNALS:
    void dirtyChanged(int count);

private:
    struct Row {
        CalRow def;
        double device = 0.0;
        bool haveDevice = false;
        double edited = 0.0;
        bool hasEdit = false;
    };
    QString rowProblem(int i, double v) const;
    void logChange(const QString &name, double from, double to, const QString &result);
    int dirtyCount() const;

    QVector<Row> m_rows;
    QString m_sku = QStringLiteral("8xx_sic");
    bool m_haveDevice = false;
    QVector<ParamChange> m_log;
};
