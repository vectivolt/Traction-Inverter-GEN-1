// CommissioningPage — FW-39 motor self-commissioning (contract §10g), the way VESC Tool's motor detection does it but
// through the firmware's interlocked service mode: the preconditions, the operator's rig attestation (typed), one
// routine at a time with the results poll as the heartbeat, stop, the decoded results with their verdicts and the
// staged mask, the commit into a new calibration record version (typed), JSON/CSV export; round 23 the FW-45 saturation
// map sweep (typed, the two tables against the bias currents) and the FW-46 torque-ripple table (a CSV imported, written,
// read back, committed with the rest). The logic is CommissioningSequencer's; this page renders it. No torque is ever
// sent from here.
#pragma once

#include "CommissioningSequencer.h"
#include "Page.h"

class CheckList;
class KvGrid;
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QTableWidget;

class CommissioningPage : public Page
{
    Q_OBJECT
public:
    explicit CommissioningPage(Session &s, QWidget *parent = nullptr);
    void applyTheme() override;
    CommissioningSequencer &sequencer() { return m_seq; }
    bool importRipple(const QString &path); // the CSV made into the table and shown (false: the info line says why)
    const CommissioningSequencer::Ripple &ripple() const { return m_ripple; }

private:
    void renderLive();    // the checklist, the enables, the elapsed time (10 Hz)
    void renderResults(); // the outcome, the table, the history (on every change)
    void renderMap();
    void renderRipple();
    void startRoutine();
    void startMap();
    void commit();
    void exportJson();
    void exportCsv();

    CommissioningSequencer m_seq;
    CheckList *m_checks;
    QPushButton *m_arm, *m_enableOff, *m_vspeed0, *m_start, *m_stop, *m_commit, *m_map, *m_ripImport, *m_ripWrite, *m_ripRead;
    QComboBox *m_routine;
    QLabel *m_attest, *m_pill, *m_outcome, *m_staged;
    KvGrid *m_progress;
    QTableWidget *m_table, *m_mapTable[2], *m_ripTable;
    QLabel *m_mapStaged, *m_ripInfo;
    QListWidget *m_history;
    CommissioningSequencer::Ripple m_ripple;
    QString m_ripFile;
};
