// FaultsPage — active DTCs with their description and the firmware's response (tool/protocol/dtcs.json), the fault
// history on a timeline, user alert rules on any telemetry channel (notification + log), and the one-file bug
// report (state, the last N seconds of telemetry, parameters, firmware identity, DTCs, alerts, events).
#pragma once

#include "Page.h"

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTableWidget;
class QTextBrowser;
class Timeline;

class FaultsPage : public Page
{
    Q_OBJECT
public:
    explicit FaultsPage(Session &s, QWidget *parent = nullptr);
    void saveBugReport(); // asks for a note and a file
    QTableWidget *dtcTable() const { return m_dtcs; }

private:
    void rebuildDtcs();
    void showDtc();
    void rebuildRules();
    void addRule();
    void refreshTimeline();
    void addAlertLog(const AlertEvent &e);

    QTableWidget *m_dtcs;
    QTextBrowser *m_dtcDetail;
    QLabel *m_dtcSummary;
    QPushButton *m_clear;
    QString m_signature;
    Timeline *m_timeline;
    QTableWidget *m_rules;
    QComboBox *m_ruleChannel, *m_ruleOp, *m_ruleSev;
    QDoubleSpinBox *m_ruleThr, *m_ruleHyst, *m_ruleHold;
    QLineEdit *m_ruleNote;
    QListWidget *m_alertLog;
};
