// ControlsPage — the operator's controls: arm/disarm behind the firmware's own preconditions (the bridge's arming
// checklist; on CAN what INV_STATUS proves), torque with a hold-to-apply deadman (re-sent every 100 ms with a
// 300 ms wall-clock hold_ms, PROTOCOL.md A.4.3 — release, focus loss or a stale link drops it), gear, the dyno speed,
// ASC and discharge behind a typed confirmation, fault injection (simulator only), the BMS limits, the vehicle
// signals and the EOL provisioning. On a real CAN bus every transmitting control stays disabled until the service
// key is entered.
#pragma once

#include "Page.h"

#include <QTimer>

class CheckList;
class KvGrid;
class QButtonGroup;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;
class Card;

class ControlsPage : public Page
{
    Q_OBJECT
public:
    explicit ControlsPage(Session &s, QWidget *parent = nullptr);
    // Typed confirmation for a protective action; false when cancelled.
    static bool confirmTyped(QWidget *parent, const QString &title, const QString &text, const QString &word);
    void setTorqueSetpoint(double nm); // tests

private:
    void onFrame(const TelemetryFrame &f);
    void updateEnables();
    void startDeadman();
    void stopDeadman();
    void sendTorque(bool withHold);
    void inject(const QString &fault, bool on);
    double torqueMax() const;

    CheckList *m_checks;
    QLabel *m_armNote, *m_lockBanner;
    QPushButton *m_arm, *m_disarm;
    QSlider *m_tqSlider;
    QDoubleSpinBox *m_tq, *m_slew;
    QPushButton *m_deadman, *m_zero;
    QButtonGroup *m_gear;
    KvGrid *m_tqLive;
    QSpinBox *m_rpm, *m_ramp;
    QPushButton *m_speedApply;
    QPushButton *m_ascOn, *m_ascOff, *m_discharge;
    QVector<QPushButton *> m_injectButtons;
    QVector<QPushButton *> m_simButtons;  // simulator-only controls
    QVector<QWidget *> m_driveWidgets;    // need a live, commandable device
    QVector<QWidget *> m_canWidgets;
    QDoubleSpinBox *m_dt, *m_chg, *m_dis, *m_pack, *m_coolant;
    QCheckBox *m_prov[4];
    QPushButton *m_vcuOn;
    QComboBox *m_contactors;
    Card *m_simCard, *m_canCard, *m_injCard;
    QTimer m_deadmanTimer;
    bool m_holding = false;
    QString m_injected;
};
