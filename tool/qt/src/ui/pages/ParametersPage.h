// ParametersPage — the calibration table: grouped and searchable, units, ranges, defaults, the device's values and
// the operator's edits (dirty until written), a diff filter, validation before any write (ranges, types and the
// ti_params_validate() cross-field rules), import/export JSON and the change log.
#pragma once

#include "Page.h"

class QCheckBox;
class QComboBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSortFilterProxyModel;
class QTableView;
class QTextBrowser;

class ParametersPage : public Page
{
    Q_OBJECT
public:
    explicit ParametersPage(Session &s, QWidget *parent = nullptr);

private:
    void updateDetail();
    void updateState();
    void write();
    void importFile();
    void exportFile();
    void refreshLog();

    QLineEdit *m_search;
    QComboBox *m_group;
    QComboBox *m_diff;
    QTableView *m_table;
    QSortFilterProxyModel *m_proxy;
    QPushButton *m_read, *m_write, *m_revert, *m_defaults;
    QLabel *m_status;
    QTextBrowser *m_detail;
    QListWidget *m_problems, *m_log;
};
