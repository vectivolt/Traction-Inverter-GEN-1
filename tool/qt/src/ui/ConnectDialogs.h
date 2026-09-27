// ConnectDialogs — choosing what to talk to: the simulator bridge (path, SKU, telemetry rate, time factor) and a CAN
// adapter (every Qt SerialBus plugin present on this machine, its interfaces, bit rates, CAN FD). Nothing is assumed
// present: an absent plugin or interface is shown as such, never an error at start-up.
#pragma once

#include "BridgeTransport.h"
#include "CanTransport.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QSpinBox;

class SimulatorDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SimulatorDialog(QWidget *parent = nullptr);
    BridgeTransport::Options options() const;

private:
    QLineEdit *m_path;
    QComboBox *m_sku;
    QSpinBox *m_rate;
    QDoubleSpinBox *m_time;
    QCheckBox *m_paused;
    QLabel *m_status;
};

class CanDialog : public QDialog
{
    Q_OBJECT
public:
    explicit CanDialog(QWidget *parent = nullptr);
    CanTransport::Options options() const;
    // Adapter plugin names with whether this machine has them (Qt SerialBus's plugins, plus the adapters people ask
    // for that Qt has no plugin of — listed with how to reach them).
    static QVector<QPair<QString, bool>> plugins();
    static QString hint(const QString &plugin);

private:
    void fillInterfaces();
    QComboBox *m_plugin, *m_iface, *m_bitrate, *m_dataBitrate;
    QCheckBox *m_fd;
    QLabel *m_status;
};
